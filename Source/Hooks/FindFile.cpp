/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FindFile.cpp
 *   Implementation of hook function for the FindFile family of Windows API
 *   functions, including FindFirstFile*, FindNextFile* and FindClose. These
 *   are used for enumerating files in directories.
 *****************************************************************************/

#include "ApiWindows.h"
#include "Hooks.h"


using namespace Pathwinder;


// -------- HOOK FUNCTIONS ------------------------------------------------- //
// See original function and Hookshot documentation for details.

HANDLE Pathwinder::Hooks::DynamicHook_FindFirstFileA::Hook(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
    Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Invoked with input \"%s\" but hook function not yet implemented.", GetFunctionName(), Strings::ConvertStringNarrowToWide(lpFileName).AsCString());
    return Original(lpFileName, lpFindFileData);
}

// --------

HANDLE Pathwinder::Hooks::DynamicHook_FindFirstFileW::Hook(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
    Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Invoked with input \"%s\" but hook function not yet implemented.", GetFunctionName(), lpFileName);
    return Original(lpFileName, lpFindFileData);
}

// --------

HANDLE Pathwinder::Hooks::DynamicHook_FindFirstFileExA::Hook(LPCSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPVOID lpFindFileData, FINDEX_SEARCH_OPS fSearchOp, LPVOID lpSearchFilter, DWORD dwAdditionalFlags)
{
    Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Invoked with input \"%s\" but hook function not yet implemented.", GetFunctionName(), Strings::ConvertStringNarrowToWide(lpFileName).AsCString());
    return Original(lpFileName, fInfoLevelId, lpFindFileData, fSearchOp, lpSearchFilter, dwAdditionalFlags);
}

// --------

HANDLE Pathwinder::Hooks::DynamicHook_FindFirstFileExW::Hook(LPCWSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPVOID lpFindFileData, FINDEX_SEARCH_OPS fSearchOp, LPVOID lpSearchFilter, DWORD dwAdditionalFlags)
{
    Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Invoked with input \"%s\" but hook function not yet implemented.", GetFunctionName(), lpFileName);
    return Original(lpFileName, fInfoLevelId, lpFindFileData, fSearchOp, lpSearchFilter, dwAdditionalFlags);
}

// --------

HANDLE Pathwinder::Hooks::DynamicHook_FindFirstFileTransactedA::Hook(LPCSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPVOID lpFindFileData, FINDEX_SEARCH_OPS fSearchOp, LPVOID lpSearchFilter, DWORD dwAdditionalFlags, HANDLE hTransaction)
{
    Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Invoked with input \"%s\" but hook function not yet implemented.", GetFunctionName(), Strings::ConvertStringNarrowToWide(lpFileName).AsCString());
    return Original(lpFileName, fInfoLevelId, lpFindFileData, fSearchOp, lpSearchFilter, dwAdditionalFlags, hTransaction);
}

// --------

HANDLE Pathwinder::Hooks::DynamicHook_FindFirstFileTransactedW::Hook(LPCWSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPVOID lpFindFileData, FINDEX_SEARCH_OPS fSearchOp, LPVOID lpSearchFilter, DWORD dwAdditionalFlags, HANDLE hTransaction)
{
    Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Invoked with input \"%s\" but hook function not yet implemented.", GetFunctionName(), lpFileName);
    return Original(lpFileName, fInfoLevelId, lpFindFileData, fSearchOp, lpSearchFilter, dwAdditionalFlags, hTransaction);
}

// --------

BOOL Pathwinder::Hooks::DynamicHook_FindNextFileA::Hook(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)
{
    Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Hook function not yet implemented.", GetFunctionName());
    return Original(hFindFile, lpFindFileData);
}

// --------

BOOL Pathwinder::Hooks::DynamicHook_FindNextFileW::Hook(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData)
{
    Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Hook function not yet implemented.", GetFunctionName());
    return Original(hFindFile, lpFindFileData);
}

// --------

BOOL Pathwinder::Hooks::DynamicHook_FindClose::Hook(HANDLE hFindFile)
{
    Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Hook function not yet implemented.", GetFunctionName());
    return Original(hFindFile);
}
