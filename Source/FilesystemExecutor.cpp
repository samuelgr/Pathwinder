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
    /// Retrieves a reference to the open handle store instance. It is maintained on the heap so it
    /// is not destroyed automatically by the runtime on program exit.
    /// @return Mutable reference to the open handle store instance.
    static inline OpenHandleStore& OpenHandleStoreInstance(void)
    {
      static OpenHandleStore* const openHandleStore = new OpenHandleStore;
      return *openHandleStore;
    }

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
            NtAccessMaskToString(desiredAccess).AsCString());
        Message::OutputFormatted(
            kDumpSeverity,
            L"%s(%u):   ShareAccess = %s",
            functionName,
            functionRequestIdentifier,
            NtShareAccessToString(shareAccess).AsCString());

        if (functionNameView.contains(L"Create"))
        {
          Message::OutputFormatted(
              kDumpSeverity,
              L"%s(%u):   CreateDisposition = %s",
              functionName,
              functionRequestIdentifier,
              NtCreateDispositionToString(createDisposition).AsCString());
          Message::OutputFormatted(
              kDumpSeverity,
              L"%s(%u):   CreateOptions = %s",
              functionName,
              functionRequestIdentifier,
              NtCreateOrOpenOptionsToString(createOptions).AsCString());
        }
        else if (functionNameView.contains(L"Open"))
        {
          Message::OutputFormatted(
              kDumpSeverity,
              L"%s(%u):   OpenOptions = %s",
              functionName,
              functionRequestIdentifier,
              NtCreateOrOpenOptionsToString(createOptions).AsCString());
        }
      }
    }

    NTSTATUS EntryPointCloseHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        HANDLE handle,
        std::function<NTSTATUS(HANDLE)> underlyingSystemCallInvoker)
    {
      std::optional<OpenHandleStore::SHandleDataView> maybeClosedHandleData =
          OpenHandleStoreInstance().GetDataForHandle(handle);
      if (false == maybeClosedHandleData.has_value()) return underlyingSystemCallInvoker(handle);

      OpenHandleStore::SHandleData closedHandleData;
      NTSTATUS closeHandleResult =
          OpenHandleStoreInstance().RemoveAndCloseHandle(handle, &closedHandleData);

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

    std::optional<NTSTATUS> EntryPointDirectoryEnumeration(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        HANDLE fileHandle,
        HANDLE event,
        PIO_APC_ROUTINE apcRoutine,
        PVOID apcContext,
        PIO_STATUS_BLOCK ioStatusBlock,
        PVOID fileInformation,
        ULONG length,
        FILE_INFORMATION_CLASS fileInformationClass,
        ULONG queryFlags,
        PUNICODE_STRING fileName)
    {
      std::optional<FileInformationStructLayout> maybeFileInformationStructLayout =
          FileInformationStructLayout::LayoutForFileInformationClass(fileInformationClass);
      if (false == maybeFileInformationStructLayout.has_value()) return std::nullopt;

      std::optional<OpenHandleStore::SHandleDataView> maybeHandleData =
          OpenHandleStoreInstance().GetDataForHandle(fileHandle);
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
            FilesystemDirector::Singleton().GetInstructionForDirectoryEnumeration(
                maybeHandleData->associatedPath, maybeHandleData->realOpenedPath);

        if (true == Globals::GetConfigurationData().isDryRunMode)
        {
          // This will mark the directory enumeration object as present but no-op. Future
          // invocations will therefore not attempt to query for a directory enumeration instruction
          // and will just be forwarded to the system.
          OpenHandleStoreInstance().AssociateDirectoryEnumerationState(
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
        OpenHandleStoreInstance().AssociateDirectoryEnumerationState(
            fileHandle,
            std::move(directoryOperationQueueUniquePtr),
            *maybeFileInformationStructLayout);
        newDirectoryEnumerationCreated = true;
      }

      maybeHandleData = OpenHandleStoreInstance().GetDataForHandle(fileHandle);
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

    NTSTATUS EntryPointNewFileHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        PHANDLE fileHandle,
        ACCESS_MASK desiredAccess,
        POBJECT_ATTRIBUTES objectAttributes,
        ULONG shareAccess,
        ULONG createDisposition,
        ULONG createOptions,
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
          objectAttributes->RootDirectory,
          Strings::NtConvertUnicodeStringToStringView(*(objectAttributes->ObjectName)),
          FileAccessModeFromNtParameter(desiredAccess),
          CreateDispositionFromNtParameter(createDisposition));
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
      NTSTATUS systemCallResult = NtStatus::kInternalError;

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
                NtCreateDispositionToString(ntParamCreateDispositionToTry).AsCString(),
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
            newlyOpenedHandle,
            redirectionInstruction,
            lastAttemptedPath,
            unredirectedPath);

      *fileHandle = newlyOpenedHandle;
      return systemCallResult;
    }

    NTSTATUS EntryPointRenameByHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        HANDLE fileHandle,
        SFileRenameInformation& renameInformation,
        ULONG renameInformationLength,
        std::function<NTSTATUS(HANDLE, SFileRenameInformation&, ULONG)> underlyingSystemCallInvoker)
    {
      std::wstring_view unredirectedPath =
          FileInformationStructLayout::ReadFileNameByType(renameInformation);

      const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(
          functionName,
          functionRequestIdentifier,
          renameInformation.rootDirectory,
          unredirectedPath,
          FileAccessMode::Delete(),
          CreateDisposition::CreateNewFile());
      const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

      if (true == Globals::GetConfigurationData().isDryRunMode ||
          (FileOperationInstruction::NoRedirectionOrInterception() == redirectionInstruction))
        return underlyingSystemCallInvoker(fileHandle, renameInformation, renameInformationLength);

      NTSTATUS preOperationResult = ExecuteExtraPreOperations(
          functionName, functionRequestIdentifier, operationContext.instruction);
      if (!(NT_SUCCESS(preOperationResult))) return preOperationResult;

      // Due to how the file rename information structure is laid out, including an embedded
      // filename buffer of variable size, there is overhead to generating a new one. Without a
      // redirected filename present it is better to bail early than to generate a new one
      // unconditionally.
      if (false == redirectionInstruction.HasRedirectedFilename())
        return underlyingSystemCallInvoker(fileHandle, renameInformation, renameInformationLength);

      FileRenameInformationAndFilename redirectedFileRenameInformationAndFilename =
          CopyFileRenameInformationAndReplaceFilename(
              renameInformation, redirectionInstruction.GetRedirectedFilename());
      SFileRenameInformation& redirectedFileRenameInformation =
          redirectedFileRenameInformationAndFilename.GetFileRenameInformation();

      NTSTATUS systemCallResult = NtStatus::kInternalError;
      std::wstring_view lastAttemptedPath;

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

      if (true == lastAttemptedPath.empty())
        return underlyingSystemCallInvoker(fileHandle, renameInformation, renameInformationLength);

      if (NT_SUCCESS(systemCallResult))
        SelectFilenameAndUpdateOpenHandle(
            functionName,
            functionRequestIdentifier,
            fileHandle,
            redirectionInstruction,
            lastAttemptedPath,
            unredirectedPath);

      return systemCallResult;
    }

    NTSTATUS EntryPointQueryByObjectAttributes(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        FileAccessMode fileAccessMode,
        POBJECT_ATTRIBUTES objectAttributes,
        std::function<NTSTATUS(POBJECT_ATTRIBUTES)> underlyingSystemCallInvoker)
    {
      const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(
          functionName,
          functionRequestIdentifier,
          objectAttributes->RootDirectory,
          Strings::NtConvertUnicodeStringToStringView(*(objectAttributes->ObjectName)),
          fileAccessMode,
          CreateDisposition::OpenExistingFile());
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

      NTSTATUS systemCallResult = NtStatus::kInternalError;

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

    NTSTATUS EntryPointQueryNameByHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        HANDLE fileHandle,
        SFileNameInformation* fileNameInformation,
        ULONG fileNameInformationBufferCapacity,
        std::function<NTSTATUS(HANDLE)> underlyingSystemCallInvoker,
        std::function<std::wstring_view(std::wstring_view)> fileNameTransform)
    {
      NTSTATUS systemCallResult = underlyingSystemCallInvoker(fileHandle);
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

      // If the file handle is not stored, meaning it could not possibly be the result of a
      // redirection, then it is not necessary to replace the filename.
      std::optional<OpenHandleStore::SHandleDataView> maybeHandleData =
          OpenHandleStoreInstance().GetDataForHandle(fileHandle);
      if (false == maybeHandleData.has_value()) return systemCallResult;

      std::wstring_view systemReturnedFileName =
          FileInformationStructLayout::ReadFileNameByType(*fileNameInformation);
      std::wstring_view replacementFileName = fileNameTransform(maybeHandleData->associatedPath);
      if (replacementFileName == systemReturnedFileName) return systemCallResult;

      Message::OutputFormatted(
          Message::ESeverity::Debug,
          L"%s(%u): Invoked with handle %zu, the system returned path \"%.*s\", and it is being replaced with path \"%.*s\".",
          functionName,
          functionRequestIdentifier,
          reinterpret_cast<size_t>(fileHandle),
          static_cast<int>(systemReturnedFileName.length()),
          systemReturnedFileName.data(),
          static_cast<int>(replacementFileName.length()),
          replacementFileName.data());

      FileInformationStructLayout::WriteFileNameByType(
          *fileNameInformation, fileNameInformationBufferCapacity, replacementFileName);
      if (FileInformationStructLayout::ReadFileNameByType(*fileNameInformation).length() <
          replacementFileName.length())
        return NtStatus::kBufferOverflow;

      // If the original system call resulted in a buffer overflow, but the buffer was large enough
      // to hold the replacement filename, then the application should be told that the operation
      // succeeded. Any other return code should be passed back to the application without
      // modification.
      return (
          (NtStatus::kBufferOverflow == systemCallResult) ? NtStatus::kSuccess : systemCallResult);
    }

    TemporaryString NtAccessMaskToString(ACCESS_MASK accessMask)
    {
      constexpr std::wstring_view kSeparator = L" | ";
      TemporaryString outputString = Strings::FormatString(L"0x%08x (", accessMask);

      if (0 == accessMask)
      {
        outputString << L"none" << kSeparator;
      }
      else
      {
        if (FILE_ALL_ACCESS == (accessMask & FILE_ALL_ACCESS))
        {
          outputString << L"FILE_ALL_ACCESS" << kSeparator;
          accessMask &= (~(FILE_ALL_ACCESS));
        }

        if (FILE_GENERIC_READ == (accessMask & FILE_GENERIC_READ))
        {
          outputString << L"FILE_GENERIC_READ" << kSeparator;
          accessMask &= (~(FILE_GENERIC_READ));
        }

        if (FILE_GENERIC_WRITE == (accessMask & FILE_GENERIC_WRITE))
        {
          outputString << L"FILE_GENERIC_WRITE" << kSeparator;
          accessMask &= (~(FILE_GENERIC_WRITE));
        }

        if (FILE_GENERIC_EXECUTE == (accessMask & FILE_GENERIC_EXECUTE))
        {
          outputString << L"FILE_GENERIC_EXECUTE" << kSeparator;
          accessMask &= (~(FILE_GENERIC_EXECUTE));
        }

        if (0 != (accessMask & GENERIC_ALL)) outputString << L"GENERIC_ALL" << kSeparator;
        if (0 != (accessMask & GENERIC_READ)) outputString << L"GENERIC_READ" << kSeparator;
        if (0 != (accessMask & GENERIC_WRITE)) outputString << L"GENERIC_WRITE" << kSeparator;
        if (0 != (accessMask & GENERIC_EXECUTE)) outputString << L"GENERIC_EXECUTE" << kSeparator;
        if (0 != (accessMask & DELETE)) outputString << L"DELETE" << kSeparator;
        if (0 != (accessMask & FILE_READ_DATA)) outputString << L"FILE_READ_DATA" << kSeparator;
        if (0 != (accessMask & FILE_READ_ATTRIBUTES))
          outputString << L"FILE_READ_ATTRIBUTES" << kSeparator;
        if (0 != (accessMask & FILE_READ_EA)) outputString << L"FILE_READ_EA" << kSeparator;
        if (0 != (accessMask & READ_CONTROL)) outputString << L"READ_CONTROL" << kSeparator;
        if (0 != (accessMask & FILE_WRITE_DATA)) outputString << L"FILE_WRITE_DATA" << kSeparator;
        if (0 != (accessMask & FILE_WRITE_ATTRIBUTES))
          outputString << L"FILE_WRITE_ATTRIBUTES" << kSeparator;
        if (0 != (accessMask & FILE_WRITE_EA)) outputString << L"FILE_WRITE_EA" << kSeparator;
        if (0 != (accessMask & FILE_APPEND_DATA)) outputString << L"FILE_APPEND_DATA" << kSeparator;
        if (0 != (accessMask & WRITE_DAC)) outputString << L"WRITE_DAC" << kSeparator;
        if (0 != (accessMask & WRITE_OWNER)) outputString << L"WRITE_OWNER" << kSeparator;
        if (0 != (accessMask & SYNCHRONIZE)) outputString << L"SYNCHRONIZE" << kSeparator;
        if (0 != (accessMask & FILE_EXECUTE)) outputString << L"FILE_EXECUTE" << kSeparator;
        if (0 != (accessMask & FILE_LIST_DIRECTORY))
          outputString << L"FILE_LIST_DIRECTORY" << kSeparator;
        if (0 != (accessMask & FILE_TRAVERSE)) outputString << L"FILE_TRAVERSE" << kSeparator;

        accessMask &= (~(
            GENERIC_ALL | GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | DELETE | FILE_READ_DATA |
            FILE_READ_ATTRIBUTES | FILE_READ_EA | READ_CONTROL | FILE_WRITE_DATA |
            FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_APPEND_DATA | WRITE_DAC | WRITE_OWNER |
            SYNCHRONIZE | FILE_EXECUTE | FILE_LIST_DIRECTORY | FILE_TRAVERSE));
        if (0 != accessMask)
          outputString << Strings::FormatString(L"0x%08x", accessMask) << kSeparator;
      }

      outputString.RemoveSuffix(static_cast<unsigned int>(kSeparator.length()));
      outputString << L")";

      return outputString;
    }

    TemporaryString NtCreateDispositionToString(ULONG createDisposition)
    {
      constexpr wchar_t kFormatString[] = L"0x%08x (%s)";

      switch (createDisposition)
      {
        case FILE_SUPERSEDE:
          return Strings::FormatString(kFormatString, createDisposition, L"FILE_SUPERSEDE");
        case FILE_CREATE:
          return Strings::FormatString(kFormatString, createDisposition, L"FILE_CREATE");
        case FILE_OPEN:
          return Strings::FormatString(kFormatString, createDisposition, L"FILE_OPEN");
        case FILE_OPEN_IF:
          return Strings::FormatString(kFormatString, createDisposition, L"FILE_OPEN_IF");
        case FILE_OVERWRITE:
          return Strings::FormatString(kFormatString, createDisposition, L"FILE_OVERWRITE");
        case FILE_OVERWRITE_IF:
          return Strings::FormatString(kFormatString, createDisposition, L"FILE_OVERWRITE_IF");
        default:
          return Strings::FormatString(kFormatString, createDisposition, L"unknown");
      }
    }

    TemporaryString NtCreateOrOpenOptionsToString(ULONG createOrOpenOptions)
    {
      constexpr std::wstring_view kSeparator = L" | ";
      TemporaryString outputString = Strings::FormatString(L"0x%08x (", createOrOpenOptions);

      if (0 == createOrOpenOptions)
      {
        outputString << L"none" << kSeparator;
      }
      else
      {
        if (0 != (createOrOpenOptions & FILE_DIRECTORY_FILE))
          outputString << L"FILE_DIRECTORY_FILE" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_WRITE_THROUGH))
          outputString << L"FILE_WRITE_THROUGH" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_SEQUENTIAL_ONLY))
          outputString << L"FILE_SEQUENTIAL_ONLY" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_NO_INTERMEDIATE_BUFFERING))
          outputString << L"FILE_NO_INTERMEDIATE_BUFFERING" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_SYNCHRONOUS_IO_ALERT))
          outputString << L"FILE_SYNCHRONOUS_IO_ALERT" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_SYNCHRONOUS_IO_NONALERT))
          outputString << L"FILE_SYNCHRONOUS_IO_NONALERT" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_NON_DIRECTORY_FILE))
          outputString << L"FILE_NON_DIRECTORY_FILE" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_CREATE_TREE_CONNECTION))
          outputString << L"FILE_CREATE_TREE_CONNECTION" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_COMPLETE_IF_OPLOCKED))
          outputString << L"FILE_COMPLETE_IF_OPLOCKED" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_NO_EA_KNOWLEDGE))
          outputString << L"FILE_NO_EA_KNOWLEDGE" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_OPEN_REMOTE_INSTANCE))
          outputString << L"FILE_OPEN_REMOTE_INSTANCE" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_RANDOM_ACCESS))
          outputString << L"FILE_RANDOM_ACCESS" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_DELETE_ON_CLOSE))
          outputString << L"FILE_DELETE_ON_CLOSE" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_OPEN_BY_FILE_ID))
          outputString << L"FILE_OPEN_BY_FILE_ID" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_OPEN_FOR_BACKUP_INTENT))
          outputString << L"FILE_OPEN_FOR_BACKUP_INTENT" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_NO_COMPRESSION))
          outputString << L"FILE_NO_COMPRESSION" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_OPEN_REQUIRING_OPLOCK))
          outputString << L"FILE_OPEN_REQUIRING_OPLOCK" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_DISALLOW_EXCLUSIVE))
          outputString << L"FILE_DISALLOW_EXCLUSIVE" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_SESSION_AWARE))
          outputString << L"FILE_SESSION_AWARE" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_RESERVE_OPFILTER))
          outputString << L"FILE_RESERVE_OPFILTER" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_OPEN_REPARSE_POINT))
          outputString << L"FILE_OPEN_REPARSE_POINT" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_OPEN_NO_RECALL))
          outputString << L"FILE_OPEN_NO_RECALL" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_OPEN_FOR_FREE_SPACE_QUERY))
          outputString << L"FILE_OPEN_FOR_FREE_SPACE_QUERY" << kSeparator;
        if (0 != (createOrOpenOptions & FILE_CONTAINS_EXTENDED_CREATE_INFORMATION))
          outputString << L"FILE_CONTAINS_EXTENDED_CREATE_INFORMATION" << kSeparator;

        createOrOpenOptions &=
            (~(FILE_DIRECTORY_FILE | FILE_WRITE_THROUGH | FILE_SEQUENTIAL_ONLY |
               FILE_NO_INTERMEDIATE_BUFFERING | FILE_SYNCHRONOUS_IO_ALERT |
               FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE |
               FILE_CREATE_TREE_CONNECTION | FILE_COMPLETE_IF_OPLOCKED | FILE_NO_EA_KNOWLEDGE |
               FILE_OPEN_REMOTE_INSTANCE | FILE_RANDOM_ACCESS | FILE_DELETE_ON_CLOSE |
               FILE_OPEN_BY_FILE_ID | FILE_OPEN_FOR_BACKUP_INTENT | FILE_NO_COMPRESSION |
               FILE_OPEN_REQUIRING_OPLOCK | FILE_DISALLOW_EXCLUSIVE | FILE_SESSION_AWARE |
               FILE_RESERVE_OPFILTER | FILE_OPEN_REPARSE_POINT | FILE_OPEN_NO_RECALL |
               FILE_OPEN_FOR_FREE_SPACE_QUERY | FILE_CONTAINS_EXTENDED_CREATE_INFORMATION));
        if (0 != createOrOpenOptions)
          outputString << Strings::FormatString(L"0x%08x", createOrOpenOptions) << kSeparator;
      }

      outputString.RemoveSuffix(static_cast<unsigned int>(kSeparator.length()));
      outputString << L")";

      return outputString;
    }

    TemporaryString NtShareAccessToString(ULONG shareAccess)
    {
      constexpr std::wstring_view kSeparator = L" | ";
      TemporaryString outputString = Strings::FormatString(L"0x%08x (", shareAccess);

      if (0 == shareAccess)
      {
        outputString << L"none" << kSeparator;
      }
      else
      {
        if (0 != (shareAccess & FILE_SHARE_READ)) outputString << L"FILE_SHARE_READ" << kSeparator;
        if (0 != (shareAccess & FILE_SHARE_WRITE))
          outputString << L"FILE_SHARE_WRITE" << kSeparator;
        if (0 != (shareAccess & FILE_SHARE_DELETE))
          outputString << L"FILE_SHARE_DELETE" << kSeparator;

        shareAccess &= (~(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE));
        if (0 != shareAccess)
          outputString << Strings::FormatString(L"0x%08x", shareAccess) << kSeparator;
      }

      outputString.RemoveSuffix(static_cast<unsigned int>(kSeparator.length()));
      outputString << L")";

      return outputString;
    }

    NTSTATUS AdvanceDirectoryEnumerationOperation(
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

    FileRenameInformationAndFilename CopyFileRenameInformationAndReplaceFilename(
        const SFileRenameInformation& inputFileRenameInformation,
        std::wstring_view replacementFilename)
    {
      TemporaryVector<uint8_t> newFileRenameInformation;

      SFileRenameInformation outputFileRenameInformation = inputFileRenameInformation;
      outputFileRenameInformation.fileNameLength =
          (static_cast<ULONG>(replacementFilename.length()) * sizeof(wchar_t));

      for (size_t i = 0; i < offsetof(SFileRenameInformation, fileName); ++i)
        newFileRenameInformation.PushBack(
            (reinterpret_cast<const uint8_t*>(&outputFileRenameInformation))[i]);

      for (size_t i = 0; i < (replacementFilename.length() * sizeof(wchar_t)); ++i)
        newFileRenameInformation.PushBack(
            (reinterpret_cast<const uint8_t*>(replacementFilename.data()))[i]);

      return FileRenameInformationAndFilename(std::move(newFileRenameInformation));
    }

    CreateDisposition CreateDispositionFromNtParameter(ULONG ntCreateDisposition)
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

    NTSTATUS ExecuteExtraPreOperations(
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

    FileAccessMode FileAccessModeFromNtParameter(ACCESS_MASK ntDesiredAccess)
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

    void FillRedirectedObjectNameAndAttributesForInstruction(
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

    SFileOperationContext GetFileOperationRedirectionInformation(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        HANDLE rootDirectory,
        std::wstring_view inputFilename,
        FileAccessMode fileAccessMode,
        CreateDisposition createDisposition)
    {
      std::optional<TemporaryString> maybeRedirectedFilename = std::nullopt;
      std::optional<OpenHandleStore::SHandleDataView> maybeRootDirectoryHandleData =
          ((nullptr == rootDirectory) ? std::nullopt
                                      : OpenHandleStoreInstance().GetDataForHandle(rootDirectory));

      if (true == maybeRootDirectoryHandleData.has_value())
      {
        // Input object attributes structure specifies an open directory handle as the root
        // directory and the handle was found in the cache. Before querying for redirection it
        // is necessary to assemble the full filename, including the root directory path.

        std::wstring_view rootDirectoryHandlePath = maybeRootDirectoryHandleData->associatedPath;

        TemporaryString inputFullFilename;
        inputFullFilename << rootDirectoryHandlePath << L'\\' << inputFilename;

        FileOperationInstruction redirectionInstruction =
            Pathwinder::FilesystemDirector::Singleton().GetInstructionForFileOperation(
                inputFullFilename, fileAccessMode, createDisposition);
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
            Pathwinder::FilesystemDirector::Singleton().GetInstructionForFileOperation(
                inputFilename, fileAccessMode, createDisposition);

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

    std::optional<std::wstring_view> GetHandleAssociatedPath(HANDLE handle)
    {
      std::optional<OpenHandleStore::SHandleDataView> maybeHandleData =
          OpenHandleStoreInstance().GetDataForHandle(handle);

      if (false == maybeHandleData.has_value()) return std::nullopt;

      return maybeHandleData->associatedPath;
    }

    EInputOutputMode GetInputOutputModeForHandle(HANDLE handle)
    {
      SFileModeInformation modeInformation{};
      IO_STATUS_BLOCK unusedStatusBlock{};

      if (!(NT_SUCCESS(Hooks::ProtectedDependency::NtQueryInformationFile::SafeInvoke(
              handle,
              &unusedStatusBlock,
              &modeInformation,
              sizeof(modeInformation),
              SFileModeInformation::kFileInformationClass))))
        return EInputOutputMode::Unknown;

      switch (modeInformation.mode & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT))
      {
        case 0:
          return EInputOutputMode::Asynchronous;
        default:
          return EInputOutputMode::Synchronous;
      }
    }

    TCreateDispositionsList SelectCreateDispositionsToTry(
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

    template <typename FileObjectType> TFileOperationsList<FileObjectType>
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

    bool ShouldTryNextFilename(NTSTATUS systemCallResult)
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

    void SelectFilenameAndStoreNewlyOpenedHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
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

        OpenHandleStoreInstance().InsertHandle(
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

    void SelectFilenameAndUpdateOpenHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
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
          if (true == OpenHandleStoreInstance().RemoveHandle(handleToUpdate, &erasedHandleData))
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

        OpenHandleStoreInstance().InsertOrUpdateHandle(
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
  } // namespace FilesystemExecutor
} // namespace Pathwinder
