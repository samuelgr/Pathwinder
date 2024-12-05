/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file ApiWindows.cpp
 *   Implementation of supporting functions for the Windows API.
 **************************************************************************************************/

#include "ApiWindows.h"

#include <Infra/DebugAssert.h>

namespace Pathwinder
{
  void* GetInternalWindowsApiFunctionAddress(const char* const funcName)
  {
    // List of low-level binary module handles, specified as the result of a call to LoadLibrary
    // with the name of the binary. Each is checked in sequence for the specified function,
    // which is looked up by base name. Order matters, with lowest-level binaries specified
    // first.
    static const HMODULE hmodLowLevelBinaries[] = {
        LoadLibrary(L"ntdll.dll"), LoadLibrary(L"kernelbase.dll"), LoadLibrary(L"kernel32.dll")};

    for (int i = 0; i < _countof(hmodLowLevelBinaries); ++i)
    {
      if (nullptr != hmodLowLevelBinaries[i])
      {
        void* const funcPossibleAddress = GetProcAddress(hmodLowLevelBinaries[i], funcName);

        if (nullptr != funcPossibleAddress) return funcPossibleAddress;
      }
    }

    return nullptr;
  }

  namespace WindowsInternal
  {
    BOOLEAN RtlIsNameInExpression(
        PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PWCH UpcaseTable)
    {
      static BOOLEAN(__stdcall * functionPtr)(PUNICODE_STRING, PUNICODE_STRING, BOOLEAN, PWCH) =
          reinterpret_cast<decltype(functionPtr)>(
              GetInternalWindowsApiFunctionAddress("RtlIsNameInExpression"));
      DebugAssert(
          nullptr != functionPtr,
          "Failed to locate the address of the \"" __FUNCTION__ "\" function.");

      return functionPtr(Expression, Name, IgnoreCase, UpcaseTable);
    }
  } // namespace WindowsInternal
} // namespace Pathwinder
