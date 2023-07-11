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
#include "Globals.h"
#include "Hooks.h"
#include "Message.h"
#include "MutexWrapper.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <Hookshot/DynamicHook.h>
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
        /// @return Result of the underlying system call to `NtClose` to close the handle.
        inline NTSTATUS RemoveAndCloseHandle(HANDLE handleToRemove)
        {
            std::unique_lock lock(openHandlesMutex);

            NTSTATUS systemCallResult = Hooks::ProtectedDependency::NtClose::SafeInvoke(handleToRemove);
            if (!(NT_SUCCESS(systemCallResult)))
                return systemCallResult;

            const bool removalWasSuccessful = openHandles.erase(handleToRemove);
            DebugAssert(true == removalWasSuccessful, "Failed to remove a handle from the handle cache.");

            return systemCallResult;
        }
    };

    /// Holds all of the information needed to represent a full set of object attributes, as needed by the Nt family of system calls.
    /// This structure places particular emphasis on owning the file name buffer so that it can easily be manipulated.
    struct SNtObjectInfo
    {
        bool wasRedirected;                                                 ///< Indicates whether the object information represents a path that was redirected (if `true`) or is just a copy of the original without a redirection (if `false`).
        TemporaryString objectNameBuffer;                                   ///< Buffer for holding the object name.
        UNICODE_STRING objectName;                                          ///< View structure for representing a Unicode string in the Nt family of system calls.
        OBJECT_ATTRIBUTES objectAttributes;                                 ///< Top-level object attributes structure.

        /// Initialization constructor.
        /// Requires an existing object attributes structure. Copies and transforms the object name to lowercase.
        inline SNtObjectInfo(bool wasRedirected, const OBJECT_ATTRIBUTES& existingObjectAttributes) : wasRedirected(wasRedirected), objectNameBuffer(Strings::NtConvertUnicodeStringToStringView(*(existingObjectAttributes.ObjectName))), objectName(), objectAttributes(existingObjectAttributes)
        {
            objectNameBuffer.ToLowercase();
            objectAttributes.ObjectName = &objectName;
            objectName = objectName = Strings::NtConvertStringViewToUnicodeString(objectNameBuffer.AsStringView());
        }

        /// Initialization constructor.
        /// Requires an existing object attributes structure and a consumable object name, which must already be lowercase.
        inline SNtObjectInfo(bool wasRedirected, const OBJECT_ATTRIBUTES& existingObjectAttributes, TemporaryString&& replacementObjectNameLowercase) : wasRedirected(wasRedirected), objectNameBuffer(std::move(replacementObjectNameLowercase)), objectName(), objectAttributes(existingObjectAttributes)
        {
            objectAttributes.ObjectName = &objectName;
            objectName = Strings::NtConvertStringViewToUnicodeString(objectNameBuffer.AsStringView());
        }

        /// Move constructor.
        inline SNtObjectInfo(SNtObjectInfo&& other) : wasRedirected(std::move(other.wasRedirected)), objectNameBuffer(std::move(other.objectNameBuffer)), objectName(), objectAttributes(std::move(other.objectAttributes))
        {
            objectAttributes.ObjectName = &objectName;
            objectName = Strings::NtConvertStringViewToUnicodeString(objectNameBuffer.AsStringView());
        }
    };


    // -------- INTERNAL VARIABLES ----------------------------------------- //

    /// Cache for all open handles for directories that might at some point become the `RootDirectory` member of an `OBJECT_ATTRIBUTES` structure.
    /// Implemented as a pointer with backing storage on the heap to avoid premature destruction when the program closes.
    static OpenHandleCache* const openHandleCache = new OpenHandleCache;


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Conditionally inserts a newly-opened handle into the open handle cache.
    /// Not all newly-opened handles are "interesting" and need to be cached.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] newlyOpenedHandle Handle to add to the cache.
    /// @param [in] objectInfo Object information structure that describes how the request was presented to the underlying system call, including the path of the file.
    static void InsertNewlyOpenedHandleIntoCache(const wchar_t* functionName, HANDLE newlyOpenedHandle, const SNtObjectInfo& objectInfo)
    {
        std::wstring_view pathForSystemCall = Strings::RemoveTrailing(Strings::NtConvertUnicodeStringToStringView(objectInfo.objectName), L'\\');
        if (true == objectInfo.wasRedirected)
        {
            // If a new handle was opened for a path that has been redirected, it is possible that the handle represents a directory that could be passed as a parameter to one of the directory enumeration system call functions.
            // For this reason, it will be cached in the handle cache. Unconditionally caching is most likely less expensive than checking with the filesystem directly, so this is the approach that will be taken.

            Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Handle %llu is being cached for redirected path \"%.*s\".", functionName, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(newlyOpenedHandle)), static_cast<int>(pathForSystemCall.length()), pathForSystemCall.data());
            openHandleCache->InsertHandle(newlyOpenedHandle, pathForSystemCall);
        }
        else if ((false == objectInfo.wasRedirected) && (true == FilesystemDirector::Singleton().IsPrefixForAnyRule(pathForSystemCall)))
        {
            // If a new handle was opened for a directory path that was not redirected, but exists in the hierarchy towards filesystem rules that could result in redirection, the handle needs to be cached along with the path.
            // In a future call this handle could be specified as the `RootDirectory` handle in an `OBJECT_ATTRIBUTES` structure, which means the resulting combined path might need to be redirected.

            Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Handle %llu is being cached for non-redirected path \"%.*s\".", functionName, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(newlyOpenedHandle)), static_cast<int>(pathForSystemCall.length()), pathForSystemCall.data());
            openHandleCache->InsertHandle(newlyOpenedHandle, pathForSystemCall);
        }
    }


    static HANDLE RedirectDirectoryEnumerationByHandle(const wchar_t* functionName, HANDLE openDirectoryHandle)
    {
        auto maybeDirectoryPath = openHandleCache->GetPathForHandle(openDirectoryHandle);
        if (false == maybeDirectoryPath.has_value())
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s: Invoked with handle %llu which is not cached.", functionName, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(openDirectoryHandle)));
            // TODO: return something useful
            return openDirectoryHandle;
        }

        auto maybeRedirectedDirectoryPath = FilesystemDirector::Singleton().RedirectDirectoryEnumeration(maybeDirectoryPath.value());
        if (false == maybeRedirectedDirectoryPath.has_value())
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s: Invoked with handle %llu for directory at path \"%.*s\" which would not be redirected.", functionName, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(openDirectoryHandle)), static_cast<int>(maybeDirectoryPath.value().length()), maybeDirectoryPath.value().data());
            // TODO: return something useful
            return openDirectoryHandle;
        }

        Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Invoked with handle %llu for directory at path \"%.*s\" which would be redirected to \"%s\".", functionName, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(openDirectoryHandle)), static_cast<int>(maybeDirectoryPath.value().length()), maybeDirectoryPath.value().data(), maybeRedirectedDirectoryPath.value().AsCString());
        // TODO: return something useful
        return openDirectoryHandle;
    }

    /// Redirects an individual filename identified by an object attributes structure.
    /// Used as the main body of several of the hook functions that operate on individual files and directories.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] inputObjectAttributes Object attributes structure received from the application. Used as input for redirection.
    /// @return New object information structure containing everything required to replace the input object attributes structure when invoking the original system call.
    static SNtObjectInfo RedirectSingleFileByObjectAttributes(const wchar_t* functionName, const OBJECT_ATTRIBUTES& inputObjectAttributes)
    {
        std::optional<TemporaryString> maybeRedirectedFilename = std::nullopt;
        std::optional<std::wstring_view> maybeRootDirectoryHandlePath = ((nullptr == inputObjectAttributes.RootDirectory) ? std::nullopt : openHandleCache->GetPathForHandle(inputObjectAttributes.RootDirectory));

        std::wstring_view inputFilename = Strings::NtConvertUnicodeStringToStringView(*(inputObjectAttributes.ObjectName));

        if (true == maybeRootDirectoryHandlePath.has_value())
        {
            // Input object attributes structure specifies an open directory handle as the root directory and the handle was found in the cache.
            // Before querying for redirection it is necessary to assemble the full filename, including the root directory path.

            TemporaryString inputFullFilenameForRedirection;
            inputFullFilenameForRedirection << maybeRootDirectoryHandlePath.value() << L'\\' << inputFilename;
            maybeRedirectedFilename = Pathwinder::FilesystemDirector::Singleton().RedirectSingleFile(inputFullFilenameForRedirection);
        }
        else if (nullptr == inputObjectAttributes.RootDirectory)
        {
            // Input object attributes structure does not specify an open directory handle as the root directory.
            // It is sufficient to send the object name directly for redirection.

            std::wstring_view inputFullFilenameForRedirection = inputFilename;
            maybeRedirectedFilename = Pathwinder::FilesystemDirector::Singleton().RedirectSingleFile(inputFullFilenameForRedirection);
        }
        else
        {
            // Input object attributes structure specifies an open directory handle as the root directory but the handle is not in cache.
            // When the root directory handle was originally opened it was determined that there is no possible match with a filesystem rule.
            // Therefore, it is not necessary to attempt redirection.

            maybeRedirectedFilename = std::nullopt;
        }

        if (true == maybeRedirectedFilename.has_value())
        {
            std::wstring_view redirectedFilename = maybeRedirectedFilename.value().AsStringView();

            if (true == maybeRootDirectoryHandlePath.has_value())
            {
                // A relative root directory was present in the original input object attributes structure, but the complete pathname has been redirected.
                // The redirected pathname is absolute, and the process of redirection invalidates the root directory handle.

                Message::OutputFormatted(Message::ESeverity::Info, L"%s: Invoked with root directory path \"%.*s\" (via handle %llu) and relative path \"%.*s\" which were combined and redirected to \"%.*s\".", functionName, static_cast<int>(maybeRootDirectoryHandlePath.value().length()), maybeRootDirectoryHandlePath.value().data(), static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(inputObjectAttributes.RootDirectory)), static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectedFilename.length()), redirectedFilename.data());

                if (false == Globals::GetConfigurationData().isDryRunMode)
                {
                    SNtObjectInfo redirectedObjectInfo(true, inputObjectAttributes, std::move(maybeRedirectedFilename.value()));
                    redirectedObjectInfo.objectAttributes.RootDirectory = nullptr;

                    return redirectedObjectInfo;
                }
                else
                {
                    return SNtObjectInfo(true, inputObjectAttributes);
                }
            }
            else
            {
                // A relative root directory was not in the original input object attributes structure and a path redirection took place.
                // This is the simplest case. All that needs to happen is a new object attributes structure be created with its object name set to the redirected path.

                Message::OutputFormatted(Message::ESeverity::Info, L"%s: Invoked with path \"%.*s\" which was redirected to \"%.*s\".", functionName, static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectedFilename.length()), redirectedFilename.data());

                if (false == Globals::GetConfigurationData().isDryRunMode)
                    return SNtObjectInfo(true, inputObjectAttributes, std::move(maybeRedirectedFilename.value()));
                else
                    return SNtObjectInfo(true, inputObjectAttributes);
            }
        }
        else
        {
            // No redirection took place.
            // If a root directory handle was present in the input object attributes structure, it was not found in cache and therefore redirection was skipped.
            // Otherwise, the complete input object name was queried for redirection, but a matching filesystem rule was not found.

            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s: Invoked with path \"%.*s\" which was not redirected.", functionName, static_cast<int>(inputFilename.length()), inputFilename.data());
            return SNtObjectInfo(false, inputObjectAttributes);
        }
    }
}


// -------- PROTECTED HOOK FUNCTIONS --------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtClose::Hook(HANDLE Handle)
{
    using namespace Pathwinder;


    auto maybeClosedHandlePath = openHandleCache->GetPathForHandle(Handle);
    if (false == maybeClosedHandlePath.has_value())
        return Original(Handle);

    if (true == Message::WillOutputMessageOfSeverity(Message::ESeverity::Debug))
    {
        // This branch is just for logging and does a non-trivial amount of extra work.

        TemporaryString closedHandlePath = maybeClosedHandlePath.value();
        NTSTATUS closeResult = openHandleCache->RemoveAndCloseHandle(Handle);

        if (NT_SUCCESS(closeResult))
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s: Handle %llu for path \"%s\" was closed and removed from the cache.", GetFunctionName(), static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(Handle)), closedHandlePath.AsCString());

        return closeResult;
    }
    else
    {
        return openHandleCache->RemoveAndCloseHandle(Handle);
    }
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtCreateFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    using namespace Pathwinder;


    SNtObjectInfo objectInfo = RedirectSingleFileByObjectAttributes(GetFunctionName(), *ObjectAttributes);
    HANDLE newlyOpenedHandle = nullptr;

    NTSTATUS systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, &objectInfo.objectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

    if (NT_SUCCESS(systemCallResult))
        InsertNewlyOpenedHandleIntoCache(GetFunctionName(), newlyOpenedHandle, objectInfo);

    *FileHandle = newlyOpenedHandle;
    return systemCallResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtOpenFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    using namespace Pathwinder;


    SNtObjectInfo objectInfo = RedirectSingleFileByObjectAttributes(GetFunctionName(), *ObjectAttributes);
    HANDLE newlyOpenedHandle = nullptr;

    NTSTATUS systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, &objectInfo.objectAttributes, IoStatusBlock, ShareAccess, OpenOptions);

    if (NT_SUCCESS(systemCallResult))
        InsertNewlyOpenedHandleIntoCache(GetFunctionName(), newlyOpenedHandle, objectInfo);

    *FileHandle = newlyOpenedHandle;
    return systemCallResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFile::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName, BOOLEAN RestartScan)
{
    using namespace Pathwinder;


    HANDLE directoryHandleForSystemCall = RedirectDirectoryEnumerationByHandle(GetFunctionName(), FileHandle);
    return Original(directoryHandleForSystemCall, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, ReturnSingleEntry, FileName, RestartScan);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFileEx::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, ULONG QueryFlags, PUNICODE_STRING FileName)
{
    using namespace Pathwinder;


    HANDLE directoryHandleForSystemCall = RedirectDirectoryEnumerationByHandle(GetFunctionName(), FileHandle);
    return Original(directoryHandleForSystemCall, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, QueryFlags, FileName);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationByName::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    using namespace Pathwinder;


    SNtObjectInfo objectInfo = RedirectSingleFileByObjectAttributes(GetFunctionName(), *ObjectAttributes);
    return Original(&objectInfo.objectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
}


// -------- UNPROTECTED HOOK FUNCTIONS ------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryAttributesFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFO FileInformation)
{
    using namespace Pathwinder;


    SNtObjectInfo objectInfo = RedirectSingleFileByObjectAttributes(GetFunctionName(), *ObjectAttributes);
    return Original(&objectInfo.objectAttributes, FileInformation);
}
