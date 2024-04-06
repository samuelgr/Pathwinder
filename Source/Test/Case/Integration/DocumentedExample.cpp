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
} // namespace PathwinderTest
