/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file StringsTest.cpp
 *   Unit tests for functions that manipulate strings.
 **************************************************************************************************/

#include "TestCase.h"

#include "Strings.h"

#include <string_view>

#include "TemporaryBuffer.h"

namespace PathwinderTest
{
    using namespace ::Pathwinder;

    // The following sequence of tests, which together comprise the Tokenize suite, exercise the
    // TokenizeString function.

    // Nominal case of a string with delimiters being tokenized.
    TEST_CASE(Strings_Tokenize_Nominal)
    {
        constexpr std::wstring_view kTokenDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"ABCD%EFGH%IJKL%MNOP%QRSTUV WX Y  % Z  ";

        const TemporaryVector<std::wstring_view> expectedPieces = {
            L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV WX Y  ", L" Z  "};
        TemporaryVector<std::wstring_view> actualPieces;

        size_t tokenizeState = 0;
        for (std::optional<std::wstring_view> maybeNextPiece =
                 Strings::TokenizeString(tokenizeState, kInputString, kTokenDelimiter);
             true == maybeNextPiece.has_value();
             maybeNextPiece = Strings::TokenizeString(tokenizeState, kInputString, kTokenDelimiter))
            actualPieces.PushBack(maybeNextPiece.value());

        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Same as the nominal case but with a multi-character delimiter.
    TEST_CASE(Strings_Tokenize_MultiCharacterDelimiter)
    {
        constexpr std::wstring_view kTokenDelimiter = L":::";
        constexpr std::wstring_view kInputString =
            L"ABCD:::EFGH:::IJKL:::MNOP:::QRSTUV WX Y  ::: Z  ";

        const TemporaryVector<std::wstring_view> expectedPieces = {
            L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV WX Y  ", L" Z  "};
        TemporaryVector<std::wstring_view> actualPieces;

        size_t tokenizeState = 0;
        for (std::optional<std::wstring_view> maybeNextPiece =
                 Strings::TokenizeString(tokenizeState, kInputString, kTokenDelimiter);
             true == maybeNextPiece.has_value();
             maybeNextPiece = Strings::TokenizeString(tokenizeState, kInputString, kTokenDelimiter))
            actualPieces.PushBack(maybeNextPiece.value());

        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Same as the nominal case but with multiple delimiters supplied simultaneously.
    TEST_CASE(Strings_Tokenize_MultipleDelimiters)
    {
        constexpr std::wstring_view kTokenDelimiters[] = {
            L"%%%%%%%%%%", L"//", L"----", L"+++++", L"_", L":::"};
        constexpr std::wstring_view kInputString =
            L"ABCD:::EFGH//IJKL+++++MNOP%%%%%%%%%%QRSTUV WX Y  _ Z  ";

        const TemporaryVector<std::wstring_view> expectedPieces = {
            L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV WX Y  ", L" Z  "};
        TemporaryVector<std::wstring_view> actualPieces;

        size_t tokenizeState = 0;
        for (std::optional<std::wstring_view> maybeNextPiece = Strings::TokenizeString(
                 tokenizeState, kInputString, kTokenDelimiters, _countof(kTokenDelimiters));
             true == maybeNextPiece.has_value();
             maybeNextPiece = Strings::TokenizeString(
                 tokenizeState, kInputString, kTokenDelimiters, _countof(kTokenDelimiters)))
            actualPieces.PushBack(maybeNextPiece.value());

        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Same as the nominal case but with a trailing delimiter at the end of the input string.
    TEST_CASE(Strings_Tokenize_TerminalDelimiter)
    {
        constexpr std::wstring_view kTokenDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"ABCD%EFGH%IJKL%MNOP%QRSTUV WX Y  % Z  %";

        const TemporaryVector<std::wstring_view> expectedPieces = {
            L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV WX Y  ", L" Z  ", L""};
        TemporaryVector<std::wstring_view> actualPieces;

        size_t tokenizeState = 0;
        for (std::optional<std::wstring_view> maybeNextPiece =
                 Strings::TokenizeString(tokenizeState, kInputString, kTokenDelimiter);
             true == maybeNextPiece.has_value();
             maybeNextPiece = Strings::TokenizeString(tokenizeState, kInputString, kTokenDelimiter))
            actualPieces.PushBack(maybeNextPiece.value());

        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Input string consists of delimiter characters exclusively.
    TEST_CASE(Strings_Tokenize_ExclusivelyDelimiters)
    {
        constexpr std::wstring_view kTokenDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"%%%%%";

        const TemporaryVector<std::wstring_view> expectedPieces = {L"", L"", L"", L"", L"", L""};
        TemporaryVector<std::wstring_view> actualPieces;

        size_t tokenizeState = 0;
        for (std::optional<std::wstring_view> maybeNextPiece =
                 Strings::TokenizeString(tokenizeState, kInputString, kTokenDelimiter);
             true == maybeNextPiece.has_value();
             maybeNextPiece = Strings::TokenizeString(tokenizeState, kInputString, kTokenDelimiter))
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
        // Since there is no delimiter after "Part 3" the specific delimiter passed does not matter
        // and can be empty.

        maybeNextPiece =
            Strings::TokenizeString(tokenizeState, kInputString, std::wstring_view(L":::"));
        TEST_ASSERT(true == maybeNextPiece.has_value());
        TEST_ASSERT(maybeNextPiece.value() == L"Part 1");

        maybeNextPiece =
            Strings::TokenizeString(tokenizeState, kInputString, std::wstring_view(L"!!"));
        TEST_ASSERT(true == maybeNextPiece.has_value());
        TEST_ASSERT(maybeNextPiece.value() == L"Part 2");

        maybeNextPiece =
            Strings::TokenizeString(tokenizeState, kInputString, std::wstring_view(L""));
        TEST_ASSERT(true == maybeNextPiece.has_value());
        TEST_ASSERT(maybeNextPiece.value() == L"Part 3");

        maybeNextPiece =
            Strings::TokenizeString(tokenizeState, kInputString, std::wstring_view(L""));
        TEST_ASSERT(false == maybeNextPiece.has_value());
    }

    // The following sequence of tests, which together comprise the Split suite, exercise the
    // SplitString functions.

    // Nominal case of a string with delimiters being split into pieces.
    TEST_CASE(Strings_Split_Nominal)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"ABCD%EFGH%IJKL%MNOP%QRSTUV WX Y  % Z  ";
        const TemporaryVector<std::wstring_view> expectedPieces = {
            L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV WX Y  ", L" Z  "};
        const TemporaryVector<std::wstring_view> actualPieces =
            Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Same as the nominal case but with a multi-character delimiter.
    TEST_CASE(Strings_Split_MultiCharacterDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiter = L":::";
        constexpr std::wstring_view kInputString =
            L"ABCD:::EFGH:::IJKL:::MNOP:::QRSTUV WX Y  ::: Z  ";
        const TemporaryVector<std::wstring_view> expectedPieces = {
            L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV WX Y  ", L" Z  "};
        const TemporaryVector<std::wstring_view> actualPieces =
            Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Same as the nominal case but with multiple delimiters of varying lengths.
    TEST_CASE(Strings_Split_MultipleDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiters[] = {L":::", L"%", L"!!!", L"//"};
        constexpr std::wstring_view kInputString = L"ABCD%EFGH//IJKL:::MNOP!!!QRSTUV%WX:::YZ";
        const TemporaryVector<std::wstring_view> expectedPieces = {
            L"ABCD", L"EFGH", L"IJKL", L"MNOP", L"QRSTUV", L"WX", L"YZ"};
        const TemporaryVector<std::wstring_view> actualPieces = Strings::SplitString<wchar_t>(
            kInputString, kSplitDelimiters, _countof(kSplitDelimiters));
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // No delimiters are present, so the entire string should be returned in one piece.
    TEST_CASE(Strings_Split_NoDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"ABCD EFGH IJKL MNOP QRSTUV WX Y  Z  ";
        const TemporaryVector<std::wstring_view> expectedPieces = {kInputString};
        const TemporaryVector<std::wstring_view> actualPieces =
            Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Multiple consecutive delimiters are present, so those pieces should be empty.
    TEST_CASE(Strings_Split_ConsecutiveDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"ABCD%%EFGH%%IJKL%%MNOP%%QRSTUV WX Y  %% Z  ";
        const TemporaryVector<std::wstring_view> expectedPieces = {
            L"ABCD", L"", L"EFGH", L"", L"IJKL", L"", L"MNOP", L"", L"QRSTUV WX Y  ", L"", L" Z  "};
        const TemporaryVector<std::wstring_view> actualPieces =
            Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Multiple consecutive multi-character delimiters are present, so those pieces should be empty.
    TEST_CASE(Strings_Split_ConsecutiveMultiCharacterDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiter = L":::";
        constexpr std::wstring_view kInputString =
            L"ABCD::::::EFGH::::::IJKL::::::MNOP::::::QRSTUV WX Y  :::::: Z  ";
        const TemporaryVector<std::wstring_view> expectedPieces = {
            L"ABCD", L"", L"EFGH", L"", L"IJKL", L"", L"MNOP", L"", L"QRSTUV WX Y  ", L"", L" Z  "};
        const TemporaryVector<std::wstring_view> actualPieces =
            Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Multiple delimiters exist at the start of the string.
    TEST_CASE(Strings_Split_InitialDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"%%%%MyTestString";
        const TemporaryVector<std::wstring_view> expectedPieces = {
            L"", L"", L"", L"", L"MyTestString"};
        const TemporaryVector<std::wstring_view> actualPieces =
            Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // A single terminal delimiter exists at the end of the string.
    TEST_CASE(Strings_Split_TerminalDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"MyTestString%";
        const TemporaryVector<std::wstring_view> expectedPieces = {L"MyTestString", L""};
        const TemporaryVector<std::wstring_view> actualPieces =
            Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Multiple delimiters exist at the end of the string.
    TEST_CASE(Strings_Split_TerminalDelimiters)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"MyTestString%%%%";
        const TemporaryVector<std::wstring_view> expectedPieces = {
            L"MyTestString", L"", L"", L"", L""};
        const TemporaryVector<std::wstring_view> actualPieces =
            Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Empty input string.
    TEST_CASE(Strings_Split_EmptyInput)
    {
        constexpr std::wstring_view kSplitDelimiter = L"%";
        constexpr std::wstring_view kInputString = L"";
        const TemporaryVector<std::wstring_view> expectedPieces = {L""};
        const TemporaryVector<std::wstring_view> actualPieces =
            Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Empty delimiter, which semantically means that no characters match the delimiter and thus the
    // entire input string is returned in one piece.
    TEST_CASE(Strings_Split_EmptyDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiter = L"";
        constexpr std::wstring_view kInputString = L"MyTestString";
        const TemporaryVector<std::wstring_view> expectedPieces = {kInputString};
        const TemporaryVector<std::wstring_view> actualPieces =
            Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // Delimiter and input strings are equal, so the output should be two empty string, one for the
    // empty part before the delimiter and one for the empty part after it.
    TEST_CASE(Strings_Split_OnlyDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiters[] = {L"%", L"::", L"MyTestString"};

        for (const auto kSplitDelimiter : kSplitDelimiters)
        {
            const std::wstring_view inputString = kSplitDelimiter;
            const TemporaryVector<std::wstring_view> expectedPieces = {L"", L""};
            const TemporaryVector<std::wstring_view> actualPieces =
                Strings::SplitString(inputString, kSplitDelimiter);
            TEST_ASSERT(actualPieces == expectedPieces);
        }
    }

    // Both input and delimiter strings are empty. Because the delimiter is empty there is no match,
    // so the result is a single empty string.
    TEST_CASE(Strings_Split_EmptyInputAndDelimiter)
    {
        constexpr std::wstring_view kSplitDelimiter = L"";
        constexpr std::wstring_view kInputString = kSplitDelimiter;
        const TemporaryVector<std::wstring_view> expectedPieces = {L""};
        const TemporaryVector<std::wstring_view> actualPieces =
            Strings::SplitString(kInputString, kSplitDelimiter);
        TEST_ASSERT(actualPieces == expectedPieces);
    }

    // The following sequence of tests, which together comprise the Compare suite, exercise the
    // string comparison operations CompareCaseInsensitive, EqualsCaseInsensitive, and
    // StartsWithCaseInsensitive.

    // Tests case-insensitive string comparison for equality by providing some input strings that
    // are supposed to be equal to a fixed test string.
    TEST_CASE(Strings_Compare_CompareCaseInsensitive_EqualToTestString)
    {
        constexpr std::wstring_view kTestString = L"TestStringAbCdEfG";
        constexpr std::wstring_view kCompareInputs[] = {
            L"TestStringAbCdEfG", L"teststringabcdefg", L"TESTSTRINGABCDEFG", L"tEsTsTrInGaBcDeFg"};

        for (const auto compareInput : kCompareInputs)
            TEST_ASSERT(Strings::CompareCaseInsensitive(compareInput, kTestString) == 0);
    }

    // Tests case-insensitive string comparison for less-than comparison by providing some input
    // strings that are supposed to be less than a fixed test string.
    TEST_CASE(Strings_Compare_CompareCaseInsensitive_LessThanTestString)
    {
        constexpr std::wstring_view kTestString = L"abcdefg";
        constexpr std::wstring_view kCompareInputs[] = {
            L"", L"..", L"aaaaaaa", L"AAAAAAA", L"abcdef", L".abcdefg"};

        for (const auto compareInput : kCompareInputs)
            TEST_ASSERT(Strings::CompareCaseInsensitive(compareInput, kTestString) < 0);
    }

    // Tests case-insensitive string comparison for greater-than comparison by providing some input
    // strings that are supposed to be greater than a fixed test string.
    TEST_CASE(Strings_Compare_CompareCaseInsensitive_GreaterThanTestString)
    {
        constexpr std::wstring_view kTestString = L"abcdefg";
        constexpr std::wstring_view kCompareInputs[] = {L"b", L"B", L"abcdeHg", L"abcdefghijk"};

        for (const auto compareInput : kCompareInputs)
            TEST_ASSERT(Strings::CompareCaseInsensitive(compareInput, kTestString) > 0);
    }

    // Tests case-insensitive string equality comparison by providing some matching and some
    // non-matching inputs.
    TEST_CASE(Strings_Compare_EqualsCaseInsensitive)
    {
        constexpr std::wstring_view kTestString = L"TestStringAbCdEfG";
        constexpr std::wstring_view kMatchingInputs[] = {
            L"TestStringAbCdEfG", L"teststringabcdefg", L"TESTSTRINGABCDEFG", L"tEsTsTrInGaBcDeFg"};
        constexpr std::wstring_view kNonMatchingInputs[] = {
            L"TestString", L"AbCdEfG", L"Totally_unrelated_string"};

        for (const auto matchingInput : kMatchingInputs)
            TEST_ASSERT(true == Strings::EqualsCaseInsensitive(kTestString, matchingInput));

        for (const auto nonMatchingInput : kNonMatchingInputs)
            TEST_ASSERT(false == Strings::EqualsCaseInsensitive(kTestString, nonMatchingInput));
    }

    // Tests case-insensitive string prefix matching by providing some matching and some
    // non-matching inputs.
    TEST_CASE(Strings_Compare_StartsWithCaseInsensitive)
    {
        constexpr std::wstring_view kTestString = L"TestStringAbCdEfG";
        constexpr std::wstring_view kMatchingInputs[] = {
            L"TestStringAbCdEfG",
            L"TestStringAbCdEf",
            L"teststringabcdef",
            L"teststring",
            L"TEST",
            L"tEsTsTrInGaB"};
        constexpr std::wstring_view kNonMatchingInputs[] = {
            L"TestStringAbCdEfGhIj",
            L"AbCdEfG",
            L"TestOtherStringAbC",
            L"Totally_unrelated_string"};

        for (const auto matchingInput : kMatchingInputs)
            TEST_ASSERT(true == Strings::StartsWithCaseInsensitive(kTestString, matchingInput));

        for (const auto nonMatchingInput : kNonMatchingInputs)
            TEST_ASSERT(false == Strings::StartsWithCaseInsensitive(kTestString, nonMatchingInput));
    }
}  // namespace PathwinderTest
