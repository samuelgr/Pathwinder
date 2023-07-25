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
#include <array>
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
        // -------- CLASS METHODS ------------------------------------------ //

        /// Retrieves a reference to the singleton instance of this object.
        /// It holds all open handles for directories that might at some point become the `RootDirectory` member of an `OBJECT_ATTRIBUTES` structure or the subject of a directory enumeration query.
        static inline OpenHandleStore& Singleton(void)
        {
            static OpenHandleStore* const openHandleCache = new OpenHandleStore;
            return *openHandleCache;
        }


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
    struct SFileOperationContext
    {
        FileOperationRedirectInstruction instruction;                       ///< How the redirection should be performed.
        std::optional<TemporaryString> composedInputPath;                   ///< If an input path was composed, for example due to combination with a root directory, then that input path is stored here.
    };

    /// Contains all of the information needed to represent a file name and attributes in the format needed to interact with underlying system calls.
    struct SObjectNameAndAttributes
    {
        UNICODE_STRING objectName;                                          ///< Name of the object, as a Unicode string in the format supported by system calls.
        OBJECT_ATTRIBUTES objectAttributes;                                 ///< Attributes of the object, which includes a field that points to the name.
    };

    /// Contains pointers to all of the object attributes to try, in order, for a file operation.
    /// Any values that are `nullptr` are skipped.
    typedef std::array<POBJECT_ATTRIBUTES, 2> TFileOperationObjectAttributesToTry;


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Executes any pre-operations needed ahead of invoking underlying system calls.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation, including identifying any pre-operations needed.
    /// @return Result of executing all of the pre-operations. The code will indicate success if they all succeed or a failure that corresponds to the first applicable operation failure.
    static NTSTATUS ExecuteExtraPreOperations(const wchar_t* functionName, unsigned int functionRequestIdentifier, const FileOperationRedirectInstruction& instruction)
    {
        if (instruction.GetExtraPreOperations().contains(static_cast<int>(FileOperationRedirectInstruction::EExtraPreOperation::EnsurePathHierarchyExists)))
        {
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Ensuring directory hierarchy exists for \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(instruction.GetExtraPreOperationOperand().length()), instruction.GetExtraPreOperationOperand().data());
            // TODO
        }

        return 0;
    }

    /// Converts a `CreateDisposition` parameter, which system calls use to identify filesystem behavior regarding creating new files or opening existing files, into an appropriate file operation mode.
    /// @param [in] ntCreateDisposition `CreateDisposition` parameter received from the application.
    /// @return Corresponding file operation mode enumerator.
    static FilesystemDirector::EFileOperationMode FileOperationModeFromCreateDisposition(ULONG ntCreateDisposition)
    {
        switch (ntCreateDisposition)
        {
        case FILE_CREATE:
            return FilesystemDirector::EFileOperationMode::CreateNewFile;

        case FILE_SUPERSEDE:
        case FILE_OPEN_IF:
        case FILE_OVERWRITE_IF:
            return FilesystemDirector::EFileOperationMode::CreateNewOrOpenExistingFile;

        case FILE_OPEN:
        case FILE_OVERWRITE:
            return FilesystemDirector::EFileOperationMode::OpenExistingFile;

        default:
            return FilesystemDirector::EFileOperationMode::OpenExistingFile;
        }
    }

    /// Fills the supplied object name and attributes structure with the name and attributes needed to represent the redirected filename from a file operation redirection instruction.
    /// Does nothing if the file operation redirection instruction does not specify any redirection.
    /// This must be done in place because the `OBJECT_ATTRIBUTES` structure refers to its `ObjectName` field by pointer. Returning by value would invalidate the address of the `ObjectName` field and therefore not work.
    /// @param [out] redirectedObjectNameAndAttributes Mutable reference to the structure to be filled.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] unredirectedObjectAttributes Object attributes structure received from the application.
    static void FillRedirectedObjectNameAndAttributesForInstruction(SObjectNameAndAttributes& redirectedObjectNameAndAttributes, const FileOperationRedirectInstruction& instruction, const OBJECT_ATTRIBUTES& unredirectedObjectAttributes)
    {
        if (true == instruction.HasRedirectedFilename())
        {
            redirectedObjectNameAndAttributes.objectName = Strings::NtConvertStringViewToUnicodeString(instruction.GetRedirectedFilename());

            redirectedObjectNameAndAttributes.objectAttributes = unredirectedObjectAttributes;
            redirectedObjectNameAndAttributes.objectAttributes.RootDirectory = nullptr;
            redirectedObjectNameAndAttributes.objectAttributes.ObjectName = &redirectedObjectNameAndAttributes.objectName;
        }
    }

    /// Retrieves an identifier for a particular invocation of a hook function.
    /// Used exclusively for logging.
    /// @return Numeric identifier for an invocation.
    static inline unsigned int GetRequestIdentifier(void)
    {
        static std::atomic<unsigned int> nextRequestIdentifier;
        return nextRequestIdentifier++;
    }

    /// Determines how to redirect an individual file operation in which the affected file is identified by an object attributes structure.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] inputObjectAttributes Object attributes structure received from the application. Used as input for redirection.
    /// @param [in] fileOperationMode Type of file operation requested by the application.
    /// @return Context that contains all of the information needed to submit the file operation to the underlying system call.
    static SFileOperationContext GetFileOperationRedirectionContextByObjectAttributes(const wchar_t* functionName, unsigned int functionRequestIdentifier, const OBJECT_ATTRIBUTES& inputObjectAttributes, FilesystemDirector::EFileOperationMode fileOperationMode)
    {
        std::optional<TemporaryString> maybeRedirectedFilename = std::nullopt;
        std::optional<std::wstring_view> maybeRootDirectoryHandlePath = ((nullptr == inputObjectAttributes.RootDirectory) ? std::nullopt : OpenHandleStore::Singleton().GetPathForHandle(inputObjectAttributes.RootDirectory));

        std::wstring_view inputFilename = Strings::NtConvertUnicodeStringToStringView(*(inputObjectAttributes.ObjectName));

        if (true == maybeRootDirectoryHandlePath.has_value())
        {
            // Input object attributes structure specifies an open directory handle as the root directory and the handle was found in the cache.
            // Before querying for redirection it is necessary to assemble the full filename, including the root directory path.

            TemporaryString inputFullFilename;
            inputFullFilename << maybeRootDirectoryHandlePath.value() << L'\\' << inputFilename;

            FileOperationRedirectInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().RedirectFileOperation(inputFullFilename, fileOperationMode);
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

            FileOperationRedirectInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().RedirectFileOperation(inputFilename, fileOperationMode);

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

    /// Places pointers to the object attributes structures to try, in order, to submit to the underlying system call for a file operation.
    /// The number of pointers placed, and the order in which they are placed, is controlled by the file operation redirection instruction.
    /// Any entries placed as `nullptr` are invalid and should be skipped.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] unredirectedObjectAttributes Pointer to the object attributes structure received from the application.
    /// @param [in] redirectedObjectAttributes Pointer to the object attributes structure generated by querying for file operation redirection.
    /// @return Object attributes to be tried, in order.
    static TFileOperationObjectAttributesToTry GetFileOperationObjectAttributesToTry(const FileOperationRedirectInstruction& instruction, POBJECT_ATTRIBUTES unredirectedObjectAttributes, POBJECT_ATTRIBUTES redirectedObjectAttributes)
    {
        switch (instruction.GetFilenamesToTry())
        {
        case FileOperationRedirectInstruction::ETryFiles::UnredirectedOnly:
            return {unredirectedObjectAttributes, nullptr};

        case FileOperationRedirectInstruction::ETryFiles::UnredirectedFirst:
            return {unredirectedObjectAttributes, redirectedObjectAttributes};

        case FileOperationRedirectInstruction::ETryFiles::RedirectedFirst:
            return {redirectedObjectAttributes, unredirectedObjectAttributes};

        case FileOperationRedirectInstruction::ETryFiles::RedirectedOnly:
            return {redirectedObjectAttributes};

        default:
            // This should never be executed because it indicates the enumerator is invalid.
            return {nullptr, nullptr};
        }
    }

    /// Determines if the next possible filename should be tried or if the existing system call result should be returned to the application.
    /// @param [in] systemCallResult Result of the system call for the present attempt.
    /// @return `true` if the result indicates that the next filename should be tried, `false` if the result indicates to stop trying and move on.
    static bool ShouldTryNextFilename(NTSTATUS systemCallResult)
    {
        // This check is simply based on whether or not the call was successful.
        // If not, advance to the next file, otherwise stop.
        return (!(NT_SUCCESS(systemCallResult)));
    }

    /// Inserts a newly-opened handle into the open handle store.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] newlyOpenedHandle Handle to add to the open handles store.
    /// @param [in] associatedPath Path associated with the newly opened handle being added to storage.
    static void StoreNewlyOpenedHandle(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE newlyOpenedHandle, std::wstring_view associatedPath)
    {
        std::wstring_view associatedPathTrimmed = Strings::RemoveTrailing(associatedPath, L'\\');
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu is being stored in association with path \"%.*s\".", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(newlyOpenedHandle), static_cast<int>(associatedPathTrimmed.length()), associatedPathTrimmed.data());
        OpenHandleStore::Singleton().InsertHandle(newlyOpenedHandle, associatedPathTrimmed);
    }
}


// -------- PROTECTED HOOK FUNCTIONS --------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtClose::Hook(HANDLE Handle)
{
    using namespace Pathwinder;


    auto maybeClosedHandlePath = OpenHandleStore::Singleton().GetPathForHandle(Handle);
    if (false == maybeClosedHandlePath.has_value())
        return Original(Handle);

    const unsigned int requestIdentifier = GetRequestIdentifier();

    std::wstring closedHandlePath;
    NTSTATUS closeHandleResult = OpenHandleStore::Singleton().RemoveAndCloseHandle(Handle, &closedHandlePath);

    if (NT_SUCCESS(closeHandleResult))
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %llu for path \"%s\" was closed and erased from storage.", GetFunctionName(), requestIdentifier, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(Handle)), closedHandlePath.c_str());

    return closeHandleResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtCreateFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionContextByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes, FileOperationModeFromCreateDisposition(CreateDisposition));
    const FileOperationRedirectInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    std::wstring_view unredirectedPath = ((true == operationContext.composedInputPath.has_value()) ? operationContext.composedInputPath.value().AsStringView() : Strings::NtConvertUnicodeStringToStringView(*ObjectAttributes->ObjectName));
    std::wstring_view redirectedPath = ((true == redirectionInstruction.HasRedirectedFilename()) ? redirectionInstruction.GetRedirectedFilename() : L"");

    HANDLE newlyOpenedHandle = nullptr;
    NTSTATUS systemCallResult = 0;
    std::wstring_view lastAttemptedPath = L"";

    for (POBJECT_ATTRIBUTES objectAttributesToTry : GetFileOperationObjectAttributesToTry(redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
    {
        if (nullptr == objectAttributesToTry)
            continue;

        lastAttemptedPath = Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, objectAttributesToTry, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
    {
        Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationRedirectInstruction::ETryFiles = %u).", GetFunctionName(), requestIdentifier, static_cast<unsigned int>(redirectionInstruction.GetFilenamesToTry()));
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
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


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionContextByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes, FilesystemDirector::EFileOperationMode::OpenExistingFile);
    const FileOperationRedirectInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    std::wstring_view unredirectedPath = ((true == operationContext.composedInputPath.has_value()) ? operationContext.composedInputPath.value().AsStringView() : Strings::NtConvertUnicodeStringToStringView(*ObjectAttributes->ObjectName));
    std::wstring_view redirectedPath = ((true == redirectionInstruction.HasRedirectedFilename()) ? redirectionInstruction.GetRedirectedFilename() : L"");

    HANDLE newlyOpenedHandle = nullptr;
    NTSTATUS systemCallResult = 0;
    std::wstring_view lastAttemptedPath;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : GetFileOperationObjectAttributesToTry(redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
    {
        if (nullptr == objectAttributesToTry)
            continue;

        lastAttemptedPath = Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
        systemCallResult = Original(&newlyOpenedHandle, DesiredAccess, objectAttributesToTry, IoStatusBlock, ShareAccess, OpenOptions);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
    {
        Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationRedirectInstruction::ETryFiles = %u).", GetFunctionName(), requestIdentifier, static_cast<unsigned int>(redirectionInstruction.GetFilenamesToTry()));
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
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


    const unsigned int requestIdentifier = GetRequestIdentifier();

    return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, ReturnSingleEntry, FileName, RestartScan);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryDirectoryFileEx::Hook(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, ULONG QueryFlags, PUNICODE_STRING FileName)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    return Original(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass, QueryFlags, FileName);
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryInformationByName::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionContextByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes, FilesystemDirector::EFileOperationMode::OpenExistingFile);
    const FileOperationRedirectInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    std::wstring_view unredirectedPath = ((true == operationContext.composedInputPath.has_value()) ? operationContext.composedInputPath.value().AsStringView() : Strings::NtConvertUnicodeStringToStringView(*ObjectAttributes->ObjectName));
    std::wstring_view redirectedPath = ((true == redirectionInstruction.HasRedirectedFilename()) ? redirectionInstruction.GetRedirectedFilename() : L"");
    std::wstring_view lastAttemptedPath = L"";

    NTSTATUS systemCallResult = 0;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : GetFileOperationObjectAttributesToTry(redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
    {
        if (nullptr == objectAttributesToTry)
            continue;

        lastAttemptedPath = Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
        systemCallResult = Original(objectAttributesToTry, IoStatusBlock, FileInformation, Length, FileInformationClass);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
    {
        Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationRedirectInstruction::ETryFiles = %u).", GetFunctionName(), requestIdentifier, static_cast<unsigned int>(redirectionInstruction.GetFilenamesToTry()));
        return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);
    }

    return systemCallResult;
}


// -------- UNPROTECTED HOOK FUNCTIONS ------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryAttributesFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFO FileInformation)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionContextByObjectAttributes(GetFunctionName(), requestIdentifier, *ObjectAttributes, FilesystemDirector::EFileOperationMode::OpenExistingFile);
    const FileOperationRedirectInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(ObjectAttributes, FileInformation);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    std::wstring_view unredirectedPath = ((true == operationContext.composedInputPath.has_value()) ? operationContext.composedInputPath.value().AsStringView() : Strings::NtConvertUnicodeStringToStringView(*ObjectAttributes->ObjectName));
    std::wstring_view redirectedPath = ((true == redirectionInstruction.HasRedirectedFilename()) ? redirectionInstruction.GetRedirectedFilename() : L"");
    std::wstring_view lastAttemptedPath = L"";

    NTSTATUS systemCallResult = 0;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : GetFileOperationObjectAttributesToTry(redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
    {
        if (nullptr == objectAttributesToTry)
            continue;

        lastAttemptedPath = Strings::NtConvertUnicodeStringToStringView(*(objectAttributesToTry->ObjectName));
        systemCallResult = Original(objectAttributesToTry, FileInformation);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
    {
        Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationRedirectInstruction::ETryFiles = %u).", GetFunctionName(), requestIdentifier, static_cast<unsigned int>(redirectionInstruction.GetFilenamesToTry()));
        return Original(ObjectAttributes, FileInformation);
    }

    return systemCallResult;
}
