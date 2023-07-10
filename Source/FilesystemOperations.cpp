/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemOperations.cpp
 *   Declaration of functions that provide an abstraction for filesystem
 *   operations executed internally.
 *****************************************************************************/

#pragma once

#include "ApiWindows.h"
#include "FilesystemOperations.h"
#include "Hooks.h"
#include "Message.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <limits>
#include <optional>
#include <string_view>
#include <winternl.h>


namespace Pathwinder
{
    namespace FilesystemOperations
    {        
        // -------- INTERNAL TYPES ----------------------------------------- //

        // Contains metadata about a file, as retrieved by invoking Windows system calls.
        struct FileStatInformation
        {
            // Identifies this structure as the type of information being requested from Windows system calls.
            // Corresponds to the `FileStatInformation` enumerator in the `FILE_INFORMATION_CLASS` enumeration.
            // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ne-wdm-_file_information_class for more information.
            static constexpr FILE_INFORMATION_CLASS kFileInformationClass = static_cast<FILE_INFORMATION_CLASS>(68);
            
            // Holds file metadata. Corresponds to the `FILE_STAT_INFORMATION` structure.
            // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_stat_information for more information.
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


        // -------- INTERNAL FUNCTIONS ------------------------------------- //

        /// Queries for the attributes of the object identified by the specified absolute path.
        /// Roughly analogous to the Windows API function `GetFileAttributes`.
        /// @param [in] absolutePath Absolute path for which attributes are requested.
        /// @return File attributes, or `INVALID_FILE_ATTRIBUTES` in the event of an error.
        static DWORD GetAttributesForPath(std::wstring_view absolutePath)
        {
            std::optional<TemporaryString> maybePrefixedAbsolutePath = std::nullopt;

            if (false == Strings::PathHasWindowsNamespacePrefix(absolutePath))
            {
                maybePrefixedAbsolutePath = Strings::PathAddWindowsNamespacePrefix(absolutePath);
                absolutePath = maybePrefixedAbsolutePath.value().AsStringView();
            }

            FileStatInformation absolutePathObjectInfo{};

            UNICODE_STRING absolutePathSystemString = Strings::NtConvertStringViewToUnicodeString(absolutePath);
            OBJECT_ATTRIBUTES absolutePathObjectAttributes{};
            InitializeObjectAttributes(&absolutePathObjectAttributes, &absolutePathSystemString, 0, nullptr, nullptr);

            IO_STATUS_BLOCK unusedStatusBlock{};
            NTSTATUS queryResult = Hooks::ProtectedDependency::NtQueryInformationByName::SafeInvoke(&absolutePathObjectAttributes, &unusedStatusBlock, &absolutePathObjectInfo, sizeof(absolutePathObjectInfo), FileStatInformation::kFileInformationClass);

            if (!(NT_SUCCESS(queryResult)))
                return INVALID_FILE_ATTRIBUTES;

            return absolutePathObjectInfo.fileAttributes;
        }


        // -------- FUNCTIONS ---------------------------------------------- //
        // See "FilesystemOperations.h" for documentation.

        bool Exists(std::wstring_view absolutePath)
        {
            const DWORD pathAttributes = GetAttributesForPath(absolutePath);
            return (INVALID_FILE_ATTRIBUTES != pathAttributes);
        }

        // --------

        bool IsDirectory(std::wstring_view absolutePath)
        {
            constexpr DWORD kDirectoryAttributeMask = FILE_ATTRIBUTE_DIRECTORY;
            const DWORD pathAttributes = GetAttributesForPath(absolutePath);
            return ((INVALID_FILE_ATTRIBUTES != pathAttributes) && (0 != (kDirectoryAttributeMask & pathAttributes)));
        }
    }
}
