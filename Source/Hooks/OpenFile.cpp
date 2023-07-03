/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file OpenFile.cpp
 *   Implementation of hook function for the OpenFile family of Windows API
 *   functions.
 *****************************************************************************/

#include "ApiWindows.h"
#include "Hooks.h"


using namespace Pathwinder;


// -------- HOOK FUNCTIONS ------------------------------------------------- //
// See original function and Hookshot documentation for details.

HFILE Pathwinder::Hooks::DynamicHook_OpenFile::Hook(LPCSTR lpFileName, LPOFSTRUCT lpReOpenBuff, UINT uStyle)
{
    HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(lpFileName);
    return Original(lpFileName, lpReOpenBuff, uStyle);
}
