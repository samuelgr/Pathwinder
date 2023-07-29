/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file ApiWindowsInternal.cpp
 *   Implementation of wrappers for accessing functions without header files
 *   and generally only available via the Windows driver kit.
 *****************************************************************************/

#include "ApiWindowsInternal.h"
#include "DebugAssert.h"


namespace Pathwinder
{
    namespace WindowsInternal
    {
        // -------- FUNCTIONS ---------------------------------------------- //
        // See "ApiWindowsInternal.h" for documentation.

        BOOLEAN RtlIsNameInExpression(PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PWCH UpcaseTable)
        {
            static BOOLEAN(__stdcall * functionPtr)(PUNICODE_STRING, PUNICODE_STRING, BOOLEAN, PWCH) = reinterpret_cast<BOOLEAN(__stdcall *)(PUNICODE_STRING, PUNICODE_STRING, BOOLEAN, PWCH)>(GetInternalWindowsApiFunctionAddress("RtlIsNameInExpression"));

            DebugAssert(nullptr != functionPtr, "Failed to locate the address of the \"" __FUNCTIONW__ "\" function.");
            return functionPtr(Expression, Name, IgnoreCase, UpcaseTable);
        }
    }
}
