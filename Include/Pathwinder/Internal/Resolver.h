/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file Resolver.h
 *   Interface declaration for resolving references identified by name and relative path
 *   components.
 **************************************************************************************************/

#pragma once

#include <map>
#include <string>
#include <string_view>

#include <Infra/Core/Configuration.h>
#include <Infra/Core/TemporaryBuffer.h>
#include <Infra/Core/ValueOrError.h>

namespace Pathwinder
{
  namespace Resolver
  {
    /// Type alias for representing either the result of resolving references or an error
    /// message. This version fully contains and owns the resulting string.
    using ResolvedStringOrError = Infra::ValueOrError<std::wstring, Infra::TemporaryString>;

    /// Type alias for representing either the result of resolving references or an error
    /// message. This version provides the resulting string as a read-only view.
    using ResolvedStringViewOrError =
        Infra::ValueOrError<std::wstring_view, Infra::TemporaryString>;

    /// Type alias for representing all the definitions of values that correspond to the CONF
    /// domain. Typically these would be located in a configuration file.
    using TConfiguredDefinitions = std::map<std::wstring, std::wstring, std::less<>>;

    /// Resolves a single reference represented by the input string. Input string is expected to
    /// be of the form [DOMAIN]::[REFERENCE_NAME]. Single reference resolution results are
    /// cached internally, so the result is a view into the internal cache data structure.
    /// @param [in] str Input string representing a single reference.
    /// @return Resolved reference or an error message if the resolution failed.
    ResolvedStringViewOrError ResolveSingleReference(std::wstring_view str);

    /// Resolves all references contained in the input string and optionally escapes special
    /// characters if they appear within the results of any references that are resolved. For
    /// example, if variable X is defined as "ABC!DEF" and this function is asked to escape
    /// characters including '!' then the result of "%X%" is "ABC\!DEF" with a backslash escape
    /// in the proper location. Each reference is expected to be of the form
    /// %[DOMAIN]::[REFERENCE_NAME]% with %% used to indicate a literal '%' sign. Full string
    /// reference resolution results are not cached and involve a fair bit of dynamic memory
    /// manipulation, so the result fully contains and owns its string.
    /// @param [in] str Input string for which references should be resolved.
    /// @param [in] escapeCharacters Optional string containing characters to escape if they
    /// appear within the results of any references that are resolved, defaults to none.
    /// @param [in] escapeSequenceStart Optional string specifying what character sequence to
    /// use to begin an escape sequence, defaults to a single backslash.
    /// @param [in] escapeSequenceEnd Optional string specifying what character sequence to use
    /// to end an escape sequence, defaults to an empty string.
    /// @return Input string with all references resolved or an error message if the resolution
    /// failed.
    ResolvedStringOrError ResolveAllReferences(
        std::wstring_view str,
        std::wstring_view escapeCharacters = std::wstring_view(),
        std::wstring_view escapeSequenceStart = L"\\",
        std::wstring_view escapeSequenceEnd = std::wstring_view());

    /// Resolves a filesystem path that potentially has relative path components ('.' and '..') by
    /// turning it into an absolute path.
    /// @param [in] potentiallyRelativePath Path to be resolved from potentially relative to
    /// absolute.
    /// @param [in] pathDelimiter Delimiter to use when separating components of the path. Defaults
    /// to the Windows standard delimiter of a single backslash.
    /// @return Input path turned into an absolute path or an error message if the process failed.
    ResolvedStringOrError ResolveRelativePathComponents(
        std::wstring_view potentiallyRelativePath, std::wstring_view pathDelimiter = L"\\");

    /// Clears the configured definitions. This operation is primarily intended for tests.
    /// Invoking this function also clears the internal reference resolution cache.
    void ClearConfiguredDefinitions(void);

    /// Sets the configured definitions, which correspond to the CONF domain for reference
    /// resolution. Typically these would be supplied in a configuration file but may be
    /// overridden for testing. Invoking this function also clears the internal reference
    /// resolution cache.
    /// @param [in] newConfiguredDefinitions Map containing new contents.
    void SetConfiguredDefinitions(TConfiguredDefinitions&& newConfiguredDefinitions);

    /// Examines the supplied configuration section object and uses it to build a map of
    /// configured definitions, which are then set by invoking #SetConfiguredDefinitions.
    /// Section data supplied this way is expected to contain string values. This is the
    /// expected entry point for using a configuration file to set configured definitions.
    /// @param [in] configuredDefinitionsSection Configuration data section containing
    /// definitions.
    void SetConfiguredDefinitionsFromSection(
        Infra::Configuration::Section&& configuredDefinitionsSection);
  } // namespace Resolver
} // namespace Pathwinder
