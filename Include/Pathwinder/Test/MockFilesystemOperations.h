/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file MockFilesystemOperations.h
 *   Declaration of controlled fake filesystem operations that can be used
 *   for testing.
 *****************************************************************************/

#pragma once

#include "FilesystemOperations.h"
#include "MockFreeFunctionContext.h"

#include <map>
#include <string>
#include <string_view>


namespace PathwinderTest
{
    /// Context controlling object that implements mock filesystem operations.
    /// Each object supports creation of a fake filesystem, which is then supplied to test cases via the internal filesystem operations API.
    MOCK_FREE_FUNCTION_CONTEXT_CLASS(MockFilesystemOperations)
    {
    private:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Enumerates different kinds of filesystem entities that can be part of the mock filesystem.
        enum class EFilesystemEntityType
        {
            File,
            Directory
        };

        /// Contains the information needed to represent a filesystem entity.
        /// This forms the "value" part of a key-value store representing a filesystem, so the name is not necessary here. Rather, it is the "key" part.
        struct SFilesystemEntity
        {
            EFilesystemEntityType type;
            unsigned int sizeInBytes;
        };


        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Contents of the mock filesystem.
        /// Top-level map key is an absolute directory name and value is a set of directory contents.
        std::map<std::wstring, std::map<std::wstring, SFilesystemEntity, std::less<>>, std::less<>> filesystemContents;


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Inserts a filesystem entity and all of its parent directories into the fake filesystem.
        /// For internal use only.
        /// @param [in] absolutePath Absolute path of the directory to insert. Paths are case-insensitive.
        /// @param [in] type Type of filesystem entity to be inserted.
        /// @param [in] sizeInBytes Size, in bytes, of the new filesystem entity.
        void AddFilesystemEntityInternal(std::wstring_view absolutePath, EFilesystemEntityType type, unsigned int sizeInBytes);

    public:
        /// Inserts a directory and all its parents into the fake filesystem.
        /// @param [in] absolutePath Absolute path of the directory to insert. Paths are case-insensitive.
        inline void AddDirectory(std::wstring_view absolutePath)
        {
            AddFilesystemEntityInternal(absolutePath, EFilesystemEntityType::Directory, 0);
        }

        /// Inserts a file and all its parent directories into the fake filesystem.
        /// @param [in] absolutePath Absolute path of the file to insert. Paths are case-insensitive.
        /// @param [in] sizeInBytes Size, in bytes, of the file being added. Defaults to 0.
        inline void AddFile(std::wstring_view absolutePath, unsigned int fileSizeInBytes = 0)
        {
            AddFilesystemEntityInternal(absolutePath, EFilesystemEntityType::File, fileSizeInBytes);
        }


        // -------- MOCK INSTANCE METHODS ---------------------------------- //
        // See "FilesystemOperations.h" for documentation.

        bool Exists(std::wstring_view absolutePath);
        bool IsDirectory(std::wstring_view absolutePath);
    };
}
