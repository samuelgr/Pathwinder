/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file FilesystemExecutor.cpp
 *   Implementation of functions used to execute filesystem operations under control of
 *   filesystem instructions.
 **************************************************************************************************/

#pragma once

#include "FilesystemExecutor.h"

#include <cstdint>
#include <functional>
#include <mutex>

#include "ApiWindows.h"
#include "ArrayList.h"
#include "FileInformationStruct.h"
#include "FilesystemOperations.h"
#include "Globals.h"
#include "Message.h"
#include "MutexWrapper.h"
#include "OpenHandleStore.h"
#include "Strings.h"
#include "TemporaryBuffer.h"
#include "ValueOrError.h"

namespace Pathwinder
{
  namespace FilesystemExecutor
  {
    /// Enumerates the possible modes for I/O using a file handle.
    enum class EInputOutputMode : uint8_t
    {
      /// I/O mode is not known. This represents an error case.
      Unknown,

      /// I/O is asynchronous. System calls will return immediately, and completion information is
      /// provided out-of-band.
      Asynchronous,

      /// I/O is synchronous. System calls will return only after the requested operation
      /// completes.
      Synchronous,

      /// Not used as a value. Identifies the number of enumerators present in this enumeration.
      Count
    };

    /// Contains all of the information associated with a file operation.
    struct SFileOperationContext
    {
      /// How the redirection should be performed.
      FileOperationInstruction instruction;

      /// If an input path was composed, for example due to combination with a root directory,
      /// then that input path is stored here.
      std::optional<TemporaryString> composedInputPath;
    };

    /// Holds all of the information needed to represent a create disposition that should be
    /// attempted.
    struct SCreateDispositionToTry
    {
      /// Enumerates possible conditions on whether or not the create disposition should be
      /// attempted.
      enum class ECondition : ULONG
      {
        /// Unconditionally attempt the file operation using the supplied create disposition.
        Unconditional,

        /// Attempt the file operation using the supplied create disposition only if the file
        /// exists.
        FileMustExist,

        /// Attempt the file operation using the supplied create disposition only if the file
        /// does not exist.
        FileMustNotExist
      };

      /// Condition on whether the supplied create disposition should be attempted or skipped.
      ECondition condition : 8;

      /// Create disposition parameter to provide to the underlying system call.
      ULONG ntParamCreateDisposition : 24;
    };

    /// Contains all of the information needed to represent a file name and attributes in the format
    /// needed to interact with underlying system calls.
    struct SObjectNameAndAttributes
    {
      /// Name of the object, as a Unicode string in the format supported by system calls.
      UNICODE_STRING objectName;

      /// Attributes of the object, which includes a field that points to the name.
      OBJECT_ATTRIBUTES objectAttributes;
    };

    /// Holds multiple create dispositions that should be tried in order when attempting file
    /// operations. Each element is either a create disposition that should be attempted or a forced
    /// result code, in which case the file operation should not be attempted but rather assumed to
    /// have the forced result.
    using TCreateDispositionsList = ArrayList<ValueOrError<SCreateDispositionToTry, NTSTATUS>, 2>;

    /// Holds multiple file operations to attempt in a small list, ordered by priority.
    /// Each element is either a single file operation that should be submitted to the system or a
    /// forced result code, in which case submitting to the system is skipped and assumed to have
    /// the forced result.
    /// @tparam FileObjectType Data structure type that identifies files to try.
    template <typename FileObjectType> using TFileOperationsList =
        ArrayList<ValueOrError<FileObjectType*, NTSTATUS>, 2>;

    /// Dumps to the log all relevant invocation parameters for a file operation that results in the
    /// creation of a new file handle. Parameters are a subset of those that would normally be
    /// passed to `NtCreateFile`, with the exception of some additional metadata about the invoked
    /// system call function for logging purposes.
    static void DumpNewFileHandleParameters(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        POBJECT_ATTRIBUTES objectAttributes,
        ACCESS_MASK desiredAccess,
        ULONG shareAccess,
        ULONG createDisposition,
        ULONG createOptions)
    {
      constexpr Message::ESeverity kDumpSeverity = Message::ESeverity::SuperDebug;

      // There is overhead involved with producing a dump of parameter values.
      // This is why it is helpful to guard the block on whether or not the output would actually
      // be logged.
      if (true == Message::WillOutputMessageOfSeverity(kDumpSeverity))
      {
        const std::wstring_view functionNameView = std::wstring_view(functionName);
        const std::wstring_view objectNameParam =
            Strings::NtConvertUnicodeStringToStringView(*objectAttributes->ObjectName);

        // Ensures that multiple functions invoked at the same time do not overlap when dumping
        // their parameters to the log file. This is just a cosmetic readability issue. Furthermore,
        // using a mutex does not prevent other log messages unrelated to dumping parameters from
        // being interleaved.
        static Mutex paramPrintMutex;
        std::unique_lock paramPrintLock(paramPrintMutex);

        Message::OutputFormatted(
            kDumpSeverity,
            L"%s(%u): Invoked with these parameters:",
            functionName,
            functionRequestIdentifier);
        Message::OutputFormatted(
            kDumpSeverity,
            L"%s(%u):   ObjectName = \"%.*s\"",
            functionName,
            functionRequestIdentifier,
            static_cast<int>(objectNameParam.length()),
            objectNameParam.data());
        Message::OutputFormatted(
            kDumpSeverity,
            L"%s(%u):   RootDirectory = %zu",
            functionName,
            functionRequestIdentifier,
            reinterpret_cast<size_t>(objectAttributes->RootDirectory));
        Message::OutputFormatted(
            kDumpSeverity,
            L"%s(%u):   DesiredAccess = %s",
            functionName,
            functionRequestIdentifier,
            Strings::NtAccessMaskToString(desiredAccess).AsCString());
        Message::OutputFormatted(
            kDumpSeverity,
            L"%s(%u):   ShareAccess = %s",
            functionName,
            functionRequestIdentifier,
            Strings::NtShareAccessToString(shareAccess).AsCString());

        if (functionNameView.contains(L"Create"))
        {
          Message::OutputFormatted(
              kDumpSeverity,
              L"%s(%u):   CreateDisposition = %s",
              functionName,
              functionRequestIdentifier,
              Strings::NtCreateDispositionToString(createDisposition).AsCString());
          Message::OutputFormatted(
              kDumpSeverity,
              L"%s(%u):   CreateOptions = %s",
              functionName,
              functionRequestIdentifier,
              Strings::NtCreateOrOpenOptionsToString(createOptions).AsCString());
        }
        else if (functionNameView.contains(L"Open"))
        {
          Message::OutputFormatted(
              kDumpSeverity,
              L"%s(%u):   OpenOptions = %s",
              functionName,
              functionRequestIdentifier,
              Strings::NtCreateOrOpenOptionsToString(createOptions).AsCString());
        }
      }
    }

    /// Advances an in-progress directory enumeration operation by copying file information
    /// structures to an application-supplied buffer. Most parameters come directly from
    /// `NtQueryDirectoryFileEx` but those that do not are documented.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] enumerationState Enumeration state data structure, which must be mutable so it
    /// can be updated as the enumeration proceeds.
    /// @param [in] isFirstInvocation Whether or not to enable special behavior for the first
    /// invocation of a directory enumeration function, as specified by `NtQueryDirectoryFileEx`
    /// documentation.
    /// @return Windows error code corresponding to the result of advancing the directory
    /// enumeration operation.
    static NTSTATUS AdvanceDirectoryEnumerationOperation(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore::SInProgressDirectoryEnumeration& enumerationState,
        bool isFirstInvocation,
        PIO_STATUS_BLOCK ioStatusBlock,
        PVOID outputBuffer,
        ULONG outputBufferSizeBytes,
        ULONG queryFlags,
        std::wstring_view queryFilePattern)
    {
      DebugAssert(
          nullptr != enumerationState.queue,
          "Advancing directory enumeration state without an operation queue.");

      if (queryFlags & SL_RESTART_SCAN)
      {
        enumerationState.queue->Restart(queryFilePattern);
        enumerationState.enumeratedFilenames.clear();
        isFirstInvocation = true;
      }

      // The `Information` field of the output I/O status block records the total number of bytes
      // written.
      ioStatusBlock->Information = 0;

      // This block will cause `STATUS_NO_MORE_FILES` to be returned if the queue is empty and
      // enumeration is complete. Getting past here means the queue is not empty and more files
      // can be enumerated.
      NTSTATUS enumerationStatus = enumerationState.queue->EnumerationStatus();
      if (!(NT_SUCCESS(enumerationStatus)))
      {
        // If the first invocation has resulted in no files available for enumeration then that
        // should be the result. This would only happen if a query file pattern is specified and
        // it matches no files. Otherwise, a directory enumeration would at very least include
        // "." and ".." entries.
        if ((true == isFirstInvocation) && NtStatus::kNoMoreFiles == enumerationStatus)
          return NtStatus::kNoSuchFile;

        return enumerationStatus;
      }

      // Some extra checks are required if this is the first invocation. This is to handle partial
      // writes when the buffer is too small to hold a single complete structure. On subsequent
      // calls these checks are omitted, per `NtQueryDirectoryFileEx` documentation.
      if (true == isFirstInvocation)
      {
        if (outputBufferSizeBytes <
            enumerationState.fileInformationStructLayout.BaseStructureSize())
          return NtStatus::kBufferTooSmall;

        if (outputBufferSizeBytes < enumerationState.queue->SizeOfFront())
        {
          ioStatusBlock->Information = static_cast<ULONG_PTR>(
              enumerationState.queue->CopyFront(outputBuffer, outputBufferSizeBytes));
          enumerationState.fileInformationStructLayout.ClearNextEntryOffset(outputBuffer);
          return NtStatus::kBufferOverflow;
        }
      }

      const unsigned int maxElementsToWrite =
          ((queryFlags & SL_RETURN_SINGLE_ENTRY) ? 1 : std::numeric_limits<unsigned int>::max());
      unsigned int numElementsWritten = 0;
      unsigned int numBytesWritten = 0;
      void* lastBufferPosition = nullptr;

      // At this point only full structures will be written, and it is safe to assume there is at
      // least one file information structure left in the queue.
      while ((NT_SUCCESS(enumerationStatus)) && (numElementsWritten < maxElementsToWrite))
      {
        void* const bufferPosition = reinterpret_cast<void*>(
            reinterpret_cast<size_t>(outputBuffer) + static_cast<size_t>(numBytesWritten));
        const unsigned int bufferCapacityLeftBytes = outputBufferSizeBytes - numBytesWritten;

        if (bufferCapacityLeftBytes < enumerationState.queue->SizeOfFront()) break;

        // If this is the first invocation, or just freshly-restarted, then no enumerated
        // filenames have already been seen. Otherwise the queue will have been pre-advanced to
        // the first unique filename. For these reasons it is correct to copy first and advance
        // the queue after.
        numBytesWritten +=
            enumerationState.queue->CopyFront(bufferPosition, bufferCapacityLeftBytes);
        numElementsWritten += 1;

        // There are a few reasons why the next entry offset field might not be correct.
        // If the file information structure is received from the system, then the value might
        // be 0 to indicate no more files from the system, but that might not be correct to
        // communicate to the application. Sometimes the system also adds padding which can be
        // removed. For these reasons it is necessary to update the next entry offset here and
        // track the last written file information structure so that its next entry offset can
        // be cleared after the loop.
        enumerationState.fileInformationStructLayout.UpdateNextEntryOffset(bufferPosition);
        lastBufferPosition = bufferPosition;

        enumerationState.enumeratedFilenames.emplace(
            std::wstring(enumerationState.queue->FileNameOfFront()));
        enumerationState.queue->PopFront();

        // Enumeration status must be checked first because, if there are no file information
        // structures left in the queue, checking the front element's filename will cause a
        // crash.
        while ((NT_SUCCESS(enumerationState.queue->EnumerationStatus())) &&
               (enumerationState.enumeratedFilenames.contains(
                   enumerationState.queue->FileNameOfFront())))
          enumerationState.queue->PopFront();

        enumerationStatus = enumerationState.queue->EnumerationStatus();
      }

      if (nullptr != lastBufferPosition)
        enumerationState.fileInformationStructLayout.ClearNextEntryOffset(lastBufferPosition);

      ioStatusBlock->Information = numBytesWritten;

      // Whether or not the queue still has any file information structures is not relevant.
      // Coming into this function call there was at least one such structure available.
      // Even if it was not actually copied to the application buffer, and hence 0 bytes were
      // copied, this is still considered success per `NtQueryDirectoryFileEx` documentation.
      switch (enumerationStatus)
      {
        case NtStatus::kMoreEntries:
        case NtStatus::kNoMoreFiles:
          enumerationStatus = NtStatus::kSuccess;
          break;

        default:
          break;
      }

      return enumerationStatus;
    }

    /// Converts a `CreateDisposition` parameter, which system calls use to identify filesystem
    /// behavior regarding creating new files or opening existing files, into an appropriate
    /// internal create disposition object.
    /// @param [in] ntCreateDisposition `CreateDisposition` parameter received from the application.
    /// @return Corresponding create disposition object.
    static CreateDisposition CreateDispositionFromNtParameter(ULONG ntCreateDisposition)
    {
      switch (ntCreateDisposition)
      {
        case FILE_CREATE:
          return CreateDisposition::CreateNewFile();

        case FILE_SUPERSEDE:
        case FILE_OPEN_IF:
        case FILE_OVERWRITE_IF:
          return CreateDisposition::CreateNewOrOpenExistingFile();

        case FILE_OPEN:
        case FILE_OVERWRITE:
          return CreateDisposition::OpenExistingFile();

        default:
          return CreateDisposition::OpenExistingFile();
      }
    }

    /// Executes any pre-operations needed ahead of invoking underlying system calls.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation,
    /// including identifying any pre-operations needed.
    /// @return Result of executing the pre-operations. The code will indicate success if they all
    /// succeed or a failure that corresponds to the first applicable pre-operation failure.
    static NTSTATUS ExecuteExtraPreOperations(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        const FileOperationInstruction& instruction)
    {
      NTSTATUS extraPreOperationResult = NtStatus::kSuccess;

      if (instruction.GetExtraPreOperations().contains(
              static_cast<int>(EExtraPreOperation::EnsurePathHierarchyExists)) &&
          (NT_SUCCESS(extraPreOperationResult)))
      {
        Message::OutputFormatted(
            Message::ESeverity::Debug,
            L"%s(%u): Ensuring directory hierarchy exists for \"%.*s\".",
            functionName,
            functionRequestIdentifier,
            static_cast<int>(instruction.GetExtraPreOperationOperand().length()),
            instruction.GetExtraPreOperationOperand().data());
        extraPreOperationResult =
            static_cast<NTSTATUS>(FilesystemOperations::CreateDirectoryHierarchy(
                instruction.GetExtraPreOperationOperand()));
      }

      if (!(NT_SUCCESS(extraPreOperationResult)))
        Message::OutputFormatted(
            Message::ESeverity::Error,
            L"%s(%u): A required pre-operation failed (NTSTATUS = 0x%08x).",
            functionName,
            functionRequestIdentifier,
            static_cast<unsigned int>(extraPreOperationResult));

      return extraPreOperationResult;
    }

    /// Converts a `DesiredAccess` parameter, which system calls use to identify the type of access
    /// requested to a file, into an appropriate internal file access mode object.
    /// @param [in] ntDesiredAccess `DesiredAccess` parameter received from the application.
    /// @return Corresponding file access mode object.
    static FileAccessMode FileAccessModeFromNtParameter(ACCESS_MASK ntDesiredAccess)
    {
      constexpr ACCESS_MASK kReadAccessMask =
          (GENERIC_READ | FILE_READ_DATA | FILE_READ_ATTRIBUTES | FILE_READ_EA | READ_CONTROL |
           FILE_EXECUTE | FILE_LIST_DIRECTORY | FILE_TRAVERSE);
      constexpr ACCESS_MASK kWriteAccessMask =
          (GENERIC_WRITE | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA |
           FILE_APPEND_DATA | WRITE_DAC | WRITE_OWNER | FILE_DELETE_CHILD);
      constexpr ACCESS_MASK kDeleteAccessMask = (DELETE);

      const bool canRead = (0 != (ntDesiredAccess & kReadAccessMask));
      const bool canWrite = (0 != (ntDesiredAccess & kWriteAccessMask));
      const bool canDelete = (0 != (ntDesiredAccess & kDeleteAccessMask));

      return FileAccessMode(canRead, canWrite, canDelete);
    }

    /// Fills the supplied object name and attributes structure with the name and attributes needed
    /// to represent the redirected filename from a file operation redirection instruction. Does
    /// nothing if the file operation redirection instruction does not specify any redirection. This
    /// must be done in place because the `OBJECT_ATTRIBUTES` structure refers to its `ObjectName`
    /// field by pointer. Returning by value would invalidate the address of the `ObjectName` field
    /// and therefore not work.
    /// @param [out] redirectedObjectNameAndAttributes Mutable reference to the structure to be
    /// filled.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] unredirectedObjectAttributes Object attributes structure received from the
    /// application.
    static void FillRedirectedObjectNameAndAttributesForInstruction(
        SObjectNameAndAttributes& redirectedObjectNameAndAttributes,
        const FileOperationInstruction& instruction,
        const OBJECT_ATTRIBUTES& unredirectedObjectAttributes)
    {
      if (true == instruction.HasRedirectedFilename())
      {
        redirectedObjectNameAndAttributes.objectName =
            Strings::NtConvertStringViewToUnicodeString(instruction.GetRedirectedFilename());

        redirectedObjectNameAndAttributes.objectAttributes = unredirectedObjectAttributes;
        redirectedObjectNameAndAttributes.objectAttributes.RootDirectory = nullptr;
        redirectedObjectNameAndAttributes.objectAttributes.ObjectName =
            &redirectedObjectNameAndAttributes.objectName;
      }
    }

    /// Determines how to redirect an individual file operation in which the affected file is
    /// identified by an object attributes structure.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] rootDirectory Open handle for the root directory that contains the input
    /// filename. May be `nullptr`, in which case the input filename must be a full and absolute
    /// path. Supplied by an application that invokes a system call.
    /// @param [in] inputFilename Filename received from the application that invoked the system
    /// call. Must be a full and absolute path if the root directory handle is not provided.
    /// @param [in] fileAccessMode Type of access or accesses to be performed on the file.
    /// @param [in] createDisposition Create disposition for the requsted file operation, which
    /// specifies whether a new file should be created, an existing file opened, or either.
    /// @param [in] instructionSourceFunc Function to be invoked that will retrieve a file operation
    /// instruction, given a source path, file access mode, and create disposition.
    /// @return Context that contains all of the information needed to submit the file operation to
    /// the underlying system call.
    static SFileOperationContext GetFileOperationRedirectionInformation(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE rootDirectory,
        std::wstring_view inputFilename,
        FileAccessMode fileAccessMode,
        CreateDisposition createDisposition,
        std::function<FileOperationInstruction(
            std::wstring_view, FileAccessMode, CreateDisposition)> instructionSourceFunc)
    {
      std::optional<TemporaryString> maybeRedirectedFilename = std::nullopt;
      std::optional<OpenHandleStore::SHandleDataView> maybeRootDirectoryHandleData =
          ((nullptr == rootDirectory) ? std::nullopt
                                      : openHandleStore.GetDataForHandle(rootDirectory));

      if (true == maybeRootDirectoryHandleData.has_value())
      {
        // Input object attributes structure specifies an open directory handle as the root
        // directory and the handle was found in the cache. Before querying for redirection it
        // is necessary to assemble the full filename, including the root directory path.

        std::wstring_view rootDirectoryHandlePath = maybeRootDirectoryHandleData->associatedPath;

        TemporaryString inputFullFilename;
        inputFullFilename << rootDirectoryHandlePath << L'\\' << inputFilename;

        FileOperationInstruction redirectionInstruction =
            instructionSourceFunc(inputFullFilename, fileAccessMode, createDisposition);
        if (true == redirectionInstruction.HasRedirectedFilename())
          Message::OutputFormatted(
              Message::ESeverity::Debug,
              L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined and redirected to \"%.*s\".",
              functionName,
              functionRequestIdentifier,
              static_cast<int>(rootDirectoryHandlePath.length()),
              rootDirectoryHandlePath.data(),
              reinterpret_cast<size_t>(rootDirectory),
              static_cast<int>(inputFilename.length()),
              inputFilename.data(),
              static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()),
              redirectionInstruction.GetRedirectedFilename().data());
        else
          Message::OutputFormatted(
              Message::ESeverity::SuperDebug,
              L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined but not redirected.",
              functionName,
              functionRequestIdentifier,
              static_cast<int>(rootDirectoryHandlePath.length()),
              rootDirectoryHandlePath.data(),
              reinterpret_cast<size_t>(rootDirectory),
              static_cast<int>(inputFilename.length()),
              inputFilename.data());

        return {
            .instruction = std::move(redirectionInstruction),
            .composedInputPath = std::move(inputFullFilename)};
      }
      else if (nullptr == rootDirectory)
      {
        // Input object attributes structure does not specify an open directory handle as the
        // root directory. It is sufficient to send the object name directly for redirection.

        FileOperationInstruction redirectionInstruction =
            instructionSourceFunc(inputFilename, fileAccessMode, createDisposition);

        if (true == redirectionInstruction.HasRedirectedFilename())
        {
          Message::OutputFormatted(
              Message::ESeverity::Debug,
              L"%s(%u): Invoked with path \"%.*s\" which was redirected to \"%.*s\".",
              functionName,
              functionRequestIdentifier,
              static_cast<int>(inputFilename.length()),
              inputFilename.data(),
              static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()),
              redirectionInstruction.GetRedirectedFilename().data());
        }
        else
        {
          Message::OutputFormatted(
              Message::ESeverity::SuperDebug,
              L"%s(%u): Invoked with path \"%.*s\" which was not redirected.",
              functionName,
              functionRequestIdentifier,
              static_cast<int>(inputFilename.length()),
              inputFilename.data());
        }

        return {
            .instruction = std::move(redirectionInstruction), .composedInputPath = std::nullopt};
      }
      else
      {
        // Input object attributes structure specifies an open directory handle as the root
        // directory but the handle is not in cache. When the root directory handle was
        // originally opened it was determined that there is no possible match with a filesystem
        // rule. Therefore, it is not necessary to attempt redirection.

        Message::OutputFormatted(
            Message::ESeverity::SuperDebug,
            L"%s(%u): Invoked with root directory handle %zu and relative path \"%.*s\" for which no redirection was attempted.",
            functionName,
            functionRequestIdentifier,
            reinterpret_cast<size_t>(rootDirectory),
            static_cast<int>(inputFilename.length()),
            inputFilename.data());
        return {
            .instruction = FileOperationInstruction::NoRedirectionOrInterception(),
            .composedInputPath = std::nullopt};
      }
    }

    /// Determines the input/output mode for the specified file handle.
    /// @param [in] handle Filesystem object handle to check.
    /// @return Input/output mode for the handle, or #EInputOutputMode::Unknown in the event of an
    /// error.
    static EInputOutputMode GetInputOutputModeForHandle(HANDLE handle)
    {
      auto handleModeInformation = FilesystemOperations::QueryFileHandleMode(handle);
      if (handleModeInformation.HasError()) return EInputOutputMode::Unknown;

      switch (handleModeInformation.Value() &
              (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT))
      {
        case 0:
          return EInputOutputMode::Asynchronous;
        default:
          return EInputOutputMode::Synchronous;
      }
    }

    /// Identifies the create dispositions to try, in order, when determining which file operations
    /// to submit to the underlying system call. Uses the preference indicated in the file operation
    /// instruction, along with the supplied create disposition parameter, to determine which create
    /// dispositions need to be attempted.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] ntParamCreateDisposition Creation disposition options received from the
    /// application.
    /// @return List of create dispositions to be tried, in order.
    static TCreateDispositionsList SelectCreateDispositionsToTry(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        const FileOperationInstruction& instruction,
        ULONG ntParamCreateDisposition)
    {
      TCreateDispositionsList createDispositionsList;

      switch (instruction.GetCreateDispositionPreference())
      {
        case ECreateDispositionPreference::NoPreference:
          createDispositionsList.PushBack(SCreateDispositionToTry{
              .condition = SCreateDispositionToTry::ECondition::Unconditional,
              .ntParamCreateDisposition = ntParamCreateDisposition});
          break;

        case ECreateDispositionPreference::PreferCreateNewFile:
          switch (ntParamCreateDisposition)
          {
            case FILE_OPEN_IF:
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = FILE_CREATE});
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = FILE_OPEN});
              break;

            case FILE_OVERWRITE_IF:
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = FILE_CREATE});
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = FILE_OVERWRITE});
              break;

            case FILE_SUPERSEDE:
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = FILE_CREATE});
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = FILE_SUPERSEDE});
              break;

            default:
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = ntParamCreateDisposition});
              break;
          }
          break;

        case ECreateDispositionPreference::PreferOpenExistingFile:
          switch (ntParamCreateDisposition)
          {
            case FILE_OPEN_IF:
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = FILE_OPEN});
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = FILE_CREATE});
              break;

            case FILE_OVERWRITE_IF:
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = FILE_OVERWRITE});
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = FILE_CREATE});
              break;

            case FILE_SUPERSEDE:
              // It may seem unnecessary to add two `FILE_SUPERSEDE` entries, one conditional and
              // one unconditional, but this is important for how files and create dispositions are
              // ordered. For each create disposition, each file to try is tried in sequence before
              // moving to the next create disposition. Therefore, this ordering ensures that
              // whichever file already exists is superseded before allowing non-existent files to
              // be opened for supersede. That is how the preference for opening an existing file is
              // implemented.
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::FileMustExist,
                  .ntParamCreateDisposition = FILE_SUPERSEDE});
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = FILE_SUPERSEDE});
              break;

            default:
              createDispositionsList.PushBack(SCreateDispositionToTry{
                  .condition = SCreateDispositionToTry::ECondition::Unconditional,
                  .ntParamCreateDisposition = ntParamCreateDisposition});
              break;
          }
          break;

        default:
          Message::OutputFormatted(
              Message::ESeverity::Error,
              L"%s(%u): Internal error: unrecognized file operation instruction (ECreateDispositionPreference = %u).",
              functionName,
              functionRequestIdentifier,
              static_cast<unsigned int>(instruction.GetCreateDispositionPreference()));
          createDispositionsList.PushBack(NtStatus::kInternalError);
      }

      return createDispositionsList;
    }

    /// Identifies the file operations to try, in order, to submit to the underlying system call for
    /// a file operation. The number of file operations placed, and the order in which they are
    /// placed, is controlled by the file operation redirection instruction. Any entries placed with
    /// file object `nullptr` are invalid and should be skipped. Likewise, any entries that are
    /// error codes should use that error code as the forced result of the attempt, instead of
    /// submitting the operation to the system.
    /// @tparam FileObjectType Data structure type that identifies files to try.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] unredirectedFileObject Pointer to the data structure received from the
    /// application.
    /// @param [in] redirectedFileObject Pointer to the data structure generated by querying for
    /// file operation redirection.
    /// @return List of data structures that identify the files to be tried, in order.
    template <typename FileObjectType> static TFileOperationsList<FileObjectType>
        SelectFileOperationsToTry(
            const wchar_t* functionName,
            unsigned int functionRequestIdentifier,
            const FileOperationInstruction& instruction,
            FileObjectType& unredirectedFileObject,
            FileObjectType& redirectedFileObject)
    {
      TFileOperationsList<FileObjectType> fileOperationsList;

      switch (instruction.GetFilenamesToTry())
      {
        case ETryFiles::UnredirectedOnly:
          fileOperationsList.PushBack(&unredirectedFileObject);
          break;

        case ETryFiles::UnredirectedFirst:
          fileOperationsList.PushBack(&unredirectedFileObject);
          fileOperationsList.PushBack(&redirectedFileObject);
          break;

        case ETryFiles::RedirectedFirst:
          fileOperationsList.PushBack(&redirectedFileObject);
          fileOperationsList.PushBack(&unredirectedFileObject);
          break;

        case ETryFiles::RedirectedOnly:
          fileOperationsList.PushBack(&redirectedFileObject);
          break;

        default:
          Message::OutputFormatted(
              Message::ESeverity::Error,
              L"%s(%u): Internal error: unrecognized file operation instruction (ETryFiles = %u).",
              functionName,
              functionRequestIdentifier,
              static_cast<unsigned int>(instruction.GetFilenamesToTry()));
          fileOperationsList.PushBack(NtStatus::kInternalError);
      }

      return fileOperationsList;
    }

    /// Determines if the next possible filename should be tried or if the existing system call
    /// result should be returned to the application.
    /// @param [in] systemCallResult Result of the system call for the present attempt.
    /// @return `true` if the result indicates that the next filename should be tried, `false` if
    /// the result indicates to stop trying and move on.
    static bool ShouldTryNextFilename(NTSTATUS systemCallResult)
    {
      // If the error code is related to a file not being found then it is safe to try the next
      // file. All other codes, including I/O errors, permission issues, or even success, should
      // be passed to the application.
      switch (systemCallResult)
      {
        case NtStatus::kObjectNameInvalid:
        case NtStatus::kObjectNameNotFound:
        case NtStatus::kObjectPathInvalid:
        case NtStatus::kObjectPathNotFound:
          return true;

        default:
          return false;
      }
    }

    /// Inserts a newly-opened handle into the open handle store, selecting an associated path based
    /// on the file operation redirection instruction.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] newlyOpenedHandle Handle to add to the open handles store.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] successfulPath Path that was used successfully to create the file handle.
    /// @param [in] unredirectedPath Original file name supplied by the application.
    static void SelectFilenameAndStoreNewlyOpenedHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE newlyOpenedHandle,
        const FileOperationInstruction& instruction,
        std::wstring_view successfulPath,
        std::wstring_view unredirectedPath)
    {
      std::wstring_view selectedPath;

      switch (instruction.GetFilenameHandleAssociation())
      {
        case EAssociateNameWithHandle::None:
          break;

        case EAssociateNameWithHandle::WhicheverWasSuccessful:
          selectedPath = successfulPath;
          break;

        case EAssociateNameWithHandle::Unredirected:
          selectedPath = unredirectedPath;
          break;

        case EAssociateNameWithHandle::Redirected:
          selectedPath = instruction.GetRedirectedFilename();
          break;

        default:
          Message::OutputFormatted(
              Message::ESeverity::Error,
              L"%s(%u): Internal error: unrecognized file operation instruction (EAssociateNameWithHandle = %u).",
              functionName,
              functionRequestIdentifier,
              static_cast<unsigned int>(instruction.GetFilenameHandleAssociation()));
          break;
      }

      if (false == selectedPath.empty())
      {
        successfulPath = Strings::RemoveTrailing(successfulPath, L'\\');
        selectedPath = Strings::RemoveTrailing(selectedPath, L'\\');

        openHandleStore.InsertHandle(
            newlyOpenedHandle, std::wstring(selectedPath), std::wstring(successfulPath));
        Message::OutputFormatted(
            Message::ESeverity::Debug,
            L"%s(%u): Handle %zu was opened for path \"%.*s\" and stored in association with path \"%.*s\".",
            functionName,
            functionRequestIdentifier,
            reinterpret_cast<size_t>(newlyOpenedHandle),
            static_cast<int>(successfulPath.length()),
            successfulPath.data(),
            static_cast<int>(selectedPath.length()),
            selectedPath.data());
      }
    }

    /// Updates a handle that might already be in the open handle store, selecting an associated
    /// path based on the file operation redirection instruction.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] handleToUpdate Handle to update in the open handles store, if it is present.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] successfulPath Path that was used successfully to create the file handle.
    /// @param [in] unredirectedPath Original file name supplied by the application.
    static void SelectFilenameAndUpdateOpenHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE handleToUpdate,
        const FileOperationInstruction& instruction,
        std::wstring_view successfulPath,
        std::wstring_view unredirectedPath)
    {
      std::wstring_view selectedPath;

      switch (instruction.GetFilenameHandleAssociation())
      {
        case EAssociateNameWithHandle::None:
        {
          OpenHandleStore::SHandleData erasedHandleData;
          if (true == openHandleStore.RemoveHandle(handleToUpdate, &erasedHandleData))
            Message::OutputFormatted(
                Message::ESeverity::Debug,
                L"%s(%u): Handle %zu associated with path \"%s\" was erased from storage.",
                functionName,
                functionRequestIdentifier,
                reinterpret_cast<size_t>(handleToUpdate),
                erasedHandleData.associatedPath.c_str());
          break;
        }

        case EAssociateNameWithHandle::WhicheverWasSuccessful:
          selectedPath = successfulPath;
          break;

        case EAssociateNameWithHandle::Unredirected:
          selectedPath = unredirectedPath;
          break;

        case EAssociateNameWithHandle::Redirected:
          selectedPath = instruction.GetRedirectedFilename();
          break;

        default:
          Message::OutputFormatted(
              Message::ESeverity::Error,
              L"%s(%u): Internal error: unrecognized file operation instruction (EAssociateNameWithHandle = %u).",
              functionName,
              functionRequestIdentifier,
              static_cast<unsigned int>(instruction.GetFilenameHandleAssociation()));
          break;
      }

      if (false == selectedPath.empty())
      {
        successfulPath = Strings::RemoveTrailing(successfulPath, L'\\');
        selectedPath = Strings::RemoveTrailing(selectedPath, L'\\');

        openHandleStore.InsertOrUpdateHandle(
            handleToUpdate, std::wstring(selectedPath), std::wstring(successfulPath));
        Message::OutputFormatted(
            Message::ESeverity::Debug,
            L"%s(%u): Handle %zu was updated in storage to be opened with path \"%.*s\" and associated with path \"%.*s\".",
            functionName,
            functionRequestIdentifier,
            reinterpret_cast<size_t>(handleToUpdate),
            static_cast<int>(successfulPath.length()),
            successfulPath.data(),
            static_cast<int>(selectedPath.length()),
            selectedPath.data());
      }
    }

    NTSTATUS CloseHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE handle,
        std::function<NTSTATUS(HANDLE)> underlyingSystemCallInvoker)
    {
      std::optional<OpenHandleStore::SHandleDataView> maybeClosedHandleData =
          openHandleStore.GetDataForHandle(handle);
      if (false == maybeClosedHandleData.has_value()) return underlyingSystemCallInvoker(handle);

      OpenHandleStore::SHandleData closedHandleData;
      NTSTATUS closeHandleResult = openHandleStore.RemoveAndCloseHandle(handle, &closedHandleData);

      if (NT_SUCCESS(closeHandleResult))
        Message::OutputFormatted(
            Message::ESeverity::Debug,
            L"%s(%u): Handle %zu for path \"%s\" was closed and erased from storage.",
            functionName,
            functionRequestIdentifier,
            reinterpret_cast<size_t>(handle),
            closedHandleData.associatedPath.c_str());

      return closeHandleResult;
    }

    std::optional<NTSTATUS> DirectoryEnumeration(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE fileHandle,
        HANDLE event,
        PIO_APC_ROUTINE apcRoutine,
        PVOID apcContext,
        PIO_STATUS_BLOCK ioStatusBlock,
        PVOID fileInformation,
        ULONG length,
        FILE_INFORMATION_CLASS fileInformationClass,
        ULONG queryFlags,
        PUNICODE_STRING fileName,
        std::function<DirectoryEnumerationInstruction(std::wstring_view, std::wstring_view)>
            instructionSourceFunc)
    {
      std::optional<FileInformationStructLayout> maybeFileInformationStructLayout =
          FileInformationStructLayout::LayoutForFileInformationClass(fileInformationClass);
      if (false == maybeFileInformationStructLayout.has_value()) return std::nullopt;

      std::optional<OpenHandleStore::SHandleDataView> maybeHandleData =
          openHandleStore.GetDataForHandle(fileHandle);
      if (false == maybeHandleData.has_value()) return std::nullopt;

      switch (GetInputOutputModeForHandle(fileHandle))
      {
        case EInputOutputMode::Synchronous:
          break;

        case EInputOutputMode::Asynchronous:
          Message::OutputFormatted(
              Message::ESeverity::Warning,
              L"%s(%u): Application requested asynchronous directory enumeration with handle %zu, which is unimplemented.",
              functionName,
              functionRequestIdentifier,
              reinterpret_cast<size_t>(fileHandle));
          return std::nullopt;

        default:
          Message::OutputFormatted(
              Message::ESeverity::Error,
              L"%s(%u): Failed to determine I/O mode during directory enumeration for handle %zu.",
              functionName,
              functionRequestIdentifier,
              reinterpret_cast<size_t>(fileHandle));
          return std::nullopt;
      }

      std::wstring_view queryFilePattern =
          ((nullptr == fileName) ? std::wstring_view()
                                 : Strings::NtConvertUnicodeStringToStringView(*fileName));
      if (true == queryFilePattern.empty())
      {
        Message::OutputFormatted(
            Message::ESeverity::Debug,
            L"%s(%u): Invoked with handle %zu, which is associated with path \"%.*s\" and opened for path \"%.*s\".",
            functionName,
            functionRequestIdentifier,
            reinterpret_cast<size_t>(fileHandle),
            static_cast<int>(maybeHandleData->associatedPath.length()),
            maybeHandleData->associatedPath.data(),
            static_cast<int>(maybeHandleData->realOpenedPath.length()),
            maybeHandleData->realOpenedPath.data());
      }
      else
      {
        Message::OutputFormatted(
            Message::ESeverity::Debug,
            L"%s(%u): Invoked with file pattern \"%.*s\" and handle %zu, which is associated with path \"%.*s\" and opened for path \"%.*s\".",
            functionName,
            functionRequestIdentifier,
            static_cast<int>(queryFilePattern.length()),
            queryFilePattern.data(),
            reinterpret_cast<size_t>(fileHandle),
            static_cast<int>(maybeHandleData->associatedPath.length()),
            maybeHandleData->associatedPath.data(),
            static_cast<int>(maybeHandleData->realOpenedPath.length()),
            maybeHandleData->realOpenedPath.data());
      }

      // The underlying system calls are expected to behave slightly differently on a first
      // invocation versus subsequent invocations.
      bool newDirectoryEnumerationCreated = false;

      if (false == maybeHandleData->directoryEnumeration.has_value())
      {
        // A new directory enumeration queue needs to be created because a directory enumeration
        // is being requested for the first time.

        DirectoryEnumerationInstruction directoryEnumerationInstruction =
            instructionSourceFunc(maybeHandleData->associatedPath, maybeHandleData->realOpenedPath);

        if (true == Globals::GetConfigurationData().isDryRunMode)
        {
          // This will mark the directory enumeration object as present but no-op. Future
          // invocations will therefore not attempt to query for a directory enumeration instruction
          // and will just be forwarded to the system.
          openHandleStore.AssociateDirectoryEnumerationState(
              fileHandle, nullptr, *maybeFileInformationStructLayout);

          return std::nullopt;
        }

        std::unique_ptr<IDirectoryOperationQueue> directoryOperationQueueUniquePtr =
            CreateDirectoryOperationQueueForInstruction(
                directoryEnumerationInstruction,
                fileInformationClass,
                queryFilePattern,
                maybeHandleData->associatedPath,
                maybeHandleData->realOpenedPath);
        openHandleStore.AssociateDirectoryEnumerationState(
            fileHandle,
            std::move(directoryOperationQueueUniquePtr),
            *maybeFileInformationStructLayout);
        newDirectoryEnumerationCreated = true;
      }

      maybeHandleData = openHandleStore.GetDataForHandle(fileHandle);
      DebugAssert(
          (true == maybeHandleData->directoryEnumeration.has_value()),
          "Failed to locate an in-progress directory enumearation stat data structure which should already exist.");

      // At this point a directory enumeration queue will be present. If it is `nullptr` then it is
      // a no-op and the original request just needs to be forwarded to the system.
      OpenHandleStore::SInProgressDirectoryEnumeration& directoryEnumerationState =
          *(*(maybeHandleData->directoryEnumeration));
      if (nullptr == directoryEnumerationState.queue) return std::nullopt;

      return AdvanceDirectoryEnumerationOperation(
          functionName,
          functionRequestIdentifier,
          directoryEnumerationState,
          newDirectoryEnumerationCreated,
          ioStatusBlock,
          fileInformation,
          length,
          queryFlags,
          queryFilePattern);
    }

    NTSTATUS NewFileHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        PHANDLE fileHandle,
        ACCESS_MASK desiredAccess,
        POBJECT_ATTRIBUTES objectAttributes,
        ULONG shareAccess,
        ULONG createDisposition,
        ULONG createOptions,
        std::function<FileOperationInstruction(
            std::wstring_view, FileAccessMode, CreateDisposition)> instructionSourceFunc,
        std::function<NTSTATUS(PHANDLE, POBJECT_ATTRIBUTES, ULONG)> underlyingSystemCallInvoker)
    {
      DumpNewFileHandleParameters(
          functionName,
          functionRequestIdentifier,
          objectAttributes,
          desiredAccess,
          shareAccess,
          createDisposition,
          createOptions);

      const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(
          functionName,
          functionRequestIdentifier,
          openHandleStore,
          objectAttributes->RootDirectory,
          Strings::NtConvertUnicodeStringToStringView(*(objectAttributes->ObjectName)),
          FileAccessModeFromNtParameter(desiredAccess),
          CreateDispositionFromNtParameter(createDisposition),
          instructionSourceFunc);
      const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

      if (true == Globals::GetConfigurationData().isDryRunMode ||
          (FileOperationInstruction::NoRedirectionOrInterception() == redirectionInstruction))
        return underlyingSystemCallInvoker(fileHandle, objectAttributes, createDisposition);

      NTSTATUS preOperationResult = ExecuteExtraPreOperations(
          functionName, functionRequestIdentifier, operationContext.instruction);
      if (!(NT_SUCCESS(preOperationResult))) return preOperationResult;

      SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
      FillRedirectedObjectNameAndAttributesForInstruction(
          redirectedObjectNameAndAttributes, operationContext.instruction, *objectAttributes);

      HANDLE newlyOpenedHandle = nullptr;
      NTSTATUS systemCallResult = NtStatus::kObjectPathNotFound;

      std::wstring_view unredirectedPath =
          ((true == operationContext.composedInputPath.has_value())
               ? operationContext.composedInputPath->AsStringView()
               : Strings::NtConvertUnicodeStringToStringView(*objectAttributes->ObjectName));
      std::wstring_view lastAttemptedPath;

      for (const auto& createDispositionToTry : SelectCreateDispositionsToTry(
               functionName, functionRequestIdentifier, redirectionInstruction, createDisposition))
      {
        if (true == createDispositionToTry.HasError())
        {
          Message::OutputFormatted(
              Message::ESeverity::SuperDebug,
              L"%s(%u): NTSTATUS = 0x%08x (forced result).",
              functionName,
              functionRequestIdentifier,
              static_cast<unsigned int>(createDispositionToTry.Error()));
          return createDispositionToTry.Error();
        }

        for (const auto& operationToTry : SelectFileOperationsToTry(
                 functionName,
                 functionRequestIdentifier,
                 redirectionInstruction,
                 *objectAttributes,
                 redirectedObjectNameAndAttributes.objectAttributes))
        {
          if (true == operationToTry.HasError())
          {
            Message::OutputFormatted(
                Message::ESeverity::SuperDebug,
                L"%s(%u): NTSTATUS = 0x%08x (forced result).",
                functionName,
                functionRequestIdentifier,
                static_cast<unsigned int>(operationToTry.Error()));
            return operationToTry.Error();
          }

          const POBJECT_ATTRIBUTES objectAttributesToTry = operationToTry.Value();
          const ULONG ntParamCreateDispositionToTry =
              createDispositionToTry.Value().ntParamCreateDisposition;

          std::wstring_view fileToTryAbsolutePath =
              ((nullptr != operationToTry.Value()->RootDirectory)
                   ? operationContext.composedInputPath->AsStringView()
                   : Strings::NtConvertUnicodeStringToStringView(
                         *(objectAttributesToTry->ObjectName)));

          bool shouldTryThisFile = false;
          switch (createDispositionToTry.Value().condition)
          {
            case SCreateDispositionToTry::ECondition::Unconditional:
              shouldTryThisFile = true;
              break;

            case SCreateDispositionToTry::ECondition::FileMustExist:
              shouldTryThisFile = FilesystemOperations::Exists(fileToTryAbsolutePath);
              break;

            case SCreateDispositionToTry::ECondition::FileMustNotExist:
              shouldTryThisFile = !(FilesystemOperations::Exists(fileToTryAbsolutePath));
              break;

            default:
              Message::OutputFormatted(
                  Message::ESeverity::Error,
                  L"%s(%u): Internal error: unrecognized create disposition condition (SCreateDispositionToTry::ECondition = %u).",
                  functionName,
                  functionRequestIdentifier,
                  static_cast<unsigned int>(createDispositionToTry.Value().condition));
              return NtStatus::kInternalError;
          }

          if (false == shouldTryThisFile) continue;

          lastAttemptedPath = fileToTryAbsolutePath;
          systemCallResult = underlyingSystemCallInvoker(
              &newlyOpenedHandle, objectAttributesToTry, ntParamCreateDispositionToTry);

          if (true == Message::WillOutputMessageOfSeverity(Message::ESeverity::SuperDebug))
            Message::OutputFormatted(
                Message::ESeverity::SuperDebug,
                L"%s(%u): NTSTATUS = 0x%08x, CreateDisposition = %s, ObjectName = \"%.*s\".",
                functionName,
                functionRequestIdentifier,
                systemCallResult,
                Strings::NtCreateDispositionToString(ntParamCreateDispositionToTry).AsCString(),
                static_cast<int>(lastAttemptedPath.length()),
                lastAttemptedPath.data());

          if (false == ShouldTryNextFilename(systemCallResult)) break;
        }

        if (false == ShouldTryNextFilename(systemCallResult)) break;
      }

      if (true == lastAttemptedPath.empty())
        return underlyingSystemCallInvoker(fileHandle, objectAttributes, createDisposition);

      if (NT_SUCCESS(systemCallResult))
        SelectFilenameAndStoreNewlyOpenedHandle(
            functionName,
            functionRequestIdentifier,
            openHandleStore,
            newlyOpenedHandle,
            redirectionInstruction,
            lastAttemptedPath,
            unredirectedPath);

      *fileHandle = newlyOpenedHandle;
      return systemCallResult;
    }

    NTSTATUS RenameByHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE fileHandle,
        SFileRenameInformation& renameInformation,
        ULONG renameInformationLength,
        std::function<FileOperationInstruction(
            std::wstring_view, FileAccessMode, CreateDisposition)> instructionSourceFunc,
        std::function<NTSTATUS(HANDLE, SFileRenameInformation&, ULONG)> underlyingSystemCallInvoker)
    {
      std::wstring_view unredirectedPath =
          FileInformationStructLayout::ReadFileNameByType(renameInformation);

      // A relative target path without a root directory handle means that the desired new absolute
      // path (after the rename) is relative to the directory in which the file is currently
      // located. Since the file is identified only by its handle, Pathwinder needs to figure out
      // the path of its containing directory so that redirection rules can be applied correctly.
      std::optional<TemporaryString> maybeComposedUnredirectedPath = std::nullopt;
      if ((nullptr == renameInformation.rootDirectory) &&
          (false == Strings::PathBeginsWithDriveLetter(unredirectedPath)))
      {
        // The file handle that identifies the file being rename may be cached in the open handle
        // store, meaning it was the subject of a Pathwinder operation in the past.
        // If so, the directory can be obtained using the handle's associated path.
        // If not, the system must be queried directly for the full path, from which the directory
        // can be obtained.

        std::optional<OpenHandleStore::SHandleDataView> maybeDirectorySourceHandleData =
            openHandleStore.GetDataForHandle(fileHandle);

        if (true == maybeDirectorySourceHandleData.has_value())
        {
          maybeComposedUnredirectedPath = TemporaryString();
          (*maybeComposedUnredirectedPath)
              << Strings::PathGetParentDirectory(maybeDirectorySourceHandleData->associatedPath)
              << L'\\' << unredirectedPath;
          unredirectedPath = maybeComposedUnredirectedPath->AsStringView();
        }
        else
        {
          auto maybeAbsolutePath = FilesystemOperations::QueryAbsolutePathByHandle(fileHandle);

          if (true == maybeAbsolutePath.HasValue())
          {
            maybeComposedUnredirectedPath = TemporaryString();
            (*maybeComposedUnredirectedPath)
                << Strings::PathGetParentDirectory(maybeAbsolutePath.Value().AsStringView())
                << L'\\' << unredirectedPath;
            unredirectedPath = maybeComposedUnredirectedPath->AsStringView();
          }
        }

        Message::OutputFormatted(
            Message::ESeverity::Debug,
            L"%s(%u): Rename by relative move for file handle %zu resolved to path \"%.*s\".",
            functionName,
            functionRequestIdentifier,
            reinterpret_cast<size_t>(fileHandle),
            static_cast<int>(unredirectedPath.length()),
            unredirectedPath.data());
      }

      const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(
          functionName,
          functionRequestIdentifier,
          openHandleStore,
          renameInformation.rootDirectory,
          unredirectedPath,
          FileAccessMode::Delete(),
          CreateDisposition::CreateNewFile(),
          instructionSourceFunc);
      const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

      if (true == Globals::GetConfigurationData().isDryRunMode)
        return underlyingSystemCallInvoker(fileHandle, renameInformation, renameInformationLength);

      NTSTATUS preOperationResult = ExecuteExtraPreOperations(
          functionName, functionRequestIdentifier, operationContext.instruction);
      if (!(NT_SUCCESS(preOperationResult))) return preOperationResult;

      NTSTATUS systemCallResult = NtStatus::kObjectPathNotFound;
      std::wstring_view lastAttemptedPath;

      // Due to how the file rename information structure is laid out, including an embedded
      // filename buffer of variable size, there is overhead to generating a new one. Without a
      // redirected filename present it is better to skip that process altogether.
      if (true == redirectionInstruction.HasRedirectedFilename())
      {
        BytewiseDanglingFilenameStruct<SFileRenameInformation>
            redirectedFileRenameInformationAndFilename(
                renameInformation, redirectionInstruction.GetRedirectedFilename());
        SFileRenameInformation& redirectedFileRenameInformation =
            redirectedFileRenameInformationAndFilename.GetFileInformationStruct();

        for (const auto& operationToTry : SelectFileOperationsToTry(
                 functionName,
                 functionRequestIdentifier,
                 redirectionInstruction,
                 renameInformation,
                 redirectedFileRenameInformation))
        {
          if (true == operationToTry.HasError())
          {
            Message::OutputFormatted(
                Message::ESeverity::SuperDebug,
                L"%s(%u): NTSTATUS = 0x%08x (forced result).",
                functionName,
                functionRequestIdentifier,
                static_cast<unsigned int>(operationToTry.Error()));
            return operationToTry.Error();
          }
          SFileRenameInformation* const renameInformationToTry = operationToTry.Value();

          lastAttemptedPath =
              FileInformationStructLayout::ReadFileNameByType(*renameInformationToTry);
          systemCallResult = underlyingSystemCallInvoker(
              fileHandle,
              *renameInformationToTry,
              FileInformationStructLayout::SizeOfStructByType(*renameInformationToTry));
          Message::OutputFormatted(
              Message::ESeverity::SuperDebug,
              L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".",
              functionName,
              functionRequestIdentifier,
              systemCallResult,
              static_cast<int>(lastAttemptedPath.length()),
              lastAttemptedPath.data());

          if (false == ShouldTryNextFilename(systemCallResult)) break;
        }
      }

      if (true == lastAttemptedPath.empty())
        systemCallResult =
            underlyingSystemCallInvoker(fileHandle, renameInformation, renameInformationLength);

      if (NT_SUCCESS(systemCallResult))
        SelectFilenameAndUpdateOpenHandle(
            functionName,
            functionRequestIdentifier,
            openHandleStore,
            fileHandle,
            redirectionInstruction,
            lastAttemptedPath,
            unredirectedPath);

      return systemCallResult;
    }

    NTSTATUS QueryByObjectAttributes(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        POBJECT_ATTRIBUTES objectAttributes,
        std::function<FileOperationInstruction(
            std::wstring_view, FileAccessMode, CreateDisposition)> instructionSourceFunc,
        std::function<NTSTATUS(POBJECT_ATTRIBUTES)> underlyingSystemCallInvoker)
    {
      const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(
          functionName,
          functionRequestIdentifier,
          openHandleStore,
          objectAttributes->RootDirectory,
          Strings::NtConvertUnicodeStringToStringView(*(objectAttributes->ObjectName)),
          FileAccessMode::ReadOnly(),
          CreateDisposition::OpenExistingFile(),
          instructionSourceFunc);
      const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

      if (true == Globals::GetConfigurationData().isDryRunMode ||
          (FileOperationInstruction::NoRedirectionOrInterception() == redirectionInstruction))
        return underlyingSystemCallInvoker(objectAttributes);

      NTSTATUS preOperationResult = ExecuteExtraPreOperations(
          functionName, functionRequestIdentifier, operationContext.instruction);
      if (!(NT_SUCCESS(preOperationResult))) return preOperationResult;

      SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
      FillRedirectedObjectNameAndAttributesForInstruction(
          redirectedObjectNameAndAttributes, operationContext.instruction, *objectAttributes);

      std::wstring_view lastAttemptedPath;

      NTSTATUS systemCallResult = NtStatus::kObjectPathNotFound;

      for (const auto& operationToTry : SelectFileOperationsToTry(
               functionName,
               functionRequestIdentifier,
               redirectionInstruction,
               *objectAttributes,
               redirectedObjectNameAndAttributes.objectAttributes))
      {
        if (true == operationToTry.HasError())
        {
          Message::OutputFormatted(
              Message::ESeverity::SuperDebug,
              L"%s(%u): NTSTATUS = 0x%08x (forced result).",
              functionName,
              functionRequestIdentifier,
              static_cast<unsigned int>(operationToTry.Error()));
          return operationToTry.Error();
        }

        const POBJECT_ATTRIBUTES objectAttributesToTry = operationToTry.Value();

        lastAttemptedPath =
            Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
        systemCallResult = underlyingSystemCallInvoker(objectAttributesToTry);
        Message::OutputFormatted(
            Message::ESeverity::SuperDebug,
            L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".",
            functionName,
            functionRequestIdentifier,
            systemCallResult,
            static_cast<int>(lastAttemptedPath.length()),
            lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult)) break;
      }

      if (true == lastAttemptedPath.empty()) return underlyingSystemCallInvoker(objectAttributes);

      return systemCallResult;
    }

    NTSTATUS QueryNameByHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE fileHandle,
        SFileNameInformation* fileNameInformation,
        ULONG fileNameInformationBufferCapacity,
        std::function<NTSTATUS(HANDLE, SFileNameInformation*, ULONG)> underlyingSystemCallInvoker,
        std::function<std::optional<std::wstring_view>(std::wstring_view, std::wstring_view)>
            replacementFileNameFilterAndTransform)
    {
      NTSTATUS systemCallResult = underlyingSystemCallInvoker(
          fileHandle, fileNameInformation, fileNameInformationBufferCapacity);
      switch (systemCallResult)
      {
        case NtStatus::kBufferOverflow:
          // Buffer overflows are allowed because the filename part will be overwritten and a true
          // overflow condition detected at that time.
          break;

        default:
          if (!(NT_SUCCESS(systemCallResult))) return systemCallResult;
          break;
      }

      // If the buffer is not big enough to hold any part of the filename then it is not necessary
      // to try replacing it.
      if (offsetof(SFileNameInformation, fileName) >=
          static_cast<size_t>(fileNameInformationBufferCapacity))
        return systemCallResult;

      const std::wstring_view systemReturnedFileName =
          FileInformationStructLayout::ReadFileNameByType(*fileNameInformation);

      // If the file handle is not stored, meaning it could not possibly be the result of a
      // redirection, then it is not necessary to replace the filename.
      std::optional<OpenHandleStore::SHandleDataView> maybeHandleData =
          openHandleStore.GetDataForHandle(fileHandle);
      if (false == maybeHandleData.has_value())
      {
        Message::OutputFormatted(
            Message::ESeverity::SuperDebug,
            L"%s(%u): Invoked with handle %zu, the system returned path \"%.*s\", and is it not being replaced.",
            functionName,
            functionRequestIdentifier,
            reinterpret_cast<size_t>(fileHandle),
            static_cast<int>(systemReturnedFileName.length()),
            systemReturnedFileName.data());

        return systemCallResult;
      }

      // The filter function is given an opportunity to veto the replacement or alter the result.
      // There is nothing further to do if it vetos the replacement or returns the same as the
      // system-returned path.
      std::optional<std::wstring_view> maybeReplacementFileName =
          replacementFileNameFilterAndTransform(
              systemReturnedFileName, maybeHandleData->associatedPath);
      if ((false == maybeReplacementFileName.has_value()) ||
          (*maybeReplacementFileName == systemReturnedFileName))
      {
        Message::OutputFormatted(
            Message::ESeverity::SuperDebug,
            L"%s(%u): Invoked with handle %zu, the system returned path \"%.*s\", and is it not being replaced.",
            functionName,
            functionRequestIdentifier,
            reinterpret_cast<size_t>(fileHandle),
            static_cast<int>(systemReturnedFileName.length()),
            systemReturnedFileName.data());

        return systemCallResult;
      }

      Message::OutputFormatted(
          Message::ESeverity::Debug,
          L"%s(%u): Invoked with handle %zu, the system returned path \"%.*s\", and it is being replaced with path \"%.*s\".",
          functionName,
          functionRequestIdentifier,
          reinterpret_cast<size_t>(fileHandle),
          static_cast<int>(systemReturnedFileName.length()),
          systemReturnedFileName.data(),
          static_cast<int>(maybeReplacementFileName->length()),
          maybeReplacementFileName->data());

      FileInformationStructLayout::WriteFileNameByType(
          *fileNameInformation, fileNameInformationBufferCapacity, *maybeReplacementFileName);

      const size_t requiredBufferSpaceBytes = maybeReplacementFileName->length() * sizeof(wchar_t);
      const size_t actualBufferSpaceBytes = fileNameInformation->fileNameLength;

      // If the actual number of bytes written is not enough to contain the entire replacement
      // filename then the file name length field should indicate the required buffer space, in
      // bytes, and the return code should indicate a buffer overflow condition.
      if (actualBufferSpaceBytes < requiredBufferSpaceBytes)
      {
        fileNameInformation->fileNameLength = static_cast<ULONG>(requiredBufferSpaceBytes);
        return NtStatus::kBufferOverflow;
      }

      // If the original system call resulted in a buffer overflow, but the buffer was large enough
      // to hold the replacement filename, then the application should be told that the operation
      // succeeded. Any other return code should be passed back to the application without
      // modification.
      return (
          (NtStatus::kBufferOverflow == systemCallResult) ? NtStatus::kSuccess : systemCallResult);
    }
  } // namespace FilesystemExecutor
} // namespace Pathwinder
