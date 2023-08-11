/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemOperations.h
 *   Declaration of functions that provide an abstraction for filesystem
 *   operations executed internally.
 *****************************************************************************/

#pragma once

#include "ApiWindowsInternal.h"
#include "ValueOrError.h"

#include <cstdint>
#include <string_view>


namespace Pathwinder
{
    namespace FilesystemOperations
    {
        // -------- FUNCTIONS ---------------------------------------------- //

        /// Closes a handle that was previously opened by calling filesystem operation functions.
        /// @param [in] handle Handle to be closed.
        void CloseHandle(HANDLE handle);

        /// Attempts to create the specified directory if it does not already exist.
        /// If needed, also attempts to create all directories that are ancestors of the specified directory.
        /// @param [in] absoluteDirectoryPath Absolute path of the directory to be created along with its hierarchy of ancestors.
        /// @return System call return code for the last system call that completed successfully, safely cast from `NTSTATUS` to a standard integer type.
        intptr_t CreateDirectoryHierarchy(std::wstring_view absoluteDirectoryPath);

        /// Checks if the specified filesystem entity (file, directory, or otherwise) exists.
        /// @param path [in] Absolute path of the entity to check.
        /// @return `true` if the entity exists, `false` otherwise.
        bool Exists(std::wstring_view absolutePath);

        /// Checks if the specified path exists in the filesystem as a directory.
        /// @param path [in] Absolute path to check.
        /// @return `true` if the path exists as a directory, `false` otherwise.
        bool IsDirectory(std::wstring_view absolutePath);

        /// Opens the specified directory for synchronous enumeration.
        /// @param [in] absoluteDirectoryPath Absolute path to the directory to be opened.
        /// @return Handle for the directory file on success, Windows error code on failure.
        ValueOrError<HANDLE, NTSTATUS> OpenDirectoryForEnumeration(std::wstring_view absoluteDirectoryPath);

        /// Attempts to enumerate the contents of the directory identified by open handle, up to whatever portion of the overall contents will fit in the specified buffer.
        /// Can be invoked multiple times on the same handle until all of the directory contents have been enumerated.
        /// @param [in] directoryHandle Open handle for the directory to enumerate.
        /// @param [in] fileInformationClass Type of information to request for each file in the directory.
        /// @param [out] enumerationBuffer Buffer into which to write the information received about the file.
        /// @param [in] enumerationBufferCapacityBytes Size of the destination buffer, in bytes.
        /// @param [in] queryFlags Optional flags for `NtQueryDirectoryFileEx` that describe any customizations to be applied to the query.
        /// @param [in] filePattern Optional file name or pattern to use for filtering the filenames that are returned in the enumeration.
        /// @return Windows error code identifying the result of the operation.
        NTSTATUS PartialEnumerateDirectoryContents(HANDLE directoryHandle, FILE_INFORMATION_CLASS fileInformationClass, void* enumerationBuffer, unsigned int enumerationBufferCapacityBytes, ULONG queryFlags = 0, std::wstring_view filePattern = std::wstring_view());

        /// Obtains information about the specified file by asking the system to enumerate it via directory enumeration.
        /// @param [in] absoluteDirectoryPath Absolute path to the directory containing the file to be enumerated. Windows namespace prefix is not required.
        /// @param [in] fileName Name of the file within the directory. Must not contain any wildcards or backslashes.
        /// @param [in] fileInformationClass Type of information to obtain about the specified file.
        /// @param [out] enumerationBuffer Buffer into which to write the information received about the file.
        /// @param [in] enumerationBufferCapacityBytes Size of the destination buffer, in bytes.
        /// @return Windows error code identifying the result of the operation.
        NTSTATUS QuerySingleFileDirectoryInformation(std::wstring_view absoluteDirectoryPath, std::wstring_view fileName, FILE_INFORMATION_CLASS fileInformationClass, void* enumerationBuffer, unsigned int enumerationBufferCapacityBytes);
    }
}
