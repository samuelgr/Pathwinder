/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file ApiWindows.cpp
 *   Implementation of supporting functions for the Windows API.
 *****************************************************************************/

#include "ApiWindows.h"


namespace Pathwinder
{
    // -------- FUNCTIONS -------------------------------------------------- //
    // See "ApiWindows.h" for documentation.

    void* GetWindowsApiFunctionAddress(const char* const funcName, void* const funcStaticAddress)
    {
        // List of low-level binary module handles, specified as the result of a call to LoadLibrary with the name of the binary.
        // Each is checked in sequence for the specified function, which is looked up by base name.
        static const HMODULE hmodLowLevelBinaries[] = {
            LoadLibrary(L"KernelBase.dll")
        };

        void* funcAddress = funcStaticAddress;

        for (int i = 0; (funcAddress == funcStaticAddress) && (i < _countof(hmodLowLevelBinaries)); ++i)
        {
            if (nullptr != hmodLowLevelBinaries[i])
            {
                void* const funcPossibleAddress = GetProcAddress(hmodLowLevelBinaries[i], funcName);

                if (nullptr != funcPossibleAddress)
                    funcAddress = funcPossibleAddress;
            }
        }

        return funcAddress;
    }
}