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
#include "DebugAssert.h"
#include "FilesystemDirector.h"
#include "Globals.h"
#include "Hooks.h"
#include "Message.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <Hookshot/DynamicHook.h>
#include <limits>


namespace Pathwinder
{
    // -------- INTERNAL TYPES --------------------------------------------- //

    /// Holds all of the information needed to represent a full set of object attributes, as needed by the Nt family of system calls.
    /// This structure places particular emphasis on owning the file name buffer so that it can easily be manipulated.
    struct SRedirectedNtObjectInfo
    {
        TemporaryString objectNameBuffer;                                   ///< Buffer for holding the object name.
        UNICODE_STRING objectName;                                          ///< View structure for representing a Unicode string in the Nt family of system calls.
        OBJECT_ATTRIBUTES objectAttributes;                                 ///< Top-level object attributes structure.

        /// Initialization constructor.
        /// Requires an existing object attributes structure. Copies and transforms the object name to lowercase.
        inline SRedirectedNtObjectInfo(const OBJECT_ATTRIBUTES& existingObjectAttributes) : objectNameBuffer(Strings::NtConvertUnicodeStringToStringView(*(existingObjectAttributes.ObjectName))), objectName(), objectAttributes(existingObjectAttributes)
        {
            objectNameBuffer.ToLowercase();
            objectAttributes.ObjectName = &objectName;
            objectName = objectName = Strings::NtConvertStringViewToUnicodeString(objectNameBuffer.AsStringView());
        }

        /// Initialization constructor.
        /// Requires an existing object attributes structure and a consumable object name, which must already be lowercase.
        inline SRedirectedNtObjectInfo(const OBJECT_ATTRIBUTES& existingObjectAttributes, TemporaryString&& replacementObjectNameLowercase) : objectNameBuffer(std::move(replacementObjectNameLowercase)), objectName(), objectAttributes(existingObjectAttributes)
        {
            objectAttributes.ObjectName = &objectName;
            objectName = Strings::NtConvertStringViewToUnicodeString(objectNameBuffer.AsStringView());
        }

        /// Move constructor.
        inline SRedirectedNtObjectInfo(SRedirectedNtObjectInfo&& other) : objectNameBuffer(std::move(other.objectNameBuffer)), objectName(), objectAttributes(other.objectAttributes)
        {
            objectAttributes.ObjectName = &objectName;
            objectName = Strings::NtConvertStringViewToUnicodeString(objectNameBuffer.AsStringView());
        }
    };


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Redirects an individual filename identified by an object attributes structure.
    /// Used as the main body of several of the hook functions that operate on individual files and directories.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] inputObjectAttributes Object attributes structure received from the application. Used as input for redirection.
    /// @return New object information structure containing everything required to replace the input object attributes structure when invoking the original system call.
    static SRedirectedNtObjectInfo RedirectSingleFileByObjectAttributes(const wchar_t* functionName, const OBJECT_ATTRIBUTES& inputObjectAttributes)
    {
        std::wstring_view inputFilename = Strings::NtConvertUnicodeStringToStringView(*(inputObjectAttributes.ObjectName));
        auto maybeRedirectedFilename = Pathwinder::FilesystemDirector::Singleton().RedirectSingleFile(inputFilename);

        if (true == maybeRedirectedFilename.has_value())
        {
            std::wstring_view redirectedFilename = maybeRedirectedFilename.value().AsStringView();
            Message::OutputFormatted(Message::ESeverity::Info, L"%s: Invoked with path \"%.*s\" which was redirected to \"%.*s\".", functionName, static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectedFilename.length()), redirectedFilename.data());

            if (false == Globals::GetConfigurationData().isDryRunMode)
                return SRedirectedNtObjectInfo(inputObjectAttributes, std::move(maybeRedirectedFilename.value()));
        }
        else
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s: Invoked with path \"%.*s\" which was not redirected.", functionName, static_cast<int>(inputFilename.length()), inputFilename.data());
        }

        return SRedirectedNtObjectInfo(inputObjectAttributes);
    }
}


// -------- PROTECTED HOOK FUNCTIONS --------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtClose::Hook(HANDLE Handle)
{
    using namespace Pathwinder;


    Pathwinder::Message::OutputFormatted(Pathwinder::Message::ESeverity::SuperDebug, L"%s: Invoked.", GetFunctionName());
    return Original(Handle);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtCreateFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    using namespace Pathwinder;


    SRedirectedNtObjectInfo objectInfo = RedirectSingleFileByObjectAttributes(GetFunctionName(), *ObjectAttributes);

    std::wstring_view pathForSystemCall = Strings::NtConvertUnicodeStringToStringView(objectInfo.objectName);
    if (FilesystemDirector::Singleton().IsPrefixForAnyRule(pathForSystemCall))
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Path \"%.*s\" is interesting and would have its handle cached.", GetFunctionName(), static_cast<int>(pathForSystemCall.length()), pathForSystemCall.data());

    return Original(FileHandle, DesiredAccess, &objectInfo.objectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtOpenFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    using namespace Pathwinder;


    SRedirectedNtObjectInfo objectInfo = RedirectSingleFileByObjectAttributes(GetFunctionName(), *ObjectAttributes);

    std::wstring_view pathForSystemCall = Strings::NtConvertUnicodeStringToStringView(objectInfo.objectName);
    if (FilesystemDirector::Singleton().IsPrefixForAnyRule(pathForSystemCall))
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Path \"%.*s\" is interesting and would have its handle cached.", GetFunctionName(), static_cast<int>(pathForSystemCall.length()), pathForSystemCall.data());

    return Original(FileHandle, DesiredAccess, &objectInfo.objectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFile::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName, BOOLEAN RestartScan)
{
    using namespace Pathwinder;


    Pathwinder::Message::OutputFormatted(Pathwinder::Message::ESeverity::Debug, L"%s: Invoked.", GetFunctionName());
    return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, ReturnSingleEntry, FileName, RestartScan);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFileEx::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, ULONG QueryFlags, PUNICODE_STRING FileName)
{
    using namespace Pathwinder;


    Pathwinder::Message::OutputFormatted(Pathwinder::Message::ESeverity::Debug, L"%s: Invoked.", GetFunctionName());
    return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, QueryFlags, FileName);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationByName::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    using namespace Pathwinder;


    SRedirectedNtObjectInfo objectInfo = RedirectSingleFileByObjectAttributes(GetFunctionName(), *ObjectAttributes);
    return Original(&objectInfo.objectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
}


// -------- UNPROTECTED HOOK FUNCTIONS ------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryAttributesFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFO FileInformation)
{
    using namespace Pathwinder;


    SRedirectedNtObjectInfo objectInfo = RedirectSingleFileByObjectAttributes(GetFunctionName(), *ObjectAttributes);
    return Original(&objectInfo.objectAttributes, FileInformation);
}
