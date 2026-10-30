/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file Hooks.cpp
 *   Implementation of all Windows API hook functions used to implement path
 *   redirection.
 **************************************************************************************************/

#include "Hooks.h"

#include <atomic>
#include <cwchar>
#include <functional>
#include <new>
#include <optional>
#include <string>

#include <Hookshot/DynamicHook.h>
#include <Infra/Core/DebugAssert.h>
#include <Infra/Core/Message.h>
#include <Infra/Core/Strings.h>
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
  tempDirectory.UnsafeSetSize(GetTempPathW(tempDirectory.Capacity(), tempDirectory.Data()));
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


/// Determines if the specified file exists and is not a directory.
/// Used to decide whether an overlay-mode image path redirection is safe to apply. The Win32 call
/// updates the calling thread's last error value, which belongs to the caller, so it is preserved
/// across this function in order to leave no trace on the invoking application.
/// @param [in] path Absolute path to check.
/// @return `true` if a file exists at the specified path, `false` otherwise.
[[maybe_unused]] static bool FileExists(std::wstring_view path)
{
  const DWORD lastError = GetLastError();

  const std::wstring nullTerminatedPath(path.begin(), path.end());
  const DWORD attributes = GetFileAttributesW(nullTerminatedPath.c_str());

  SetLastError(lastError);

  return (INVALID_FILE_ATTRIBUTES != attributes) &&
      (0 == (attributes & FILE_ATTRIBUTE_DIRECTORY));
}

/// Determines the path that an executable should actually be launched from, applying a redirection
/// rule when one matches and the redirected file exists.
/// @param [in] imagePath Absolute path of the executable as requested by the caller.
/// @return Redirected path if one should be used, otherwise no value.
static std::optional<std::wstring> LookupRedirectedImagePath(std::wstring_view imagePath)
{
  const Pathwinder::FileOperationInstruction instruction =
      FilesystemDirectorInstance().GetInstructionForFileOperation(
          imagePath,
          Pathwinder::FileAccessMode::ReadOnly(),
          Pathwinder::CreateDisposition::OpenExistingFile());

  if (false == instruction.HasRedirectedFilename()) return std::nullopt;

  // The redirected filename carries a Windows namespace prefix, but a process image path is
  // expected to be a plain absolute path, so the prefix is removed.
  std::wstring_view redirectedPath = instruction.GetRedirectedFilename();
  redirectedPath.remove_prefix(
      Pathwinder::Strings::PathGetWindowsNamespacePrefix(redirectedPath).length());

  // Process creation cannot fall back to another path when the redirected path is unusable: the
  // kernel resolves the image file itself and fails the entire call, and the caller does not retry
  // with the original path. Rewriting the image path to a file that does not exist would therefore
  // turn a launch that works today into a failure, so the redirected file is always checked for
  // existence first and the original path is retained when it is absent.
  if (false == FileExists(redirectedPath)) return std::nullopt;

  return std::wstring(redirectedPath);
}

BOOL Pathwinder::Hooks::DynamicHook_CreateProcessW::Hook(
    LPCWSTR lpApplicationName,
    LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation)
{
  // The executable path is supplied either directly as `lpApplicationName`, or as the first token
  // of `lpCommandLine` when the former is null. Both forms are redirected so that the two remain
  // consistent. The caller's buffers are never modified: replacement strings are owned by this
  // function and merely passed to the original function.
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Info,
      L"%s(%u): lpApplicationName=\"%s\", lpCommandLine=\"%s\", lpCurrentDirectory=\"%s\".",
      GetFunctionName(),
      GetRequestIdentifier(),
      (nullptr != lpApplicationName) ? lpApplicationName : L"<null>",
      (nullptr != lpCommandLine) ? lpCommandLine : L"<null>",
      (nullptr != lpCurrentDirectory) ? lpCurrentDirectory : L"<null>");

  std::optional<std::wstring> applicationNameReplacement;
  std::optional<std::wstring> commandLineReplacement;

  if (nullptr != lpApplicationName)
  {
    applicationNameReplacement = LookupRedirectedImagePath(lpApplicationName);

    if (true == applicationNameReplacement.has_value())
    {
      Infra::Message::OutputFormatted(
          Infra::Message::ESeverity::Info,
          L"%s(%u): Redirecting process image path from \"%s\" to \"%s\".",
          GetFunctionName(),
          GetRequestIdentifier(),
          lpApplicationName,
          applicationNameReplacement->c_str());

      lpApplicationName = applicationNameReplacement->c_str();
    }
  }

  // Every path that appears on the command line has to be redirected, not just the executable.
  // Programs such as the WeChat plugin host receive directories through arguments like
  // "--flue-runtime-dir=<path>" and open them directly, without going through any hook, so leaving
  // them pointing at an origin directory that does not exist on this machine makes the child
  // process abort right after it starts. This must run regardless of whether `lpApplicationName`
  // was supplied, because callers that pass an explicit application name may still embed
  // origin-drive paths (e.g. crashpad's "--database=<path>") in the command line.
  if (nullptr != lpCommandLine)
  {
    const std::wstring_view original(lpCommandLine);

    std::wstring rebuiltCommandLine;
    rebuiltCommandLine.reserve(original.size() + 256);

    size_t copiedUpTo = 0;
    size_t index = 0;

    while ((index + 2) < original.size())
    {
      const bool startsArgument =
          (0 == index) || (L'"' == original[index - 1]) || (L'=' == original[index - 1]) ||
          (L' ' == original[index - 1]);

      if ((true == startsArgument) && (0 != iswalpha(original[index])) &&
          (L':' == original[index + 1]) && (L'\\' == original[index + 2]))
      {
        size_t pathEnd = index;

        while ((pathEnd < original.size()) && (L'"' != original[pathEnd]) &&
               (L' ' != original[pathEnd]))
        {
          ++pathEnd;
        }

        const std::wstring_view path(original.data() + index, pathEnd - index);

        const Pathwinder::FileOperationInstruction instruction =
            FilesystemDirectorInstance().GetInstructionForFileOperation(
                path,
                Pathwinder::FileAccessMode::ReadOnly(),
                Pathwinder::CreateDisposition::OpenExistingFile());

        if (true == instruction.HasRedirectedFilename())
        {
          std::wstring_view redirectedPath = instruction.GetRedirectedFilename();
          redirectedPath.remove_prefix(
              Pathwinder::Strings::PathGetWindowsNamespacePrefix(redirectedPath).length());

          rebuiltCommandLine.append(original.substr(copiedUpTo, index - copiedUpTo));
          rebuiltCommandLine.append(redirectedPath);
          copiedUpTo = pathEnd;

          Infra::Message::OutputFormatted(
              Infra::Message::ESeverity::Info,
              L"%s(%u): Redirecting command line path from \"%.*s\" to \"%s\".",
              GetFunctionName(),
              GetRequestIdentifier(),
              static_cast<int>(path.length()),
              path.data(),
              redirectedPath.data());
        }

        index = pathEnd;
      }
      else
      {
        ++index;
      }
    }

    if (copiedUpTo > 0)
    {
      rebuiltCommandLine.append(original.substr(copiedUpTo));
      commandLineReplacement = std::move(rebuiltCommandLine);
      lpCommandLine = const_cast<LPWSTR>(commandLineReplacement->c_str());
    }
  }

  // The current directory is redirected as well. The kernel applies it while creating the process,
  // before any of this process' hooks can intercept the lookup, so an origin directory that is
  // absent on this machine makes the whole call fail even when the executable was redirected
  // successfully. Unlike an image path, a directory is a valid target here, so its existence check
  // does not exclude directories.
  std::optional<std::wstring> currentDirectoryReplacement;

  if (nullptr != lpCurrentDirectory)
  {
    const Pathwinder::FileOperationInstruction instruction =
        FilesystemDirectorInstance().GetInstructionForFileOperation(
            lpCurrentDirectory,
            Pathwinder::FileAccessMode::ReadOnly(),
            Pathwinder::CreateDisposition::OpenExistingFile());

    if (true == instruction.HasRedirectedFilename())
    {
      std::wstring_view redirectedPath = instruction.GetRedirectedFilename();
      redirectedPath.remove_prefix(
          Pathwinder::Strings::PathGetWindowsNamespacePrefix(redirectedPath).length());

      std::wstring redirectedDirectory(redirectedPath);

      const DWORD lastError = GetLastError();
      const DWORD attributes = GetFileAttributesW(redirectedDirectory.c_str());
      SetLastError(lastError);

      if (INVALID_FILE_ATTRIBUTES != attributes)
      {
        Infra::Message::OutputFormatted(
            Infra::Message::ESeverity::Info,
            L"%s(%u): Redirecting current directory from \"%s\" to \"%s\".",
            GetFunctionName(),
            GetRequestIdentifier(),
            lpCurrentDirectory,
            redirectedDirectory.c_str());

        currentDirectoryReplacement = std::move(redirectedDirectory);
        lpCurrentDirectory = currentDirectoryReplacement->c_str();
      }
    }
  }

  const BOOL callSucceeded = Original(
      lpApplicationName,
      lpCommandLine,
      lpProcessAttributes,
      lpThreadAttributes,
      bInheritHandles,
      dwCreationFlags,
      lpEnvironment,
      lpCurrentDirectory,
      lpStartupInfo,
      lpProcessInformation);

  const DWORD lastError = GetLastError();

  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Info,
      L"%s(%u): CreateProcessW returned %s (lastError=%lu, processId=%lu).",
      GetFunctionName(),
      GetRequestIdentifier(),
      (TRUE == callSucceeded) ? L"TRUE" : L"FALSE",
      lastError,
      ((TRUE == callSucceeded) && (nullptr != lpProcessInformation))
          ? lpProcessInformation->dwProcessId
          : 0UL);

  return callSucceeded;
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

HINSTANCE Pathwinder::Hooks::DynamicHook_ShellExecuteA::Hook(
    HWND hwnd,
    LPCSTR lpOperation,
    LPCSTR lpFile,
    LPCSTR lpParameters,
    LPCSTR lpDirectory,
    INT nShowCmd)
{
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"%s: Hook function not yet implemented. Selected request parameters dumped below.",
      GetFunctionName());
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"  lpOperation = %s",
      ((nullptr != lpOperation) ? Infra::Strings::ConvertNarrowToWide(lpOperation).AsCString()
                                : L"nullptr"));
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"  lpFile = %s",
      ((nullptr != lpFile) ? Infra::Strings::ConvertNarrowToWide(lpFile).AsCString() : L"nullptr"));
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"  lpParameters = %s",
      ((nullptr != lpParameters) ? Infra::Strings::ConvertNarrowToWide(lpParameters).AsCString()
                                 : L"nullptr"));
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"  lpDirectory = %s",
      ((nullptr != lpDirectory) ? Infra::Strings::ConvertNarrowToWide(lpDirectory).AsCString()
                                : L"nullptr"));

  return Original(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd);
}

HINSTANCE Pathwinder::Hooks::DynamicHook_ShellExecuteW::Hook(
    HWND hwnd,
    LPCWSTR lpOperation,
    LPCWSTR lpFile,
    LPCWSTR lpParameters,
    LPCWSTR lpDirectory,
    INT nShowCmd)
{
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"%s: Hook function not yet implemented. Selected request parameters dumped below.",
      GetFunctionName());
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"  lpOperation = %s",
      ((nullptr != lpOperation) ? lpOperation : L"nullptr"));
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"  lpFile = %s",
      ((nullptr != lpFile) ? lpFile : L"nullptr"));
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"  lpParameters = %s",
      ((nullptr != lpParameters) ? lpParameters : L"nullptr"));
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"  lpDirectory = %s",
      ((nullptr != lpDirectory) ? lpDirectory : L"nullptr"));

  return Original(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd);
}

BOOL Pathwinder::Hooks::DynamicHook_ShellExecuteExA::Hook(SHELLEXECUTEINFOA* pExecInfo)
{
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"%s: Hook function not yet implemented. Selected request parameters dumped below.",
      GetFunctionName());
  if (nullptr != pExecInfo)
  {
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Warning,
        L"  pExecInfo->lpVerb = %s",
        ((nullptr != pExecInfo->lpVerb)
             ? Infra::Strings::ConvertNarrowToWide(pExecInfo->lpVerb).AsCString()
             : L"nullptr"));
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Warning,
        L"  pExecInfo->lpFile = %s",
        ((nullptr != pExecInfo->lpFile)
             ? Infra::Strings::ConvertNarrowToWide(pExecInfo->lpFile).AsCString()
             : L"nullptr"));
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Warning,
        L"  pExecInfo->lpParameters = %s",
        ((nullptr != pExecInfo->lpParameters)
             ? Infra::Strings::ConvertNarrowToWide(pExecInfo->lpParameters).AsCString()
             : L"nullptr"));
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Warning,
        L"  pExecInfo->lpDirectory = %s",
        ((nullptr != pExecInfo->lpDirectory)
             ? Infra::Strings::ConvertNarrowToWide(pExecInfo->lpDirectory).AsCString()
             : L"nullptr"));
  }
  else
  {
    Infra::Message::Output(Infra::Message::ESeverity::Warning, L"  pExecInfo = nullptr");
  }

  return Original(pExecInfo);
}

BOOL Pathwinder::Hooks::DynamicHook_ShellExecuteExW::Hook(SHELLEXECUTEINFOW* pExecInfo)
{
  Infra::Message::OutputFormatted(
      Infra::Message::ESeverity::Warning,
      L"%s: Hook function not yet implemented. Selected request parameters dumped below.",
      GetFunctionName());
  if (nullptr != pExecInfo)
  {
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Warning,
        L"  pExecInfo->lpVerb = %s",
        ((nullptr != pExecInfo->lpVerb) ? pExecInfo->lpVerb : L"nullptr"));
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Warning,
        L"  pExecInfo->lpFile = %s",
        ((nullptr != pExecInfo->lpFile) ? pExecInfo->lpFile : L"nullptr"));
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Warning,
        L"  pExecInfo->lpParameters = %s",
        ((nullptr != pExecInfo->lpParameters) ? pExecInfo->lpParameters : L"nullptr"));
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Warning,
        L"  pExecInfo->lpDirectory = %s",
        ((nullptr != pExecInfo->lpDirectory) ? pExecInfo->lpDirectory : L"nullptr"));
  }
  else
  {
    Infra::Message::Output(Infra::Message::ESeverity::Warning, L"  pExecInfo = nullptr");
  }

  return Original(pExecInfo);
}
