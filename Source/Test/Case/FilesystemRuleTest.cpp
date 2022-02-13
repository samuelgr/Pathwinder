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


    // Verifies that valid strings for identifying origin and target directories are accepted as such.
    TEST_CASE(FilesystemRule_IsValidDirectoryString_Valid)
    {
        constexpr std::wstring_view kDirectoryStrings[] = {
            L"C:",
            L"C:\\Directory",
            L"C:\\Program Files (x86)\\Games\\Some Game With A Title",
            L"\\sharepath\\shared folder$\\another shared folder",
            L"C:\\Program Files (x86)\\Games\\Some Game With A Title\\..",
            L"C:\\Program Files (x86)\\Games\\Some Game With A Title\\."
        };

        for (const auto& kDirectoryString : kDirectoryStrings)
            TEST_ASSERT(true == FilesystemRule::IsValidDirectoryString(kDirectoryString));
    }

    // Verifies that invalid strings for identifying origin and target directories are rejected.
    TEST_CASE(FilesystemRule_IsValidDirectoryString_Invalid)
    {
        constexpr std::wstring_view kDirectoryStrings[] = {
            L"",
            L"D:\\",
            L"C:\\Program Files <x86>\\Games\\Some Game With A Title",
            L"\"C:\\Program Files (x86)\\Games\\Some Game With A Title\"",
            L"C:\\Program Files (x86)\\Games\\Some Game With A Title\\",
            L"C:\\Program Files*",
            L"C:\\Program Files (???)",
            L"C:\\Program Files\\*",
            L"C:\\Program Files\t(x86)\\Games\\Some Game With A Title",
            L"C:\\Program Files\n(x86)\\Games\\Some Game With A Title",
            L"C:\\Program Files\b(x86)\\Games\\Some Game With A Title",
        };

        for (const auto& kDirectoryString : kDirectoryStrings)
            TEST_ASSERT(false == FilesystemRule::IsValidDirectoryString(kDirectoryString));
    }

    // Verifies that valid strings for identifying file patterns within an origin or target directory are accepted as such.
    TEST_CASE(FilesystemRule_IsValidFilePatternString_Valid)
    {
        constexpr std::wstring_view kFilePatternStrings[] = {
            L"*",
            L"?",
            L"***????",
            L"data???.sav",
            L"*.bin",
            L".*",
            L"data???.MyGame.MyPublisher.sav"
        };

        for (const auto& kFilePatternString : kFilePatternStrings)
            TEST_ASSERT(true == FilesystemRule::IsValidFilePatternString(kFilePatternString));
    }

    // Verifies that invalid strings for identifying file patterns within an origin or target directory are rejected.
    TEST_CASE(FilesystemRule_IsValidFilePatternString_Invalid)
    {
        constexpr std::wstring_view kFilePatternStrings[] = {
            L"",
            L"data000.sav|data001.sav",
            L"\\*.bin",
            L"C:*.bin"
        };

        for (const auto& kFilePatternString : kFilePatternStrings)
            TEST_ASSERT(false == FilesystemRule::IsValidFilePatternString(kFilePatternString));
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

        const auto kMaybeFilesystemRule = FilesystemRule::Create(kOriginDirectory, kTargetDirectory);
        TEST_ASSERT(true == kMaybeFilesystemRule.HasValue());

        const FilesystemRule& kFilesystemRule = kMaybeFilesystemRule.Value();

        for (const auto& kTestFile : kTestFiles)
        {
            TemporaryString inputPath = kOriginDirectory;
            inputPath << L'\\' << kTestFile;

            TemporaryString expectedOutputPath = kTargetDirectory;
            expectedOutputPath << L'\\' << kTestFile;

            std::optional<TemporaryString> actualOutputPath = kFilesystemRule.RedirectPathOriginToTarget(inputPath.AsCString());
            TEST_ASSERT(true == actualOutputPath.has_value());
            TEST_ASSERT(actualOutputPath.value() == expectedOutputPath);
        }

        for (const auto& kTestFile : kTestFiles)
        {
            TemporaryString inputPath = kTargetDirectory;
            inputPath << L'\\' << kTestFile;

            TemporaryString expectedOutputPath = kOriginDirectory;
            expectedOutputPath << L'\\' << kTestFile;

            std::optional<TemporaryString> actualOutputPath = kFilesystemRule.RedirectPathTargetToOrigin(inputPath.AsCString());
            TEST_ASSERT(true == actualOutputPath.has_value());
            TEST_ASSERT(actualOutputPath.value() == expectedOutputPath);
        }
    }

    // Verifies that paths are successfully redirected when the candidate path contains subdirectories with "." and ".." that still end up matching the origin directory.
    TEST_CASE(FilesystemRule_RedirectPath_RelativePathResolution)
    {
        constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
        constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";
        constexpr std::wstring_view kTestFile = L"test.file";

        constexpr std::wstring_view kInputSubdirectories[] = {
            L"Subdir\\..",
            L".\\.\\.\\Sub Directory\\.\\..\\.",
            L"1\\2\\3\\..\\33\\4\\55\\666\\..\\..\\..\\..\\..\\.."
        };

        const auto kMaybeFilesystemRule = FilesystemRule::Create(kOriginDirectory, kTargetDirectory);
        TEST_ASSERT(true == kMaybeFilesystemRule.HasValue());

        const FilesystemRule& kFilesystemRule = kMaybeFilesystemRule.Value();

        for (const auto& kInputSubdirectory : kInputSubdirectories)
        {
            TemporaryString inputPath = kOriginDirectory;
            inputPath << L'\\' << kInputSubdirectory << L'\\' << kTestFile;

            TemporaryString expectedOutputPath = kTargetDirectory;
            expectedOutputPath << L'\\' << kTestFile;

            std::optional<TemporaryString> actualOutputPath = kFilesystemRule.RedirectPathOriginToTarget(inputPath.AsCString());
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
            L"gh.jkl",
            L"A",
            L"test.file"
        };

        const auto kMaybeFilesystemRule = FilesystemRule::Create(kOriginDirectory, kTargetDirectory, std::move(kFilePatterns));
        TEST_ASSERT(true == kMaybeFilesystemRule.HasValue());

        const FilesystemRule& kFilesystemRule = kMaybeFilesystemRule.Value();

        for (const auto& kTestFile : kTestFilesMatching)
        {
            TemporaryString inputPath = kOriginDirectory;
            inputPath << L'\\' << kTestFile;

            TemporaryString expectedOutputPath = kTargetDirectory;
            expectedOutputPath << L'\\' << kTestFile;

            std::optional<TemporaryString> actualOutputPath = kFilesystemRule.RedirectPathOriginToTarget(inputPath.AsCString());
            TEST_ASSERT(true == actualOutputPath.has_value());
            TEST_ASSERT(actualOutputPath.value() == expectedOutputPath);
        }

        for (const auto& kTestFile : kTestFilesNotMatching)
        {
            TemporaryString inputPath = kOriginDirectory;
            inputPath << L'\\' << kTestFile;

            TEST_ASSERT(false == kFilesystemRule.RedirectPathOriginToTarget(inputPath.AsCString()).has_value());
        }
    }
}
