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

    /// Implements a concurrency-safe cache for open filesystem handles.
    class OpenHandleCache
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
            DebugAssert(true == insertionWasSuccessful, "Failed to insert a handle into the handle cache.");
        }

        /// Attempts to close and subsequently remove an existing handle and corresponding path from the open handle cache.
        /// Both handle closure and removal need to be done while the lock is held, to ensure proper concurrency control.
        /// This avoids a race condition in which a closed handle is reused and re-added to the cache before the closing thread has a chance to remove it first.
        /// @param [in] handleToRemove Handle to be removed.
        /// @param [out] correspondingPath String object to receive ownership of the corresponding path to the handle that was removed, if not null. Only filled if the underlying system call to close the handle succeeds.
        /// @return Result of the underlying system call to `NtClose` to close the handle.
        inline NTSTATUS RemoveAndCloseHandle(HANDLE handleToRemove, std::wstring* correspondingPath)
        {
            std::unique_lock lock(openHandlesMutex);

            auto removalIter = openHandles.find(handleToRemove);
            DebugAssert(openHandles.end() != removalIter, "Attempting to remove a handle that was not cached in the open handles cache.");

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


    // -------- INTERNAL VARIABLES ----------------------------------------- //

    /// Holds an atomic counter for hook function requests that increments with each invocation.
    /// Used exclusively for logging.
    static std::atomic<unsigned int> nextRequestIdentifier;


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Retrieves a reference to the singleton instance of the open file handles cache.
    /// This cache holds all open handles for directories that might at some point become the `RootDirectory` member of an `OBJECT_ATTRIBUTES` structure or the subject of a directory enumeration query.
    static OpenHandleCache& SingletonOpenHandleCache(void)
    {
        static OpenHandleCache* const openHandleCache = new OpenHandleCache;
        return *openHandleCache;
    }

    /// Conditionally inserts a newly-opened handle into the open handle cache.
    /// Not all newly-opened handles are "interesting" and need to be cached.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] newlyOpenedHandle Handle to add to the cache.
    /// @param [in] associatedPath Path associated with the newly opened handle being added to cache.
    /// @param [in] wasRedirected Whether or not the handle was opened after a file operation redirection took place.
    /// @param [in] objectInfo Object information structure that describes how the request was presented to the underlying system call, including the path of the file.
    static void InsertNewlyOpenedHandleIntoCache(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE newlyOpenedHandle, std::wstring_view associatedPath, bool wasRedirected)
    {
        std::wstring_view associatedPathTrimmed = Strings::RemoveTrailing(associatedPath, L'\\');
        if (true == wasRedirected)
        {
            // If a new handle was opened for a path that has been redirected, it is possible that the handle represents a directory that could be passed as a parameter to one of the directory enumeration system call functions.
            // For this reason, it will be cached in the handle cache. Unconditionally caching is most likely less expensive than checking with the filesystem directly, so this is the approach that will be taken.

            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu is being cached for redirected path \"%.*s\".", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(newlyOpenedHandle), static_cast<int>(associatedPathTrimmed.length()), associatedPathTrimmed.data());
            SingletonOpenHandleCache().InsertHandle(newlyOpenedHandle, associatedPathTrimmed);
        }
        else if ((false == wasRedirected) && (true == FilesystemDirector::Singleton().IsPrefixForAnyRule(associatedPathTrimmed)))
        {
            // If a new handle was opened for a directory path that was not redirected, but exists in the hierarchy towards filesystem rules that could result in redirection, the handle needs to be cached along with the path.
            // In a future call this handle could be specified as the `RootDirectory` handle in an `OBJECT_ATTRIBUTES` structure, which means the resulting combined path might need to be redirected.

            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu is being cached for non-redirected path \"%.*s\".", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(newlyOpenedHandle), static_cast<int>(associatedPathTrimmed.length()), associatedPathTrimmed.data());
            SingletonOpenHandleCache().InsertHandle(newlyOpenedHandle, associatedPathTrimmed);
        }
    }

    /// Redirects an individual filename identified by an object attributes structure.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] inputObjectAttributes Object attributes structure received from the application. Used as input for redirection.
    /// @return New object information structure containing everything required to replace the input object attributes structure when invoking the original system call.
    static std::optional<FileOperationRedirectInstruction> RedirectFileOperationByObjectAttributes(const wchar_t* functionName, unsigned int functionRequestIdentifier, const OBJECT_ATTRIBUTES& inputObjectAttributes)
    {
        std::optional<TemporaryString> maybeRedirectedFilename = std::nullopt;
        std::optional<std::wstring_view> maybeRootDirectoryHandlePath = ((nullptr == inputObjectAttributes.RootDirectory) ? std::nullopt : SingletonOpenHandleCache().GetPathForHandle(inputObjectAttributes.RootDirectory));

        std::wstring_view inputFilename = Strings::NtConvertUnicodeStringToStringView(*(inputObjectAttributes.ObjectName));

        if (true == maybeRootDirectoryHandlePath.has_value())
        {
            // Input object attributes structure specifies an open directory handle as the root directory and the handle was found in the cache.
            // Before querying for redirection it is necessary to assemble the full filename, including the root directory path.

            TemporaryString inputFullFilename;
            inputFullFilename << maybeRootDirectoryHandlePath.value() << L'\\' << inputFilename;

            FileOperationRedirectInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().RedirectFileOperation(inputFullFilename);
            if (true == redirectionInstruction.IsFilesystemOperationRedirected())
                Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined and redirected via rule \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(maybeRootDirectoryHandlePath.value().length()), maybeRootDirectoryHandlePath.value().data(), reinterpret_cast<size_t>(inputObjectAttributes.RootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectionInstruction.GetFilesystemRule().GetName().length()), redirectionInstruction.GetFilesystemRule().GetName().data());
            else
                Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined but not redirected.", functionName, functionRequestIdentifier, static_cast<int>(maybeRootDirectoryHandlePath.value().length()), maybeRootDirectoryHandlePath.value().data(), reinterpret_cast<size_t>(inputObjectAttributes.RootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data());

            return redirectionInstruction;
        }
        else if (nullptr == inputObjectAttributes.RootDirectory)
        {
            // Input object attributes structure does not specify an open directory handle as the root directory.
            // It is sufficient to send the object name directly for redirection.

            FileOperationRedirectInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().RedirectFileOperation(inputFilename);

            if (true == redirectionInstruction.IsFilesystemOperationRedirected())
                Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with path \"%.*s\" which was redirected via rule \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectionInstruction.GetFilesystemRule().GetName().length()), redirectionInstruction.GetFilesystemRule().GetName().data());
            else
                Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with path \"%.*s\" which was not redirected.", functionName, functionRequestIdentifier, static_cast<int>(inputFilename.length()), inputFilename.data());

            return redirectionInstruction;
        }
        else
        {
            // Input object attributes structure specifies an open directory handle as the root directory but the handle is not in cache.
            // When the root directory handle was originally opened it was determined that there is no possible match with a filesystem rule.
            // Therefore, it is not necessary to attempt redirection.

            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with root directory handle %zu and relative path \"%.*s\" for which no redirection was attempted.", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(inputObjectAttributes.RootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data());
            return std::nullopt;
        }
    }
}


// -------- PROTECTED HOOK FUNCTIONS --------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtClose::Hook(HANDLE Handle)
{
    using namespace Pathwinder;


    auto maybeClosedHandlePath = SingletonOpenHandleCache().GetPathForHandle(Handle);
    if (false == maybeClosedHandlePath.has_value())
        return Original(Handle);

    const unsigned int requestIdentifier = nextRequestIdentifier++;

    std::wstring closedHandlePath;
    NTSTATUS closeHandleResult = SingletonOpenHandleCache().RemoveAndCloseHandle(Handle, &closedHandlePath);

    if (NT_SUCCESS(closeHandleResult))
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %llu for path \"%s\" was closed and removed from the cache.", GetFunctionName(), requestIdentifier, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(Handle)), closedHandlePath.c_str());

    return closeHandleResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtCreateFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = nextRequestIdentifier++;

    std::optional<FileOperationRedirectInstruction> maybeRedirectionInstruction = RedirectFileOperationByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes);
    if (false == maybeRedirectionInstruction.has_value())
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

    if (true == Globals::GetConfigurationData().isDryRunMode)
    {
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Continuing with unmodified original path because dry run mode is enabled.", GetFunctionName(), requestIdentifier);
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }

    FileOperationRedirectInstruction& redirectionInstruction = maybeRedirectionInstruction.value();
    HANDLE newlyOpenedHandle = nullptr;
    NTSTATUS systemCallResult = 0;

    for (std::wstring_view absolutePathToTry : redirectionInstruction.AbsolutePathsToTry())
    {
        UNICODE_STRING absolutePathToTryUnicodeString = Strings::NtConvertStringViewToUnicodeString(absolutePathToTry);
        
        OBJECT_ATTRIBUTES objectAttributesToTry = *ObjectAttributes;
        objectAttributesToTry.RootDirectory = nullptr;
        objectAttributesToTry.ObjectName = &absolutePathToTryUnicodeString;

        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Trying path \"%.*s\".", GetFunctionName(), requestIdentifier, static_cast<int>(absolutePathToTry.length()), absolutePathToTry.data());
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, &objectAttributesToTry, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

        if (NT_SUCCESS(systemCallResult))
        {
            InsertNewlyOpenedHandleIntoCache(GetFunctionName(), requestIdentifier, newlyOpenedHandle, absolutePathToTry, redirectionInstruction.IsFilesystemOperationRedirected());
            break;
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

    std::optional<FileOperationRedirectInstruction> maybeRedirectionInstruction = RedirectFileOperationByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes);
    if (false == maybeRedirectionInstruction.has_value())
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);

    if (true == Globals::GetConfigurationData().isDryRunMode)
    {
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Continuing with unmodified original path because dry run mode is enabled.", GetFunctionName(), requestIdentifier);
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
    }

    FileOperationRedirectInstruction& redirectionInstruction = maybeRedirectionInstruction.value();
    HANDLE newlyOpenedHandle = nullptr;
    NTSTATUS systemCallResult = 0;

    for (std::wstring_view absolutePathToTry : redirectionInstruction.AbsolutePathsToTry())
    {
        UNICODE_STRING absolutePathToTryUnicodeString = Strings::NtConvertStringViewToUnicodeString(absolutePathToTry);
        
        OBJECT_ATTRIBUTES objectAttributesToTry = *ObjectAttributes;
        objectAttributesToTry.RootDirectory = nullptr;
        objectAttributesToTry.ObjectName = &absolutePathToTryUnicodeString;

        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Trying path \"%.*s\".", GetFunctionName(), requestIdentifier, static_cast<int>(absolutePathToTry.length()), absolutePathToTry.data());
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, &objectAttributesToTry, IoStatusBlock, ShareAccess, OpenOptions);

        if (NT_SUCCESS(systemCallResult))
        {
            InsertNewlyOpenedHandleIntoCache(GetFunctionName(), requestIdentifier, newlyOpenedHandle, absolutePathToTry, redirectionInstruction.IsFilesystemOperationRedirected());
            break;
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

    std::optional<FileOperationRedirectInstruction> maybeRedirectionInstruction = RedirectFileOperationByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes);
    if (false == maybeRedirectionInstruction.has_value())
        return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

    if (true == Globals::GetConfigurationData().isDryRunMode)
    {
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Continuing with unmodified original path because dry run mode is enabled.", GetFunctionName(), requestIdentifier);
        return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
    }

    FileOperationRedirectInstruction& redirectionInstruction = maybeRedirectionInstruction.value();
    NTSTATUS systemCallResult = 0;

    for (std::wstring_view absolutePathToTry : redirectionInstruction.AbsolutePathsToTry())
    {
        UNICODE_STRING absolutePathToTryUnicodeString = Strings::NtConvertStringViewToUnicodeString(absolutePathToTry);

        OBJECT_ATTRIBUTES objectAttributesToTry = *ObjectAttributes;
        objectAttributesToTry.RootDirectory = nullptr;
        objectAttributesToTry.ObjectName = &absolutePathToTryUnicodeString;

        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Trying path \"%.*s\".", GetFunctionName(), requestIdentifier, static_cast<int>(absolutePathToTry.length()), absolutePathToTry.data());
        systemCallResult = Original(&objectAttributesToTry, IoStatusBlock, FileInformation, Length, FileInformationClass);

        if (NT_SUCCESS(systemCallResult))
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

    std::optional<FileOperationRedirectInstruction> maybeRedirectionInstruction = RedirectFileOperationByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes);
    if (false == maybeRedirectionInstruction.has_value())
        return Original(ObjectAttributes, FileInformation);

    if (true == Globals::GetConfigurationData().isDryRunMode)
    {
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Continuing with unmodified original path because dry run mode is enabled.", GetFunctionName(), requestIdentifier);
        return Original(ObjectAttributes, FileInformation);
    }

    FileOperationRedirectInstruction& redirectionInstruction = maybeRedirectionInstruction.value();
    NTSTATUS systemCallResult = 0;

    for (std::wstring_view absolutePathToTry : redirectionInstruction.AbsolutePathsToTry())
    {
        UNICODE_STRING absolutePathToTryUnicodeString = Strings::NtConvertStringViewToUnicodeString(absolutePathToTry);

        OBJECT_ATTRIBUTES objectAttributesToTry = *ObjectAttributes;
        objectAttributesToTry.RootDirectory = nullptr;
        objectAttributesToTry.ObjectName = &absolutePathToTryUnicodeString;

        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Trying path \"%.*s\".", GetFunctionName(), requestIdentifier, static_cast<int>(absolutePathToTry.length()), absolutePathToTry.data());
        systemCallResult = Original(&objectAttributesToTry, FileInformation);

        if (NT_SUCCESS(systemCallResult))
            break;
    }

    return systemCallResult;
}
