/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file CreateFile2.cpp
 *   Implementation of the hook function for CreateFile2.
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

HANDLE Pathwinder::Hooks::DynamicHook_CreateFile2::Hook(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwCreationDisposition, LPCREATEFILE2_EXTENDED_PARAMETERS pCreateExParams)
{
    Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s: Invoked with path \"%s\".", GetFunctionName(), lpFileName);

    TemporaryString redirectedFileName = FilesystemDirector::Singleton().RedirectSingleFile(lpFileName);

    if (false == Globals::GetConfigurationData().isDryRunMode)
        lpFileName = redirectedFileName.AsCString();

    return Original(lpFileName, dwDesiredAccess, dwShareMode, dwCreationDisposition, pCreateExParams);
}
