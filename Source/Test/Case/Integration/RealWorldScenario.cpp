/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file RealWorldScenario.cpp
 *   Integration tests based on situations tested with real applications.
 **************************************************************************************************/

#include <set>
#include <string_view>

#include <Infra/Test/TestCase.h>

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

  // Tests a real-world scenario in which only one rule is defined but it uses relative path
  // components for both the origin and target directories. The resulting filesystem director is
  // checked for having a filesystem rule with the correct origin and target directories.
  TEST_CASE(RealWorldScenario_SingleRule_RelativePathComponents)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:Test]\n"
        L"OriginDirectory = C:\\Test\\OriginDir1\\..\\OriginDir2\\.\n"
        L"TargetDirectory = C:\\Test\\TargetDir1\\.\\.\\\\\\..\\TargetDir2\\";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\Test");

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    const FilesystemRule* testRule = context->filesystemDirector.FindRuleByName(L"Test");
    TEST_ASSERT(nullptr != testRule);
    TEST_ASSERT(L"C:\\Test\\OriginDir2" == testRule->GetOriginDirectoryFullPath());
    TEST_ASSERT(L"C:\\Test\\TargetDir2" == testRule->GetTargetDirectoryFullPath());
  }

  // Tests a real-world scenario in which only one rule is defined but it refers to an origin
  // directory that does not really exist. If the target directory also does not exist then the
  // origin directory is not made available to the application. If the target directory is
  // subsequently created, then the origin directory appears to the application too.
  TEST_CASE(RealWorldScenario_SingleRule_OriginDirectoryOnlyShownIfTargetExists)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:Test]\n"
        L"OriginDirectory = C:\\Test\\OriginDir\n"
        L"TargetDirectory = C:\\Test\\TargetDir";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\Test");

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    // Since neither the origin nor the target directories actually exist in the real filesystem,
    // neither should be visible to the application.
    VerifyDirectoryAppearsToContain(context, L"C:\\Test", {});

    // Once the target directory is created, the origin directory should be visible to the
    // application too. This test simulates creating the target directory externally (for example,
    // by using File Explorer) by accessing the mock filesystem directly rather than by using the
    // filesystem executor.
    mockFilesystem.AddDirectory(L"C:\\Test\\TargetDir");
    VerifyDirectoryAppearsToContain(context, L"C:\\Test", {L"OriginDir", L"TargetDir"});
  }

  // Tests a real-world scenario in which multiple rules all have the same origin directory. This is
  // about rule precedence: all rules except one use Overlay mode and have file patterns, and the
  // final rule uses Simple mode and has no file patterns. Any files in scope of the first three
  // rules, that exist for real in the origin directory but not the target directory, should be
  // available.
  TEST_CASE(RealWorldScenario_MultipleRulesSameOriginDirectory_SimpleWildcardOverlayFilePatterns)
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

  // Verifies that file operations that use a root directory file handle are appropriately
  // redirected or not. In this case the root directory handle is exactly equal to a
  // filesystem rule's origin directory, meaning it is redirected elsewhere, and may need to have
  // its path re-composed to the origin side or a different rule's target side.
  TEST_CASE(RealWorldScenario_OpenOriginDirectory_RootDirectoryHandlePathComposition)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:Test]\n"
        L"OriginDirectory = C:\\Test\\OriginDir\n"
        L"TargetDirectory = C:\\Test\\TargetDir\n"
        L"FilePattern = *.txt\n"
        L"\n"
        L"[FilesystemRule:Test2]\n"
        L"OriginDirectory = C:\\Test\\OriginDir\n"
        L"TargetDirectory = C:\\Test\\TargetDir2\n"
        L"FilePattern = *.log";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\Test");
    mockFilesystem.AddFilesInDirectory(L"C:\\Test\\OriginDir", {L"OriginFile.bin"});
    mockFilesystem.AddFilesInDirectory(L"C:\\Test\\TargetDir", {L"TargetFile.txt"});
    mockFilesystem.AddFilesInDirectory(L"C:\\Test\\TargetDir2", {L"TargetFile2.log"});

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    HANDLE rootDirectoryHandle = OpenUsingFilesystemExecutor(context, L"C:\\Test\\OriginDir");

    // This part of the test verifies that the files can be accessed correctly when opened by
    // creating a new file handle.

    HANDLE originSideFileHandle =
        OpenUsingFilesystemExecutor(context, L"OriginFile.bin", rootDirectoryHandle);
    TEST_ASSERT(
        L"C:\\Test\\OriginDir\\OriginFile.bin" ==
        mockFilesystem.GetPathFromHandle(originSideFileHandle));

    HANDLE targetSideFileHandle =
        OpenUsingFilesystemExecutor(context, L"TargetFile.txt", rootDirectoryHandle);
    TEST_ASSERT(
        L"C:\\Test\\TargetDir\\TargetFile.txt" ==
        mockFilesystem.GetPathFromHandle(targetSideFileHandle));

    HANDLE targetSideFileHandle2 =
        OpenUsingFilesystemExecutor(context, L"TargetFile2.log", rootDirectoryHandle);
    TEST_ASSERT(
        L"C:\\Test\\TargetDir2\\TargetFile2.log" ==
        mockFilesystem.GetPathFromHandle(targetSideFileHandle2));

    // This part of the test verifies that the files can be accessed correctly when queried for
    // information by name, with no file handle expected to be created.

    TEST_ASSERT(
        true ==
        QueryExistsUsingFilesystemExecutor(context, L"OriginFile.bin", rootDirectoryHandle));
    TEST_ASSERT(
        true ==
        QueryExistsUsingFilesystemExecutor(context, L"TargetFile.txt", rootDirectoryHandle));
    TEST_ASSERT(
        true ==
        QueryExistsUsingFilesystemExecutor(context, L"TargetFile2.log", rootDirectoryHandle));
  }

  // Exercises a real-world scenario in which a deep hierarchy of illusionary directories is created
  // using filesystem rules and a new file is created at the deepest level. Even though the
  // containing directory on the origin side does not really exist, because the containing directory
  // is an illusionary directory it should result in the correct target-side hierarchy being created
  // automatically. As a result, the file creation attempt should succeed on the target side.
  TEST_CASE(RealWorldScenario_CreateNewFile_DeepOriginDirectoryHierarchy)
  {
    constexpr std::wstring_view kConfigurationFileString =
        L"[FilesystemRule:Intermediate1]\n"
        L"OriginDirectory = C:\\Origin\\Level1\n"
        L"TargetDirectory = C:\\Temp\\Intermediate1\n"
        L"\n"
        L"[FilesystemRule:Intermediate2]\n"
        L"OriginDirectory = C:\\Origin\\Level1\\Level2\n"
        L"TargetDirectory = C:\\Temp\\Intermediate2\n"
        L"\n"
        L"[FilesystemRule:Intermediate3]\n"
        L"OriginDirectory = C:\\Origin\\Level1\\Level2\\Level3\n"
        L"TargetDirectory = C:\\Temp\\Intermediate3\n"
        L"\n"
        L"[FilesystemRule:Intermediate4]\n"
        L"OriginDirectory = C:\\Origin\\Level1\\Level2\\Level3\\Level4\n"
        L"TargetDirectory = C:\\Temp\\Intermediate4\n"
        L"\n"
        L"[FilesystemRule:Intermediate5]\n"
        L"OriginDirectory = C:\\Origin\\Level1\\Level2\\Level3\\Level4\\Level5\n"
        L"TargetDirectory = C:\\Temp\\Intermediate5\n"
        L"\n"
        L"[FilesystemRule:Test]\n"
        L"OriginDirectory = C:\\Origin\\Level1\\Level2\\Level3\\Level4\\Level5\\DesiredOrigin\n"
        L"TargetDirectory = C:\\DesiredTarget\\Subdir";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(L"C:\\Origin");

    TIntegrationTestContext context =
        CreateIntegrationTestContext(mockFilesystem, kConfigurationFileString);

    CreateFileUsingFilesystemExecutor(
        context, L"C:\\Origin\\Level1\\Level2\\Level3\\Level4\\Level5\\DesiredOrigin\\File.txt");

    TEST_ASSERT(true == mockFilesystem.IsDirectory(L"C:\\DesiredTarget"));
    TEST_ASSERT(true == mockFilesystem.IsDirectory(L"C:\\DesiredTarget\\Subdir"));
    TEST_ASSERT(true == mockFilesystem.Exists(L"C:\\DesiredTarget\\Subdir\\File.txt"));
  }
} // namespace PathwinderTest
