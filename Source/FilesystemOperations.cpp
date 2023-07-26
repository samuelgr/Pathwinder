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

#include <cwctype>
#include <limits>
#include <optional>
#include <string_view>
#include <winternl.h>


namespace Pathwinder
{
    namespace FilesystemOperations
    {
        // -------- INTERNAL CONSTANTS -------------------------------------- //

        // NTSTATUS values. Many are not defined in header files outside of the Windows driver kit.
        // See https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55 for more information.
        static constexpr NTSTATUS kNtStatusSuccess                          = 0x00000000;
        static constexpr NTSTATUS kNtStatusObjectPathInvalid                = 0xC0000039;


        // -------- INTERNAL TYPES ----------------------------------------- //

        // Contains metadata about a file, as retrieved by invoking Windows system calls.
        struct SFileStatInformation
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

        /// Determines if the specified file attributes indicate that the file in question exists.
        /// @param [in] attributes File attributes retrieved from a system call.
        /// @return `true` if the file exists based on the attributes, `false` otherwise.
        static inline bool AttributesIndicateFileExists(DWORD attributes)
        {
            return (INVALID_FILE_ATTRIBUTES != attributes);
        }

        /// Determines if the specified file attributes indicate that the file in question exists and is a directory.
        /// @param [in] attributes File attributes retrieved from a system call.
        /// @return `true` if the file exists and is a directory based on the attributes, `false` otherwise.
        static inline bool AttributesIndicateFileExistsAndIsDirectory(DWORD attributes)
        {
            return ((true == AttributesIndicateFileExists(attributes)) && (0 != (FILE_ATTRIBUTE_DIRECTORY & attributes)));
        }

        /// Ensures the specified directory exists.
        /// Attempts to open a file handle for the specified directory that will cause it to be unchanged if it exists but created if it does not.
        /// If the handle is opened successfully, it is immediately closed.
        /// @param [in] absoluteDirectoryPath Absolute path of the directory to be created.
        /// @return Result of the system call that ensures the specified directory exists.
        static inline NTSTATUS EnsureDirectoryExists(std::wstring_view absoluteDirectoryPath)
        {
            std::optional<TemporaryString> maybePrefixedAbsoluteDirectoryPath = std::nullopt;

            if (false == Strings::PathHasWindowsNamespacePrefix(absoluteDirectoryPath))
            {
                maybePrefixedAbsoluteDirectoryPath = Strings::PathAddWindowsNamespacePrefix(absoluteDirectoryPath);
                absoluteDirectoryPath = maybePrefixedAbsoluteDirectoryPath.value().AsStringView();
            }

            HANDLE directoryHandle = nullptr;

            UNICODE_STRING absoluteDirectoryPathSystemString = Strings::NtConvertStringViewToUnicodeString(absoluteDirectoryPath);
            OBJECT_ATTRIBUTES absoluteDirectoryPathObjectAttributes{};
            InitializeObjectAttributes(&absoluteDirectoryPathObjectAttributes, &absoluteDirectoryPathSystemString, 0, nullptr, nullptr);

            IO_STATUS_BLOCK unusedStatusBlock{};
            NTSTATUS createOrOpenDirectoryResult = Hooks::ProtectedDependency::NtCreateFile::SafeInvoke(&directoryHandle, FILE_TRAVERSE, &absoluteDirectoryPathObjectAttributes, &unusedStatusBlock, 0, 0, (FILE_SHARE_READ | FILE_SHARE_WRITE), FILE_OPEN_IF, FILE_DIRECTORY_FILE, nullptr, 0);

            if (NT_SUCCESS(createOrOpenDirectoryResult))
                Hooks::ProtectedDependency::NtClose::SafeInvoke(directoryHandle);

            return createOrOpenDirectoryResult;
        }

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

            SFileStatInformation absolutePathObjectInfo{};

            UNICODE_STRING absolutePathSystemString = Strings::NtConvertStringViewToUnicodeString(absolutePath);
            OBJECT_ATTRIBUTES absolutePathObjectAttributes{};
            InitializeObjectAttributes(&absolutePathObjectAttributes, &absolutePathSystemString, 0, nullptr, nullptr);

            IO_STATUS_BLOCK unusedStatusBlock{};
            NTSTATUS queryResult = Hooks::ProtectedDependency::NtQueryInformationByName::SafeInvoke(&absolutePathObjectAttributes, &unusedStatusBlock, &absolutePathObjectInfo, sizeof(absolutePathObjectInfo), SFileStatInformation::kFileInformationClass);

            if (!(NT_SUCCESS(queryResult)))
                return INVALID_FILE_ATTRIBUTES;

            return absolutePathObjectInfo.fileAttributes;
        }

        /// Determines if the specified absolute path begins with a drive letter, which consists of an alphabetic character followed by a colon character.
        /// @param [in] absolutePath Absolute path to check without any Windows namespace prefix.
        /// @return `true` if the path begins with a drive letter, `false` otherwise.
        static inline bool PathBeginsWithDriveLetter(std::wstring_view absolutePath)
        {
            return ((0 != std::iswalpha(absolutePath[0])) && (L':' == absolutePath[1]));
        }

        /// Determines if the specified absolute path consists of just a drive letter with no trailing backslash.
        /// In other words, it begins with a drive letter and has a length of 2 characters.
        /// @param [in] absolutePath Absolute path to check without any Windows namespace prefix.
        /// @return `true` if the path consists of just a drive letter, `false` otherwise.
        static inline bool PathIsDriveLetterOnly(std::wstring_view absolutePath)
        {
            return ((2 == absolutePath.length()) && PathBeginsWithDriveLetter(absolutePath));
        }

        /// Extracts the drive letter prefix from the specified absolute path string, if it exists.
        /// The drive letter prefix consists of a letter, a colon, and a backslash.
        /// @param [in] absolutePath Absolute path for which a drive letter prefix is desired, without any Windows namespace prefix.
        /// @return Drive letter prefix if it exists, or an empty string view if it does not.
        static inline std::wstring_view PathGetDriveLetterPrefix(std::wstring_view absolutePath)
        {
            if ((false == PathBeginsWithDriveLetter(absolutePath)) || (absolutePath.length() < 3) || (absolutePath[2] != L'\\'))
                return std::wstring_view();

            return absolutePath.substr(0, 3);
        }

        // -------- FUNCTIONS ---------------------------------------------- //
        // See "FilesystemOperations.h" for documentation.

        intptr_t CreateDirectoryHierarchy(std::wstring_view absoluteDirectoryPath)
        {
            const std::wstring_view windowsNamespacePrefix = Strings::PathGetWindowsNamespacePrefix(absoluteDirectoryPath);
            const std::wstring_view absoluteDirectoryPathTrimmed = Strings::RemoveTrailing(absoluteDirectoryPath.substr(windowsNamespacePrefix.length()), L'\\');

            std::wstring_view driveLetterPrefix = PathGetDriveLetterPrefix(absoluteDirectoryPathTrimmed);
            if ((true == driveLetterPrefix.empty()) || (absoluteDirectoryPath.length() == driveLetterPrefix.length()))
                return kNtStatusObjectPathInvalid;

            // This loop skips over the drive letter prefix and iteratively tries to open directories one level at a time throughout the target hierarchy.
            // For example, if the parameter is "C:\111\222\333\444" then the order of directories will be "C:\111", "C:\111\222", "C:\111\222\333", and finally "C:\111\222\333\444".
            // Current position is initialized to the position of the drive letter prefix backslash character so that when it is incremented it refers to the character right after.

            size_t currentBackslashPosition = (driveLetterPrefix.length() - 1);
            do
            {
                currentBackslashPosition = absoluteDirectoryPathTrimmed.find_first_of(L'\\', (1 + currentBackslashPosition));
                
                std::wstring_view currentDirectoryToTry = absoluteDirectoryPathTrimmed.substr(0, currentBackslashPosition);
                currentDirectoryToTry = absoluteDirectoryPath.substr(0, windowsNamespacePrefix.length() + currentDirectoryToTry.length());

                NTSTATUS currentDirectoryResult = EnsureDirectoryExists(currentDirectoryToTry);
                if (!(NT_SUCCESS(currentDirectoryResult)))
                    return currentDirectoryResult;
            } while (std::wstring_view::npos != currentBackslashPosition);

            return kNtStatusSuccess;
        }

        // --------

        bool Exists(std::wstring_view absolutePath)
        {
            DWORD pathAttributes = 0;

            if (true == PathIsDriveLetterOnly(absolutePath))
            {
                // If the query is for a drive letter itself (for example, "C:") then appending a backslash is necessary.
                // This is a special case. Ordinarily appending a backslash is not required, but for just a drive letter the system call will reject the path if it does not have a trailing backslash.

                wchar_t driveLetterWithBackslash[] = {absolutePath[0], absolutePath[1], L'\\'};
                pathAttributes = GetAttributesForPath(std::wstring_view(driveLetterWithBackslash, _countof(driveLetterWithBackslash)));
            }
            else
            {
                pathAttributes = GetAttributesForPath(absolutePath);
            }

            return AttributesIndicateFileExists(pathAttributes);
        }

        // --------

        bool IsDirectory(std::wstring_view absolutePath)
        {
            DWORD pathAttributes = 0;

            if (true == PathIsDriveLetterOnly(absolutePath))
            {
                // If the query is for a drive letter itself (for example, "C:") then appending a backslash is necessary.
                // This is a special case. Ordinarily appending a backslash is not required, but for just a drive letter the system call will reject the path if it does not have a trailing backslash.

                wchar_t driveLetterWithBackslash[] = {absolutePath[0], absolutePath[1], L'\\'};
                pathAttributes = GetAttributesForPath(std::wstring_view(driveLetterWithBackslash, _countof(driveLetterWithBackslash)));
            }
            else
            {
                pathAttributes = GetAttributesForPath(absolutePath);
            }
            
            return AttributesIndicateFileExistsAndIsDirectory(pathAttributes);
        }
    }
}
