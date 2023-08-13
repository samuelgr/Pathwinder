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

    // Enumerates the parent directory of a single filesystem rule's origin directory such that the rule's origin directory and target directory both exist in the filesystem.
    // That origin directory should be the only item enumerated.
    TEST_CASE(NameInsertionQueue_SingleFilesystemRule_OriginAndTargetExist)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\Directory1\\Origin");
        mockFilesystem.AddDirectory(L"C:\\Directory2\\Target");

        const FilesystemRule filesystemRules[] = {
            FilesystemRule(L"C:\\Directory1\\Origin", L"C:\\Directory2\\Target")
        };

        TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion> nameInsertionInstructions;
        for (const auto& filesystemRule : filesystemRules)
            nameInsertionInstructions.EmplaceBack(filesystemRule);

        NameInsertionQueue nameInsertionQueue(std::move(nameInsertionInstructions), SFileNamesInformation::kFileInformationClass);

        for (const auto& filesystemRule : filesystemRules)
        {
            TEST_ASSERT(NT_SUCCESS(nameInsertionQueue.EnumerationStatus()));
            TEST_ASSERT(nameInsertionQueue.FileNameOfFront() == filesystemRule.GetOriginDirectoryName());
            nameInsertionQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == nameInsertionQueue.EnumerationStatus());
    }

    // Enumerates the parent directory of a single filesystem rule's origin directory such that the rule's origin directory does not exist but the target directory does exist in the filesystem.
    // That origin directory should be the only item enumerated. It is irrelevant that it does not exist for real in the filesystem.
    TEST_CASE(NameInsertionQueue_SingleFilesystemRule_OriginDoesNotExist)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\Directory2\\Target");

        const FilesystemRule filesystemRules[] = {
            FilesystemRule(L"C:\\Directory1\\Origin", L"C:\\Directory2\\Target")
        };

        TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion> nameInsertionInstructions;
        for (const auto& filesystemRule : filesystemRules)
            nameInsertionInstructions.EmplaceBack(filesystemRule);

        NameInsertionQueue nameInsertionQueue(std::move(nameInsertionInstructions), SFileNamesInformation::kFileInformationClass);

        for (const auto& filesystemRule : filesystemRules)
        {
            TEST_ASSERT(NT_SUCCESS(nameInsertionQueue.EnumerationStatus()));
            TEST_ASSERT(nameInsertionQueue.FileNameOfFront() == filesystemRule.GetOriginDirectoryName());
            nameInsertionQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == nameInsertionQueue.EnumerationStatus());
    }

    // Enumerates the parent directory of a single filesystem rule's origin directory such that the rule's target directory does not exist but the origin directory does exist in the filesystem.
    // Nothing should be enumerated because the target directory does not exist.
    TEST_CASE(NameInsertionQueue_SingleFilesystemRule_TargetDoesNotExist)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\Directory2\\Origin");

        const FilesystemRule filesystemRules[] = {
            FilesystemRule(L"C:\\Directory1\\Origin", L"C:\\Directory2\\Target")
        };

        TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion> nameInsertionInstructions;
        for (const auto& filesystemRule : filesystemRules)
            nameInsertionInstructions.EmplaceBack(filesystemRule);

        NameInsertionQueue nameInsertionQueue(std::move(nameInsertionInstructions), SFileNamesInformation::kFileInformationClass);
        TEST_ASSERT(NtStatus::kNoMoreFiles == nameInsertionQueue.EnumerationStatus());
    }

    // Enumerates the parent directory of four filesystem rule's origin directories such that two of them have target directories that exist on the real filesystem.
    // Only the two origin directories that belong to filesystem rules with target directories that exist should be enumerated.
    TEST_CASE(NameInsertionQueue_MultipleFilesystemRules_SomeTargetDirectoriesExist)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\DirectoryTarget\\Target2");
        mockFilesystem.AddDirectory(L"C:\\DirectoryTarget\\Target3");

        const FilesystemRule filesystemRules[] = {
            FilesystemRule(L"C:\\DirectoryOrigin\\Origin1", L"C:\\DirectoryTarget\\Target1"),
            FilesystemRule(L"C:\\DirectoryOrigin\\Origin2", L"C:\\DirectoryTarget\\Target2"),
            FilesystemRule(L"C:\\DirectoryOrigin\\Origin3", L"C:\\DirectoryTarget\\Target3"),
            FilesystemRule(L"C:\\DirectoryOrigin\\Origin4", L"C:\\DirectoryTarget\\Target4")
        };

        TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion> nameInsertionInstructions;
        for (const auto& filesystemRule : filesystemRules)
            nameInsertionInstructions.EmplaceBack(filesystemRule);

        NameInsertionQueue nameInsertionQueue(std::move(nameInsertionInstructions), SFileNamesInformation::kFileInformationClass);

        constexpr std::wstring_view kExpectedEnumeratedItems[] = {
            L"Origin2",
            L"Origin3"
        };

        for (const auto& expectedEnumeratedItem : kExpectedEnumeratedItems)
        {
            TEST_ASSERT(NT_SUCCESS(nameInsertionQueue.EnumerationStatus()));
            TEST_ASSERT(nameInsertionQueue.FileNameOfFront() == expectedEnumeratedItem);
            nameInsertionQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == nameInsertionQueue.EnumerationStatus());
    }
}
