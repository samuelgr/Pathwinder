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
#include "FileInformationStruct.h"
#include "FilesystemInstruction.h"
#include "FilesystemRule.h"
#include "InProgressDirectoryEnumeration.h"
#include "MockDirectoryOperationQueue.h"
#include "MockFilesystemOperations.h"
#include "TemporaryBuffer.h"

#include <set>
#include <string>
#include <string_view>


namespace PathwinderTest
{
    using namespace ::Pathwinder;


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Generates and returns a filesystem rule that is intended to function as a file pattern source.
    /// For these tests the origin and target directories are not useful.
    /// @param [in] filePattern File pattern to include with the rule.
    /// @return Filesystem rule object with the specified file pattern.
    static inline FilesystemRule CreateFilePatternSourceRule(std::wstring_view filePattern)
    {
        return FilesystemRule(L"", L"", {std::wstring(filePattern)});
    }

    /// Generates and returns a single directory enumeration instruction that basically acts as a no-op and includes all filenames.
    /// For these tests the directory path source is not useful.
    /// @return Single directory enumeration instruction that includes all filenames.
    static inline DirectoryEnumerationInstruction::SingleDirectoryEnumeration InstructionToIncludeAllFiles(void)
    {
        return DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::None);
    }

    /// Generates and returns a single directory enumeration instruction that includes only those filenames that match a file pattern associated with the specified rule.
    /// For these tests the directory path source is not useful.
    /// @return Single directory enumeration instruction that includes only those files that match the specified filesystem rule's file patterns.
    static inline DirectoryEnumerationInstruction::SingleDirectoryEnumeration InstructionToIncludeMatchingFiles(const FilesystemRule& filePatternSource)
    {
        return DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeOnlyMatchingFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::None, filePatternSource);
    }

    /// Generates and returns a single directory enumeration instruction that includes only those filenames that do not match a file pattern associated with the specified rule.
    /// For these tests the directory path source is not useful.
    /// @return Single directory enumeration instruction that includes only those files that do not match the specified filesystem rule's file patterns.
    static inline DirectoryEnumerationInstruction::SingleDirectoryEnumeration InstructionToExcludeMatchingFiles(const FilesystemRule& filePatternSource)
    {
        return DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllExceptMatchingFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::None, filePatternSource);
    }


    // -------- TEST CASES ------------------------------------------------- //

    // Creates a directory with a small number of files and expects that they are all enumerated.
    TEST_CASE(EnumerationQueue_EnumerateAllFiles)
    {
        constexpr std::wstring_view kDirectoryName = L"C:\\Directory";
        constexpr std::wstring_view kFileNames[] = {
            L"asdf.txt",
            L"File1.txt",
            L"File2.txt",
            L"File3.txt",
            L"File4.txt",
            L"File5.txt",
            L"zZz.txt"
        };

        MockFilesystemOperations mockFilesystem;
        for (auto fileName : kFileNames)
        {
            TemporaryString fileAbsolutePath;
            fileAbsolutePath << kDirectoryName << L'\\' << fileName;
            mockFilesystem.AddFile(fileAbsolutePath.AsStringView());
        }

        EnumerationQueue enumerationQueue(InstructionToIncludeAllFiles(), L"C:\\Directory", SFileNamesInformation::kFileInformationClass);

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
            L"asdf.txt",
            L"File1.txt",
            L"File2.txt",
            L"File3.txt",
            L"File4.txt",
            L"File5.txt",
            L"zZz.txt"
        };

        MockFilesystemOperations mockFilesystem;
        for (auto fileName : kFileNames)
        {
            TemporaryString fileAbsolutePath;
            fileAbsolutePath << kDirectoryName << L'\\' << fileName;
            mockFilesystem.AddFile(fileAbsolutePath.AsStringView());
        }

        EnumerationQueue enumerationQueue(InstructionToIncludeAllFiles(), L"C:\\Directory", SFileNamesInformation::kFileInformationClass);

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

    // Creates a directory with a small number of files and expects that only files that match the file pattern, supplied as part of the original query, are enumerated.
    TEST_CASE(EnumerationQueue_EnumerateOnlyQueryMatchingFiles)
    {
        constexpr std::wstring_view kQueryFilePattern = L"*.txt";
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
            L"File0.log",
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

        EnumerationQueue enumerationQueue(InstructionToIncludeAllFiles(), L"C:\\Directory", SFileNamesInformation::kFileInformationClass, kQueryFilePattern);

        for (auto fileName : kMatchingFileNames)
        {
            TEST_ASSERT(NT_SUCCESS(enumerationQueue.EnumerationStatus()));
            TEST_ASSERT(enumerationQueue.FileNameOfFront() == fileName);
            enumerationQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerationQueue.EnumerationStatus());
    }

    // Creates a directory with a small number of files, enumerates all of them, and then restarts the scan but with a different file pattern.
    // Both file patterns should be honored.
    TEST_CASE(EnumerationQueue_EnumerateWithDifferentFilePatternOnRestart)
    {
        constexpr std::wstring_view kDirectoryName = L"C:\\Directory";
        constexpr std::wstring_view kFileNames[] = {
            L"asdfZ.txt",
            L"File1.txt",
            L"File2.txt",
            L"File3.txt",
            L"File4.txt",
            L"File5.txt",
            L"zZz.txt"
        };

        constexpr std::wstring_view kFirstFilePattern = L"F*";
        constexpr std::wstring_view kFirstFilePatternMatches[] = {
            L"File1.txt",
            L"File2.txt",
            L"File3.txt",
            L"File4.txt",
            L"File5.txt",
        };

        constexpr std::wstring_view kSecondFilePattern = L"*z.txt";
        constexpr std::wstring_view kSecondFilePatternMatches[] = {
            L"asdfZ.txt",
            L"zZz.txt"
        };

        MockFilesystemOperations mockFilesystem;
        for (auto fileName : kFileNames)
        {
            TemporaryString fileAbsolutePath;
            fileAbsolutePath << kDirectoryName << L'\\' << fileName;
            mockFilesystem.AddFile(fileAbsolutePath.AsStringView());
        }

        EnumerationQueue enumerationQueue(InstructionToIncludeAllFiles(), L"C:\\Directory", SFileNamesInformation::kFileInformationClass, kFirstFilePattern);

        for (auto fileName : kFirstFilePatternMatches)
        {
            TEST_ASSERT(NT_SUCCESS(enumerationQueue.EnumerationStatus()));
            TEST_ASSERT(enumerationQueue.FileNameOfFront() == fileName);
            enumerationQueue.PopFront();
        }

        enumerationQueue.Restart(kSecondFilePattern);

        for (auto fileName : kSecondFilePatternMatches)
        {
            TEST_ASSERT(NT_SUCCESS(enumerationQueue.EnumerationStatus()));
            TEST_ASSERT(enumerationQueue.FileNameOfFront() == fileName);
            enumerationQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerationQueue.EnumerationStatus());
    }

    // Creates a directory with a small number of files and expects that only files that match the file pattern, supplied in a filesystem rule, are enumerated.
    TEST_CASE(EnumerationQueue_EnumerateOnlyRuleMatchingFiles)
    {
        constexpr std::wstring_view kRuleFilePattern = L"File*";
        constexpr std::wstring_view kDirectoryName = L"C:\\Directory";
        constexpr std::wstring_view kMatchingFileNames[] = {
            L"File0.log",
            L"File1.txt",
            L"File2.txt",
            L"File3.txt",
            L"File4.txt",
            L"File5.txt",
        };
        constexpr std::wstring_view kNonMatchingFileNames[] = {
            L"asdf.txt",
            L"SomeOtherFile.bin",
            L"Program.exe"
            L"zZz.txt"
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

        FilesystemRule filePatternSource = CreateFilePatternSourceRule(kRuleFilePattern);
        EnumerationQueue enumerationQueue(InstructionToIncludeMatchingFiles(filePatternSource), L"C:\\Directory", SFileNamesInformation::kFileInformationClass);

        for (auto fileName : kMatchingFileNames)
        {
            TEST_ASSERT(NT_SUCCESS(enumerationQueue.EnumerationStatus()));
            TEST_ASSERT(enumerationQueue.FileNameOfFront() == fileName);
            enumerationQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerationQueue.EnumerationStatus());
    }

    // Creates a directory with a small number of files and expects that only files that match the file patterns, supplied both via a filesystem rule and via query, are enumerated.
    TEST_CASE(EnumerationQueue_EnumerateOnlyRuleAndQueryMatchingFiles)
    {
        constexpr std::wstring_view kQueryFilePattern = L"*.txt";
        constexpr std::wstring_view kRuleFilePattern = L"File*";
        constexpr std::wstring_view kDirectoryName = L"C:\\Directory";
        constexpr std::wstring_view kMatchingFileNames[] = {
            L"File1.txt",
            L"File2.txt",
            L"File3.txt",
            L"File4.txt",
            L"File5.txt",
        };
        constexpr std::wstring_view kNonMatchingFileNames[] = {
            L"File0.log",
            L"asdf.txt",
            L"SomeOtherFile.bin",
            L"Program.exe"
            L"zZz.txt"
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

        FilesystemRule filePatternSource = CreateFilePatternSourceRule(kRuleFilePattern);
        EnumerationQueue enumerationQueue(InstructionToIncludeMatchingFiles(filePatternSource), L"C:\\Directory", SFileNamesInformation::kFileInformationClass, kQueryFilePattern);

        for (auto fileName : kMatchingFileNames)
        {
            TEST_ASSERT(NT_SUCCESS(enumerationQueue.EnumerationStatus()));
            TEST_ASSERT(enumerationQueue.FileNameOfFront() == fileName);
            enumerationQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerationQueue.EnumerationStatus());
    }

    // Creates a directory with a small number of files and expects that none are enumerated due to no matches with the file pattern supplied within a filesystem rule.
    // In this case the instruction specifies to include all matching files, so the file pattern is one that does not match.
    TEST_CASE(EnumerationQueue_NothingMatchesInclusiveRuleFilePattern)
    {
        constexpr std::wstring_view kRuleFilePattern = L"*.exe";
        constexpr std::wstring_view kDirectoryName = L"C:\\Directory";
        constexpr std::wstring_view kFileNames[] = {
            L"asdf.txt",
            L"File1.txt",
            L"File2.txt",
            L"File3.txt",
            L"File4.txt",
            L"File5.txt",
            L"zZz.txt"
        };

        MockFilesystemOperations mockFilesystem;
        for (auto fileName : kFileNames)
        {
            TemporaryString fileAbsolutePath;
            fileAbsolutePath << kDirectoryName << L'\\' << fileName;
            mockFilesystem.AddFile(fileAbsolutePath.AsStringView());
        }

        FilesystemRule filePatternSource = CreateFilePatternSourceRule(kRuleFilePattern);
        EnumerationQueue enumerationQueue(InstructionToIncludeMatchingFiles(filePatternSource), L"C:\\Directory", SFileNamesInformation::kFileInformationClass);
        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerationQueue.EnumerationStatus());
    }

    // Creates a directory with a small number of files and expects that none are enumerated due to no matches with the file pattern supplied within a filesystem rule.
    // In this case the instruction specifies to exclude all matching files, so the file pattern is one that does match.
    TEST_CASE(EnumerationQueue_EverythingMatchesExclusiveRuleFilePattern)
    {
        constexpr std::wstring_view kRuleFilePattern = L"*.txt";
        constexpr std::wstring_view kDirectoryName = L"C:\\Directory";
        constexpr std::wstring_view kFileNames[] = {
            L"asdf.txt",
            L"File1.txt",
            L"File2.txt",
            L"File3.txt",
            L"File4.txt",
            L"File5.txt",
            L"zZz.txt"
        };

        MockFilesystemOperations mockFilesystem;
        for (auto fileName : kFileNames)
        {
            TemporaryString fileAbsolutePath;
            fileAbsolutePath << kDirectoryName << L'\\' << fileName;
            mockFilesystem.AddFile(fileAbsolutePath.AsStringView());
        }

        FilesystemRule filePatternSource = CreateFilePatternSourceRule(kRuleFilePattern);
        EnumerationQueue enumerationQueue(InstructionToExcludeMatchingFiles(filePatternSource), L"C:\\Directory", SFileNamesInformation::kFileInformationClass);
        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerationQueue.EnumerationStatus());
    }

    // Attempts to enumerate an empty directory. This should succeed but return no files.
    TEST_CASE(EnumerationQueue_EnumerateEmptyDirectory)
    {
        constexpr std::wstring_view kDirectoryName = L"C:\\Directory";

        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(kDirectoryName);

        EnumerationQueue enumerationQueue(InstructionToIncludeAllFiles(), L"C:\\Directory", SFileNamesInformation::kFileInformationClass);
        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerationQueue.EnumerationStatus());
    }

    // Attempts to enumerate a directory that does not exist. This should result in an error code prior to enumeration.
    TEST_CASE(EnumerationQueue_EnumerateNonExistentDirectory)
    {
        MockFilesystemOperations mockFilesystem;
        EnumerationQueue enumerationQueue(InstructionToIncludeAllFiles(), L"C:\\Directory", SFileNamesInformation::kFileInformationClass);
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

    // Creates two directory enumeration queues and verifies that they are correctly merged, with output properly being provided in sorted order.
    TEST_CASE(MergedFileInformationQueue_SimpleMergeTwo_Nominal)
    {
        FileInformationStructLayout layout = *FileInformationStructLayout::LayoutForFileInformationClass(SFileNamesInformation::kFileInformationClass);

        const MockDirectoryOperationQueue::TFileNamesToEnumerate kFileNamesFirstQueue = {
            L"File10.txt",
            L"File20.txt",
            L"File30.txt",
            L"File40.txt",
            L"File70.txt"
        };
        auto firstQueue = std::make_unique<MockDirectoryOperationQueue>(layout, MockDirectoryOperationQueue::TFileNamesToEnumerate(kFileNamesFirstQueue));

        const MockDirectoryOperationQueue::TFileNamesToEnumerate kFileNamesSecondQueue = {
            L"File18.txt",
            L"File35.txt",
            L"File50.txt",
            L"File67.txt"
        };
        auto secondQueue = std::make_unique<MockDirectoryOperationQueue>(layout, MockDirectoryOperationQueue::TFileNamesToEnumerate(kFileNamesSecondQueue));

        MockDirectoryOperationQueue::TFileNamesToEnumerate combinedFileNamesSorted;
        for (const auto& filename : kFileNamesFirstQueue)
            combinedFileNamesSorted.insert(filename);
        for (const auto& filename : kFileNamesSecondQueue)
            combinedFileNamesSorted.insert(filename);

        MergedFileInformationQueue mergedQueue({std::move(firstQueue), std::move(secondQueue)});

        for (const auto& fileName : combinedFileNamesSorted)
        {
            TEST_ASSERT(NT_SUCCESS(mergedQueue.EnumerationStatus()));
            TEST_ASSERT(mergedQueue.FileNameOfFront() == fileName);
            mergedQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == mergedQueue.EnumerationStatus());
    }

    // Creates two directory enumeration queues and verifies that they are correctly merged, with output properly being provided in sorted order.
    // In this case one queue has entities that all come before entities in the other and hence the queues are drained one at a time.
    TEST_CASE(MergedFileInformationQueue_SimpleMergeTwo_OneQueueDrainsCompletelyFirst)
    {
        FileInformationStructLayout layout = *FileInformationStructLayout::LayoutForFileInformationClass(SFileNamesInformation::kFileInformationClass);

        const MockDirectoryOperationQueue::TFileNamesToEnumerate kFileNamesFirstQueue = {
            L"File10.txt",
            L"File20.txt",
            L"File30.txt",
            L"File40.txt",
            L"File70.txt"
        };
        auto firstQueue = std::make_unique<MockDirectoryOperationQueue>(layout, MockDirectoryOperationQueue::TFileNamesToEnumerate(kFileNamesFirstQueue));

        const MockDirectoryOperationQueue::TFileNamesToEnumerate kFileNamesSecondQueue = {
            L"File01.txt",
            L"File02.txt",
            L"File03.txt",
            L"File04.txt",
            L"File05.txt",
            L"File06.txt",
            L"File07.txt",
            L"File08.txt",
            L"File09.txt",
        };
        auto secondQueue = std::make_unique<MockDirectoryOperationQueue>(layout, MockDirectoryOperationQueue::TFileNamesToEnumerate(kFileNamesSecondQueue));

        MockDirectoryOperationQueue::TFileNamesToEnumerate combinedFileNamesSorted;
        for (const auto& filename : kFileNamesFirstQueue)
            combinedFileNamesSorted.insert(filename);
        for (const auto& filename : kFileNamesSecondQueue)
            combinedFileNamesSorted.insert(filename);

        MergedFileInformationQueue mergedQueue({std::move(firstQueue), std::move(secondQueue)});

        for (const auto& fileName : combinedFileNamesSorted)
        {
            TEST_ASSERT(NT_SUCCESS(mergedQueue.EnumerationStatus()));
            TEST_ASSERT(mergedQueue.FileNameOfFront() == fileName);
            mergedQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == mergedQueue.EnumerationStatus());
    }

    // Creates two directory enumeration queues and verifies that they are correctly merged, with output properly being provided in sorted order.
    // The scan is restarted after getting part-way through it. After the restart all the files should be enumerated.
    TEST_CASE(MergedFileInformationQueue_SimpleMergeTwo_WithRestart)
    {
        FileInformationStructLayout layout = *FileInformationStructLayout::LayoutForFileInformationClass(SFileNamesInformation::kFileInformationClass);

        const MockDirectoryOperationQueue::TFileNamesToEnumerate kFileNamesFirstQueue = {
            L"File10.txt",
            L"File20.txt",
            L"File30.txt",
            L"File40.txt",
            L"File70.txt"
        };
        auto firstQueue = std::make_unique<MockDirectoryOperationQueue>(layout, MockDirectoryOperationQueue::TFileNamesToEnumerate(kFileNamesFirstQueue));

        const MockDirectoryOperationQueue::TFileNamesToEnumerate kFileNamesSecondQueue = {
            L"File18.txt",
            L"File35.txt",
            L"File50.txt",
            L"File67.txt"
        };
        auto secondQueue = std::make_unique<MockDirectoryOperationQueue>(layout, MockDirectoryOperationQueue::TFileNamesToEnumerate(kFileNamesSecondQueue));

        MockDirectoryOperationQueue::TFileNamesToEnumerate combinedFileNamesSorted;
        for (const auto& filename : kFileNamesFirstQueue)
            combinedFileNamesSorted.insert(filename);
        for (const auto& filename : kFileNamesSecondQueue)
            combinedFileNamesSorted.insert(filename);

        MergedFileInformationQueue mergedQueue({std::move(firstQueue), std::move(secondQueue)});

        for (int i = 0; i < combinedFileNamesSorted.size() - 2; ++i)
        {
            TEST_ASSERT(NT_SUCCESS(mergedQueue.EnumerationStatus()));
            mergedQueue.PopFront();
        }

        mergedQueue.Restart();

        for (const auto& fileName : combinedFileNamesSorted)
        {
            TEST_ASSERT(NT_SUCCESS(mergedQueue.EnumerationStatus()));
            TEST_ASSERT(mergedQueue.FileNameOfFront() == fileName);
            mergedQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == mergedQueue.EnumerationStatus());
    }

    // Creates two directory enumeration queues and verifies that they are correctly merged, with output properly being provided in sorted order.
    // The scan is restarted after getting part-way through it, and the file pattern is changed on restart. Both file patterns should be honored.
    // This test uses a standard directory enumeration queue, rather than a mock, because the former supports query file patterns and this test is intended to ensure that query file patterns are correctly routed to underlying queue objects.
    TEST_CASE(MergedFileInformationQueue_SimpleMergeTwo_DifferentFilePatternOnRestart)
    {
        constexpr FILE_INFORMATION_CLASS kFileInformationClass = SFileNamesInformation::kFileInformationClass;
        FileInformationStructLayout layout = *FileInformationStructLayout::LayoutForFileInformationClass(kFileInformationClass);

        constexpr std::wstring_view kFirstDirectoryName = L"C:\\FirstDirectory";
        constexpr std::wstring_view kFirstDirectoryFileNames[] = {
            L"File10.txt",
            L"File20.txt",
            L"File30.txt",
            L"File40.txt",
            L"File70.txt",
            L"File79.txt",
        };

        constexpr std::wstring_view kSecondDirectoryName = L"C:\\SecondDirectory";
        constexpr std::wstring_view kSecondDirectoryFileNames[] = {
            L"File18.txt",
            L"File35.txt",
            L"File50.txt",
            L"File67.txt",
            L"File77.txt",
            L"File78.txt",
        };

        MockFilesystemOperations mockFilesystem;
        for (auto fileName : kFirstDirectoryFileNames)
        {
            TemporaryString fileAbsolutePath;
            fileAbsolutePath << kFirstDirectoryName << L'\\' << fileName;
            mockFilesystem.AddFile(fileAbsolutePath.AsStringView());
        }
        for (auto fileName : kSecondDirectoryFileNames)
        {
            TemporaryString fileAbsolutePath;
            fileAbsolutePath << kSecondDirectoryName << L'\\' << fileName;
            mockFilesystem.AddFile(fileAbsolutePath.AsStringView());
        }

        constexpr std::wstring_view kFirstFilePattern = L"*0.txt";
        constexpr std::wstring_view kFirstFilePatternMatchesInSortedOrder[] = {
            L"File10.txt",
            L"File20.txt",
            L"File30.txt",
            L"File40.txt",
            L"File50.txt",
            L"File70.txt",
        };

        constexpr std::wstring_view kSecondFilePattern = L"File7*.txt";
        constexpr std::wstring_view kSecondFilePatternMatchesInSortedOrder[] = {
            L"File70.txt",
            L"File77.txt",
            L"File78.txt",
            L"File79.txt",
        };

        auto firstQueue = std::make_unique<EnumerationQueue>(InstructionToIncludeAllFiles(), kFirstDirectoryName, kFileInformationClass, kFirstFilePattern);
        auto secondQueue = std::make_unique<EnumerationQueue>(InstructionToIncludeAllFiles(), kSecondDirectoryName, kFileInformationClass, kFirstFilePattern);
        
        MergedFileInformationQueue mergedQueue({std::move(firstQueue), std::move(secondQueue)});

        for (auto fileName : kFirstFilePatternMatchesInSortedOrder)
        {
            TEST_ASSERT(NT_SUCCESS(mergedQueue.EnumerationStatus()));
            TEST_ASSERT(mergedQueue.FileNameOfFront() == fileName);
            mergedQueue.PopFront();
        }

        mergedQueue.Restart(kSecondFilePattern);

        for (auto fileName : kSecondFilePatternMatchesInSortedOrder)
        {
            TEST_ASSERT(NT_SUCCESS(mergedQueue.EnumerationStatus()));
            TEST_ASSERT(mergedQueue.FileNameOfFront() == fileName);
            mergedQueue.PopFront();
        }

        TEST_ASSERT(NtStatus::kNoMoreFiles == mergedQueue.EnumerationStatus());
    }

    // Verifies that a merged file information queue correctly reports that the enumeration is in progress if at least one underlying queue reports the same.
    // None of the underlying queues report error conditions. They either report "enumeration in progress" or "enumeration done."
    TEST_CASE(MergedFileInformationQueue_EnumerationStatus_EnumerationInProgress)
    {
        constexpr std::pair<NTSTATUS, NTSTATUS> kEnumerationStatusRecords[] = {
            {Pathwinder::NtStatus::kMoreEntries, Pathwinder::NtStatus::kMoreEntries},
            {Pathwinder::NtStatus::kMoreEntries, Pathwinder::NtStatus::kNoMoreFiles},
            {Pathwinder::NtStatus::kNoMoreFiles, Pathwinder::NtStatus::kMoreEntries},
        };

        constexpr NTSTATUS expectedEnumerationStatus = Pathwinder::NtStatus::kMoreEntries;

        for (const auto& enumerationStatusRecord : kEnumerationStatusRecords)
        {
            MergedFileInformationQueue mergedQueue({
                std::make_unique<MockDirectoryOperationQueue>(enumerationStatusRecord.first),
                std::make_unique<MockDirectoryOperationQueue>(enumerationStatusRecord.second)
            });

            const NTSTATUS actualEnumerationStatus = mergedQueue.EnumerationStatus();
            TEST_ASSERT(actualEnumerationStatus == expectedEnumerationStatus);
        }
    }

    // Verifies that a merged file information queue correctly reports that the enumeration is completed when all of the underlying queues report the same.
    TEST_CASE(MergedFileInformationQueue_EnumerationStatus_EnumerationComplete)
    {
        constexpr std::pair<NTSTATUS, NTSTATUS> kEnumerationStatusRecords[] = {
            {Pathwinder::NtStatus::kNoMoreFiles, Pathwinder::NtStatus::kNoMoreFiles},
        };

        constexpr NTSTATUS expectedEnumerationStatus = Pathwinder::NtStatus::kNoMoreFiles;

        for (const auto& enumerationStatusRecord : kEnumerationStatusRecords)
        {
            MergedFileInformationQueue mergedQueue({
                std::make_unique<MockDirectoryOperationQueue>(enumerationStatusRecord.first),
                std::make_unique<MockDirectoryOperationQueue>(enumerationStatusRecord.second)
            });

            const NTSTATUS actualEnumerationStatus = mergedQueue.EnumerationStatus();
            TEST_ASSERT(actualEnumerationStatus == expectedEnumerationStatus);
        }
    }

    // Verifies that a merged file information queue correctly reports an enumeration error if any underlying queue reports the same, regardless of what the other underlying queues report.
    TEST_CASE(MergedFileInformationQueue_EnumerationStatus_EnumerationError)
    {
        constexpr std::pair<NTSTATUS, NTSTATUS> kEnumerationStatusRecords[] = {
            {Pathwinder::NtStatus::kMoreEntries, Pathwinder::NtStatus::kInternalError},
            {Pathwinder::NtStatus::kNoMoreFiles, Pathwinder::NtStatus::kInternalError},
            {Pathwinder::NtStatus::kInternalError, Pathwinder::NtStatus::kMoreEntries},
            {Pathwinder::NtStatus::kInternalError, Pathwinder::NtStatus::kNoMoreFiles},
            {Pathwinder::NtStatus::kInternalError, Pathwinder::NtStatus::kInternalError},
        };

        constexpr NTSTATUS expectedEnumerationStatus = Pathwinder::NtStatus::kInternalError;

        for (const auto& enumerationStatusRecord : kEnumerationStatusRecords)
        {
            MergedFileInformationQueue mergedQueue({
                std::make_unique<MockDirectoryOperationQueue>(enumerationStatusRecord.first),
                std::make_unique<MockDirectoryOperationQueue>(enumerationStatusRecord.second)
            });

            const NTSTATUS actualEnumerationStatus = mergedQueue.EnumerationStatus();
            TEST_ASSERT(actualEnumerationStatus == expectedEnumerationStatus);
        }
    }
}
