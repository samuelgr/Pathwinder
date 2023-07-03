/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file CreateFile.cpp
 *   Implementation of hook function for the CreateFile family of Windows API
 *   functions.
 *****************************************************************************/

#include "ApiWindows.h"
#include "Hooks.h"


using namespace Pathwinder;


// -------- HOOK FUNCTIONS ------------------------------------------------- //
// See original function and Hookshot documentation for details.

HANDLE Pathwinder::Hooks::DynamicHook_CreateFile2::Hook(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwCreationDisposition, LPCREATEFILE2_EXTENDED_PARAMETERS pCreateExParams)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_WIDE(lpFileName);
    return Original(lpFileName, dwDesiredAccess, dwShareMode, dwCreationDisposition, pCreateExParams);
}

// --------

HANDLE Pathwinder::Hooks::DynamicHook_CreateFileA::Hook(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(lpFileName);
    return Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

// --------

HANDLE Pathwinder::Hooks::DynamicHook_CreateFileW::Hook(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_WIDE(lpFileName);
    return Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

// --------

HANDLE Pathwinder::Hooks::DynamicHook_CreateFileTransactedA::Hook(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile, HANDLE hTransaction, PUSHORT pusMiniVersion, PVOID lpExtendedParameter)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(lpFileName);
    return Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile, hTransaction, pusMiniVersion, lpExtendedParameter);
}

// --------

HANDLE Pathwinder::Hooks::DynamicHook_CreateFileTransactedW::Hook(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile, HANDLE hTransaction, PUSHORT pusMiniVersion, PVOID lpExtendedParameter)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_WIDE(lpFileName);
    return Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile, hTransaction, pusMiniVersion, lpExtendedParameter);
}
