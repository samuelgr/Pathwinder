/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file Hooks.cpp
 *   Implementation of all Windows API hook functions used to implement path
 *   redirection.
 *****************************************************************************/

#include "ApiBitSet.h"
#include "ApiWindowsInternal.h"
#include "FileInformationStruct.h"
#include "FilesystemDirector.h"
#include "FilesystemInstruction.h"
#include "FilesystemOperations.h"
#include "Globals.h"
#include "Hooks.h"
#include "Message.h"
#include "OpenHandleStore.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <Hookshot/DynamicHook.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>


namespace Pathwinder
{
    // -------- INTERNAL TYPES --------------------------------------------- //

    /// Enumerates the possible modes for I/O using a file handle.
    enum class EInputOutputMode
    {
        Unknown,                                                            ///< I/O mode is not known. This represents an error case.
        Asynchronous,                                                       ///< I/O is asynchronous. System calls will return immediately, and completion information is provided out-of-band.
        Synchronous,                                                        ///< I/O is synchronous. System calls will return only after the requested operation completes.
        Count                                                               ///< Not used as a value. Identifies the number of enumerators present in this enumeration.
    };

    /// Holds file rename information, including the variably-sized filename, in a bytewise buffer and exposes it in a type-safe way.
    /// This object is used for representation only, not for any functionality surrounding actually creating a byte-wise buffer representation of a file rename information structure.
    class FileRenameInformationAndFilename
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Byte-wise buffer, which will hold the file rename information data structure.
        TemporaryVector<uint8_t> bytewiseBuffer;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor. Requires an already-constructed bytewise buffer, which is moved into place.
        inline FileRenameInformationAndFilename(TemporaryVector<uint8_t>&& bytewiseBuffer) : bytewiseBuffer(std::move(bytewiseBuffer))
        {
            // Nothing to do here.
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Convenience method for retrieving a properly-typed file rename information structure.
        inline SFileRenameInformation& GetFileRenameInformation(void)
        {
            return *(reinterpret_cast<SFileRenameInformation*>(bytewiseBuffer.Data()));
        }

        /// Convenience method for retrieving the size, in bytes, of the stored file rename information structure including filename.
        inline unsigned int GetFileRenameInformationSizeBytes(void)
        {
            return bytewiseBuffer.Size();
        }
    };

    /// Contains all of the information associated with a file operation.
    struct SFileOperationContext
    {
        FileOperationInstruction instruction;                               ///< How the redirection should be performed.
        std::optional<TemporaryString> composedInputPath;                   ///< If an input path was composed, for example due to combination with a root directory, then that input path is stored here.
    };

    /// Contains all of the information needed to represent a file name and attributes in the format needed to interact with underlying system calls.
    struct SObjectNameAndAttributes
    {
        UNICODE_STRING objectName;                                          ///< Name of the object, as a Unicode string in the format supported by system calls.
        OBJECT_ATTRIBUTES objectAttributes;                                 ///< Attributes of the object, which includes a field that points to the name.
    };


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Advances an in-progress directory enumeration operation by copying file information structures to an application-supplied buffer.
    /// Most parameters come directly from `NtQueryDirectoryFileEx` but those that do not are documented.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] enumerationState Enumeration state data structure, which must be mutable so it can be updated as the enumeration proceeds.
    /// @param [in] isFirstInvocation Whether or not to enable special behavior for the first invocation of a directory enumeration function, as specified by `NtQueryDirectoryFileEx` documentation.
    /// @return Windows error code corresponding to the result of advancing the directory enumeration operation.
    static NTSTATUS AdvanceDirectoryEnumerationOperation(const wchar_t* functionName, unsigned int functionRequestIdentifier, OpenHandleStore::SInProgressDirectoryEnumeration& enumerationState, bool isFirstInvocation, PIO_STATUS_BLOCK ioStatusBlock, PVOID outputBuffer, ULONG outputBufferSizeBytes, ULONG queryFlags, std::wstring_view queryFilePattern)
    {
        DebugAssert(nullptr != enumerationState.queue, "Advancing directory enumeration state without an operation queue.");

        if (queryFlags & Pathwinder::QueryFlag::kRestartScan)
        {
            enumerationState.queue->Restart(queryFilePattern);
            enumerationState.enumeratedFilenames.clear();
            isFirstInvocation = true;
        }

        // The `Information` field of the output I/O status block records the total number of bytes written.
        ioStatusBlock->Information = 0;

        // This block will cause `STATUS_NO_MORE_FILES` to be returned if the queue is empty and enumeration is complete.
        // Getting past here means the queue is not empty and more files can be enumerated.
        NTSTATUS enumerationStatus = enumerationState.queue->EnumerationStatus();
        if (!(NT_SUCCESS(enumerationStatus)))
        {
            // If the first invocation has resulted in no files available for enumeration then that should be the result.
            // This would only happen if a query file pattern is specified and it matches no files. Otherwise, a directory enumeration would at very least include "." and ".." entries.
            if ((true == isFirstInvocation) && NtStatus::kNoMoreFiles == enumerationStatus)
                return NtStatus::kNoSuchFile;

            return enumerationStatus;
        }

        // Some extra checks are required if this is the first invocation. This is to handle partial writes when the buffer is too small to hold a single complete structure.
        // On subsequent calls these checks are omitted, per `NtQueryDirectoryFileEx` documentation.
        if (true == isFirstInvocation)
        {
            if (outputBufferSizeBytes < enumerationState.fileInformationStructLayout.BaseStructureSize())
                return NtStatus::kBufferTooSmall;

            if (outputBufferSizeBytes < enumerationState.queue->SizeOfFront())
            {
                ioStatusBlock->Information = static_cast<ULONG_PTR>(enumerationState.queue->CopyFront(outputBuffer, outputBufferSizeBytes));
                enumerationState.fileInformationStructLayout.ClearNextEntryOffset(outputBuffer);
                return NtStatus::kBufferOverflow;
            }
        }

        const unsigned int maxElementsToWrite = ((queryFlags & Pathwinder::QueryFlag::kReturnSingleEntry) ? 1 : std::numeric_limits<unsigned int>::max());
        unsigned int numElementsWritten = 0;
        unsigned int numBytesWritten = 0;
        void* lastBufferPosition = nullptr;

        // At this point only full structures will be written, and it is safe to assume there is at least one file information structure left in the queue.
        while ((NT_SUCCESS(enumerationStatus)) && (numElementsWritten < maxElementsToWrite))
        {
            void* const bufferPosition = reinterpret_cast<void*>(reinterpret_cast<size_t>(outputBuffer) + static_cast<size_t>(numBytesWritten));
            const unsigned int bufferCapacityLeftBytes = outputBufferSizeBytes - numBytesWritten;

            if (bufferCapacityLeftBytes < enumerationState.queue->SizeOfFront())
                break;

            // If this is the first invocation, or just freshly-restarted, then no enumerated filenames have already been seen.
            // Otherwise the queue will have been pre-advanced to the first unique filename.
            // For these reasons it is correct to copy first and advance the queue after.
            numBytesWritten += enumerationState.queue->CopyFront(bufferPosition, bufferCapacityLeftBytes);
            numElementsWritten += 1;

            // There are a few reasons why the next entry offset field might not be correct.
            // If the file information structure is received from the system, then the value might be 0 to indicate no more files from the system, but that might not be correct to communicate to the application. Sometimes the system also adds padding which can be removed.
            // For these reasons it is necessary to update the next entry offset here and track the last written file information structure so that its next entry offset can be cleared after the loop.
            enumerationState.fileInformationStructLayout.UpdateNextEntryOffset(bufferPosition);
            lastBufferPosition = bufferPosition;

            enumerationState.enumeratedFilenames.emplace(std::wstring(enumerationState.queue->FileNameOfFront()));
            enumerationState.queue->PopFront();

            // Enumeration status must be checked first because, if there are no file information structures left in the queue, checking the front element's filename will cause a crash.
            while((NT_SUCCESS(enumerationState.queue->EnumerationStatus())) && (enumerationState.enumeratedFilenames.contains(enumerationState.queue->FileNameOfFront())))
                enumerationState.queue->PopFront();

            enumerationStatus = enumerationState.queue->EnumerationStatus();
        }

        if (nullptr != lastBufferPosition)
            enumerationState.fileInformationStructLayout.ClearNextEntryOffset(lastBufferPosition);

        ioStatusBlock->Information = numBytesWritten;

        // Whether or not the queue still has any file information structures is not relevant.
        // Coming into this function call there was at least one such structure available.
        // Even if it was not actually copied to the application buffer, and hence 0 bytes were copied, this is still considered success per `NtQueryDirectoryFileEx` documentation.
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

    /// Copies the contents of the supplied file rename information structure but replaces the filename.
    /// This has the effect of changing the new name of the specified file after the rename operation completes.
    /// @param [in] inputFileRenameInformation Original file rename information structurewhose contents are to be copied.
    /// @param [in] replacementFilename Replacement filename.
    /// @return Buffer containing a new file rename information structure with the filename replaced.
    static FileRenameInformationAndFilename CopyFileRenameInformationAndReplaceFilename(const SFileRenameInformation& inputFileRenameInformation, std::wstring_view replacementFilename)
    {
        TemporaryVector<uint8_t> newFileRenameInformation;

        SFileRenameInformation outputFileRenameInformation = inputFileRenameInformation;
        outputFileRenameInformation.fileNameLength = (static_cast<ULONG>(replacementFilename.length()) * sizeof(wchar_t));

        for (size_t i = 0; i < offsetof(SFileRenameInformation, fileName); ++i)
            newFileRenameInformation.PushBack((reinterpret_cast<const uint8_t*>(&outputFileRenameInformation))[i]);

        for (size_t i = 0; i < (replacementFilename.length() * sizeof(wchar_t)); ++i)
            newFileRenameInformation.PushBack((reinterpret_cast<const uint8_t*>(replacementFilename.data()))[i]);

        return FileRenameInformationAndFilename(std::move(newFileRenameInformation));
    }

    /// Executes any pre-operations needed ahead of invoking underlying system calls.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation, including identifying any pre-operations needed.
    /// @return Result of executing the pre-operations. The code will indicate success if they all succeed or a failure that corresponds to the first applicable pre-operation failure.
    static NTSTATUS ExecuteExtraPreOperations(const wchar_t* functionName, unsigned int functionRequestIdentifier, const FileOperationInstruction& instruction)
    {
        NTSTATUS extraPreOperationResult = NtStatus::kSuccess;

        if (instruction.GetExtraPreOperations().contains(static_cast<int>(FileOperationInstruction::EExtraPreOperation::EnsurePathHierarchyExists)) && (NT_SUCCESS(extraPreOperationResult)))
        {
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Ensuring directory hierarchy exists for \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(instruction.GetExtraPreOperationOperand().length()), instruction.GetExtraPreOperationOperand().data());
            extraPreOperationResult = static_cast<NTSTATUS>(FilesystemOperations::CreateDirectoryHierarchy(instruction.GetExtraPreOperationOperand()));
        }

        if (!(NT_SUCCESS(extraPreOperationResult)))
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): A required pre-operation failed (NTSTATUS = 0x%08x).", functionName, functionRequestIdentifier, static_cast<unsigned int>(extraPreOperationResult));

        return extraPreOperationResult;
    }

    /// Converts a `CreateDisposition` parameter, which system calls use to identify filesystem behavior regarding creating new files or opening existing files, into an appropriate file operation mode.
    /// @param [in] ntCreateDisposition `CreateDisposition` parameter received from the application.
    /// @return Corresponding file operation mode enumerator.
    static FilesystemDirector::EFileOperationMode FileOperationModeFromCreateDisposition(ULONG ntCreateDisposition)
    {
        switch (ntCreateDisposition)
        {
        case FILE_CREATE:
            return FilesystemDirector::EFileOperationMode::CreateNewFile;

        case FILE_SUPERSEDE:
        case FILE_OPEN_IF:
        case FILE_OVERWRITE_IF:
            return FilesystemDirector::EFileOperationMode::CreateNewOrOpenExistingFile;

        case FILE_OPEN:
        case FILE_OVERWRITE:
            return FilesystemDirector::EFileOperationMode::OpenExistingFile;

        default:
            return FilesystemDirector::EFileOperationMode::OpenExistingFile;
        }
    }

    /// Fills the supplied object name and attributes structure with the name and attributes needed to represent the redirected filename from a file operation redirection instruction.
    /// Does nothing if the file operation redirection instruction does not specify any redirection.
    /// This must be done in place because the `OBJECT_ATTRIBUTES` structure refers to its `ObjectName` field by pointer. Returning by value would invalidate the address of the `ObjectName` field and therefore not work.
    /// @param [out] redirectedObjectNameAndAttributes Mutable reference to the structure to be filled.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] unredirectedObjectAttributes Object attributes structure received from the application.
    static void FillRedirectedObjectNameAndAttributesForInstruction(SObjectNameAndAttributes& redirectedObjectNameAndAttributes, const FileOperationInstruction& instruction, const OBJECT_ATTRIBUTES& unredirectedObjectAttributes)
    {
        if (true == instruction.HasRedirectedFilename())
        {
            redirectedObjectNameAndAttributes.objectName = Strings::NtConvertStringViewToUnicodeString(instruction.GetRedirectedFilename());

            redirectedObjectNameAndAttributes.objectAttributes = unredirectedObjectAttributes;
            redirectedObjectNameAndAttributes.objectAttributes.RootDirectory = nullptr;
            redirectedObjectNameAndAttributes.objectAttributes.ObjectName = &redirectedObjectNameAndAttributes.objectName;
        }
    }

    /// Retrieves an identifier for a particular invocation of a hook function.
    /// Used exclusively for logging.
    /// @return Numeric identifier for an invocation.
    static inline unsigned int GetRequestIdentifier(void)
    {
        static std::atomic<unsigned int> nextRequestIdentifier;
        return nextRequestIdentifier++;
    }

    /// Determines how to redirect an individual file operation in which the affected file is identified by an object attributes structure.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] rootDirectory Open handle for the root directory that contains the input filename. May be `nullptr`, in which case the input filename must be a full and absolute path. Supplied by an application that invokes a system call.
    /// @param [in] inputFilename Filename received from the application that invoked the system call. Must be a full and absolute path if the root directory handle is not provided.
    /// @param [in] fileOperationMode Type of file operation requested by the application.
    /// @return Context that contains all of the information needed to submit the file operation to the underlying system call.
    static SFileOperationContext GetFileOperationRedirectionInformation(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE rootDirectory, std::wstring_view inputFilename, FilesystemDirector::EFileOperationMode fileOperationMode)
    {
        std::optional<TemporaryString> maybeRedirectedFilename = std::nullopt;
        std::optional<OpenHandleStore::SHandleDataView> maybeRootDirectoryHandleData = ((nullptr == rootDirectory) ? std::nullopt : OpenHandleStore::Singleton().GetDataForHandle(rootDirectory));

        if (true == maybeRootDirectoryHandleData.has_value())
        {
            // Input object attributes structure specifies an open directory handle as the root directory and the handle was found in the cache.
            // Before querying for redirection it is necessary to assemble the full filename, including the root directory path.

            std::wstring_view rootDirectoryHandlePath = maybeRootDirectoryHandleData->associatedPath;

            TemporaryString inputFullFilename;
            inputFullFilename << rootDirectoryHandlePath << L'\\' << inputFilename;

            FileOperationInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().GetInstructionForFileOperation(inputFullFilename, fileOperationMode);
            if (true == redirectionInstruction.HasRedirectedFilename())
                Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined and redirected to \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(rootDirectoryHandlePath.length()), rootDirectoryHandlePath.data(), reinterpret_cast<size_t>(rootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
            else
                Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined but not redirected.", functionName, functionRequestIdentifier, static_cast<int>(rootDirectoryHandlePath.length()), rootDirectoryHandlePath.data(), reinterpret_cast<size_t>(rootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data());

            return {.instruction = std::move(redirectionInstruction), .composedInputPath = std::move(inputFullFilename)};
        }
        else if (nullptr == rootDirectory)
        {
            // Input object attributes structure does not specify an open directory handle as the root directory.
            // It is sufficient to send the object name directly for redirection.

            FileOperationInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().GetInstructionForFileOperation(inputFilename, fileOperationMode);

            if (true == redirectionInstruction.HasRedirectedFilename())
                Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with path \"%.*s\" which was redirected to \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
            else
                Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with path \"%.*s\" which was not redirected.", functionName, functionRequestIdentifier, static_cast<int>(inputFilename.length()), inputFilename.data());

            return {.instruction = std::move(redirectionInstruction), .composedInputPath = std::nullopt};
        }
        else
        {
            // Input object attributes structure specifies an open directory handle as the root directory but the handle is not in cache.
            // When the root directory handle was originally opened it was determined that there is no possible match with a filesystem rule.
            // Therefore, it is not necessary to attempt redirection.

            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with root directory handle %zu and relative path \"%.*s\" for which no redirection was attempted.", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(rootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data());
            return {.instruction = FileOperationInstruction::NoRedirectionOrInterception(), .composedInputPath = std::nullopt};
        }
    }

    /// Determines the input/output mode for the specified file handle.
    /// @param [in] handle Filesystem object handle to check.
    /// @return Input/output mode for the handle, or #EInputOutputMode::Unknown in the event of an error.
    static EInputOutputMode GetInputOutputModeForHandle(HANDLE handle)
    {
        SFileModeInformation modeInformation;
        IO_STATUS_BLOCK unusedStatusBlock{};

        if (!(NT_SUCCESS(Hooks::ProtectedDependency::NtQueryInformationFile::SafeInvoke(handle, &unusedStatusBlock, &modeInformation, sizeof(modeInformation), SFileModeInformation::kFileInformationClass))))
            return EInputOutputMode::Unknown;

        switch (modeInformation.mode & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT))
        {
        case 0:
            return EInputOutputMode::Asynchronous;
        default:
            return EInputOutputMode::Synchronous;
        }
    }

    /// Places pointers to the data structures that identify files to try, in order, to submit to the underlying system call for a file operation.
    /// The number of pointers placed, and the order in which they are placed, is controlled by the file operation redirection instruction.
    /// Any entries placed as `nullptr` are invalid and should be skipped.
    /// @tparam FileObjectType Data structure type that identifies files to try.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] unredirectedFileObject Pointer to the data structure received from the application.
    /// @param [in] redirectedFileObject Pointer to the data structure generated by querying for file operation redirection.
    /// @return Object attributes to be tried, in order.
    template <typename FileObjectType> std::array<FileObjectType*, 2> SelectFilesToTry(const wchar_t* functionName, unsigned int functionRequestIdentifier, const FileOperationInstruction& instruction, FileObjectType* unredirectedFileObject, FileObjectType* redirectedFileObject)
    {
        switch (instruction.GetFilenamesToTry())
        {
        case FileOperationInstruction::ETryFiles::UnredirectedOnly:
            return {unredirectedFileObject, nullptr};

        case FileOperationInstruction::ETryFiles::UnredirectedFirst:
            return {unredirectedFileObject, redirectedFileObject};

        case FileOperationInstruction::ETryFiles::RedirectedFirst:
            return {redirectedFileObject, unredirectedFileObject};

        case FileOperationInstruction::ETryFiles::RedirectedOnly:
            return {redirectedFileObject};

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationInstruction::ETryFiles = %u).", functionName, functionRequestIdentifier, static_cast<unsigned int>(instruction.GetFilenamesToTry()));
            return {nullptr, nullptr};
        }
    }

    /// Determines if the next possible filename should be tried or if the existing system call result should be returned to the application.
    /// @param [in] systemCallResult Result of the system call for the present attempt.
    /// @return `true` if the result indicates that the next filename should be tried, `false` if the result indicates to stop trying and move on.
    static inline bool ShouldTryNextFilename(NTSTATUS systemCallResult)
    {
        // This check is simply based on whether or not the call was successful.
        // If not, advance to the next file, otherwise stop.
        return (!(NT_SUCCESS(systemCallResult)));
    }

    /// Inserts a newly-opened handle into the open handle store, selecting an associated path based on the file operation redirection instruction.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] newlyOpenedHandle Handle to add to the open handles store.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] successfulPath Path that was used successfully to create the file handle.
    /// @param [in] unredirectedPath Original file name supplied by the application.
    static void SelectFilenameAndStoreNewlyOpenedHandle(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE newlyOpenedHandle, const FileOperationInstruction& instruction, std::wstring_view successfulPath, std::wstring_view unredirectedPath)
    {
        std::wstring_view selectedPath;

        switch (instruction.GetFilenameHandleAssociation())
        {
        case FileOperationInstruction::EAssociateNameWithHandle::None:
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::WhicheverWasSuccessful:
            selectedPath = successfulPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Unredirected:
            selectedPath = unredirectedPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Redirected:
            selectedPath = instruction.GetRedirectedFilename();
            break;

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationInstruction::EAssociateNameWithHandle = %u).", functionName, functionRequestIdentifier, static_cast<unsigned int>(instruction.GetFilenameHandleAssociation()));
            break;
        }

        if (false == selectedPath.empty())
        {
            successfulPath = Strings::RemoveTrailing(successfulPath, L'\\');
            selectedPath = Strings::RemoveTrailing(selectedPath, L'\\');

            OpenHandleStore::Singleton().InsertHandle(newlyOpenedHandle, std::wstring(selectedPath), std::wstring(successfulPath));
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu was opened for path \"%.*s\" and stored in association with path \"%.*s\".", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(newlyOpenedHandle), static_cast<int>(successfulPath.length()), successfulPath.data(), static_cast<int>(selectedPath.length()), selectedPath.data());
        }
    }

    /// Updates a handle that might already be in the open handle store, selecting an associated path based on the file operation redirection instruction.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] handleToUpdate Handle to update in the open handles store, if it is present.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] successfulPath Path that was used successfully to create the file handle.
    /// @param [in] unredirectedPath Original file name supplied by the application.
    static void SelectFilenameAndUpdateOpenHandle(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE handleToUpdate, const FileOperationInstruction& instruction, std::wstring_view successfulPath, std::wstring_view unredirectedPath)
    {
        std::wstring_view selectedPath;

        switch (instruction.GetFilenameHandleAssociation())
        {
        case FileOperationInstruction::EAssociateNameWithHandle::None:
            do {
                OpenHandleStore::SHandleData erasedHandleData;
                if (true == OpenHandleStore::Singleton().RemoveHandle(handleToUpdate, &erasedHandleData))
                    Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu associated with path \"%s\" was erased from storage.", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(handleToUpdate), erasedHandleData.associatedPath.c_str());
            } while (false);
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::WhicheverWasSuccessful:
            selectedPath = successfulPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Unredirected:
            selectedPath = unredirectedPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Redirected:
            selectedPath = instruction.GetRedirectedFilename();
            break;

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationInstruction::EAssociateNameWithHandle = %u).", functionName, functionRequestIdentifier, static_cast<unsigned int>(instruction.GetFilenameHandleAssociation()));
            break;
        }

        if (false == selectedPath.empty())
        {
            successfulPath = Strings::RemoveTrailing(successfulPath, L'\\');
            selectedPath = Strings::RemoveTrailing(selectedPath, L'\\');

            OpenHandleStore::Singleton().InsertOrUpdateHandle(handleToUpdate, std::wstring(selectedPath), std::wstring(successfulPath));
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu was updated in storage to be opened with path \"%.*s\" and associated with path \"%.*s\".", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(handleToUpdate), static_cast<int>(successfulPath.length()), successfulPath.data(), static_cast<int>(selectedPath.length()), selectedPath.data());
        }
    }

    /// Common internal implementation of directory enumeration hook functions.
    /// Parameters correspond to the `NtQueryDirectoryFileEx` system call, with the exception of `functionName` which is just the hook function name for logging purposes.
    /// @return Result to be returned to the application on system call completion, or nothing at all if the request should be forwarded to the application.
    std::optional<NTSTATUS> HookFunctionCommonImplementationQueryDirectoryFile(const wchar_t* functionName, HANDLE fileHandle, HANDLE event, PIO_APC_ROUTINE apcRoutine, PVOID apcContext, PIO_STATUS_BLOCK ioStatusBlock, PVOID fileInformation, ULONG length, FILE_INFORMATION_CLASS fileInformationClass, ULONG queryFlags, PUNICODE_STRING fileName)
    {
        std::optional<FileInformationStructLayout> maybeFileInformationStructLayout = FileInformationStructLayout::LayoutForFileInformationClass(fileInformationClass);
        if (false == maybeFileInformationStructLayout.has_value())
            return std::nullopt;

        std::optional<OpenHandleStore::SHandleDataView> maybeHandleData = OpenHandleStore::Singleton().GetDataForHandle(fileHandle);
        if (false == maybeHandleData.has_value())
            return std::nullopt;

        const unsigned int requestIdentifier = GetRequestIdentifier();

        switch (GetInputOutputModeForHandle(fileHandle))
        {
        case EInputOutputMode::Synchronous:
            break;

        case EInputOutputMode::Asynchronous:
            Message::OutputFormatted(Message::ESeverity::Warning, L"%s(%u): Application requested asynchronous directory enumeration with handle %zu, which is unimplemented.", functionName, requestIdentifier, reinterpret_cast<size_t>(fileHandle));
            return std::nullopt;

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Failed to determine I/O mode during directory enumeration for handle %zu.", functionName, requestIdentifier, reinterpret_cast<size_t>(fileHandle));
            return std::nullopt;
        }

        std::wstring_view queryFilePattern = ((nullptr == fileName) ? std::wstring_view() : Strings::NtConvertUnicodeStringToStringView(*fileName));
        if (true == queryFilePattern.empty())
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with handle %zu, which is associated with path \"%.*s\" and opened for path \"%.*s\".", functionName, requestIdentifier, reinterpret_cast<size_t>(fileHandle), static_cast<int>(maybeHandleData->associatedPath.length()), maybeHandleData->associatedPath.data(), static_cast<int>(maybeHandleData->realOpenedPath.length()), maybeHandleData->realOpenedPath.data());
        else
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with file pattern \"%.*s\" and handle %zu, which is associated with path \"%.*s\" and opened for path \"%.*s\".", functionName, requestIdentifier, static_cast<int>(queryFilePattern.length()), queryFilePattern.data(), reinterpret_cast<size_t>(fileHandle), static_cast<int>(maybeHandleData->associatedPath.length()), maybeHandleData->associatedPath.data(), static_cast<int>(maybeHandleData->realOpenedPath.length()), maybeHandleData->realOpenedPath.data());

        bool newDirectoryEnumerationCreated = false;
        if (false == maybeHandleData->directoryEnumeration.has_value())
        {
            // A new directory enumeration queue needs to be created because a directory enumeration is being requested for the first time.
            // The underlying system calls are expected to behave slightly differently on a first invocation versus subsequent invocations, hence the need to identify when a new directory enumeration queue is first created.

            DirectoryEnumerationInstruction directoryEnumerationInstruction = FilesystemDirector::Singleton().GetInstructionForDirectoryEnumeration(maybeHandleData->associatedPath, maybeHandleData->realOpenedPath);

            if (true == Globals::GetConfigurationData().isDryRunMode)
            {
                // This will mark the directory enumeration object as present but no-op.
                // Future invocations will therefore not attempt to query for a directory enumeration instruction and will just be forwarded to the system.
                OpenHandleStore::Singleton().AssociateDirectoryEnumerationState(fileHandle, nullptr, *maybeFileInformationStructLayout);
                return std::nullopt;
            }

            std::unique_ptr<IDirectoryOperationQueue> directoryOperationQueueUniquePtr = CreateDirectoryOperationQueueForInstruction(directoryEnumerationInstruction, fileInformationClass, queryFilePattern, maybeHandleData->associatedPath, maybeHandleData->realOpenedPath);
            OpenHandleStore::Singleton().AssociateDirectoryEnumerationState(fileHandle, std::move(directoryOperationQueueUniquePtr), *maybeFileInformationStructLayout);
            newDirectoryEnumerationCreated = true;
        }

        maybeHandleData = OpenHandleStore::Singleton().GetDataForHandle(fileHandle);
        DebugAssert((true == maybeHandleData->directoryEnumeration.has_value()), "Failed to locate an in-progress directory enumearation stat data structure which should already exist.");

        OpenHandleStore::SInProgressDirectoryEnumeration& directoryEnumerationState = *(*(maybeHandleData->directoryEnumeration));

        // At this point a directory enumeration queue will be present.
        // If it is `nullptr` then it is a no-op and the original request just needs to be forwarded to the system.
        if (nullptr == directoryEnumerationState.queue)
            return std::nullopt;

        return AdvanceDirectoryEnumerationOperation(functionName, requestIdentifier, directoryEnumerationState, newDirectoryEnumerationCreated, ioStatusBlock, fileInformation, length, queryFlags, queryFilePattern);
    }
}


// -------- PROTECTED HOOK FUNCTIONS --------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtClose::Hook(HANDLE Handle)
{
    using namespace Pathwinder;


    std::optional<OpenHandleStore::SHandleDataView> maybeClosedHandleData = OpenHandleStore::Singleton().GetDataForHandle(Handle);
    if (false == maybeClosedHandleData.has_value())
        return Original(Handle);

    const unsigned int requestIdentifier = GetRequestIdentifier();

    OpenHandleStore::SHandleData closedHandleData;
    NTSTATUS closeHandleResult = OpenHandleStore::Singleton().RemoveAndCloseHandle(Handle, &closedHandleData);

    if (NT_SUCCESS(closeHandleResult))
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu for path \"%s\" was closed and erased from storage.", GetFunctionName(), requestIdentifier, reinterpret_cast<size_t>(Handle), closedHandleData.associatedPath.c_str());

    return closeHandleResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtCreateFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, ObjectAttributes->RootDirectory, Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)), FileOperationModeFromCreateDisposition(CreateDisposition));
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    HANDLE newlyOpenedHandle = nullptr;
    NTSTATUS systemCallResult = NtStatus::kSuccess;

    std::wstring_view unredirectedPath = ((true == operationContext.composedInputPath.has_value()) ? operationContext.composedInputPath->AsStringView() : Strings::NtConvertUnicodeStringToStringView(*ObjectAttributes->ObjectName));
    std::wstring_view lastAttemptedPath;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
    {
        if (nullptr == objectAttributesToTry)
            continue;

        lastAttemptedPath = Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, objectAttributesToTry, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

    if (NT_SUCCESS(systemCallResult))
        SelectFilenameAndStoreNewlyOpenedHandle(GetFunctionName(), requestIdentifier, newlyOpenedHandle, redirectionInstruction, lastAttemptedPath, unredirectedPath);

    *FileHandle = newlyOpenedHandle;
    return systemCallResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtDeleteFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes)
{
    using namespace Pathwinder;


    return Original(ObjectAttributes);
}
// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtOpenFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, ObjectAttributes->RootDirectory, Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)), FilesystemDirector::EFileOperationMode::OpenExistingFile);
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    HANDLE newlyOpenedHandle = nullptr;
    NTSTATUS systemCallResult = NtStatus::kSuccess;

    std::wstring_view unredirectedPath = ((true == operationContext.composedInputPath.has_value()) ? operationContext.composedInputPath->AsStringView() : Strings::NtConvertUnicodeStringToStringView(*ObjectAttributes->ObjectName));
    std::wstring_view lastAttemptedPath;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
    {
        if (nullptr == objectAttributesToTry)
            continue;

        lastAttemptedPath = Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, objectAttributesToTry, IoStatusBlock, ShareAccess, OpenOptions);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);

    if (NT_SUCCESS(systemCallResult))
        SelectFilenameAndStoreNewlyOpenedHandle(GetFunctionName(), requestIdentifier, newlyOpenedHandle, redirectionInstruction, lastAttemptedPath, unredirectedPath);

    *FileHandle = newlyOpenedHandle;
    return systemCallResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFile::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName, BOOLEAN RestartScan)
{
    using namespace Pathwinder;


    ULONG queryFlags = 0;
    if (RestartScan != 0) queryFlags |= QueryFlag::kRestartScan;
    if (ReturnSingleEntry != 0) queryFlags |= QueryFlag::kReturnSingleEntry;

    auto maybeHookFunctionResult = HookFunctionCommonImplementationQueryDirectoryFile(GetFunctionName(), FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, queryFlags, FileName);
    if (false == maybeHookFunctionResult.has_value())
        return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, ReturnSingleEntry, FileName, RestartScan);

    return *maybeHookFunctionResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFileEx::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, ULONG QueryFlags, PUNICODE_STRING FileName)
{
    using namespace Pathwinder;


    auto maybeHookFunctionResult = HookFunctionCommonImplementationQueryDirectoryFile(GetFunctionName(), FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, QueryFlags, FileName);
    if (false == maybeHookFunctionResult.has_value())
        return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, QueryFlags, FileName);

    return *maybeHookFunctionResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationFile::Hook(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    using namespace Pathwinder;


    NTSTATUS systemCallResult = Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
    switch (systemCallResult)
    {
    case NtStatus::kBufferOverflow:
        // Buffer overflows are allowed because Pathwinder will attempt to overwrite the filename part and, in doing so, determine for itself if a buffer overflow condition actually exists.
        break;

    default:
        if (!(NT_SUCCESS(systemCallResult)))
            return systemCallResult;
        break;
    }

    SFileNameInformation* fileNameInformation = nullptr;
    switch (FileInformationClass)
    {
    case SFileNameInformation::kFileInformationClass:
        fileNameInformation = reinterpret_cast<SFileNameInformation*>(FileInformation);
        break;

    case SFileAllInformation::kFileInformationClass:
        fileNameInformation = &(reinterpret_cast<SFileAllInformation*>(FileInformation)->nameInformation);
        break;

    default:
        return systemCallResult;
    }

    // If the buffer is not big enough to hold any part of the filename then it is not necessary to try replacing it.
    const size_t fileNameInformationBufferOffset = reinterpret_cast<size_t>(fileNameInformation) - reinterpret_cast<size_t>(FileInformation);
    if ((fileNameInformationBufferOffset + offsetof(SFileNameInformation, fileName)) >= static_cast<size_t>(Length))
        return systemCallResult;

    std::wstring_view systemReturnedFileName = GetFileInformationStructFilename(*fileNameInformation);
    if (false == systemReturnedFileName.starts_with(L'\\'))
        return systemCallResult;

    std::optional<OpenHandleStore::SHandleDataView> maybeHandleData = OpenHandleStore::Singleton().GetDataForHandle(FileHandle);
    if (false == maybeHandleData.has_value())
        return systemCallResult;

    std::wstring_view replacementFileName = maybeHandleData->associatedPath;

    // Paths in the open handle store are expected to have a Windows name prefix and begin with a drive letter.
    // According to the documentation for `NtQueryInformationFile` absolute paths begin with a backslash and skip over the drive letter component.
    // So if the actual path is "C:\Dir\file.txt" the path returned by the system call is "\Dir\file.txt" and hence Pathwinder needs to do the same transformation.
    replacementFileName.remove_prefix(Strings::PathGetWindowsNamespacePrefix(replacementFileName).length());
    replacementFileName.remove_prefix(replacementFileName.find_first_of(L'\\'));
    if (replacementFileName == systemReturnedFileName)
        return systemCallResult;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with handle %zu, the system returned path \"%.*s\", and it is being replaced with path \"%.*s\".", GetFunctionName(), requestIdentifier, reinterpret_cast<size_t>(FileHandle), static_cast<int>(systemReturnedFileName.length()), systemReturnedFileName.data(), static_cast<int>(replacementFileName.length()), replacementFileName.data());

    const size_t numReplacementCharsWritten = SetFileInformationStructFilename(*fileNameInformation, (static_cast<size_t>(Length) - fileNameInformationBufferOffset), replacementFileName);
    if (numReplacementCharsWritten < replacementFileName.length())
        return NtStatus::kBufferOverflow;

    // If the original system call resulted in a buffer overflow, but the buffer was large enough to hold the replacement filename, then the application should be told that the operation succeeded.
    // Any other return code should be passed back to the application without modification.
    return ((NtStatus::kBufferOverflow == systemCallResult) ? NtStatus::kSuccess : systemCallResult);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationByName::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, ObjectAttributes->RootDirectory, Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)), FilesystemDirector::EFileOperationMode::OpenExistingFile);
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    std::wstring_view lastAttemptedPath;

    NTSTATUS systemCallResult = NtStatus::kSuccess;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
    {
        if (nullptr == objectAttributesToTry)
            continue;

        lastAttemptedPath = Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
        systemCallResult = Original(objectAttributesToTry, IoStatusBlock, FileInformation, Length, FileInformationClass);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
        return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

    return systemCallResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtSetInformationFile::Hook(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    using namespace Pathwinder;


    // This invocation is only interesting if it is a rename operation. Otherwise there is no change being made to the input file handle, which is already open.
    if (SFileRenameInformation::kFileInformationClass != FileInformationClass)
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    const unsigned int requestIdentifier = GetRequestIdentifier();

    SFileRenameInformation& unredirectedFileRenameInformation = *(reinterpret_cast<SFileRenameInformation*>(FileInformation));
    std::wstring_view unredirectedPath = GetFileInformationStructFilename(unredirectedFileRenameInformation);

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, unredirectedFileRenameInformation.rootDirectory, unredirectedPath, FilesystemDirector::EFileOperationMode::CreateNewFile);
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    // Due to how the file rename information structure is laid out, including an embedded filename buffer of variable size, there is overhead to generating a new one.
    // Without a redirected filename present it is better to bail early than to generate a new one unconditionally.
    if (false == redirectionInstruction.HasRedirectedFilename())
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    FileRenameInformationAndFilename redirectedFileRenameInformationAndFilename = CopyFileRenameInformationAndReplaceFilename(unredirectedFileRenameInformation, redirectionInstruction.GetRedirectedFilename());
    SFileRenameInformation& redirectedFileRenameInformation = redirectedFileRenameInformationAndFilename.GetFileRenameInformation();

    NTSTATUS systemCallResult = NtStatus::kSuccess;
    std::wstring_view lastAttemptedPath;

    for (SFileRenameInformation* renameInformationToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, &unredirectedFileRenameInformation, &redirectedFileRenameInformation))
    {
        if (nullptr == renameInformationToTry)
            continue;

        lastAttemptedPath = GetFileInformationStructFilename(*renameInformationToTry);
        systemCallResult = Original(FileHandle, IoStatusBlock, reinterpret_cast<PVOID>(renameInformationToTry), Length, FileInformationClass);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    if (NT_SUCCESS(systemCallResult))
        SelectFilenameAndUpdateOpenHandle(GetFunctionName(), requestIdentifier, FileHandle, redirectionInstruction, lastAttemptedPath, unredirectedPath);

    return systemCallResult;
}


// -------- UNPROTECTED HOOK FUNCTIONS ------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryAttributesFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFO FileInformation)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, ObjectAttributes->RootDirectory, Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)), FilesystemDirector::EFileOperationMode::OpenExistingFile);
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(ObjectAttributes, FileInformation);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    std::wstring_view lastAttemptedPath;

    NTSTATUS systemCallResult = NtStatus::kSuccess;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
    {
        if (nullptr == objectAttributesToTry)
            continue;

        lastAttemptedPath = Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
        systemCallResult = Original(objectAttributesToTry, FileInformation);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
        return Original(ObjectAttributes, FileInformation);

    return systemCallResult;
}
