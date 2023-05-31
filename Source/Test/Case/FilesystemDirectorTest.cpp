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
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        TEST_ASSERT(RuleIsPresentAndNamed(L"1", director.SelectRuleForSingleFile(L"C:\\Origin1\\file1.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"2", director.SelectRuleForSingleFile(L"C:\\Origin2\\Subdir2\\file2.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"3", director.SelectRuleForSingleFile(L"C:\\Origin3\\Subdir3\\Subdir3_2\\file3.txt")));
        TEST_ASSERT(RuleIsNotPresent(director.SelectRuleForSingleFile(L"C:\\Origin4\\Subdir4\\Subdir4_2\\Subdir4_3\\file4.txt")));
    }

    // Creates a filesystem with a few overlapping rules and queries it with a few file inputs.
    // Verifies that the most specific rule is always chosen.
    TEST_CASE(FilesystemDirector_SelectRuleForSingleFile_ChooseMostSpecific)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin1\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin1\\Origin2\\Origin3", L"C:\\Target3")},
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
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
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
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }
}
