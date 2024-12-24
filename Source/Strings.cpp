/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file Strings.cpp
 *   Implementation of functions for manipulating Pathwinder-specific strings.
 **************************************************************************************************/

#include "Strings.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#include <Infra/Core/DebugAssert.h>
#include <Infra/Core/ProcessInfo.h>
#include <Infra/Core/Strings.h>
#include <Infra/Core/TemporaryBuffer.h>

#include "ApiWindows.h"

namespace Pathwinder
{
  namespace Strings
  {
    /// Length of a drive letter prefix, in characters. A drive letter prefix consists of a single
    /// letter, a colon, and a backslash, for a total of three characters.
    static constexpr size_t kPathDriveLetterPrefixLengthChars = 3;

    bool FileNameMatchesPattern(std::wstring_view fileName, std::wstring_view filePatternUpperCase)
    {
      if (true == filePatternUpperCase.empty()) return true;

      UNICODE_STRING fileNameString = NtConvertStringViewToUnicodeString(fileName);
      UNICODE_STRING filePatternString = NtConvertStringViewToUnicodeString(filePatternUpperCase);

      return (
          TRUE ==
          Pathwinder::WindowsInternal::RtlIsNameInExpression(
              &filePatternString, &fileNameString, TRUE, nullptr));
    }

    Infra::TemporaryString NtAccessMaskToString(ACCESS_MASK accessMask)
    {
      constexpr std::wstring_view kSeparator = L" | ";
      Infra::TemporaryString outputString = Infra::Strings::Format(L"0x%08x (", accessMask);

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
          outputString << Infra::Strings::Format(L"0x%08x", accessMask) << kSeparator;
      }

      outputString.RemoveSuffix(static_cast<unsigned int>(kSeparator.length()));
      outputString << L")";

      return outputString;
    }

    Infra::TemporaryString NtCreateDispositionToString(ULONG createDisposition)
    {
      constexpr wchar_t kFormatString[] = L"0x%08x (%s)";

      switch (createDisposition)
      {
        case FILE_SUPERSEDE:
          return Infra::Strings::Format(kFormatString, createDisposition, L"FILE_SUPERSEDE");
        case FILE_CREATE:
          return Infra::Strings::Format(kFormatString, createDisposition, L"FILE_CREATE");
        case FILE_OPEN:
          return Infra::Strings::Format(kFormatString, createDisposition, L"FILE_OPEN");
        case FILE_OPEN_IF:
          return Infra::Strings::Format(kFormatString, createDisposition, L"FILE_OPEN_IF");
        case FILE_OVERWRITE:
          return Infra::Strings::Format(kFormatString, createDisposition, L"FILE_OVERWRITE");
        case FILE_OVERWRITE_IF:
          return Infra::Strings::Format(kFormatString, createDisposition, L"FILE_OVERWRITE_IF");
        default:
          return Infra::Strings::Format(kFormatString, createDisposition, L"unknown");
      }
    }

    Infra::TemporaryString NtCreateOrOpenOptionsToString(ULONG createOrOpenOptions)
    {
      constexpr std::wstring_view kSeparator = L" | ";
      Infra::TemporaryString outputString =
          Infra::Strings::Format(L"0x%08x (", createOrOpenOptions);

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
          outputString << Infra::Strings::Format(L"0x%08x", createOrOpenOptions) << kSeparator;
      }

      outputString.RemoveSuffix(static_cast<unsigned int>(kSeparator.length()));
      outputString << L")";

      return outputString;
    }

    Infra::TemporaryString NtShareAccessToString(ULONG shareAccess)
    {
      constexpr std::wstring_view kSeparator = L" | ";
      Infra::TemporaryString outputString = Infra::Strings::Format(L"0x%08x (", shareAccess);

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
          outputString << Infra::Strings::Format(L"0x%08x", shareAccess) << kSeparator;
      }

      outputString.RemoveSuffix(static_cast<unsigned int>(kSeparator.length()));
      outputString << L")";

      return outputString;
    }

    UNICODE_STRING NtConvertStringViewToUnicodeString(std::wstring_view strView)
    {
      DebugAssert(
          (strView.length() * sizeof(wchar_t)) <=
              static_cast<size_t>(std::numeric_limits<decltype(UNICODE_STRING::Length)>::max()),
          "Attempting to make an unrepresentable UNICODE_STRING due to the length exceeding representable range for Length.");
      DebugAssert(
          (strView.length() * sizeof(wchar_t)) <=
              static_cast<size_t>(
                  std::numeric_limits<decltype(UNICODE_STRING::MaximumLength)>::max()),
          "Attempting to make an unrepresentable UNICODE_STRING due to the length exceeding representable range for MaximumLength.");

      return {
          .Length =
              static_cast<decltype(UNICODE_STRING::Length)>(strView.length() * sizeof(wchar_t)),
          .MaximumLength = static_cast<decltype(UNICODE_STRING::MaximumLength)>(
              strView.length() * sizeof(wchar_t)),
          .Buffer = const_cast<decltype(UNICODE_STRING::Buffer)>(strView.data())};
    }

    Infra::TemporaryString PathAddWindowsNamespacePrefix(std::wstring_view absolutePath)
    {
      static constexpr std::wstring_view kWindowsNamespacePrefixToPrepend = L"\\??\\";

      Infra::TemporaryString prependedPath;
      prependedPath << kWindowsNamespacePrefixToPrepend << absolutePath;

      return prependedPath;
    }

    bool PathBeginsWithDriveLetter(std::wstring_view absolutePath)
    {
      std::wstring_view absolutePathWithoutWindowsPrefix =
          absolutePath.substr(PathGetWindowsNamespacePrefix(absolutePath).length());

      if (absolutePathWithoutWindowsPrefix.length() < kPathDriveLetterPrefixLengthChars)
        return false;

      if ((0 != std::iswalpha(absolutePathWithoutWindowsPrefix[0])) &&
          (L':' == absolutePathWithoutWindowsPrefix[1]) &&
          (L'\\' == absolutePathWithoutWindowsPrefix[2]))
        return true;

      return false;
    }

    std::wstring_view PathGetParentDirectory(std::wstring_view path)
    {
      std::wstring_view pathTrimmed = path.substr(PathGetWindowsNamespacePrefix(path).length());

      size_t numTrailingBackslashes = 0;
      while (pathTrimmed.ends_with(L'\\'))
      {
        pathTrimmed.remove_suffix(1);
        numTrailingBackslashes += 1;
      }

      const size_t lastBackslashPos = pathTrimmed.find_last_of(L"\\");
      if (std::wstring_view::npos == lastBackslashPos) return std::wstring_view();

      path.remove_suffix(pathTrimmed.length() - lastBackslashPos + numTrailingBackslashes);
      return path;
    }

    std::wstring_view PathGetWindowsNamespacePrefix(std::wstring_view absolutePath)
    {
      static constexpr std::wstring_view kKnownWindowsNamespacePrefixes[] = {
          L"\\??\\", L"\\\\?\\", L"\\\\.\\"};

      for (const auto& windowsNamespacePrefix : kKnownWindowsNamespacePrefixes)
      {
        if (true == absolutePath.starts_with(windowsNamespacePrefix))
          return absolutePath.substr(0, windowsNamespacePrefix.length());
      }

      return std::wstring_view();
    }

    bool PathIsVolumeRoot(std::wstring_view absolutePath)
    {
      return PathBeginsWithDriveLetter(absolutePath) &&
          ((kPathDriveLetterPrefixLengthChars +
            PathGetWindowsNamespacePrefix(absolutePath).length()) == absolutePath.length());
    }

    Infra::TemporaryString UniqueTemporaryDirectory(void)
    {
      static std::unordered_set<size_t> generatedRandomNumbers;
      size_t newRandomNumber = 0;
      do
      {
        static auto kFixedTimestamp = GetTickCount();
        newRandomNumber = std::hash<size_t>()(
            static_cast<size_t>(std::rand()) + static_cast<size_t>(kFixedTimestamp) +
            static_cast<size_t>(Infra::ProcessInfo::GetCurrentProcessId()));
      }
      while (false == generatedRandomNumbers.insert(newRandomNumber).second);

      Infra::TemporaryString tempDirectoryBase;
      tempDirectoryBase.UnsafeSetSize(
          GetTempPathW(tempDirectoryBase.Capacity(), tempDirectoryBase.Data()));
      if (true == tempDirectoryBase.Empty()) return L"";
      if (L'\\' != tempDirectoryBase.Back()) tempDirectoryBase += L'\\';

      return Infra::Strings::Format(
          L"%s%.*s_%zx",
          tempDirectoryBase.AsCString(),
          static_cast<int>(Infra::ProcessInfo::GetProductName().length()),
          Infra::ProcessInfo::GetProductName().data(),
          newRandomNumber);
    }
  } // namespace Strings
} // namespace Pathwinder
