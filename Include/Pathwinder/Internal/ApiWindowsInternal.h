/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file ApiWindowsInternal.h
 *   Common header file for accessing Windows internals, including things that
 *   are normally only available via the Windows driver kit.
 *****************************************************************************/

#pragma once


#include "ApiWindows.h"

#include <string_view>
#include <winternl.h>


namespace Pathwinder
{
    // -------- CONSTANTS ----------------------------------------------- //

    // NTSTATUS values. Many are not defined in header files outside of the Windows driver kit.
    // See https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55 for more information.
    namespace NtStatus
    {
        inline constexpr NTSTATUS kSuccess = 0x00000000;
        inline constexpr NTSTATUS kObjectPathInvalid = 0xC0000039;
    }


    // -------- TYPE DEFINITIONS --------------------------------------- //

    /// Contains information on how to rename a file. Same layout as `FILE_RENAME_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information for more information.
    struct SFileRenameInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(10);

#if (_WIN32_WINNT >= _WIN32_WINNT_WIN10_RS1)
        union {
            BOOLEAN replaceIfExists;
            ULONG flags;
        };
#else
        BOOLEAN replaceIfExists;
#endif
        HANDLE rootDirectory;
        ULONG fileNameLength;
        WCHAR fileName[1];

        /// Convenience method for quickly accessing the stored filename.
        constexpr inline std::wstring_view GetFilename(void)
        {
            return std::wstring_view(fileName, fileNameLength / sizeof(wchar_t));
        }
    };

    /// Corresponds to `FILE_STAT_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_stat_information for more information.
    struct SFileStatInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(68);

        LARGE_INTEGER fileId;
        LARGE_INTEGER creationTime;
        LARGE_INTEGER lastAccessTime;
        LARGE_INTEGER lastWriteTime;
        LARGE_INTEGER changeTime;
        LARGE_INTEGER allocationSize;
        LARGE_INTEGER endOfFile;
        ULONG fileAttributes;
        ULONG reparseTag;
        ULONG numberOfLinks;
        ACCESS_MASK effectiveAccess;
    };
}
