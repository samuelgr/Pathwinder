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

#include <string>
#include <string_view>


namespace Pathwinder
{
    namespace Resolver
    {
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Type alias for representing either the result of resolving one or more references or an error message.
        typedef ValueOrError<std::wstring_view, std::wstring> ResolvedStringOrError;


        // -------- FUNCTIONS ---------------------------------------------- //

        /// Resolves a single reference represented by the input string.
        /// Input string is expected to be of the form [DOMAIN]::[REFERENCE_NAME].
        /// @param [in] Input string representing a single reference.
        /// @return Resolved reference or an error message if the resolution failed.
        ResolvedStringOrError ResolveSingleReference(std::wstring_view str);

        /// Resolves all references contained in the input string.
        /// Each reference is expected to be of the form %[DOMAIN]::[REFERENCE_NAME]% with %% used to indicate a literal '%' sign.
        /// @param [in] Input string for which references should be resolved.
        /// @return Input string with all references resolved or an error message if the resolution failed.
        ResolvedStringOrError ResolveAllReferences(std::wstring_view str);
    }
}
