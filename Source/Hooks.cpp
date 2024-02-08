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
#include <functional>
#include <optional>

#include <Hookshot/DynamicHook.h>

#include "ApiWindows.h"
#include "DebugAssert.h"
#include "FileInformationStruct.h"
#include "FilesystemDirector.h"
#include "FilesystemExecutor.h"
#include "FilesystemInstruction.h"
#include "FilesystemOperations.h"
#include "Globals.h"
#include "Message.h"
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
          Pathwinder::Message::OutputFormatted(
              Pathwinder::Message::ESeverity::Error,
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

  auto maybeHookFunctionResult = Pathwinder::FilesystemExecutor::DirectoryEnumeration(
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
      FileName,
      InstructionSourceForDirectoryEnumeration);

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
  auto maybeHookFunctionResult = Pathwinder::FilesystemExecutor::DirectoryEnumeration(
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
      FileName,
      InstructionSourceForDirectoryEnumeration);

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
  // This enumerator does not have an associated structure but additionally uses
  // `FILE_NAME_INFORMATION` (internally `SFileNameInformation`) to request file name information.
  // For more information in this enumerator, see
  // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationfile.
  constexpr FILE_INFORMATION_CLASS kFileInformationClassNormalizedName =
      static_cast<FILE_INFORMATION_CLASS>(48);

  Pathwinder::SFileNameInformation* fileNameInformation = nullptr;

  // There are only three file information classes that result in the filename being identified. Any
  // other file information class is uninteresting, and in those cases the query does not need to be
  // intercepted.
  switch (FileInformationClass)
  {
    case Pathwinder::SFileNameInformation::kFileInformationClass:
    case kFileInformationClassNormalizedName:
      fileNameInformation = reinterpret_cast<Pathwinder::SFileNameInformation*>(FileInformation);
      break;

    case Pathwinder::SFileAllInformation::kFileInformationClass:
      fileNameInformation =
          &(reinterpret_cast<Pathwinder::SFileAllInformation*>(FileInformation)->nameInformation);
      break;

    default:
      return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
  }

  const ULONG fileNameInformationBufferCapacity = Length -
      static_cast<ULONG>(reinterpret_cast<size_t>(fileNameInformation) -
                         reinterpret_cast<size_t>(FileInformation));

  return Pathwinder::FilesystemExecutor::QueryNameByHandle(
      GetFunctionName(),
      GetRequestIdentifier(),
      OpenHandleStoreInstance(),
      FileHandle,
      fileNameInformation,
      fileNameInformationBufferCapacity,
      [IoStatusBlock, Length, FileInformationClass](
          HANDLE fileHandle, Pathwinder::SFileNameInformation* fileNameInformation) -> NTSTATUS
      {
        return Original(
            fileHandle, IoStatusBlock, fileNameInformation, Length, FileInformationClass);
      },
      [](std::wstring_view systemReturnedFileName,
         std::wstring_view proposedReplacementFileName) -> std::optional<std::wstring_view>
      {
        // When queried such that it returns a full path, the `NtQueryInformationFile` returns full
        // path and file name information beginning with a backslash character, omitting the drive
        // letter. Thus, if the system-returned string does not begin with a backslash, it is not
        // a full path and is therefore uninteresting.
        if (false == systemReturnedFileName.starts_with(L'\\')) return std::nullopt;
        proposedReplacementFileName.remove_prefix(
            Pathwinder::Strings::PathGetWindowsNamespacePrefix(proposedReplacementFileName)
                .length());
        proposedReplacementFileName.remove_prefix(proposedReplacementFileName.find_first_of(L'\\'));
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
      InstructionSourceForFileOperation,
      [FileInformation](POBJECT_ATTRIBUTES ObjectAttributes) -> NTSTATUS
      {
        return Original(ObjectAttributes, FileInformation);
      });
}
