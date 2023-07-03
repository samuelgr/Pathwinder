/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file GetFileAttributesA.cpp
 *   Implementation of the hook function for GetFileAttributesA.
 *****************************************************************************/

#include "Hooks.h"


using namespace Pathwinder;


// -------- HOOK FUNCTION -------------------------------------------------- //
// See original function and Hookshot documentation for details.

DWORD Pathwinder::Hooks::DynamicHook_GetFileAttributesA::Hook(LPCSTR lpFileName)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(lpFileName);
    return Original(lpFileName);
}
