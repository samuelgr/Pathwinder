/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file FilesystemOperations.cpp
 *   Declaration of functions that provide an abstraction for filesystem operations executed
 *   internally.
 **************************************************************************************************/

#include "FilesystemOperations.h"

#include <bitset>
#include <cwctype>
#include <limits>
#include <optional>
#include <string_view>

#include <Infra/DebugAssert.h>
#include <Infra/TemporaryBuffer.h>
#include <Infra/ValueOrError.h>

#include "ApiWindows.h"
#include "FileInformationStruct.h"
#include "Hooks.h"
#include "Message.h"
#include "Strings.h"

/// Ensures that the specified function parameter, which is a string view, has a Windows namespace
/// prefix. This is a common requirement when invoking any Windows system calls.
#define ENSURE_ABSOLUTE_PATH_PARAM_HAS_WINDOWS_NAMESPCE_PREFIX(param)                              \
  std::optional<Infra::TemporaryString> _internal_maybePrefixed_##param = std::nullopt;            \
  if (false == Strings::PathHasWindowsNamespacePrefix(param))                                      \
  {                                                                                                \
    _internal_maybePrefixed_##param = Strings::PathAddWindowsNamespacePrefix(param);               \
    param = _internal_maybePrefixed_##param->AsStringView();                                       \
  }

namespace Pathwinder
{
  namespace FilesystemOperations
  {
    /// Determines if the specified file attributes indicate that the file in question exists.
    /// @param [in] attributes File attributes retrieved from a system call.
    /// @return `true` if the file exists based on the attributes, `false` otherwise.
    static inline bool AttributesIndicateFileExists(DWORD attributes)
    {
      return (INVALID_FILE_ATTRIBUTES != attributes);
    }

    /// Determines if the specified file attributes indicate that the file in question exists
    /// and is a directory.
    /// @param [in] attributes File attributes retrieved from a system call.
    /// @return `true` if the file exists and is a directory based on the attributes, `false`
    /// otherwise.
    static inline bool AttributesIndicateFileExistsAndIsDirectory(DWORD attributes)
    {
      return (
          (true == AttributesIndicateFileExists(attributes)) &&
          (0 != (FILE_ATTRIBUTE_DIRECTORY & attributes)));
    }

    /// Ensures the specified directory exists.
    /// Attempts to open a file handle for the specified directory that will cause it to be
    /// unchanged if it exists but created if it does not. If the handle is opened successfully,
    /// it is immediately closed.
    /// @param [in] absoluteDirectoryPath Absolute path of the directory to be created.
    /// @return Result of the system call that ensures the specified directory exists.
    static inline NTSTATUS EnsureDirectoryExists(std::wstring_view absoluteDirectoryPath)
    {
      ENSURE_ABSOLUTE_PATH_PARAM_HAS_WINDOWS_NAMESPCE_PREFIX(absoluteDirectoryPath);

      HANDLE directoryHandle = nullptr;

      UNICODE_STRING absoluteDirectoryPathSystemString =
          Strings::NtConvertStringViewToUnicodeString(absoluteDirectoryPath);
      OBJECT_ATTRIBUTES absoluteDirectoryPathObjectAttributes{};
      InitializeObjectAttributes(
          &absoluteDirectoryPathObjectAttributes,
          &absoluteDirectoryPathSystemString,
          0,
          nullptr,
          nullptr);

      IO_STATUS_BLOCK unusedStatusBlock{};
      NTSTATUS createOrOpenDirectoryResult = Hooks::ProtectedDependency::NtCreateFile::SafeInvoke(
          &directoryHandle,
          FILE_TRAVERSE,
          &absoluteDirectoryPathObjectAttributes,
          &unusedStatusBlock,
          0,
          0,
          (FILE_SHARE_READ | FILE_SHARE_WRITE),
          FILE_OPEN_IF,
          FILE_DIRECTORY_FILE,
          nullptr,
          0);

      if (NT_SUCCESS(createOrOpenDirectoryResult))
        Hooks::ProtectedDependency::NtClose::SafeInvoke(directoryHandle);

      return createOrOpenDirectoryResult;
    }

    /// Queries for the attributes of the object identified by the specified absolute path.
    /// Roughly analogous to the Windows API function `GetFileAttributes`.
    /// @param [in] absolutePath Absolute path for which attributes are requested.
    /// @return File attributes, or `INVALID_FILE_ATTRIBUTES` in the event of an error.
    static DWORD GetAttributesForPath(std::wstring_view absolutePath)
    {
      ENSURE_ABSOLUTE_PATH_PARAM_HAS_WINDOWS_NAMESPCE_PREFIX(absolutePath);

      UNICODE_STRING absolutePathSystemString =
          Strings::NtConvertStringViewToUnicodeString(absolutePath);
      OBJECT_ATTRIBUTES absolutePathObjectAttributes{};
      InitializeObjectAttributes(
          &absolutePathObjectAttributes, &absolutePathSystemString, 0, nullptr, nullptr);

      IO_STATUS_BLOCK unusedStatusBlock{};

      HANDLE queryHandle = nullptr;
      NTSTATUS openResult = Hooks::ProtectedDependency::NtOpenFile::SafeInvoke(
          &queryHandle,
          FILE_READ_ATTRIBUTES | SYNCHRONIZE,
          &absolutePathObjectAttributes,
          &unusedStatusBlock,
          FILE_SHARE_READ | FILE_SHARE_WRITE,
          FILE_SYNCHRONOUS_IO_NONALERT);
      if (!(NT_SUCCESS(openResult))) return INVALID_FILE_ATTRIBUTES;

      SFileBasicInformation queryFileInformation{};
      NTSTATUS queryResult = Hooks::ProtectedDependency::NtQueryInformationFile::SafeInvoke(
          queryHandle,
          &unusedStatusBlock,
          &queryFileInformation,
          sizeof(queryFileInformation),
          SFileBasicInformation::kFileInformationClass);
      Hooks::ProtectedDependency::NtClose::SafeInvoke(queryHandle);
      if (!(NT_SUCCESS(openResult))) return INVALID_FILE_ATTRIBUTES;
      return queryFileInformation.fileAttributes;
    }

    /// Retrieves the character used to represent the logical drive at index n. This is a simple
    /// computation that begins with 0 being A, 1 being B, 2 being C, and so on all the way up to 25
    /// being Z. No error-checking is performed.
    /// @param [in] n Index of the desired drive letter.
    /// @return Letter of the drive at the specified index.
    static inline wchar_t GetNthLogicalDriveLetter(unsigned int n)
    {
      wchar_t logicalDriveLetter = L'A' + static_cast<wchar_t>(n);
      DebugAssert(std::iswalpha(logicalDriveLetter), "Invalid drive letter.");

      return logicalDriveLetter;
    }

    /// Retrieves the logical drive letter of the volume on which a file is stored, identified by
    /// open handle.
    /// @param [in] handle Open handle for which the associated drive letter is desired.
    /// @return Letter of the drive on which the associated file is stored, or a null character if
    /// this could not be determined.
    static wchar_t GetLogicalDriveLetterForHandle(HANDLE handle)
    {
      wchar_t rootPathBuf[] = L"?:\\";

      DWORD serialNumberByHandle = 0;
      const DWORD volumeInformationByHandleResult = GetVolumeInformationByHandleW(
          handle, nullptr, 0, &serialNumberByHandle, nullptr, nullptr, nullptr, 0);
      if (0 == volumeInformationByHandleResult) return L'\0';

      const std::bitset<26> logicalDrivesMask(GetLogicalDrives());
      for (unsigned int logicalDriveIndex = 0; logicalDriveIndex < logicalDrivesMask.size();
           ++logicalDriveIndex)
      {
        if (logicalDrivesMask[logicalDriveIndex])
        {
          const wchar_t logicalDriveLetter = GetNthLogicalDriveLetter(logicalDriveIndex);
          rootPathBuf[0] = logicalDriveLetter;

          DWORD serialNumberByPath = 0;
          const DWORD volumeInformationByPathResult = GetVolumeInformationW(
              rootPathBuf, nullptr, 0, &serialNumberByPath, nullptr, nullptr, nullptr, 0);
          if (0 == volumeInformationByPathResult) continue;

          if (serialNumberByPath == serialNumberByHandle) return logicalDriveLetter;
        }
      }

      return L'\0';
    }

    /// Determines if the specified absolute path begins with a drive letter, which consists of
    /// an alphabetic character followed by a colon character.
    /// @param [in] absolutePath Absolute path to check without any Windows namespace prefix.
    /// @return `true` if the path begins with a drive letter, `false` otherwise.
    static inline bool PathBeginsWithDriveLetter(std::wstring_view absolutePath)
    {
      return ((0 != std::iswalpha(absolutePath[0])) && (L':' == absolutePath[1]));
    }

    /// Determines if the specified absolute path consists of just a drive letter with no
    /// trailing backslash. In other words, it begins with a drive letter and has a length of 2
    /// characters.
    /// @param [in] absolutePath Absolute path to check without any Windows namespace prefix.
    /// @return `true` if the path consists of just a drive letter, `false` otherwise.
    static inline bool PathIsDriveLetterOnly(std::wstring_view absolutePath)
    {
      return ((2 == absolutePath.length()) && PathBeginsWithDriveLetter(absolutePath));
    }

    /// Extracts the drive letter prefix from the specified absolute path string, if it exists.
    /// The drive letter prefix consists of a letter, a colon, and a backslash.
    /// @param [in] absolutePath Absolute path for which a drive letter prefix is desired,
    /// without any Windows namespace prefix.
    /// @return Drive letter prefix if it exists, or an empty string view if it does not.
    static inline std::wstring_view PathGetDriveLetterPrefix(std::wstring_view absolutePath)
    {
      if ((false == PathBeginsWithDriveLetter(absolutePath)) || (absolutePath.length() < 3) ||
          (absolutePath[2] != L'\\'))
        return std::wstring_view();

      return absolutePath.substr(0, 3);
    }

    NTSTATUS CloseHandle(HANDLE handle)
    {
      return Hooks::ProtectedDependency::NtClose::SafeInvoke(handle);
    }

    intptr_t CreateDirectoryHierarchy(std::wstring_view absoluteDirectoryPath)
    {
      const std::wstring_view windowsNamespacePrefix =
          Strings::PathGetWindowsNamespacePrefix(absoluteDirectoryPath);
      const std::wstring_view absoluteDirectoryPathTrimmed = Strings::RemoveTrailing(
          absoluteDirectoryPath.substr(windowsNamespacePrefix.length()), L'\\');

      std::wstring_view driveLetterPrefix = PathGetDriveLetterPrefix(absoluteDirectoryPathTrimmed);
      if ((true == driveLetterPrefix.empty()) ||
          (absoluteDirectoryPath.length() == driveLetterPrefix.length()))
        return NtStatus::kObjectPathInvalid;

      std::wstring_view currentDirectoryToTry = absoluteDirectoryPathTrimmed;

      // This loop tries to verify that directories exist starting with the input path
      // provided and working its way up one level at a time but stopping one level right
      // before the drive letter prefix. For example, if the input path is "C:\111\222\333" it
      // will check directories, in order, "C:\111\222\333", "C:\111\222", and finally
      // "C:\111". Upon completion, the directory in the hierarchy with the longest path known
      // (or assumed, in the case of a drive letter alone) to exist is stored. From there all
      // subsequent hierarchy levels can be created in order. Rationale for this approach is
      // to optimize for the common case whereby the directory hierarchy already exists, in
      // which case a single system call to check for that is all it takes to complete the
      // entire operation.
      do
      {
        std::wstring_view currentDirectoryToTryWithPrefix = absoluteDirectoryPath.substr(
            0, currentDirectoryToTry.length() + windowsNamespacePrefix.length());

        if (true == IsDirectory(currentDirectoryToTryWithPrefix)) break;

        currentDirectoryToTry =
            currentDirectoryToTry.substr(0, currentDirectoryToTry.find_last_of(L'\\'));
      }
      while (currentDirectoryToTry.length() > driveLetterPrefix.length());

      // No need to check string content because the current directory is always a substring
      // of the absolute input path. If this condition is true then the full input path
      // hierarchy exists, so there is no need to create any new directories.
      if (currentDirectoryToTry.length() == absoluteDirectoryPathTrimmed.length())
        return NtStatus::kSuccess;

      // This loop goes the opposite way, starting with the longest path known to exist and
      // working its way down the absolute input path hierarchy. Each step of the way an
      // attempt is made to create a directory.
      std::wstring_view remainingHierarchyToCreate =
          absoluteDirectoryPathTrimmed.substr(1 + currentDirectoryToTry.length());
      for (std::wstring_view nextHierarchyLevelToCreate :
           Strings::Tokenizer(remainingHierarchyToCreate, L"\\"))
      {
        currentDirectoryToTry = absoluteDirectoryPathTrimmed.substr(
            0, 1 + currentDirectoryToTry.length() + nextHierarchyLevelToCreate.length());

        std::wstring_view currentDirectoryToTryWithPrefix = absoluteDirectoryPath.substr(
            0, currentDirectoryToTry.length() + windowsNamespacePrefix.length());
        NTSTATUS currentDirectoryCreateResult = EnsureDirectoryExists(currentDirectoryToTry);
        if (!(NT_SUCCESS(currentDirectoryCreateResult))) return currentDirectoryCreateResult;
      }

      return NtStatus::kSuccess;
    }

    bool Exists(std::wstring_view absolutePath)
    {
      DWORD pathAttributes = 0;

      if (true == PathIsDriveLetterOnly(absolutePath))
      {
        // If the query is for a drive letter itself (for example, "C:") then appending a
        // backslash is necessary. This is a special case. Ordinarily appending a backslash
        // is not required, but for just a drive letter the system call will reject the path
        // if it does not have a trailing backslash.

        wchar_t driveLetterWithBackslash[] = {absolutePath[0], absolutePath[1], L'\\'};
        pathAttributes = GetAttributesForPath(
            std::wstring_view(driveLetterWithBackslash, _countof(driveLetterWithBackslash)));
      }
      else
      {
        pathAttributes = GetAttributesForPath(absolutePath);
      }

      return AttributesIndicateFileExists(pathAttributes);
    }

    bool IsDirectory(std::wstring_view absolutePath)
    {
      DWORD pathAttributes = 0;

      if (true == PathIsDriveLetterOnly(absolutePath))
      {
        // If the query is for a drive letter itself (for example, "C:") then appending a
        // backslash is necessary. This is a special case. Ordinarily appending a backslash
        // is not required, but for just a drive letter the system call will reject the path
        // if it does not have a trailing backslash.

        wchar_t driveLetterWithBackslash[] = {absolutePath[0], absolutePath[1], L'\\'};
        pathAttributes = GetAttributesForPath(
            std::wstring_view(driveLetterWithBackslash, _countof(driveLetterWithBackslash)));
      }
      else
      {
        pathAttributes = GetAttributesForPath(absolutePath);
      }

      return AttributesIndicateFileExistsAndIsDirectory(pathAttributes);
    }

    Infra::ValueOrError<HANDLE, NTSTATUS> OpenDirectoryForEnumeration(
        std::wstring_view absoluteDirectoryPath)
    {
      ENSURE_ABSOLUTE_PATH_PARAM_HAS_WINDOWS_NAMESPCE_PREFIX(absoluteDirectoryPath);

      HANDLE directoryHandle = nullptr;

      UNICODE_STRING absoluteDirectoryPathSystemString =
          Strings::NtConvertStringViewToUnicodeString(absoluteDirectoryPath);
      OBJECT_ATTRIBUTES absoluteDirectoryPathObjectAttributes{};
      InitializeObjectAttributes(
          &absoluteDirectoryPathObjectAttributes,
          &absoluteDirectoryPathSystemString,
          0,
          nullptr,
          nullptr);

      IO_STATUS_BLOCK unusedStatusBlock{};

      NTSTATUS openDirectoryForEnumerationResult =
          Hooks::ProtectedDependency::NtOpenFile::SafeInvoke(
              &directoryHandle,
              (FILE_LIST_DIRECTORY | SYNCHRONIZE),
              &absoluteDirectoryPathObjectAttributes,
              &unusedStatusBlock,
              (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE),
              (FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT));
      if (!(NT_SUCCESS(openDirectoryForEnumerationResult)))
        return openDirectoryForEnumerationResult;

      return directoryHandle;
    }

    NTSTATUS PartialEnumerateDirectoryContents(
        HANDLE directoryHandle,
        FILE_INFORMATION_CLASS fileInformationClass,
        void* enumerationBuffer,
        unsigned int enumerationBufferCapacityBytes,
        ULONG queryFlags,
        std::wstring_view filePattern)
    {
      UNICODE_STRING filePatternSystemString{};
      UNICODE_STRING* filePatternSystemStringPtr = nullptr;

      if (false == filePattern.empty())
      {
        filePatternSystemString = Strings::NtConvertStringViewToUnicodeString(filePattern);
        filePatternSystemStringPtr = &filePatternSystemString;
      }

      IO_STATUS_BLOCK statusBlock{};

      NTSTATUS directoryEnumerationResult =
          Hooks::ProtectedDependency::NtQueryDirectoryFileEx::SafeInvoke(
              directoryHandle,
              NULL,
              NULL,
              NULL,
              &statusBlock,
              enumerationBuffer,
              enumerationBufferCapacityBytes,
              fileInformationClass,
              queryFlags,
              filePatternSystemStringPtr);
      if (NT_SUCCESS(directoryEnumerationResult) && (0 == statusBlock.Information))
        return NtStatus::kBufferTooSmall;

      return directoryEnumerationResult;
    }

    Infra::ValueOrError<Infra::TemporaryString, NTSTATUS> QueryAbsolutePathByHandle(
        HANDLE fileHandle)
    {
      wchar_t driveLetterPrefix[] = {GetLogicalDriveLetterForHandle(fileHandle), L':', L'\0'};
      if (L'\0' == driveLetterPrefix[0]) return NtStatus::kObjectNameNotFound;

      BytewiseDanglingFilenameStruct<SFileNameInformation> absolutePathNameInformation{};

      IO_STATUS_BLOCK statusBlock{};

      NTSTATUS queryFileNameResult = Hooks::ProtectedDependency::NtQueryInformationFile::SafeInvoke(
          fileHandle,
          &statusBlock,
          absolutePathNameInformation.Data(),
          absolutePathNameInformation.CapacityBytes(),
          SFileNameInformation::kFileInformationClass);
      if (!(NT_SUCCESS(queryFileNameResult))) return queryFileNameResult;
      if (false == absolutePathNameInformation.GetDanglingFilename().starts_with(L'\\'))
        return NtStatus::kObjectNameNotFound;

      Infra::TemporaryString absolutePath;
      absolutePath << driveLetterPrefix << absolutePathNameInformation.GetDanglingFilename();

      return absolutePath;
    }

    Infra::ValueOrError<ULONG, NTSTATUS> QueryFileHandleMode(HANDLE fileHandle)
    {
      SFileModeInformation modeInformation{};
      IO_STATUS_BLOCK unusedStatusBlock{};

      NTSTATUS queryInformationResult =
          Hooks::ProtectedDependency::NtQueryInformationFile::SafeInvoke(
              fileHandle,
              &unusedStatusBlock,
              &modeInformation,
              sizeof(modeInformation),
              SFileModeInformation::kFileInformationClass);
      if (!(NT_SUCCESS(queryInformationResult))) return queryInformationResult;

      return modeInformation.mode;
    }

    NTSTATUS QuerySingleFileDirectoryInformation(
        std::wstring_view absoluteDirectoryPath,
        std::wstring_view fileName,
        FILE_INFORMATION_CLASS fileInformationClass,
        void* enumerationBuffer,
        unsigned int enumerationBufferCapacityBytes)
    {
      auto maybeDirectoryHandle = OpenDirectoryForEnumeration(absoluteDirectoryPath);
      if (true == maybeDirectoryHandle.HasError()) return maybeDirectoryHandle.Error();

      UNICODE_STRING fileNameSystemString = Strings::NtConvertStringViewToUnicodeString(fileName);
      IO_STATUS_BLOCK unusedStatusBlock{};

      NTSTATUS directoryEnumResult = Hooks::ProtectedDependency::NtQueryDirectoryFileEx::SafeInvoke(
          maybeDirectoryHandle.Value(),
          NULL,
          NULL,
          NULL,
          &unusedStatusBlock,
          enumerationBuffer,
          static_cast<ULONG>(enumerationBufferCapacityBytes),
          fileInformationClass,
          0,
          &fileNameSystemString);
      Hooks::ProtectedDependency::NtClose::SafeInvoke(maybeDirectoryHandle.Value());

      return directoryEnumResult;
    }
  } // namespace FilesystemOperations
} // namespace Pathwinder
