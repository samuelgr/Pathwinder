/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file FilesystemRuleTest.cpp
 *   Unit tests for filesystem rule objects.
 *****************************************************************************/

#include "FilesystemRule.h"
#include "TemporaryBuffer.h"
#include "TestCase.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>


namespace PathwinderTest
{
    using namespace ::Pathwinder;


    // -------- TEST CASES ------------------------------------------------- //

    // Verifies that origin and target directory strings are parsed correctly into origin and target full paths and names.
    TEST_CASE(FilesystemRule_GetOriginAndTargetDirectories)
    {
        constexpr std::pair<std::pair<std::wstring_view, std::wstring_view>, std::pair<std::wstring_view, std::wstring_view>> kDirectoryTestRecords[] = {
            {{L"C:\\Directory", L"Directory"}, {L"D:\\Some Other Directory", L"Some Other Directory"}},
            {{L"C:", L"C:"}, {L"D:", L"D:"}},
            {{L"\\sharepath\\shared folder$\\another shared folder", L"another shared folder"}, {L"D:\\Long\\Sub Directory \\   Path To Directory\\Yes", L"Yes"}}
        };

        for (const auto& kDirectoryTestRecord : kDirectoryTestRecords)
        {
            const std::wstring_view kExpectedOriginDirectoryFullPath = kDirectoryTestRecord.first.first;
            const std::wstring_view kExpectedOriginDirectoryName = kDirectoryTestRecord.first.second;
            const std::wstring_view kExpectedTargetDirectoryFullPath = kDirectoryTestRecord.second.first;
            const std::wstring_view kExpectedTargetDirectoryName = kDirectoryTestRecord.second.second;

            const FilesystemRule kFilesystemRule(kExpectedOriginDirectoryFullPath, kExpectedTargetDirectoryFullPath);

            const std::wstring_view kActualOriginDirectoryFullPath = kFilesystemRule.GetOriginDirectoryFullPath();
            TEST_ASSERT(kActualOriginDirectoryFullPath == kExpectedOriginDirectoryFullPath);

            const std::wstring_view kActualOriginDirectoryName = kFilesystemRule.GetOriginDirectoryName();
            TEST_ASSERT(kActualOriginDirectoryName == kExpectedOriginDirectoryName);

            const std::wstring_view kActualTargetDirectoryFullPath = kFilesystemRule.GetTargetDirectoryFullPath();
            TEST_ASSERT(kActualTargetDirectoryFullPath == kExpectedTargetDirectoryFullPath);

            const std::wstring_view kActualTargetDirectoryName = kFilesystemRule.GetTargetDirectoryName();
            TEST_ASSERT(kActualTargetDirectoryName == kExpectedTargetDirectoryName);
        }
    }

    // Verifies that origin and target directory strings are parsed correctly and their immediate parent directories are returned.
    TEST_CASE(FilesystemRule_GetOriginAndTargetDirectoryParents)
    {
        constexpr std::pair<std::pair<std::wstring_view, std::wstring_view>, std::pair<std::wstring_view, std::wstring_view>> kDirectoryTestRecords[] = {
            {{L"C:\\Directory", L"C:"}, {L"D:\\Some Other Directory", L"D:"}},
            {{L"C:", L""}, {L"D:", L""}},
            {{L"\\sharepath\\shared folder$\\another shared folder", L"\\sharepath\\shared folder$"}, {L"D:\\Long\\Sub Directory \\   Path To Directory\\Yes", L"D:\\Long\\Sub Directory \\   Path To Directory"}}
        };

        for (const auto& kDirectoryTestRecord : kDirectoryTestRecords)
        {
            const std::wstring_view kExpectedOriginDirectoryParent = kDirectoryTestRecord.first.second;
            const std::wstring_view kExpectedTargetDirectoryParent = kDirectoryTestRecord.second.second;

            const FilesystemRule kFilesystemRule(kDirectoryTestRecord.first.first, kDirectoryTestRecord.second.first);

            const std::wstring_view kActualOriginDirectoryParent = kFilesystemRule.GetOriginDirectoryParent();
            TEST_ASSERT(kActualOriginDirectoryParent == kExpectedOriginDirectoryParent);

            const std::wstring_view kActualTargetDirectoryParent = kFilesystemRule.GetTargetDirectoryParent();
            TEST_ASSERT(kActualTargetDirectoryParent == kExpectedTargetDirectoryParent);
        }
    }

    // Verifies that paths are successfully redirected in the nominal case of straightforward absolute paths.
    // In this test both forward and backward redirection is exercised.
    TEST_CASE(FilesystemRule_RedirectPath_Nominal)
    {
        constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
        constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

        constexpr std::wstring_view kTestFiles[] = {
            L"File1",
            L".file2",
            L"FILE3.BIN"
        };

        const FilesystemRule kFilesystemRule(kOriginDirectory, kTargetDirectory);

        for (const auto& kTestFile : kTestFiles)
        {
            TemporaryString expectedOutputPath = kTargetDirectory;
            expectedOutputPath << L'\\' << kTestFile;

            std::optional<TemporaryString> actualOutputPath = kFilesystemRule.RedirectPathOriginToTarget(kOriginDirectory, kTestFile.data());
            TEST_ASSERT(true == actualOutputPath.has_value());
            TEST_ASSERT(actualOutputPath.value() == expectedOutputPath);
        }

        for (const auto& kTestFile : kTestFiles)
        {
            TemporaryString expectedOutputPath = kOriginDirectory;
            expectedOutputPath << L'\\' << kTestFile;

            std::optional<TemporaryString> actualOutputPath = kFilesystemRule.RedirectPathTargetToOrigin(kTargetDirectory, kTestFile.data());
            TEST_ASSERT(true == actualOutputPath.has_value());
            TEST_ASSERT(actualOutputPath.value() == expectedOutputPath);
        }
    }

    // Verifies that paths are successfully redirected when the file part matches a pattern and left alone when the file part does not match a pattern.
    TEST_CASE(FilesystemRule_RedirectPath_FilePattern)
    {
        constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
        constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";
        std::vector<std::wstring_view> kFilePatterns = {L"ASDF*", L"?gh.jkl"};

        constexpr std::wstring_view kTestFilesMatching[] = {
            L"ASDF",
            L"ASDFGHJKL",
            L"_gh.jkl",
            L"ggh.jkl"
        };

        constexpr std::wstring_view kTestFilesNotMatching[] = {
            L"",
            L"    ASDF",
            L"gh.jkl",
            L"A",
            L"test.file"
        };

        const FilesystemRule kFilesystemRule(kOriginDirectory, kTargetDirectory, std::move(kFilePatterns));
        
        for (const auto& kTestFile : kTestFilesMatching)
        {
            TemporaryString expectedOutputPath = kTargetDirectory;
            expectedOutputPath << L'\\' << kTestFile;

            std::optional<TemporaryString> actualOutputPath = kFilesystemRule.RedirectPathOriginToTarget(kOriginDirectory, kTestFile.data());
            TEST_ASSERT(true == actualOutputPath.has_value());
            TEST_ASSERT(actualOutputPath.value() == expectedOutputPath);
        }

        for (const auto& kTestFile : kTestFilesNotMatching)
        {
            TEST_ASSERT(false == kFilesystemRule.RedirectPathOriginToTarget(kOriginDirectory, kTestFile.data()).has_value());
        }
    }

    // Verifies that directories that are equal to a directory associated with a filesystem rule are correctly identified and that routing to either origin or target directories is correct.
    // This test compares with both origin and target directories.
    TEST_CASE(FilesystemRule_DirectoryCompare_Equal)
    {
        constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
        constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

        const FilesystemRule kFilesystemRule(kOriginDirectory, kTargetDirectory);
        
        TEST_ASSERT(FilesystemRule::EDirectoryCompareResult::Equal == kFilesystemRule.DirectoryCompareWithOrigin(kOriginDirectory));
        TEST_ASSERT(FilesystemRule::EDirectoryCompareResult::Unrelated == kFilesystemRule.DirectoryCompareWithTarget(kOriginDirectory));

        TEST_ASSERT(FilesystemRule::EDirectoryCompareResult::Unrelated == kFilesystemRule.DirectoryCompareWithOrigin(kTargetDirectory));
        TEST_ASSERT(FilesystemRule::EDirectoryCompareResult::Equal == kFilesystemRule.DirectoryCompareWithTarget(kTargetDirectory));
    }

    // Verifies that directories that are children or descendants of a directory associated with a filesystem rule are correctly identified as such.
    // This test compares with the origin directory.
    TEST_CASE(FilesystemRule_DirectoryCompare_CandidateIsChildOrDescendant)
    {
        constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
        constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

        constexpr std::pair<std::wstring_view, FilesystemRule::EDirectoryCompareResult> kDirectoryTestRecords[] = {
            {L"C:\\Directory\\Origin\\Subdir", FilesystemRule::EDirectoryCompareResult::CandidateIsChild},
            {L"C:\\Directory\\Origin\\Sub Directory 2", FilesystemRule::EDirectoryCompareResult::CandidateIsChild},
            {L"C:\\Directory\\Origin\\Sub Directory 2\\Subdir3\\Subdir4", FilesystemRule::EDirectoryCompareResult::CandidateIsDescendant}
        };

        const FilesystemRule kFilesystemRule(kOriginDirectory, kTargetDirectory);
        
        for (const auto& kDirectoryTestRecord : kDirectoryTestRecords)
            TEST_ASSERT(kDirectoryTestRecord.second == kFilesystemRule.DirectoryCompareWithOrigin(kDirectoryTestRecord.first));
    }

    // Verifies that directories that are parents or ancestors of a directory associated with a filesystem rule are correctly identified as such.
    // This test compares with the target directory.
    TEST_CASE(FilesystemRule_DirectoryCompare_CandidateIsParentOrAncestor)
    {
        constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
        constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

        constexpr std::pair<std::wstring_view, FilesystemRule::EDirectoryCompareResult> kDirectoryTestRecords[] = {
            {L"D:", FilesystemRule::EDirectoryCompareResult::CandidateIsAncestor},
            {L"D:\\AnotherDirectory", FilesystemRule::EDirectoryCompareResult::CandidateIsParent}
        };

        const FilesystemRule kFilesystemRule(kOriginDirectory, kTargetDirectory);
        
        for (const auto& kDirectoryTestRecord : kDirectoryTestRecords)
            TEST_ASSERT(kDirectoryTestRecord.second == kFilesystemRule.DirectoryCompareWithTarget(kDirectoryTestRecord.first));
    }

    // Verifies that directories that are unrelated to a directory associated with a filesystem rule are correctly identified as such.
    // This test compares with both origin and target directories.
    TEST_CASE(FilesystemRule_DirectoryCompare_Unrelated)
    {
        constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
        constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

        constexpr std::wstring_view kDirectories[] = {
            L"",
            L"C:\\Dir",
            L"C:\\Directory\\Origin2"
            L"C:\\Directory\\Orig",
            L"D:\\Another",
            L"D:\\AnotherDirectory\\Target234"
        };

        const FilesystemRule kFilesystemRule(kOriginDirectory, kTargetDirectory);
        
        for (const auto& kDirectory : kDirectories)
        {
            TEST_ASSERT(FilesystemRule::EDirectoryCompareResult::Unrelated == kFilesystemRule.DirectoryCompareWithOrigin(kDirectory));
            TEST_ASSERT(FilesystemRule::EDirectoryCompareResult::Unrelated == kFilesystemRule.DirectoryCompareWithTarget(kDirectory));
        }
    }
}
