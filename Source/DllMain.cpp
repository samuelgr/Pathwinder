/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file DllMain.cpp
 *   Entry point when loading or unloading this dynamic library.
 **************************************************************************************************/

#include "ApiWindows.h"
#include "FilesystemOperations.h"
#include "Globals.h"

/// Performs library initialization and teardown functions.
/// Invoked automatically by the operating system.
/// Refer to Windows documentation for more information.
/// @param [in] hModule Instance handle for this library.
/// @param [in] ulReasonForCall Specifies the event that caused this function to be invoked.
/// @param [in] lpReserved Reserved.
/// @return `TRUE` if this function successfully initialized or uninitialized this library, `FALSE`
/// otherwise.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReasonForCall, LPVOID lpReserved)
{
  switch (ulReasonForCall)
  {
    case DLL_PROCESS_ATTACH:
      break;

    case DLL_THREAD_ATTACH:
      break;

    case DLL_THREAD_DETACH:
      break;

    case DLL_PROCESS_DETACH:
      if (nullptr != lpReserved)
      {
        for (const auto& tempPathToClean : Pathwinder::Globals::TemporaryPathsToClean())
          Pathwinder::FilesystemOperations::Delete(tempPathToClean);
      }
      break;
  }

  return TRUE;
}
