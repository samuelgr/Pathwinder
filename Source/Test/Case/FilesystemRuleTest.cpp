/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file FilesystemRuleTest.cpp
 *   Unit tests for filesystem rule objects.
 **************************************************************************************************/

#include "TestCase.h"

#include "FilesystemRule.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Strings.h"
#include "TemporaryBuffer.h"

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  // Verifies that a filesystem rule can be created with file patterns and that those file patterns
  // are properly made available once it is created.
  TEST_CASE(FilesystemRule_GetFilePatterns_Nominal)
  {
    const std::vector<std::wstring> testFilePatterns = {L"*.bin", L"*.txt", L"*.log"};

    const FilesystemRule testRule(
        L"", L"C:\\Origin", L"C:\\Target", std::vector<std::wstring>(testFilePatterns));

    const std::vector<std::wstring>& expectedFilePatterns = testFilePatterns;
    const std::vector<std::wstring>& actualFilePatterns = testRule.GetFilePatterns();

    TEST_ASSERT(true == testRule.HasFilePatterns());
    TEST_ASSERT(actualFilePatterns.size() == expectedFilePatterns.size());
    for (size_t i = 0; i < expectedFilePatterns.size(); ++i)
      TEST_ASSERT(
          Strings::EqualsCaseInsensitive<wchar_t>(actualFilePatterns[i], expectedFilePatterns[i]));
  }

  // Verifies that a filesystem rule can be created without file patterns and that the lack of file
  // patterns is properly made available once it is created.
  TEST_CASE(FilesystemRule_GetFilePatterns_NoneDefined)
  {
    const FilesystemRule testRule(L"", L"C:\\Origin", L"C:\\Target", {});

    const std::vector<std::wstring>& expectedFilePatterns = {};
    const std::vector<std::wstring>& actualFilePatterns = testRule.GetFilePatterns();

    TEST_ASSERT(false == testRule.HasFilePatterns());
    TEST_ASSERT(actualFilePatterns.size() == expectedFilePatterns.size());
    for (size_t i = 0; i < expectedFilePatterns.size(); ++i)
      TEST_ASSERT(
          Strings::EqualsCaseInsensitive<wchar_t>(actualFilePatterns[i], expectedFilePatterns[i]));
  }

  // Verifies that a filesystem rule can be created with file patterns whereby they are equivalent
  // to matching all possible filenames. Once created, the filesystem rule should have no file
  // patterns defined.
  TEST_CASE(FilesystemRule_GetFilePatterns_EquivalentToNoneDefined)
  {
    constexpr std::wstring_view kTestFilePatternsToTryOneByOne[] = {L"", L"*", L"**", L"***"};

    for (const auto testFilePatternInput : kTestFilePatternsToTryOneByOne)
    {
      const FilesystemRule testRule(
          L"", L"C:\\Origin", L"C:\\Target", {std::wstring(testFilePatternInput)});

      const std::vector<std::wstring>& expectedFilePatterns = {};
      const std::vector<std::wstring>& actualFilePatterns = testRule.GetFilePatterns();

      TEST_ASSERT(false == testRule.HasFilePatterns());
      TEST_ASSERT(actualFilePatterns.size() == expectedFilePatterns.size());
      for (size_t i = 0; i < expectedFilePatterns.size(); ++i)
        TEST_ASSERT(Strings::EqualsCaseInsensitive<wchar_t>(
            actualFilePatterns[i], expectedFilePatterns[i]));
    }
  }

  // Verifies that origin and target directory strings are parsed correctly into origin and target
  // full paths and names.
  TEST_CASE(FilesystemRule_GetOriginAndTargetDirectories)
  {
    constexpr std::pair<
        std::pair<std::wstring_view, std::wstring_view>,
        std::pair<std::wstring_view, std::wstring_view>>
        kDirectoryTestRecords[] = {
            {{L"C:\\Directory", L"Directory"},
             {L"D:\\Some Other Directory", L"Some Other Directory"}},
            {{L"C:", L"C:"}, {L"D:", L"D:"}},
            {{L"\\sharepath\\shared folder$\\another shared folder", L"another shared folder"},
             {L"D:\\Long\\Sub Directory \\   Path To Directory\\Yes", L"Yes"}}};

    for (const auto& kDirectoryTestRecord : kDirectoryTestRecords)
    {
      const std::wstring_view expectedOriginDirectoryFullPath = kDirectoryTestRecord.first.first;
      const std::wstring_view expectedOriginDirectoryName = kDirectoryTestRecord.first.second;
      const std::wstring_view expectedTargetDirectoryFullPath = kDirectoryTestRecord.second.first;
      const std::wstring_view expectedTargetDirectoryName = kDirectoryTestRecord.second.second;

      const FilesystemRule filesystemRule(
          L"", expectedOriginDirectoryFullPath, expectedTargetDirectoryFullPath);

      const std::wstring_view actualOriginDirectoryFullPath =
          filesystemRule.GetOriginDirectoryFullPath();
      TEST_ASSERT(actualOriginDirectoryFullPath == expectedOriginDirectoryFullPath);

      const std::wstring_view actualOriginDirectoryName = filesystemRule.GetOriginDirectoryName();
      TEST_ASSERT(actualOriginDirectoryName == expectedOriginDirectoryName);

      const std::wstring_view actualTargetDirectoryFullPath =
          filesystemRule.GetTargetDirectoryFullPath();
      TEST_ASSERT(actualTargetDirectoryFullPath == expectedTargetDirectoryFullPath);

      const std::wstring_view actualTargetDirectoryName = filesystemRule.GetTargetDirectoryName();
      TEST_ASSERT(actualTargetDirectoryName == expectedTargetDirectoryName);
    }
  }

  // Verifies that origin and target directory strings are parsed correctly and their immediate
  // parent directories are returned.
  TEST_CASE(FilesystemRule_GetOriginAndTargetDirectoryParents)
  {
    constexpr std::pair<
        std::pair<std::wstring_view, std::wstring_view>,
        std::pair<std::wstring_view, std::wstring_view>>
        kDirectoryTestRecords[] = {
            {{L"C:\\Directory", L"C:"}, {L"D:\\Some Other Directory", L"D:"}},
            {{L"C:", L""}, {L"D:", L""}},
            {{L"\\sharepath\\shared folder$\\another shared folder",
              L"\\sharepath\\shared folder$"},
             {L"D:\\Long\\Sub Directory \\   Path To Directory\\Yes",
              L"D:\\Long\\Sub Directory \\   Path To Directory"}}};

    for (const auto& kDirectoryTestRecord : kDirectoryTestRecords)
    {
      const std::wstring_view expectedOriginDirectoryParent = kDirectoryTestRecord.first.second;
      const std::wstring_view expectedTargetDirectoryParent = kDirectoryTestRecord.second.second;

      const FilesystemRule filesystemRule(
          L"", kDirectoryTestRecord.first.first, kDirectoryTestRecord.second.first);

      const std::wstring_view actualOriginDirectoryParent =
          filesystemRule.GetOriginDirectoryParent();
      TEST_ASSERT(actualOriginDirectoryParent == expectedOriginDirectoryParent);

      const std::wstring_view actualTargetDirectoryParent =
          filesystemRule.GetTargetDirectoryParent();
      TEST_ASSERT(actualTargetDirectoryParent == expectedTargetDirectoryParent);
    }
  }

  // Verifies that paths are successfully redirected in the nominal case of straightforward
  // absolute paths. In this test both forward and backward redirection is exercised.
  TEST_CASE(FilesystemRule_RedirectPath_Nominal)
  {
    constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
    constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

    constexpr std::wstring_view kTestFiles[] = {L"File1", L".file2", L"FILE3.BIN"};

    const FilesystemRule filesystemRule(L"", kOriginDirectory, kTargetDirectory);

    for (const auto& kTestFile : kTestFiles)
    {
      TemporaryString expectedOutputPath = kTargetDirectory;
      expectedOutputPath << L'\\' << kTestFile;

      std::optional<TemporaryString> actualOutputPath =
          filesystemRule.RedirectPathOriginToTarget(kOriginDirectory, kTestFile.data());
      TEST_ASSERT(true == actualOutputPath.has_value());
      TEST_ASSERT(actualOutputPath.value() == expectedOutputPath);
    }

    for (const auto& kTestFile : kTestFiles)
    {
      TemporaryString expectedOutputPath = kOriginDirectory;
      expectedOutputPath << L'\\' << kTestFile;

      std::optional<TemporaryString> actualOutputPath =
          filesystemRule.RedirectPathTargetToOrigin(kTargetDirectory, kTestFile.data());
      TEST_ASSERT(true == actualOutputPath.has_value());
      TEST_ASSERT(actualOutputPath.value() == expectedOutputPath);
    }
  }

  // Verifies that paths are successfully redirected in the nominal case of straightforward
  // absolute paths, but this time the request asks that a Windows namespace prefix be prepended
  // to the output. In this test both forward and backward redirection is exercised.
  TEST_CASE(FilesystemRule_RedirectPath_PrependNamespacePrefix)
  {
    constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
    constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";
    constexpr std::wstring_view kNamespacePrefix = L"\\??\\";

    constexpr std::wstring_view kTestFiles[] = {L"File1", L".file2", L"FILE3.BIN"};

    const FilesystemRule filesystemRule(L"", kOriginDirectory, kTargetDirectory);

    for (const auto& kTestFile : kTestFiles)
    {
      TemporaryString expectedOutputPath;
      expectedOutputPath << kNamespacePrefix << kTargetDirectory << L'\\' << kTestFile;

      std::optional<TemporaryString> actualOutputPath = filesystemRule.RedirectPathOriginToTarget(
          kOriginDirectory, kTestFile.data(), kNamespacePrefix);
      TEST_ASSERT(true == actualOutputPath.has_value());
      TEST_ASSERT(actualOutputPath.value() == expectedOutputPath);
    }

    for (const auto& kTestFile : kTestFiles)
    {
      TemporaryString expectedOutputPath;
      expectedOutputPath << kNamespacePrefix << kOriginDirectory << L'\\' << kTestFile;

      std::optional<TemporaryString> actualOutputPath = filesystemRule.RedirectPathTargetToOrigin(
          kTargetDirectory, kTestFile.data(), kNamespacePrefix);
      TEST_ASSERT(true == actualOutputPath.has_value());
      TEST_ASSERT(actualOutputPath.value() == expectedOutputPath);
    }
  }

  // Verifies that paths are successfully redirected when the input path is a descendent of the
  // origin directory. Only the matching prefix part should be replaced with the target directory.
  TEST_CASE(FilesystemRule_RedirectPath_WithSubdirectoryHierarchy)
  {
    constexpr std::wstring_view kOriginDirectory = L"C:\\Origin";
    constexpr std::wstring_view kTargetDirectory = L"D:\\Target";

    constexpr std::wstring_view kInputPathDirectory = L"C:\\Origin\\Subdir2";
    constexpr std::wstring_view kInputPathFile = L"file2.txt";
    constexpr std::wstring_view kExpectedOutputPath = L"D:\\Target\\Subdir2\\file2.txt";

    const FilesystemRule filesystemRule(L"", kOriginDirectory, kTargetDirectory);

    std::optional<TemporaryString> actualOutputPath =
        filesystemRule.RedirectPathOriginToTarget(kInputPathDirectory, kInputPathFile);
    TEST_ASSERT(true == actualOutputPath.has_value());
    TEST_ASSERT(actualOutputPath.value() == kExpectedOutputPath);
  }

  // Verifies that paths are successfully redirected when the file part matches a pattern and left
  // alone when the file part does not match a pattern.
  TEST_CASE(FilesystemRule_RedirectPath_FilePattern)
  {
    constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
    constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";
    std::vector<std::wstring> filePatterns = {L"A*F*", L"?gh.jkl"};

    constexpr std::wstring_view kTestFilesMatching[] = {
        L"ASDF", L"ASDFGHJKL", L"_gh.jkl", L"ggh.jkl"};

    constexpr std::wstring_view kTestFilesNotMatching[] = {
        L"    ASDF", L"gh.jkl", L"A", L"test.file"};

    const FilesystemRule filesystemRule(
        L"", kOriginDirectory, kTargetDirectory, std::move(filePatterns));

    for (const auto& kTestFile : kTestFilesMatching)
    {
      TemporaryString expectedOutputPath = kTargetDirectory;
      expectedOutputPath << L'\\' << kTestFile;

      std::optional<TemporaryString> actualOutputPath =
          filesystemRule.RedirectPathOriginToTarget(kOriginDirectory, kTestFile.data());
      TEST_ASSERT(true == actualOutputPath.has_value());
      TEST_ASSERT(actualOutputPath.value() == expectedOutputPath);
    }

    for (const auto& kTestFile : kTestFilesNotMatching)
    {
      TEST_ASSERT(
          false ==
          filesystemRule.RedirectPathOriginToTarget(kOriginDirectory, kTestFile.data())
              .has_value());
    }
  }

  // Verifies that paths are successfully redirected using prefix matching when the actual file
  // being directed is deep in a directory hierarchy. No file patterns are used.
  TEST_CASE(FilesystemRule_RedirectPath_DeepDirectoryHierarchyNoFilePattern)
  {
    constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
    constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

    constexpr std::wstring_view kInputDirectory = L"C:\\Directory\\Origin\\Subdir1\\Subdir2";
    constexpr std::wstring_view kInputFile = L"file.txt";

    constexpr std::wstring_view kExpectedOutputPath =
        L"D:\\AnotherDirectory\\Target\\Subdir1\\Subdir2\\file.txt";

    const FilesystemRule filesystemRule(L"", kOriginDirectory, kTargetDirectory);
    std::optional<TemporaryString> actualOutputPath =
        filesystemRule.RedirectPathOriginToTarget(kInputDirectory, kInputFile);
    TEST_ASSERT(actualOutputPath.has_value());
    TEST_ASSERT(actualOutputPath.value() == kExpectedOutputPath);
  }

  // Verifies that paths are not redirected even though there is a directory hierarchy match
  // because of a file pattern mismatch. Here, the redirection should fail because "Subdir1" does
  // not match the file pattern of the rule even though the file part, "file.txt," does.
  TEST_CASE(FilesystemRule_RedirectPath_DeepDirectoryHierarchyNonMatchingFilePattern)
  {
    constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
    constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";
    std::vector<std::wstring> filePatterns = {L"f*"};

    constexpr std::wstring_view kInputDirectory = L"C:\\Directory\\Origin\\Subdir1\\Subdir2";
    constexpr std::wstring_view kInputFile = L"file.txt";

    const FilesystemRule filesystemRule(
        L"", kOriginDirectory, kTargetDirectory, std::move(filePatterns));
    std::optional<TemporaryString> actualOutputPath =
        filesystemRule.RedirectPathOriginToTarget(kInputDirectory, kInputFile);
    TEST_ASSERT(false == actualOutputPath.has_value());
  }

  // Verifies that directories that are equal to a directory associated with a filesystem rule are
  // correctly identified and that routing to either origin or target directories is correct. This
  // test compares with both origin and target directories.
  TEST_CASE(FilesystemRule_DirectoryCompare_Equal)
  {
    constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
    constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

    const FilesystemRule filesystemRule(L"", kOriginDirectory, kTargetDirectory);

    TEST_ASSERT(
        EDirectoryCompareResult::Equal ==
        filesystemRule.DirectoryCompareWithOrigin(kOriginDirectory));
    TEST_ASSERT(
        EDirectoryCompareResult::Unrelated ==
        filesystemRule.DirectoryCompareWithTarget(kOriginDirectory));

    TEST_ASSERT(
        EDirectoryCompareResult::Unrelated ==
        filesystemRule.DirectoryCompareWithOrigin(kTargetDirectory));
    TEST_ASSERT(
        EDirectoryCompareResult::Equal ==
        filesystemRule.DirectoryCompareWithTarget(kTargetDirectory));
  }

  // Verifies that directories are compared without regard for case.
  TEST_CASE(FilesystemRule_DirectoryCompare_EqualCaseInsensitive)
  {
    constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
    constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

    constexpr std::wstring_view kOriginCompareDirectory = L"c:\\direCTory\\oriGin";
    constexpr std::wstring_view kTargetCompareDirectory = L"d:\\aNOTHeRdireCTORy\\tARgeT";

    const FilesystemRule filesystemRule(L"", kOriginDirectory, kTargetDirectory);

    TEST_ASSERT(
        EDirectoryCompareResult::Equal ==
        filesystemRule.DirectoryCompareWithOrigin(kOriginCompareDirectory));
    TEST_ASSERT(
        EDirectoryCompareResult::Equal ==
        filesystemRule.DirectoryCompareWithTarget(kTargetCompareDirectory));
  }

  // Verifies that directories that are children or descendants of a directory associated with a
  // filesystem rule are correctly identified as such. This test compares with the origin
  // directory.
  TEST_CASE(FilesystemRule_DirectoryCompare_CandidateIsChildOrDescendant)
  {
    constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
    constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

    constexpr std::pair<std::wstring_view, EDirectoryCompareResult> kDirectoryTestRecords[] = {
        {L"C:\\Directory\\Origin\\Subdir", EDirectoryCompareResult::CandidateIsChild},
        {L"C:\\Directory\\Origin\\Sub Directory 2", EDirectoryCompareResult::CandidateIsChild},
        {L"C:\\Directory\\Origin\\Sub Directory 2\\Subdir3\\Subdir4",
         EDirectoryCompareResult::CandidateIsDescendant},
        {L"c:\\diRECTory\\oRIGin\\sUBDir", EDirectoryCompareResult::CandidateIsChild},
        {L"c:\\diRECTory\\oRIGin\\sub dIRECTory 2", EDirectoryCompareResult::CandidateIsChild},
        {L"c:\\diRECTory\\oRIGin\\sub dIRECTory 2\\suBDir3\\suBDir4",
         EDirectoryCompareResult::CandidateIsDescendant}};

    const FilesystemRule filesystemRule(L"", kOriginDirectory, kTargetDirectory);

    for (const auto& kDirectoryTestRecord : kDirectoryTestRecords)
      TEST_ASSERT(
          kDirectoryTestRecord.second ==
          filesystemRule.DirectoryCompareWithOrigin(kDirectoryTestRecord.first));
  }

  // Verifies that directories that are parents or ancestors of a directory associated with a
  // filesystem rule are correctly identified as such. This test compares with the target
  // directory.
  TEST_CASE(FilesystemRule_DirectoryCompare_CandidateIsParentOrAncestor)
  {
    constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
    constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

    constexpr std::pair<std::wstring_view, EDirectoryCompareResult> kDirectoryTestRecords[] = {
        {L"D:", EDirectoryCompareResult::CandidateIsAncestor},
        {L"D:\\AnotherDirectory", EDirectoryCompareResult::CandidateIsParent},
        {L"d:", EDirectoryCompareResult::CandidateIsAncestor},
        {L"d:\\aNOTHeRdiRECTorY", EDirectoryCompareResult::CandidateIsParent}};

    const FilesystemRule filesystemRule(L"", kOriginDirectory, kTargetDirectory);

    for (const auto& kDirectoryTestRecord : kDirectoryTestRecords)
      TEST_ASSERT(
          kDirectoryTestRecord.second ==
          filesystemRule.DirectoryCompareWithTarget(kDirectoryTestRecord.first));
  }

  // Verifies that directories that are unrelated to a directory associated with a filesystem rule
  // are correctly identified as such. This test compares with both origin and target directories.
  TEST_CASE(FilesystemRule_DirectoryCompare_Unrelated)
  {
    constexpr std::wstring_view kOriginDirectory = L"C:\\Directory\\Origin";
    constexpr std::wstring_view kTargetDirectory = L"D:\\AnotherDirectory\\Target";

    constexpr std::wstring_view kDirectories[] = {
        L"",
        L"C:\\Dir",
        L"C:\\Directory\\Origin2"
        L"C:\\Directory\\Orig",
        L"D:\\Another",
        L"D:\\AnotherDirectory\\Target234"};

    const FilesystemRule filesystemRule(L"", kOriginDirectory, kTargetDirectory);

    for (const auto& kDirectory : kDirectories)
    {
      TEST_ASSERT(
          EDirectoryCompareResult::Unrelated ==
          filesystemRule.DirectoryCompareWithOrigin(kDirectory));
      TEST_ASSERT(
          EDirectoryCompareResult::Unrelated ==
          filesystemRule.DirectoryCompareWithTarget(kDirectory));
    }
  }

  // Verifies that a filesystem rule container correctly identifies rules that match file patterns.
  // In this case all file patterns are totally disjoint.
  TEST_CASE(RelatedFilesystemRuleContainer_IdentifyRuleMatchingFilename)
  {
    const FilesystemRule rules[] = {
        FilesystemRule(
            L"TXT", std::wstring_view(), std::wstring_view(), std::vector<std::wstring>{L"*.txt"}),
        FilesystemRule(
            L"BIN", std::wstring_view(), std::wstring_view(), std::vector<std::wstring>{L"*.bin"}),
        FilesystemRule(
            L"LOG", std::wstring_view(), std::wstring_view(), std::vector<std::wstring>{L"*.log"}),
        FilesystemRule(
            L"EXE", std::wstring_view(), std::wstring_view(), std::vector<std::wstring>{L"*.exe"}),
    };

    RelatedFilesystemRuleContainer ruleContainer;
    for (const auto& rule : rules)
      TEST_ASSERT(true == ruleContainer.InsertRule(rule).second);

    constexpr struct
    {
      std::wstring_view inputFileName;
      std::wstring_view expectedRuleName;
    } kTestRecords[] = {
        {.inputFileName = L"file1.TXT", .expectedRuleName = L"TXT"},
        {.inputFileName = L"File2.txt", .expectedRuleName = L"TXT"},
        {.inputFileName = L"log file.Log", .expectedRuleName = L"LOG"},
        {.inputFileName = L"app.exe", .expectedRuleName = L"EXE"},
        {.inputFileName = L"binfile_1234.bin", .expectedRuleName = L"BIN"},
        {.inputFileName = L"document.docx", .expectedRuleName = L""}};

    for (const auto& testRecord : kTestRecords)
    {
      if (true == testRecord.expectedRuleName.empty())
      {
        TEST_ASSERT(false == ruleContainer.HasRuleMatchingFileName(testRecord.inputFileName));
        TEST_ASSERT(nullptr == ruleContainer.RuleMatchingFileName(testRecord.inputFileName));
      }
      else
      {
        TEST_ASSERT(true == ruleContainer.HasRuleMatchingFileName(testRecord.inputFileName));
        TEST_ASSERT(nullptr != ruleContainer.RuleMatchingFileName(testRecord.inputFileName));
        TEST_ASSERT(
            ruleContainer.RuleMatchingFileName(testRecord.inputFileName)->GetName() ==
            testRecord.expectedRuleName);
      }
    }
  }

  // Verifies that a filesystem rule container correctly orders filesystem rules based on the
  // documented ordering mechanism of descending by number of file patterns and then ascending by
  // rule name.
  TEST_CASE(RelatedFilesystemRuleContainer_RuleOrder)
  {
    const FilesystemRule rules[] = {
        // These rules all have three file patterns.
        FilesystemRule(
            L"C3",
            std::wstring_view(),
            std::wstring_view(),
            std::vector<std::wstring>{L"1", L"2", L"3"}),
        FilesystemRule(
            L"D3",
            std::wstring_view(),
            std::wstring_view(),
            std::vector<std::wstring>{L"4", L"5", L"6"}),
        FilesystemRule(
            L"B3",
            std::wstring_view(),
            std::wstring_view(),
            std::vector<std::wstring>{L"7", L"8", L"9"}),

        // These rules all have two file patterns.
        FilesystemRule(
            L"B2", std::wstring_view(), std::wstring_view(), std::vector<std::wstring>{L"a", L"b"}),
        FilesystemRule(
            L"D2", std::wstring_view(), std::wstring_view(), std::vector<std::wstring>{L"c", L"d"}),
        FilesystemRule(
            L"C2", std::wstring_view(), std::wstring_view(), std::vector<std::wstring>{L"e", L"f"}),

        // These rules all have one file pattern.
        FilesystemRule(
            L"D1", std::wstring_view(), std::wstring_view(), std::vector<std::wstring>{L"g"}),
        FilesystemRule(
            L"C1", std::wstring_view(), std::wstring_view(), std::vector<std::wstring>{L"h"}),
        FilesystemRule(
            L"B1", std::wstring_view(), std::wstring_view(), std::vector<std::wstring>{L"i"}),

        // This rule has no file patterns.
        FilesystemRule(L"A", std::wstring_view(), std::wstring_view())};

    RelatedFilesystemRuleContainer ruleContainer;
    for (const auto& rule : rules)
      TEST_ASSERT(true == ruleContainer.InsertRule(rule).second);

    // Filesystem rules are expected to be ordered first by number of file patterns in descending
    // order and second by rule name. So more file patterns means earlier in the order.
    const std::vector<std::wstring_view> expectedRuleOrder = {
        L"B3", L"C3", L"D3", L"B2", L"C2", L"D2", L"B1", L"C1", L"D1", L"A"};
    std::vector<std::wstring_view> actualRuleOrder;
    for (const auto& filesystemRule : ruleContainer.AllRules())
      actualRuleOrder.push_back(filesystemRule.GetName());
    TEST_ASSERT(actualRuleOrder == expectedRuleOrder);
  }
} // namespace PathwinderTest
