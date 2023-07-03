/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file GetFileAttributesExA.cpp
 *   Implementation of the hook function for GetFileAttributesExA.
 *****************************************************************************/

#include "Hooks.h"


using namespace Pathwinder;


// -------- HOOK FUNCTION -------------------------------------------------- //
// See original function and Hookshot documentation for details.

BOOL Pathwinder::Hooks::DynamicHook_GetFileAttributesExA::Hook(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(lpFileName);
    return Original(lpFileName, fInfoLevelId, lpFileInformation);
}
