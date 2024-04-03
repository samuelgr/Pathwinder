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

  /// Type alias for sets that hold compile-time constant filenames.
  using TFileNameSet = std::set<std::wstring_view>;

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
  /// information. Sends requests via the filesystem executor but can fall back to direct file
  /// operations if no redirection is needed for the operation. If the directory enumeration
  /// operation fails, this function causes a test failure.
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
    const ULONG queryFlags =
        ((true == restartEnumeration) ? SL_RESTART_SCAN : 0) | SL_RETURN_SINGLE_ENTRY;

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

    TEST_ASSERT((false == prepareResult.has_value()) || (NtStatus::kSuccess == prepareResult));

    if (false == prepareResult.has_value())
    {
      const NTSTATUS advanceResult = FilesystemOperations::PartialEnumerateDirectoryContents(
          directoryHandle,
          SFileNamesInformation::kFileInformationClass,
          nextFileInformation.Data(),
          nextFileInformation.CapacityBytes(),
          queryFlags,
          queryFilePattern);

      nextFileInformation.UnsafeSetStructSizeBytes(
          FileInformationStructLayout::SizeOfStructByType<SFileNamesInformation>(
              nextFileInformation.GetFileInformationStruct()));
      return advanceResult;
    }
    else
    {
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
          queryFlags,
          queryFilePatternUnicodeStringPtr);

      nextFileInformation.UnsafeSetStructSizeBytes(
          static_cast<unsigned int>(ioStatusBlock.Information));
      return advanceResult;
    }
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
        FILE_SYNCHRONOUS_IO_NONALERT,
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

  /// Uses the filesystem executor subsystem to create a new file and add it to the mock filesystem.
  /// @param [in] absolutePathToCreate Absolute path of the file to be opened. In order for this
  /// function to succeed the file must exist in the fake filesystem.
  /// @param [in] director Filesystem director object, created as part of the calling test case.
  /// @param [in, out] openHandleStore Open handle store object, created and maintained by the
  /// calling test case and potentially updated by the filesystem executor.
  /// @param [in, out] mockFilesystem Fake filesystem object, created and maintained by the calling
  /// test case and potentially modified by the filesystem executor.
  /// @return Handle to the newly-opened file.
  static void AddFileUsingFilesystemExecutor(
      std::wstring_view absolutePathToCreate,
      const FilesystemDirector& director,
      OpenHandleStore& openHandleStore,
      MockFilesystemOperations& mockFilesystem)
  {
    HANDLE newlyOpenedFileHandle = nullptr;

    UNICODE_STRING absolutePathToCreateUnicodeString =
        Strings::NtConvertStringViewToUnicodeString(absolutePathToCreate);
    OBJECT_ATTRIBUTES absolutePathToCreateObjectAttributes = {
        .Length = sizeof(OBJECT_ATTRIBUTES),
        .RootDirectory = nullptr,
        .ObjectName = &absolutePathToCreateUnicodeString};

    NTSTATUS openHandleResult = FilesystemExecutor::NewFileHandle(
        __FUNCTIONW__,
        kFunctionRequestIdentifier,
        openHandleStore,
        &newlyOpenedFileHandle,
        FILE_GENERIC_READ | FILE_GENERIC_WRITE,
        &absolutePathToCreateObjectAttributes,
        0,
        FILE_CREATE,
        FILE_SYNCHRONOUS_IO_NONALERT,
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
          std::wstring_view absolutePathToCreate =
              Strings::NtConvertUnicodeStringToStringView(*objectAttributes->ObjectName);

          mockFilesystem.AddFile(absolutePathToCreate);
          HANDLE newlyOpenedFileHandle = mockFilesystem.Open(absolutePathToCreate);
          if (newlyOpenedFileHandle != nullptr)
          {
            *fileHandle = newlyOpenedFileHandle;
            return NtStatus::kSuccess;
          }

          return NtStatus::kObjectNameNotFound;
        });

    TEST_ASSERT(NtStatus::kSuccess == openHandleResult);
    CloseHandleUsingFilesystemExecutor(newlyOpenedFileHandle, openHandleStore, mockFilesystem);
  }

  /// Verifies that a set of files are all accessible and can be opened by directly requesting them
  /// using their absolute paths.
  /// @param [in] directoryAbsolutePath Absolute path of the directory, with no trailing backslash,
  /// in which the files should be accessible.
  /// @param [in] expectedFiles Filenames that are expected to be accessible.
  /// @param [in] filesystemDirector Filesystem director object, created as part of the calling test
  /// case.
  /// @param [in, out] openHandleStore Open handle store object, created and maintained by the
  /// calling test case and potentially updated by the filesystem executor.
  /// @param [in, out] mockFilesystem Fake filesystem object, created and maintained by the calling
  /// test case and potentially modified by the filesystem executor.
  static void VerifyFilesAccessibleByAbsolutePath(
      std::wstring_view directoryAbsolutePath,
      const TFileNameSet& expectedFiles,
      const FilesystemDirector& filesystemDirector,
      OpenHandleStore& openHandleStore,
      MockFilesystemOperations& mockFilesystem)
  {
    for (const auto& expectedFile : expectedFiles)
    {
      TemporaryString expectedFileAbsolutePath;
      expectedFileAbsolutePath << directoryAbsolutePath << L'\\' << expectedFile;

      HANDLE expectedFileHandle = OpenFileUsingFilesystemExecutor(
          expectedFileAbsolutePath, filesystemDirector, openHandleStore, mockFilesystem);
      CloseHandleUsingFilesystemExecutor(expectedFileHandle, openHandleStore, mockFilesystem);
    }
  }

  /// Verifies that a specific set of files is enumerated as being present in a particular
  /// directory.
  /// @param [in] directoryAbsolutePath Absolute path of the directory, with no trailing backslash,
  /// that should be enumerated.
  /// @param [in] expectedFiles Filenames that are expected to be enumerated.
  /// @param [in] filesystemDirector Filesystem director object, created as part of the calling test
  /// case.
  /// @param [in, out] openHandleStore Open handle store object, created and maintained by the
  /// calling test case and potentially updated by the filesystem executor.
  /// @param [in, out] mockFilesystem Fake filesystem object, created and maintained by the calling
  /// test case and potentially modified by the filesystem executor.
  /// @param [in] queryFilePattern File pattern to be passed to the filesystem executor to use for
  /// filtering results when enumerating directory contents. Defaults to no file pattern, which
  /// means to match all files.
  static void VerifyFilesEnumeratedForDirectory(
      std::wstring_view directoryAbsolutePath,
      const TFileNameSet& expectedFiles,
      const FilesystemDirector& filesystemDirector,
      OpenHandleStore& openHandleStore,
      MockFilesystemOperations& mockFilesystem,
      std::wstring_view queryFilePattern = std::wstring_view())
  {
    HANDLE directoryHandle = OpenFileUsingFilesystemExecutor(
        directoryAbsolutePath, filesystemDirector, openHandleStore, mockFilesystem);

    BytewiseDanglingFilenameStruct<SFileNamesInformation> singleEnumeratedFileInformation;

    std::set<std::wstring> actualFiles;
    while (actualFiles.size() < expectedFiles.size())
    {
      const NTSTATUS enumerateResult = EnumerateOneFileUsingFilesystemExecutor(
          directoryHandle,
          queryFilePattern,
          false,
          singleEnumeratedFileInformation,
          filesystemDirector,
          openHandleStore);

      if (NtStatus::kSuccess == enumerateResult)
      {
        std::wstring enumeratedFileName =
            std::wstring(singleEnumeratedFileInformation.GetDanglingFilename());

        TEST_ASSERT(true == expectedFiles.contains(enumeratedFileName));
        TEST_ASSERT(true == actualFiles.insert(std::move(enumeratedFileName)).second);
      }
      else
      {
        TEST_ASSERT(NtStatus::kNoMoreFiles == enumerateResult);
        break;
      }
    }

    TEST_ASSERT(actualFiles.size() == expectedFiles.size());
  }

  /// Verifies that a directory appears to contain exactly the specified set of files and
  /// subdirectories. Queries for the contents of the directory of interest by using the filesystem
  /// executor and, where necessary, filesystem operations (which would in turn hit the mock
  /// filesystem).
  /// @param [in] expectedFiles Filenames that are expected to be enumerated.
  /// @param [in] directoryAbsolutePath Absolute path of the directory, with no trailing backslash,
  /// that should be enumerated.
  /// @param [in] filesystemDirector Filesystem director object, created as part of the calling test
  /// case.
  /// @param [in, out] openHandleStore Open handle store object, created and maintained by the
  /// calling test case and potentially updated by the filesystem executor.
  /// @param [in, out] mockFilesystem Fake filesystem object, created and maintained by the calling
  /// test case and potentially modified by the filesystem executor.
  /// @param [in] queryFilePattern File pattern to be passed to the filesystem executor to use for
  /// filtering results when enumerating directory contents. Defaults to no file pattern, which
  /// means to match all files.
  static void VerifyDirectoryAppearsToContain(
      std::wstring_view directoryAbsolutePath,
      const TFileNameSet& expectedFiles,
      const FilesystemDirector& filesystemDirector,
      OpenHandleStore& openHandleStore,
      MockFilesystemOperations& mockFilesystem)
  {
    VerifyFilesAccessibleByAbsolutePath(
        directoryAbsolutePath, expectedFiles, filesystemDirector, openHandleStore, mockFilesystem);
    VerifyFilesEnumeratedForDirectory(
        directoryAbsolutePath, expectedFiles, filesystemDirector, openHandleStore, mockFilesystem);
  }

  /// Creates a filesystem director object by building it from a string representation of a
  /// configuration file, which should contain one or more filesystem rules. Triggers a test failure
  /// if the filesystem director fails to build.
  /// @param configurationFileString Configuration file containing one or more filesystem rules.
  /// @return Filesystem director object built from the configuration file string.
  static FilesystemDirector FilesystemDirectorFromConfigurationFileString(
      std::wstring_view configurationFileString)
  {
    ConfigurationData configurationData =
        PathwinderConfigReader().ReadInMemoryConfigurationFile(configurationFileString);

    FilesystemDirectorBuilder filesystemDirectorBuilder;
    auto maybeFilesystemDirector =
        filesystemDirectorBuilder.BuildFromConfigurationData(configurationData);
    TEST_ASSERT(true == maybeFilesystemDirector.has_value());

    return std::move(*maybeFilesystemDirector);
  }

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
    mockFilesystem.SetConfigAllowOpenNonExistentFile(true);

    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"TextFile.txt", L"Output.log"});
    mockFilesystem.AddDirectory(L"C:\\TargetDir\\TargetSub");

    FilesystemDirector filesystemDirector =
        FilesystemDirectorFromConfigurationFileString(kConfigurationFileString);
    OpenHandleStore openHandleStore;

    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir",
        {L"TextFile.txt", L"Output.log", L"TargetSub"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
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
    mockFilesystem.SetConfigAllowOpenNonExistentFile(true);

    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddDirectory(L"C:\\AppData\\DataDir");
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"TextFile.txt", L"Output.log"});
    mockFilesystem.AddDirectory(L"C:\\TargetDir\\TargetSub");

    FilesystemDirector filesystemDirector =
        FilesystemDirectorFromConfigurationFileString(kConfigurationFileString);
    OpenHandleStore openHandleStore;

    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir",
        {L"TextFile.txt", L"Output.log", L"TargetSub"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
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
    mockFilesystem.SetConfigAllowOpenNonExistentFile(true);

    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir", {L"DataFile1.dat", L"DataFile2.dat"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir\\DataSubdir", {L"DataSubFile.dat.dat"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"TextFile.txt", L"Output.log"});
    mockFilesystem.AddDirectory(L"C:\\TargetDir\\TargetSub");

    FilesystemDirector filesystemDirector =
        FilesystemDirectorFromConfigurationFileString(kConfigurationFileString);
    OpenHandleStore openHandleStore;

    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir",
        {L"TextFile.txt", L"Output.log", L"TargetSub"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
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
    mockFilesystem.SetConfigAllowOpenNonExistentFile(true);

    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir", {L"1stOrigin.txt", L"2ndOrigin.bin"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"3rdTarget.txt", L"4thTarget.log"});

    FilesystemDirector filesystemDirector =
        FilesystemDirectorFromConfigurationFileString(kConfigurationFileString);
    OpenHandleStore openHandleStore;

    // First part from the documented example is just the results of applying the rule. The *.txt
    // file originally present in the origin directory is hidden, and the *.txt file in the target
    // directory is visible.
    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir",
        {L"2ndOrigin.bin", L"3rdTarget.txt"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);

    // Second part of the documented example is to create an out-of-scope file. It should be added
    // to, and visible in, the origin directory as a real file and not present in the target
    // directory.
    AddFileUsingFilesystemExecutor(
        L"C:\\AppDir\\DataDir\\Data.dat", filesystemDirector, openHandleStore, mockFilesystem);

    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir",
        {L"2ndOrigin.bin", L"3rdTarget.txt", L"Data.dat"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);

    TEST_ASSERT(true == mockFilesystem.Exists(L"C:\\AppDir\\DataDir\\Data.dat"));
    TEST_ASSERT(false == mockFilesystem.Exists(L"C:\\TargetDir\\Data.dat"));

    // Third part of the documented example is to create an in-scope file. It should be added to the
    // target directory and visible in the origin directory.
    AddFileUsingFilesystemExecutor(
        L"C:\\AppDir\\DataDir\\Output.txt", filesystemDirector, openHandleStore, mockFilesystem);

    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir",
        {L"2ndOrigin.bin", L"3rdTarget.txt", L"Data.dat", L"Output.txt"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);

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
    mockFilesystem.SetConfigAllowOpenNonExistentFile(true);

    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir", {L"1stOrigin.txt", L"2ndOrigin.bin"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\OriginSubA", {L"OutputA.txt"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\OriginSubB.txt", {L"OutputB.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"3rdTarget.txt", L"4thTarget.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir\\TargetSubA", {L"ContentsA.txt"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\TargetDir\\TargetSubB.txt", {L"ContentsB.txt", L"ContentsB2.bin"});

    FilesystemDirector filesystemDirector =
        FilesystemDirectorFromConfigurationFileString(kConfigurationFileString);
    OpenHandleStore openHandleStore;

    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir",
        {L"2ndOrigin.bin", L"3rdTarget.txt", L"OriginSubA", L"TargetSubB.txt"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);

    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir\\OriginSubA",
        {L"OutputA.txt"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);

    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir\\TargetSubB.txt",
        {L"ContentsB.txt", L"ContentsB2.bin"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
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
    mockFilesystem.SetConfigAllowOpenNonExistentFile(true);

    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir", {L"1stOrigin.txt", L"2ndOrigin.bin"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\OriginSub", {L"OutputA.txt"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\MoreData.txt", {L"OutputB.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"3rdTarget.txt", L"4thTarget.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir\\TargetSub", {L"ContentsA.txt"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\TargetDir\\MoreData.txt", {L"OutputB.log", L"ContentsB2.bin"});

    FilesystemDirector filesystemDirector =
        FilesystemDirectorFromConfigurationFileString(kConfigurationFileString);
    OpenHandleStore openHandleStore;

    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir",
        {L"1stOrigin.txt",
         L"2ndOrigin.bin",
         L"3rdTarget.txt",
         L"4thTarget.log",
         L"OriginSub",
         L"TargetSub",
         L"MoreData.txt"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir\\OriginSub",
        {L"OutputA.txt"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir\\TargetSub",
        {L"ContentsA.txt"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir\\MoreData.txt",
        {L"OutputB.log", L"ContentsB2.bin"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
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
    mockFilesystem.SetConfigAllowOpenNonExistentFile(true);

    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir", {L"App.exe"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\AppDir\\DataDir", {L"1stOrigin.txt", L"2ndOrigin.bin"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\OriginSub", {L"OutputA.txt"});
    mockFilesystem.AddFilesInDirectory(L"C:\\AppDir\\DataDir\\MoreData.txt", {L"OutputB.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir", {L"3rdTarget.txt", L"4thTarget.log"});
    mockFilesystem.AddFilesInDirectory(L"C:\\TargetDir\\TargetSub", {L"ContentsA.txt"});
    mockFilesystem.AddFilesInDirectory(
        L"C:\\TargetDir\\MoreData.txt", {L"OutputB.log", L"ContentsB2.bin"});

    FilesystemDirector filesystemDirector =
        FilesystemDirectorFromConfigurationFileString(kConfigurationFileString);
    OpenHandleStore openHandleStore;

    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir",
        {L"1stOrigin.txt", L"2ndOrigin.bin", L"3rdTarget.txt", L"OriginSub", L"MoreData.txt"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir\\OriginSub",
        {L"OutputA.txt"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
    VerifyDirectoryAppearsToContain(
        L"C:\\AppDir\\DataDir\\MoreData.txt",
        {L"OutputB.log", L"ContentsB2.bin"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
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

    FilesystemDirector filesystemDirector =
        FilesystemDirectorFromConfigurationFileString(kConfigurationFileString);
    OpenHandleStore openHandleStore;

    // Expected behavior when accessing C:\Origin is that these files should be accessible both by
    // enumeration and by direct request:
    //  - All *.rtf files in C:\Target\1 and in C:\Origin
    //  - All *.odt files in C:\Target\2 and in C:\Origin
    //  - All *.txt files in C:\Target\3 and in C:\Origin
    //  - All files of other types in C:\Target\4

    VerifyDirectoryAppearsToContain(
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
         L"4_C.log"},
        filesystemDirector,
        openHandleStore,
        mockFilesystem);
  }
} // namespace PathwinderTest
