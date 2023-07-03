/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file SetFileAttributes.cpp
 *   Implementation of hook function for the SetFileAttributes family of
 *   Windows API functions.
 *****************************************************************************/

#include "ApiWindows.h"
#include "Hooks.h"


using namespace Pathwinder;


// -------- HOOK FUNCTIONS ------------------------------------------------- //
// See original function and Hookshot documentation for details.

BOOL Pathwinder::Hooks::DynamicHook_SetFileAttributesA::Hook(LPCSTR lpFileName, DWORD dwFileAttributes)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(lpFileName);
    return Original(lpFileName, dwFileAttributes);
}

// --------

BOOL Pathwinder::Hooks::DynamicHook_SetFileAttributesW::Hook(LPCWSTR lpFileName, DWORD dwFileAttributes)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_WIDE(lpFileName);
    return Original(lpFileName, dwFileAttributes);
}

// --------

BOOL Pathwinder::Hooks::DynamicHook_SetFileAttributesTransactedA::Hook(LPCSTR lpFileName, DWORD dwFileAttributes, HANDLE hTransaction)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(lpFileName);
    return Original(lpFileName, dwFileAttributes, hTransaction);
}

// --------

BOOL Pathwinder::Hooks::DynamicHook_SetFileAttributesTransactedW::Hook(LPCWSTR lpFileName, DWORD dwFileAttributes, HANDLE hTransaction)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_WIDE(lpFileName);
    return Original(lpFileName, dwFileAttributes, hTransaction);
}
