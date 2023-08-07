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

        NTSTATUS NtQueryInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
        {
            static decltype(&NtQueryInformationFile) functionPtr = reinterpret_cast<decltype(&NtQueryInformationFile)>(GetInternalWindowsApiFunctionAddress("NtQueryInformationFile"));
            DebugAssert(nullptr != functionPtr, "Failed to locate the address of the \"" __FUNCTIONW__ "\" function.");

            return functionPtr(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
        }

        // --------

        BOOLEAN RtlIsNameInExpression(PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PWCH UpcaseTable)
        {
            static decltype(&RtlIsNameInExpression) functionPtr = reinterpret_cast<decltype(&RtlIsNameInExpression)>(GetInternalWindowsApiFunctionAddress("RtlIsNameInExpression"));
            DebugAssert(nullptr != functionPtr, "Failed to locate the address of the \"" __FUNCTIONW__ "\" function.");

            return functionPtr(Expression, Name, IgnoreCase, UpcaseTable);
        }
    }
}
