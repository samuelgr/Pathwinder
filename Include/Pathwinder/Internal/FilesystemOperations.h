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


namespace Pathwinder
{
    namespace FilesystemOperations
    {
        // -------- FUNCTIONS ---------------------------------------------- //

        /// Checks if the specified filesystem entity (file, directory, or otherwise) exists.
        /// @param path [in] Path of the entity to check. Must be null-terminated.
        /// @return `true` if the entity exists, `false` otherwise.
        bool Exists(const wchar_t* path);

        /// Checks if the specified path exists in the filesystem as a directory.
        /// @param path [in] Path to check. Must be null-terminated.
        /// @return `true` if the path exists as a directory, `false` otherwise.
        bool IsDirectory(const wchar_t* path);

        /// Checks if the specified path exists in the filesystem as a file.
        /// @param path [in] Path to check. Must be null-terminated.
        /// @return `true` if the path exists as a directory, `false` otherwise.
        bool IsFile(const wchar_t* path);
    }
}
