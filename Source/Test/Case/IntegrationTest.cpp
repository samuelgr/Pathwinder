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
#include "MockFilesystemOperations.h"
#include "PathwinderConfigReader.h"
#include "Strings.h"

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  /// Function request identifier to be passed to all filesystem executor functions when they are
  /// invoked for testing.
  static constexpr unsigned int kFunctionRequestIdentifier = 9999;

  /// Uses the filesystem executor subsystem to close an open handle. If the operation fails, this
  /// function causes a test failure.
  /// @param [in] handleToClose Previously-opened handle that should be closed.
  /// @param [in, out] openHandleStore Open handle store object, created and maintained by the
  /// calling test case and potentially updated by the filesystem executor.
  /// @param [in, out] mockFilesystem Fake filesystem object, created and maintained by the calling
  /// test case and potentially modified by the filesystem executor.
  static void CloseHandleUsingFilesystemExecutor(
      HANDLE handleToClose,
      OpenHandleStore& openHandleStore,
      MockFilesystemOperations& mockFilesystem)
  {
    NTSTATUS closeHandleResult = FilesystemExecutor::CloseHandle(
        __FUNCTIONW__,
        kFunctionRequestIdentifier,
        openHandleStore,
        handleToClose,
        [&mockFilesystem](HANDLE handleToClose) -> NTSTATUS
        {
          return mockFilesystem.CloseHandle(handleToClose);
        });

    TEST_ASSERT(NT_SUCCESS(closeHandleResult));
  }

  /// Enumerates a single file and fills its file name information structure with the resulting
  /// information. If the directory enumeration operation fails or would be sent to the system
  /// without interception, this function causes a test failure.
  /// @param [in] directoryHandle Open handle to the directory that is being enumerated.
  /// @param [in] queryFilePattern File pattern for filtering out which files are enumerated.
  /// Corresponds to an application-requested file pattern.
  /// @param [in] restartEnumeration Whether or not to restart the enumeration over again from the
  /// beginning.
  /// @param [out] nextFileInformation Buffer to which the next file's information is expected to be
  /// written.
  /// @param [in] director Filesystem director object, created as part of the calling test case.
  /// @param [in, out] openHandleStore Open handle store object, created and maintained by the
  /// calling test case and potentially updated by the filesystem executor.
  /// @return
  static NTSTATUS EnumerateOneFileUsingFilesystemExecutor(
      HANDLE directoryHandle,
      std::wstring_view queryFilePattern,
      bool restartEnumeration,
      BytewiseDanglingFilenameStruct<SFileNamesInformation>& nextFileInformation,
      const FilesystemDirector& director,
      OpenHandleStore& openHandleStore)
  {
    UNICODE_STRING queryFilePatternUnicodeString =
        Strings::NtConvertStringViewToUnicodeString(queryFilePattern);
    PUNICODE_STRING queryFilePatternUnicodeStringPtr =
        ((false == queryFilePattern.empty()) ? &queryFilePatternUnicodeString : nullptr);

    const std::optional<NTSTATUS> prepareResult = FilesystemExecutor::DirectoryEnumerationPrepare(
        __FUNCTIONW__,
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        nextFileInformation.Data(),
        nextFileInformation.CapacityBytes(),
        SFileNamesInformation::kFileInformationClass,
        queryFilePatternUnicodeStringPtr,
        [&director](std::wstring_view associatedPath, std::wstring_view realOpenedPath)
            -> DirectoryEnumerationInstruction
        {
          return director.GetInstructionForDirectoryEnumeration(associatedPath, realOpenedPath);
        });

    TEST_ASSERT(NtStatus::kSuccess == prepareResult);

    IO_STATUS_BLOCK ioStatusBlock{};

    const NTSTATUS advanceResult = FilesystemExecutor::DirectoryEnumerationAdvance(
        __FUNCTIONW__,
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        nullptr,
        nullptr,
        nullptr,
        &ioStatusBlock,
        nextFileInformation.Data(),
        nextFileInformation.CapacityBytes(),
        SFileNamesInformation::kFileInformationClass,
        ((true == restartEnumeration) ? SL_RESTART_SCAN : 0) | SL_RETURN_SINGLE_ENTRY,
        queryFilePatternUnicodeStringPtr);

    nextFileInformation.UnsafeSetStructSizeBytes(
        static_cast<unsigned int>(ioStatusBlock.Information));
    return advanceResult;
  }

  /// Uses the filesystem executor subsystem to open a file handle for reading, including directory
  /// enumeration, for the specified absolute file path. If the operation fails, this function
  /// causes a test failure.
  /// @param [in] absolutePathToOpen Absolute path of the file to be opened. In order for this
  /// function to succeed the file must exist in the fake filesystem.
  /// @param [in] director Filesystem director object, created as part of the calling test case.
  /// @param [in, out] openHandleStore Open handle store object, created and maintained by the
  /// calling test case and potentially updated by the filesystem executor.
  /// @param [in, out] mockFilesystem Fake filesystem object, created and maintained by the calling
  /// test case and potentially modified by the filesystem executor.
  /// @return Handle to the newly-opened file.
  static HANDLE OpenFileUsingFilesystemExecutor(
      std::wstring_view absolutePathToOpen,
      const FilesystemDirector& director,
      OpenHandleStore& openHandleStore,
      MockFilesystemOperations& mockFilesystem)
  {
    HANDLE newlyOpenedFileHandle = nullptr;

    UNICODE_STRING absolutePathToOpenUnicodeString =
        Strings::NtConvertStringViewToUnicodeString(absolutePathToOpen);
    OBJECT_ATTRIBUTES absolutePathToOpenObjectAttributes = {
        .Length = sizeof(OBJECT_ATTRIBUTES),
        .RootDirectory = nullptr,
        .ObjectName = &absolutePathToOpenUnicodeString};

    NTSTATUS openHandleResult = FilesystemExecutor::NewFileHandle(
        __FUNCTIONW__,
        kFunctionRequestIdentifier,
        openHandleStore,
        &newlyOpenedFileHandle,
        FILE_GENERIC_READ,
        &absolutePathToOpenObjectAttributes,
        0,
        FILE_OPEN,
        0,
        [&director](
            std::wstring_view absolutePath,
            FileAccessMode fileAccessMode,
            CreateDisposition createDisposition) -> FileOperationInstruction
        {
          return director.GetInstructionForFileOperation(
              absolutePath, fileAccessMode, createDisposition);
        },
        [&mockFilesystem](
            PHANDLE fileHandle,
            POBJECT_ATTRIBUTES objectAttributes,
            ULONG createDisposition) -> NTSTATUS
        {
          std::wstring_view absolutePathToOpen =
              Strings::NtConvertUnicodeStringToStringView(*objectAttributes->ObjectName);

          HANDLE newlyOpenedFileHandle = mockFilesystem.Open(absolutePathToOpen);
          if (newlyOpenedFileHandle != nullptr)
          {
            *fileHandle = newlyOpenedFileHandle;
            return NtStatus::kSuccess;
          }

          return NtStatus::kObjectNameNotFound;
        });

    TEST_ASSERT(NtStatus::kSuccess == openHandleResult);
    return newlyOpenedFileHandle;
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
    mockFilesystem.SetConfigAllowOpenNonExistentFile(true);
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

    ConfigurationData configurationData =
        PathwinderConfigReader().ReadInMemoryConfigurationFile(kConfigurationFileString);

    FilesystemDirectorBuilder filesystemDirectorBuilder;
    auto maybeFilesystemDirector =
        filesystemDirectorBuilder.BuildFromConfigurationData(configurationData);
    TEST_ASSERT(true == maybeFilesystemDirector.has_value());

    const FilesystemDirector& filesystemDirector = *maybeFilesystemDirector;
    OpenHandleStore openHandleStore;

    // Expected behavior when accessing C:\Origin is that these files should be accessible both by
    // enumeration and by direct request:
    //  - All *.rtf files in C:\Target\1 and in C:\Origin
    //  - All *.odt files in C:\Target\2 and in C:\Origin
    //  - All *.txt files in C:\Target\3 and in C:\Origin
    //  - All files of other types in C:\Target\4
    const std::set<std::wstring> expectedAccessibleFilesInOriginDirectory = {
        L"1_A.rtf",
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
        L"4_C.log"};

    // This loop verifies that all of the files that should be accessible within the origin
    // directory can be opened by accessing them directly using their absolute paths.
    for (const auto& expectedAccessibleFile : expectedAccessibleFilesInOriginDirectory)
    {
      TemporaryString expectedAccessibleFileAbsolutePath;
      expectedAccessibleFileAbsolutePath << L"C:\\Origin\\" << expectedAccessibleFile;

      HANDLE expectedAccessibleFileHandle = OpenFileUsingFilesystemExecutor(
          expectedAccessibleFileAbsolutePath, filesystemDirector, openHandleStore, mockFilesystem);
      CloseHandleUsingFilesystemExecutor(
          expectedAccessibleFileHandle, openHandleStore, mockFilesystem);
    }

    // The next part of the test enumerates the contents of the origin directory and verifies that
    // all files that should be present there are correctly enumerated.
    HANDLE originDirectoryHandle = OpenFileUsingFilesystemExecutor(
        L"C:\\Origin", filesystemDirector, openHandleStore, mockFilesystem);

    BytewiseDanglingFilenameStruct<SFileNamesInformation> singleEnumeratedFileInformation;

    std::set<std::wstring> actualAccessibleFilesInOriginDirectory;
    while (actualAccessibleFilesInOriginDirectory.size() <
           expectedAccessibleFilesInOriginDirectory.size())
    {
      const NTSTATUS enumerateResult = EnumerateOneFileUsingFilesystemExecutor(
          originDirectoryHandle,
          std::wstring_view(),
          false,
          singleEnumeratedFileInformation,
          filesystemDirector,
          openHandleStore);

      if (NtStatus::kSuccess == enumerateResult)
      {
        std::wstring_view enumeratedFileName =
            singleEnumeratedFileInformation.GetDanglingFilename();

        TEST_ASSERT(
            true ==
            actualAccessibleFilesInOriginDirectory.insert(std::wstring(enumeratedFileName)).second);
      }
      else
      {
        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerateResult);
        break;
      }
    }

    TEST_ASSERT(actualAccessibleFilesInOriginDirectory == expectedAccessibleFilesInOriginDirectory);
  }
} // namespace PathwinderTest
