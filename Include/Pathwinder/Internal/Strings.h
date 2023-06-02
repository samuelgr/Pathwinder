/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file Strings.h
 *   Declaration of common strings and functions to manipulate them.
 *****************************************************************************/

#pragma once

#include "TemporaryBuffer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sal.h>
#include <string_view>


namespace Pathwinder
{
    namespace Strings
    {
        // -------- COMPILE-TIME CONSTANTS --------------------------------- //
        // Can safely be used at any time, including to perform static initialization.
        // Views are guaranteed to be null-terminated.

        /// Delimiter used to separate portions of a string that are to be taken as literals versus to be taken as named references.
        inline constexpr std::wstring_view kStrDelimiterReferenceVsLiteral = L"%";

        /// Delimiter used to separate a named reference into a domain part and a name part.
        inline constexpr std::wstring_view kStrDelimterReferenceDomainVsName = L"::";

        /// Domain part of a named reference that identifies the domain as being a built-in string.
        inline constexpr std::wstring_view kStrReferenceDomainBuiltin = L"BUILTIN";

        /// Domain part of a named reference that identifies the domain as being a definition contained in the configuration file.
        inline constexpr std::wstring_view kStrReferenceDomainConfigDefinition = L"CONF";

        /// Domain part of a named reference that identifies the domain as being an environment variable.
        inline constexpr std::wstring_view kStrReferenceDomainEnvironmentVariable = L"ENV";

        /// Domain part of a named reference that identifies the domain as being a shell "known folder" identifier.
        inline constexpr std::wstring_view kStrReferenceDomainKnownFolderIdentifier = L"FOLDERID";


        /// Configuration file setting for enabling or disabling "dry run" mode.
        inline constexpr std::wstring_view kStrConfigurationSettingDryRun = L"DryRun";

        /// Configuration file setting for enabling and specifying the verbosity of output to the log file.
        inline constexpr std::wstring_view kStrConfigurationSettingLogLevel = L"LogLevel";


        /// Configuration file section for defining variables.
        inline constexpr std::wstring_view kStrConfigurationSectionDefinitions = L"Definitions";


        /// Prefix for configuration file sections that define filesystem rules.
        inline constexpr std::wstring_view kStrConfigurationSectionFilesystemRulePrefix = L"FilesystemRule:";

        /// Configuration file setting for identifying the origin directory of a filesystem rule.
        inline constexpr std::wstring_view kStrConfigurationSettingFilesystemRuleOriginDirectory = L"OriginDirectory";

        /// Configuration file setting for identifying the target directory of a filesystem rule.
        inline constexpr std::wstring_view kStrConfigurationSettingFilesystemRuleTargetDirectory = L"TargetDirectory";

        /// Configuration file setting for specifying a file pattern for a filesystem rule.
        inline constexpr std::wstring_view kStrConfigurationSettingFilesystemRuleFilePattern = L"FilePattern";


        // -------- RUN-TIME CONSTANTS ------------------------------------- //
        // Not safe to access before run-time, and should not be used to perform dynamic initialization.
        // Views are guaranteed to be null-terminated.

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
        /// Pathwinder log filename = (current user's desktop)\Pathwinder_(base name of the running executable)_(process ID).log
        extern const std::wstring_view kStrLogFilename;


        // -------- FUNCTIONS ---------------------------------------------- //

        /// Converts characters in a narrow character string to wide character format.
        /// @param [in] str Null-terminated string to convert.
        /// @return Result of the conversion, or an empty string on failure.
        TemporaryString ConvertStringNarrowToWide(const char* str);

        /// Converts characters in a wide character string to narrow character format.
        /// @param [in] str Null-terminated string to convert.
        /// @return Result of the conversion, or an empty string on failure.
        TemporaryBuffer<char> ConvertStringWideToNarrow(const wchar_t* str);

        /// Compares two strings without regard for the case of each individual character.
        /// @tparam CharType Type of character in each string, either narrow or wide.
        /// @param [in] strA First string in the comparison.
        /// @param [in] strB Second string in the comparison.
        /// @return `true` if the strings compare equal, `false` otherwise.
        template <typename CharType> bool EqualsCaseInsensitive(std::basic_string_view<CharType> strA, std::basic_string_view<CharType> strB);

        /// Formats a string and returns the result in a newly-allocated null-terminated temporary buffer.
        /// @param [in] format Format string, possibly with format specifiers which must be matched with the arguments that follow.
        /// @return Resulting string after all formatting is applied.
        TemporaryString FormatString(_Printf_format_string_ const wchar_t* format, ...);

        /// Splits a string using the specified delimiter character and returns a list of views each corresponding to a part of the input string.
        /// If there are too many delimiters present such that not all of the pieces can fit into the returned container type then the returned container will be empty.
        /// Otherwise the returned container will contain at least one element.
        /// @tparam CharType Type of character in each string, either narrow or wide.
        /// @param [in] stringToSplit Input string to be split.
        /// @param [in] delimiter Delimiter character sequence that identifies boundaries between pieces of the input string.
        /// @return Container that holds views referring to pieces of the input string split using the specified delimiter.
        template <typename CharType> TemporaryVector<std::basic_string_view<CharType>> SplitString(std::basic_string_view<CharType> stringToSplit, std::basic_string_view<CharType> delimiter);

        /// Splits a string using the specified delimiter strings and returns a list of views each corresponding to a part of the input string.
        /// If there are too many delimiters present such that not all of the pieces can fit into the returned container type then the returned container will be empty.
        /// Otherwise the returned container will contain at least one element.
        /// @tparam CharType Type of character in each string, either narrow or wide.
        /// @param [in] stringToSplit Input string to be split.
        /// @param [in] delimiters Pointer to an array of delimiter character sequences each of which identifies a boundary between pieces of the input string.
        /// @param [in] numDelimiters Number of delimiters contained in the delimiter array.
        /// @return Container that holds views referring to pieces of the input string split using the specified delimiter.
        template <typename CharType> TemporaryVector<std::basic_string_view<CharType>> SplitString(std::basic_string_view<CharType> stringToSplit, const std::basic_string_view<CharType>* delimiters, unsigned int numDelimiters);

        /// Checks if one string is a prefix of another without regard for the case of each individual character.
        /// @tparam CharType Type of character in each string, either narrow or wide.
        /// @param [in] str String to be checked for a possible prefix.
        /// @param [in] maybePrefix Candidate prefix to compare with the beginning of the string.
        /// @return `true` if the candidate prefix is a prefix of the specified string, `false` otherwise.
        template <typename CharType> bool StartsWithCaseInsensitive(std::basic_string_view<CharType> str, std::basic_string_view<CharType> maybePrefix);

        /// Tokenizes a string, one token at a time, using the specified delimiter. Returns the next token found and updates the state variable for subsequent calls.
        /// Each invocation returns the next token found in the input string.
        /// @tparam CharType Type of character in each string, either narrow or wide.
        /// @param [in, out] tokenizeState Opaque state variable that tracks tokenizing progress. Must be 0 on first invocation and preserved between invocations on the same input string.
        /// @param [in] stringToTokenize String to be tokenized.
        /// @param [in] delimiter Delimiter, which can consist of multiple characters, that separates tokens in the input string. Can additionally vary between invocations.
        /// @return Next token found in the input string, if it exists.
        template <typename CharType> std::optional<std::basic_string_view<CharType>> TokenizeString(size_t& tokenizeState, std::basic_string_view<CharType> stringToTokenize, std::basic_string_view<CharType> delimiter);

        /// Tokenizes a string, one token at a time, using the specified delimiter strings. Returns the next token found and updates the state variable for subsequent calls.
        /// Each invocation returns the next token found in the input string.
        /// @tparam CharType Type of character in each string, either narrow or wide.
        /// @param [in, out] tokenizeState Opaque state variable that tracks tokenizing progress. Must be 0 on first invocation and preserved between invocations on the same input string.
        /// @param [in] stringToTokenize String to be tokenized.
        /// @param [in] delimiters Pointer to an array of delimiter character sequences each of which identifies a boundary between pieces of the input string.
        /// @param [in] numDelimiters Number of delimiters contained in the delimiter array.
        /// @return Next token found in the input string, if it exists.
        template <typename CharType> std::optional<std::basic_string_view<CharType>> TokenizeString(size_t& tokenizeState, std::basic_string_view<CharType> stringToTokenize, const std::basic_string_view<CharType>* delimiters, unsigned int numDelimiters);

        /// Generates a string representation of a system error code.
        /// @param [in] systemErrorCode System error code for which to generate a string.
        /// @return String representation of the system error code.
        TemporaryString SystemErrorCodeString(const unsigned long systemErrorCode);
    }
}
