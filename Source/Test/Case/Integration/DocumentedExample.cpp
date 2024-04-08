/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file DocumentedExample.cpp
 *   Integration tests based on examples presented in project documentation.
 **************************************************************************************************/

#include "TestCase.h"

#include <map>
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
      DocumentedExample_MechanicsOfFilesystemRules_EntireDirectoryReplacement_DataDirDoesNotExist)
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
  TEST_CASE(DocumentedExample_MechanicsOfFilesystemRules_EntireDirectoryReplacement_DataDirIsEmpty)
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
      DocumentedExample_MechanicsOfFilesystemRules_EntireDirectoryReplacement_DataDirIsNotEmpty)
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
      DocumentedExample_MechanicsOfFilesystemRules_PartialDirectoryReplacement_WithoutSubdirectories)
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
    CreateFileUsingFilesystemExecutor(context, L"C:\\AppDir\\DataDir\\Data.dat");

    VerifyDirectoryAppearsToContain(
        context, L"C:\\AppDir\\DataDir", {L"2ndOrigin.bin", L"3rdTarget.txt", L"Data.dat"});

    TEST_ASSERT(true == mockFilesystem.Exists(L"C:\\AppDir\\DataDir\\Data.dat"));
    TEST_ASSERT(false == mockFilesystem.Exists(L"C:\\TargetDir\\Data.dat"));

    // Third part of the documented example is to create an in-scope file. It should be added to the
    // target directory and visible in the origin directory.
    CreateFileUsingFilesystemExecutor(context, L"C:\\AppDir\\DataDir\\Output.txt");

    VerifyDirectoryAppearsToContain(
        context,
        L"C:\\AppDir\\DataDir",
        {L"2ndOrigin.bin", L"3rdTarget.txt", L"Data.dat", L"Output.txt"});

    TEST_ASSERT(false == mockFilesystem.Exists(L"C:\\AppDir\\DataDir\\Output.txt"));
    TEST_ASSERT(true == mockFilesystem.Exists(L"C:\\TargetDir\\Output.txt"));
  }

  TEST_CASE(
      DocumentedExample_MechanicsOfFilesystemRules_PartialDirectoryReplacement_WithSubdirectories)
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
  TEST_CASE(DocumentedExample_MechanicsOfFilesystemRules_OverlayWithoutFilePatterns)
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
  TEST_CASE(DocumentedExample_MechanicsOfFilesystemRules_OverlayWithFilePatterns)
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

  // Verifies correct functionality of the "UnrelatedOriginDirectories" example provided on the
  // Mechanics of Filesystem Rules documentation page. This uses two rules with unrelated origin
  // directories.
  TEST_CASE(DocumentedExample_MechanicsOfFilesystemRules_UnrelatedOriginDirectories)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:UnrelatedOriginDirectories1]\n"
        L"OriginDirectory = C:\\OriginSide\\Origin1\n"
        L"TargetDirectory = C:\\TargetSide\\Target1\n"
        L"\n"
        L"[FilesystemRule:UnrelatedOriginDirectories2]\n"
        L"OriginDirectory = C:\\OriginSide\\Origin2\n"
        L"TargetDirectory = C:\\TargetSide\\Target2\n"
        L"FilePattern = *.txt";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\OriginSide\\Origin1");
    mockFilesystem.AddDirectory(L"C:\\OriginSide\\Origin2");
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetSide\\Target1", {L"File1_1.bin"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\TargetSide\\Target2", {L"File2_1.bin", L"File2_2.txt"});

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    VerifyDirectoryAppearsToContain(context, L"C:\\OriginSide\\Origin1", {L"File1_1.bin"});
    VerifyDirectoryAppearsToContain(context, L"C:\\OriginSide\\Origin2", {L"File2_2.txt"});
  }

  // Verifies correct functionality of the "RelatedOriginDirectories" example provided on the
  // Mechanics of Filesystem Rules documentation page when no file patterns are used. This uses two
  // rules with related origin directories and verifies that the rule with the deeper origin
  // directory takes precedence.
  TEST_CASE(
      DocumentedExample_MechanicsOfFilesystemRules_RelatedOriginDirectories_WithoutFilePatterns)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:RelatedOriginDirectories1]\n"
        L"OriginDirectory = C:\\OriginSide\\Level1\n"
        L"TargetDirectory = C:\\TargetSide\\Dir1\n"
        L"\n"
        L"[FilesystemRule:RelatedOriginDirectories2]\n"
        L"OriginDirectory = C:\\OriginSide\\Level1\\Level2\n"
        L"TargetDirectory = C:\\TargetSide\\Dir2";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\OriginSide");

    constexpr std::wstring_view kFilePathToAccess = L"C:\\OriginSide\\Level1\\Level2\\TextFile.txt";

    // These three files respectively represent no redirection, redirection using rule 1, and
    // redirection using rule 2.
    mockFilesystem.AddFile(kFilePathToAccess);
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetSide\\Dir1\\Level2", {L"TextFile.txt"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetSide\\Dir2", {L"TextFile.txt"});

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    HANDLE accessedFileHandle = OpenUsingFilesystemExecutor(context, kFilePathToAccess);
    TEST_ASSERT(
        L"C:\\TargetSide\\Dir2\\TextFile.txt" ==
        mockFilesystem.GetPathFromHandle(accessedFileHandle));
  }

  // Verifies correct functionality of the "RelatedOriginDirectories" example provided on the
  // Mechanics of Filesystem Rules documentation page when a file pattern are used. This uses two
  // rules with related origin directories and verifies that the rule with the deeper origin
  // directory takes precedence but, because of a file pattern mismatch, leads to no redirection.
  TEST_CASE(DocumentedExample_MechanicsOfFilesystemRules_RelatedOriginDirectories_WithFilePatterns)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:RelatedOriginDirectories1]\n"
        L"OriginDirectory = C:\\OriginSide\\Level1\n"
        L"TargetDirectory = C:\\TargetSide\\Dir1\n"
        L"\n"
        L"[FilesystemRule:RelatedOriginDirectories2]\n"
        L"OriginDirectory = C:\\OriginSide\\Level1\\Level2\n"
        L"TargetDirectory = C:\\TargetSide\\Dir2\n"
        L"FilePattern = *.bin";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\OriginSide");

    constexpr std::wstring_view kFilePathToAccess = L"C:\\OriginSide\\Level1\\Level2\\TextFile.txt";

    // These three files respectively represent no redirection, redirection using rule 1, and
    // redirection using rule 2.
    mockFilesystem.AddFile(kFilePathToAccess);
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetSide\\Dir1\\Level2", {L"TextFile.txt"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetSide\\Dir2", {L"TextFile.txt"});

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    HANDLE accessedFileHandle = OpenUsingFilesystemExecutor(context, kFilePathToAccess);
    TEST_ASSERT(kFilePathToAccess == mockFilesystem.GetPathFromHandle(accessedFileHandle));
  }

  // Verifies correct functionality of the rules used in the "Same Origin Directories" example
  // provided on the Mechanics of Filesystem Rules documentation page. Four rules with the same
  // origin directories are created. The test verifies that only the correct files are visible to
  // the application and that redirections happen following the correct order of precedence for rule
  // evaluation.
  TEST_CASE(DocumentedExample_MechanicsOfFilesystemRules_SameOriginDirectories)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:CatchAll]\n"
        L"OriginDirectory = C:\\AppDir\\DataDir\n"
        L"TargetDirectory = C:\\TargetDir\\CatchAll\n"
        L"\n"
        L"[FilesystemRule:TxtFilesOnly]\n"
        L"OriginDirectory = C:\\AppDir\\DataDir\n"
        L"TargetDirectory = C:\\TargetDir\\TxtFilesOnly\n"
        L"FilePattern = *.txt\n"
        L"RedirectMode = Overlay\n"
        L"\n"
        L"[FilesystemRule:BinAndLogFilesOnly]\n"
        L"OriginDirectory = C:\\AppDir\\DataDir\n"
        L"TargetDirectory = C:\\TargetDir\\BinAndLogFilesOnly\n"
        L"FilePattern = *.bin\n"
        L"FilePattern = *.log\n"
        L"\n"
        L"[FilesystemRule:ExeFilesOnly]\n"
        L"OriginDirectory = C:\\AppDir\\DataDir\n"
        L"TargetDirectory = C:\\TargetDir\\ExeFilesOnly\n"
        L"FilePattern = *.exe";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir",
        {L"Origin.txt", L"Origin.bin", L"Origin.log", L"Origin.exe", L"Origin.dat"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\TargetDir\\CatchAll",
        {L"CatchAllFile.txt",
         L"CatchAllFile.bin",
         L"CatchAllFile.log",
         L"CatchAllFile.exe",
         L"CatchAllFile.dat"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\TargetDir\\TxtFilesOnly",
        {L"TxtFilesOnly.txt",
         L"TxtFilesOnly.bin",
         L"TxtFilesOnly.log",
         L"TxtFilesOnly.exe",
         L"TxtFilesOnly.dat"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\TargetDir\\BinAndLogFilesOnly",
        {L"BinAndLogFilesOnly.txt",
         L"BinAndLogFilesOnly.bin",
         L"BinAndLogFilesOnly.log",
         L"BinAndLogFilesOnly.exe",
         L"BinAndLogFilesOnly.dat"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\TargetDir\\ExeFilesOnly",
        {L"ExeFilesOnly.txt",
         L"ExeFilesOnly.bin",
         L"ExeFilesOnly.log",
         L"ExeFilesOnly.exe",
         L"ExeFilesOnly.dat"});

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    VerifyDirectoryAppearsToContain(
        context,
        L"C:\\AppDir\\DataDir",
        {L"Origin.txt",
         L"CatchAllFile.dat",
         L"TxtFilesOnly.txt",
         L"BinAndLogFilesOnly.bin",
         L"BinAndLogFilesOnly.log",
         L"ExeFilesOnly.exe"});
  }
} // namespace PathwinderTest
