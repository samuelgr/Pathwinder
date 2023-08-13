/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file MockDirectoryOperationQueue.h
 *   Declaration of controlled fake directory enumeration operation queues
 *   that can be used for testing.
 *****************************************************************************/

#pragma once

#include "FileInformationStruct.h"
#include "InProgressDirectoryEnumeration.h"

#include <set>
#include <string>
#include <string_view>


namespace PathwinderTest
{
    /// Implements a fake stream of file information structures which are exposed via a queue-like interface.
    class MockDirectoryOperationQueue : public Pathwinder::IDirectoryOperationQueue
    {
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Type alias for the container type used to hold a sorted set of file names to be enumerated.
        typedef std::set<std::wstring, std::less<>> TFileNamesToEnumerate;


    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// File information structure layout information. Used to determine the offsets and sizes of file information structures to provide as output.
        Pathwinder::FileInformationStructLayout fileInformationStructLayout;

        /// All of the filenames to enumerate, in sorted order.
        TFileNamesToEnumerate fileNamesToEnumerate;

        /// Iterator for the next filename to be enumerated.
        TFileNamesToEnumerate::const_iterator nextFileNameToEnumerate;

        /// Optional override for the enumeration status.
        std::optional<NTSTATUS> enumerationStatusOverride;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Requires a file information structure layout and a pre-created set of file names to be enumerated.
        MockDirectoryOperationQueue(Pathwinder::FileInformationStructLayout fileInformationStructLayout, TFileNamesToEnumerate&& fileNamesToEnumerate);


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Sets or clears the enumeration status that should be returned whenever this object is queried.
        /// @param [in] newEnumerationStatusOverride New enumeration status to be returned. If not present, the override is cleared and the returned status is based on progress enumerating through the filenames.
        inline void OverrideEnumerationStatus(std::optional<NTSTATUS> newEnumerationStatusOverride)
        {
            enumerationStatusOverride = newEnumerationStatusOverride;
        }


        // -------- CONCRETE INSTANCE METHODS ------------------------------ //
        // See above for documentation.

        unsigned int CopyFront(void* dest, unsigned int capacityBytes) const override;
        NTSTATUS EnumerationStatus(void) const override;
        std::wstring_view FileNameOfFront(void) const override;
        void PopFront(void) override;
        void Restart(std::wstring_view unusedQueryFilePattern = std::wstring_view()) override;
        unsigned int SizeOfFront(void) const override;
    };
}