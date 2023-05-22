/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
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

    // The following sequence of tests, which together comprise the Tokenize suite, exercise the TokenizeString function.

    // Nominal case of a string with delimiters being tokenized.
    TEST_CASE(Strings_Tokenize_Nominal)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"ABCD%EFGH%IJKL%MNOP%QRSTUV WX Y  % Z  ";
        
        const TemporaryVector<std::wstring_view> expectedPieces = {L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV WX Y  ", L" Z  "};
        TemporaryVector<std::wstring_view> actualPieces;

        size_t tokenizeState = 0;
        for (std::optional<std::wstring_view> maybeNextPiece = Strings::TokenizeString(kInputString, kSplitDelimiter, tokenizeState); true == maybeNextPiece.has_value(); maybeNextPiece = Strings::TokenizeString(kInputString, kSplitDelimiter, tokenizeState))
            actualPieces.PushBack(maybeNextPiece.value());

        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Same as the nominal case but with a multi-character delimiter.
    TEST_CASE(Strings_Tokenize_MultiCharacterDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiter = L":::";
        constexpr std::wstring_view kInputString = L"ABCD:::EFGH:::IJKL:::MNOP:::QRSTUV WX Y  ::: Z  ";

        const TemporaryVector<std::wstring_view> expectedPieces = {L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV WX Y  ", L" Z  "};
        TemporaryVector<std::wstring_view> actualPieces;

        size_t tokenizeState = 0;
        for (std::optional<std::wstring_view> maybeNextPiece = Strings::TokenizeString(kInputString, kSplitDelimiter, tokenizeState); true == maybeNextPiece.has_value(); maybeNextPiece = Strings::TokenizeString(kInputString, kSplitDelimiter, tokenizeState))
            actualPieces.PushBack(maybeNextPiece.value());

        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Same as the nominal case but with a trailing delimiter at the end of the input string.
    TEST_CASE(Strings_Tokenize_TerminalDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"ABCD%EFGH%IJKL%MNOP%QRSTUV WX Y  % Z  %";
        
        const TemporaryVector<std::wstring_view> expectedPieces = {L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV WX Y  ", L" Z  ", L""};
        TemporaryVector<std::wstring_view> actualPieces;

        size_t tokenizeState = 0;
        for (std::optional<std::wstring_view> maybeNextPiece = Strings::TokenizeString(kInputString, kSplitDelimiter, tokenizeState); true == maybeNextPiece.has_value(); maybeNextPiece = Strings::TokenizeString(kInputString, kSplitDelimiter, tokenizeState))
            actualPieces.PushBack(maybeNextPiece.value());

        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Input string consists of delimiter characters exclusively.
    TEST_CASE(Strings_Tokenize_ExclusivelyDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"%%%%%";
        
        const TemporaryVector<std::wstring_view> expectedPieces = {L"", L"", L"", L"", L"", L""};
        TemporaryVector<std::wstring_view> actualPieces;

        size_t tokenizeState = 0;
        for (std::optional<std::wstring_view> maybeNextPiece = Strings::TokenizeString(kInputString, kSplitDelimiter, tokenizeState); true == maybeNextPiece.has_value(); maybeNextPiece = Strings::TokenizeString(kInputString, kSplitDelimiter, tokenizeState))
            actualPieces.PushBack(maybeNextPiece.value());

        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Three-piece string with two different delimiters that changes between invocations.
    TEST_CASE(Strings_Tokenize_DifferentDelimiterBetweenCalls)
    {
        constexpr std::wstring_view kInputString = L"Part 1:::Part 2!!Part 3";

        size_t tokenizeState = 0;
        std::optional<std::wstring_view> maybeNextPiece = L"";

        // First two delimiter inputs must match the input string.
        // Since there is no delimiter after "Part 3" the specific delimiter passed does not matter and can be empty.

        maybeNextPiece = Strings::TokenizeString(kInputString, std::wstring_view(L":::"), tokenizeState);
        TEST_ASSERT(true == maybeNextPiece.has_value());
        TEST_ASSERT(maybeNextPiece.value() == L"Part 1");

        maybeNextPiece = Strings::TokenizeString(kInputString, std::wstring_view(L"!!"), tokenizeState);
        TEST_ASSERT(true == maybeNextPiece.has_value());
        TEST_ASSERT(maybeNextPiece.value() == L"Part 2");

        maybeNextPiece = Strings::TokenizeString(kInputString, std::wstring_view(L""), tokenizeState);
        TEST_ASSERT(true == maybeNextPiece.has_value());
        TEST_ASSERT(maybeNextPiece.value() == L"Part 3");

        maybeNextPiece = Strings::TokenizeString(kInputString, std::wstring_view(L""), tokenizeState);
        TEST_ASSERT(false == maybeNextPiece.has_value());
    }


    // The following sequence of tests, which together comprise the Split suite, exercise the SplitString functions.

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

    // Same as the nominal case but with multiple delimiters of varying lengths.
    TEST_CASE(Strings_Split_MultipleDelimiters)
    {
        constexpr std::wstring_view kInputString = L"ABCD%EFGH//IJKL:::MNOP!!!QRSTUV%WX:::YZ";
        const TemporaryVector<std::wstring_view> expectedPieces = {L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV", L"WX", L"YZ"};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString<wchar_t>(kInputString, {L":::", L"%", L"!!!", L"//"});
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
            const TemporaryVector<std::wstring_view> expectedPieces = {L"", L""};
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


    // The following sequence of tests, which together comprise the Compare suite, exercise the string comparison operations EqualsCaseInsensitive and StartsWithCaseInsensitive.

    // Tests case-insensitive string equality comparison by providing some matching and some non-matching inputs.
    TEST_CASE(Strings_Compare_EqualsCaseInsensitive)
    {
        constexpr std::wstring_view kTestString = L"TestStringAbCdEfG";
        constexpr std::wstring_view kMatchingInputs[] = {
            L"TestStringAbCdEfG",
            L"teststringabcdefg",
            L"TESTSTRINGABCDEFG",
            L"tEsTsTrInGaBcDeFg"
        };
        constexpr std::wstring_view kNonMatchingInputs[] = {
            L"TestString",
            L"AbCdEfG",
            L"Totally_unrelated_string"
        };

        for (const auto matchingInput : kMatchingInputs)
            TEST_ASSERT(true == Strings::EqualsCaseInsensitive(kTestString, matchingInput));

        for (const auto nonMatchingInput : kNonMatchingInputs)
            TEST_ASSERT(false == Strings::EqualsCaseInsensitive(kTestString, nonMatchingInput));
    }

    TEST_CASE(Strings_Compare_StartsWithCaseInsensitive)
    {
        constexpr std::wstring_view kTestString = L"TestStringAbCdEfG";
        constexpr std::wstring_view kMatchingInputs[] = {
            L"TestStringAbCdEfG",
            L"TestStringAbCdEf",
            L"teststringabcdef",
            L"teststring",
            L"TEST",
            L"tEsTsTrInGaB"
        };
        constexpr std::wstring_view kNonMatchingInputs[] = {
            L"TestStringAbCdEfGhIj",
            L"AbCdEfG",
            L"TestOtherStringAbC",
            L"Totally_unrelated_string"
        };

        for (const auto matchingInput : kMatchingInputs)
            TEST_ASSERT(true == Strings::StartsWithCaseInsensitive(kTestString, matchingInput));

        for (const auto nonMatchingInput : kNonMatchingInputs)
            TEST_ASSERT(false == Strings::StartsWithCaseInsensitive(kTestString, nonMatchingInput));
    }
}
