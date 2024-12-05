/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file MockDirectoryOperationQueue.h
 *   Declaration of controlled fake directory enumeration operation queues that can be used for
 *   testing.
 **************************************************************************************************/

#pragma once

#include <set>
#include <string>
#include <string_view>

#include <Infra/Strings.h>

#include "ApiWindows.h"
#include "DirectoryOperationQueue.h"
#include "FileInformationStruct.h"

namespace PathwinderTest
{
  /// Implements a fake stream of file information structures which are exposed via a queue-like
  /// interface.
  class MockDirectoryOperationQueue : public Pathwinder::IDirectoryOperationQueue
  {
  public:

    /// Type alias for the container type used to hold a sorted set of file names to be
    /// enumerated.
    using TFileNamesToEnumerate =
        std::set<std::wstring, Infra::Strings::CaseInsensitiveLessThanComparator<wchar_t>>;

    /// Queues created this way will not enumerate any files but can be used to test enumeration
    /// status reporting. This can also be used as a default constructor for creating objects that
    /// cannot enumerate anything but instead simply report failure.
    MockDirectoryOperationQueue(NTSTATUS enumerationStatus = Pathwinder::NtStatus::kInternalError);

    MockDirectoryOperationQueue(
        Pathwinder::FileInformationStructLayout fileInformationStructLayout,
        TFileNamesToEnumerate&& fileNamesToEnumerate);

    /// Retrieves the last query file pattern passed when restarting this queue's enumeration
    /// progress.
    /// @return Last-used query file pattern.
    inline std::wstring_view GetLastRestartedQueryFilePattern(void) const
    {
      return lastRestartedQueryFilePattern;
    }

    // IDirectoryOperationQueue
    unsigned int CopyFront(void* dest, unsigned int capacityBytes) const override;
    NTSTATUS EnumerationStatus(void) const override;
    std::wstring_view FileNameOfFront(void) const override;
    void PopFront(void) override;
    void Restart(std::wstring_view queryFilePattern = std::wstring_view()) override;
    unsigned int SizeOfFront(void) const override;

  private:

    /// File information structure layout information. Used to determine the offsets and sizes
    /// of file information structures to provide as output.
    Pathwinder::FileInformationStructLayout fileInformationStructLayout;

    /// All of the filenames to enumerate, in sorted order.
    TFileNamesToEnumerate fileNamesToEnumerate;

    /// Iterator for the next filename to be enumerated.
    TFileNamesToEnumerate::const_iterator nextFileNameToEnumerate;

    /// Optional override for the enumeration status.
    std::optional<NTSTATUS> enumerationStatusOverride;

    /// Holds the last query file pattern passed when attempting to restart this queue's enumeration
    /// progress. Not used for anything internally.
    std::wstring_view lastRestartedQueryFilePattern;
  };
} // namespace PathwinderTest
