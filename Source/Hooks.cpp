/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file Hooks.cpp
 *   Implementation of all Windows API hook functions used to implement path
 *   redirection.
 *****************************************************************************/

#pragma once

#include "ApiBitSet.h"
#include "ApiWindows.h"
#include "DebugAssert.h"
#include "FilesystemDirector.h"
#include "FilesystemInstruction.h"
#include "Globals.h"
#include "Hooks.h"
#include "Message.h"
#include "MutexWrapper.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <Hookshot/DynamicHook.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>


namespace Pathwinder
{
    // -------- INTERNAL TYPES --------------------------------------------- //

    /// Implements a concurrency-safe storage data structure for open filesystem handles.
    class OpenHandleStore
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Open handle data structure itself.
        /// Maps from a handle to the filesystem path that was used to open it.
        std::unordered_map<HANDLE, std::wstring> openHandles;

        /// Mutex for ensuring concurrency-safe access to the open handles data structure.
        SharedMutex openHandlesMutex;


    public:
        // -------- INSTANCE METHODS --------------------------------------- //

        /// Queries the open handle cache for the specified handle and retrieves the corresponding path if it exists.
        /// @param [in] handleToQuery Handle for which to query.
        /// @return Corresponding filesystem path, if the handle is contained in the cache.
        inline std::optional<std::wstring_view> GetPathForHandle(HANDLE handleToQuery)
        {
            std::shared_lock lock(openHandlesMutex);

            auto openHandleIter = openHandles.find(handleToQuery);
            if (openHandleIter == openHandles.cend())
                return std::nullopt;

            return openHandleIter->second;
        }

        /// Inserts a new handle and corresponding path into the open handle cache.
        /// @param [in] handleToInsert Handle to be inserted.
        /// @param [in] correspondingPath Corresponding filesystem path to store for the handle.
        inline void InsertHandle(HANDLE handleToInsert, std::wstring_view correspondingPath)
        {
            std::unique_lock lock(openHandlesMutex);

            const bool insertionWasSuccessful = openHandles.emplace(handleToInsert, correspondingPath).second;
            DebugAssert(true == insertionWasSuccessful, "Failed to insert a handle into storage.");
        }

        /// Attempts to close and subsequently remove an existing handle and corresponding path from the open handle store.
        /// Both handle closure and removal need to be done while the lock is held, to ensure proper concurrency control.
        /// This avoids a race condition in which a closed handle is reused and re-added to the store before the closing thread has a chance to remove it first.
        /// @param [in] handleToRemove Handle to be removed.
        /// @param [out] correspondingPath String object to receive ownership of the corresponding path to the handle that was removed, if not null. Only filled if the underlying system call to close the handle succeeds.
        /// @return Result of the underlying system call to `NtClose` to close the handle.
        inline NTSTATUS RemoveAndCloseHandle(HANDLE handleToRemove, std::wstring* correspondingPath)
        {
            std::unique_lock lock(openHandlesMutex);

            auto removalIter = openHandles.find(handleToRemove);
            DebugAssert(openHandles.end() != removalIter, "Attempting to erase a handle that was not previously stored.");

            NTSTATUS systemCallResult = Hooks::ProtectedDependency::NtClose::SafeInvoke(handleToRemove);
            if (!(NT_SUCCESS(systemCallResult)))
                return systemCallResult;

            if (nullptr == correspondingPath)
                openHandles.erase(removalIter);
            else
                *correspondingPath = std::move(openHandles.extract(removalIter).mapped());

            return systemCallResult;
        }
    };

    /// Contains all of the information associated with a file operation.
    struct FileOperationContext
    {
        FileOperationRedirectInstruction instruction;                   ///< How the redirection should be performed.
        std::optional<TemporaryString> composedInputPath;               ///< If an input path was composed, for example due to combination with a root directory, then that input path is stored here.
    };


    // -------- INTERNAL VARIABLES ----------------------------------------- //

    /// Holds an atomic counter for hook function requests that increments with each invocation.
    /// Used exclusively for logging.
    static std::atomic<unsigned int> nextRequestIdentifier;


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Retrieves a reference to the singleton instance of the open file handles store.
    /// This cache holds all open handles for directories that might at some point become the `RootDirectory` member of an `OBJECT_ATTRIBUTES` structure or the subject of a directory enumeration query.
    static OpenHandleStore& SingletonOpenHandleStore(void)
    {
        static OpenHandleStore* const openHandleCache = new OpenHandleStore;
        return *openHandleCache;
    }

    /// Conditionally inserts a newly-opened handle into the open handle cache.
    /// Not all newly-opened handles are "interesting" and need to be cached.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] newlyOpenedHandle Handle to add to the cache.
    /// @param [in] associatedPath Path associated with the newly opened handle being added to cache.
    /// @param [in] objectInfo Object information structure that describes how the request was presented to the underlying system call, including the path of the file.
    static void StoreNewlyOpenedHandle(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE newlyOpenedHandle, std::wstring_view associatedPath)
    {
        std::wstring_view associatedPathTrimmed = Strings::RemoveTrailing(associatedPath, L'\\');
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu is being stored in association with path \"%.*s\".", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(newlyOpenedHandle), static_cast<int>(associatedPathTrimmed.length()), associatedPathTrimmed.data());
        SingletonOpenHandleStore().InsertHandle(newlyOpenedHandle, associatedPathTrimmed);
    }

    /// Executes any pre-operations needed ahead of a file operation redirection.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation, including identifying any pre-operations needed.
    /// @return Result of executing all of the pre-operations. The code will indicate success if they all succeed or a failure that corresponds to the first applicable operation failure.
    static NTSTATUS ExecuteExtraPreOperationsForFileOperationRedirection(const wchar_t* functionName, unsigned int functionRequestIdentifier, const FileOperationRedirectInstruction& instruction)
    {
        if (instruction.GetExtraPreOperations().contains(static_cast<int>(FileOperationRedirectInstruction::EExtraPreOperation::EnsurePathHierarchyExists)))
        {
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Ensuring directory hierarchy exists for \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(instruction.GetExtraPreOperationOperand().length()), instruction.GetExtraPreOperationOperand().data());
            // TODO
        }

        return 0;
    }

    /// Determines how to redirect an individual file operation in which the affected file is identified by an object attributes structure.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] inputObjectAttributes Object attributes structure received from the application. Used as input for redirection.
    /// @return Context that contains all of the information needed to submit the file operation to the underlying system call.
    static FileOperationContext GetFileOperationRedirectionContextByObjectAttributes(const wchar_t* functionName, unsigned int functionRequestIdentifier, const OBJECT_ATTRIBUTES& inputObjectAttributes)
    {
        std::optional<TemporaryString> maybeRedirectedFilename = std::nullopt;
        std::optional<std::wstring_view> maybeRootDirectoryHandlePath = ((nullptr == inputObjectAttributes.RootDirectory) ? std::nullopt : SingletonOpenHandleStore().GetPathForHandle(inputObjectAttributes.RootDirectory));

        std::wstring_view inputFilename = Strings::NtConvertUnicodeStringToStringView(*(inputObjectAttributes.ObjectName));

        if (true == maybeRootDirectoryHandlePath.has_value())
        {
            // Input object attributes structure specifies an open directory handle as the root directory and the handle was found in the cache.
            // Before querying for redirection it is necessary to assemble the full filename, including the root directory path.

            TemporaryString inputFullFilename;
            inputFullFilename << maybeRootDirectoryHandlePath.value() << L'\\' << inputFilename;

            FileOperationRedirectInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().RedirectFileOperation(inputFullFilename);
            if (true == redirectionInstruction.HasRedirectedFilename())
                Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined and redirected to \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(maybeRootDirectoryHandlePath.value().length()), maybeRootDirectoryHandlePath.value().data(), reinterpret_cast<size_t>(inputObjectAttributes.RootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
            else
                Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined but not redirected.", functionName, functionRequestIdentifier, static_cast<int>(maybeRootDirectoryHandlePath.value().length()), maybeRootDirectoryHandlePath.value().data(), reinterpret_cast<size_t>(inputObjectAttributes.RootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data());

            return {.instruction = std::move(redirectionInstruction), .composedInputPath = std::move(inputFullFilename)};
        }
        else if (nullptr == inputObjectAttributes.RootDirectory)
        {
            // Input object attributes structure does not specify an open directory handle as the root directory.
            // It is sufficient to send the object name directly for redirection.

            FileOperationRedirectInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().RedirectFileOperation(inputFilename);

            if (true == redirectionInstruction.HasRedirectedFilename())
                Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with path \"%.*s\" which was redirected to \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
            else
                Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with path \"%.*s\" which was not redirected.", functionName, functionRequestIdentifier, static_cast<int>(inputFilename.length()), inputFilename.data());

            return {.instruction = std::move(redirectionInstruction), .composedInputPath = std::nullopt};
        }
        else
        {
            // Input object attributes structure specifies an open directory handle as the root directory but the handle is not in cache.
            // When the root directory handle was originally opened it was determined that there is no possible match with a filesystem rule.
            // Therefore, it is not necessary to attempt redirection.

            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with root directory handle %zu and relative path \"%.*s\" for which no redirection was attempted.", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(inputObjectAttributes.RootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data());
            return {.instruction = FileOperationRedirectInstruction::NoRedirectionOrInterception(), .composedInputPath = std::nullopt};
        }
    }
}


// -------- PROTECTED HOOK FUNCTIONS --------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtClose::Hook(HANDLE Handle)
{
    using namespace Pathwinder;


    auto maybeClosedHandlePath = SingletonOpenHandleStore().GetPathForHandle(Handle);
    if (false == maybeClosedHandlePath.has_value())
        return Original(Handle);

    const unsigned int requestIdentifier = nextRequestIdentifier++;

    std::wstring closedHandlePath;
    NTSTATUS closeHandleResult = SingletonOpenHandleStore().RemoveAndCloseHandle(Handle, &closedHandlePath);

    if (NT_SUCCESS(closeHandleResult))
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %llu for path \"%s\" was closed and erased from storage.", GetFunctionName(), requestIdentifier, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(Handle)), closedHandlePath.c_str());

    return closeHandleResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtCreateFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = nextRequestIdentifier++;

    const FileOperationContext operationContext = GetFileOperationRedirectionContextByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes);
    const FileOperationRedirectInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

    NTSTATUS preOperationResult = ExecuteExtraPreOperationsForFileOperationRedirection(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    std::wstring_view unredirectedPath = ((true == operationContext.composedInputPath.has_value()) ? operationContext.composedInputPath.value().AsStringView() : Strings::NtConvertUnicodeStringToStringView(*ObjectAttributes->ObjectName));
    std::wstring_view redirectedPath;

    UNICODE_STRING redirectedUnicodeString = {};
    OBJECT_ATTRIBUTES redirectedObjectAttributes = *ObjectAttributes;
    if (true == redirectionInstruction.HasRedirectedFilename())
    {
        redirectedPath = redirectionInstruction.GetRedirectedFilename();
        redirectedUnicodeString = Strings::NtConvertStringViewToUnicodeString(redirectedPath);
        redirectedObjectAttributes.ObjectName = &redirectedUnicodeString;
    }

    HANDLE newlyOpenedHandle = nullptr;
    NTSTATUS systemCallResult = 0;
    std::wstring_view lastAttemptedPath;

    switch (redirectionInstruction.GetFilenamesToTry())
    {
    case FileOperationRedirectInstruction::ETryFiles::UnredirectedOnly:
        lastAttemptedPath = unredirectedPath;
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);
        break;

    case FileOperationRedirectInstruction::ETryFiles::UnredirectedFirst:
        lastAttemptedPath = unredirectedPath;
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);

        if (!(NT_SUCCESS(systemCallResult)))
        {
            lastAttemptedPath = redirectedPath;
            systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, &redirectedObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        }
        break;

    case FileOperationRedirectInstruction::ETryFiles::RedirectedFirst:
        lastAttemptedPath = redirectedPath;
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, &redirectedObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        
        if (!(NT_SUCCESS(systemCallResult)))
        {
            lastAttemptedPath = unredirectedPath;
            systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);
        }
        break;

    case FileOperationRedirectInstruction::ETryFiles::RedirectedOnly:
        lastAttemptedPath = redirectedPath;
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, &redirectedObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        break;

    default:
        Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationRedirectInstruction::ETryFiles = %u).", GetFunctionName(), requestIdentifier, static_cast<unsigned int>(redirectionInstruction.GetFilenamesToTry()));
        break;
    }

    if (NT_SUCCESS(systemCallResult))
    {
        switch (redirectionInstruction.GetFilenameHandleAssociation())
        {
        case FileOperationRedirectInstruction::EAssociateNameWithHandle::None:
            break;

        case FileOperationRedirectInstruction::EAssociateNameWithHandle::WhicheverWasSuccessful:
            StoreNewlyOpenedHandle(GetFunctionName(), requestIdentifier, newlyOpenedHandle, lastAttemptedPath);
            break;

        case FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected:
            StoreNewlyOpenedHandle(GetFunctionName(), requestIdentifier, newlyOpenedHandle, unredirectedPath);
            break;

        case FileOperationRedirectInstruction::EAssociateNameWithHandle::Redirected:
            StoreNewlyOpenedHandle(GetFunctionName(), requestIdentifier, newlyOpenedHandle, redirectedPath);
            break;

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationRedirectInstruction::EAssociateNameWithHandle = %u).", GetFunctionName(), requestIdentifier, static_cast<unsigned int>(redirectionInstruction.GetFilenameHandleAssociation()));
        }
    }

    *FileHandle = newlyOpenedHandle;
    return systemCallResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtOpenFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = nextRequestIdentifier++;

    const FileOperationContext operationContext = GetFileOperationRedirectionContextByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes);
    const FileOperationRedirectInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);

    NTSTATUS preOperationResult = ExecuteExtraPreOperationsForFileOperationRedirection(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    std::wstring_view unredirectedPath = ((true == operationContext.composedInputPath.has_value()) ? operationContext.composedInputPath.value().AsStringView() : Strings::NtConvertUnicodeStringToStringView(*ObjectAttributes->ObjectName));
    std::wstring_view redirectedPath;

    UNICODE_STRING redirectedUnicodeString = {};
    OBJECT_ATTRIBUTES redirectedObjectAttributes = *ObjectAttributes;
    if (true == redirectionInstruction.HasRedirectedFilename())
    {
        redirectedPath = redirectionInstruction.GetRedirectedFilename();
        redirectedUnicodeString = Strings::NtConvertStringViewToUnicodeString(redirectedPath);
        redirectedObjectAttributes.ObjectName = &redirectedUnicodeString;
    }

    HANDLE newlyOpenedHandle = nullptr;
    NTSTATUS systemCallResult = 0;
    std::wstring_view lastAttemptedPath;

    switch (redirectionInstruction.GetFilenamesToTry())
    {
    case FileOperationRedirectInstruction::ETryFiles::UnredirectedOnly:
        lastAttemptedPath = unredirectedPath;
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);
        break;

    case FileOperationRedirectInstruction::ETryFiles::UnredirectedFirst:
        lastAttemptedPath = unredirectedPath;
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);

        if (!(NT_SUCCESS(systemCallResult)))
        {
            lastAttemptedPath = redirectedPath;
            systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, &redirectedObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        }
        break;

    case FileOperationRedirectInstruction::ETryFiles::RedirectedFirst:
        lastAttemptedPath = redirectedPath;
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, &redirectedObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        
        if (!(NT_SUCCESS(systemCallResult)))
        {
            lastAttemptedPath = unredirectedPath;
            systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);
        }
        break;

    case FileOperationRedirectInstruction::ETryFiles::RedirectedOnly:
        lastAttemptedPath = redirectedPath;
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, &redirectedObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        break;

    default:
        Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationRedirectInstruction::ETryFiles = %u).", GetFunctionName(), requestIdentifier, static_cast<unsigned int>(redirectionInstruction.GetFilenamesToTry()));
        break;
    }

    if (NT_SUCCESS(systemCallResult))
    {
        switch (redirectionInstruction.GetFilenameHandleAssociation())
        {
        case FileOperationRedirectInstruction::EAssociateNameWithHandle::None:
            break;

        case FileOperationRedirectInstruction::EAssociateNameWithHandle::WhicheverWasSuccessful:
            StoreNewlyOpenedHandle(GetFunctionName(), requestIdentifier, newlyOpenedHandle, lastAttemptedPath);
            break;

        case FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected:
            StoreNewlyOpenedHandle(GetFunctionName(), requestIdentifier, newlyOpenedHandle, unredirectedPath);
            break;

        case FileOperationRedirectInstruction::EAssociateNameWithHandle::Redirected:
            StoreNewlyOpenedHandle(GetFunctionName(), requestIdentifier, newlyOpenedHandle, redirectedPath);
            break;

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationRedirectInstruction::EAssociateNameWithHandle = %u).", GetFunctionName(), requestIdentifier, static_cast<unsigned int>(redirectionInstruction.GetFilenameHandleAssociation()));
        }
    }

    *FileHandle = newlyOpenedHandle;
    return systemCallResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFile::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName, BOOLEAN RestartScan)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = nextRequestIdentifier++;

    return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, ReturnSingleEntry, FileName, RestartScan);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFileEx::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, ULONG QueryFlags, PUNICODE_STRING FileName)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = nextRequestIdentifier++;

    return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, QueryFlags, FileName);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationByName::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = nextRequestIdentifier++;

    const FileOperationContext operationContext = GetFileOperationRedirectionContextByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes);
    const FileOperationRedirectInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

    NTSTATUS preOperationResult = ExecuteExtraPreOperationsForFileOperationRedirection(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    UNICODE_STRING redirectedUnicodeString = {};
    OBJECT_ATTRIBUTES redirectedObjectAttributes = *ObjectAttributes;
    if (true == redirectionInstruction.HasRedirectedFilename())
    {
        redirectedUnicodeString = Strings::NtConvertStringViewToUnicodeString(redirectionInstruction.GetRedirectedFilename());
        redirectedObjectAttributes.ObjectName = &redirectedUnicodeString;
    }

    NTSTATUS systemCallResult = 0;

    switch (redirectionInstruction.GetFilenamesToTry())
    {
    case FileOperationRedirectInstruction::ETryFiles::UnredirectedOnly:
        systemCallResult = Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);
        break;

    case FileOperationRedirectInstruction::ETryFiles::UnredirectedFirst:
        systemCallResult = Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);

        if (!(NT_SUCCESS(systemCallResult)))
        {
            systemCallResult = Original(&redirectedObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        }
        break;

    case FileOperationRedirectInstruction::ETryFiles::RedirectedFirst:
        systemCallResult = Original(&redirectedObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        
        if (!(NT_SUCCESS(systemCallResult)))
        {
            systemCallResult = Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);
        }
        break;

    case FileOperationRedirectInstruction::ETryFiles::RedirectedOnly:
        systemCallResult = Original(&redirectedObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        break;

    default:
        Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationRedirectInstruction::ETryFiles = %u).", GetFunctionName(), requestIdentifier, static_cast<unsigned int>(redirectionInstruction.GetFilenamesToTry()));
        break;
    }

    return systemCallResult;
}


// -------- UNPROTECTED HOOK FUNCTIONS ------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryAttributesFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFO FileInformation)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = nextRequestIdentifier++;

    const FileOperationContext operationContext = GetFileOperationRedirectionContextByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes);
    const FileOperationRedirectInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(ObjectAttributes, FileInformation);

    NTSTATUS preOperationResult = ExecuteExtraPreOperationsForFileOperationRedirection(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    UNICODE_STRING redirectedUnicodeString = {};
    OBJECT_ATTRIBUTES redirectedObjectAttributes = *ObjectAttributes;
    if (true == redirectionInstruction.HasRedirectedFilename())
    {
        redirectedUnicodeString = Strings::NtConvertStringViewToUnicodeString(redirectionInstruction.GetRedirectedFilename());
        redirectedObjectAttributes.ObjectName = &redirectedUnicodeString;
    }

    NTSTATUS systemCallResult = 0;

    switch (redirectionInstruction.GetFilenamesToTry())
    {
    case FileOperationRedirectInstruction::ETryFiles::UnredirectedOnly:
        systemCallResult = Original(ObjectAttributes, FileInformation);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);
        break;

    case FileOperationRedirectInstruction::ETryFiles::UnredirectedFirst:
        systemCallResult = Original(ObjectAttributes, FileInformation);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);

        if (!(NT_SUCCESS(systemCallResult)))
        {
            systemCallResult = Original(&redirectedObjectAttributes, FileInformation);
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        }
        break;

    case FileOperationRedirectInstruction::ETryFiles::RedirectedFirst:
        systemCallResult = Original(&redirectedObjectAttributes, FileInformation);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        
        if (!(NT_SUCCESS(systemCallResult)))
        {
            systemCallResult = Original(ObjectAttributes, FileInformation);
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for unredirected path.", GetFunctionName(), requestIdentifier, systemCallResult);
        }
        break;

    case FileOperationRedirectInstruction::ETryFiles::RedirectedOnly:
        systemCallResult = Original(&redirectedObjectAttributes, FileInformation);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x for redirected path \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
        break;

    default:
        Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationRedirectInstruction::ETryFiles = %u).", GetFunctionName(), requestIdentifier, static_cast<unsigned int>(redirectionInstruction.GetFilenamesToTry()));
        break;
    }

    return systemCallResult;
}
