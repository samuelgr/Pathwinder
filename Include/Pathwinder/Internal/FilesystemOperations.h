/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemOperations.h
 *   Declaration of functions that provide an abstraction for filesystem
 *   operations executed internally.
 *****************************************************************************/

#pragma once

#include <string_view>


namespace Pathwinder
{
    namespace FilesystemOperations
    {
        // -------- FUNCTIONS ---------------------------------------------- //

        /// Checks if the specified filesystem entity (file, directory, or otherwise) exists.
        /// @param path [in] Absolute path of the entity to check.
        /// @return `true` if the entity exists, `false` otherwise.
        bool Exists(std::wstring_view absolutePath);

        /// Checks if the specified path exists in the filesystem as a directory.
        /// @param path [in] Absolute path to check.
        /// @return `true` if the path exists as a directory, `false` otherwise.
        bool IsDirectory(std::wstring_view absolutePath);
    }
}
