/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file FilesystemOperations.h
 *   Declaration of functions that provide an abstraction for filesystem operations executed
 *   internally.
 **************************************************************************************************/

#pragma once

#include <cstdint>
#include <string_view>

#include <Infra/Core/TemporaryBuffer.h>
#include <Infra/Core/ValueOrError.h>

#include "ApiWindows.h"

namespace Pathwinder
{
  namespace FilesystemOperations
  {
    /// Closes a handle that was previously opened by calling filesystem operation functions.
    /// @param [in] handle Handle to be closed.
    /// @return Result of the underlying system call that closes the handle.
    NTSTATUS CloseHandle(HANDLE handle);

    /// Attempts to create the specified directory if it does not already exist.
    /// If needed, also attempts to create all directories that are ancestors of the specified
    /// directory.
    /// @param [in] absoluteDirectoryPath Absolute path of the directory to be created along
    /// with its hierarchy of ancestors.
    /// @return System call return code for the last system call that completed successfully.
    NTSTATUS CreateDirectoryHierarchy(std::wstring_view absoluteDirectoryPath);

    /// Attempts to delete the specified file or directory.
    /// @param [in] absolutePath Absolute path of the entity to delete.
    /// @return System call return code for the deletion operation.
    NTSTATUS Delete(std::wstring_view absolutePath);

    /// Checks if the specified filesystem entity (file, directory, or otherwise) exists.
    /// @param [in] absolutePath Absolute path of the entity to check.
    /// @return `true` if the entity exists, `false` otherwise.
    bool Exists(std::wstring_view absolutePath);

    /// Checks if the specified path exists in the filesystem as a directory.
    /// @param [in] absolutePath Absolute path of the entity to check.
    /// @return `true` if the path exists as a directory, `false` otherwise.
    bool IsDirectory(std::wstring_view absolutePath);

    /// Opens the specified directory for synchronous enumeration.
    /// @param [in] absoluteDirectoryPath Absolute path to the directory to be opened.
    /// @return Handle for the directory file on success, Windows error code on failure.
    Infra::ValueOrError<HANDLE, NTSTATUS> OpenDirectoryForEnumeration(
        std::wstring_view absoluteDirectoryPath);

    /// Attempts to enumerate the contents of the directory identified by open handle, up to
    /// whatever portion of the overall contents will fit in the specified buffer. Can be
    /// invoked multiple times on the same handle until all of the directory contents have been
    /// enumerated.
    /// @param [in] directoryHandle Open handle for the directory to enumerate.
    /// @param [in] fileInformationClass Type of information to request for each file in the
    /// directory.
    /// @param [out] enumerationBuffer Buffer into which to write the information received about
    /// the file.
    /// @param [in] enumerationBufferCapacityBytes Size of the destination buffer, in bytes.
    /// @param [in] queryFlags Optional flags for `NtQueryDirectoryFileEx` that describe any
    /// customizations to be applied to the query.
    /// @param [in] filePattern Optional file name or pattern to use for filtering the filenames
    /// that are returned in the enumeration.
    /// @return Windows error code identifying the result of the operation.
    NTSTATUS PartialEnumerateDirectoryContents(
        HANDLE directoryHandle,
        FILE_INFORMATION_CLASS fileInformationClass,
        void* enumerationBuffer,
        unsigned int enumerationBufferCapacityBytes,
        ULONG queryFlags = 0,
        std::wstring_view filePattern = std::wstring_view());

    /// Obtains the full absolute path for the specified file handle, without a Windows namespace
    /// prefix.
    /// @param [in] fileHandle File handle for which the full absolute path is desired.
    /// @return Absolute path for the file handle, or a Windows error code on failure.
    Infra::ValueOrError<Infra::TemporaryString, NTSTATUS> QueryAbsolutePathByHandle(
        HANDLE fileHandle);

    /// Obtains mode information for the specified file handle. The file handle must be open
    /// already. The mode is itself a bitmask that identifies the effective options that determine
    /// how the I/O system behaves with respect to the file.
    /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_mode_information
    /// @param [in] fileHandle File handle for which mode information is desired.
    /// @return Mode information for the file handle, or a Windows error code on failure.
    Infra::ValueOrError<ULONG, NTSTATUS> QueryFileHandleMode(HANDLE fileHandle);

    /// Obtains information about the specified file by asking the system to enumerate it via
    /// directory enumeration.
    /// @param [in] absoluteDirectoryPath Absolute path to the directory containing the file to
    /// be enumerated. Windows namespace prefix is not required.
    /// @param [in] fileName Name of the file within the directory. Must not contain any
    /// wildcards or backslashes.
    /// @param [in] fileInformationClass Type of information to obtain about the specified file.
    /// @param [out] enumerationBuffer Buffer into which to write the information received about
    /// the file.
    /// @param [in] enumerationBufferCapacityBytes Size of the destination buffer, in bytes.
    /// @return Windows error code identifying the result of the operation.
    NTSTATUS QuerySingleFileDirectoryInformation(
        std::wstring_view absoluteDirectoryPath,
        std::wstring_view fileName,
        FILE_INFORMATION_CLASS fileInformationClass,
        void* enumerationBuffer,
        unsigned int enumerationBufferCapacityBytes);
  } // namespace FilesystemOperations
} // namespace Pathwinder
