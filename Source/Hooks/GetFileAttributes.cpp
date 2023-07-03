/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file GetFileAttributes.cpp
 *   Implementation of hook function for the GetFileAttributes family of
 *   Windows API functions.
 *****************************************************************************/

#include "ApiWindows.h"
#include "Hooks.h"


using namespace Pathwinder;


// -------- HOOK FUNCTIONS ------------------------------------------------- //
// See original function and Hookshot documentation for details.

DWORD Pathwinder::Hooks::DynamicHook_GetFileAttributesA::Hook(LPCSTR lpFileName)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(lpFileName);
    return Original(lpFileName);
}

// --------

DWORD Pathwinder::Hooks::DynamicHook_GetFileAttributesW::Hook(LPCWSTR lpFileName)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_WIDE(lpFileName);
    return Original(lpFileName);
}

// --------

BOOL Pathwinder::Hooks::DynamicHook_GetFileAttributesExA::Hook(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(lpFileName);
    return Original(lpFileName, fInfoLevelId, lpFileInformation);
}

// --------

BOOL Pathwinder::Hooks::DynamicHook_GetFileAttributesExW::Hook(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_WIDE(lpFileName);
    return Original(lpFileName, fInfoLevelId, lpFileInformation);
}

// --------

BOOL Pathwinder::Hooks::DynamicHook_GetFileAttributesTransactedA::Hook(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation, HANDLE hTransaction)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(lpFileName);
    return Original(lpFileName, fInfoLevelId, lpFileInformation, hTransaction);
}

// --------

BOOL Pathwinder::Hooks::DynamicHook_GetFileAttributesTransactedW::Hook(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation, HANDLE hTransaction)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_WIDE(lpFileName);
    return Original(lpFileName, fInfoLevelId, lpFileInformation, hTransaction);
}
