/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file CreateDirectory.cpp
 *   Implementation of hook function for the CreateDirectory family of Windows
 *   API functions.
 *****************************************************************************/

#include "ApiWindows.h"
#include "Hooks.h"


using namespace Pathwinder;


// -------- HOOK FUNCTIONS ------------------------------------------------- //
// See original function and Hookshot documentation for details.

BOOL Pathwinder::Hooks::DynamicHook_CreateDirectoryA::Hook(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(lpPathName);
    return Original(lpPathName, lpSecurityAttributes);
}

// --------

BOOL Pathwinder::Hooks::DynamicHook_CreateDirectoryW::Hook(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_WIDE(lpPathName);
    return Original(lpPathName, lpSecurityAttributes);
}
