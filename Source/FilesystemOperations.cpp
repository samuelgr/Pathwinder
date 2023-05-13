/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemOperations.cpp
 *   Declaration of functions that provide an abstraction for filesystem
 *   operations executed internally.
 *****************************************************************************/

#pragma once

#include "ApiWindows.h"
#include "FilesystemOperations.h"


namespace Pathwinder
{
    namespace FilesystemOperations
    {
        // -------- FUNCTIONS ---------------------------------------------- //
        // See "FilesystemOperations.h" for documentation.

        bool Exists(const wchar_t* path)
        {
            const DWORD pathAttributes = GetFileAttributes(path);
            return (INVALID_FILE_ATTRIBUTES != pathAttributes);
        }

        // --------

        bool IsDirectory(const wchar_t* path)
        {
            constexpr DWORD kDirectoryAttributeMask = FILE_ATTRIBUTE_DIRECTORY;
            const DWORD pathAttributes = GetFileAttributes(path);
            return ((INVALID_FILE_ATTRIBUTES != pathAttributes) && (0 != (kDirectoryAttributeMask & pathAttributes)));
        }

        // --------

        bool IsFile(const wchar_t* path)
        {
            constexpr DWORD kNotFileAttributeMask = (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT);
            const DWORD pathAttributes = GetFileAttributes(path);
            return ((INVALID_FILE_ATTRIBUTES != pathAttributes) && (0 == (kNotFileAttributeMask & pathAttributes)));
        }
    }
}
