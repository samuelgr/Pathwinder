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

    /// Contains information about a file in a directory. Same layout as `FILE_DIRECTORY_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_directory_information for more information.
    struct SFileDirectoryInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(1);

        ULONG nextEntryOffset;
        ULONG fileIndex;
        LARGE_INTEGER creationTime;
        LARGE_INTEGER lastAccessTime;
        LARGE_INTEGER lastWriteTime;
        LARGE_INTEGER changeTime;
        LARGE_INTEGER endOfFile;
        LARGE_INTEGER allocationSize;
        ULONG fileAttributes;
        ULONG fileNameLength;
        WCHAR fileName[1];
    };

    /// Contains information about a file in a directory. Same layout as `FILE_FULL_DIR_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_full_dir_information for more information.
    struct SFileFullDirectoryInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(2);

        ULONG nextEntryOffset;
        ULONG fileIndex;
        LARGE_INTEGER creationTime;
        LARGE_INTEGER lastAccessTime;
        LARGE_INTEGER lastWriteTime;
        LARGE_INTEGER changeTime;
        LARGE_INTEGER endOfFile;
        LARGE_INTEGER allocationSize;
        ULONG fileAttributes;
        ULONG fileNameLength;
        ULONG eaSize;
        WCHAR fileName[1];
    };

    /// Contains information about a file in a directory. Same layout as `FILE_BOTH_DIR_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_both_dir_information for more information.
    struct SFileBothDirectoryInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(3);

        ULONG nextEntryOffset;
        ULONG fileIndex;
        LARGE_INTEGER creationTime;
        LARGE_INTEGER lastAccessTime;
        LARGE_INTEGER lastWriteTime;
        LARGE_INTEGER changeTime;
        LARGE_INTEGER endOfFile;
        LARGE_INTEGER allocationSize;
        ULONG fileAttributes;
        ULONG fileNameLength;
        ULONG eaSize;
        CCHAR shortNameLength;
        WCHAR shortName[12];
        WCHAR fileName[1];
    };

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
    };

    /// Contains information about a file in a directory. Same layout as `FILE_NAMES_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_names_information for more information.
    struct SFileNamesInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(12);

        ULONG nextEntryOffset;
        ULONG fileIndex;
        ULONG fileNameLength;
        WCHAR fileName[1];
    };

    /// Contains information about a file in a directory. Same layout as `FILE_ID_BOTH_DIR_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_both_dir_information for more information.
    struct SFileIdBothDirInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(37);

        ULONG nextEntryOffset;
        ULONG fileIndex;
        LARGE_INTEGER creationTime;
        LARGE_INTEGER lastAccessTime;
        LARGE_INTEGER lastWriteTime;
        LARGE_INTEGER changeTime;
        LARGE_INTEGER endOfFile;
        LARGE_INTEGER allocationSize;
        ULONG fileAttributes;
        ULONG fileNameLength;
        ULONG eaSize;
        CCHAR shortNameLength;
        WCHAR shortName[12];
        LARGE_INTEGER fileId;
        WCHAR fileName[1];
    };

    /// Contains information about a file in a directory. Same layout as `FILE_ID_FULL_DIR_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_full_dir_information for more information.
    struct SFileIdFullDirInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(38);

        ULONG nextEntryOffset;
        ULONG fileIndex;
        LARGE_INTEGER creationTime;
        LARGE_INTEGER lastAccessTime;
        LARGE_INTEGER lastWriteTime;
        LARGE_INTEGER changeTime;
        LARGE_INTEGER endOfFile;
        LARGE_INTEGER allocationSize;
        ULONG fileAttributes;
        ULONG fileNameLength;
        ULONG eaSize;
        LARGE_INTEGER fileId;
        WCHAR fileName[1];
    };

    /// Contains information about a file in a directory. Same layout as `FILE_ID_GLOBAL_TX_DIR_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_global_tx_dir_information for more information.
    struct SFileIdGlobalTxDirInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(50);

        ULONG nextEntryOffset;
        ULONG fileIndex;
        LARGE_INTEGER creationTime;
        LARGE_INTEGER lastAccessTime;
        LARGE_INTEGER lastWriteTime;
        LARGE_INTEGER changeTime;
        LARGE_INTEGER endOfFile;
        LARGE_INTEGER allocationSize;
        ULONG fileAttributes;
        ULONG fileNameLength;
        LARGE_INTEGER fileId;
        GUID lockingTransactionId;
        ULONG txInfoFlags;
        WCHAR fileName[1];
    };

    /// Contains information about a file in a directory. Same layout as `FILE_ID_EXTD_DIR_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-file_id_extd_dir_information for more information.
    struct SFileIdExtdDirInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(60);

        ULONG nextEntryOffset;
        ULONG fileIndex;
        LARGE_INTEGER creationTime;
        LARGE_INTEGER lastAccessTime;
        LARGE_INTEGER lastWriteTime;
        LARGE_INTEGER changeTime;
        LARGE_INTEGER endOfFile;
        LARGE_INTEGER allocationSize;
        ULONG fileAttributes;
        ULONG fileNameLength;
        ULONG eaSize;
        ULONG reparsePointTag;
        FILE_ID_128 fileId;
        WCHAR fileName[1];
    };

    /// Contains information about a file in a directory. Same layout as `FILE_ID_EXTD_BOTH_DIR_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_extd_both_dir_information for more information.
    struct SFileIdExtdBothDirInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(63);

        ULONG nextEntryOffset;
        ULONG fileIndex;
        LARGE_INTEGER creationTime;
        LARGE_INTEGER lastAccessTime;
        LARGE_INTEGER lastWriteTime;
        LARGE_INTEGER changeTime;
        LARGE_INTEGER endOfFile;
        LARGE_INTEGER allocationSize;
        ULONG fileAttributes;
        ULONG fileNameLength;
        ULONG eaSize;
        ULONG reparsePointTag;
        FILE_ID_128 fileId;
        CCHAR shortNameLength;
        WCHAR shortName[12];
        WCHAR fileName[1];
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


    // -------- FUNCTIONS ---------------------------------------------- //

    /// Convenience function for quickly accessing the stored filename in one of the many structures that uses a dangling filename field.
    /// @tparam FileInformationStructType Windows internal structure type that uses a wide-character dangling filename field.
    /// @param [in] fileInformationStruct Read-only reference to a structure with a wide-character dangling filename field.
    /// @return String view representation of the wide-character dangling filename field.
    template <typename FileInformationStructType> constexpr inline std::wstring_view GetFileInformationStructFilename(const FileInformationStructType& fileInformationStruct)
    {
        return std::wstring_view(fileInformationStruct.fileName, (fileInformationStruct.fileNameLength / sizeof(wchar_t)));
    }

    namespace WindowsInternal
    {
        /// Wrapper around the internal `RtlIsNameInExpression` function, which has no associated header file and requires dynamically linking.
        /// See https://learn.microsoft.com/en-us/windows/win32/devnotes/rtlisnameinexpression for more information.
        BOOLEAN RtlIsNameInExpression(PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PWCH UpcaseTable);
    }
}
