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
#include "FilesystemInstruction.h"
#include "MockFilesystemOperations.h"
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
    TEST_CASE(FilesystemDirector_SelectRuleForRedirectionQuery_Nominal)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        TEST_ASSERT(RuleIsPresentAndNamed(L"1", director.SelectRuleForRedirectionQuery(L"C:\\Origin1\\file1.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"2", director.SelectRuleForRedirectionQuery(L"C:\\Origin2\\Subdir2\\file2.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"3", director.SelectRuleForRedirectionQuery(L"C:\\Origin3\\Subdir3\\Subdir3_2\\file3.txt")));
        TEST_ASSERT(RuleIsNotPresent(director.SelectRuleForRedirectionQuery(L"C:\\Origin4\\Subdir4\\Subdir4_2\\Subdir4_3\\file4.txt")));
    }

    // Creates a filesystem director with a few non-overlapping rules and queries it with a few file inputs.
    // Verifies that each time the correct rule is chosen or, if the file path does not match any rule, no rule is chosen.
    // This variation exercises case insensitivity by varying the case between rule creation and redirection query.
    TEST_CASE(FilesystemDirector_SelectRuleForRedirectionQuery_CaseInsensitive)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        TEST_ASSERT(RuleIsPresentAndNamed(L"1", director.SelectRuleForRedirectionQuery(L"C:\\ORIGIN1\\file1.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"2", director.SelectRuleForRedirectionQuery(L"C:\\origin2\\SubDir2\\file2.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"3", director.SelectRuleForRedirectionQuery(L"C:\\ORiGiN3\\SubdIR3\\SubdIR3_2\\file3.txt")));
        TEST_ASSERT(RuleIsNotPresent(director.SelectRuleForRedirectionQuery(L"C:\\OrigIN4\\SUBdir4\\SUBdir4_2\\SUBdir4_3\\file4.txt")));
    }

    // Creates a filesystem with a few overlapping rules and queries it with a few file inputs.
    // Verifies that the most specific rule is always chosen.
    TEST_CASE(FilesystemDirector_SelectRuleForRedirectionQuery_ChooseMostSpecific)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin1\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin1\\Origin2\\Origin3", L"C:\\Target3")},
        }));

        TEST_ASSERT(RuleIsPresentAndNamed(L"1", director.SelectRuleForRedirectionQuery(L"C:\\Origin1\\file1.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"2", director.SelectRuleForRedirectionQuery(L"C:\\Origin1\\Origin2\\file2.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"3", director.SelectRuleForRedirectionQuery(L"C:\\Origin1\\Origin2\\Origin3\\file3.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"2", director.SelectRuleForRedirectionQuery(L"C:\\Origin1\\Origin2\\AnotherDirectory\\somefile.txt")));
        TEST_ASSERT(RuleIsPresentAndNamed(L"1", director.SelectRuleForRedirectionQuery(L"C:\\Origin1\\AnotherPathway\\SomeDirectory\\Subdir\\logfile.log")));
    }

    // Creates a filesystem director with a single rule at a deep level in the hierarchy and queries it a few times to see if it can successfully identify rule prefixes.
    TEST_CASE(FilesystemDirector_IsPrefixForAnyRule_Nominal)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Level1\\Level2\\Level3\\Origin", L"C:\\Target")},
        }));

        constexpr std::pair<std::wstring_view, bool> kTestInputsAndExpectedOutputs[] = {
            {L"C:\\", true},
            {L"C:\\Level1", true},
            {L"C:\\Level1\\Level2\\", true},
            {L"C:\\Level1\\Level2\\Level3", true},
            {L"C:\\Level1\\Level2\\Level3\\Origin\\", true},
            {L"X:\\", false},
            {L"C:\\Unrelated\\Level2", false}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const bool expectedOutput = testRecord.second;

            const bool actualOutput = director.IsPrefixForAnyRule(testInput);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Creates a filesystem director with a few non-overlapping rules and queries it for redirection with a few file inputs.
    // Verifies that each time the resulting redirected path is correct.
    TEST_CASE(FilesystemDirector_RedirectFileOperation_Nominal)
    {
        MockFilesystemOperations mockFilesystem;

        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        const std::pair<std::wstring_view, FileOperationRedirectInstruction> kTestInputsAndExpectedOutputs[] = {
            {L"C:\\Origin1\\file1.txt",                                 FileOperationRedirectInstruction::RedirectTo(L"C:\\Target1\\file1.txt", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
            {L"C:\\Origin2\\Subdir2\\file2.txt",                        FileOperationRedirectInstruction::RedirectTo(L"C:\\Target2\\Subdir2\\file2.txt", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
            {L"C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt",    FileOperationRedirectInstruction::RedirectTo(L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const auto& expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectFileOperation(testInput, FilesystemDirector::EFileOperationMode::OpenExistingFile);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Verifies that pre-operations are correctly added when a hierarchy exists on the origin side and the file operation attempts to open an existing file.
    // When the query is for a directory that exists on the origin side, it is expected that a pre-operation is added to ensure the same hierarchy exists on the target side.
    // When the query is for a file, whether or not it exists on the origin side, no such pre-operation is necessary.
    TEST_CASE(FilesystemDirector_RedirectFileOperation_OriginHierarchyExists_OpenExistingFile)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\Origin1");
        mockFilesystem.AddDirectory(L"C:\\Origin2\\Subdir2");
        mockFilesystem.AddDirectory(L"C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C");
        mockFilesystem.AddFile(L"C:\\Origin1\\file1.txt");

        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        const std::pair<std::wstring_view, FileOperationRedirectInstruction> kTestInputsAndExpectedOutputs[] = {
            {L"C:\\Origin1",                                FileOperationRedirectInstruction::RedirectTo(L"C:\\Target1",                                FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected, {static_cast<int>(FileOperationRedirectInstruction::EExtraPreOperation::EnsurePathHierarchyExists)}, L"C:\\Target1")},
            {L"C:\\Origin2\\Subdir2",                       FileOperationRedirectInstruction::RedirectTo(L"C:\\Target2\\Subdir2",                       FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected, {static_cast<int>(FileOperationRedirectInstruction::EExtraPreOperation::EnsurePathHierarchyExists)}, L"C:\\Target2\\Subdir2")},
            {L"C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C",   FileOperationRedirectInstruction::RedirectTo(L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C",   FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected, {static_cast<int>(FileOperationRedirectInstruction::EExtraPreOperation::EnsurePathHierarchyExists)}, L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C")},
            {L"C:\\Origin1\\file1.txt",                     FileOperationRedirectInstruction::RedirectTo(L"C:\\Target1\\file1.txt",                     FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected, {}, L"")},
            {L"C:\\Origin2\\Subdir2\\file2.bin",            FileOperationRedirectInstruction::RedirectTo(L"C:\\Target2\\Subdir2\\file2.bin",            FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected, {}, L"")}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const auto& expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectFileOperation(testInput, FilesystemDirector::EFileOperationMode::OpenExistingFile);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Verifies that pre-operations are correctly added when a hierarchy exists on the origin side and the file operation attempts to create a new file.
    // Regardless of the nature of the filesystem entity that is the subject of the query (file or directory) a pre-operation is needed to ensure the parent hierarchy exists on the target side if it also exists on the origin side.
    TEST_CASE(FilesystemDirector_RedirectFileOperation_OriginHierarchyExists_CreateNewFile)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\Origin1");

        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")}
        }));

        const std::pair<std::wstring_view, FileOperationRedirectInstruction> kTestInputsAndExpectedOutputs[] = {
            {L"C:\\Origin1\\AnyTypeOfFile", FileOperationRedirectInstruction::RedirectTo(L"C:\\Target1\\AnyTypeOfFile", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected, {static_cast<int>(FileOperationRedirectInstruction::EExtraPreOperation::EnsurePathHierarchyExists)}, L"C:\\Target1")}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const auto& expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectFileOperation(testInput, FilesystemDirector::EFileOperationMode::CreateNewFile);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Creates a filesystem director with a few non-overlapping rules and queries it for redirection with a few directory inputs.
    // In this case all of the query inputs have trailing backslash characters, which is allowed for directories.
    // Verifies that the trailing backslash is preserved after the redirection operation completes.
    TEST_CASE(FilesystemDirector_RedirectFileOperation_PreservesTrailingBackslash)
    {
        MockFilesystemOperations mockFilesystem;

        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        const std::pair<std::wstring_view, FileOperationRedirectInstruction> kTestInputsAndExpectedOutputs[] = {
            {L"C:\\Origin1\\Subdir1\\",                                 FileOperationRedirectInstruction::RedirectTo(L"C:\\Target1\\Subdir1\\", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
            {L"C:\\Origin2\\Subdir2\\Subdir2B\\",                       FileOperationRedirectInstruction::RedirectTo(L"C:\\Target2\\Subdir2\\Subdir2B\\", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
            {L"C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C\\",             FileOperationRedirectInstruction::RedirectTo(L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C\\", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const auto& expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectFileOperation(testInput, FilesystemDirector::EFileOperationMode::OpenExistingFile);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Creates a filesystem director with a few non-overlapping rules and queries it for redirection with a few file inputs.
    // Verifies that each time the resulting redirected path is correct.
    // This test case variation additionally adds namespace prefixes to the filenames submitted for query. These should be passed through unchanged.
    TEST_CASE(FilesystemDirector_RedirectFileOperation_QueryInputContainsWindowsNamespacePrefix)
    {
        MockFilesystemOperations mockFilesystem;

        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        const std::pair<std::wstring_view, FileOperationRedirectInstruction> kTestInputsAndExpectedOutputs[] = {
            {L"\\??\\C:\\Origin1\\file1.txt",                               FileOperationRedirectInstruction::RedirectTo(L"\\??\\C:\\Target1\\file1.txt", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
            {L"\\\\?\\C:\\Origin2\\Subdir2\\file2.txt",                     FileOperationRedirectInstruction::RedirectTo(L"\\\\?\\C:\\Target2\\Subdir2\\file2.txt", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
            {L"\\\\.\\C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt", FileOperationRedirectInstruction::RedirectTo(L"\\\\.\\C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const auto& expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectFileOperation(testInput, FilesystemDirector::EFileOperationMode::OpenExistingFile);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Creates a filesystem director with a few non-overlapping rules and queries it with inputs that should not be redirected due to no match.
    TEST_CASE(FilesystemDirector_RedirectFileOperation_NonRedirectedInputPath)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        const std::pair<std::wstring_view, FileOperationRedirectInstruction> kTestInputsAndExpectedOutputs[] = {
            {L"D:\\NonRedirectedFile\\Subdir\\file.log", FileOperationRedirectInstruction::NoRedirectionOrInterception()}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const auto& expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectFileOperation(testInput, FilesystemDirector::EFileOperationMode::OpenExistingFile);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Creates a filesystem director a single filesystem rule and queries it with inputs that should not be redirected due to no match.
    // In this case the input query string is not null-terminated, but the buffer itself contains a null-terminated string that ordinarily would be redirected.
    // If the implementation is properly handling non-null-terminated input string views then no redirection should occur, otherwise an erroneous redirection will occur.
    // One query uses a Windows namespace prefix, and the other does not.
    TEST_CASE(FilesystemDirector_RedirectFileOperation_NoRedirectionNotNullTerminated)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Base\\Origin", L"C:\\Base\\Target")},
        }));

        // String buffer identifies "C:\Base\Origin" which intuitively should be redirected to "C:\Base\Target".
        // However, the length field of the string view object means that the view only represents "C:\Base" or "C:\Base\" which has no matching rule and should not be redirected.
        // These inputs are prefixes to rule origin directories and therefore the instruction should be not to redirect but to intercept for processing for possible future filename combination.
        const std::pair<std::wstring_view, FileOperationRedirectInstruction> kTestInputsAndExpectedOutputs[] = {
            {std::wstring_view(L"C:\\Base\\Origin", std::wstring_view(L"C:\\Base").length()),                   FileOperationRedirectInstruction::InterceptWithoutRedirection(FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
            {std::wstring_view(L"C:\\Base\\Origin", std::wstring_view(L"C:\\Base\\").length()),                 FileOperationRedirectInstruction::InterceptWithoutRedirection(FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
            {std::wstring_view(L"\\??\\C:\\Base\\Origin", std::wstring_view(L"\\??\\C:\\Base").length()),       FileOperationRedirectInstruction::InterceptWithoutRedirection(FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
            {std::wstring_view(L"\\??\\C:\\Base\\Origin", std::wstring_view(L"\\??\\C:\\Base\\").length()),     FileOperationRedirectInstruction::InterceptWithoutRedirection(FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const auto& expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectFileOperation(testInput, FilesystemDirector::EFileOperationMode::OpenExistingFile);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Creates a filesystem director with a single filesystem rule and queries it for redirection with an input path exactly equal to the origin directory.
    // Verifies that redirection to the target directory does occur but the associated filename with the newly-created handle is the origin directory.
    // The instruction should also indicate to ensure that the target directory exists.
    TEST_CASE(FilesystemDirector_RedirectFileOperation_EqualsOriginDirectory)
    {
        MockFilesystemOperations mockFilesystem;

        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
        }));

        const std::pair<std::wstring_view, FileOperationRedirectInstruction> kTestInputsAndExpectedOutputs[] = {
            {L"C:\\Origin1",   FileOperationRedirectInstruction::RedirectTo(L"C:\\Target1", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
            {L"C:\\Origin1\\", FileOperationRedirectInstruction::RedirectTo(L"C:\\Target1\\", FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const auto& expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectFileOperation(testInput, FilesystemDirector::EFileOperationMode::OpenExistingFile);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Cerates a filesystem directory with a single filesystem rule and queries it for redirection with an input path that is a prefix of the origin directory.
    // No redirection should occur, but the resulting instruction should indicate that the created file handle should be associated with the query path.
    TEST_CASE(FilesystemDirectory_RedirectFileOperation_PrefixOfOriginDirectory)
    {
        MockFilesystemOperations mockFilesystem;

        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Base\\Origin", L"C:\\Base\\Target")},
        }));

        const std::pair<std::wstring_view, FileOperationRedirectInstruction> kTestInputsAndExpectedOutputs[] = {
            {L"C:\\Base",   FileOperationRedirectInstruction::InterceptWithoutRedirection(FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected)},
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const auto& expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectFileOperation(testInput, FilesystemDirector::EFileOperationMode::OpenExistingFile);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }

    // Creates a filesystem director with a few non-overlapping rules and queries it for redirecting with file inputs that are invalid.
    // Verifies that each time the resulting returned path is not present.
    TEST_CASE(FilesystemDirector_RedirectFileOperation_InvalidInputPath)
    {
        const FilesystemDirector director(MakeFilesystemDirector({
            {L"1", FilesystemRule(L"C:\\Origin1", L"C:\\Target1")},
            {L"2", FilesystemRule(L"C:\\Origin2", L"C:\\Target2")},
            {L"3", FilesystemRule(L"C:\\Origin3", L"C:\\Target3")},
        }));

        const std::pair<std::wstring_view, FileOperationRedirectInstruction> kTestInputsAndExpectedOutputs[] = {
            {L"", FileOperationRedirectInstruction::NoRedirectionOrInterception()}
        };

        for (const auto& testRecord : kTestInputsAndExpectedOutputs)
        {
            const std::wstring_view testInput = testRecord.first;
            const auto& expectedOutput = testRecord.second;

            auto actualOutput = director.RedirectFileOperation(testInput, FilesystemDirector::EFileOperationMode::OpenExistingFile);
            TEST_ASSERT(actualOutput == expectedOutput);
        }
    }
}
