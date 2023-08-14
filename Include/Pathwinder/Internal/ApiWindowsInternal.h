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

#include <algorithm>
#include <cwchar>
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
        inline constexpr NTSTATUS kMoreEntries = 0x00000105;                ///< `STATUS_MORE_ENTRIES`: Returned by enumeration APIs to indicate more information is available to successive calls.
        inline constexpr NTSTATUS kObjectNameExists = 0x40000000;           ///< `STATUS_OBJECT_NAME_EXISTS`: An attempt was made to create an object but the object name already exists.
        inline constexpr NTSTATUS kBufferOverflow = 0x80000005;             ///< `STATUS_BUFFER_OVERFLOW`: The data was too large to fit into the specified buffer.
        inline constexpr NTSTATUS kNoMoreFiles = 0x80000006;                ///< `STATUS_NO_MORE_FILES`: No more files were found which match the file specification.
        inline constexpr NTSTATUS kInvalidInfoClass = 0xC0000003;           ///< `STATUS_INVALID_INFO_CLASS`: The specified information class is not a valid information class for the specified object.
        inline constexpr NTSTATUS kInvalidParameter = 0xC000000D;           ///< `STATUS_INVALID_PARAMETER`: An invalid parameter was passed to a service or function.
        inline constexpr NTSTATUS kNoSuchFile = 0xC000000F;                 ///< `STATUS_NO_SUCH_FILE`: The file does not exist.
        inline constexpr NTSTATUS kBufferTooSmall = 0xC0000023;             ///< `STATUS_BUFFER_TOO_SMALL`: The buffer is too small to contain the entry. No information has been written to the buffer.
        inline constexpr NTSTATUS kObjectNameInvalid = 0xC0000033;          ///< `STATUS_OBJECT_NAME_INVALID`: The object name is invalid.
        inline constexpr NTSTATUS kObjectNameNotFound = 0xC0000034;         ///< `STATUS_OBJECT_NAME_NOT_FOUND`: The object name is not found.
        inline constexpr NTSTATUS kObjectNameCollision = 0xC0000035;        ///< `STATUS_OBJECT_NAME_COLLISION`: The object name already exists.
        inline constexpr NTSTATUS kObjectPathInvalid = 0xC0000039;          ///< `STATUS_OBJECT_PATH_INVALID`: The object path component was not a directory object.
        inline constexpr NTSTATUS kObjectPathNotFound = 0xC000003A;         ///< `STATUS_OBJECT_PATH_NOT_FOUND`: The object path does not exist.
        inline constexpr NTSTATUS kObjectPathSyntaxBad = 0xC000003B;        ///< `STATUS_OBJECT_PATH_SYNTAX_BAD`: The object path component was not a directory object.
        inline constexpr NTSTATUS kInternalError = 0xC00000E5;              ///< `STATUS_INTERNAL_ERROR`: An internal error occurred.
    }

    // Query flags for use with the `NtQueryDirectoryFileEx` function.
    // These constants not defined in header files outside of the Windows driver kit.
    // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfileex for more information.
    namespace QueryFlag
    {
        inline constexpr ULONG kRestartScan = 0x00000001;                   ///< `SL_RESTART_SCAN`: The scan will start at the first entry in the directory. If this flag is not set, the scan will resume from where the last query ended.
        inline constexpr ULONG kReturnSingleEntry = 0x00000002;             ///< `SL_RETURN_SINGLE_ENTRY`: Normally the return buffer is packed with as many matching directory entries that fit. If this flag is set, the file system will return only one directory entry at a time. This does make the operation less efficient.
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

    /// Contains information about a file. Same layout as `FILE_INTERNAL_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_internal_information for more information.
    struct SFileInternalInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(6);

        LARGE_INTEGER indexNumber;
    };

    /// Contains information about a file. Same layout as `FILE_EA_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_ea_information for more information.
    struct SFileExtendedAttributeInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(7);

        ULONG eaSize;
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

    /// Contains information about a file. Same layout as `FILE_POSITION_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_file_position_information for more information.
    struct SFilePositionInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(14);

        LARGE_INTEGER currentByteOffset;
    };

    /// Contains information about a file. Same layout as `FILE_MODE_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_mode_information for more information.
    struct SFileModeInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(16);

        ULONG mode;
    };

    /// Contains information about a file. Same layout as `FILE_ALIGNMENT_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_alignment_information for more information.
    struct SFileAlignmentInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(17);

        ULONG alignmentRequirement;
    };

    /// Contains information about a file. Same layout as `FILE_ALL_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_alignment_information for more information.
    struct SFileAllInformation
    {
        static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(18);

        SFileBasicInformation basicInformation;
        SFileStandardInformation standardInformation;
        SFileInternalInformation internalInformation;
        SFileExtendedAttributeInformation eaInformation;
        SFileAccessInformation accessInformation;
        SFilePositionInformation positionInformation;
        SFileModeInformation modeInformation;
        SFileAlignmentInformation alignmentInformation;
        SFileNameInformation nameInformation;
    };

    /// Contains information about a file in a directory. Same layout as `FILE_ID_BOTH_DIR_INFORMATION` from the Windows driver kit.
    /// See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_both_dir_information for more information.
    struct SFileIdBothDirectoryInformation
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
    struct SFileIdFullDirectoryInformation
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
    struct SFileIdGlobalTxDirectoryInformation
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
    struct SFileIdExtdDirectoryInformation
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
    struct SFileIdExtdBothDirectoryInformation
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

    /// Changes the stored filename within one of the many structures that uses a dangling filename field.
    /// On output, the filename member is updated to represent the specified filename string, but only up to whatever number of characters fit in the buffer containing the structure.
    /// Regardless, the length field is updated to represent the number of characters needed to represent the entire string.
    /// @tparam FileInformationStructType Windows internal structure type that uses a wide-character dangling filename field.
    /// @param [in, out] fileInformationStruct Mutable reference to a structure with a wide-character dangling filename field.
    /// @param [in] bufferSizeBytes Total size of the buffer containing the file information structure, in bytes.
    /// @param [in] filename Filename to be set in the file information structure.
    /// @return Number of characters written. If this is less than the number of characters in the input filename string then the buffer was too small to hold the entire filename.
    template <typename FileInformationStructType, typename = decltype(FileInformationStructType::fileNameLength), typename = decltype(FileInformationStructType::fileName[0])> inline size_t SetFileInformationStructFilename(FileInformationStructType& fileInformationStruct, size_t bufferSizeBytes, std::wstring_view filename)
    {
        wchar_t* const filenameBuffer = fileInformationStruct.fileName;
        const size_t filenameBufferCapacityChars = (bufferSizeBytes - offsetof(FileInformationStructType, fileName)) / sizeof(wchar_t);

        const size_t filenameNumberOfBytesNeeded = (filename.length() * sizeof(wchar_t));
        const size_t filenameNumberOfCharsToWrite = std::min(filenameBufferCapacityChars, filename.length());

        std::wmemcpy(filenameBuffer, filename.data(), filenameNumberOfCharsToWrite);
        fileInformationStruct.fileNameLength = static_cast<decltype(fileInformationStruct.fileNameLength)>(filenameNumberOfBytesNeeded);

        return filenameNumberOfCharsToWrite;
    }

    /// Computes the size, in bytes, of the specified filename information structure of a type which uses a dangling filename field.
    /// @tparam FileInformationStructType Windows internal structure type that uses a wide-character dangling filename field.
    /// @param [in] fileInformationStruct Read-only reference to a structure that uses a wide-character dangling filename field.
    /// @return Size of the specified file information structure, in bytes.
    template <typename FileInformationStructType, typename = decltype(FileInformationStructType::fileNameLength), typename = decltype(FileInformationStructType::fileName[0])> constexpr inline size_t SizeOfFileInformationStructWithFilename(const FileInformationStructType& fileInformationStruct)
    {
        const size_t structureBaseSize = sizeof(FileInformationStructType);
        const size_t structureSizeFromFilenameLength = offsetof(FileInformationStructType, fileName) + fileInformationStruct.fileNameLength;

        return std::max(structureBaseSize, structureSizeFromFilenameLength);
    }

    namespace WindowsInternal
    {
        /// Wrapper around the internal `RtlIsNameInExpression` function, which has no associated header file and requires dynamically linking.
        /// See https://learn.microsoft.com/en-us/windows/win32/devnotes/rtlisnameinexpression for more information.
        BOOLEAN RtlIsNameInExpression(PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PWCH UpcaseTable);
    }
}
