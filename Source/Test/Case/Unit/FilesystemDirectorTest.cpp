/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file FilesystemDirectorTest.cpp
 *   Unit tests for all functionality related to making filesystem-related decisions by applying
 *   filesystem rules.
 **************************************************************************************************/

#include "FilesystemDirector.h"

#include <map>
#include <set>
#include <string>
#include <string_view>

#include <Infra/Core/TemporaryBuffer.h>
#include <Infra/Test/TestCase.h>

#include "FilesystemInstruction.h"
#include "MockFilesystemOperations.h"
#include "PrefixTree.h"

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  /// Type alias for holding filesystem rules created in-line inside individual test cases.
  using TFilesystemRulesByName = std::map<std::wstring_view, FilesystemRule>;

  /// Convenience function for constructing a filesystem director object from a map of rules.
  /// Performs some of the same operations that a filesystem director builder would do internally
  /// but without any of the filesystem consistency checks. Assumes all strings used in filesystem
  /// rules are owned by the test case and therefore does not transfer ownership to the filesystem
  /// director object.
  /// @param [in] filesystemRules Map of filesystem rule names to filesystem rules, which is
  /// consumed using move semantics.
  /// @return Newly-constructed filesystem director object.
  static FilesystemDirector MakeFilesystemDirector(TFilesystemRulesByName&& filesystemRules)
  {
    TFilesystemRulePrefixTree filesystemRulesByOriginDirectory;
    TFilesystemRuleIndexByName filesystemRulesByName;

    for (auto& filesystemRulePair : filesystemRules)
    {
      const auto destinationRelatedRulesContainerNode =
          filesystemRulesByOriginDirectory
              .Emplace(filesystemRulePair.second.GetOriginDirectoryFullPath())
              .first;
      TEST_ASSERT(nullptr != destinationRelatedRulesContainerNode);

      const auto createResult = destinationRelatedRulesContainerNode->Data().InsertRule(
          std::move(filesystemRulePair.second));
      TEST_ASSERT(true == createResult.second);

      const FilesystemRule* const newRule = createResult.first;
      filesystemRulesByName.emplace(newRule->GetName(), newRule);
    }

    return FilesystemDirector(
        std::move(filesystemRulesByOriginDirectory), std::move(filesystemRulesByName));
  }

  /// Convenience helper for evaluating an expected outcome of a container of rules not being
  /// present. Simply compares the pointer to `nullptr`.
  /// @param [in] rule Rule container pointer to check.
  /// @return `true` if the pointer is `nullptr`, `false` otherwise.
  static bool RulesAreNotPresent(const RelatedFilesystemRuleContainer* rules)
  {
    return (nullptr == rules);
  }

  /// Convenience helper for evaluating an expected outcome of a rule container being present and
  /// having a specific set of named rules within it.
  /// @param [in] rules Rule container pointer to check.
  /// @param [in] names Names that the contained rules is expected to have.
  /// @return `true` if the rule pointer is not `nullptr` and the contained rules have the correct
  /// names, `false` otherwise.
  static bool RulesArePresentAndNamed(
      std::set<std::wstring_view>&& names, const RelatedFilesystemRuleContainer* rules)
  {
    if (nullptr == rules) return false;
    if (rules->AllRules().size() != names.size()) return false;

    for (const auto& rule : rules->AllRules())
      if (false == names.contains(rule.GetName())) return false;

    return true;
  }

  /// Convenience helper for evaluating an expected outcome of a rule container being present and
  /// having a single specific named rule within it.
  /// @param [in] rules Rule container pointer to check.
  /// @param [in] name Name that the contained rule is expected to have.
  /// @return `true` if the rule pointer is not `nullptr` and the contained rule has the correct
  /// name, `false` otherwise.
  static inline bool RuleIsPresentAndNamed(
      std::wstring_view name, const RelatedFilesystemRuleContainer* rules)
  {
    return RulesArePresentAndNamed({name}, rules);
  }

  // Creates a filesystem director with a few non-overlapping rules and queries it with a few file
  // inputs. Verifies that each time the correct rule is chosen or, if the file path does not
  // match any rule, no rule is chosen.
  TEST_CASE(FilesystemDirector_SelectRulesForPath_Nominal)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin2", L"C:\\Target2")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin3", L"C:\\Target3")},
    }));

    TEST_ASSERT(
        RuleIsPresentAndNamed(L"1", director.SelectRulesForPath(L"C:\\Origin1\\file1.txt")));
    TEST_ASSERT(RuleIsPresentAndNamed(
        L"2", director.SelectRulesForPath(L"C:\\Origin2\\Subdir2\\file2.txt")));
    TEST_ASSERT(RuleIsPresentAndNamed(
        L"3", director.SelectRulesForPath(L"C:\\Origin3\\Subdir3\\Subdir3_2\\file3.txt")));
    TEST_ASSERT(RulesAreNotPresent(
        director.SelectRulesForPath(L"C:\\Origin4\\Subdir4\\Subdir4_2\\Subdir4_3\\file4.txt")));
  }

  // Creates a filesystem director with a few non-overlapping rules and queries it with a few file
  // inputs. Verifies that each time the correct rule is chosen or, if the file path does not
  // match any rule, no rule is chosen. This variation exercises case insensitivity by varying the
  // case between rule creation and redirection query.
  TEST_CASE(FilesystemDirector_SelectRulesForPath_CaseInsensitive)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin2", L"C:\\Target2")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin3", L"C:\\Target3")},
    }));

    TEST_ASSERT(
        RuleIsPresentAndNamed(L"1", director.SelectRulesForPath(L"C:\\ORIGIN1\\file1.txt")));
    TEST_ASSERT(RuleIsPresentAndNamed(
        L"2", director.SelectRulesForPath(L"C:\\origin2\\SubDir2\\file2.txt")));
    TEST_ASSERT(RuleIsPresentAndNamed(
        L"3", director.SelectRulesForPath(L"C:\\ORiGiN3\\SubdIR3\\SubdIR3_2\\file3.txt")));
    TEST_ASSERT(RulesAreNotPresent(
        director.SelectRulesForPath(L"C:\\OrigIN4\\SUBdir4\\SUBdir4_2\\SUBdir4_3\\file4.txt")));
  }

  // Creates a filesystem with a few overlapping rules and queries it with a few file inputs.
  // Verifies that the most specific rule is always chosen.
  TEST_CASE(FilesystemDirector_SelectRulesForPath_ChooseMostSpecific)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin1\\Origin2", L"C:\\Target2")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin1\\Origin2\\Origin3", L"C:\\Target3")},
    }));

    TEST_ASSERT(
        RuleIsPresentAndNamed(L"1", director.SelectRulesForPath(L"C:\\Origin1\\file1.txt")));
    TEST_ASSERT(RuleIsPresentAndNamed(
        L"2", director.SelectRulesForPath(L"C:\\Origin1\\Origin2\\file2.txt")));
    TEST_ASSERT(RuleIsPresentAndNamed(
        L"3", director.SelectRulesForPath(L"C:\\Origin1\\Origin2\\Origin3\\file3.txt")));
    TEST_ASSERT(RuleIsPresentAndNamed(
        L"2",
        director.SelectRulesForPath(L"C:\\Origin1\\Origin2\\AnotherDirectory\\somefile.txt")));
    TEST_ASSERT(RuleIsPresentAndNamed(
        L"1",
        director.SelectRulesForPath(
            L"C:\\Origin1\\AnotherPathway\\SomeDirectory\\Subdir\\logfile.log")));
  }

  // Creates a filesystem director with a single rule at a deep level in the hierarchy and queries
  // it a few times to see if it can successfully identify rule prefixes.
  TEST_CASE(FilesystemDirector_IsPrefixForAnyRule_Nominal)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Level1\\Level2\\Level3\\Origin", L"C:\\Target")},
    }));

    constexpr std::pair<std::wstring_view, bool> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\", true},
        {L"C:\\Level1", true},
        {L"C:\\Level1\\Level2\\", true},
        {L"C:\\Level1\\Level2\\Level3", true},
        {L"C:\\Level1\\Level2\\Level3\\Origin\\", true},
        {L"X:\\", false},
        {L"C:\\Unrelated\\Level2", false}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const bool expectedOutput = testRecord.second;

      const bool actualOutput = director.IsPrefixForAnyRule(testInput);
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director with a few non-overlapping rules and queries it for redirection
  // with a few file inputs. Verifies that each time the resulting redirected path is correct.
  TEST_CASE(FilesystemDirector_GetInstructionForFileOperation_Nominal)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin2", L"C:\\Target2")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin3", L"C:\\Target3")},
    }));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\Origin1\\file1.txt",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target1\\file1.txt", EAssociateNameWithHandle::Unredirected)},
        {L"C:\\Origin2\\Subdir2\\file2.txt",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target2\\Subdir2\\file2.txt", EAssociateNameWithHandle::Unredirected)},
        {L"C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt",
             EAssociateNameWithHandle::Unredirected)}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director with two rules having the same origin directory and queries with
  // a few file name inputs. Verifies that each time the resulting redirected path is correct.
  TEST_CASE(
      FilesystemDirector_GetInstructionForFileOperation_MultipleRulesSameOriginDirectory_QueryForContentsOnly)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(MakeFilesystemDirector(
        {{L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\TargetForTxt", {L"*.txt"})},
         {L"2", FilesystemRule(L"2", L"C:\\Origin", L"C:\\TargetForBin", {L"*.bin"})},
         {L"3", FilesystemRule(L"3", L"C:\\Origin", L"C:\\TargetForExe", {L"*.exe"})}}));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\Origin\\file1.txt",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\TargetForTxt\\file1.txt", EAssociateNameWithHandle::Unredirected)},
        {L"C:\\Origin\\file2.bin",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\TargetForBin\\file2.bin", EAssociateNameWithHandle::Unredirected)},
        {L"C:\\Origin\\file3.exe",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\TargetForExe\\file3.exe", EAssociateNameWithHandle::Unredirected)}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director with two rules having the same origin directory and queries with
  // a few file name inputs that target subdirectories of the origin directory. Verifies that each
  // time the resulting redirected path is correct. This test case exercises the filesystem director
  // behavior that only the immediate content of the origin directory is compared with file
  // patterns. Here, even though the filename at the end of the path does not match any of the file
  // patterns, the name of the subdirectory that is the immediate child of the origin directory does
  // match and hence a redirection is warranted in all cases.
  TEST_CASE(
      FilesystemDirector_GetInstructionForFileOperation_MultipleRulesSameOriginDirectory_QueryForSubdirectories)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(MakeFilesystemDirector(
        {{L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\TargetForTxt", {L"*.txt"})},
         {L"2", FilesystemRule(L"2", L"C:\\Origin", L"C:\\TargetForBin", {L"*.bin"})},
         {L"3", FilesystemRule(L"3", L"C:\\Origin", L"C:\\TargetForExe", {L"*.exe"})}}));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\Origin\\SubDir.txt\\file1",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\TargetForTxt\\SubDir.txt\\file1", EAssociateNameWithHandle::Unredirected)},
        {L"C:\\Origin\\SubDir.bin\\file2",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\TargetForBin\\SubDir.bin\\file2", EAssociateNameWithHandle::Unredirected)},
        {L"C:\\Origin\\SubDir.exe\\AnotherSubDir\\file3",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\TargetForExe\\SubDir.exe\\AnotherSubDir\\file3",
             EAssociateNameWithHandle::Unredirected)}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director with a few non-overlapping rules and queries it for redirection
  // with a few file inputs. Verifies that each time the resulting redirected path is correct.
  // This variation of the test case uses the overlay redirection mode.
  TEST_CASE(FilesystemDirector_GetInstructionForFileOperation_Overlay)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1", {}, ERedirectMode::Overlay)},
        {L"2", FilesystemRule(L"2", L"C:\\Origin2", L"C:\\Target2", {}, ERedirectMode::Overlay)},
        {L"3", FilesystemRule(L"3", L"C:\\Origin3", L"C:\\Target3", {}, ERedirectMode::Overlay)},
    }));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\Origin1\\file1.txt",
         FileOperationInstruction::OverlayRedirectTo(
             L"C:\\Target1\\file1.txt", EAssociateNameWithHandle::Unredirected)},
        {L"C:\\Origin2\\Subdir2\\file2.txt",
         FileOperationInstruction::OverlayRedirectTo(
             L"C:\\Target2\\Subdir2\\file2.txt", EAssociateNameWithHandle::Unredirected)},
        {L"C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt",
         FileOperationInstruction::OverlayRedirectTo(
             L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt",
             EAssociateNameWithHandle::Unredirected)}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director with a few non-overlapping rules and queries it for redirection
  // with a few file inputs. Verifies that each time the resulting redirected path is correct.
  // This variation of the test case uses the overlay redirection mode and a create disposition
  // that allows file creation. Since a new file is permitted to be created in overlay mode, a
  // preference is expected to be encoded in the instruction for opening an existing file rather
  // than creating a new file.
  TEST_CASE(FilesystemDirector_GetInstructionForFileOperation_OverlayWithFileCreation)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1", {}, ERedirectMode::Overlay)},
        {L"2", FilesystemRule(L"2", L"C:\\Origin2", L"C:\\Target2", {}, ERedirectMode::Overlay)},
        {L"3", FilesystemRule(L"3", L"C:\\Origin3", L"C:\\Target3", {}, ERedirectMode::Overlay)},
    }));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\Origin1\\file1.txt",
         FileOperationInstruction::OverlayRedirectTo(
             L"C:\\Target1\\file1.txt",
             EAssociateNameWithHandle::Unredirected,
             ECreateDispositionPreference::PreferOpenExistingFile,
             {static_cast<int>(EExtraPreOperation::EnsurePathHierarchyExists)},
             L"C:\\Target1")},
        {L"C:\\Origin2\\Subdir2\\file2.txt",
         FileOperationInstruction::OverlayRedirectTo(
             L"C:\\Target2\\Subdir2\\file2.txt",
             EAssociateNameWithHandle::Unredirected,
             ECreateDispositionPreference::PreferOpenExistingFile)},
        {L"C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt",
         FileOperationInstruction::OverlayRedirectTo(
             L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt",
             EAssociateNameWithHandle::Unredirected,
             ECreateDispositionPreference::PreferOpenExistingFile)}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::CreateNewOrOpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Verifies that pre-operations are correctly added when a hierarchy exists on the origin side
  // and the file operation attempts to open an existing file. When the query is for a directory
  // that exists on the origin side, it is expected that a pre-operation is added to ensure the
  // same hierarchy exists on the target side. When the query is for a file, whether or not it
  // exists on the origin side, no such pre-operation is necessary.
  TEST_CASE(
      FilesystemDirector_GetInstructionForFileOperation_OriginHierarchyExists_OpenExistingFile)
  {
    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\Origin1");
    mockFilesystem.AddDirectory(L"C:\\Origin2\\Subdir2");
    mockFilesystem.AddDirectory(L"C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C");
    mockFilesystem.AddFile(L"C:\\Origin1\\file1.txt");

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin2", L"C:\\Target2")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin3", L"C:\\Target3")},
    }));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\Origin1",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target1",
             EAssociateNameWithHandle::Unredirected,
             {static_cast<int>(EExtraPreOperation::EnsurePathHierarchyExists)},
             L"C:\\Target1")},
        {L"C:\\Origin2\\Subdir2",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target2\\Subdir2",
             EAssociateNameWithHandle::Unredirected,
             {static_cast<int>(EExtraPreOperation::EnsurePathHierarchyExists)},
             L"C:\\Target2\\Subdir2")},
        {L"C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C",
             EAssociateNameWithHandle::Unredirected,
             {static_cast<int>(EExtraPreOperation::EnsurePathHierarchyExists)},
             L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C")},
        {L"C:\\Origin1\\file1.txt",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target1\\file1.txt", EAssociateNameWithHandle::Unredirected, {}, L"")},
        {L"C:\\Origin2\\Subdir2\\file2.bin",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target2\\Subdir2\\file2.bin", EAssociateNameWithHandle::Unredirected, {}, L"")}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Verifies that pre-operations are correctly added when a hierarchy exists on the origin side
  // and the file operation attempts to create a new file. Regardless of the nature of the
  // filesystem entity that is the subject of the query (file or directory) a pre-operation is
  // needed to ensure the parent hierarchy exists on the target side if it also exists on the
  // origin side.
  TEST_CASE(FilesystemDirector_GetInstructionForFileOperation_OriginHierarchyExists_CreateNewFile)
  {
    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\Origin1");

    const FilesystemDirector director(
        MakeFilesystemDirector({{L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")}}));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\Origin1\\AnyTypeOfFile",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target1\\AnyTypeOfFile",
             EAssociateNameWithHandle::Unredirected,
             {static_cast<int>(EExtraPreOperation::EnsurePathHierarchyExists)},
             L"C:\\Target1")}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::CreateNewFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Verifies that pre-operations are correctly added when a hierarchy does not exist on the origin
  // side and the file operation attempts to create a new file inside a filesystem rule's origin
  // directory. Regardless of the nature of the filesystem entity that is the subject of the query
  // (file or directory) a pre-operation is needed to ensure the parent hierarchy exists on the
  // target side if the directory matches a filesystem rule's origin directory.
  TEST_CASE(
      FilesystemDirector_GetInstructionForFileOperation_OriginHierarchyDoesNotExist_CreateNewFileInOriginDirectory)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(
        MakeFilesystemDirector({{L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")}}));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\Origin1\\AnyTypeOfFile",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target1\\AnyTypeOfFile",
             EAssociateNameWithHandle::Unredirected,
             {static_cast<int>(EExtraPreOperation::EnsurePathHierarchyExists)},
             L"C:\\Target1")}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::CreateNewFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director with a few non-overlapping rules and queries it for redirection
  // with a few directory inputs. In this case all of the query inputs have trailing backslash
  // characters, which is allowed for directories. Verifies that the trailing backslash is
  // preserved after the redirection operation completes.
  TEST_CASE(FilesystemDirector_GetInstructionForFileOperation_PreservesTrailingBackslash)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin2", L"C:\\Target2")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin3", L"C:\\Target3")},
    }));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\Origin1\\Subdir1\\",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target1\\Subdir1\\", EAssociateNameWithHandle::Unredirected)},
        {L"C:\\Origin2\\Subdir2\\Subdir2B\\",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target2\\Subdir2\\Subdir2B\\", EAssociateNameWithHandle::Unredirected)},
        {L"C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C\\",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C\\",
             EAssociateNameWithHandle::Unredirected)}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director with a few non-overlapping rules and queries it for redirection
  // with a few file inputs. Verifies that each time the resulting redirected path is correct.
  // This test case variation additionally adds namespace prefixes to the filenames submitted for
  // query. These should be passed through unchanged.
  TEST_CASE(
      FilesystemDirector_GetInstructionForFileOperation_QueryInputContainsWindowsNamespacePrefix)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin2", L"C:\\Target2")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin3", L"C:\\Target3")},
    }));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"\\??\\C:\\Origin1\\file1.txt",
         FileOperationInstruction::SimpleRedirectTo(
             L"\\??\\C:\\Target1\\file1.txt", EAssociateNameWithHandle::Unredirected)},
        {L"\\\\?\\C:\\Origin2\\Subdir2\\file2.txt",
         FileOperationInstruction::SimpleRedirectTo(
             L"\\\\?\\C:\\Target2\\Subdir2\\file2.txt", EAssociateNameWithHandle::Unredirected)},
        {L"\\\\.\\C:\\Origin3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt",
         FileOperationInstruction::SimpleRedirectTo(
             L"\\\\.\\C:\\Target3\\Subdir3\\Subdir3B\\Subdir3C\\file3.txt",
             EAssociateNameWithHandle::Unredirected)}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director with a few non-overlapping rules and queries it with inputs
  // that should not be redirected due to no match.
  TEST_CASE(FilesystemDirector_GetInstructionForFileOperation_NonRedirectedInputPath)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin2", L"C:\\Target2")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin3", L"C:\\Target3")},
    }));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"D:\\NonRedirectedFile\\Subdir\\file.log",
         FileOperationInstruction::NoRedirectionOrInterception()}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director a single filesystem rule and queries it with inputs that should
  // not be redirected due to no match. In this case the input query string is not
  // null-terminated, but the buffer itself contains a null-terminated string that ordinarily
  // would be redirected. If the implementation is properly handling non-null-terminated input
  // string views then no redirection should occur, otherwise an erroneous redirection will occur.
  // One query uses a Windows namespace prefix, and the other does not.
  TEST_CASE(FilesystemDirector_GetInstructionForFileOperation_NoRedirectionNotNullTerminated)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Base\\Origin", L"C:\\Base\\Target")},
    }));

    // String buffer identifies "C:\Base\Origin" which intuitively should be redirected to
    // "C:\Base\Target". However, the length field of the string view object means that the view
    // only represents "C:\Base" or "C:\Base\" which has no matching rule and should not be
    // redirected. These inputs are prefixes to rule origin directories and therefore the
    // instruction should be not to redirect but to intercept for processing for possible future
    // filename combination.
    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {std::wstring_view(L"C:\\Base\\Origin", std::wstring_view(L"C:\\Base").length()),
         FileOperationInstruction::InterceptWithoutRedirection(
             EAssociateNameWithHandle::Unredirected)},
        {std::wstring_view(L"C:\\Base\\Origin", std::wstring_view(L"C:\\Base\\").length()),
         FileOperationInstruction::InterceptWithoutRedirection(
             EAssociateNameWithHandle::Unredirected)},
        {std::wstring_view(
             L"\\??\\C:\\Base\\Origin", std::wstring_view(L"\\??\\C:\\Base").length()),
         FileOperationInstruction::InterceptWithoutRedirection(
             EAssociateNameWithHandle::Unredirected)},
        {std::wstring_view(
             L"\\??\\C:\\Base\\Origin", std::wstring_view(L"\\??\\C:\\Base\\").length()),
         FileOperationInstruction::InterceptWithoutRedirection(
             EAssociateNameWithHandle::Unredirected)}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director with a single filesystem rule and queries it for redirection
  // with an input path exactly equal to the origin directory. Verifies that redirection to the
  // target directory does occur but the associated filename with the newly-created handle is the
  // origin directory. The instruction should also indicate to ensure that the target directory
  // exists.
  TEST_CASE(FilesystemDirector_GetInstructionForFileOperation_EqualsOriginDirectory)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")},
    }));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\Origin1",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target1", EAssociateNameWithHandle::Unredirected)},
        {L"C:\\Origin1\\",
         FileOperationInstruction::SimpleRedirectTo(
             L"C:\\Target1\\", EAssociateNameWithHandle::Unredirected)},
    };

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Cerates a filesystem directory with a single filesystem rule and queries it for redirection
  // with an input path that is a prefix of the origin directory. No redirection should occur, but
  // the resulting instruction should indicate that the created file handle should be associated
  // with the query path.
  TEST_CASE(FilesystemDirectory_GetInstructionForFileOperation_PrefixOfOriginDirectory)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Base\\Origin", L"C:\\Base\\Target")},
    }));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"C:\\Base",
         FileOperationInstruction::InterceptWithoutRedirection(
             EAssociateNameWithHandle::Unredirected)},
    };

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director with a few non-overlapping rules and queries it for redirecting
  // with file inputs that are invalid. Verifies that each time the resulting returned path is not
  // present.
  TEST_CASE(FilesystemDirector_GetInstructionForFileOperation_InvalidInputPath)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin1", L"C:\\Target1")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin2", L"C:\\Target2")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin3", L"C:\\Target3")},
    }));

    const std::pair<std::wstring_view, FileOperationInstruction> kTestInputsAndExpectedOutputs[] = {
        {L"", FileOperationInstruction::NoRedirectionOrInterception()}};

    for (const auto& testRecord : kTestInputsAndExpectedOutputs)
    {
      const std::wstring_view testInput = testRecord.first;
      const auto& expectedOutput = testRecord.second;

      auto actualOutput = director.GetInstructionForFileOperation(
          testInput, FileAccessMode::ReadOnly(), CreateDisposition::OpenExistingFile());
      TEST_ASSERT(actualOutput == expectedOutput);
    }
  }

  // Creates a filesystem director with a single filesystem rule without file patterns.
  // Requests a directory enumeration instruction and verifies that it correctly indicates to
  // enumerate the target directory without any further processing.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateOriginDirectory_Nominal)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target")},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with multiple filesystem rules all with the same origin
  // directory. Requests a directory enumeration instruction and verifies that it correctly
  // indicates to merge all of the appropriately-matched target directory contents. One of the
  // filesystem rules has no file patterns, and all of them use Simple redirection mode, so the
  // origin directory itself should not be enumerated. Whichever rule has its target directory as
  // the real opened path is the one whose enumeration is simplified to use the real opened path
  // instead of the target directory.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateOriginDirectory_NoOriginSideEnumeration)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target1", {L"*.pdf"})},
        {L"2", FilesystemRule(L"2", L"C:\\Origin", L"C:\\Target2", {L"*.txt"})},
        {L"3", FilesystemRule(L"3", L"C:\\Origin", L"C:\\Target3")},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target1";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectories({
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                IncludeOnlyMatchingFilenames(
                    EDirectoryPathSource::FilePatternSourceTargetDirectory,
                    *director.SelectRulesForPath(L"C:\\Origin"),
                    EFilePatternMatchCondition::MatchByPositionInvertAllPriorToSelected,
                    0),
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                IncludeOnlyMatchingFilenames(
                    EDirectoryPathSource::FilePatternSourceTargetDirectory,
                    *director.SelectRulesForPath(L"C:\\Origin"),
                    EFilePatternMatchCondition::MatchByPositionInvertAllPriorToSelected,
                    1),
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                IncludeOnlyMatchingFilenames(
                    EDirectoryPathSource::FilePatternSourceTargetDirectory,
                    *director.SelectRulesForPath(L"C:\\Origin"),
                    EFilePatternMatchCondition::MatchByPositionInvertAllPriorToSelected,
                    2),
        });
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with multiple filesystem rules all with the same origin
  // directory. Requests a directory enumeration instruction and verifies that it correctly
  // indicates to merge all of the appropriately-matched target directory contents. All of the rules
  // have file patterns, so the origin side directory needs to be enumerated as well. Whichever rule
  // has its target directory as the real opened path is the one whose enumeration is simplified to
  // use the real opened path instead of the target directory.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateOriginDirectory_WithOriginSideEnumeration)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target1", {L"*.pdf"})},
        {L"2", FilesystemRule(L"2", L"C:\\Origin", L"C:\\Target2", {L"*.txt"})},
        {L"3", FilesystemRule(L"3", L"C:\\Origin", L"C:\\Target3", {L"*.log"})},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target2";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectories(
            {DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                 IncludeOnlyMatchingFilenames(
                     EDirectoryPathSource::FilePatternSourceTargetDirectory,
                     *director.SelectRulesForPath(L"C:\\Origin"),
                     EFilePatternMatchCondition::MatchByPositionInvertAllPriorToSelected,
                     0),
             DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                 IncludeOnlyMatchingFilenames(
                     EDirectoryPathSource::FilePatternSourceTargetDirectory,
                     *director.SelectRulesForPath(L"C:\\Origin"),
                     EFilePatternMatchCondition::MatchByPositionInvertAllPriorToSelected,
                     1),
             DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                 IncludeOnlyMatchingFilenames(
                     EDirectoryPathSource::FilePatternSourceTargetDirectory,
                     *director.SelectRulesForPath(L"C:\\Origin"),
                     EFilePatternMatchCondition::MatchByPositionInvertAllPriorToSelected,
                     2),
             DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                 IncludeAllExceptMatchingFilenames(
                     EDirectoryPathSource::AssociatedPath,
                     *director.SelectRulesForPath(L"C:\\Origin"),
                     EFilePatternMatchCondition::MatchByRedirectModeInvertOverlay)});
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with multiple filesystem rules all with the same origin
  // directory. Requests a directory enumeration instruction and verifies that it correctly
  // indicates to merge all of the appropriately-matched target directory contents. File patterns
  // vary, but all of the rules use overlay mode, so the final enumeration needs to be on the origin
  // side. The rule without a file pattern is also the one that originally did the redirection, so
  // its entry in the directory enumeration instruction can be simplified somewhat.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateOriginDirectoryMultiRuleAllOverlay)
  {
    // Filesystem rules should exist in the container in this order. Their indices would
    // respectively be 0, 1, and 2.
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1",
         FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target1", {L"*.pdf"}, ERedirectMode::Overlay)},
        {L"2",
         FilesystemRule(L"2", L"C:\\Origin", L"C:\\Target2", {L"*.exe"}, ERedirectMode::Overlay)},
        {L"3", FilesystemRule(L"3", L"C:\\Origin", L"C:\\Target3", {}, ERedirectMode::Overlay)},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target3";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectories(
            {DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                 IncludeOnlyMatchingFilenames(
                     EDirectoryPathSource::FilePatternSourceTargetDirectory,
                     *director.SelectRulesForPath(L"C:\\Origin"),
                     EFilePatternMatchCondition::MatchByPositionInvertAllPriorToSelected,
                     0),
             DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                 IncludeOnlyMatchingFilenames(
                     EDirectoryPathSource::FilePatternSourceTargetDirectory,
                     *director.SelectRulesForPath(L"C:\\Origin"),
                     EFilePatternMatchCondition::MatchByPositionInvertAllPriorToSelected,
                     1),
             DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                 IncludeOnlyMatchingFilenames(
                     EDirectoryPathSource::FilePatternSourceTargetDirectory,
                     *director.SelectRulesForPath(L"C:\\Origin"),
                     EFilePatternMatchCondition::MatchByPositionInvertAllPriorToSelected,
                     2),
             DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                 EDirectoryPathSource::AssociatedPath)});
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with a single filesystem rule without file patterns.
  // Requests a directory enumeration instruction such that the rule is configured for overlay
  // mode and verifies that it correctly merges the target and origin directory contents.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateOriginDirectoryInOverlayMode)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target", {}, ERedirectMode::Overlay)},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectories(
            {DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                 EDirectoryPathSource::RealOpenedPath),
             DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                 EDirectoryPathSource::AssociatedPath)});
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with a single filesystem rule with file patterns.
  // Requests a directory enumeration instruction and verifies that it correctly indicates to
  // merge in-scope target directory contents with out-of-scope origin directory contents.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateOriginDirectoryWithFilePattern)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target", {L"*.txt", L"*.rtf"})},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectories(
            {DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                 IncludeOnlyMatchingFilenames(
                     EDirectoryPathSource::RealOpenedPath, *director.FindRuleByName(L"1")),
             DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                 IncludeAllExceptMatchingFilenames(
                     EDirectoryPathSource::AssociatedPath, *director.FindRuleByName(L"1"))});
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with three filesystem rules, two of which have origin
  // directories that are direct children of the third. Requests a directory enumeration
  // instruction and verifies that it correcly inserts both origin directories into the
  // enumeration result.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateOriginDirectoryWithChildRules)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin\\SubA", L"C:\\TargetA")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin\\SubB", L"C:\\TargetB")},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::InsertRuleOriginDirectoryNames(
            {*director.FindRuleByName(L"2"), *director.FindRuleByName(L"3")});
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateOriginDirectoryWithSingleMultiRuleChild)
  {
    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\TargetD");

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin\\Subdir", L"C:\\TargetA")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin\\Subdir", L"C:\\TargetB")},
        {L"4", FilesystemRule(L"4", L"C:\\Origin\\Subdir", L"C:\\TargetC")},
        {L"5", FilesystemRule(L"5", L"C:\\Origin\\Subdir", L"C:\\TargetD")},
        {L"6", FilesystemRule(L"6", L"C:\\Origin\\Subdir", L"C:\\TargetE")},
        {L"7", FilesystemRule(L"7", L"C:\\Origin\\Subdir", L"C:\\TargetF")},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target";

    // The target directory for rule "5" exists in the filesystem and so that is the rule that
    // should be selected. No other target directory exists.
    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::InsertRuleOriginDirectoryNames(
            {*director.FindRuleByName(L"5")});
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with multiple filesystem rules, one of which has a top-level
  // origin directory and the others of which have origin directories that are a direct child of
  // the top-level origin directory. All target directories also exist in the filesystem. Requests
  // a directory enumeration instruction and verifies that it correcly inserts all of the direct
  // child rule origin directories into the enumeration result such that the directories to be
  // inserted are in sorted order. The sorting is expected to be by origin directory base name.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateOriginDirectoryWithMultipleSortedChildRules)
  {
    MockFilesystemOperations mockFilesystem;

    // Rule names are random and totally unordered strings to make sure that rule name is not
    // used for sorting. Rules are inserted in arbitrary order with origin directories also
    // out-of-order. The sorting should be on the basis of the "SubX..." part of the origin
    // directories.
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"hLHzENdEZK", FilesystemRule(L"hLHzENdEZK", L"C:\\Origin", L"C:\\Target")},
        {L"FinvonNsbQ", FilesystemRule(L"FinvonNsbQ", L"C:\\Origin\\SubE1", L"C:\\TargetE")},
        {L"PKwVeAGYUo", FilesystemRule(L"PKwVeAGYUo", L"C:\\Origin\\SubC123456", L"C:\\TargetC")},
        {L"sIyMXWTnKx", FilesystemRule(L"sIyMXWTnKx", L"C:\\Origin\\SubA", L"C:\\TargetA")},
        {L"OlwBqHThwu", FilesystemRule(L"OlwBqHThwu", L"C:\\Origin\\SubD12345678", L"C:\\TargetD")},
        {L"jSRmdsNLMw", FilesystemRule(L"jSRmdsNLMw", L"C:\\Origin\\SubB123", L"C:\\TargetB")},
        {L"FVWrFofofc", FilesystemRule(L"FVWrFofofc", L"C:\\Origin\\SubF12345", L"C:\\TargetF")},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::InsertRuleOriginDirectoryNames(
            {*director.FindRuleByName(L"sIyMXWTnKx"),
             *director.FindRuleByName(L"jSRmdsNLMw"),
             *director.FindRuleByName(L"PKwVeAGYUo"),
             *director.FindRuleByName(L"OlwBqHThwu"),
             *director.FindRuleByName(L"FinvonNsbQ"),
             *director.FindRuleByName(L"FVWrFofofc")});
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with three filesystem rules, two of which have origin
  // directories that are direct children of the third. Of those two, one has a target directory
  // that exists and the other does not. All three rules have file patterns, although this only
  // matters for the top-level rule with the children. Requests a directory enumeration
  // instruction and verifies that it both correctly indicates to merge in-scope target directory
  // contents with out-of-scope origin directory contents and correctly inserts both of the origin
  // directories into the enumeration result.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateOriginDirectoryWithFilePatternAndChildRules)
  {
    MockFilesystemOperations mockFilesystem;

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target", {L"*.txt", L"*.rtf"})},
        {L"2", FilesystemRule(L"2", L"C:\\Origin\\SubA", L"C:\\TargetA", {L"*.exe"})},
        {L"3", FilesystemRule(L"3", L"C:\\Origin\\SubB", L"C:\\TargetB", {L"*.bat"})},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectoriesAndInsertRuleOriginDirectoryNames(
            {DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                 IncludeOnlyMatchingFilenames(
                     EDirectoryPathSource::RealOpenedPath, *director.FindRuleByName(L"1")),
             DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                 IncludeAllExceptMatchingFilenames(
                     EDirectoryPathSource::AssociatedPath, *director.FindRuleByName(L"1"))},
            {*director.FindRuleByName(L"2"), *director.FindRuleByName(L"3")});
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with a single filesystem rule with no file patterns.
  // Requests a directory enumeration instruction for a child of the origin directory and
  // verifies that it correctly indicates to enumerate the target-side redirected directory
  // without any further processing.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateChildOfOriginDirectory)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target")},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin\\Subdir123";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target\\Subdir123";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with a single filesystem rule with no file patterns.
  // Requests a directory enumeration instruction for a descendant of the origin directory and
  // verifies that it correctly indicates to enumerate the target-side redirected directory
  // without any further processing.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateDescendantOfOriginDirectory)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target")},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin\\Subdir123\\AnotherDir";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target\\Subdir123\\AnotherDir";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with a single filesystem rule without file patterns.
  // Requests a directory enumeration instruction for a descendant of the origin directory in
  // overlay mode and verifies that it correctly indicates to enumerate both target-side and
  // origin-side directories.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateDescendantOfOriginDirectoryInOverlayMode)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target", {}, ERedirectMode::Overlay)},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin\\Subdir123\\AnotherDir";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target\\Subdir123\\AnotherDir";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectories(
            {DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                 EDirectoryPathSource::RealOpenedPath),
             DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                 EDirectoryPathSource::AssociatedPath)});
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with a single filesystem rule with file patterns.
  // Requests a directory enumeration instruction for a descendant of the origin directory, which
  // is also within its scope, and verifies that it correctly indicates to enumerate the
  // target-side redirected directory without any further processing.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateDescendantOfOriginDirectoryWithFilePatterns)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target", {L"Subdir*"})},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin\\Subdir123\\AnotherDir";
    constexpr std::wstring_view realOpenedPath = L"C:\\Target\\Subdir123\\AnotherDir";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with a single filesystem rule and opens the parent of the rule's
  // origin directory for enumeration. Verifies that the rule's origin directory will be inserted
  // into the output.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateParentOfOriginDirectory)
  {
    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\Origin");

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin\\Subdir", L"C:\\Target")},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Origin";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::InsertRuleOriginDirectoryNames(
            {*director.FindRuleByName(L"1")});
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director with a single filesystem rule and opens the parent of the rule's
  // origin directory for enumeration. Verifies that the rule's origin directory will be inserted
  // into the output.
  TEST_CASE(
      FilesystemDirector_GetInstructionForDirectoryEnumeration_EnumerateParentOfMultiRuleOriginDirectory)
  {
    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\Origin");
    mockFilesystem.AddDirectory(L"C:\\Target1");

    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin\\Subdir", L"C:\\Target1")},
        {L"2", FilesystemRule(L"2", L"C:\\Origin\\Subdir", L"C:\\Target2")},
        {L"3", FilesystemRule(L"3", L"C:\\Origin\\Subdir", L"C:\\Target3")},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\Origin";
    constexpr std::wstring_view realOpenedPath = L"C:\\Origin";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::InsertRuleOriginDirectoryNames(
            {*director.FindRuleByName(L"1")});
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }

  // Creates a filesystem director and requests an instruction for directory enumeration with a
  // directory that is totally outside the scope of any filesystem rules. The instruction is
  // expected to indicate that the request should be passed through to the system without
  // modification.
  TEST_CASE(FilesystemDirectory_GetInstructionForDirectoryEnumeration_EnumerateUnrelatedDirectory)
  {
    const FilesystemDirector director(MakeFilesystemDirector({
        {L"1", FilesystemRule(L"1", L"C:\\Origin", L"C:\\Target")},
    }));

    constexpr std::wstring_view associatedPath = L"C:\\SomeOtherDirectory";
    constexpr std::wstring_view realOpenedPath = L"C:\\SomeOtherDirectory";

    const DirectoryEnumerationInstruction expectedDirectoryEnumerationInstruction =
        DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
    const DirectoryEnumerationInstruction actualDirectoryEnumerationInstruction =
        director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);

    TEST_ASSERT(actualDirectoryEnumerationInstruction == expectedDirectoryEnumerationInstruction);
  }
} // namespace PathwinderTest
