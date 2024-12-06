/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file Hooks.cpp
 *   Implementation of all Windows API hook functions used to implement path
 *   redirection.
 **************************************************************************************************/

#include "Hooks.h"

#include <atomic>
#include <functional>
#include <optional>

#include <Hookshot/DynamicHook.h>
#include <Infra/Core/DebugAssert.h>
#include <Infra/Core/Message.h>
#include <Infra/Core/TemporaryBuffer.h>

#include "ApiWindows.h"
#include "FileInformationStruct.h"
#include "FilesystemDirector.h"
#include "FilesystemExecutor.h"
#include "FilesystemInstruction.h"
#include "FilesystemOperations.h"
#include "OpenHandleStore.h"
#include "Strings.h"

/// Retrieves an identifier for a particular invocation of a hook function.
/// Used exclusively for logging.
/// @return Numeric identifier for an invocation.
static inline unsigned int GetRequestIdentifier(void)
{
  static std::atomic<unsigned int> nextRequestIdentifier;
  return nextRequestIdentifier.fetch_add(1, std::memory_order::relaxed);
}

/// Retrieves a reference to the filesystem director object instance. It is maintained on the heap
/// so it is not destroyed automatically by the runtime on program exit.
/// @return Mutable reference to the filesystem director object instance.
static inline Pathwinder::FilesystemDirector& FilesystemDirectorInstance(void)
{
  static Pathwinder::FilesystemDirector* const filesystemDirector =
      new Pathwinder::FilesystemDirector;
  return *filesystemDirector;
}

/// Retrieves a reference to the open handle store instance. It is maintained on the heap so it
/// is not destroyed automatically by the runtime on program exit.
/// @return Mutable reference to the open handle store instance.
static inline Pathwinder::OpenHandleStore& OpenHandleStoreInstance(void)
{
  static Pathwinder::OpenHandleStore* const openHandleStore = new Pathwinder::OpenHandleStore;
  return *openHandleStore;
}

/// Instruction source function for obtaining directory enumeration instructions using the singleton
/// filesystem director object instance.
static Pathwinder::DirectoryEnumerationInstruction InstructionSourceForDirectoryEnumeration(
    std::wstring_view associatedPath, std::wstring_view realOpenedPath)
{
  return FilesystemDirectorInstance().GetInstructionForDirectoryEnumeration(
      associatedPath, realOpenedPath);
}

/// Instruction source function for obtaining file operation instructions using the singleton
/// filesystem director object instance.
static Pathwinder::FileOperationInstruction InstructionSourceForFileOperation(
    std::wstring_view absoluteFilePath,
    Pathwinder::FileAccessMode fileAccessMode,
    Pathwinder::CreateDisposition createDisposition)
{
  return FilesystemDirectorInstance().GetInstructionForFileOperation(
      absoluteFilePath, fileAccessMode, createDisposition);
}

void Pathwinder::Hooks::SetFilesystemDirectorInstance(
    Pathwinder::FilesystemDirector&& filesystemDirector)
{
  FilesystemDirectorInstance() = std::move(filesystemDirector);
}

void Pathwinder::Hooks::ReinitializeCurrentDirectory(void)
{
  Infra::TemporaryString currentDirectory;
  currentDirectory.UnsafeSetSize(
      GetCurrentDirectory(currentDirectory.Capacity(), currentDirectory.Data()));
  if (true == currentDirectory.Empty())
  {
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Error,
        L"Failed to determine the current working directory (GetLastError() = %u).",
        GetLastError());
    return;
  }

  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Info,
      L"Current working directory is \"%s\".",
      currentDirectory.AsCString());

  Infra::TemporaryString tempDirectory;
  tempDirectory.UnsafeSetSize(GetTempPath2(tempDirectory.Capacity(), tempDirectory.Data()));
  if (true == tempDirectory.Empty())
  {
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Error,
        L"Failed to determine the current user's temporary directory (GetLastError() = %u).",
        GetLastError());
    return;
  }

  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Info,
      L"Current user's temporary directory is \"%s\".",
      tempDirectory.AsCString());

  if (0 == SetCurrentDirectory(tempDirectory.AsCString()))
  {
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Error,
        L"Failed to set the current working directory to the current user's temporary directory (GetLastError() = %u).",
        GetLastError());
    return;
  }

  if (0 == SetCurrentDirectory(currentDirectory.AsCString()))
  {
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Error,
        L"Failed to set the current working directory back to the original working directory (GetLastError() = %u).",
        GetLastError());
    return;
  }
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtClose::Hook(HANDLE Handle)
{
  return Pathwinder::FilesystemExecutor::CloseHandle(
      GetFunctionName(),
      GetRequestIdentifier(),
      OpenHandleStoreInstance(),
      Handle,
      [](HANDLE handle) -> NTSTATUS
      {
        return Original(handle);
      });
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
  return Pathwinder::FilesystemExecutor::NewFileHandle(
      GetFunctionName(),
      GetRequestIdentifier(),
      OpenHandleStoreInstance(),
      FileHandle,
      DesiredAccess,
      ObjectAttributes,
      ShareAccess,
      CreateDisposition,
      CreateOptions,
      InstructionSourceForFileOperation,
      [DesiredAccess,
       IoStatusBlock,
       AllocationSize,
       FileAttributes,
       ShareAccess,
       CreateOptions,
       EaBuffer,
       EaLength](PHANDLE fileHandle, POBJECT_ATTRIBUTES objectAttributes, ULONG createDisposition)
          -> NTSTATUS
      {
        return Original(
            fileHandle,
            DesiredAccess,
            objectAttributes,
            IoStatusBlock,
            AllocationSize,
            FileAttributes,
            ShareAccess,
            createDisposition,
            CreateOptions,
            EaBuffer,
            EaLength);
      });
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtDeleteFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes)
{
  return Pathwinder::FilesystemExecutor::QueryByObjectAttributes(
      GetFunctionName(),
      GetRequestIdentifier(),
      OpenHandleStoreInstance(),
      ObjectAttributes,
      DELETE,
      InstructionSourceForFileOperation,
      [](POBJECT_ATTRIBUTES ObjectAttributes) -> NTSTATUS
      {
        return Original(ObjectAttributes);
      });
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtOpenFile::Hook(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    ULONG ShareAccess,
    ULONG OpenOptions)
{
  const wchar_t* const functionName = GetFunctionName();
  const unsigned int requestIdentifier = GetRequestIdentifier();

  return Pathwinder::FilesystemExecutor::NewFileHandle(
      functionName,
      requestIdentifier,
      OpenHandleStoreInstance(),
      FileHandle,
      DesiredAccess,
      ObjectAttributes,
      ShareAccess,
      FILE_OPEN,
      OpenOptions,
      InstructionSourceForFileOperation,
      [functionName, requestIdentifier, DesiredAccess, IoStatusBlock, ShareAccess, OpenOptions](
          PHANDLE fileHandle,
          POBJECT_ATTRIBUTES objectAttributes,
          ULONG createDisposition) -> NTSTATUS
      {
        // `NtOpenFile` only supports a single create disposition, `FILE_OPEN`, because its
        // semantics are by definition to open a file that already exists. The top-level hook
        // function also hard-codes the create disposition as `FILE_OPEN`, so it is reasonable to
        // expect that same value to be present here too.
        if (FILE_OPEN != createDisposition)
        {
          Infra::Message::OutputFormatted(
              Infra::Message::ESeverity::Error,
              L"%s(%u): Internal error: Invoked with an invalid create disposition (0x%08x).",
              functionName,
              requestIdentifier,
              createDisposition);
          return Pathwinder::NtStatus::kInvalidParameter;
        }

        DebugAssert(FILE_OPEN == createDisposition, "Invalid create disposition for NtOpenFile.");

        return Original(
            fileHandle, DesiredAccess, objectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
      });
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
  if (RestartScan != 0) queryFlags |= SL_RESTART_SCAN;
  if (ReturnSingleEntry != 0) queryFlags |= SL_RETURN_SINGLE_ENTRY;

  const std::optional<NTSTATUS> prepareResult =
      Pathwinder::FilesystemExecutor::DirectoryEnumerationPrepare(
          GetFunctionName(),
          GetRequestIdentifier(),
          OpenHandleStoreInstance(),
          FileHandle,
          FileInformation,
          Length,
          FileInformationClass,
          FileName,
          InstructionSourceForDirectoryEnumeration);

  if (false == prepareResult.has_value())
  {
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
  }
  else if (Pathwinder::NtStatus::kSuccess != *prepareResult)
  {
    // Failures encountered during the preparation stage indicate a problem with the parameters.
    // Based on the observed behavior of Windows itself, an appropriate status code is returned to
    // the application without touching the I/O status block. This is true irrespective of whether
    // the directory enumeration is supposed to be synchronous or asynchronous.

    return *prepareResult;
  }

  return Pathwinder::FilesystemExecutor::DirectoryEnumerationAdvance(
      GetFunctionName(),
      GetRequestIdentifier(),
      OpenHandleStoreInstance(),
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
  const std::optional<NTSTATUS> prepareResult =
      Pathwinder::FilesystemExecutor::DirectoryEnumerationPrepare(
          GetFunctionName(),
          GetRequestIdentifier(),
          OpenHandleStoreInstance(),
          FileHandle,
          FileInformation,
          Length,
          FileInformationClass,
          FileName,
          InstructionSourceForDirectoryEnumeration);

  if (false == prepareResult.has_value())
  {
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
  }
  else if (Pathwinder::NtStatus::kSuccess != *prepareResult)
  {
    // Failures encountered during the preparation stage indicate a problem with the parameters.
    // Based on the observed behavior of Windows itself, an appropriate status code is returned to
    // the application without touching the I/O status block. This is true irrespective of whether
    // the directory enumeration is supposed to be synchronous or asynchronous.

    return *prepareResult;
  }

  return Pathwinder::FilesystemExecutor::DirectoryEnumerationAdvance(
      GetFunctionName(),
      GetRequestIdentifier(),
      OpenHandleStoreInstance(),
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
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationFile::Hook(
    HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass)
{
  return Pathwinder::FilesystemExecutor::QueryByHandle(
      GetFunctionName(),
      GetRequestIdentifier(),
      OpenHandleStoreInstance(),
      FileHandle,
      IoStatusBlock,
      FileInformation,
      Length,
      FileInformationClass,
      [](HANDLE fileHandle,
         PIO_STATUS_BLOCK ioStatusBlock,
         PVOID fileInformation,
         ULONG length,
         FILE_INFORMATION_CLASS fileInformationClass) -> NTSTATUS
      {
        return Original(fileHandle, ioStatusBlock, fileInformation, length, fileInformationClass);
      },
      [](std::wstring_view proposedReplacementFileName) -> std::wstring_view
      {
        // When queried such that it returns a full path, the `NtQueryInformationFile` returns the
        // full path beginning with a backslash character, omitting the drive letter.

        if (Pathwinder::Strings::PathBeginsWithDriveLetter(proposedReplacementFileName))
        {
          const size_t firstBackslashPosition = proposedReplacementFileName.find_first_of(L'\\');
          if (std::wstring_view::npos != firstBackslashPosition)
            proposedReplacementFileName.remove_prefix(firstBackslashPosition);
        }

        return proposedReplacementFileName;
      });
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationByName::Hook(
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass)
{
  return Pathwinder::FilesystemExecutor::QueryByObjectAttributes(
      GetFunctionName(),
      GetRequestIdentifier(),
      OpenHandleStoreInstance(),
      ObjectAttributes,
      GENERIC_READ,
      InstructionSourceForFileOperation,
      [IoStatusBlock, FileInformation, Length, FileInformationClass](
          POBJECT_ATTRIBUTES ObjectAttributes) -> NTSTATUS
      {
        return Original(
            ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
      });
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtSetInformationFile::Hook(
    HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass)
{
  if (Pathwinder::SFileRenameInformation::kFileInformationClass != FileInformationClass)
    return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

  return Pathwinder::FilesystemExecutor::RenameByHandle(
      GetFunctionName(),
      GetRequestIdentifier(),
      OpenHandleStoreInstance(),
      FileHandle,
      *reinterpret_cast<Pathwinder::SFileRenameInformation*>(FileInformation),
      Length,
      InstructionSourceForFileOperation,
      [IoStatusBlock, FileInformationClass](
          HANDLE fileHandle,
          Pathwinder::SFileRenameInformation& renameInformation,
          ULONG renameInformationLength) -> NTSTATUS
      {
        return Original(
            fileHandle,
            IoStatusBlock,
            &renameInformation,
            renameInformationLength,
            FileInformationClass);
      });
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryAttributesFile::Hook(
    POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFO FileInformation)
{
  return Pathwinder::FilesystemExecutor::QueryByObjectAttributes(
      GetFunctionName(),
      GetRequestIdentifier(),
      OpenHandleStoreInstance(),
      ObjectAttributes,
      GENERIC_READ,
      InstructionSourceForFileOperation,
      [FileInformation](POBJECT_ATTRIBUTES ObjectAttributes) -> NTSTATUS
      {
        return Original(ObjectAttributes, FileInformation);
      });
}

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryFullAttributesFile::Hook(
    POBJECT_ATTRIBUTES ObjectAttributes, Pathwinder::SFileNetworkOpenInformation* FileInformation)
{
  return Pathwinder::FilesystemExecutor::QueryByObjectAttributes(
      GetFunctionName(),
      GetRequestIdentifier(),
      OpenHandleStoreInstance(),
      ObjectAttributes,
      GENERIC_READ,
      InstructionSourceForFileOperation,
      [FileInformation](POBJECT_ATTRIBUTES ObjectAttributes) -> NTSTATUS
      {
        return Original(ObjectAttributes, FileInformation);
      });
}
