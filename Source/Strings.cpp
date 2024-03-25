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

#include "ApiWindows.h"
#include "DebugAssert.h"
#include "Globals.h"
#include "TemporaryBuffer.h"

namespace Pathwinder
{
  namespace Strings
  {
    /// File extension for a configuration file.
    static constexpr std::wstring_view kStrConfigurationFileExtension = L".ini";

    /// File extension for a log file.
    static constexpr std::wstring_view kStrLogFileExtension = L".log";

    /// Length of a drive letter prefix, in characters. A drive letter prefix consists of a single
    /// letter, a colon, and a backslash, for a total of three characters.
    static constexpr size_t kPathDriveLetterPrefixLengthChars = 3;

    /// Converts a single character to lowercase.
    /// Default implementation does nothing useful.
    /// @tparam CharType Character type.
    /// @param [in] c Character to convert.
    /// @return Null character, as the default implementation does nothing useful.
    template <typename CharType> static inline CharType ToLowercase(CharType c)
    {
      return L'\0';
    }

    /// Converts a single narrow character to lowercase.
    /// @tparam CharType Character type.
    /// @param [in] c Character to convert.
    /// @return Lowercase version of the input, if a conversion is possible, or the same
    /// character as the input otherwise.
    template <> char static inline ToLowercase(char c)
    {
      return std::tolower(c);
    }

    /// Converts a single wide character to lowercase.
    /// Default implementation does nothing useful.
    /// @tparam CharType Character type.
    /// @param [in] c Character to convert.
    /// @return Lowercase version of the input, if a conversion is possible, or the same
    /// character as the input otherwise.
    template <> wchar_t static inline ToLowercase(wchar_t c)
    {
      return std::towlower(c);
    }

    /// Generates the value for kStrProductName; see documentation of this run-time constant for
    /// more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetProductName(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            const wchar_t* stringStart = nullptr;
            int stringLength = LoadString(
                Globals::GetInstanceHandle(),
                IDS_PATHWINDER_PRODUCT_NAME,
                (wchar_t*)&stringStart,
                0);

            while ((stringLength > 0) && (L'\0' == stringStart[stringLength - 1]))
              stringLength -= 1;

            if (stringLength > 0) initString.assign(stringStart, &stringStart[stringLength]);
          });

      return initString;
    }

    /// Generates the value for kStrExecutableCompleteFilename; see documentation of this
    /// run-time constant for more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetExecutableCompleteFilename(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            TemporaryBuffer<wchar_t> buf;
            GetModuleFileName(nullptr, buf.Data(), static_cast<DWORD>(buf.Capacity()));

            initString.assign(buf.Data());
          });

      return initString;
    }

    /// Generates the value for kStrExecutableBaseName; see documentation of this run-time
    /// constant for more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetExecutableBaseName(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            std::wstring_view executableBaseName = GetExecutableCompleteFilename();

            const size_t lastBackslashPos = executableBaseName.find_last_of(L"\\");
            if (std::wstring_view::npos != lastBackslashPos)
              executableBaseName.remove_prefix(1 + lastBackslashPos);

            initString.assign(executableBaseName);
          });

      return initString;
    }

    /// Generates the value for kStrExecutableDirectoryName; see documentation of this run-time
    /// constant for more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetExecutableDirectoryName(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            std::wstring_view executableDirectoryName = GetExecutableCompleteFilename();

            const size_t lastBackslashPos = executableDirectoryName.find_last_of(L"\\");
            if (std::wstring_view::npos != lastBackslashPos)
            {
              executableDirectoryName.remove_suffix(
                  executableDirectoryName.length() - lastBackslashPos);
              initString.assign(executableDirectoryName);
            }
          });

      return initString;
    }

    /// Generates the value for kStrPathwinderCompleteFilename; see documentation of this
    /// run-time constant for more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetPathwinderCompleteFilename(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            TemporaryBuffer<wchar_t> buf;
            GetModuleFileName(
                Globals::GetInstanceHandle(), buf.Data(), static_cast<DWORD>(buf.Capacity()));

            initString.assign(buf.Data());
          });

      return initString;
    }

    /// Generates the value for kStrPathwinderBaseName; see documentation of this run-time
    /// constant for more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetPathwinderBaseName(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            std::wstring_view executableBaseName = GetPathwinderCompleteFilename();

            const size_t lastBackslashPos = executableBaseName.find_last_of(L"\\");
            if (std::wstring_view::npos != lastBackslashPos)
              executableBaseName.remove_prefix(1 + lastBackslashPos);

            initString.assign(executableBaseName);
          });

      return initString;
    }

    /// Generates the value for kStrPathwinderDirectoryName; see documentation of this run-time
    /// constant for more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetPathwinderDirectoryName(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            std::wstring_view executableDirectoryName = GetPathwinderCompleteFilename();

            const size_t lastBackslashPos = executableDirectoryName.find_last_of(L"\\");
            if (std::wstring_view::npos != lastBackslashPos)
            {
              executableDirectoryName.remove_suffix(
                  executableDirectoryName.length() - lastBackslashPos);
              initString.assign(executableDirectoryName);
            }
          });

      return initString;
    }

    /// Generates the value for kStrNetBiosHostname; see documentation of this run-time
    /// constant for more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetNetBiosHostname(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            TemporaryBuffer<wchar_t> buf;
            DWORD bufsize = static_cast<DWORD>(buf.Capacity());

            GetComputerNameEx(ComputerNamePhysicalNetBIOS, buf.Data(), &bufsize);

            initString.assign(buf.Data());
          });

      return initString;
    }

    /// Generates the value for kStrDnsHostname; see documentation of this run-time
    /// constant for more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetDnsHostname(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            TemporaryBuffer<wchar_t> buf;
            DWORD bufsize = static_cast<DWORD>(buf.Capacity());

            GetComputerNameEx(ComputerNamePhysicalDnsHostname, buf.Data(), &bufsize);

            initString.assign(buf.Data());
          });

      return initString;
    }

    /// Generates the value for kStrDnsDomain; see documentation of this run-time
    /// constant for more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetDnsDomain(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            TemporaryBuffer<wchar_t> buf;
            DWORD bufsize = static_cast<DWORD>(buf.Capacity());

            GetComputerNameEx(ComputerNamePhysicalDnsDomain, buf.Data(), &bufsize);

            initString.assign(buf.Data());
          });

      return initString;
    }

    /// Generates the value for kStrDnsFullyQualified; see documentation of this run-time
    /// constant for more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetDnsFullyQualified(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            TemporaryBuffer<wchar_t> buf;
            DWORD bufsize = static_cast<DWORD>(buf.Capacity());

            GetComputerNameEx(ComputerNamePhysicalDnsFullyQualified, buf.Data(), &bufsize);

            initString.assign(buf.Data());
          });

      return initString;
    }

    /// Generates the value for kStrConfigurationFilename; see documentation of this run-time
    /// constant for more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetConfigurationFilename(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            std::wstring_view pieces[] = {
                GetPathwinderDirectoryName(),
                L"\\",
                GetProductName(),
                kStrConfigurationFileExtension};

            size_t totalLength = 0;
            for (int i = 0; i < _countof(pieces); ++i)
              totalLength += pieces[i].length();

            initString.reserve(1 + totalLength);

            for (int i = 0; i < _countof(pieces); ++i)
              initString.append(pieces[i]);
          });

      return initString;
    }

    /// Generates the value for kStrLogFilename; see documentation of this run-time constant for
    /// more information.
    /// @return Corresponding run-time constant value.
    static const std::wstring& GetLogFilename(void)
    {
      static std::wstring initString;
      static std::once_flag initFlag;

      std::call_once(
          initFlag,
          []() -> void
          {
            TemporaryString logFilename;

            PWSTR knownFolderPath;
            const HRESULT result =
                SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &knownFolderPath);

            if (S_OK == result)
            {
              logFilename << knownFolderPath << L'\\';
              CoTaskMemFree(knownFolderPath);
            }

            logFilename << GetProductName().c_str() << L'_' << GetExecutableBaseName().c_str()
                        << L'_' << Globals::GetCurrentProcessId() << kStrLogFileExtension;

            initString.assign(logFilename);
          });

      return initString;
    }

    extern const std::wstring_view kStrProductName(GetProductName());
    extern const std::wstring_view kStrExecutableCompleteFilename(GetExecutableCompleteFilename());
    extern const std::wstring_view kStrExecutableBaseName(GetExecutableBaseName());
    extern const std::wstring_view kStrExecutableDirectoryName(GetExecutableDirectoryName());
    extern const std::wstring_view kStrPathwinderCompleteFilename(GetPathwinderCompleteFilename());
    extern const std::wstring_view kStrPathwinderBaseName(GetPathwinderBaseName());
    extern const std::wstring_view kStrPathwinderDirectoryName(GetPathwinderDirectoryName());
    extern const std::wstring_view kStrNetBiosHostname(GetNetBiosHostname());
    extern const std::wstring_view kStrDnsHostname(GetDnsHostname());
    extern const std::wstring_view kStrDnsDomain(GetDnsDomain());
    extern const std::wstring_view kStrDnsFullyQualified(GetDnsFullyQualified());
    extern const std::wstring_view kStrConfigurationFilename(GetConfigurationFilename());
    extern const std::wstring_view kStrLogFilename(GetLogFilename());

    template <typename CharType> int CompareCaseInsensitive(
        std::basic_string_view<CharType> strA, std::basic_string_view<CharType> strB)
    {
      for (size_t i = 0; i < std::min(strA.length(), strB.length()); ++i)
      {
        const wchar_t charA = ToLowercase(strA[i]);
        const wchar_t charB = ToLowercase(strB[i]);

        if (charA != charB) return (static_cast<int>(charA) - static_cast<int>(charB));
      }

      return (static_cast<int>(strA.length()) - static_cast<int>(strB.length()));
    }

    template int CompareCaseInsensitive<char>(std::string_view, std::string_view);
    template int CompareCaseInsensitive<wchar_t>(std::wstring_view, std::wstring_view);

    TemporaryString ConvertStringNarrowToWide(const char* str)
    {
      TemporaryString convertedStr;
      size_t numCharsConverted = 0;

      if (0 ==
          mbstowcs_s(
              &numCharsConverted,
              convertedStr.Data(),
              convertedStr.Capacity(),
              str,
              static_cast<size_t>(convertedStr.Capacity()) - 1))
        convertedStr.UnsafeSetSize(static_cast<unsigned int>(numCharsConverted));

      return convertedStr;
    }

    TemporaryBuffer<char> ConvertStringWideToNarrow(const wchar_t* str)
    {
      TemporaryBuffer<char> convertedStr;
      size_t numCharsConverted = 0;

      if (0 !=
          wcstombs_s(
              &numCharsConverted,
              convertedStr.Data(),
              convertedStr.Capacity(),
              str,
              static_cast<size_t>(convertedStr.Capacity()) - 1))
        convertedStr[0] = '\0';

      return convertedStr;
    }

    template <typename CharType> bool EqualsCaseInsensitive(
        std::basic_string_view<CharType> strA, std::basic_string_view<CharType> strB)
    {
      if (strA.length() != strB.length()) return false;

      for (size_t i = 0; i < strA.length(); ++i)
      {
        if (ToLowercase(strA[i]) != ToLowercase(strB[i])) return false;
      }

      return true;
    }

    template bool EqualsCaseInsensitive<char>(std::string_view, std::string_view);
    template bool EqualsCaseInsensitive<wchar_t>(std::wstring_view, std::wstring_view);

    bool FileNameMatchesPattern(std::wstring_view fileName, std::wstring_view filePatternUpperCase)
    {
      if (true == filePatternUpperCase.empty()) return true;

      UNICODE_STRING fileNameString =
          Pathwinder::Strings::NtConvertStringViewToUnicodeString(fileName);
      UNICODE_STRING filePatternString =
          Pathwinder::Strings::NtConvertStringViewToUnicodeString(filePatternUpperCase);

      return (
          TRUE ==
          Pathwinder::WindowsInternal::RtlIsNameInExpression(
              &filePatternString, &fileNameString, TRUE, nullptr));
    }

    TemporaryString FormatString(_Printf_format_string_ const wchar_t* format, ...)
    {
      TemporaryString buf;

      va_list args;
      va_start(args, format);

      buf.UnsafeSetSize(static_cast<size_t>(vswprintf_s(buf.Data(), buf.Capacity(), format, args)));

      va_end(args);

      return buf;
    }

    template <typename CharType> size_t HashCaseInsensitive(std::basic_string_view<CharType> str)
    {
      // Implements the FNV-1a hash algorithm. References:
      // https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
      // https://softwareengineering.stackexchange.com/questions/49550/which-hashing-algorithm-is-best-for-uniqueness-and-speed/145633#145633

#ifdef _WIN64
      constexpr uint64_t hashPrime = 1099511628211ull;
      uint64_t hash = 14695981039346656037ull;
#else
      constexpr uint32_t hashPrime = 16777619u;
      uint32_t hash = 2166136261u;
#endif
      static_assert(sizeof(size_t) == sizeof(hash), "Hash size mismatch.");

      for (size_t charIndex = 0; charIndex < str.length(); ++charIndex)
      {
        const CharType currentChar = Strings::ToLowercase(str[charIndex]);
        const uint8_t* const charByteBase = reinterpret_cast<const uint8_t*>(&currentChar);
        const size_t charByteCount = sizeof(currentChar);

        for (size_t charByteIndex = 0; charByteIndex < charByteCount; ++charByteIndex)
        {
          const decltype(hash) currentByte =
              static_cast<decltype(hash)>(charByteBase[charByteIndex]);
          hash = hash ^ currentByte;
          hash = hash * hashPrime;
        }
      }

      return hash;
    }

    template size_t HashCaseInsensitive<char>(std::string_view);
    template size_t HashCaseInsensitive<wchar_t>(std::wstring_view);

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

    TemporaryString PathAddWindowsNamespacePrefix(std::wstring_view absolutePath)
    {
      static constexpr std::wstring_view kWindowsNamespacePrefixToPrepend = L"\\??\\";

      TemporaryString prependedPath;
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

    template <typename CharType> TemporaryVector<std::basic_string_view<CharType>> SplitString(
        std::basic_string_view<CharType> stringToSplit, std::basic_string_view<CharType> delimiter)
    {
      return SplitString(stringToSplit, &delimiter, 1);
    }

    template TemporaryVector<std::string_view> SplitString<char>(
        std::string_view, std::string_view);
    template TemporaryVector<std::wstring_view> SplitString<wchar_t>(
        std::wstring_view, std::wstring_view);

    template <typename CharType> TemporaryVector<std::basic_string_view<CharType>> SplitString(
        std::basic_string_view<CharType> stringToSplit,
        const std::basic_string_view<CharType>* delimiters,
        unsigned int numDelimiters)
    {
      TemporaryVector<std::basic_string_view<CharType>> stringPieces;

      auto beginIter = stringToSplit.cbegin();
      auto endIter = beginIter;

      while ((stringPieces.Size() < stringPieces.Capacity()) && (stringToSplit.cend() != endIter))
      {
        bool delimiterFound = false;
        std::basic_string_view<CharType> remainingStringToSplit(endIter, stringToSplit.cend());

        for (unsigned int i = 0; i < numDelimiters; ++i)
        {
          auto delimiter = delimiters[i];

          if (true == delimiter.empty()) continue;

          if (true == remainingStringToSplit.starts_with(delimiter))
          {
            stringPieces.EmplaceBack(beginIter, endIter);
            endIter += delimiter.length();
            beginIter = endIter;
            delimiterFound = true;
            break;
          }
        }

        if (false == delimiterFound) endIter += 1;
      }

      if (stringPieces.Size() < stringPieces.Capacity())
        stringPieces.EmplaceBack(beginIter, endIter);
      else
        stringPieces.Clear();

      return stringPieces;
    }

    template TemporaryVector<std::string_view> SplitString<char>(
        std::string_view, const std::string_view*, unsigned int);
    template TemporaryVector<std::wstring_view> SplitString<wchar_t>(
        std::wstring_view, const std::wstring_view*, unsigned int);

    template <typename CharType> bool StartsWithCaseInsensitive(
        std::basic_string_view<CharType> str, std::basic_string_view<CharType> maybePrefix)
    {
      if (str.length() < maybePrefix.length()) return false;

      str.remove_suffix(str.length() - maybePrefix.length());
      return EqualsCaseInsensitive(str, maybePrefix);
    }

    template bool StartsWithCaseInsensitive<char>(std::string_view, std::string_view);
    template bool StartsWithCaseInsensitive<wchar_t>(std::wstring_view, std::wstring_view);

    TemporaryString SystemErrorCodeString(const unsigned long systemErrorCode)
    {
      TemporaryString systemErrorString;
      DWORD systemErrorLength = FormatMessage(
          FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
          nullptr,
          systemErrorCode,
          0,
          systemErrorString.Data(),
          systemErrorString.Capacity(),
          nullptr);

      if (0 == systemErrorLength)
      {
        systemErrorString =
            FormatString(L"System error %u.", static_cast<unsigned int>(systemErrorCode));
      }
      else
      {
        for (; systemErrorLength > 0; --systemErrorLength)
        {
          if (L'\0' != systemErrorString[systemErrorLength] &&
              !iswspace(systemErrorString[systemErrorLength]))
            break;

          systemErrorString[systemErrorLength] = L'\0';
          systemErrorString.UnsafeSetSize(systemErrorLength);
        }
      }

      return systemErrorString;
    }

    template <typename CharType> std::optional<std::basic_string_view<CharType>> TokenizeString(
        size_t& tokenizeState,
        std::basic_string_view<CharType> stringToTokenize,
        std::basic_string_view<CharType> delimiter)
    {
      return TokenizeString(tokenizeState, stringToTokenize, &delimiter, 1);
    }

    template std::optional<std::string_view> TokenizeString<char>(
        size_t&, std::string_view, std::string_view);
    template std::optional<std::wstring_view> TokenizeString<wchar_t>(
        size_t&, std::wstring_view, std::wstring_view);

    template <typename CharType> std::optional<std::basic_string_view<CharType>> TokenizeString(
        size_t& tokenizeState,
        std::basic_string_view<CharType> stringToTokenize,
        const std::basic_string_view<CharType>* delimiters,
        unsigned int numDelimiters)
    {
      if (stringToTokenize.length() < tokenizeState) return std::nullopt;

      auto beginIter = stringToTokenize.cbegin() + tokenizeState;
      auto endIter = beginIter;

      while (stringToTokenize.cend() != endIter)
      {
        std::basic_string_view<CharType> remainingStringToTokenize(
            endIter, stringToTokenize.cend());

        for (unsigned int i = 0; i < numDelimiters; ++i)
        {
          auto delimiter = delimiters[i];

          if (true == delimiter.empty()) continue;

          if (true == remainingStringToTokenize.starts_with(delimiter))
          {
            tokenizeState += delimiter.length();
            return std::basic_string_view<CharType>(beginIter, endIter);
          }
        }

        tokenizeState += 1;
        endIter += 1;
      }

      tokenizeState = (1 + stringToTokenize.length());
      return std::basic_string_view<CharType>(beginIter, endIter);
    }

    template std::optional<std::string_view> TokenizeString<char>(
        size_t&, std::string_view, const std::string_view*, unsigned int);
    template std::optional<std::wstring_view> TokenizeString<wchar_t>(
        size_t&, std::wstring_view, const std::wstring_view*, unsigned int);
  } // namespace Strings
} // namespace Pathwinder
