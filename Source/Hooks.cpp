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
    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Converts a Windows internal Unicode string view to a standard string view.
    /// @param [in] unicodeStr Unicode string view to convert.
    /// @return Resulting standard string view.
    static std::wstring_view UnicodeStringToStringView(const UNICODE_STRING& unicodeStr)
    {
        return std::wstring_view(unicodeStr.Buffer, (unicodeStr.Length / sizeof(wchar_t)));
    }

    /// Converts a standard string view to a Windows internal Unicode string view.
    /// @param [in] strView Standard string view to convert.
    /// @return Resulting Windows internal Unicode string view.
    static UNICODE_STRING StringViewToUnicodeString(std::wstring_view strView)
    {
        DebugAssert((strView.length() * sizeof(wchar_t)) <= static_cast<size_t>(std::numeric_limits<decltype(UNICODE_STRING::Length)>::max()), "Attempting to make an unrepresentable UNICODE_STRING due to the length exceeding representable range for Length.");
        DebugAssert((strView.length() * sizeof(wchar_t)) <= static_cast<size_t>(std::numeric_limits<decltype(UNICODE_STRING::MaximumLength)>::max()), "Attempting to make an unrepresentable UNICODE_STRING due to the length exceeding representable range for MaximumLength.");

        return {
            .Length = static_cast<decltype(UNICODE_STRING::Length)>(strView.length() * sizeof(wchar_t)),
            .MaximumLength = static_cast<decltype(UNICODE_STRING::MaximumLength)>(strView.length() * sizeof(wchar_t)),
            .Buffer = const_cast<decltype(UNICODE_STRING::Buffer)>(strView.data())
        };
    }

    static OBJECT_ATTRIBUTES RedirectObjectAttributesForHookFunction(const wchar_t* functionName, const OBJECT_ATTRIBUTES& inputObjectAttributes, UNICODE_STRING& redirectedFilenameUnicodeStr)
    {
        std::wstring_view inputFilename = UnicodeStringToStringView(*(inputObjectAttributes.ObjectName));
        std::wstring_view redirectedFilename = inputFilename;

        OBJECT_ATTRIBUTES redirectedObjectAttributes = inputObjectAttributes;
        redirectedObjectAttributes.ObjectName = &redirectedFilenameUnicodeStr;
        redirectedFilenameUnicodeStr = *(inputObjectAttributes.ObjectName);

        auto maybeRedirectedFilename = Pathwinder::FilesystemDirector::Singleton().RedirectSingleFile(inputFilename);

        if (true == maybeRedirectedFilename.has_value())
        {
            redirectedFilename = maybeRedirectedFilename.value().AsStringView();

            if (false == Globals::GetConfigurationData().isDryRunMode)
                redirectedFilenameUnicodeStr = StringViewToUnicodeString(redirectedFilename);

            Message::OutputFormatted(Message::ESeverity::Info, L"%s: Invoked with path \"%.*s\" which was redirected to \"%.*s\".", functionName, static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectedFilename.length()), redirectedFilename.data());
        }
        else
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s: Invoked with path \"%.*s\" which was not redirected.", functionName, static_cast<int>(inputFilename.length()), inputFilename.data());
        }

        return redirectedObjectAttributes;
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


    UNICODE_STRING redirectedPath{};
    OBJECT_ATTRIBUTES redirectedObjectAttributes = RedirectObjectAttributesForHookFunction(GetFunctionName(), *ObjectAttributes, redirectedPath);

    return Original(FileHandle, DesiredAccess, &redirectedObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtOpenFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    using namespace Pathwinder;


    UNICODE_STRING redirectedPath{};
    OBJECT_ATTRIBUTES redirectedObjectAttributes = RedirectObjectAttributesForHookFunction(GetFunctionName(), *ObjectAttributes, redirectedPath);

    return Original(FileHandle, DesiredAccess, &redirectedObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
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


    UNICODE_STRING redirectedPath{};
    OBJECT_ATTRIBUTES redirectedObjectAttributes = RedirectObjectAttributesForHookFunction(GetFunctionName(), *ObjectAttributes, redirectedPath);

    return Original(&redirectedObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
}


// -------- UNPROTECTED HOOK FUNCTIONS ------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryAttributesFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFO FileInformation)
{
    using namespace Pathwinder;


    UNICODE_STRING redirectedPath{};
    OBJECT_ATTRIBUTES redirectedObjectAttributes = RedirectObjectAttributesForHookFunction(GetFunctionName(), *ObjectAttributes, redirectedPath);

    return Original(&redirectedObjectAttributes, FileInformation);
}
