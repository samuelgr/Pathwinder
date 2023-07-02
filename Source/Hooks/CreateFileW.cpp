/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file CreateFileW.cpp
 *   Implementation of the hook function for CreateFileW.
 *****************************************************************************/

#include "ApiWindows.h"
#include "FilesystemDirector.h"
#include "Globals.h"
#include "Message.h"
#include "Hooks.h"
#include "TemporaryBuffer.h"

#include <Hookshot/DynamicHook.h>


using namespace Pathwinder;


// -------- HOOK FUNCTION -------------------------------------------------- //
// See original function and Hookshot documentation for details.

HANDLE Pathwinder::Hooks::DynamicHook_CreateFileW::Hook(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s: Invoked with path \"%s\".", GetFunctionName(), lpFileName);

    TemporaryString redirectedFileName = FilesystemDirector::Singleton().RedirectSingleFile(lpFileName);

    if (false == Globals::GetConfigurationData().isDryRunMode)
        lpFileName = redirectedFileName.AsCString();

    return Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}
