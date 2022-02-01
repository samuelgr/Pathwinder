/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry keys.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file HookModuleMain.cpp
 *   Entry point when injecting Pathwinder as a hook module.
 *****************************************************************************/

#include "Globals.h"

#include <Hookshot/Hookshot.h>


// -------- ENTRY POINT ---------------------------------------------------- //

/// Hook module entry point. 
HOOKSHOT_HOOK_MODULE_ENTRY(hookshot)
{
    Pathwinder::Globals::Initialize();
}
