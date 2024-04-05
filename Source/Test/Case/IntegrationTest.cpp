/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file IntegrationTest.cpp
 *   Multi-subsystem combined integration tests for exercising end-to-end redirection situations
 *   not easily captured by other tests.
 **************************************************************************************************/

#include "TestCase.h"

#include <set>
#include <string_view>

#include "FileInformationStruct.h"
#include "FilesystemDirector.h"
#include "FilesystemDirectorBuilder.h"
#include "FilesystemExecutor.h"
#include "FilesystemInstruction.h"
#include "FilesystemRule.h"
#include "IntegrationTestSupport.h"
#include "MockFilesystemOperations.h"
#include "Strings.h"

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  // Verifies correct functionality of the "EntireDirectoryReplacement" example provided on the
  // Mechanics of Filesystem Rules documentation page. This uses a single simple filesystem rule
  // and no file patterns. The starting condition is that C:\DataDir does not exist.
  TEST_CASE(
      IntegrationTest_MechanicsOfFilesystemRulesExample_EntireDirectoryReplacement_DataDirDoesNotExist)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:EntireDirectoryReplacement]\n"
        L"OriginDirectory = C:\\AppDir\\DataDir\n"
        L"TargetDirectory = C:\\TargetDir\n"
        L"RedirectMode = Simple";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"TextFile.txt", L"Output.log"});
    mockFilesystem.AddDirectory(L"C:\\TargetDir\\TargetSub");

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    VerifyDirectoryAppearsToContain(
        context, L"C:\\AppDir\\DataDir", {L"TextFile.txt", L"Output.log", L"TargetSub"});
  }

  // Verifies correct functionality of the "EntireDirectoryReplacement" example provided on the
  // Mechanics of Filesystem Rules documentation page. This uses a single simple filesystem rule
  // and no file patterns. The starting condition is that C:\DataDir exists but is empty.
  TEST_CASE(
      IntegrationTest_MechanicsOfFilesystemRulesExample_EntireDirectoryReplacement_DataDirIsEmpty)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:EntireDirectoryReplacement]\n"
        L"OriginDirectory = C:\\AppDir\\DataDir\n"
        L"TargetDirectory = C:\\TargetDir\n"
        L"RedirectMode = Simple";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddDirectory(L"C:\\AppData\\DataDir");
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"TextFile.txt", L"Output.log"});
    mockFilesystem.AddDirectory(L"C:\\TargetDir\\TargetSub");

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    VerifyDirectoryAppearsToContain(
        context, L"C:\\AppDir\\DataDir", {L"TextFile.txt", L"Output.log", L"TargetSub"});
  }

  // Verifies correct functionality of the "EntireDirectoryReplacement" example provided on the
  // Mechanics of Filesystem Rules documentation page. This uses a single simple filesystem rule
  // and no file patterns. The starting condition is that C:\DataDir exists and contains files and
  // subdirectories.
  TEST_CASE(
      IntegrationTest_MechanicsOfFilesystemRulesExample_EntireDirectoryReplacement_DataDirIsNotEmpty)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:EntireDirectoryReplacement]\n"
        L"OriginDirectory = C:\\AppDir\\DataDir\n"
        L"TargetDirectory = C:\\TargetDir\n"
        L"RedirectMode = Simple";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir", {L"DataFile1.dat", L"DataFile2.dat"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir\\DataSubdir", {L"DataSubFile.dat.dat"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"TextFile.txt", L"Output.log"});
    mockFilesystem.AddDirectory(L"C:\\TargetDir\\TargetSub");

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    VerifyDirectoryAppearsToContain(
        context, L"C:\\AppDir\\DataDir", {L"TextFile.txt", L"Output.log", L"TargetSub"});
  }

  TEST_CASE(
      IntegrationTest_MechanicsOfFilesystemRulesExample_PartialDirectoryReplacement_WithoutSubdirectories)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:PartialDirectoryReplacement]\n"
        L"OriginDirectory = C:\\AppDir\\DataDir\n"
        L"TargetDirectory = C:\\TargetDir\n"
        L"FilePattern = *.txt\n"
        L"RedirectMode = Simple";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir", {L"1stOrigin.txt", L"2ndOrigin.bin"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"3rdTarget.txt", L"4thTarget.log"});

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    // First part from the documented example is just the results of applying the rule. The *.txt
    // file originally present in the origin directory is hidden, and the *.txt file in the target
    // directory is visible.
    VerifyDirectoryAppearsToContain(
        context, L"C:\\AppDir\\DataDir", {L"2ndOrigin.bin", L"3rdTarget.txt"});

    // Second part of the documented example is to create an out-of-scope file. It should be added
    // to, and visible in, the origin directory as a real file and not present in the target
    // directory.
    CreateNewFileUsingFilesystemExecutor(context, L"C:\\AppDir\\DataDir\\Data.dat");

    VerifyDirectoryAppearsToContain(
        context, L"C:\\AppDir\\DataDir", {L"2ndOrigin.bin", L"3rdTarget.txt", L"Data.dat"});

    TEST_ASSERT(true == mockFilesystem.Exists(L"C:\\AppDir\\DataDir\\Data.dat"));
    TEST_ASSERT(false == mockFilesystem.Exists(L"C:\\TargetDir\\Data.dat"));

    // Third part of the documented example is to create an in-scope file. It should be added to the
    // target directory and visible in the origin directory.
    CreateNewFileUsingFilesystemExecutor(context, L"C:\\AppDir\\DataDir\\Output.txt");

    VerifyDirectoryAppearsToContain(
        context,
        L"C:\\AppDir\\DataDir",
        {L"2ndOrigin.bin", L"3rdTarget.txt", L"Data.dat", L"Output.txt"});

    TEST_ASSERT(false == mockFilesystem.Exists(L"C:\\AppDir\\DataDir\\Output.txt"));
    TEST_ASSERT(true == mockFilesystem.Exists(L"C:\\TargetDir\\Output.txt"));
  }

  TEST_CASE(
      IntegrationTest_MechanicsOfFilesystemRulesExample_PartialDirectoryReplacement_WithSubdirectories)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:PartialDirectoryReplacement]\n"
        L"OriginDirectory = C:\\AppDir\\DataDir\n"
        L"TargetDirectory = C:\\TargetDir\n"
        L"FilePattern = *.txt\n"
        L"RedirectMode = Simple";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir", {L"1stOrigin.txt", L"2ndOrigin.bin"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\OriginSubA", {L"OutputA.txt"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\OriginSubB.txt", {L"OutputB.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"3rdTarget.txt", L"4thTarget.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir\\TargetSubA", {L"ContentsA.txt"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\TargetDir\\TargetSubB.txt", {L"ContentsB.txt", L"ContentsB2.bin"});

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    VerifyDirectoryAppearsToContain(
        context,
        L"C:\\AppDir\\DataDir",
        {L"2ndOrigin.bin", L"3rdTarget.txt", L"OriginSubA", L"TargetSubB.txt"});

    VerifyDirectoryAppearsToContain(context, L"C:\\AppDir\\DataDir\\OriginSubA", {L"OutputA.txt"});

    VerifyDirectoryAppearsToContain(
        context, L"C:\\AppDir\\DataDir\\TargetSubB.txt", {L"ContentsB.txt", L"ContentsB2.bin"});
  }

  // Verifies correct functionality of the "OverlayWithoutFilePatterns" example provided on the
  // Mechanics of Filesystem Rules documentation page. This uses a single overlay filesystem rule
  // and no file patterns.
  TEST_CASE(IntegrationTest_MechanicsOfFilesystemRulesExample_OverlayWithoutFilePatterns)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:OverlayWithoutFilePatterns]\n"
        L"OriginDirectory = C:\\AppDir\\DataDir\n"
        L"TargetDirectory = C:\\TargetDir\n"
        L"RedirectMode = Overlay";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir", {L"1stOrigin.txt", L"2ndOrigin.bin"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\OriginSub", {L"OutputA.txt"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\MoreData.txt", {L"OutputB.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"3rdTarget.txt", L"4thTarget.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir\\TargetSub", {L"ContentsA.txt"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\TargetDir\\MoreData.txt", {L"OutputB.log", L"ContentsB2.bin"});

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    VerifyDirectoryAppearsToContain(
        context,
        L"C:\\AppDir\\DataDir",
        {L"1stOrigin.txt",
         L"2ndOrigin.bin",
         L"3rdTarget.txt",
         L"4thTarget.log",
         L"OriginSub",
         L"TargetSub",
         L"MoreData.txt"});
    VerifyDirectoryAppearsToContain(context, L"C:\\AppDir\\DataDir\\OriginSub", {L"OutputA.txt"});
    VerifyDirectoryAppearsToContain(context, L"C:\\AppDir\\DataDir\\TargetSub", {L"ContentsA.txt"});
    VerifyDirectoryAppearsToContain(
        context, L"C:\\AppDir\\DataDir\\MoreData.txt", {L"OutputB.log", L"ContentsB2.bin"});
  }

  // Verifies correct functionality of the "OverlayWithFilePatterns" example provided on the
  // Mechanics of Filesystem Rules documentation page. This uses a single overlay filesystem rule
  // with a file pattern.
  TEST_CASE(IntegrationTest_MechanicsOfFilesystemRulesExample_OverlayWithFilePatterns)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:OverlayWithoutFilePatterns]\n"
        L"OriginDirectory = C:\\AppDir\\DataDir\n"
        L"TargetDirectory = C:\\TargetDir\n"
        L"FilePattern = *.txt\n"
        L"RedirectMode = Overlay";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir", {L"1stOrigin.txt", L"2ndOrigin.bin"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\OriginSub", {L"OutputA.txt"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\MoreData.txt", {L"OutputB.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"3rdTarget.txt", L"4thTarget.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir\\TargetSub", {L"ContentsA.txt"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\TargetDir\\MoreData.txt", {L"OutputB.log", L"ContentsB2.bin"});

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    VerifyDirectoryAppearsToContain(
        context,
        L"C:\\AppDir\\DataDir",
        {L"1stOrigin.txt", L"2ndOrigin.bin", L"3rdTarget.txt", L"OriginSub", L"MoreData.txt"});
    VerifyDirectoryAppearsToContain(context, L"C:\\AppDir\\DataDir\\OriginSub", {L"OutputA.txt"});
    VerifyDirectoryAppearsToContain(
        context, L"C:\\AppDir\\DataDir\\MoreData.txt", {L"OutputB.log", L"ContentsB2.bin"});
  }

  // Checks for consistency between directory enumeration and direct file access when multiple rules
  // exist all with the same origin directory but different file patterns and redirection modes. In
  // this case, one rule is a wildcard Simple redirection mode rule, but the others are Overlay
  // rules with file patterns.
  TEST_CASE(
      IntegrationTest_FilesystemConsistencyCheck_MultipleRulesSameOriginDirectory_SimpleWildcardOverlayFilePatterns)
  {
    // This configuration file defines four rules all having the same origin directory. Three rules
    // use Overlay mode and each cover their own individual types of files, and one uses in Simple
    // mode as a catch-all for all other file types (it does not use any file patterns.
    //
    // Rules Test1 to Test3 all use Overlay mode and each have a different file type covered by
    // their respective file patterns.
    //
    // Rule Test4 uses Simple mode and covers all other files, regardless of type.
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:Test1]\n"
        L"OriginDirectory = C:\\Origin\n"
        L"TargetDirectory = C:\\Target\\1\n"
        L"RedirectMode = Overlay\n"
        L"FilePattern = *.rtf\n"
        L"\n"
        L"[FilesystemRule:Test2]\n"
        L"OriginDirectory = C:\\Origin\n"
        L"TargetDirectory = C:\\Target\\2\n"
        L"RedirectMode = Overlay\n"
        L"FilePattern = *.odt\n"
        L"\n"
        L"[FilesystemRule:Test3]\n"
        L"OriginDirectory = C:\\Origin\n"
        L"TargetDirectory = C:\\Target\\3\n"
        L"RedirectMode = Overlay\n"
        L"FilePattern = *.txt\n"
        L"\n"
        L"[FilesystemRule:Test4]\n"
        L"OriginDirectory = C:\\Origin\n"
        L"TargetDirectory = C:\\Target\\4\n"
        L"RedirectMode = Simple\n";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFilesInDirectory(
        L"C:\\Target\\1", {L"1_A.rtf", L"1_B.rtf", L"1_C.rtf", L"1_D.txt", L"1_E.odt"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\Target\\2", {L"2_A.odt", L"2_B.odt", L"2_C.odt", L"2_D.rtf", L"2_E.txt"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\Target\\3", {L"3_A.txt", L"3_B.txt", L"3_C.txt", L"3_D.rtf", L"3_E.odt"});
    mockFilesystem.AddFilesInDirectory(L"C:\\Target\\4", {L"4_A.exe", L"4_B.bin", L"4_C.log"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\Origin",
        {L"OriginSide.docx",
         L"OriginSide.rtf",
         L"OriginSide.txt",
         L"OriginSide.odt",
         L"OriginSide.exe"});

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    // Expected behavior when accessing C:\Origin is that these files should be accessible both by
    // enumeration and by direct request:
    //  - All *.rtf files in C:\Target\1 and in C:\Origin
    //  - All *.odt files in C:\Target\2 and in C:\Origin
    //  - All *.txt files in C:\Target\3 and in C:\Origin
    //  - All files of other types in C:\Target\4

    VerifyDirectoryAppearsToContain(
        context,
        L"C:\\Origin",
        {L"1_A.rtf",
         L"1_B.rtf",
         L"1_C.rtf",
         L"OriginSide.rtf",
         L"2_A.odt",
         L"2_B.odt",
         L"2_C.odt",
         L"OriginSide.odt",
         L"3_A.txt",
         L"3_B.txt",
         L"3_C.txt",
         L"OriginSide.txt",
         L"4_A.exe",
         L"4_B.bin",
         L"4_C.log"});
  }
} // namespace PathwinderTest
