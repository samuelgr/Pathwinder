/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemDirectorTest.cpp
 *   Unit tests for filesystem redirector objects.
 *****************************************************************************/

#include "FilesystemDirector.h"
#include "FilesystemOperations.h"
#include "MockFilesystemOperations.h"
#include "TemporaryBuffer.h"
#include "TestCase.h"
#include "ValueOrError.h"

#include <string_view>
#include <windows.h>


namespace PathwinderTest
{
    using namespace ::Pathwinder;


    // -------- TEST CASES ------------------------------------------------- //

    // Verifies that valid strings for identifying origin and target directories are accepted as such.
    TEST_CASE(FilesystemDirector_IsValidDirectoryString_Valid)
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
            TEST_ASSERT(true == FilesystemDirector::IsValidDirectoryString(kDirectoryString));
    }

    // Verifies that invalid strings for identifying origin and target directories are rejected.
    TEST_CASE(FilesystemDirector_IsValidDirectoryString_Invalid)
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
            TEST_ASSERT(false == FilesystemDirector::IsValidDirectoryString(kDirectoryString));
    }

    // Verifies that valid strings for identifying file patterns within an origin or target directory are accepted as such.
    TEST_CASE(FilesystemDirector_IsValidFilePatternString_Valid)
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
            TEST_ASSERT(true == FilesystemDirector::IsValidFilePatternString(kFilePatternString));
    }

    // Verifies that invalid strings for identifying file patterns within an origin or target directory are rejected.
    TEST_CASE(FilesystemDirector_IsValidFilePatternString_Invalid)
    {
        constexpr std::wstring_view kFilePatternStrings[] = {
            L"",
            L"data000.sav|data001.sav",
            L"\\*.bin",
            L"C:*.bin"
        };

        for (const auto& kFilePatternString : kFilePatternStrings)
            TEST_ASSERT(false == FilesystemDirector::IsValidFilePatternString(kFilePatternString));
    }

    // Verifies the nominal situation of creating rules that do not overlap and contain no file patterns.
    TEST_CASE(FilesystemDirector_CreateRule_Success_Nominal)
    {
        FilesystemDirector director;
        TEST_ASSERT(director.CreateRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir1").HasValue());
        TEST_ASSERT(director.CreateRule(L"2", L"C:\\OriginDir2", L"C:\\TargetDir2").HasValue());
    }

    // Verifies that non-overlapping filesystem rules can be created with file patterns.
    TEST_CASE(FilesystemDirector_CreateRule_Success_WithFileMasks)
    {
        FilesystemDirector director;
        TEST_ASSERT(director.CreateRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir1", {L"file*.txt", L"*.bin"}).HasValue());
        TEST_ASSERT(director.CreateRule(L"2", L"C:\\OriginDir2", L"C:\\TargetDir2", {L"log*", L"file???.dat"}).HasValue());
    }

    // Verifies that non-overlapping filesystem rules can be created but one of the origin directories is a subdirectory of the other.
    // Three rules are used here with the mid-level rule created first to verify that order does not matter.
    TEST_CASE(FilesystemDirector_CreateRule_Success_OriginIsSubdir)
    {
        FilesystemDirector director;
        TEST_ASSERT(director.CreateRule(L"2", L"C:\\Level1\\Level2", L"C:\\TargetDir2").HasValue());
        TEST_ASSERT(director.CreateRule(L"1", L"C:\\Level1", L"C:\\TargetDir1").HasValue());
        TEST_ASSERT(director.CreateRule(L"3", L"C:\\Level1\\Level2\\Level3", L"C:\\TargetDir3").HasValue());
    }

    // Verifies that rule creation fails when multiple rules have the same name.
    TEST_CASE(FilesystemDirector_CreateRule_Failure_DuplicateRuleName)
    {
        FilesystemDirector director;
        TEST_ASSERT(director.CreateRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir1").HasValue());
        TEST_ASSERT(director.CreateRule(L"1", L"C:\\OriginDir2", L"C:\\TargetDir2").HasError());
    }

    // Verifies that rule creation fails if either the origin directory or the target directory is a filesystem root.
    TEST_CASE(FilesystemDirector_CreateRule_Failure_FilesystemRoot)
    {
        FilesystemDirector director;

        ValueOrError<const FilesystemRule*, TemporaryString> createRuleResults[] = {
            director.CreateRule(L"1", L"C:\\.", L"D:\\RedirectFromC"),
            director.CreateRule(L"2", L"C:\\RedirectToD", L"D:\\."),
            director.CreateRule(L"3", L"C:\\.", L"D:\\.")
        };

        for (auto& createRuleResult : createRuleResults)
        {
            TEST_ASSERT(createRuleResult.HasError());
            TEST_ASSERT(createRuleResult.Error().AsStringView().contains(L"filesystem root"));
        }
    }

    // Verifies that rule creation fails if the origin directory is the same as another rule's origin or target directory.
    TEST_CASE(FilesystemDirector_CreateRule_Failure_OverlappingOrigin)
    {
        FilesystemDirector director;
        TEST_ASSERT(director.CreateRule(L"1", L"C:\\OriginDir", L"C:\\TargetDir1").HasValue());
        TEST_ASSERT(director.CreateRule(L"2", L"C:\\OriginDir", L"C:\\TargetDir2").HasError());
        TEST_ASSERT(director.CreateRule(L"3", L"C:\\OriginDir3", L"C:\\OriginDir").HasError());
    }

    // Verifies that rule creation fails if the target directory is the same as another rule's origin directory.
    TEST_CASE(FilesystemDirector_CreateRule_Failure_OverlappingTarget)
    {
        FilesystemDirector director;
        TEST_ASSERT(director.CreateRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir").HasValue());
        TEST_ASSERT(director.CreateRule(L"2", L"C:\\OriginDir2", L"C:\\OriginDir1").HasError());
    }

    // Verifies that rule set finalization completes successfully in the nominal case of filesystem rules having origin directories that exist and whose parent directories also exist.
    TEST_CASE(FilesystemDirector_Finalize_Success_Nominal)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\OriginDir1");
        mockFilesystem.AddDirectory(L"E:\\OriginDir2");

        FilesystemDirector director;
        TEST_ASSERT(director.CreateRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir").HasValue());
        TEST_ASSERT(director.CreateRule(L"2", L"E:\\OriginDir2", L"E:\\TargetDir2").HasValue());

        auto finalizeResult = director.Finalize();
        TEST_ASSERT(finalizeResult.HasValue());
        TEST_ASSERT(finalizeResult.Value() == 2);
    }

    // Verifies that rule set finalization completes successfully where rules have origin directories whose parents do not exist but themselves are the origin directories of other rules.
    // No rules have any file patterns.
    TEST_CASE(FilesystemDirector_Finalize_Success_OriginHierarchy)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\OriginBase");

        FilesystemDirector director;
        TEST_ASSERT(director.CreateRule(L"1", L"C:\\OriginBase\\OriginSubdir\\Subdir1\\Subdir2", L"C:\\TargetBase\\Target2").HasValue());
        TEST_ASSERT(director.CreateRule(L"2", L"C:\\OriginBase\\OriginSubdir\\Subdir1", L"C:\\TargetBase\\Target1").HasValue());
        TEST_ASSERT(director.CreateRule(L"3", L"C:\\OriginBase\\OriginSubdir", L"C:\\TargetBase\\TargetBase").HasValue());

        auto finalizeResult = director.Finalize();
        TEST_ASSERT(finalizeResult.HasValue());
        TEST_ASSERT(finalizeResult.Value() == 3);
    }

    // Verifies that rule set finalization fails when the origin directory path already exists but is not a directory.
    TEST_CASE(FilesystemDirector_Finalize_Failure_OriginExistsNotAsDirectory)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddFile(L"C:\\OriginDir\\File");

        FilesystemDirector director;
        TEST_ASSERT(director.CreateRule(L"1", L"C:\\OriginDir\\File", L"C:\\TargetDir").HasValue());

        auto finalizeResult = director.Finalize();
        TEST_ASSERT(finalizeResult.HasError());
    }

    // Verifies that rule set finalization fails when the origin directory's parent does not exist in the filesystem or as another origin directory.
    TEST_CASE(FilesystemDirector_Finalize_Failure_OriginParentMissing)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:");

        FilesystemDirector director;
        TEST_ASSERT(director.CreateRule(L"1", L"C:\\OriginDir\\Subdir1", L"C:\\TargetDir\\Subdir1").HasValue());

        auto finalizeResult = director.Finalize();
        TEST_ASSERT(finalizeResult.HasError());
    }
}
