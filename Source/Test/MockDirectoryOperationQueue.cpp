/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file MockDirectoryOperationQueue.cpp
 *   Implementation of controlled fake directory enumeration operation queues
 *   that can be used for testing.
 *****************************************************************************/

#include "ApiWindowsInternal.h"
#include "FileInformationStruct.h"
#include "MockDirectoryOperationQueue.h"

#include <cstring>
#include <set>
#include <string>
#include <string_view>


namespace PathwinderTest
{
    // -------- CONSTRUCTION AND DESTRUCTION ------------------------------- //
    // See "MockDirectoryOperationQueue.h" for documentation.

    MockDirectoryOperationQueue::MockDirectoryOperationQueue(Pathwinder::FileInformationStructLayout fileInformationStructLayout, TFileNamesToEnumerate&& fileNamesToEnumerate) : fileInformationStructLayout(fileInformationStructLayout), fileNamesToEnumerate(std::move(fileNamesToEnumerate)), nextFileNameToEnumerate(), enumerationStatusOverride()
    {
        Restart();
    }


    // -------- CONCRETE INSTANCE METHODS ---------------------------------- //
    // See "MockDirectoryOperationQueue.h" for documentation.

    unsigned int MockDirectoryOperationQueue::CopyFront(void* dest, unsigned int capacityBytes) const
    {
        std::wstring_view fileName = FileNameOfFront();
        const unsigned int numBytesToCopy = std::min(SizeOfFront(), capacityBytes);

        // For testing purposes, it is sufficient to fill the entire file information structure space with a fake value and then overwrite the relevant filename fields.
        std::memset(dest, 0, numBytesToCopy);
        fileInformationStructLayout.WriteFileName(dest, fileName, numBytesToCopy);

        return numBytesToCopy;
    }

    // --------

    NTSTATUS MockDirectoryOperationQueue::EnumerationStatus(void) const
    {
        if (true == enumerationStatusOverride.has_value())
            return *enumerationStatusOverride;

        if (fileNamesToEnumerate.cend() == nextFileNameToEnumerate)
            return Pathwinder::NtStatus::kNoMoreFiles;

        return Pathwinder::NtStatus::kMoreEntries;
    }

    // --------

    std::wstring_view MockDirectoryOperationQueue::FileNameOfFront(void) const
    {
        return *nextFileNameToEnumerate;
    }

    // --------

    unsigned int MockDirectoryOperationQueue::SizeOfFront(void) const
    {
        return fileInformationStructLayout.HypotheticalSizeForFileNameLength(static_cast<unsigned int>(FileNameOfFront().length()));
    }

    // --------

    void MockDirectoryOperationQueue::PopFront(void)
    {
        ++nextFileNameToEnumerate;
    }

    // --------

    void MockDirectoryOperationQueue::Restart(void)
    {
        nextFileNameToEnumerate = fileNamesToEnumerate.cbegin();
    }
}
