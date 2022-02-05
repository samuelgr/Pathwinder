/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file Resolver.h
 *   Interface declaration for resolving references identified by name.
 *****************************************************************************/

#pragma once

#include "TemporaryBuffer.h"
#include "ValueOrError.h"

#include <map>
#include <string>
#include <string_view>


namespace Pathwinder
{
    namespace Resolver
    {
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Type alias for representing either the result of resolving references or an error message.
        /// This version fully contains and owns the resulting string.
        typedef ValueOrError<std::wstring, std::wstring> ResolvedStringOrError;

        /// Type alias for representing either the result of resolving references or an error message.
        /// This version provides the resulting string as a read-only view.
        typedef ValueOrError<std::wstring_view, std::wstring> ResolvedStringViewOrError;


        // -------- FUNCTIONS ---------------------------------------------- //

        /// Resolves a single reference represented by the input string.
        /// Input string is expected to be of the form [DOMAIN]::[REFERENCE_NAME].
        /// Single reference resolution results are cached internally, so the result is a view into the internal cache data structure.
        /// @param [in] str Input string representing a single reference.
        /// @return Resolved reference or an error message if the resolution failed.
        ResolvedStringViewOrError ResolveSingleReference(std::wstring_view str);

        /// Resolves all references contained in the input string and optionally escapes special characters if they appear within the results of any references that are resolved.
        /// For example, if variable X is defined as "ABC!DEF" and this function is asked to escape characters including '!' then the result of "%X%" is "ABC\!DEF" with a backslash escape in the proper location.
        /// Each reference is expected to be of the form %[DOMAIN]::[REFERENCE_NAME]% with %% used to indicate a literal '%' sign.
        /// Full string reference resolution results are not cached and involve a fair bit of dynamic memory manipulation, so the result fully contains and owns its string.
        /// @param [in] str Input string for which references should be resolved.
        /// @param [in] escapeCharacters Optional string containing characters to escape if they appear within the results of any references that are resolved, defaults to none.
        /// @param [in] escapeSequenceStart Optional string specifying what character sequence to use to begin an escape sequence, defaults to a single backslash.
        /// @param [in] escapeSequenceEnd Optional string specifying what character sequence to use to end an escape sequence, defaults to an empty string.
        /// @return Input string with all references resolved or an error message if the resolution failed.
        ResolvedStringOrError ResolveAllReferences(std::wstring_view str, std::wstring_view escapeCharacters = L"", std::wstring_view escapeSequenceStart = L"\\", std::wstring_view escapeSequenceEnd = L"");

#ifdef PATHWINDER_SKIP_CONFIG
        /// Sets the configuration file definitions map contents.
        /// Intended primarily for testing.
        /// @param [in] newConfigurationFileDefinitions Map containing new contents.
        void SetConfigurationFileDefinitions(std::map<std::wstring_view, std::wstring_view>&& newConfigurationFileDefinitions);
#endif
    }
}
