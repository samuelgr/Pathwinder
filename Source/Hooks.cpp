/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file Hooks.cpp
 *   Implementation of all Windows API hook functions used to implement path
 *   redirection.
 *****************************************************************************/

#pragma once

#include "ApiWindows.h"
#include "Hooks.h"
#include "Message.h"

#include <Hookshot/DynamicHook.h>


// -------- PROTECTED HOOK FUNCTIONS --------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtClose::Hook(HANDLE Handle)
{
    Pathwinder::Message::OutputFormatted(Pathwinder::Message::ESeverity::Debug, L"%s: Invoked.", GetFunctionName());
    return Original(Handle);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtCreateFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    Pathwinder::Message::OutputFormatted(Pathwinder::Message::ESeverity::Debug, L"%s: Invoked with root directory = 0x%016llx and object name = \"%.*s\"", GetFunctionName(), (long long)ObjectAttributes->RootDirectory, (int)ObjectAttributes->ObjectName->Length, ObjectAttributes->ObjectName->Buffer);
    return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtOpenFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    Pathwinder::Message::OutputFormatted(Pathwinder::Message::ESeverity::Debug, L"%s: Invoked with root directory = 0x%016llx and object name = \"%.*s\"", GetFunctionName(), (long long)ObjectAttributes->RootDirectory, (int)ObjectAttributes->ObjectName->Length, ObjectAttributes->ObjectName->Buffer);
    return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFile::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName, BOOLEAN RestartScan)
{
    Pathwinder::Message::OutputFormatted(Pathwinder::Message::ESeverity::Debug, L"%s: Invoked.", GetFunctionName());
    return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, ReturnSingleEntry, FileName, RestartScan);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFileEx::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, ULONG QueryFlags, PUNICODE_STRING FileName)
{
    Pathwinder::Message::OutputFormatted(Pathwinder::Message::ESeverity::Debug, L"%s: Invoked.", GetFunctionName());
    return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, QueryFlags, FileName);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationByName::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    Pathwinder::Message::OutputFormatted(Pathwinder::Message::ESeverity::Debug, L"%s: Invoked with root directory = 0x%016llx and object name = \"%.*s\"", GetFunctionName(), (long long)ObjectAttributes->RootDirectory, (int)ObjectAttributes->ObjectName->Length, ObjectAttributes->ObjectName->Buffer);
    return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
}


// -------- UNPROTECTED HOOK FUNCTIONS ------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryAttributesFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFO FileInformation)
{
    Pathwinder::Message::OutputFormatted(Pathwinder::Message::ESeverity::Debug, L"%s: Invoked with root directory = 0x%016llx and object name = \"%.*s\"", GetFunctionName(), (long long)ObjectAttributes->RootDirectory, (int)ObjectAttributes->ObjectName->Length, ObjectAttributes->ObjectName->Buffer);
    return Original(ObjectAttributes, FileInformation);
}
