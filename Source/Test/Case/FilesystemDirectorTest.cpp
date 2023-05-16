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
#include "TestCase.h"

#include <string_view>


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
}
