/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file ApiWindowsShell.h
 *   Common header file for the correct version of the Windows API along with additional shell
 *   functionality.
 **************************************************************************************************/

#pragma once

// Windows header files are sensitive to include order. Compilation will fail if the order is
// incorrect.

// clang-format off

#include "ApiWindows.h"

#include <knownfolders.h>
#include <psapi.h>
#include <shlobj.h>

// clang-format on
