/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file OpenHandleStore.h
 *   Declaration and implementation of a container for open filesystem handles
 *   along with state information and metadata associated with each one.
 *****************************************************************************/

#pragma once

#include "ApiWindows.h"
#include "DirectoryOperationQueue.h"
#include "Hooks.h"
#include "MutexWrapper.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>


namespace Pathwinder
{
    /// Implements a concurrency-safe storage data structure for open filesystem handles and metadata associated with each.
    class OpenHandleStore
    {
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Read-only view of data stored about an open handle.
        struct SHandleDataView
        {
            std::wstring_view associatedPath;                                                   ///< Path associated internally with the open handle.
            std::wstring_view realOpenedPath;                                                   ///< Actual path that was opened for the handle. This could be different from the associated path based on instructions from a filesystem director.
            std::optional<IDirectoryOperationQueue*> directoryEnumerationQueue;                 ///< In-progress directory enumeration queue. Not owned by this structure. No value means there is no associated directory enumeration operation, and `nullptr` means the directory enumeration operation is a no-op and should be forwarded to the system.
        };

        /// Data stored about an open handle.
        struct SHandleData
        {
            std::wstring associatedPath;                                                        ///< Path associated internally with the open handle.
            std::wstring realOpenedPath;                                                        ///< Actual path that was opened for the handle. This could be different from the associated path based on instructions from a filesystem director.
            std::optional<std::unique_ptr<IDirectoryOperationQueue>> directoryEnumerationQueue; ///< In-progress directory enumeration queue. No value means there is no associated directory enumeration operation, and `nullptr` means the directory enumeration operation is a no-op and should be forwarded to the system.

            /// Default constructor.
            inline SHandleData(void) = default;

            /// Initialization constructor.
            /// Requires both paths be specified using move semantics, and does not construct a directory enumeration queue.
            inline SHandleData(std::wstring&& associatedPath, std::wstring&& realOpenedPath) : associatedPath(std::move(associatedPath)), realOpenedPath(std::move(realOpenedPath)), directoryEnumerationQueue()
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
                    .realOpenedPath = realOpenedPath,
                    .directoryEnumerationQueue = ((true == directoryEnumerationQueue.has_value()) ? std::optional<IDirectoryOperationQueue*>(directoryEnumerationQueue->get()) : std::nullopt)
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

        /// Associates a directory enumeration queue with the specified handle.
        /// @param [in] handleToAssociate Handle to be associated with the directory enumeration queue. If not present then the directory enumeration queue is cleared.
        /// @param [in] directoryEnumerationQueue Directory enumeration queue to associate with the handle. This object takes over ownership of the provided directory enumeration queue.
        inline void AssociateDirectoryEnumerationQueue(HANDLE handleToAssociate, std::optional<std::unique_ptr<IDirectoryOperationQueue>>&& directoryEnumerationQueue)
        {
            std::shared_lock lock(openHandlesMutex);

            auto openHandleIter = openHandles.find(handleToAssociate);
            DebugAssert(openHandleIter != openHandles.end(), "Attempting to associate a directory enumeration queue with a handle that is not in storage.");
            if (openHandleIter == openHandles.end())
                return;

            openHandleIter->second.directoryEnumerationQueue = std::move(directoryEnumerationQueue);
        }

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

        /// Inserts a new handle and corresponding metadata into the open handle store.
        /// @param [in] handleToInsert Handle to be inserted.
        /// @param [in] associatedPath Path to associate internally with the handle.
        /// @param [in] realOpenedPath Path that was actually opened when producing the handle.
        inline void InsertHandle(HANDLE handleToInsert, std::wstring&& associatedPath, std::wstring&& realOpenedPath)
        {
            std::unique_lock lock(openHandlesMutex);

            const bool insertionWasSuccessful = openHandles.emplace(handleToInsert, SHandleData(std::move(associatedPath), std::move(realOpenedPath))).second;
            DebugAssert(true == insertionWasSuccessful, "Failed to insert a handle into storage.");
        }

        /// Inserts a new handle and corresponding path into the open handle store or, if the handle already exists, updates its stored data.
        /// Does not affect the directory enumeration queue, only the path metadata.
        /// @param [in] handleToInsert Handle to be inserted.
        /// @param [in] associatedPath Path to associate internally with the handle.
        /// @param [in] realOpenedPath Path that was actually opened when producing the handle.
        inline void InsertOrUpdateHandle(HANDLE handleToInsertOrUpdate, std::wstring&& associatedPath, std::wstring&& realOpenedPath)
        {
            std::unique_lock lock(openHandlesMutex);

            auto existingHandleIter = openHandles.find(handleToInsertOrUpdate);
            if (openHandles.end() == existingHandleIter)
            {
                const bool insertionWasSuccessful = openHandles.emplace(handleToInsertOrUpdate, SHandleData(std::move(associatedPath), std::move(realOpenedPath))).second;
                DebugAssert(true == insertionWasSuccessful, "Failed to insert a handle into storage.");
            }
            else
            {
                existingHandleIter->second.associatedPath = std::move(associatedPath);
                existingHandleIter->second.realOpenedPath = std::move(realOpenedPath);
            }
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
}
