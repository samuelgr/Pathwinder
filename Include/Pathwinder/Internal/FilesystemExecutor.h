/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file FilesystemExecutor.h
 *   Declaration of types and functions used to execute filesystem operations under control of
 *   filesystem instructions.
 **************************************************************************************************/

#pragma once

#include <cstdint>
#include <functional>

#include <Infra/TemporaryBuffer.h>

#include "ApiWindows.h"
#include "FileInformationStruct.h"
#include "FilesystemDirector.h"
#include "FilesystemInstruction.h"
#include "OpenHandleStore.h"
#include "Strings.h"

namespace Pathwinder
{
  namespace FilesystemExecutor
  {
    // Many of the functions in this subsystem interact directly with an open handle store and
    // indirectly with a filesystem director. The former is expected to hold the internal state of
    // Pathwinder's filesystem redirection (including all open file handles that it needs to track),
    // and the latter is what applies filesystem rules to determine how to perform redirections.
    // The filesystem executor functions query and update open handle state at various points in
    // their implementations, which is why an open handle store must be passed as a mutable
    // reference. Conversely, obtaining a filesystem instruction is a stateless operation but one
    // that can be invoked with varying parameters by the various filesystem executor functions,
    // which is why it is a function object. Both design choices greatly facilitate testing by
    // allowing the open handle store state to be set up as part of a test case and a pre-determined
    // filesystem instruction to be returned by a function object, under the control of a test case.

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

    /// Advances an in-progress directory enumeration operation by copying file information
    /// structures to an application-supplied buffer. Most parameters come directly from
    /// `NtQueryDirectoryFileEx` but those that do not are documented. This function should only be
    /// invoked with file handles that are already cached in the open handle store and with a
    /// pre-initialized directory enumeration operation state data structure associated with it.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] enumerationState Enumeration state data structure, which must be mutable so it
    /// can be updated as the enumeration proceeds.
    /// @return Windows error code corresponding to the result of advancing the directory
    /// enumeration operation.
    NTSTATUS DirectoryEnumerationAdvance(
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

    /// Prepares for a directory enumeration operation by initializing relevant data structures and
    /// associating them with an open file handle. Similarly-named parameters correspond to the
    /// `NtQueryDirectoryFileEx` system call, with a few exceptions that are explicitly documented.
    /// If data structures have already been initialized for a specific open file handle directory
    /// enumeration, this function returns a pointer to the existing object rather than initializing
    /// a new one.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] instructionSourceFunc Function to be invoked that will retrieve a directory
    /// enumeration instruction, given a source path, file access mode, and create disposition.
    /// @return Nothing if the request should be passed to the underlying system call without
    /// modification, a status code other than 0 (success) if that code should immediately be
    /// returned without further processing, or 0 (success) to indicate that the preparations were
    /// successful and that the directory enumeration is ready to proceed.
    std::optional<NTSTATUS> DirectoryEnumerationPrepare(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE fileHandle,
        PVOID fileInformation,
        ULONG length,
        FILE_INFORMATION_CLASS fileInformationClass,
        PUNICODE_STRING fileName,
        std::function<DirectoryEnumerationInstruction(
            std::wstring_view associatedPath, std::wstring_view realOpenedPath)>
            instructionSourceFunc);

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
    /// @param [in] instructionSourceFunc Function to be invoked that will retrieve a file operation
    /// instruction, given a source path, file access mode, and create disposition.
    /// @param [in] underlyingSystemCallInvoker Invokable function object that performs the actual
    /// operation, with the variable parameters being destination file handle address, object
    /// attributes of the file to attempt, and a create disposition. Other parameters known to the
    /// caller are expected to be embedded into the function object, even those that are passed to
    /// this function.
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
        std::function<FileOperationInstruction(
            std::wstring_view absolutePath,
            FileAccessMode fileAccessMode,
            CreateDisposition createDisposition)> instructionSourceFunc,
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
    /// @param [in] instructionSourceFunc Function to be invoked that will retrieve a file operation
    /// instruction, given a target path for the rename, file access mode, and create disposition.
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
        std::function<FileOperationInstruction(
            std::wstring_view absoluteRenameTargetPath,
            FileAccessMode fileAccessMode,
            CreateDisposition createDisposition)> instructionSourceFunc,
        std::function<NTSTATUS(HANDLE, SFileRenameInformation&, ULONG)>
            underlyingSystemCallInvoker);

    /// Common internal entry point for intercepting queries for file information such that the
    /// input identifies the file of interest by open file handle. Most parameters come directly
    /// from `NtQueryInformationFile` but those that do not are documented.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] underlyingSystemCallInvoker Invokable function object that performs the actual
    /// query operation, given a file handle, I/O status block, file information buffer pointer,
    /// buffer length, and file information class. Any and all other information is expected to be
    /// captured within the object itself, including application-specified parameters. This function
    /// only examines and manipulates the output after it is written into the output file
    /// information buffer.
    /// @param [in] replacementFileNameTransform Optional transformation to apply to the
    /// filename used to replace whatever the system returns from the underlying system call query.
    /// Defaults to no transformation at all. Only invoked for those file information classes that
    /// result in a filename being returned to the calling application. Proposed replacement
    /// filenames will be the full and absolute path, without a Windows namespace prefix, and so
    /// this transformation gives the caller an opportunity to do things like add the prefix, remove
    /// the drive letter, and so on.
    /// @return Result of the operation, which should be returned to the application.
    NTSTATUS QueryByHandle(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        HANDLE fileHandle,
        PIO_STATUS_BLOCK ioStatusBlock,
        PVOID fileInformation,
        ULONG length,
        FILE_INFORMATION_CLASS fileInformationClass,
        std::function<NTSTATUS(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS)>
            underlyingSystemCallInvoker,
        std::function<std::wstring_view(std::wstring_view)> replacementFileNameFilterAndTransform =
            [](std::wstring_view proposedReplacementFileName) -> std::wstring_view
        {
          return proposedReplacementFileName;
        });

    /// Common internal entry point for intercepting queries for file information such that the
    /// input is a name identified in an `OBJECT_ATTRIBUTES` structure but the operation does not
    /// result in a new file handle being created.
    /// @param [in] functionName Name of the API function whose hook function is invoking this
    /// function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of
    /// the named function. Used only for logging.
    /// @param [in] openHandleStore Instance of an open handle store object that holds all of the
    /// file handles known to be open. Sets the context for this call.
    /// @param [in] objectAttributes Object attributes received as input from the application.
    /// @param [in] desiredAccess Access type requested for the query operation in question.
    /// Typically this would be read-only for information requests.
    /// @param [in] instructionSourceFunc Function to be invoked that will retrieve a file operation
    /// instruction, given an absolute path, file access mode, and create disposition.
    /// @param [in] underlyingSystemCallInvoker Invokable function object that performs the actual
    /// operation, with the only variable parameter being object attributes. Any and all other
    /// information is expected to be captured within the object itself, including other
    /// application-specified parameters.
    /// @return Result of the operation, which should be returned to the application.
    NTSTATUS QueryByObjectAttributes(
        const wchar_t* functionName,
        unsigned int functionRequestIdentifier,
        OpenHandleStore& openHandleStore,
        POBJECT_ATTRIBUTES objectAttributes,
        ACCESS_MASK desiredAccess,
        std::function<FileOperationInstruction(
            std::wstring_view absolutePath,
            FileAccessMode fileAccessMode,
            CreateDisposition createDisposition)> instructionSourceFunc,
        std::function<NTSTATUS(POBJECT_ATTRIBUTES)> underlyingSystemCallInvoker);
  } // namespace FilesystemExecutor
} // namespace Pathwinder
