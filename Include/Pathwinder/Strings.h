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
#include <deque>
#include <sal.h>
#include <string>
#include <string_view>


namespace Pathwinder
{
    namespace Strings
    {
        // -------- COMPILE-TIME CONSTANTS --------------------------------- //
        // Can safely be used at any time, including to perform static initialization.
        // Views are guaranteed to be null-terminated.

        // Placeholder


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

        /// Configuration file setting name for enabling and specifying the verbosity of output to the log file.
        inline constexpr std::wstring_view kStrConfigurationSettingNameLogLevel = L"LogLevel";


        // -------- FUNCTIONS ---------------------------------------------- //

        /// Formats a string and returns the result in a newly-allocated null-terminated temporary buffer.
        /// @param [in] format Format string, possibly with format specifiers which must be matched with the arguments that follow.
        /// @return Resulting string after all formatting is applied.
        TemporaryBuffer<wchar_t> FormatString(_Printf_format_string_ const wchar_t* format, ...);

        /// Splits a string using the specified delimiter character and returns a list of views each corresponding to a part of the input string.
        /// @param [in] stringToSplit Input string to be split.
        /// @param [in] delimiter Delimiter character sequence that identifies boundaries between pieces of the input string.
        /// @return Container that holds views referring to pieces of the input string split using the specified delimiter.
        std::deque<std::wstring_view> SplitString(std::wstring_view stringToSplit, std::wstring_view delimiter);

        /// Generates a string representation of a system error code.
        /// @param [in] systemErrorCode System error code for which to generate a string.
        /// @return String representation of the system error code.
        std::wstring SystemErrorCodeString(const unsigned long systemErrorCode);
    }
}
