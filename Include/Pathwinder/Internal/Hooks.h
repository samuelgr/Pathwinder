/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file Hooks.h
 *   Declarations for all Windows API hooks used to implement path redirection.
 *****************************************************************************/

#pragma once

#include "ApiWindows.h"

#include <Hookshot/DynamicHook.h>


namespace Pathwinder
{
    namespace Hooks
    {
        // -------- HOOKS -------------------------------------------------- //
        // See original Windows API functions and Hookshot documentation for details.
        // Each hook function is implemented in its own source file.

        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(CreateFile2);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(CreateFileA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(CreateFileW);
    }
}
