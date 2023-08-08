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
        inline constexpr NTSTATUS kSuccess = 0x00000000;                    ///< `STATUS_SUCCESS`: The operation completed successfully.
        inline constexpr NTSTATUS kPending = 0x00000103;                    ///< `STATUS_PENDING`: The operation that was requested is pending completion.
        inline constexpr NTSTATUS kObjectNameExists = 0x40000000;           ///< `STATUS_OBJECT_NAME_EXISTS`: An attempt was made to create an object but the object name already exists.
        inline constexpr NTSTATUS kInvalidParameter = 0xC000000D;           ///< `STATUS_INVALID_PARAMETER`: An invalid parameter was passed to a service or function.
        inline constexpr NTSTATUS kObjectNameInvalid = 0xC0000033;          ///< `STATUS_OBJECT_NAME_INVALID`: The object name is invalid.
        inline constexpr NTSTATUS kObjectNameNotFound = 0xC0000034;         ///< `STATUS_OBJECT_NAME_NOT_FOUND`: The object name is not found.
        inline constexpr NTSTATUS kObjectNameCollision = 0xC0000035;        ///< `STATUS_OBJECT_NAME_COLLISION`: The object name already exists.
        inline constexpr NTSTATUS kObjectPathInvalid = 0xC0000039;          ///< `STATUS_OBJECT_PATH_INVALID`: The object path component was not a directory object.
        inline constexpr NTSTATUS kObjectPathNotFound = 0xC000003A;         ///< `STATUS_OBJECT_PATH_NOT_FOUND`: The object path does not exist.
        inline constexpr NTSTATUS kObjectPathSyntaxBad = 0xC000003B;        ///< `STATUS_OBJECT_PATH_SYNTAX_BAD`: The object path component was not a directory object.
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

    /// Contains information about a file. Same layout as `FILE_BASIC_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_file_basic_information for more information.
    struct SFileBasicInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(4);

        LARGE_INTEGER creationTime;
        LARGE_INTEGER lastAccessTime;
        LARGE_INTEGER lastWriteTime;
        LARGE_INTEGER changeTime;
        ULONG fileAttributes;
    };

    /// Contains information about a file. Same layout as `FILE_STANDARD_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_file_standard_information for more information.
    struct SFileStandardInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(5);

        LARGE_INTEGER allocationSize;
        LARGE_INTEGER endOfFile;
        ULONG numberOfLinks;
        BOOLEAN deletePending;
        BOOLEAN directory;
    };

    /// Contains information about a file. Same layout as `FILE_ACCESS_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_access_information for more information.
    struct SFileAccessInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(8);

        ACCESS_MASK accessFlags;
    };

    /// Contains information about a file. Same layout as `FILE_NAME_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_name_information for more information.
    struct SFileNameInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(9);

        ULONG fileNameLength;
        WCHAR fileName[1];
    };

    /// Contains information on how to rename a file. Same layout as `FILE_RENAME_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information for more information.
    struct SFileRenameInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(10);

        union
        {
            BOOLEAN replaceIfExists;
            ULONG flags;
        };
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

    /// Contains information about a file. Same layout as `FILE_MODE_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_mode_information for more information.
    struct SFileModeInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(16);

        ULONG mode;
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

    /// Retrieves the stored filename from within one of the many structures that uses a dangling filename field.
    /// @tparam FileInformationStructType Windows internal structure type that uses a wide-character dangling filename field.
    /// @param [in] fileInformationStruct Read-only reference to a structure with a wide-character dangling filename field.
    /// @return String view representation of the wide-character dangling filename field.
    template <typename FileInformationStructType, typename = decltype(FileInformationStructType::fileNameLength), typename = decltype(FileInformationStructType::fileName[0])> constexpr inline std::wstring_view GetFileInformationStructFilename(const FileInformationStructType& fileInformationStruct)
    {
        return std::wstring_view(fileInformationStruct.fileName, (fileInformationStruct.fileNameLength / sizeof(wchar_t)));
    }

    /// Returns a pointer to the next file information struct in a buffer containing multiple possibly variably-sized file information structures.
    /// @tparam FileInformationStructType Windows internal structure type that is intended to be part of a buffer of contiguous structures of the same type.
    /// @param [in] fileInformationStruct Read-only reference to a structure that is part of a buffer of contiguous structures of the same type.
    /// @return Pointer to the next structure in the buffer, or `nullptr` if no more structures exist.
    template <typename FileInformationStructType, typename = decltype(FileInformationStructType::nextEntryOffset)> inline FileInformationStructType* NextFileInformationStruct(const FileInformationStructType& fileInformationStruct)
    {
        if (0 == fileInformationStruct.nextEntryOffset)
            return nullptr;

        return reinterpret_cast<FileInformationStructType*>(reinterpret_cast<size_t>(&fileInformationStruct) + static_cast<size_t>(fileInformationStruct.nextEntryOffset));
    }

    namespace WindowsInternal
    {
        /// Wrapper around the internal `RtlIsNameInExpression` function, which is in the Windows driver kit.
        /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationfile for more information.
        NTSTATUS NtQueryInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass);

        /// Wrapper around the internal `RtlIsNameInExpression` function, which has no associated header file and requires dynamically linking.
        /// See https://learn.microsoft.com/en-us/windows/win32/devnotes/rtlisnameinexpression for more information.
        BOOLEAN RtlIsNameInExpression(PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PWCH UpcaseTable);
    }
}
