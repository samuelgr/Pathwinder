/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file Strings.h
 *   Declaration of common strings and functions to manipulate them.
 *****************************************************************************/

#pragma once

#include "TemporaryBuffer.h"

#include <cstddef>
#include <cstdint>
#include <sal.h>
#include <string_view>


namespace Pathwinder
{
    namespace Strings
    {
        // -------- COMPILE-TIME CONSTANTS --------------------------------- //
        // Can safely be used at any time, including to perform static initialization.
        // Views are guaranteed to be null-terminated.

        /// Configuration file section for defining variables.
        inline constexpr std::wstring_view kStrConfigurationSectionVariables = L"Variables";

        /// Configuration file setting for enabling and specifying the verbosity of output to the log file.
        inline constexpr std::wstring_view kStrConfigurationSettingLogLevel = L"LogLevel";

        /// Delimiter used to separate portions of a string that are to be taken as literals versus to be taken as named references.
        inline constexpr std::wstring_view kStrDelimiterReferenceVsLiteral = L"%";

        /// Delimiter used to separate a named reference into a domain part and a name part.
        inline constexpr std::wstring_view kStrDelimterReferenceDomainVsName = L"::";

        /// Domain part of a named reference that identifies the domain as being an environment variable.
        inline constexpr std::wstring_view kStrReferenceDomainEnvironmentVariable = L"ENV";

        /// Domain part of a named reference that identifies the domain as being a shell "known folder" identifier.
        inline constexpr std::wstring_view kStrReferenceDomainKnownFolderIdentifier = L"FOLDERID";

        /// Domain part of a named reference that identifies the domain as being a variable defined in the configuration file.
        inline constexpr std::wstring_view kStrReferenceDomainVariable = L"VAR";


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

        /// Directory name of the currently-running executable, including trailing backslash if available.
        extern const std::wstring_view kStrExecutableDirectoryName;

        /// Complete path and filename of the currently-running form of Pathwinder.
        extern const std::wstring_view kStrPathwinderCompleteFilename;

        /// Base name of the currently-running form of Pathwinder.
        extern const std::wstring_view kStrPathwinderBaseName;

        /// Directory name of the currently-running form of Pathwinder, including trailing backslash if available.
        extern const std::wstring_view kStrPathwinderDirectoryName;

        /// Expected filename of a configuration file.
        /// Pathwinder configuration filename = (Pathwinder directory)\Pathwinder.ini
        extern const std::wstring_view kStrConfigurationFilename;

        /// Expected filename for the log file.
        /// Pathwinder log filename = (current user's desktop)\Pathwinder_(base name of the running executable)_(process ID).log
        extern const std::wstring_view kStrLogFilename;


        // -------- FUNCTIONS ---------------------------------------------- //

        /// Formats a string and returns the result in a newly-allocated null-terminated temporary buffer.
        /// @param [in] format Format string, possibly with format specifiers which must be matched with the arguments that follow.
        /// @return Resulting string after all formatting is applied.
        TemporaryBuffer<wchar_t> FormatString(_Printf_format_string_ const wchar_t* format, ...);

        /// Splits a string using the specified delimiter character and returns a list of views each corresponding to a part of the input string.
        /// If there are too many delimiters present such that not all of the pieces can fit into the returned container type then the returned container will be empty.
        /// Otherwise the returned container will contain at least one element.
        /// @param [in] stringToSplit Input string to be split.
        /// @param [in] delimiter Delimiter character sequence that identifies boundaries between pieces of the input string.
        /// @return Container that holds views referring to pieces of the input string split using the specified delimiter.
        TemporaryVector<std::wstring_view> SplitString(std::wstring_view stringToSplit, std::wstring_view delimiter);

        /// Generates a string representation of a system error code.
        /// @param [in] systemErrorCode System error code for which to generate a string.
        /// @return String representation of the system error code.
        TemporaryBuffer<wchar_t> SystemErrorCodeString(const unsigned long systemErrorCode);
    }
}
