/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file FilesystemExecutor.h
 *   Declaration of types and functions used to execute filesystem operations under control of
 *   filesystem instructions.
 **************************************************************************************************/

#pragma once

#include <cstdint>
#include <functional>

#include "ApiWindows.h"
#include "FileInformationStruct.h"
#include "FilesystemDirector.h"
#include "OpenHandleStore.h"
#include "Strings.h"
#include "TemporaryBuffer.h"
#include "ValueOrError.h"

namespace Pathwinder
{
  namespace FilesystemExecutor
  {
    /// Common internal entry point for intercepting attempts to close an existing file handle.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] handle Handle that the application has requested to close.
    /// @param [in] underlyingSystemCallInvoker Invokable function object that performs the actual
    /// operation, with the only variable parameter being the handle to close. Any and all other
    /// information is expected to be captured within the object itself.
    /// @return Result of the operation, which should be returned to the application.
    NTSTATUS CloseHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE handle,
        std::function<NTSTATUS(HANDLE)> underlyingSystemCallInvoker);

    /// Common internal entry point for intercepting directory enumerations. Parameters correspond
    /// to the `NtQueryDirectoryFileEx` system call, with the exception of `functionName` and
    /// `functionRequestIdentifier`, which are the hook function name and request identifier for
    /// logging purposes, and `openHandleStore`, which sets the context for this call.
    /// @return Result to be returned to the application on system call completion, or nothing at
    /// all if the request should be forwarded unmodified to the system.
    std::optional<NTSTATUS> DirectoryEnumeration(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE fileHandle,
        HANDLE event,
        PIO_APC_ROUTINE apcRoutine,
        PVOID apcContext,
        PIO_STATUS_BLOCK ioStatusBlock,
        PVOID fileInformation,
        ULONG length,
        FILE_INFORMATION_CLASS fileInformationClass,
        ULONG queryFlags,
        PUNICODE_STRING fileName);

    /// Common internal entry point for intercepting attempts to create or open files, resulting in
    /// the creation of a new file handle.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] fileHandle Address that will receive the newly-created file handle, if this
    /// function is successful.
    /// @param [in] desiredAccess Desired file access types requested by the application.
    /// @param [in] objectAttributes Object attributes that identify the filesystem entity for which
    /// a new handle should be created. As received from the application.
    /// @param [in] shareAccess Sharing mask received from the application.
    /// @param [in] createDisposition Create disposition received from the application. Identifies
    /// whether a new file should be created, existing file should be opened, and so on.
    /// @param [in] createOptions File creation or opening options received from the application.
    /// @param [in] underlyingSystemCallInvoker Invokable function object that performs the actual
    /// operation, with the variable parameters being destination file handle address, object
    /// attributes of the file to attempt, and a create disposition.
    /// @return Result of the operation, which should be returned to the application.
    NTSTATUS NewFileHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        PHANDLE fileHandle,
        ACCESS_MASK desiredAccess,
        POBJECT_ATTRIBUTES objectAttributes,
        ULONG shareAccess,
        ULONG createDisposition,
        ULONG createOptions,
        std::function<NTSTATUS(PHANDLE, POBJECT_ATTRIBUTES, ULONG)> underlyingSystemCallInvoker);

    /// Common internal entry point for intercepting attempts to rename a file or directory that has
    /// already been opened and associated with a file handle.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] fileHandle Open handle associated with the file or directory being renamed.
    /// @param [in] renameInformation Windows structure describing the rename operation, as supplied
    /// by the application. Among other things, contains the desired new name.
    /// @param [in] renameInformationLength Size of the rename information structure, in bytes, as
    /// supplied by the application.
    /// @param [in] underlyingSystemCallInvoker Invokable function object that performs the actual
    /// operation, with the only variable parameters being open file handle, rename information
    /// structure, and rename information structure length in bytes. Any and all other information
    /// is expected to be captured within the object itself, including other application-specified
    /// parameters.
    /// @return Result of the operation, which should be returned to the application.
    NTSTATUS RenameByHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE fileHandle,
        SFileRenameInformation& renameInformation,
        ULONG renameInformationLength,
        std::function<NTSTATUS(HANDLE, SFileRenameInformation&, ULONG)>
            underlyingSystemCallInvoker);

    /// Common internal entry point for intercepting queries for file information such that the
    /// input is a name identified in an `OBJECT_ATTRIBUTES` structure but the operation does not
    /// result in a new file handle being created.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] fileAccessMode Type of accesses that the underlying system call is expected to
    /// perform on the file.
    /// @param [in] objectAttributes Object attributes received as input from the application.
    /// @param [in] underlyingSystemCallInvoker Invokable function object that performs the actual
    /// operation, with the only variable parameter being object attributes. Any and all other
    /// information is expected to be captured within the object itself, including other
    /// application-specified parameters.
    /// @return Result of the operation, which should be returned to the application.
    NTSTATUS QueryByObjectAttributes(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        FileAccessMode fileAccessMode,
        POBJECT_ATTRIBUTES objectAttributes,
        std::function<NTSTATUS(POBJECT_ATTRIBUTES)> underlyingSystemCallInvoker);

    /// Common internal entry point for intercepting queries for file name information such that the
    /// input identifies the file of interest by open file handle.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] fileHandle Open handle associated with the file for which information is
    /// requested.
    /// @param [in] fileNameInformation Buffer that will receive file name information when the
    /// underlying system call is invoked.
    /// @param [in] fileNameInformationBufferCapacity Capacity of the buffer that holds the file
    /// name information structure.
    /// @param [in] underlyingSystemCallInvoker Invokable function object that performs the actual
    /// operation, with the only variable parameter being object attributes. Any and all other
    /// information is expected to be captured within the object itself, including other
    /// application-specified parameters.
    /// @param [in] replacementFileNameFilterAndTransform Optional transformation to apply to the
    /// filename used to replace whatever the system returns from the underlying system call query.
    /// If this function returns nothing, then the underlying system call is invoked and not
    /// intercepted. Defaults to no transformation at all. First parameter is system-returned
    /// filename, and second parameter is proposed replacement filename.
    /// @return Result of the operation, which should be returned to the application.
    NTSTATUS QueryNameByHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE fileHandle,
        SFileNameInformation* fileNameInformation,
        ULONG fileNameInformationBufferCapacity,
        std::function<NTSTATUS(HANDLE)> underlyingSystemCallInvoker,
        std::function<std::optional<std::wstring_view>(
            std::wstring_view, std::wstring_view)> replacementFileNameFilterAndTransform =
            [](std::wstring_view systemReturnedFileName,
               std::wstring_view proposedReplacementFileName) -> std::optional<std::wstring_view>
        {
          return proposedReplacementFileName;
        });
  } // namespace FilesystemExecutor
} // namespace Pathwinder
