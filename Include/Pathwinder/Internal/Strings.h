/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file Strings.h
 *   Declaration of common strings and functions to manipulate them.
 **************************************************************************************************/

#pragma once

#include <sal.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#include "ApiWindows.h"
#include "TemporaryBuffer.h"

namespace Pathwinder
{
  namespace Strings
  {
    // These strings can safely be used at any time, including to perform static initialization.
    // Views are guaranteed to be null-terminated.

    /// Delimiter used to separate portions of a string that are to be taken as literals versus
    /// to be taken as named references.
    inline constexpr std::wstring_view kStrDelimiterReferenceVsLiteral = L"%";

    /// Delimiter used to separate a named reference into a domain part and a name part.
    inline constexpr std::wstring_view kStrDelimterReferenceDomainVsName = L"::";

    /// Domain part of a named reference that identifies the domain as being a built-in string.
    inline constexpr std::wstring_view kStrReferenceDomainBuiltin = L"BUILTIN";

    /// Domain part of a named reference that identifies the domain as being a definition
    /// contained in the configuration file.
    inline constexpr std::wstring_view kStrReferenceDomainConfigDefinition = L"CONF";

    /// Domain part of a named reference that identifies the domain as being an environment
    /// variable.
    inline constexpr std::wstring_view kStrReferenceDomainEnvironmentVariable = L"ENV";

    /// Domain part of a named reference that identifies the domain as being a shell "known
    /// folder" identifier.
    inline constexpr std::wstring_view kStrReferenceDomainKnownFolderIdentifier = L"FOLDERID";

    /// Configuration file setting for enabling or disabling "dry run" mode.
    inline constexpr std::wstring_view kStrConfigurationSettingDryRun = L"DryRun";

    /// Configuration file setting for enabling and specifying the verbosity of output to the
    /// log file.
    inline constexpr std::wstring_view kStrConfigurationSettingLogLevel = L"LogLevel";

    /// Configuration file section for defining variables.
    inline constexpr std::wstring_view kStrConfigurationSectionDefinitions = L"Definitions";

    /// Prefix for configuration file sections that define filesystem rules.
    inline constexpr std::wstring_view kStrConfigurationSectionFilesystemRulePrefix =
        L"FilesystemRule:";

    /// Configuration file setting for identifying the origin directory of a filesystem rule.
    inline constexpr std::wstring_view kStrConfigurationSettingFilesystemRuleOriginDirectory =
        L"OriginDirectory";

    /// Configuration file setting for identifying the target directory of a filesystem rule.
    inline constexpr std::wstring_view kStrConfigurationSettingFilesystemRuleTargetDirectory =
        L"TargetDirectory";

    /// Configuration file setting for specifying a redirection mode for a filesystem rule.
    inline constexpr std::wstring_view kStrConfigurationSettingFilesystemRuleRedirectMode =
        L"RedirectMode";

    /// Configuration file setting for specifying a file pattern for a filesystem rule.
    inline constexpr std::wstring_view kStrConfigurationSettingFilesystemRuleFilePattern =
        L"FilePattern";

    // These strings are not safe to access before run-time, and should not be used to perform
    // dynamic initialization. Views are guaranteed to be null-terminated.

    /// Product name.
    /// Use this to identify Pathwinder in areas of user interaction.
    extern const std::wstring_view kStrProductName;

    /// Complete path and filename of the currently-running executable.
    extern const std::wstring_view kStrExecutableCompleteFilename;

    /// Base name of the currently-running executable.
    extern const std::wstring_view kStrExecutableBaseName;

    /// Directory name of the currently-running executable, without trailing backslash.
    extern const std::wstring_view kStrExecutableDirectoryName;

    /// Complete path and filename of the currently-running form of Pathwinder.
    extern const std::wstring_view kStrPathwinderCompleteFilename;

    /// Base name of the currently-running form of Pathwinder.
    extern const std::wstring_view kStrPathwinderBaseName;

    /// Directory name of the currently-running form of Pathwinder, without trailing backslash.
    extern const std::wstring_view kStrPathwinderDirectoryName;

    /// Expected filename of a configuration file.
    /// Pathwinder configuration filename = (Pathwinder directory)\Pathwinder.ini
    extern const std::wstring_view kStrConfigurationFilename;

    /// Expected filename for the log file.
    /// Pathwinder log filename = (current user's desktop)\Pathwinder_(base name of the running
    /// executable)_(process ID).log
    extern const std::wstring_view kStrLogFilename;

    /// Compares two strings without regard for the case of each individual character.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    /// @param [in] strA First string in the comparison.
    /// @param [in] strB Second string in the comparison.
    /// @return Negative number if strA is "less than" strB, positive number of strA is "greater
    /// than" strB, and 0 if the two strings are equal.
    template <typename CharType> int CompareCaseInsensitive(
        std::basic_string_view<CharType> strA, std::basic_string_view<CharType> strB);

    /// Converts characters in a narrow character string to wide character format.
    /// @param [in] str Null-terminated string to convert.
    /// @return Result of the conversion, or an empty string on failure.
    TemporaryString ConvertStringNarrowToWide(const char* str);

    /// Converts characters in a wide character string to narrow character format.
    /// @param [in] str Null-terminated string to convert.
    /// @return Result of the conversion, or an empty string on failure.
    TemporaryBuffer<char> ConvertStringWideToNarrow(const wchar_t* str);

    /// Checks if two strings are equal without regard for the case of each individual
    /// character.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    /// @param [in] strA First string in the comparison.
    /// @param [in] strB Second string in the comparison.
    /// @return `true` if the strings compare equal, `false` otherwise.
    template <typename CharType> bool EqualsCaseInsensitive(
        std::basic_string_view<CharType> strA, std::basic_string_view<CharType> strB);

    /// Determines if the specified filename matches the specified file pattern. An empty file
    /// pattern is presumed to match everything. Input filename must not contain any backslash
    /// separators, as it is intended to represent a file within a directory rather than a path.
    /// Input file pattern must be in upper-case due to an implementation quirk in the
    /// underlying Windows API that implements pattern matching.
    /// @param [in] fileName Filename to check.
    /// @param [in] filePatternUpperCase File pattern to be used for comparison with the file
    /// name.
    /// @return `true` if the file name matches the supplied pattern or if it is entirely empty,
    /// `false` otherwise.
    bool FileNameMatchesPattern(std::wstring_view fileName, std::wstring_view filePatternUpperCase);

    /// Formats a string and returns the result in a newly-allocated null-terminated temporary
    /// buffer.
    /// @param [in] format Format string, possibly with format specifiers which must be matched
    /// with the arguments that follow.
    /// @return Resulting string after all formatting is applied.
    TemporaryString FormatString(_Printf_format_string_ const wchar_t* format, ...);

    /// Computes a hash code for the specified string, without regard to case.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    /// @param [in] str String for which a hash code is desired.
    /// @return Resulting hash code for the input string.
    template <typename CharType> size_t HashCaseInsensitive(std::basic_string_view<CharType> str);

    /// Copies the specified absolute path and prepends it with an appropriate Windows namespace
    /// prefix for identifying file paths to Windows system calls. Invoke this function with an
    /// empty string as the input parameter to return a new string object filled with just the
    /// prefix.
    /// @param [in] absolutePath Absolute path to be prepended with a prefix.
    /// @return Absolute path with a prefix inserted in front of it.
    TemporaryString PathAddWindowsNamespacePrefix(std::wstring_view absolutePath);

    /// Generates a string representation of the specified access mask.
    /// @param [in] accessMask Access mask, typically received from an application when creating or
    /// opening a file.
    /// @return String representation of the access mask.
    TemporaryString NtAccessMaskToString(ACCESS_MASK accessMask);

    /// Generates a string representation of the specified create disposition value.
    /// @param [in] createDisposition Creation disposition options, typically received from an
    /// application when creating or opening a file.
    /// @return String representation of the create disposition.
    TemporaryString NtCreateDispositionToString(ULONG createDisposition);

    /// Generates a string representation of the specified create/open options flags.
    /// @param [in] createOrOpenOptions Create or open options flags.
    /// @return String representation of the create or open options flags.
    TemporaryString NtCreateOrOpenOptionsToString(ULONG createOrOpenOptions);

    /// Generates a string representation of the specified share access flags.
    /// @param [in] shareAccess Share access flags, typically received from an application when
    /// creating or opening a file.
    /// @return String representation of the share access flags.
    TemporaryString NtShareAccessToString(ULONG shareAccess);

    /// Converts a standard string view to a Windows internal Unicode string view.
    /// @param [in] strView Standard string view to convert.
    /// @return Resulting Windows internal Unicode string view.
    UNICODE_STRING NtConvertStringViewToUnicodeString(std::wstring_view strView);

    /// Converts a Windows internal Unicode string view to a standard string view.
    /// @param [in] unicodeStr Unicode string view to convert.
    /// @return Resulting standard string view.
    inline std::wstring_view NtConvertUnicodeStringToStringView(const UNICODE_STRING& unicodeStr)
    {
      return std::wstring_view(unicodeStr.Buffer, (unicodeStr.Length / sizeof(wchar_t)));
    }

    /// Determines if the specified absolute path begins with a drive letter. A valid drive letter
    /// prefix consists of a letter, a colon, and a backslash.
    /// @param [in] absolutePath Absolute path to check, with or without any Windows namespace
    /// prefixes.
    /// @return `true` if the path begins with a drive letter, `false` otherwise.
    bool PathBeginsWithDriveLetter(std::wstring_view absolutePath);

    /// Trims the specified path at the last backslash to obtain the parent directory.
    /// @param [in] path Path to check, absolute or relative, and with or without any Windows
    /// namespace prefixes.
    /// @return View of the specified path that consists of its parent directory, without a trailing
    /// backslash, or an empty view if the path is already a filesystem root and no parent exists.
    std::wstring_view PathGetParentDirectory(std::wstring_view path);

    /// Returns a view of the Windows namespace prefix from the supplied absolute path, if it is
    /// present.
    /// @param [in] absolutePath Absolute path to check for a prefix.
    /// @return View within the input absolute path containing the Windows namespace prefix, or
    /// an empty view if the input absolute path does not contain such a prefix.
    std::wstring_view PathGetWindowsNamespacePrefix(std::wstring_view absolutePath);

    /// Determines if the provided absolute path contains a prefix identifying a Windows
    /// namespace.
    /// @param [in] absolutePath Absolute path to check for a prefix.
    /// @return `true` if the supplied absolute path contains a prefix, `false` otherwise.
    inline bool PathHasWindowsNamespacePrefix(std::wstring_view absolutePath)
    {
      return (0 != PathGetWindowsNamespacePrefix(absolutePath).length());
    }

    /// Determines if the provided path is a volume root, meaning that it identifies a volume (in a
    /// way that Pathwinder recognizes) but does not contain any other path information.
    /// @param absolutePath Absolute path to check.
    /// @return Whether or not the supplied absolute path is a volume root path.
    bool PathIsVolumeRoot(std::wstring_view absolutePath);

    /// Removes the all occurrences of specified leading character from the input string view
    /// and returns the result.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    /// @param [in] str String view from which to remove the leading character.
    /// @param [in] leadingChar Leading character to strip from this string.
    /// @return Resulting string view after the leading character is removed.
    template <typename CharType> inline std::basic_string_view<CharType> RemoveLeading(
        std::basic_string_view<CharType> str, CharType leadingChar)
    {
      while (str.starts_with(leadingChar))
        str.remove_prefix(1);

      return str;
    }

    /// Removes the all occurrences of specified trailing character from the input string view
    /// and returns the result.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    /// @param [in] str String view from which to remove the trailing character.
    /// @param [in] trailingChar Trailing character to strip from this string.
    /// @return Resulting string view after the trailing character is removed.
    template <typename CharType> inline std::basic_string_view<CharType> RemoveTrailing(
        std::basic_string_view<CharType> str, CharType trailingChar)
    {
      while (str.ends_with(trailingChar))
        str.remove_suffix(1);

      return str;
    }

    /// Splits a string using the specified delimiter character and returns a list of views each
    /// corresponding to a part of the input string. If there are too many delimiters present
    /// such that not all of the pieces can fit into the returned container type then the
    /// returned container will be empty. Otherwise the returned container will contain at least
    /// one element.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    /// @param [in] stringToSplit Input string to be split.
    /// @param [in] delimiter Delimiter character sequence that identifies boundaries between
    /// pieces of the input string.
    /// @return Container that holds views referring to pieces of the input string split using
    /// the specified delimiter.
    template <typename CharType> TemporaryVector<std::basic_string_view<CharType>> SplitString(
        std::basic_string_view<CharType> stringToSplit, std::basic_string_view<CharType> delimiter);

    /// Splits a string using the specified delimiter strings and returns a list of views each
    /// corresponding to a part of the input string. If there are too many delimiters present
    /// such that not all of the pieces can fit into the returned container type then the
    /// returned container will be empty. Otherwise the returned container will contain at least
    /// one element.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    /// @param [in] stringToSplit Input string to be split.
    /// @param [in] delimiters Pointer to an array of delimiter character sequences each of
    /// which identifies a boundary between pieces of the input string.
    /// @param [in] numDelimiters Number of delimiters contained in the delimiter array.
    /// @return Container that holds views referring to pieces of the input string split using
    /// the specified delimiter.
    template <typename CharType> TemporaryVector<std::basic_string_view<CharType>> SplitString(
        std::basic_string_view<CharType> stringToSplit,
        const std::basic_string_view<CharType>* delimiters,
        unsigned int numDelimiters);

    /// Checks if one string is a prefix of another without regard for the case of each
    /// individual character.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    /// @param [in] str String to be checked for a possible prefix.
    /// @param [in] maybePrefix Candidate prefix to compare with the beginning of the string.
    /// @return `true` if the candidate prefix is a prefix of the specified string, `false`
    /// otherwise.
    template <typename CharType> bool StartsWithCaseInsensitive(
        std::basic_string_view<CharType> str, std::basic_string_view<CharType> maybePrefix);

    /// Tokenizes a string, one token at a time, using the specified delimiter. Returns the next
    /// token found and updates the state variable for subsequent calls. Each invocation returns
    /// the next token found in the input string.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    /// @param [in, out] tokenizeState Opaque state variable that tracks tokenizing progress.
    /// Must be 0 on first invocation and preserved between invocations on the same input
    /// string.
    /// @param [in] stringToTokenize String to be tokenized.
    /// @param [in] delimiter Delimiter, which can consist of multiple characters, that
    /// separates tokens in the input string. Can additionally vary between invocations.
    /// @return Next token found in the input string, if it exists.
    template <typename CharType> std::optional<std::basic_string_view<CharType>> TokenizeString(
        size_t& tokenizeState,
        std::basic_string_view<CharType> stringToTokenize,
        std::basic_string_view<CharType> delimiter);

    /// Tokenizes a string, one token at a time, using the specified delimiter strings. Returns
    /// the next token found and updates the state variable for subsequent calls. Each
    /// invocation returns the next token found in the input string.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    /// @param [in, out] tokenizeState Opaque state variable that tracks tokenizing progress.
    /// Must be 0 on first invocation and preserved between invocations on the same input
    /// string.
    /// @param [in] stringToTokenize String to be tokenized.
    /// @param [in] delimiters Pointer to an array of delimiter character sequences each of
    /// which identifies a boundary between pieces of the input string.
    /// @param [in] numDelimiters Number of delimiters contained in the delimiter array.
    /// @return Next token found in the input string, if it exists.
    template <typename CharType> std::optional<std::basic_string_view<CharType>> TokenizeString(
        size_t& tokenizeState,
        std::basic_string_view<CharType> stringToTokenize,
        const std::basic_string_view<CharType>* delimiters,
        unsigned int numDelimiters);

    /// Generates a string representation of a system error code.
    /// @param [in] systemErrorCode System error code for which to generate a string.
    /// @return String representation of the system error code.
    TemporaryString SystemErrorCodeString(const unsigned long systemErrorCode);

    /// Case-insensitive hasher for various kinds of string representations. This is a
    /// type-transparent hasher for all string representations that are implicitly convertable
    /// to string views.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    template <typename CharType> struct CaseInsensitiveHasher
    {
      using is_transparent = void;

      constexpr size_t operator()(const std::basic_string_view<CharType>& key) const
      {
        return HashCaseInsensitive(key);
      }
    };

    /// Case-insensitive equality comparator for various kinds of string representations. This
    /// is a type-transparent comparator for all string representations that are implicitly
    /// convertable to string views.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    template <typename CharType> struct CaseInsensitiveEqualityComparator
    {
      using is_transparent = void;

      constexpr bool operator()(
          const std::basic_string_view<CharType>& lhs,
          const std::basic_string_view<CharType>& rhs) const
      {
        return EqualsCaseInsensitive(lhs, rhs);
      }
    };

    /// Case-insensitive greater-than comparator for various kinds of string representations.
    /// This is a type-transparent comparator for all string representations that are implicitly
    /// convertable to string views.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    template <typename CharType> struct CaseInsensitiveGreaterThanComparator
    {
      using is_transparent = void;

      constexpr bool operator()(
          const std::basic_string_view<CharType>& lhs,
          const std::basic_string_view<CharType>& rhs) const
      {
        return (CompareCaseInsensitive(lhs, rhs) > 0);
      }
    };

    /// Case-insensitive less-than comparator for various kinds of string representations. This
    /// is a type-transparent comparator for all string representations that are implicitly
    /// convertable to string views.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    template <typename CharType> struct CaseInsensitiveLessThanComparator
    {
      using is_transparent = void;

      constexpr bool operator()(
          const std::basic_string_view<CharType>& lhs,
          const std::basic_string_view<CharType>& rhs) const
      {
        return (CompareCaseInsensitive(lhs, rhs) < 0);
      }
    };

    /// Captures the state of a tokenization operation and exposes it via an iterator interface.
    /// Intended to be constructed directly within a range-based loop to provide simple
    /// iteration over all the tokens in a string.
    template <typename CharType> class Tokenizer
    {
    public:

      /// Iterator type used to hold the complete state of a tokenization operator.
      /// Implements the minimum functionality needed for single-step forward iteration
      /// through the tokens of a string.
      class Iterator
      {
      public:

        /// Iterator state indicator to be used by all one-past-the-end iterators.
        static constexpr size_t kTokenizeStateEnd = std::numeric_limits<size_t>::max();

        constexpr Iterator(
            const Tokenizer& tokenizer,
            size_t tokenizeState = 0,
            std::optional<std::basic_string_view<CharType>> token = std::nullopt)
            : tokenizer(tokenizer), tokenizeState(tokenizeState), token(token)
        {}

        constexpr const std::basic_string_view<CharType>& operator*(void) const
        {
          return *token;
        }

        constexpr Iterator& operator++(void)
        {
          if (nullptr != tokenizer.multiDelimiters)
            token = TokenizeString(
                tokenizeState,
                tokenizer.stringToTokenize,
                tokenizer.multiDelimiters,
                tokenizer.numDelimiters);
          else
            token = TokenizeString(tokenizeState, tokenizer.stringToTokenize, tokenizer.delimiter);

          if (false == token.has_value()) tokenizeState = kTokenizeStateEnd;

          return *this;
        }

        constexpr bool operator==(const Iterator& other) const
        {
          DebugAssert(
              &tokenizer == &(other.tokenizer),
              "Iterators refer to different tokenization operations.");
          return ((tokenizeState == other.tokenizeState) && (token == other.token));
        }

      private:

        /// Read-only reference to the tokenizer object itself.
        const Tokenizer& tokenizer;

        /// Current internal state of the tokenization operation.
        size_t tokenizeState;

        /// Last token that was successfully tokenized.
        std::optional<std::basic_string_view<CharType>> token;
      };

      constexpr Tokenizer(
          std::basic_string_view<CharType> stringToTokenize,
          std::basic_string_view<CharType> delimiter)
          : stringToTokenize(stringToTokenize),
            delimiter(delimiter),
            multiDelimiters(nullptr),
            numDelimiters(1)
      {}

      constexpr Tokenizer(
          std::basic_string_view<CharType> stringToTokenize,
          const std::basic_string<CharType>& delimiter)
          : Tokenizer(stringToTokenize, std::basic_string_view<CharType>(delimiter))
      {}

      constexpr Tokenizer(
          std::basic_string_view<CharType> stringToTokenize, const CharType* delimiter)
          : Tokenizer(stringToTokenize, std::basic_string_view<CharType>(delimiter))
      {}

      constexpr Tokenizer(
          std::basic_string_view<CharType> stringToTokenize,
          const std::basic_string_view<CharType>* delimiters,
          unsigned int numDelimiters)
          : stringToTokenize(stringToTokenize),
            delimiter(),
            multiDelimiters(delimiters),
            numDelimiters(numDelimiters)
      {}

      Iterator begin(void) const
      {
        return ++Iterator(*this);
      }

      Iterator end(void) const
      {
        return Iterator(*this, Iterator::kTokenizeStateEnd);
      }

      /// Instantiates a tokenizer object configured to tokenize a special type of list of strings
      /// used throughout Windows APIs. These lists place multiple strings into a single buffer and
      /// delimit them using a single null character, with the end of the list being identified by
      /// multiple consecutive null characters.
      static constexpr Tokenizer NullDelimitedList(
          std::basic_string_view<CharType> stringToTokenize)
      {
        static constexpr CharType kNullDelimiter = static_cast<CharType>(0);

        return Tokenizer(
            RemoveTrailing(stringToTokenize, kNullDelimiter),
            std::wstring_view(&kNullDelimiter, 1));
      }

    private:

      /// String to be tokenized.
      const std::basic_string_view<CharType> stringToTokenize;

      /// Single delimiter to be used for tokenization.
      /// To be filled if this object is constructed with a single delimiter.
      const std::basic_string_view<CharType> delimiter;

      /// Pointer to an array of multiple delimiters to be used for tokenization.
      /// To be filled if this object is constructed with multiple delimiters.
      const std::basic_string_view<CharType>* const multiDelimiters;

      /// Number of delimiters.
      const unsigned int numDelimiters;
    };

    /// Special-purpose tokenization class for certain lists of strings used throughout Windows
    /// APIs. These lists place multiple wide-character strings into a single buffer and delimits
    /// them using a single null character, with the end of the list being identified by two
    /// consecutive null characters. Intended to be constructed directly within a range-based loop
    /// to provide simple iteration over all the individual list items in a null-delimited list of
    /// strings.

  } // namespace Strings
} // namespace Pathwinder
