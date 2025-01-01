/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file OpenHandleStore.h
 *   Implementation of a container for open filesystem handles along with state information and
 *   metadata associated with each one.
 **************************************************************************************************/

#include "OpenHandleStore.h"

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

#include <Infra/Core/DebugAssert.h>
#include <Infra/Core/Mutex.h>

#include "ApiWindows.h"
#include "DirectoryOperationQueue.h"
#include "FileInformationStruct.h"
#include "FilesystemOperations.h"
#include "Strings.h"

namespace Pathwinder
{
  void OpenHandleStore::AssociateDirectoryEnumerationState(
      HANDLE handleToAssociate,
      std::unique_ptr<IDirectoryOperationQueue>&& directoryEnumerationQueue,
      FileInformationStructLayout fileInformationStructLayout)
  {
    std::unique_lock lock(openHandlesMutex);

    auto openHandleIter = openHandles.find(handleToAssociate);
    DebugAssert(
        openHandleIter != openHandles.end(),
        "Attempting to associate a directory enumeration queue with a handle that is not in storage.");
    if (openHandleIter == openHandles.end()) return;

    DebugAssert(
        false == openHandleIter->second.directoryEnumeration.has_value(),
        "Attempting to re-associate a directory enumeration queue with a handle that already has one.");

    openHandleIter->second.directoryEnumeration = SInProgressDirectoryEnumeration{
        .queue = std::move(directoryEnumerationQueue),
        .fileInformationStructLayout = fileInformationStructLayout,
        .isFirstInvocation = true};
  }

  bool OpenHandleStore::Empty(void)
  {
    std::shared_lock lock(openHandlesMutex);
    return openHandles.empty();
  }

  std::optional<OpenHandleStore::SHandleDataView> OpenHandleStore::GetDataForHandle(
      HANDLE handleToQuery)
  {
    std::shared_lock lock(openHandlesMutex);

    auto openHandleIter = openHandles.find(handleToQuery);
    if (openHandleIter == openHandles.cend()) return std::nullopt;

    return openHandleIter->second;
  }

  void OpenHandleStore::InsertHandle(
      HANDLE handleToInsert, std::wstring&& associatedPath, std::wstring&& realOpenedPath)
  {
    std::unique_lock lock(openHandlesMutex);

    const bool insertionWasSuccessful =
        openHandles
            .emplace(
                handleToInsert, SHandleData(std::move(associatedPath), std::move(realOpenedPath)))
            .second;
    DebugAssert(true == insertionWasSuccessful, "Failed to insert a handle into storage.");
  }

  void OpenHandleStore::InsertOrUpdateHandle(
      HANDLE handleToInsertOrUpdate, std::wstring&& associatedPath, std::wstring&& realOpenedPath)
  {
    std::unique_lock lock(openHandlesMutex);

    auto existingHandleIter = openHandles.find(handleToInsertOrUpdate);
    if (openHandles.end() == existingHandleIter)
    {
      const bool insertionWasSuccessful =
          openHandles
              .emplace(
                  handleToInsertOrUpdate,
                  SHandleData(std::move(associatedPath), std::move(realOpenedPath)))
              .second;
      DebugAssert(true == insertionWasSuccessful, "Failed to insert a handle into storage.");
    }
    else
    {
      existingHandleIter->second.associatedPath = std::move(associatedPath);
      existingHandleIter->second.realOpenedPath = std::move(realOpenedPath);
    }
  }

  bool OpenHandleStore::RemoveHandle(HANDLE handleToRemove, SHandleData* handleData)
  {
    std::unique_lock lock(openHandlesMutex);

    auto removalIter = openHandles.find(handleToRemove);
    if (openHandles.end() == removalIter) return false;

    if (nullptr == handleData)
      openHandles.erase(removalIter);
    else
      *handleData = std::move(openHandles.extract(removalIter).mapped());

    return true;
  }

  NTSTATUS OpenHandleStore::RemoveAndCloseHandle(HANDLE handleToRemove, SHandleData* handleData)
  {
    std::unique_lock lock(openHandlesMutex);

    auto removalIter = openHandles.find(handleToRemove);
    DebugAssert(
        openHandles.end() != removalIter,
        "Attempting to close and erase a handle that was not previously stored.");

    NTSTATUS systemCallResult = FilesystemOperations::CloseHandle(handleToRemove);
    if (!(NT_SUCCESS(systemCallResult))) return systemCallResult;

    if (nullptr == handleData)
      openHandles.erase(removalIter);
    else
      *handleData = std::move(openHandles.extract(removalIter).mapped());

    return systemCallResult;
  }

  unsigned int OpenHandleStore::Size(void)
  {
    std::shared_lock lock(openHandlesMutex);
    return static_cast<unsigned int>(openHandles.size());
  }
} // namespace Pathwinder
