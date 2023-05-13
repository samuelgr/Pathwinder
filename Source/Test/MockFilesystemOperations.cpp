/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file MockFilesystemOperations.cpp
 *   Implementation of controlled fake filesystem operations that can be used
 *   for testing.
 *****************************************************************************/

#pragma once

#include "FilesystemOperations.h"


namespace PathwinderTest
{

}


namespace Pathwinder
{
    namespace FilesystemOperations
    {
        using namespace ::PathwinderTest;


        // -------- FUNCTIONS ---------------------------------------------- //
        // See "FilesystemOperations.h" for documentation.

        bool Exists(const wchar_t* path)
        {
            return false;
        }

        // --------

        bool IsDirectory(const wchar_t* path)
        {
            return false;
        }

        // --------

        bool IsFile(const wchar_t* path)
        {
            return false;
        }
    }
}
