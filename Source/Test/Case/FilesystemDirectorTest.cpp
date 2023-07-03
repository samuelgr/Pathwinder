/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemDirectorBuilderTest.cpp
 *   Unit tests for all functionality related to building filesystem director
 *   objects and ensuring consistency between filesystem rules.
 *****************************************************************************/

#include "FilesystemDirector.h"
#include "PrefixIndex.h"
#include "Strings.h"
#include "TemporaryBuffer.h"
#include "TestCase.h"

#include <map>
#include <string>
#include <string_view>


namespace PathwinderTest
{
    using namespace ::Pathwinder;


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Convenience function for constructing a filesystem director object from a map of rules.
    /// Performs some of the same operations that a filesystem director builder would do internally but without any of the filesystem consistency checks.
    /// @param [in] filesystemRules Map of filesystem rule names to filesystem rules, which is consumed using move semantics.
    /// @return Newly-constructed filesystem director object.
    static FilesystemDirector MakeFilesystemDirector(std::map<std::wstring, FilesystemRule, std::less<>>&& filesystemRules)
    {
        PrefixIndex<wchar_t, FilesystemRule> originDirectoryIndex(L"\\");

        for (auto& filesystemRulePair : filesystemRules)
        {
            filesystemRulePair.second.SetName(filesystemRulePair.first);
            originDirectoryIndex.Insert(filesystemRulePair.second.GetOriginDirectoryFullPath(), filesystemRulePair.second);
        }

        return FilesystemDirector(std::move(filesystemRules), std::move(originDirectoryIndex));
    }

    /// Convenience function for constructing a filesystem rule object from an origin directory and a target directory.
    /// Performs some of the operations that would normally be done internally for things like case-insensitivity.
    /// @param [in] originDirectory Origin directory for the filesystem rule object.
    /// @param [in] targetDirectory Target directory for the filesystem rule object.
    /// @return Newly-constructed filesystem rule object.
    static FilesystemRule MakeFilesystemRule(std::wstring_view originDirectory, std::wstring_view targetDirectory)
    {
        return FilesystemRule(Strings::ToLowercase(originDirectory), Strings::ToLowercase(targetDirectory));
    }

    /// Convenience helper for evaluating an expected outcome of a rule not being present.
    /// Simply compares the pointer to `nullptr`.
    /// @param [in] rule Rule pointer to check.
    /// @return `true` if the pointer is `nullptr`, `false` otherwise.
    static bool RuleIsNotPresent(const FilesystemRule* rule)
    {
        return (nullptr == rule);
    }

    /// Convenience helper for evaluating an expected outcome of a rule being present and having a specific name.
    /// @param [in] name Name that the rule is expected to have.
    /// @param [in] rule Rule pointer to check.
    /// @return `true` if the rule pointer is not `nullptr` and the associated rule has the correct name, `false` otherwise.
    static bool RuleIsPresentAndNamed(std::wstring_view name, const FilesystemRule* rule)
    {
        if (nullptr == rule)
            return false;

        return (rule->GetName() == name);
    }


    // -------- TEST CASES ------------------------------------------------- //

    // Creates a filesystem director with a few non-overlapping rules and queries it with a few file inputs.
    // Verifies that each time the correct rule is chosen or, if the file path does not match any rule, no rule is chosen.
    TEST_CASE(FilesystemDirector_SelectRuleForSingleFile_Nominal)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", MakeFilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", MakeFilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", MakeFilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        TEST_ASSERT(RuleIsPresentAndNamed(L"1", director.SelectRuleForSingleFile(L"C:\\Origin1\\file1.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"2", director.SelectRuleForSingleFile(L"C:\\Origin2\\Subdir2\\file2.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"3", director.SelectRuleForSingleFile(L"C:\\Origin3\\Subdir3\\Subdir3_2\\file3.txt")));
        TEST_ASSERT(RuleIsNotPresent(director.SelectRuleForSingleFile(L"C:\\Origin4\\Subdir4\\Subdir4_2\\Subdir4_3\\file4.txt")));
    }

    // Creates a filesystem director with a few non-overlapping rules and queries it with a few file inputs.
    // Verifies that each time the correct rule is chosen or, if the file path does not match any rule, no rule is chosen.
    // This variation exercises case insensitivity by varying the case between rule creation and redirection query.
    TEST_CASE(FilesystemDirector_SelectRuleForSingleFile_CaseInsensitive)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", MakeFilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", MakeFilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", MakeFilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        TEST_ASSERT(RuleIsPresentAndNamed(L"1", director.SelectRuleForSingleFile(L"C:\\ORIGIN1\\file1.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"2", director.SelectRuleForSingleFile(L"C:\\origin2\\SubDir2\\file2.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"3", director.SelectRuleForSingleFile(L"C:\\ORiGiN3\\SubdIR3\\SubdIR3_2\\file3.txt")));
        TEST_ASSERT(RuleIsNotPresent(director.SelectRuleForSingleFile(L"C:\\OrigIN4\\SUBdir4\\SUBdir4_2\\SUBdir4_3\\file4.txt")));
    }

    // Creates a filesystem with a few overlapping rules and queries it with a few file inputs.
    // Verifies that the most specific rule is always chosen.
    TEST_CASE(FilesystemDirector_SelectRuleForSingleFile_ChooseMostSpecific)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", MakeFilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", MakeFilesystemRule(L"C:\\Origin1\\Origin2", L"C:\\Target2")},
            {L"3", MakeFilesystemRule(L"C:\\Origin1\\Origin2\\Origin3", L"C:\\Target3")},
        }));

        TEST_ASSERT(RuleIsPresentAndNamed(L"1", director.SelectRuleForSingleFile(L"C:\\Origin1\\file1.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"2", director.SelectRuleForSingleFile(L"C:\\Origin1\\Origin2\\file2.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"3", director.SelectRuleForSingleFile(L"C:\\Origin1\\Origin2\\Origin3\\file3.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"2", director.SelectRuleForSingleFile(L"C:\\Origin1\\Origin2\\AnotherDirectory\\somefile.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"1", director.SelectRuleForSingleFile(L"C:\\Origin1\\AnotherPathway\\SomeDirectory\\Subdir\\logfile.log")));
    }

    // Creates a filesystem director with a few non-overlapping rules and queries it for redirection with a few file inputs.
    // Verifies that each time the resulting redirected path is correct.
    TEST_CASE(FilesystemDirector_RedirectSingleFile_Nominal)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", MakeFilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", MakeFilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", MakeFilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        constexpr std::pair<std::wstring_view, std::wstring_view> kTestInputsAndExpectedOutputs[] = {
            {L"C:\\Origin1\\file1.txt", L"C:\\Target1\\file1.txt"},
            {L"C:\\Origin2\\Subdir2\\file2.txt", L"C:\\Target2\\Subdir2\\file2.txt"},
            {L"C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt", L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt"},
            {L"D:\\NonRedirectedFile\\Subdir\\file.log", L"D:\\NonRedirectedFile\\Subdir\\file.log"}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const std::wstring_view expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectSingleFile(testInput);
            TEST_ASSERT(Strings::EqualsCaseInsensitive<wchar_t>(actualOutput, expectedOutput));
        }
    }

    // Creates a filesystem director with a few non-overlapping rules and queries it for redirection with a few file inputs.
    // Similar to the nominal test case except the file inputs this time have multiple consecutive path separators in their paths.
    // Verifies that each time the resulting redirected path is correct.
    TEST_CASE(FilesystemDirector_RedirectSingleFile_ConsecutivePathSeparators)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", MakeFilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", MakeFilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", MakeFilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        constexpr std::pair<std::wstring_view, std::wstring_view> kTestInputsAndExpectedOutputs[] = {
            {L"C:\\\\Origin1\\\\\\file1.txt", L"C:\\Target1\\file1.txt"},
            {L"C:\\\\\\Origin2\\\\\\\\\\Subdir2\\\\file2.txt", L"C:\\Target2\\Subdir2\\file2.txt"},
            {L"C:\\\\\\\\\\Origin3\\\\\\Subdir3\\\\Subdir3B\\\\Subdir3C\\\\file3.txt", L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt"},
            {L"D:\\\\NonRedirectedFile\\Subdir\\\\\\\\file.log", L"D:\\NonRedirectedFile\\Subdir\\file.log"}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const std::wstring_view expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectSingleFile(testInput);
            TEST_ASSERT(Strings::EqualsCaseInsensitive<wchar_t>(actualOutput, expectedOutput));
        }
    }

    // Creates a filesystem director with a few non-overlapping rules and queries it for redirection with a few file inputs.
    // Similar to the nominal case except the file inputs this time have '.' and '..' somewhere within to exercise relative path handling.
    TEST_CASE(FilesystemDirector_RedirectSingleFile_RelativePaths)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", MakeFilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", MakeFilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", MakeFilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        constexpr std::pair<std::wstring_view, std::wstring_view> kTestInputsAndExpectedOutputs[] = {
            {L"C:\\Origin1\\Subdir1\\..\\..\\Origin1\\file1.txt", L"C:\\Target1\\file1.txt"},
            {L"C:\\Origin2\\.\\.\\..\\Origin2\\Subdir2\\file2.txt", L"C:\\Target2\\Subdir2\\file2.txt"},
            {L"C:\\.\\.\\.\\.\\Origin3\\Subdir3\\Subdir3B\\Subdir3D\\..\\Subdir3C\\file3.txt", L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt"},
            {L"D:\\.\\.\\NonRedirectedFile\\SubdirX\\..\\.\\Subdir\\file.log", L"D:\\NonRedirectedFile\\Subdir\\file.log"}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const std::wstring_view expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectSingleFile(testInput);
            TEST_ASSERT(Strings::EqualsCaseInsensitive<wchar_t>(actualOutput, expectedOutput));
        }
    }

    // Creates a filesystem director with a few non-overlapping rules and queries it for redirectin with file inputs that are invalid.
    // Verifies that each time the resulting returned path is equal to the input string.
    TEST_CASE(FilesystemDirector_RedirectSingleFile_InvalidInputPath)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", MakeFilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", MakeFilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", MakeFilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        constexpr std::wstring_view kTestInputsAndExpectedOutputs[] = {
            L""
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord;
            const std::wstring_view expectedOutput = testRecord;

            auto actualOutput = director.RedirectSingleFile(testInput);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }
}
