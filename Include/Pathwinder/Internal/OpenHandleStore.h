/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file OpenHandleStore.h
 *   Declaration of a container for open filesystem handles along with state information and
 *   metadata associated with each one.
 **************************************************************************************************/

#pragma once

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ApiWindows.h"
#include "DirectoryOperationQueue.h"
#include "FileInformationStruct.h"
#include "MutexWrapper.h"
#include "Strings.h"

namespace Pathwinder
{
  /// Implements a concurrency-safe storage data structure for open filesystem handles and
  /// metadata associated with each.
  class OpenHandleStore
  {
  public:

    /// Record type for storing an in-progress directory enumeration operation.
    struct SInProgressDirectoryEnumeration
    {
      /// Directory enumeration queue, from which additional file information structures are
      /// transferred. A value of `nullptr` means the directory enumeration operation is a
      /// no-op and should be forwarded to the system.
      std::unique_ptr<IDirectoryOperationQueue> queue;

      /// Layout description for the file information structures produced in the directory
      /// enumeration.
      FileInformationStructLayout fileInformationStructLayout;

      /// Set of already-enumerated files. Used for deduplication in the output.
      std::set<std::wstring, Strings::CaseInsensitiveLessThanComparator<wchar_t>>
          enumeratedFilenames;
    };

    /// By-reference view of data stored about an open handle.
    struct SHandleDataView
    {
      /// Path associated internally with the open handle.
      std::wstring_view associatedPath;

      /// Actual path that was opened for the handle. This could be different from the
      /// associated path based on instructions from a filesystem director.
      std::wstring_view realOpenedPath;

      /// In-progress directory enumeration state. Not owned by this structure.
      std::optional<SInProgressDirectoryEnumeration*> directoryEnumeration;

      inline bool operator==(const SHandleDataView& other) const = default;
    };

    /// Data stored about an open handle.
    struct SHandleData
    {
      /// Path associated internally with the open handle.
      std::wstring associatedPath;

      /// Actual path that was opened for the handle. This could be different from the
      /// associated path based on instructions from a filesystem director.
      std::wstring realOpenedPath;

      /// In-progress directory enumeration state.
      std::optional<SInProgressDirectoryEnumeration> directoryEnumeration;

      SHandleData(void) = default;

      inline SHandleData(std::wstring&& associatedPath, std::wstring&& realOpenedPath)
          : associatedPath(std::move(associatedPath)),
            realOpenedPath(std::move(realOpenedPath)),
            directoryEnumeration()
      {}

      SHandleData(SHandleData&& other) = default;

      SHandleData& operator=(SHandleData&& other) = default;

      inline operator SHandleDataView(void)
      {
        return {
            .associatedPath = associatedPath,
            .realOpenedPath = realOpenedPath,
            .directoryEnumeration =
                ((true == directoryEnumeration.has_value())
                     ? std::optional<SInProgressDirectoryEnumeration*>(&(*directoryEnumeration))
                     : std::nullopt)};
      }
    };

    /// Associates a directory enumeration state object with the specified handle.
    /// @param [in] handleToAssociate Handle to be associated with the directory enumeration
    /// queue.
    /// @param [in] directoryEnumerationQueue Directory enumeration queue to associate with the
    /// handle. This object takes over ownership of the provided directory enumeration queue.
    /// @param [in] fileInformationStructLayout Layout description for the file information
    /// structures that will be produced by the directory enumeration query.
    void AssociateDirectoryEnumerationState(
        HANDLE handleToAssociate,
        std::unique_ptr<IDirectoryOperationQueue>&& directoryEnumerationQueue,
        FileInformationStructLayout fileInformationStructLayout);

    /// Queries the open handle store for the specified handle and retrieves a read-only view of
    /// the associated data, if the handle is found in the store.
    /// @param [in] handleToQuery Handle for which to query.
    /// @return Read-only view of the data associated with the handle, if the handle exists in
    /// the store.
    std::optional<SHandleDataView> GetDataForHandle(HANDLE handleToQuery);

    /// Inserts a new handle and corresponding metadata into the open handle store.
    /// @param [in] handleToInsert Handle to be inserted.
    /// @param [in] associatedPath Path to associate internally with the handle.
    /// @param [in] realOpenedPath Path that was actually opened when producing the handle.
    void InsertHandle(
        HANDLE handleToInsert, std::wstring&& associatedPath, std::wstring&& realOpenedPath);

    /// Inserts a new handle and corresponding path into the open handle store or, if the handle
    /// already exists, updates its stored data. Does not affect the directory enumeration
    /// queue, only the path metadata.
    /// @param [in] handleToInsert Handle to be inserted.
    /// @param [in] associatedPath Path to associate internally with the handle.
    /// @param [in] realOpenedPath Path that was actually opened when producing the handle.
    void InsertOrUpdateHandle(
        HANDLE handleToInsertOrUpdate,
        std::wstring&& associatedPath,
        std::wstring&& realOpenedPath);

    /// Attempts to remove an existing handle and corresponding path from the open handle store.
    /// @param [in] handleToRemove Handle to be removed.
    /// @param [out] handleData Handle data object to receive ownership of the corresponding
    /// data for the handle that was removed, if not null. Only filled if this method returns
    /// `true`.
    /// @return `true` if the handle was found and removed, `false` otherwise.
    bool RemoveHandle(HANDLE handleToRemove, SHandleData* handleData);

    /// Attempts to close and subsequently remove an existing handle and corresponding path from
    /// the open handle store. Both handle closure and removal need to be done while the lock is
    /// held, to ensure proper concurrency control. This avoids a race condition in which a
    /// closed handle is reused and re-added to the store before the closing thread has a chance
    /// to remove it first.
    /// @param [in] handleToRemove Handle to be removed.
    /// @param [out] handleData Handle data object to receive ownership of the corresponding
    /// data for the handle that was removed, if not null. Only filled if the underlying system
    /// call to close the handle succeeds.
    /// @return Result of the underlying system call to `NtClose` to close the handle.
    NTSTATUS RemoveAndCloseHandle(HANDLE handleToRemove, SHandleData* handleData);

  private:

    /// Open handle data structure itself.
    /// Maps from a handle to the filesystem path that was used to open it.
    std::unordered_map<HANDLE, SHandleData> openHandles;

    /// Mutex for ensuring concurrency-safe access to the open handles data structure.
    SharedMutex openHandlesMutex;
  };
} // namespace Pathwinder
