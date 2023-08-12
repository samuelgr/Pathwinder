/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file InProgressDirectoryEnumerationTest.cpp
 *   Unit tests for data structures that implement directory enumeration.
 *****************************************************************************/

#include "ApiWindows.h"
#include "FilesystemInstruction.h"
#include "FilesystemRule.h"
#include "InProgressDirectoryEnumeration.h"
#include "MockFilesystemOperations.h"
#include "TemporaryBuffer.h"

#include <string_view>


namespace PathwinderTest
{
    using namespace ::Pathwinder;


    // -------- TEST CASES ------------------------------------------------- //

    // Creates a directory with a small number of files and expects that they are all enumerated.
    TEST_CASE(EnumerationQueue_EnumerateAllFiles)
    {
        constexpr std::wstring_view kDirectoryName = L"C:\\Directory";
        constexpr std::wstring_view kFileNames[] = {
            L"File1.txt",
            L"File2.txt",
            L"File3.txt",
            L"File4.txt",
            L"File5.txt"
        };

        MockFilesystemOperations mockFilesystem;
        for (auto fileName : kFileNames)
        {
            TemporaryString fileAbsolutePath;
            fileAbsolutePath << kDirectoryName << L'\\' << fileName;
            mockFilesystem.AddFile(fileAbsolutePath.AsStringView());
        }

        EnumerationQueue enumerationQueue(L"C:\\Directory", SFileNamesInformation::kFileInformationClass);

        for (auto fileName : kFileNames)
        {
            TEST_ASSERT(NT_SUCCESS(enumerationQueue.EnumerationStatus()));
            TEST_ASSERT(enumerationQueue.FileNameOfFront() == fileName);
            enumerationQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerationQueue.EnumerationStatus());
    }

    // Creates a directory with a small number of files, gets part-way through enumerating them all, and then restarts the scan.
    // After the restart all the files should be enumerated.
    TEST_CASE(EnumerationQueue_EnumerateAllFilesWithRestart)
    {
        constexpr std::wstring_view kDirectoryName = L"C:\\Directory";
        constexpr std::wstring_view kFileNames[] = {
            L"File1.txt",
            L"File2.txt",
            L"File3.txt",
            L"File4.txt",
            L"File5.txt"
        };

        MockFilesystemOperations mockFilesystem;
        for (auto fileName : kFileNames)
        {
            TemporaryString fileAbsolutePath;
            fileAbsolutePath << kDirectoryName << L'\\' << fileName;
            mockFilesystem.AddFile(fileAbsolutePath.AsStringView());
        }

        EnumerationQueue enumerationQueue(L"C:\\Directory", SFileNamesInformation::kFileInformationClass);

        for (int i = 0; i < _countof(kFileNames) - 2; ++i)
        {
            TEST_ASSERT(NT_SUCCESS(enumerationQueue.EnumerationStatus()));
            TEST_ASSERT(enumerationQueue.FileNameOfFront() == kFileNames[i]);
            enumerationQueue.PopFront();
        }

        enumerationQueue.Restart();

        for (auto fileName : kFileNames)
        {
            TEST_ASSERT(NT_SUCCESS(enumerationQueue.EnumerationStatus()));
            TEST_ASSERT(enumerationQueue.FileNameOfFront() == fileName);
            enumerationQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerationQueue.EnumerationStatus());
    }

    // Creates a directory with a small number of files and expects that only files that match the file pattern are enumerated.
    TEST_CASE(EnumerationQueue_EnumerateOnlyMatchingFiles)
    {
        constexpr std::wstring_view kFilePattern = L"*.txt";
        constexpr std::wstring_view kDirectoryName = L"C:\\Directory";
        constexpr std::wstring_view kMatchingFileNames[] = {
            L"asdf.txt",
            L"File1.txt",
            L"File2.txt",
            L"File3.txt",
            L"File4.txt",
            L"File5.txt",
            L"zZz.txt"
        };
        constexpr std::wstring_view kNonMatchingFileNames[] = {
            L"SomeOtherFile.bin",
            L"File.log",
            L"Program.exe"
        };

        MockFilesystemOperations mockFilesystem;
        for (auto fileName : kMatchingFileNames)
        {
            TemporaryString fileAbsolutePath;
            fileAbsolutePath << kDirectoryName << L'\\' << fileName;
            mockFilesystem.AddFile(fileAbsolutePath.AsStringView());
        }
        for (auto fileName : kNonMatchingFileNames)
        {
            TemporaryString fileAbsolutePath;
            fileAbsolutePath << kDirectoryName << L'\\' << fileName;
            mockFilesystem.AddFile(fileAbsolutePath.AsStringView());
        }

        EnumerationQueue enumerationQueue(L"C:\\Directory", SFileNamesInformation::kFileInformationClass, kFilePattern);

        for (auto fileName : kMatchingFileNames)
        {
            TEST_ASSERT(NT_SUCCESS(enumerationQueue.EnumerationStatus()));
            TEST_ASSERT(enumerationQueue.FileNameOfFront() == fileName);
            enumerationQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerationQueue.EnumerationStatus());
    }

    // Attempts to enumerate an empty directory. This should succeed but return no files.
    TEST_CASE(EnumerationQueue_EnumerateEmptyDirectory)
    {
        constexpr std::wstring_view kDirectoryName = L"C:\\Directory";

        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(kDirectoryName);

        EnumerationQueue enumerationQueue(L"C:\\Directory", SFileNamesInformation::kFileInformationClass);
        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerationQueue.EnumerationStatus());
    }

    // Attempts to enumerate a directory that does not exist. This should result in an error code prior to enumeration.
    TEST_CASE(EnumerationQueue_EnumerateNonExistentDirectory)
    {
        MockFilesystemOperations mockFilesystem;
        EnumerationQueue enumerationQueue(L"C:\\Directory", SFileNamesInformation::kFileInformationClass);
        TEST_ASSERT(!(NT_SUCCESS(enumerationQueue.EnumerationStatus())));
    }
}
