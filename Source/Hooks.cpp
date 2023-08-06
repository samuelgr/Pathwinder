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
#include "FilesystemOperations.h"
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
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Read-only view of data stored about an open handle.
        struct SHandleDataView
        {
            std::wstring_view associatedPath;                               ///< Path associated internally with the open handle.
            std::wstring_view realOpenedPath;                               ///< Actual path that was opened for the handle. This could be different from the associated path based on instructions from a filesystem director.
        };

        /// Data stored about an open handle.
        struct SHandleData
        {
            std::wstring associatedPath;                                    ///< Path associated internally with the open handle.
            std::wstring realOpenedPath;                                    ///< Actual path that was opened for the handle. This could be different from the associated path based on instructions from a filesystem director.

            /// Default constructor.
            inline SHandleData(void) = default;

            /// Initialization constructor.
            /// Copy-converts from a read-only view structure.
            explicit inline SHandleData(const SHandleDataView& dataView) : associatedPath(dataView.associatedPath), realOpenedPath(dataView.realOpenedPath)
            {
                // Nothing to do here.
            }

            /// Move constructor.
            SHandleData(SHandleData&& other) = default;

            /// Move assignment operator.
            inline SHandleData& operator=(SHandleData&& other) = default;

            /// Implicit conversion to a read-only view.
            inline operator SHandleDataView(void) const
            {
                return {
                    .associatedPath = associatedPath,
                    .realOpenedPath = realOpenedPath
                };
            }
        };


    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Open handle data structure itself.
        /// Maps from a handle to the filesystem path that was used to open it.
        std::unordered_map<HANDLE, SHandleData> openHandles;

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

        /// Queries the open handle store for the specified handle and retrieves a read-only view of the associated data, if the handle is found in the store.
        /// @param [in] handleToQuery Handle for which to query.
        /// @return Read-only view of the data associated with the handle, if the handle exists in the store.
        inline std::optional<SHandleDataView> GetDataForHandle(HANDLE handleToQuery)
        {
            std::shared_lock lock(openHandlesMutex);

            auto openHandleIter = openHandles.find(handleToQuery);
            if (openHandleIter == openHandles.cend())
                return std::nullopt;

            return openHandleIter->second;
        }

        /// Inserts a new handle and corresponding path into the open handle store.
        /// This version moves handle data into the open handle store.
        /// @param [in] handleToInsert Handle to be inserted.
        /// @param [in] handleData Corresponding data to store for the handle.
        inline void InsertHandle(HANDLE handleToInsert, SHandleData&& handleData)
        {
            std::unique_lock lock(openHandlesMutex);

            const bool insertionWasSuccessful = openHandles.emplace(handleToInsert, std::move(handleData)).second;
            DebugAssert(true == insertionWasSuccessful, "Failed to insert a handle into storage.");
        }

        /// Inserts a new handle and corresponding path into the open handle store.
        /// This version accepts a read-only view to be copied on insert.
        /// @param [in] handleToInsert Handle to be inserted.
        /// @param [in] handleData Corresponding data to store for the handle.
        inline void InsertHandle(HANDLE handleToInsert, SHandleDataView handleData)
        {
            InsertHandle(handleToInsert, SHandleData(handleData));
        }

        /// Inserts a new handle and corresponding path into the open handle store or, if the handle already exists, updates its stored data.
        /// This version moves handle data into the open handle store.
        /// @param [in] handleToInsertOrUpdate Handle to be inserted or updated.
        /// @param [in] handleData Corresponding data to store for the handle.
        inline void InsertOrUpdateHandle(HANDLE handleToInsertOrUpdate, SHandleData&& handleData)
        {
            std::unique_lock lock(openHandlesMutex);

            openHandles.insert_or_assign(handleToInsertOrUpdate, std::move(handleData)).second;
        }

        /// Inserts a new handle and corresponding path into the open handle store or, if the handle already exists, updates its stored data.
        /// This version accepts a read-only view to be copied on assignment.
        /// @param [in] handleToInsertOrUpdate Handle to be inserted or updated.
        /// @param [in] handleData Corresponding data to store for the handle.
        inline void InsertOrUpdateHandle(HANDLE handleToInsertOrUpdate, SHandleDataView handleData)
        {
            InsertOrUpdateHandle(handleToInsertOrUpdate, SHandleData(handleData));
        }

        /// Attempts to remove an existing handle and corresponding path from the open handle store.
        /// @param [in] handleToRemove Handle to be removed.
        /// @param [out] handleData Handle data object to receive ownership of the corresponding data for the handle that was removed, if not null. Only filled if this method returns `true`.
        /// @return `true` if the handle was found and removed, `false` otherwise.
        inline bool RemoveHandle(HANDLE handleToRemove, SHandleData* handleData)
        {
            std::unique_lock lock(openHandlesMutex);

            auto removalIter = openHandles.find(handleToRemove);
            if (openHandles.end() == removalIter)
                return false;

            if (nullptr == handleData)
                openHandles.erase(removalIter);
            else
                *handleData = std::move(openHandles.extract(removalIter).mapped());

            return true;
        }

        /// Attempts to close and subsequently remove an existing handle and corresponding path from the open handle store.
        /// Both handle closure and removal need to be done while the lock is held, to ensure proper concurrency control.
        /// This avoids a race condition in which a closed handle is reused and re-added to the store before the closing thread has a chance to remove it first.
        /// @param [in] handleToRemove Handle to be removed.
        /// @param [out] handleData Handle data object to receive ownership of the corresponding data for the handle that was removed, if not null. Only filled if the underlying system call to close the handle succeeds.
        /// @return Result of the underlying system call to `NtClose` to close the handle.
        inline NTSTATUS RemoveAndCloseHandle(HANDLE handleToRemove, SHandleData* handleData)
        {
            std::unique_lock lock(openHandlesMutex);

            auto removalIter = openHandles.find(handleToRemove);
            DebugAssert(openHandles.end() != removalIter, "Attempting to close and erase a handle that was not previously stored.");

            NTSTATUS systemCallResult = Hooks::ProtectedDependency::NtClose::SafeInvoke(handleToRemove);
            if (!(NT_SUCCESS(systemCallResult)))
                return systemCallResult;

            if (nullptr == handleData)
                openHandles.erase(removalIter);
            else
                *handleData = std::move(openHandles.extract(removalIter).mapped());

            return systemCallResult;
        }
    };

    /// Holds file rename information, including the variably-sized filename, in a bytewise buffer and exposes it in a type-safe way.
    /// This object is used for representation only, not for any functionality surrounding actually creating a byte-wise buffer representation of a file rename information structure.
    class FileRenameInformationAndFilename
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Byte-wise buffer, which will hold the file rename information data structure.
        TemporaryVector<uint8_t> bytewiseBuffer;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor. Requires an already-constructed bytewise buffer, which is moved into place.
        inline FileRenameInformationAndFilename(TemporaryVector<uint8_t>&& bytewiseBuffer) : bytewiseBuffer(std::move(bytewiseBuffer))
        {
            // Nothing to do here.
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Convenience method for retrieving a properly-typed file rename information structure.
        inline SFileRenameInformation& GetFileRenameInformation(void)
        {
            return *(reinterpret_cast<SFileRenameInformation*>(bytewiseBuffer.Data()));
        }

        /// Convenience method for retrieving the size, in bytes, of the stored file rename information structure including filename.
        inline unsigned int GetFileRenameInformationSizeBytes(void)
        {
            return bytewiseBuffer.Size();
        }
    };

    /// Contains all of the information associated with a file operation.
    struct SFileOperationContext
    {
        FileOperationInstruction instruction;                       ///< How the redirection should be performed.
        std::optional<TemporaryString> composedInputPath;                   ///< If an input path was composed, for example due to combination with a root directory, then that input path is stored here.
    };

    /// Contains all of the information needed to represent a file name and attributes in the format needed to interact with underlying system calls.
    struct SObjectNameAndAttributes
    {
        UNICODE_STRING objectName;                                          ///< Name of the object, as a Unicode string in the format supported by system calls.
        OBJECT_ATTRIBUTES objectAttributes;                                 ///< Attributes of the object, which includes a field that points to the name.
    };


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Copies the contents of the supplied file rename information structure but replaces the filename.
    /// This has the effect of changing the new name of the specified file after the rename operation completes.
    /// @param [in] inputFileRenameInformation Original file rename information structurewhose contents are to be copied.
    /// @param [in] replacementFilename Replacement filename.
    /// @return Buffer containing a new file rename information structure with the filename replaced.
    static FileRenameInformationAndFilename CopyFileRenameInformationAndReplaceFilename(const SFileRenameInformation& inputFileRenameInformation, std::wstring_view replacementFilename)
    {
        TemporaryVector<uint8_t> newFileRenameInformation;

        SFileRenameInformation outputFileRenameInformation = inputFileRenameInformation;
        outputFileRenameInformation.fileNameLength = (static_cast<ULONG>(replacementFilename.length()) * sizeof(wchar_t));

        for (size_t i = 0; i < offsetof(SFileRenameInformation, fileName); ++i)
            newFileRenameInformation.PushBack((reinterpret_cast<const uint8_t*>(&outputFileRenameInformation))[i]);

        for (size_t i = 0; i < (replacementFilename.length() * sizeof(wchar_t)); ++i)
            newFileRenameInformation.PushBack((reinterpret_cast<const uint8_t*>(replacementFilename.data()))[i]);

        return FileRenameInformationAndFilename(std::move(newFileRenameInformation));
    }

    /// Executes any pre-operations needed ahead of invoking underlying system calls.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation, including identifying any pre-operations needed.
    /// @return Result of executing the pre-operations. The code will indicate success if they all succeed or a failure that corresponds to the first applicable pre-operation failure.
    static NTSTATUS ExecuteExtraPreOperations(const wchar_t* functionName, unsigned int functionRequestIdentifier, const FileOperationInstruction& instruction)
    {
        NTSTATUS extraPreOperationResult = NtStatus::kSuccess;

        if (instruction.GetExtraPreOperations().contains(static_cast<int>(FileOperationInstruction::EExtraPreOperation::EnsurePathHierarchyExists)) && (NT_SUCCESS(extraPreOperationResult)))
        {
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Ensuring directory hierarchy exists for \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(instruction.GetExtraPreOperationOperand().length()), instruction.GetExtraPreOperationOperand().data());
            extraPreOperationResult = static_cast<NTSTATUS>(FilesystemOperations::CreateDirectoryHierarchy(instruction.GetExtraPreOperationOperand()));
        }

        if (!(NT_SUCCESS(extraPreOperationResult)))
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): A required pre-operation failed (NTSTATUS = 0x%08x).", functionName, functionRequestIdentifier, static_cast<unsigned int>(extraPreOperationResult));

        return extraPreOperationResult;
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
    static void FillRedirectedObjectNameAndAttributesForInstruction(SObjectNameAndAttributes& redirectedObjectNameAndAttributes, const FileOperationInstruction& instruction, const OBJECT_ATTRIBUTES& unredirectedObjectAttributes)
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
    /// @param [in] rootDirectory Open handle for the root directory that contains the input filename. May be `nullptr`, in which case the input filename must be a full and absolute path. Supplied by an application that invokes a system call.
    /// @param [in] inputFilename Filename received from the application that invoked the system call. Must be a full and absolute path if the root directory handle is not provided.
    /// @param [in] fileOperationMode Type of file operation requested by the application.
    /// @return Context that contains all of the information needed to submit the file operation to the underlying system call.
    static SFileOperationContext GetFileOperationRedirectionInformation(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE rootDirectory, std::wstring_view inputFilename, FilesystemDirector::EFileOperationMode fileOperationMode)
    {
        std::optional<TemporaryString> maybeRedirectedFilename = std::nullopt;
        std::optional<OpenHandleStore::SHandleDataView> maybeRootDirectoryHandleData = ((nullptr == rootDirectory) ? std::nullopt : OpenHandleStore::Singleton().GetDataForHandle(rootDirectory));

        if (true == maybeRootDirectoryHandleData.has_value())
        {
            // Input object attributes structure specifies an open directory handle as the root directory and the handle was found in the cache.
            // Before querying for redirection it is necessary to assemble the full filename, including the root directory path.

            std::wstring_view rootDirectoryHandlePath = maybeRootDirectoryHandleData.value().associatedPath;

            TemporaryString inputFullFilename;
            inputFullFilename << rootDirectoryHandlePath << L'\\' << inputFilename;

            FileOperationInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().GetInstructionForFileOperation(inputFullFilename, fileOperationMode);
            if (true == redirectionInstruction.HasRedirectedFilename())
                Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined and redirected to \"%.*s\".", functionName, functionRequestIdentifier, static_cast<int>(rootDirectoryHandlePath.length()), rootDirectoryHandlePath.data(), reinterpret_cast<size_t>(rootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data(), static_cast<int>(redirectionInstruction.GetRedirectedFilename().length()), redirectionInstruction.GetRedirectedFilename().data());
            else
                Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with root directory path \"%.*s\" (via handle %zu) and relative path \"%.*s\" which were combined but not redirected.", functionName, functionRequestIdentifier, static_cast<int>(rootDirectoryHandlePath.length()), rootDirectoryHandlePath.data(), reinterpret_cast<size_t>(rootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data());

            return {.instruction = std::move(redirectionInstruction), .composedInputPath = std::move(inputFullFilename)};
        }
        else if (nullptr == rootDirectory)
        {
            // Input object attributes structure does not specify an open directory handle as the root directory.
            // It is sufficient to send the object name directly for redirection.

            FileOperationInstruction redirectionInstruction = Pathwinder::FilesystemDirector::Singleton().GetInstructionForFileOperation(inputFilename, fileOperationMode);

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

            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): Invoked with root directory handle %zu and relative path \"%.*s\" for which no redirection was attempted.", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(rootDirectory), static_cast<int>(inputFilename.length()), inputFilename.data());
            return {.instruction = FileOperationInstruction::NoRedirectionOrInterception(), .composedInputPath = std::nullopt};
        }
    }

    /// Places pointers to the data structures that identify files to try, in order, to submit to the underlying system call for a file operation.
    /// The number of pointers placed, and the order in which they are placed, is controlled by the file operation redirection instruction.
    /// Any entries placed as `nullptr` are invalid and should be skipped.
    /// @tparam FileObjectType Data structure type that identifies files to try.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] unredirectedFileObject Pointer to the data structure received from the application.
    /// @param [in] redirectedFileObject Pointer to the data structure generated by querying for file operation redirection.
    /// @return Object attributes to be tried, in order.
    template <typename FileObjectType> std::array<FileObjectType*, 2> SelectFilesToTry(const wchar_t* functionName, unsigned int functionRequestIdentifier, const FileOperationInstruction& instruction, FileObjectType* unredirectedFileObject, FileObjectType* redirectedFileObject)
    {
        switch (instruction.GetFilenamesToTry())
        {
        case FileOperationInstruction::ETryFiles::UnredirectedOnly:
            return {unredirectedFileObject, nullptr};

        case FileOperationInstruction::ETryFiles::UnredirectedFirst:
            return {unredirectedFileObject, redirectedFileObject};

        case FileOperationInstruction::ETryFiles::RedirectedFirst:
            return {redirectedFileObject, unredirectedFileObject};

        case FileOperationInstruction::ETryFiles::RedirectedOnly:
            return {redirectedFileObject};

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationInstruction::ETryFiles = %u).", functionName, functionRequestIdentifier, static_cast<unsigned int>(instruction.GetFilenamesToTry()));
            return {nullptr, nullptr};
        }
    }

    /// Determines if the next possible filename should be tried or if the existing system call result should be returned to the application.
    /// @param [in] systemCallResult Result of the system call for the present attempt.
    /// @return `true` if the result indicates that the next filename should be tried, `false` if the result indicates to stop trying and move on.
    static inline bool ShouldTryNextFilename(NTSTATUS systemCallResult)
    {
        // This check is simply based on whether or not the call was successful.
        // If not, advance to the next file, otherwise stop.
        return (!(NT_SUCCESS(systemCallResult)));
    }

    /// Inserts a newly-opened handle into the open handle store, selecting an associated path based on the file operation redirection instruction.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] newlyOpenedHandle Handle to add to the open handles store.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] successfulPath Path that was used successfully to create the file handle.
    /// @param [in] unredirectedPath Original file name supplied by the application.
    static void SelectFilenameAndStoreNewlyOpenedHandle(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE newlyOpenedHandle, const FileOperationInstruction& instruction, std::wstring_view successfulPath, std::wstring_view unredirectedPath)
    {
        std::wstring_view selectedPath;

        switch (instruction.GetFilenameHandleAssociation())
        {
        case FileOperationInstruction::EAssociateNameWithHandle::None:
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::WhicheverWasSuccessful:
            selectedPath = successfulPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Unredirected:
            selectedPath = unredirectedPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Redirected:
            selectedPath = instruction.GetRedirectedFilename();
            break;

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationInstruction::EAssociateNameWithHandle = %u).", functionName, functionRequestIdentifier, static_cast<unsigned int>(instruction.GetFilenameHandleAssociation()));
            break;
        }

        if (false == selectedPath.empty())
        {
            successfulPath = Strings::RemoveTrailing(successfulPath, L'\\');
            selectedPath = Strings::RemoveTrailing(selectedPath, L'\\');

            OpenHandleStore::Singleton().InsertHandle(newlyOpenedHandle, {.associatedPath = selectedPath, .realOpenedPath = successfulPath});
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu was opened for path \"%.*s\" and stored in association with path \"%.*s\".", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(newlyOpenedHandle), static_cast<int>(successfulPath.length()), successfulPath.data(), static_cast<int>(selectedPath.length()), selectedPath.data());
        }
    }

    /// Updates a handle that might already be in the open handle store, selecting an associated path based on the file operation redirection instruction.
    /// @param [in] functionName Name of the API function whose hook function is invoking this function. Used only for logging.
    /// @param [in] functionRequestIdentifier Request identifier associated with the invocation of the named function. Used only for logging.
    /// @param [in] handleToUpdate Handle to update in the open handles store, if it is present.
    /// @param [in] instruction Instruction that specifies how to redirect a filesystem operation.
    /// @param [in] successfulPath Path that was used successfully to create the file handle.
    /// @param [in] unredirectedPath Original file name supplied by the application.
    static void SelectFilenameAndUpdateOpenHandle(const wchar_t* functionName, unsigned int functionRequestIdentifier, HANDLE handleToUpdate, const FileOperationInstruction& instruction, std::wstring_view successfulPath, std::wstring_view unredirectedPath)
    {
        std::wstring_view selectedPath;

        switch (instruction.GetFilenameHandleAssociation())
        {
        case FileOperationInstruction::EAssociateNameWithHandle::None:
            do {
                OpenHandleStore::SHandleData erasedHandleData;
                if (true == OpenHandleStore::Singleton().RemoveHandle(handleToUpdate, &erasedHandleData))
                    Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu associated with path \"%s\" was erased from storage.", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(handleToUpdate), erasedHandleData.associatedPath.c_str());
            } while (false);
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::WhicheverWasSuccessful:
            selectedPath = successfulPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Unredirected:
            selectedPath = unredirectedPath;
            break;

        case FileOperationInstruction::EAssociateNameWithHandle::Redirected:
            selectedPath = instruction.GetRedirectedFilename();
            break;

        default:
            Message::OutputFormatted(Message::ESeverity::Error, L"%s(%u): Internal error: unrecognized file operation instruction (FileOperationInstruction::EAssociateNameWithHandle = %u).", functionName, functionRequestIdentifier, static_cast<unsigned int>(instruction.GetFilenameHandleAssociation()));
            break;
        }

        if (false == selectedPath.empty())
        {
            successfulPath = Strings::RemoveTrailing(successfulPath, L'\\');
            selectedPath = Strings::RemoveTrailing(selectedPath, L'\\');

            OpenHandleStore::Singleton().InsertOrUpdateHandle(handleToUpdate, {.associatedPath = selectedPath, .realOpenedPath = successfulPath});
            Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu was updated in storage to be opened with path \"%.*s\" and associated with path \"%.*s\".", functionName, functionRequestIdentifier, reinterpret_cast<size_t>(handleToUpdate), static_cast<int>(successfulPath.length()), successfulPath.data(), static_cast<int>(selectedPath.length()), selectedPath.data());
        }
    }
}


// -------- PROTECTED HOOK FUNCTIONS --------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtClose::Hook(HANDLE Handle)
{
    using namespace Pathwinder;


    std::optional<OpenHandleStore::SHandleDataView> maybeClosedHandleData = OpenHandleStore::Singleton().GetDataForHandle(Handle);
    if (false == maybeClosedHandleData.has_value())
        return Original(Handle);

    const unsigned int requestIdentifier = GetRequestIdentifier();

    OpenHandleStore::SHandleData closedHandleData;
    NTSTATUS closeHandleResult = OpenHandleStore::Singleton().RemoveAndCloseHandle(Handle, &closedHandleData);

    if (NT_SUCCESS(closeHandleResult))
        Message::OutputFormatted(Message::ESeverity::Debug, L"%s(%u): Handle %zu for path \"%s\" was closed and erased from storage.", GetFunctionName(), requestIdentifier, reinterpret_cast<size_t>(Handle), closedHandleData.associatedPath.c_str());

    return closeHandleResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtCreateFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, ObjectAttributes->RootDirectory, Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)), FileOperationModeFromCreateDisposition(CreateDisposition));
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    HANDLE newlyOpenedHandle = nullptr;
    NTSTATUS systemCallResult = NtStatus::kSuccess;

    std::wstring_view unredirectedPath = ((true == operationContext.composedInputPath.has_value()) ? operationContext.composedInputPath.value().AsStringView() : Strings::NtConvertUnicodeStringToStringView(*ObjectAttributes->ObjectName));
    std::wstring_view lastAttemptedPath;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
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
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

    if (NT_SUCCESS(systemCallResult))
        SelectFilenameAndStoreNewlyOpenedHandle(GetFunctionName(), requestIdentifier, newlyOpenedHandle, redirectionInstruction, lastAttemptedPath, unredirectedPath);

    *FileHandle = newlyOpenedHandle;
    return systemCallResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtOpenFile::Hook(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, ObjectAttributes->RootDirectory, Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)), FilesystemDirector::EFileOperationMode::OpenExistingFile);
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    HANDLE newlyOpenedHandle = nullptr;
    NTSTATUS systemCallResult = NtStatus::kSuccess;

    std::wstring_view unredirectedPath = ((true == operationContext.composedInputPath.has_value()) ? operationContext.composedInputPath.value().AsStringView() : Strings::NtConvertUnicodeStringToStringView(*ObjectAttributes->ObjectName));
    std::wstring_view lastAttemptedPath;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
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
        return Original(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);

    if (NT_SUCCESS(systemCallResult))
        SelectFilenameAndStoreNewlyOpenedHandle(GetFunctionName(), requestIdentifier, newlyOpenedHandle, redirectionInstruction, lastAttemptedPath, unredirectedPath);

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

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, ObjectAttributes->RootDirectory, Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)), FilesystemDirector::EFileOperationMode::OpenExistingFile);
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    std::wstring_view lastAttemptedPath;

    NTSTATUS systemCallResult = NtStatus::kSuccess;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
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
        return Original(ObjectAttributes, IoStatusBlock, FileInformation, Length, FileInformationClass);

    return systemCallResult;
}

// --------

NTSTATUS Pathwinder::Hooks::DynamicHook_NtSetInformationFile::Hook(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    using namespace Pathwinder;


    // This invocation is only interesting if it is a rename operation. Otherwise there is no change being made to the input file handle, which is already open.
    if (SFileRenameInformation::kFileInformationClass != FileInformationClass)
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    const unsigned int requestIdentifier = GetRequestIdentifier();

    SFileRenameInformation& unredirectedFileRenameInformation = *(reinterpret_cast<SFileRenameInformation*>(FileInformation));
    std::wstring_view unredirectedPath = unredirectedFileRenameInformation.GetFilename();

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, unredirectedFileRenameInformation.rootDirectory, unredirectedPath, FilesystemDirector::EFileOperationMode::CreateNewFile);
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    // Due to how the file rename information structure is laid out, including an embedded filename buffer of variable size, there is overhead to generating a new one.
    // Without a redirected filename present it is better to bail early than to generate a new one unconditionally.
    if (false == redirectionInstruction.HasRedirectedFilename())
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    FileRenameInformationAndFilename redirectedFileRenameInformationAndFilename = CopyFileRenameInformationAndReplaceFilename(unredirectedFileRenameInformation, redirectionInstruction.GetRedirectedFilename());
    SFileRenameInformation& redirectedFileRenameInformation = redirectedFileRenameInformationAndFilename.GetFileRenameInformation();

    NTSTATUS systemCallResult = NtStatus::kSuccess;
    std::wstring_view lastAttemptedPath;

    for (SFileRenameInformation* renameInformationToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, &unredirectedFileRenameInformation, &redirectedFileRenameInformation))
    {
        if (nullptr == renameInformationToTry)
            continue;

        lastAttemptedPath = renameInformationToTry->GetFilename();
        systemCallResult = Original(FileHandle, IoStatusBlock, reinterpret_cast<PVOID>(renameInformationToTry), Length, FileInformationClass);
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s(%u): NTSTATUS = 0x%08x, ObjectName = \"%.*s\".", GetFunctionName(), requestIdentifier, systemCallResult, static_cast<int>(lastAttemptedPath.length()), lastAttemptedPath.data());

        if (false == ShouldTryNextFilename(systemCallResult))
            break;
    }

    if (true == lastAttemptedPath.empty())
        return Original(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    if (NT_SUCCESS(systemCallResult))
        SelectFilenameAndUpdateOpenHandle(GetFunctionName(), requestIdentifier, FileHandle, redirectionInstruction, lastAttemptedPath, unredirectedPath);

    return systemCallResult;
}


// -------- UNPROTECTED HOOK FUNCTIONS ------------------------------------- //
// See original function and Hookshot documentation for details.

NTSTATUS Pathwinder::Hooks::DynamicHook_NtQueryAttributesFile::Hook(POBJECT_ATTRIBUTES ObjectAttributes, PFILE_BASIC_INFO FileInformation)
{
    using namespace Pathwinder;


    const unsigned int requestIdentifier = GetRequestIdentifier();

    const SFileOperationContext operationContext = GetFileOperationRedirectionInformation(GetFunctionName(), requestIdentifier, ObjectAttributes->RootDirectory, Strings::NtConvertUnicodeStringToStringView(*(ObjectAttributes->ObjectName)), FilesystemDirector::EFileOperationMode::OpenExistingFile);
    const FileOperationInstruction& redirectionInstruction = operationContext.instruction;

    if (true == Globals::GetConfigurationData().isDryRunMode)
        return Original(ObjectAttributes, FileInformation);

    NTSTATUS preOperationResult = ExecuteExtraPreOperations(GetFunctionName(), requestIdentifier, operationContext.instruction);
    if (!(NT_SUCCESS(preOperationResult)))
        return preOperationResult;

    SObjectNameAndAttributes redirectedObjectNameAndAttributes = {};
    FillRedirectedObjectNameAndAttributesForInstruction(redirectedObjectNameAndAttributes, operationContext.instruction, *ObjectAttributes);

    std::wstring_view lastAttemptedPath;

    NTSTATUS systemCallResult = NtStatus::kSuccess;

    for (POBJECT_ATTRIBUTES objectAttributesToTry : SelectFilesToTry(GetFunctionName(), requestIdentifier, redirectionInstruction, ObjectAttributes, &redirectedObjectNameAndAttributes.objectAttributes))
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
        return Original(ObjectAttributes, FileInformation);

    return systemCallResult;
}
