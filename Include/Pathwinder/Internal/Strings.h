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

#include <Infra/Core/TemporaryBuffer.h>

#include "ApiWindows.h"

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

    /// NetBIOS hostname of the machine on which the currently-running executable is running.
    extern const std::wstring_view kStrNetBiosHostname;

    /// DNS hostname of the machine on which the currently-running executable is running. Does not
    /// include the domain name, just the hostname.
    extern const std::wstring_view kStrDnsHostname;

    /// DNS domain of the machine on which the currently-running executable is running. Does not
    /// include the hostname, just the domain.
    extern const std::wstring_view kStrDnsDomain;

    /// Fully-qualified DNS name of the machine on which the currently-running executable is
    /// running. Includes both hostname and domain.
    extern const std::wstring_view kStrDnsFullyQualified;

    /// Expected base filename of a configuration file, without the directory part.
    /// Pathwinder configuration filename = Pathwinder.ini
    std::wstring_view GetConfigurationFilename(void);

    /// Expected filename for the log file.
    /// Pathwinder log filename = (current user's desktop)\Pathwinder_(base name of the running
    /// executable)_(process ID).log
    std::wstring_view GetLogFilename(void);

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

    /// Copies the specified absolute path and prepends it with an appropriate Windows namespace
    /// prefix for identifying file paths to Windows system calls. Invoke this function with an
    /// empty string as the input parameter to return a new string object filled with just the
    /// prefix.
    /// @param [in] absolutePath Absolute path to be prepended with a prefix.
    /// @return Absolute path with a prefix inserted in front of it.
    Infra::TemporaryString PathAddWindowsNamespacePrefix(std::wstring_view absolutePath);

    /// Generates a string representation of the specified access mask.
    /// @param [in] accessMask Access mask, typically received from an application when creating or
    /// opening a file.
    /// @return String representation of the access mask.
    Infra::TemporaryString NtAccessMaskToString(ACCESS_MASK accessMask);

    /// Generates a string representation of the specified create disposition value.
    /// @param [in] createDisposition Creation disposition options, typically received from an
    /// application when creating or opening a file.
    /// @return String representation of the create disposition.
    Infra::TemporaryString NtCreateDispositionToString(ULONG createDisposition);

    /// Generates a string representation of the specified create/open options flags.
    /// @param [in] createOrOpenOptions Create or open options flags.
    /// @return String representation of the create or open options flags.
    Infra::TemporaryString NtCreateOrOpenOptionsToString(ULONG createOrOpenOptions);

    /// Generates a string representation of the specified share access flags.
    /// @param [in] shareAccess Share access flags, typically received from an application when
    /// creating or opening a file.
    /// @return String representation of the share access flags.
    Infra::TemporaryString NtShareAccessToString(ULONG shareAccess);

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
    /// @param [in] absolutePath Absolute path to check.
    /// @return Whether or not the supplied absolute path is a volume root path.
    bool PathIsVolumeRoot(std::wstring_view absolutePath);
  } // namespace Strings
} // namespace Pathwinder
