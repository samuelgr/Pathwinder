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
#include "Strings.h"

#include <string>
#include <string_view>
#include <unordered_map>


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

        /// Type alias for the contents of an individual directory.
        /// Key is a filename and value is the file's metadata.
        typedef std::unordered_map <std::wstring, SFilesystemEntity, Pathwinder::Strings::CaseInsensitiveHasher<wchar_t>, Pathwinder::Strings::CaseInsensitiveEqualityComparator<wchar_t>> TDirectoryContents;

        /// Type alias for the contents of an entire mock filesystem.
        /// Key is a directory name and value is the directory's contents.
        /// This is a single-level data structure whereby all directories of arbitrary depth in the hierarchy are represented by name in this data structure.
        typedef std::unordered_map<std::wstring, TDirectoryContents, Pathwinder::Strings::CaseInsensitiveHasher<wchar_t>, Pathwinder::Strings::CaseInsensitiveEqualityComparator<wchar_t>> TFilesystemContents;


        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Contents of the mock filesystem.
        /// Top-level map key is an absolute directory name and value is a set of directory contents.
        TFilesystemContents filesystemContents;


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

        intptr_t CreateDirectoryHierarchy(std::wstring_view absoluteDirectoryPath);
        bool Exists(std::wstring_view absolutePath);
        bool IsDirectory(std::wstring_view absolutePath);
    };
}
