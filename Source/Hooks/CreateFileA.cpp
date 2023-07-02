/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file CreateFileA.cpp
 *   Implementation of the hook function for CreateFileA.
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

HANDLE Pathwinder::Hooks::DynamicHook_CreateFileA::Hook(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    if (Message::WillOutputMessageOfSeverity(Message::ESeverity::SuperDebug))
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s: Invoked with path \"%s\".", GetFunctionName(), Strings::ConvertStringNarrowToWide(lpFileName).AsCString());

    auto redirectedFileName = Strings::ConvertStringWideToNarrow(FilesystemDirector::Singleton().RedirectSingleFile(Strings::ConvertStringNarrowToWide(lpFileName)).AsCString());

    if (false == Globals::GetConfigurationData().isDryRunMode)
        lpFileName = redirectedFileName.Data();

    return Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}
