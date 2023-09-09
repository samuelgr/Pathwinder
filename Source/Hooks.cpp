/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file Hooks.cpp
 *   Implementation of all Windows API hook functions used to implement path
 *   redirection.
 **************************************************************************************************/

#include "Hooks.h"

#include <atomic>
#include <optional>

#include <Hookshot/DynamicHook.h>

#include "ApiWindowsInternal.h"
#include "FileInformationStruct.h"
#include "FilesystemExecutor.h"
#include "FilesystemInstruction.h"
#include "FilesystemOperations.h"
#include "Globals.h"
#include "Message.h"
#include "Strings.h"

/// Retrieves an identifier for a particular invocation of a hook function.
/// Used exclusively for logging.
/// @return Numeric identifier for an invocation.
static inline unsigned int GetRequestIdentifier(void)
{
  static std::atomic<unsigned int> nextRequestIdentifier;
  return nextRequestIdentifier.fetch_add(1, std::memory_order::relaxed);
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtClose::Hook(HANDLE Handle)
{
  auto maybeHookFunctionResult = Pathwinder::FilesystemExecutor::EntryPointCloseHandle(
      GetFunctionName(), GetRequestIdentifier(), Handle);

  if (false == maybeHookFunctionResult.has_value()) return Original(Handle);

  return *maybeHookFunctionResult;
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtCreateFile::Hook(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    PLARGE_INTEGER AllocationSize,
    ULONG FileAttributes,
    ULONG ShareAccess,
    ULONG CreateDisposition,
    ULONG CreateOptions,
    PVOID EaBuffer,
    ULONG EaLength)
{
  auto maybeHookFunctionResult = Pathwinder::FilesystemExecutor::EntryPointNewFileHandle(
      GetFunctionName(),
      GetRequestIdentifier(),
      FileHandle,
      DesiredAccess,
      ObjectAttributes,
      IoStatusBlock,
      AllocationSize,
      FileAttributes,
      ShareAccess,
      CreateDisposition,
      CreateOptions,
      EaBuffer,
      EaLength);

  if (false == maybeHookFunctionResult.has_value())
    return Original(
        FileHandle,
        DesiredAccess,
        ObjectAttributes,
        IoStatusBlock,
        AllocationSize,
        FileAttributes,
        ShareAccess,
        CreateDisposition,
        CreateOptions,
        EaBuffer,
        EaLength);

  return *maybeHookFunctionResult;
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtDeleteFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes)
{
  // TODO
  return Original(ObjectAttributes);
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtOpenFile::Hook(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    ULONG ShareAccess,
    ULONG OpenOptions)
{
  auto maybeHookFunctionResult = Pathwinder::FilesystemExecutor::EntryPointNewFileHandle(
      GetFunctionName(),
      GetRequestIdentifier(),
      FileHandle,
      DesiredAccess,
      ObjectAttributes,
      IoStatusBlock,
      nullptr,
      0,
      ShareAccess,
      FILE_OPEN,
      OpenOptions,
      nullptr,
      0);

  if (false == maybeHookFunctionResult.has_value())
    return Original(
        FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);

  return *maybeHookFunctionResult;
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFile::Hook(
    HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass,
    BOOLEAN ReturnSingleEntry,
    PUNICODE_STRING FileName,
    BOOLEAN RestartScan)
{
  ULONG queryFlags = 0;
  if (RestartScan != 0) queryFlags |= Pathwinder::QueryFlag::kRestartScan;
  if (ReturnSingleEntry != 0) queryFlags |= Pathwinder::QueryFlag::kReturnSingleEntry;

  auto maybeHookFunctionResult = Pathwinder::FilesystemExecutor::EntryPointDirectoryEnumeration(
      GetFunctionName(),
      GetRequestIdentifier(),
      FileHandle,
      Event,
      ApcRoutine,
      ApcContext,
      IoStatusBlock,
      FileInformation,
      Length,
      FileInformationClass,
      queryFlags,
      FileName);

  if (false == maybeHookFunctionResult.has_value())
    return Original(
        FileHandle,
        Event,
        ApcRoutine,
        ApcContext,
        IoStatusBlock,
        FileInformation,
        Length,
        FileInformationClass,
        ReturnSingleEntry,
        FileName,
        RestartScan);

  return *maybeHookFunctionResult;
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFileEx::Hook(
    HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass,
    ULONG QueryFlags,
    PUNICODE_STRING FileName)
{
  auto maybeHookFunctionResult = Pathwinder::FilesystemExecutor::EntryPointDirectoryEnumeration(
      GetFunctionName(),
      GetRequestIdentifier(),
      FileHandle,
      Event,
      ApcRoutine,
      ApcContext,
      IoStatusBlock,
      FileInformation,
      Length,
      FileInformationClass,
      QueryFlags,
      FileName);

  if (false == maybeHookFunctionResult.has_value())
    return Original(
        FileHandle,
        Event,
        ApcRoutine,
        ApcContext,
        IoStatusBlock,
        FileInformation,
        Length,
        FileInformationClass,
        QueryFlags,
        FileName);

  return *maybeHookFunctionResult;
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationFile::Hook(
    HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass)
{
  using namespace Pathwinder;

  NTSTATUS systemCallResult =
      Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
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

  SFileNameInformation* fileNameInformation = nullptr;
  switch (FileInformationClass)
  {
    case SFileNameInformation::kFileInformationClass:
      fileNameInformation = reinterpret_cast<SFileNameInformation*>(FileInformation);
      break;

    case SFileAllInformation::kFileInformationClass:
      fileNameInformation =
          &(reinterpret_cast<SFileAllInformation*>(FileInformation)->nameInformation);
      break;

    default:
      return systemCallResult;
  }

  // If the buffer is not big enough to hold any part of the filename then it is not necessary to
  // try replacing it.
  const size_t fileNameInformationBufferOffset =
      reinterpret_cast<size_t>(fileNameInformation) - reinterpret_cast<size_t>(FileInformation);
  if ((fileNameInformationBufferOffset + offsetof(SFileNameInformation, fileName)) >=
      static_cast<size_t>(Length))
    return systemCallResult;

  // The `NtQueryInformationFile` always returns full path and file name information beginning with
  // a backslash character, omitting the drive letter.
  std::wstring_view systemReturnedFileName = GetFileInformationStructFilename(*fileNameInformation);
  if (false == systemReturnedFileName.starts_with(L'\\')) return systemCallResult;

  auto maybeHandleAssociatedPath = FilesystemExecutor::GetHandleAssociatedPath(FileHandle);
  if (false == maybeHandleAssociatedPath.has_value()) return systemCallResult;

  // A stored path has a Windows namespace prefix and a drive letter, both of which need to be
  // removed to comply with the `NtQueryInformationFile` returned path documentation.
  std::wstring_view replacementFileName = *maybeHandleAssociatedPath;
  replacementFileName.remove_prefix(
      Strings::PathGetWindowsNamespacePrefix(replacementFileName).length());
  replacementFileName.remove_prefix(replacementFileName.find_first_of(L'\\'));
  if (replacementFileName == systemReturnedFileName) return systemCallResult;

  const unsigned int requestIdentifier = GetRequestIdentifier();

  Message::OutputFormatted(
      Message::ESeverity::Debug,
      L"%s(%u): Invoked with handle %zu, the system returned path \"%.*s\", and it is being replaced with path \"%.*s\".",
      GetFunctionName(),
      requestIdentifier,
      reinterpret_cast<size_t>(FileHandle),
      static_cast<int>(systemReturnedFileName.length()),
      systemReturnedFileName.data(),
      static_cast<int>(replacementFileName.length()),
      replacementFileName.data());

  const size_t numReplacementCharsWritten = SetFileInformationStructFilename(
      *fileNameInformation,
      (static_cast<size_t>(Length) - fileNameInformationBufferOffset),
      replacementFileName);
  if (numReplacementCharsWritten < replacementFileName.length()) return NtStatus::kBufferOverflow;

  // If the original system call resulted in a buffer overflow, but the buffer was large enough to
  // hold the replacement filename, then the application should be told that the operation
  // succeeded. Any other return code should be passed back to the application without
  // modification.
  return ((NtStatus::kBufferOverflow == systemCallResult) ? NtStatus::kSuccess : systemCallResult);
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationByName::Hook(
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass)
{
  using namespace Pathwinder;

  const unsigned int requestIdentifier = GetRequestIdentifier();

  const FilesystemExecutor::SFileOperationContext operationContext =
      FilesystemExecutor::GetFileOperationRedirectionInformation(
          GetFunctionName(),
          requestIdentifier,
          ObjectAttributes->RootDirectory,
          Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)),
          FileAccessMode::ReadOnly(),
          CreateDisposition::OpenExistingFile());
  const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

  if (true == Globals::GetConfigurationData().isDryRunMode)
    return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

  NTSTATUS preOperationResult = FilesystemExecutor::ExecuteExtraPreOperations(
      GetFunctionName(), requestIdentifier, operationContext.instruction);
  if (!(NT_SUCCESS(preOperationResult))) return preOperationResult;

  FilesystemExecutor::SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
  FilesystemExecutor::FillRedirectedObjectNameAndAttributesForInstruction(
      redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

  std::wstring_view lastAttemptedPath;

  NTSTATUS systemCallResult = NtStatus::kInternalError;

  for (const auto& operationToTry : FilesystemExecutor::SelectFileOperationsToTry(
           GetFunctionName(),
           requestIdentifier,
           redirectionInstruction,
           *ObjectAttributes,
           redirectedObjectNameAndAttributes.objectAttributes))
  {
    if (true == operationToTry.HasError())
    {
      Message::OutputFormatted(
          Message::ESeverity::SuperDebug,
          L"%s(%u): NTSTATUS = 0x%08x (forced result).",
          GetFunctionName(),
          requestIdentifier,
          static_cast<unsigned int>(operationToTry.Error()));
      return operationToTry.Error();
    }
    else
    {
      const POBJECT_ATTRIBUTES objectAttributesToTry = operationToTry.Value();

      lastAttemptedPath =
          Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
      systemCallResult = Original(
          objectAttributesToTry, IoStatusBlock, FileInformation, Length, FileInformationClass);
      Message::OutputFormatted(
          Message::ESeverity::SuperDebug,
          L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".",
          GetFunctionName(),
          requestIdentifier,
          systemCallResult,
          static_cast<int>(lastAttemptedPath.length()),
          lastAttemptedPath.data());

      if (false == FilesystemExecutor::ShouldTryNextFilename(systemCallResult)) break;
    }
  }

  if (true == lastAttemptedPath.empty())
    return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

  return systemCallResult;
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtSetInformationFile::Hook(
    HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass)
{
  using namespace Pathwinder;

  // This invocation is only interesting if it is a rename operation. Otherwise there is no change
  // being made to the input file handle, which is already open.
  if (SFileRenameInformation::kFileInformationClass != FileInformationClass)
    return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

  const unsigned int requestIdentifier = GetRequestIdentifier();

  SFileRenameInformation& unredirectedFileRenameInformation =
      *(reinterpret_cast<SFileRenameInformation*>(FileInformation));
  std::wstring_view unredirectedPath =
      GetFileInformationStructFilename(unredirectedFileRenameInformation);

  const FilesystemExecutor::SFileOperationContext operationContext =
      FilesystemExecutor::GetFileOperationRedirectionInformation(
          GetFunctionName(),
          requestIdentifier,
          unredirectedFileRenameInformation.rootDirectory,
          unredirectedPath,
          FileAccessMode::Delete(),
          CreateDisposition::CreateNewFile());
  const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

  if (true == Globals::GetConfigurationData().isDryRunMode)
    return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

  NTSTATUS preOperationResult = FilesystemExecutor::ExecuteExtraPreOperations(
      GetFunctionName(), requestIdentifier, operationContext.instruction);
  if (!(NT_SUCCESS(preOperationResult))) return preOperationResult;

  // Due to how the file rename information structure is laid out, including an embedded filename
  // buffer of variable size, there is overhead to generating a new one. Without a redirected
  // filename present it is better to bail early than to generate a new one unconditionally.
  if (false == redirectionInstruction.HasRedirectedFilename())
    return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

  FilesystemExecutor::FileRenameInformationAndFilename redirectedFileRenameInformationAndFilename =
      FilesystemExecutor::CopyFileRenameInformationAndReplaceFilename(
          unredirectedFileRenameInformation, redirectionInstruction.GetRedirectedFilename());
  SFileRenameInformation& redirectedFileRenameInformation =
      redirectedFileRenameInformationAndFilename.GetFileRenameInformation();

  NTSTATUS systemCallResult = NtStatus::kInternalError;
  std::wstring_view lastAttemptedPath;

  for (const auto& operationToTry : FilesystemExecutor::SelectFileOperationsToTry(
           GetFunctionName(),
           requestIdentifier,
           redirectionInstruction,
           unredirectedFileRenameInformation,
           redirectedFileRenameInformation))
  {
    if (true == operationToTry.HasError())
    {
      Message::OutputFormatted(
          Message::ESeverity::SuperDebug,
          L"%s(%u): NTSTATUS = 0x%08x (forced result).",
          GetFunctionName(),
          requestIdentifier,
          static_cast<unsigned int>(operationToTry.Error()));
      return operationToTry.Error();
    }
    else
    {
      SFileRenameInformation* const renameInformationToTry = operationToTry.Value();

      lastAttemptedPath = GetFileInformationStructFilename(*renameInformationToTry);
      systemCallResult = Original(
          FileHandle,
          IoStatusBlock,
          reinterpret_cast<PVOID>(renameInformationToTry),
          Length,
          FileInformationClass);
      Message::OutputFormatted(
          Message::ESeverity::SuperDebug,
          L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".",
          GetFunctionName(),
          requestIdentifier,
          systemCallResult,
          static_cast<int>(lastAttemptedPath.length()),
          lastAttemptedPath.data());

      if (false == FilesystemExecutor::ShouldTryNextFilename(systemCallResult)) break;
    }
  }

  if (true == lastAttemptedPath.empty())
    return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

  if (NT_SUCCESS(systemCallResult))
    FilesystemExecutor::SelectFilenameAndUpdateOpenHandle(
        GetFunctionName(),
        requestIdentifier,
        FileHandle,
        redirectionInstruction,
        lastAttemptedPath,
        unredirectedPath);

  return systemCallResult;
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryAttributesFile::Hook(
    POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFO FileInformation)
{
  using namespace Pathwinder;

  const unsigned int requestIdentifier = GetRequestIdentifier();

  const FilesystemExecutor::SFileOperationContext operationContext =
      FilesystemExecutor::GetFileOperationRedirectionInformation(
          GetFunctionName(),
          requestIdentifier,
          ObjectAttributes->RootDirectory,
          Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)),
          FileAccessMode::ReadOnly(),
          CreateDisposition::OpenExistingFile());
  const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

  if (true == Globals::GetConfigurationData().isDryRunMode)
    return Original(ObjectAttributes, FileInformation);

  NTSTATUS preOperationResult = FilesystemExecutor::ExecuteExtraPreOperations(
      GetFunctionName(), requestIdentifier, operationContext.instruction);
  if (!(NT_SUCCESS(preOperationResult))) return preOperationResult;

  FilesystemExecutor::SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
  FilesystemExecutor::FillRedirectedObjectNameAndAttributesForInstruction(
      redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

  std::wstring_view lastAttemptedPath;

  NTSTATUS systemCallResult = NtStatus::kInternalError;

  for (const auto& operationToTry : FilesystemExecutor::SelectFileOperationsToTry(
           GetFunctionName(),
           requestIdentifier,
           redirectionInstruction,
           *ObjectAttributes,
           redirectedObjectNameAndAttributes.objectAttributes))
  {
    if (true == operationToTry.HasError())
    {
      Message::OutputFormatted(
          Message::ESeverity::SuperDebug,
          L"%s(%u): NTSTATUS = 0x%08x (forced result).",
          GetFunctionName(),
          requestIdentifier,
          static_cast<unsigned int>(operationToTry.Error()));
      return operationToTry.Error();
    }
    else
    {
      const POBJECT_ATTRIBUTES objectAttributesToTry = operationToTry.Value();

      lastAttemptedPath =
          Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
      systemCallResult = Original(objectAttributesToTry, FileInformation);
      Message::OutputFormatted(
          Message::ESeverity::SuperDebug,
          L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".",
          GetFunctionName(),
          requestIdentifier,
          systemCallResult,
          static_cast<int>(lastAttemptedPath.length()),
          lastAttemptedPath.data());

      if (false == FilesystemExecutor::ShouldTryNextFilename(systemCallResult)) break;
    }
  }

  if (true == lastAttemptedPath.empty()) return Original(ObjectAttributes, FileInformation);

  return systemCallResult;
}
