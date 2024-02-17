/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file ApiWindows.h
 *   Common header file for the correct version of the Windows API.
 **************************************************************************************************/

#pragma once

// Windows header files are sensitive to include order. Compilation will fail if the order is
// incorrect. Top-level macros and headers must come first, followed by headers for other parts
// of system functionality.

// clang-format off

#define NOMINMAX
#include <sdkddkver.h>
#include <windows.h>

// clang-format on

#include <knownfolders.h>
#include <psapi.h>
#include <shlobj.h>
#include <winternl.h>

// These file create/open options flags are not defined in available headers.
#ifndef FILE_DISALLOW_EXCLUSIVE
#define FILE_DISALLOW_EXCLUSIVE 0x00020000
#endif
#ifndef FILE_SESSION_AWARE
#define FILE_SESSION_AWARE 0x00040000
#endif
#ifndef FILE_CONTAINS_EXTENDED_CREATE_INFORMATION
#define FILE_CONTAINS_EXTENDED_CREATE_INFORMATION 0x10000000
#endif

// These `SL_QUERY_DIRECTORY_MASK` flags are not defined in available headers.
// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfileex
#ifndef SL_RESTART_SCAN
#define SL_RESTART_SCAN 0x00000001
#endif
#ifndef SL_RETURN_SINGLE_ENTRY
#define SL_RETURN_SINGLE_ENTRY 0x00000002
#endif
#ifndef SL_INDEX_SPECIFIED
#define SL_INDEX_SPECIFIED 0x00000004
#endif
#ifndef SL_RETURN_ON_DISK_ENTRIES_ONLY
#define SL_RETURN_ON_DISK_ENTRIES_ONLY 0x00000008
#endif
#ifndef SL_NO_CURSOR_UPDATE_QUERY
#define SL_NO_CURSOR_UPDATE_QUERY 0x00000010
#endif

namespace Pathwinder
{
  // NTSTATUS values. Many are not defined in header files outside of the Windows driver kit.
  // https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55
  namespace NtStatus
  {
    /// `STATUS_SUCCESS`: The operation completed successfully.
    inline constexpr NTSTATUS kSuccess = 0x00000000;

    /// `STATUS_PENDING`: The operation that was requested is pending completion.
    inline constexpr NTSTATUS kPending = 0x00000103;

    /// `STATUS_MORE_ENTRIES`: Returned by enumeration APIs to indicate more information is
    /// available to successive calls.
    inline constexpr NTSTATUS kMoreEntries = 0x00000105;

    /// `STATUS_OBJECT_NAME_EXISTS`: An attempt was made to create an object but the object name
    /// already exists.
    inline constexpr NTSTATUS kObjectNameExists = 0x40000000;

    /// `STATUS_BUFFER_OVERFLOW`: The data was too large to fit into the specified buffer.
    inline constexpr NTSTATUS kBufferOverflow = 0x80000005;

    /// `STATUS_NO_MORE_FILES`: No more files were found which match the file specification.
    inline constexpr NTSTATUS kNoMoreFiles = 0x80000006;

    /// `STATUS_INVALID_INFO_CLASS`: The specified information class is not a valid information
    /// class for the specified object.
    inline constexpr NTSTATUS kInvalidInfoClass = 0xC0000003;

    /// `STATUS_INVALID_HANDLE`: An invalid HANDLE was specified.
    inline constexpr NTSTATUS kInvalidHandle = 0xC0000008;

    /// `STATUS_INVALID_PARAMETER`: An invalid parameter was passed to a service or function.
    inline constexpr NTSTATUS kInvalidParameter = 0xC000000D;

    /// `STATUS_NO_SUCH_FILE`: The file does not exist.
    inline constexpr NTSTATUS kNoSuchFile = 0xC000000F;

    /// `STATUS_BUFFER_TOO_SMALL`: The buffer is too small to contain the entry. No information
    /// has been written to the buffer.
    inline constexpr NTSTATUS kBufferTooSmall = 0xC0000023;

    /// `STATUS_OBJECT_NAME_INVALID`: The object name is invalid.
    inline constexpr NTSTATUS kObjectNameInvalid = 0xC0000033;

    /// `STATUS_OBJECT_NAME_NOT_FOUND`: The object name is not found.
    inline constexpr NTSTATUS kObjectNameNotFound = 0xC0000034;

    /// `STATUS_OBJECT_NAME_COLLISION`: The object name already exists.
    inline constexpr NTSTATUS kObjectNameCollision = 0xC0000035;

    /// `STATUS_OBJECT_PATH_INVALID`: The object path component was not a directory object.
    inline constexpr NTSTATUS kObjectPathInvalid = 0xC0000039;

    /// `STATUS_OBJECT_PATH_NOT_FOUND`: The object path does not exist.
    inline constexpr NTSTATUS kObjectPathNotFound = 0xC000003A;

    /// `STATUS_OBJECT_PATH_SYNTAX_BAD`: The object path component was not a directory object.
    inline constexpr NTSTATUS kObjectPathSyntaxBad = 0xC000003B;

    /// `STATUS_INTERNAL_ERROR`: An internal error occurred.
    inline constexpr NTSTATUS kInternalError = 0xC00000E5;
  } // namespace NtStatus

  /// Retrieves the proper internal address of a Windows API function.
  /// Many Windows API functions have been moved to lower-level binaries, and some functions are
  /// intended for drivers rather than applications.
  /// https://docs.microsoft.com/en-us/windows/win32/win7appqual/new-low-level-binaries
  /// @param [in] funcName API function name.
  /// @return Address to use for the Windows API function, or `nullptr` if the API function could
  /// not be located.
  void* GetInternalWindowsApiFunctionAddress(const char* const funcName);

  namespace WindowsInternal
  {
    /// Wrapper around the internal `RtlIsNameInExpression` function, which has no associated
    /// header file and requires dynamically linking.
    /// https://learn.microsoft.com/en-us/windows/win32/devnotes/rtlisnameinexpression
    BOOLEAN RtlIsNameInExpression(
        PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PWCH UpcaseTable);
  } // namespace WindowsInternal
} // namespace Pathwinder
