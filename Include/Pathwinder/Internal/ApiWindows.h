/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file ApiWindows.h
 *   Common header file for the correct version of the Windows API.
 **************************************************************************************************/

#pragma once

// Windows header files are sensitive to include order. Compilation will fail if the order is
// incorrect. Top-level macros and headers must come first, followed by headers for other parts
// of system functionality.

// clang-format off

#define NOMINMAX
#include <sdkddkver.h>
#include <windows.h>

// clang-format on

#include <knownfolders.h>
#include <psapi.h>
#include <shlobj.h>

namespace Pathwinder
{
  /// Retrieves the proper internal address of a Windows API function.
  /// Many Windows API functions have been moved to lower-level binaries, and some functions are
  /// intended for drivers rather than applications.
  /// https://docs.microsoft.com/en-us/windows/win32/win7appqual/new-low-level-binaries
  /// @param [in] funcName API function name.
  /// @return Address to use for the Windows API function, or `nullptr` if the API function could
  /// not be located.
  void* GetInternalWindowsApiFunctionAddress(const char* const funcName);

} // namespace Pathwinder
