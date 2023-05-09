/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file StringsTest.cpp
 *   Unit tests for functions that manipulate strings.
 *****************************************************************************/

#include "Strings.h"
#include "TemporaryBuffer.h"
#include "TestCase.h"

#include <string_view>


namespace PathwinderTest
{
    using namespace ::Pathwinder;


    // -------- TEST CASES ------------------------------------------------- //

    // The following sequence of tests, which together comprise the Split suite, exercises the SplitString function.

    // Nominal case of a string with delimiters being split into pieces.
    TEST_CASE(Strings_Split_Nominal)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"ABCD%EFGH%IJKL%MNOP%QRSTUV WX Y  % Z  ";
        const TemporaryVector<std::wstring_view> expectedPieces = {L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV WX Y  ", L" Z  "};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Same as the nominal case but with a multi-character delimiter.
    TEST_CASE(Strings_Split_MultiCharacterDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiter = L":::";
        constexpr std::wstring_view kInputString = L"ABCD:::EFGH:::IJKL:::MNOP:::QRSTUV WX Y  ::: Z  ";
        const TemporaryVector<std::wstring_view> expectedPieces = {L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV WX Y  ", L" Z  "};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // No delimiters are present, so the entire string should be returned in one piece.
    TEST_CASE(Strings_Split_NoDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"ABCD EFGH IJKL MNOP QRSTUV WX Y  Z  ";
        const TemporaryVector<std::wstring_view> expectedPieces = {kInputString};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Multiple consecutive delimiters are present, so those pieces should be empty.
    TEST_CASE(Strings_Split_ConsecutiveDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"ABCD%%EFGH%%IJKL%%MNOP%%QRSTUV WX Y  %% Z  ";
        const TemporaryVector<std::wstring_view> expectedPieces = {L"ABCD", L"", L"EFGH", L"", L"IJKL", L"", L"MNOP", L"", L"QRSTUV WX Y  ", L"", L" Z  "};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Multiple consecutive multi-character delimiters are present, so those pieces should be empty.
    TEST_CASE(Strings_Split_ConsecutiveMultiCharacterDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiter = L":::";
        constexpr std::wstring_view kInputString = L"ABCD::::::EFGH::::::IJKL::::::MNOP::::::QRSTUV WX Y  :::::: Z  ";
        const TemporaryVector<std::wstring_view> expectedPieces = {L"ABCD", L"", L"EFGH", L"", L"IJKL", L"", L"MNOP", L"", L"QRSTUV WX Y  ", L"", L" Z  "};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Multiple delimiters exist at the start of the string.
    TEST_CASE(Strings_Split_InitialDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"%%%%MyTestString";
        const TemporaryVector<std::wstring_view> expectedPieces = {L"", L"", L"", L"", L"MyTestString"};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // A single terminal delimiter exists at the end of the string.
    TEST_CASE(Strings_Split_TerminalDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"MyTestString%";
        const TemporaryVector<std::wstring_view> expectedPieces = {L"MyTestString", L""};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Multiple delimiters exist at the end of the string.
    TEST_CASE(Strings_Split_TerminalDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"MyTestString%%%%";
        const TemporaryVector<std::wstring_view> expectedPieces = {L"MyTestString", L"", L"", L"", L""};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Empty input string.
    TEST_CASE(Strings_Split_EmptyInput)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"";
        const TemporaryVector<std::wstring_view> expectedPieces = {L""};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Empty delimiter, which semantically means that no characters match the delimiter and thus the entire input string is returned in one piece.
    TEST_CASE(Strings_Split_EmptyDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiter = L"";
        constexpr std::wstring_view kInputString = L"MyTestString";
        const TemporaryVector<std::wstring_view> expectedPieces = {kInputString};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Delimiter and input strings are equal, so the output should be two empty string, one for the empty part before the delimiter and one for the empty part after it.
    TEST_CASE(Strings_Split_OnlyDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiters[] = {L"%", L"::", L"MyTestString"};

        for (const auto kSplitDelimiter : kSplitDelimiters)
        {
            const std::wstring_view inputString = kSplitDelimiter;
            const TemporaryVector<std::wstring_view> expectedPieces = { L"", L"" };
            const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(inputString, kSplitDelimiter);
            TEST_ASSERT(actualPieces == expectedPieces);
        }
    }

    // Both input and delimiter strings are empty. Because the delimiter is empty there is no match, so the result is a single empty string.
    TEST_CASE(Strings_Split_EmptyInputAndDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiter = L"";
        constexpr std::wstring_view kInputString = kSplitDelimiter;
        const TemporaryVector<std::wstring_view> expectedPieces = {L""};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }
}
