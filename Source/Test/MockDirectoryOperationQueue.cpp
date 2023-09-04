/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file MockDirectoryOperationQueue.cpp
 *   Implementation of controlled fake directory enumeration operation queues that can be used
 *   for testing.
 **************************************************************************************************/

#include "TestCase.h"

#include "MockDirectoryOperationQueue.h"

#include <cstring>
#include <set>
#include <string>
#include <string_view>

#include "ApiWindowsInternal.h"
#include "FileInformationStruct.h"

namespace PathwinderTest
{
    MockDirectoryOperationQueue::MockDirectoryOperationQueue(NTSTATUS enumerationStatus)
        : fileInformationStructLayout(),
          fileNamesToEnumerate(),
          nextFileNameToEnumerate(),
          enumerationStatusOverride(enumerationStatus)
    {}

    MockDirectoryOperationQueue::MockDirectoryOperationQueue(
        Pathwinder::FileInformationStructLayout fileInformationStructLayout,
        TFileNamesToEnumerate&& fileNamesToEnumerate)
        : fileInformationStructLayout(fileInformationStructLayout),
          fileNamesToEnumerate(std::move(fileNamesToEnumerate)),
          nextFileNameToEnumerate(),
          enumerationStatusOverride()
    {
        if (Pathwinder::FileInformationStructLayout() == this->fileInformationStructLayout)
            TEST_FAILED_BECAUSE(
                L"%s: Test implementation error due to creation of a directory operation queue with an unsupported file information class.",
                __FUNCTIONW__);

        if (true == this->fileNamesToEnumerate.empty())
            TEST_FAILED_BECAUSE(
                L"%s: Test implementation error due to creation of a directory operation queue with an empty set of filenames to enumerate.",
                __FUNCTIONW__);

        Restart();
    }

    unsigned int
        MockDirectoryOperationQueue::CopyFront(void* dest, unsigned int capacityBytes) const
    {
        if (true == fileNamesToEnumerate.empty()) return 0;

        std::wstring_view fileName = FileNameOfFront();
        const unsigned int numBytesToCopy = std::min(SizeOfFront(), capacityBytes);

        // For testing purposes, it is sufficient to fill the entire file information structure
        // space with a fake value and then overwrite the relevant filename fields.
        std::memset(dest, 0, numBytesToCopy);
        fileInformationStructLayout.WriteFileName(dest, fileName, numBytesToCopy);

        return numBytesToCopy;
    }

    NTSTATUS MockDirectoryOperationQueue::EnumerationStatus(void) const
    {
        if (true == enumerationStatusOverride.has_value()) return *enumerationStatusOverride;

        if (fileNamesToEnumerate.cend() == nextFileNameToEnumerate)
            return Pathwinder::NtStatus::kNoMoreFiles;

        return Pathwinder::NtStatus::kMoreEntries;
    }

    std::wstring_view MockDirectoryOperationQueue::FileNameOfFront(void) const
    {
        if (true == fileNamesToEnumerate.empty()) return std::wstring_view();

        return *nextFileNameToEnumerate;
    }

    void MockDirectoryOperationQueue::PopFront(void)
    {
        if (false == fileNamesToEnumerate.empty()) ++nextFileNameToEnumerate;
    }

    void MockDirectoryOperationQueue::Restart(std::wstring_view unusedQueryFilePattern)
    {
        if (false == fileNamesToEnumerate.empty())
            nextFileNameToEnumerate = fileNamesToEnumerate.cbegin();
    }

    unsigned int MockDirectoryOperationQueue::SizeOfFront(void) const
    {
        if (true == fileNamesToEnumerate.empty()) return 0;

        return fileInformationStructLayout.HypotheticalSizeForFileNameLength(
            static_cast<unsigned int>(FileNameOfFront().length()));
    }
}  // namespace PathwinderTest
