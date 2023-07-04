/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file Hooks.h
 *   Declarations for all Windows API hooks used to implement path redirection.
 *****************************************************************************/

#pragma once

#include "ApiWindows.h"

#include <Hookshot/DynamicHook.h>


namespace Pathwinder
{
    namespace Hooks
    {
        // -------- HOOKS -------------------------------------------------- //

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntclose for more information.
        HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtClose, NTSTATUS(__stdcall)(HANDLE));

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntcreatefile for more information.
        HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtCreateFile, NTSTATUS(__stdcall)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG));

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntopenfile for more information.
        HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtOpenFile, NTSTATUS(__stdcall)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG));

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile for more information.
        HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtQueryDirectoryFile, NTSTATUS(__stdcall)(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS, BOOLEAN, PUNICODE_STRING, BOOLEAN));

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfileex for more information.
        HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtQueryDirectoryFileEx, NTSTATUS(__stdcall)(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS, ULONG, PUNICODE_STRING));

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationbyname for more information.
        HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtQueryInformationByName, NTSTATUS(__stdcall)(POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS));
    }
}
