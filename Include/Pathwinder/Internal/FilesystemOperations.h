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

#include <cstdint>
#include <string_view>


namespace Pathwinder
{
    namespace FilesystemOperations
    {
        // -------- FUNCTIONS ---------------------------------------------- //

        /// Attempts to create the specified directory if it does not already exist.
        /// If needed, also attempts to create all directories that are ancestors of the specified directory.
        /// @param [in] absoluteDirectoryPath Absolute path of the directory to be created along with its hierarchy of ancestors.
        /// @return System call return code for the last system call that completed successfully, safely cast from `NTSTATUS` to a standard integer type.
        intptr_t CreateDirectoryHierarchy(std::wstring_view absoluteDirectoryPath);

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
