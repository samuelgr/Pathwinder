/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file IntegrationTestSupport.cpp
 *   Implementation of functions that facilitate the creation of integration tests by encapsulating
 *   much of the boilerplate and common logic for setting up data structures and interacting with
 *   the filesystem executor.
 **************************************************************************************************/

#include "TestCase.h"

#include "IntegrationTestSupport.h"

#include <memory>
#include <set>
#include <string_view>

#include <Infra/TemporaryBuffer.h>

#include "ApiWindows.h"
#include "FileInformationStruct.h"
#include "FilesystemDirector.h"
#include "FilesystemDirectorBuilder.h"
#include "FilesystemExecutor.h"
#include "MockFilesystemOperations.h"
#include "OpenHandleStore.h"
#include "PathwinderConfigReader.h"

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  static void CreateUsingFilesystemExecutor(
      TIntegrationTestContext& context,
      std::wstring_view pathToCreate,
      HANDLE rootDirectory,
      bool isDirectory)
  {
    HANDLE newlyCreatedFileHandle = nullptr;

    UNICODE_STRING pathToCreateUnicodeString =
        Strings::NtConvertStringViewToUnicodeString(pathToCreate);
    OBJECT_ATTRIBUTES pathToCreateObjectAttributes = {
        .Length = sizeof(OBJECT_ATTRIBUTES),
        .RootDirectory = rootDirectory,
        .ObjectName = &pathToCreateUnicodeString};

    NTSTATUS newFileHandleResult = FilesystemExecutor::NewFileHandle(
        __FUNCTIONW__,
        kFunctionRequestIdentifier,
        context->openHandleStore,
        &newlyCreatedFileHandle,
        FILE_GENERIC_READ | FILE_GENERIC_WRITE,
        &pathToCreateObjectAttributes,
        0,
        FILE_CREATE,
        FILE_SYNCHRONOUS_IO_NONALERT,
        [&context](
            std::wstring_view absolutePath,
            FileAccessMode fileAccessMode,
            CreateDisposition createDisposition) -> FileOperationInstruction
        {
          return context->filesystemDirector.GetInstructionForFileOperation(
              absolutePath, fileAccessMode, createDisposition);
        },
        [&context, isDirectory](
            PHANDLE fileHandle,
            POBJECT_ATTRIBUTES objectAttributes,
            ULONG createDisposition) -> NTSTATUS
        {
          std::wstring_view absolutePathToCreate =
              Strings::NtConvertUnicodeStringToStringView(*objectAttributes->ObjectName);

          if (true == isDirectory)
            context->mockFilesystem.InsertDirectory(absolutePathToCreate);
          else
            context->mockFilesystem.InsertFile(absolutePathToCreate);

          HANDLE newlyOpenedFileHandle = context->mockFilesystem.Open(absolutePathToCreate);
          if (newlyOpenedFileHandle != nullptr)
          {
            *fileHandle = newlyOpenedFileHandle;
            return NtStatus::kSuccess;
          }

          return NtStatus::kObjectNameNotFound;
        });

    TEST_ASSERT_WITH_FAILURE_MESSAGE(
        NtStatus::kSuccess == newFileHandleResult,
        "NTSTATUS = 0x%08x when attempting to create file \"%.*s\".",
        static_cast<unsigned int>(newFileHandleResult),
        static_cast<int>(pathToCreate.length()),
        pathToCreate.data());
    CloseHandleUsingFilesystemExecutor(context, newlyCreatedFileHandle);
  }

  /// Enumerates a single file and fills its file name information structure with the resulting
  /// information. Sends requests via the filesystem executor but can fall back to direct file
  /// operations if no redirection is needed for the operation. If the directory enumeration
  /// operation fails, this function causes a test failure.
  /// @param [in] context Integration test context object returned from
  /// #CreateIntegrationTestContext.
  /// @param [in] directoryHandle Open handle to the directory that is being enumerated.
  /// Corresponds to an application-requested file pattern.
  /// @param [out] nextFileInformation Buffer to which the next file's information is expected to
  /// be written.
  /// @return Result of the enumeration attempt.
  static NTSTATUS EnumerateOneFileUsingFilesystemExecutor(
      TIntegrationTestContext& context,
      HANDLE directoryHandle,
      BytewiseDanglingFilenameStruct<SFileNamesInformation>& nextFileInformation)
  {
    constexpr ULONG kQueryFlags = SL_RETURN_SINGLE_ENTRY;

    const std::optional<NTSTATUS> prepareResult = FilesystemExecutor::DirectoryEnumerationPrepare(
        __FUNCTIONW__,
        kFunctionRequestIdentifier,
        context->openHandleStore,
        directoryHandle,
        nextFileInformation.Data(),
        nextFileInformation.CapacityBytes(),
        SFileNamesInformation::kFileInformationClass,
        nullptr,
        [&context](std::wstring_view associatedPath, std::wstring_view realOpenedPath)
            -> DirectoryEnumerationInstruction
        {
          return context->filesystemDirector.GetInstructionForDirectoryEnumeration(
              associatedPath, realOpenedPath);
        });

    TEST_ASSERT_WITH_FAILURE_MESSAGE(
        (false == prepareResult.has_value()) || (NtStatus::kSuccess == prepareResult),
        "NTSTATUS = 0x%08x when attempting to prepare to enumerate directory represented by handle %zu.",
        static_cast<unsigned int>(*prepareResult),
        reinterpret_cast<size_t>(directoryHandle));

    if (false == prepareResult.has_value())
    {
      const NTSTATUS advanceResult = FilesystemOperations::PartialEnumerateDirectoryContents(
          directoryHandle,
          SFileNamesInformation::kFileInformationClass,
          nextFileInformation.Data(),
          nextFileInformation.CapacityBytes(),
          kQueryFlags);

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
          context->openHandleStore,
          directoryHandle,
          nullptr,
          nullptr,
          nullptr,
          &ioStatusBlock,
          nextFileInformation.Data(),
          nextFileInformation.CapacityBytes(),
          SFileNamesInformation::kFileInformationClass,
          kQueryFlags,
          nullptr);

      nextFileInformation.UnsafeSetStructSizeBytes(
          static_cast<unsigned int>(ioStatusBlock.Information));
      return advanceResult;
    }
  }

  /// Verifies that a set of files are all accessible and can be opened by directly requesting
  /// them using their absolute paths.
  /// @param [in] context Integration test context object returned from
  /// #CreateIntegrationTestContext.
  /// @param [in] directoryAbsolutePath Absolute path of the directory, with no trailing
  /// backslash, in which the files should be accessible.
  /// @param [in] expectedFiles Filenames that are expected to be accessible.
  static void VerifyFilesAccessibleByAbsolutePath(
      TIntegrationTestContext& context,
      std::wstring_view directoryAbsolutePath,
      const TFileNameSet& expectedFiles)
  {
    for (const auto& expectedFile : expectedFiles)
    {
      Infra::TemporaryString expectedFileAbsolutePath;
      expectedFileAbsolutePath << directoryAbsolutePath << L'\\' << expectedFile;

      HANDLE expectedFileHandle = OpenUsingFilesystemExecutor(context, expectedFileAbsolutePath);
      CloseHandleUsingFilesystemExecutor(context, expectedFileHandle);
    }
  }

  /// Verifies that a specific set of files is enumerated as being present in a particular
  /// directory.
  /// @param [in] context Integration test context object returned from
  /// #CreateIntegrationTestContext.
  /// @param [in] directoryAbsolutePath Absolute path of the directory, with no trailing
  /// backslash, that should be enumerated.
  /// @param [in] expectedFiles Filenames that are expected to be enumerated.
  static void VerifyFilesEnumeratedForDirectory(
      TIntegrationTestContext& context,
      std::wstring_view directoryAbsolutePath,
      const TFileNameSet& expectedFiles)
  {
    HANDLE directoryHandle = OpenUsingFilesystemExecutor(context, directoryAbsolutePath);

    BytewiseDanglingFilenameStruct<SFileNamesInformation> singleEnumeratedFileInformation;

    if (true == expectedFiles.empty())
    {
      const NTSTATUS enumerateResult = EnumerateOneFileUsingFilesystemExecutor(
          context, directoryHandle, singleEnumeratedFileInformation);

      TEST_ASSERT_WITH_FAILURE_MESSAGE(
          NtStatus::kNoSuchFile == enumerateResult,
          L"Unexpected file \"%.*s\" was enumerated in directory \"%.*s\".",
          static_cast<int>(singleEnumeratedFileInformation.GetDanglingFilename().length()),
          singleEnumeratedFileInformation.GetDanglingFilename().data(),
          static_cast<int>(directoryAbsolutePath.length()),
          directoryAbsolutePath.data());
    }

    std::set<std::wstring, std::less<>> actualFiles;
    std::set<std::wstring, std::less<>> unexpectedFiles;

    while (true)
    {
      const NTSTATUS enumerateResult = EnumerateOneFileUsingFilesystemExecutor(
          context, directoryHandle, singleEnumeratedFileInformation);

      if (NtStatus::kSuccess == enumerateResult)
      {
        std::wstring_view enumeratedFileName =
            singleEnumeratedFileInformation.GetDanglingFilename();

        if (true != expectedFiles.contains(enumeratedFileName))
          unexpectedFiles.emplace(enumeratedFileName);

        TEST_ASSERT_WITH_FAILURE_MESSAGE(
            true == actualFiles.emplace(enumeratedFileName).second,
            L"File \"%.*s\" in directory \"%.*s\" was enumerated multiple times.",
            static_cast<int>(enumeratedFileName.length()),
            enumeratedFileName.data(),
            static_cast<int>(directoryAbsolutePath.length()),
            directoryAbsolutePath.data());
      }
      else
      {
        TEST_ASSERT_WITH_FAILURE_MESSAGE(
            NtStatus::kNoMoreFiles == enumerateResult,
            L"NTSTATUS = 0x%08x while enumerating the contents of directory \"%.*s\".",
            static_cast<unsigned int>(enumerateResult),
            static_cast<int>(directoryAbsolutePath.length()),
            directoryAbsolutePath.data());
        break;
      }
    }

    if (false == unexpectedFiles.empty())
    {
      for (const auto& unexpectedFile : unexpectedFiles)
      {
        TEST_PRINT_MESSAGE(
            L"Unexpected file \"%s\" was enumerated in directory \"%.*s\".",
            unexpectedFile.c_str(),
            static_cast<int>(directoryAbsolutePath.length()),
            directoryAbsolutePath.data());
      }
      TEST_FAILED;
    }

    for (const auto& expectedFile : expectedFiles)
    {
      TEST_ASSERT_WITH_FAILURE_MESSAGE(
          actualFiles.contains(expectedFile),
          L"Directory \"%.*s\" is missing expected file \"%.*s\".",
          static_cast<int>(directoryAbsolutePath.length()),
          directoryAbsolutePath.data(),
          static_cast<int>(expectedFile.length()),
          expectedFile.data());
    }
  }

  std::unique_ptr<SIntegrationTestContext> CreateIntegrationTestContext(
      MockFilesystemOperations& mockFilesystem, std::wstring_view configurationFile)
  {
    mockFilesystem.SetConfigAllowOpenNonExistentFile(true);

    ConfigurationData configurationData =
        PathwinderConfigReader().ReadInMemoryConfigurationFile(configurationFile);
    FilesystemDirectorBuilder filesystemDirectorBuilder;
    auto maybeFilesystemDirector =
        filesystemDirectorBuilder.BuildFromConfigurationData(configurationData);
    TEST_ASSERT_WITH_FAILURE_MESSAGE(
        true == maybeFilesystemDirector.has_value(),
        "Failed to build a filesystem director object using the specified configuration file string.");

    return std::make_unique<SIntegrationTestContext>(
        mockFilesystem, std::move(*maybeFilesystemDirector));
  }

  void CloseHandleUsingFilesystemExecutor(TIntegrationTestContext& context, HANDLE handleToClose)
  {
    NTSTATUS closeHandleResult = FilesystemExecutor::CloseHandle(
        __FUNCTIONW__,
        kFunctionRequestIdentifier,
        context->openHandleStore,
        handleToClose,
        [&context](HANDLE handleToClose) -> NTSTATUS
        {
          return context->mockFilesystem.CloseHandle(handleToClose);
        });

    TEST_ASSERT_WITH_FAILURE_MESSAGE(
        NT_SUCCESS(closeHandleResult),
        "NTSTATUS = 0x%08x when attempting to close handle %zu.",
        static_cast<unsigned int>(closeHandleResult),
        reinterpret_cast<size_t>(handleToClose));
  }

  void CreateDirectoryUsingFilesystemExecutor(
      TIntegrationTestContext& context, std::wstring_view pathToCreate, HANDLE rootDirectory)
  {
    CreateUsingFilesystemExecutor(context, pathToCreate, rootDirectory, true);
  }

  void CreateFileUsingFilesystemExecutor(
      TIntegrationTestContext& context, std::wstring_view pathToCreate, HANDLE rootDirectory)
  {
    CreateUsingFilesystemExecutor(context, pathToCreate, rootDirectory, false);
  }

  HANDLE OpenUsingFilesystemExecutor(
      TIntegrationTestContext& context, std::wstring_view pathToOpen, HANDLE rootDirectory)
  {
    HANDLE newlyOpenedFileHandle = nullptr;

    UNICODE_STRING pathToOpenUnicodeString =
        Strings::NtConvertStringViewToUnicodeString(pathToOpen);
    OBJECT_ATTRIBUTES pathToOpenObjectAttributes = {
        .Length = sizeof(OBJECT_ATTRIBUTES),
        .RootDirectory = rootDirectory,
        .ObjectName = &pathToOpenUnicodeString};

    NTSTATUS newFileHandleResult = FilesystemExecutor::NewFileHandle(
        __FUNCTIONW__,
        kFunctionRequestIdentifier,
        context->openHandleStore,
        &newlyOpenedFileHandle,
        FILE_GENERIC_READ,
        &pathToOpenObjectAttributes,
        0,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT,
        [&context](
            std::wstring_view absolutePath,
            FileAccessMode fileAccessMode,
            CreateDisposition createDisposition) -> FileOperationInstruction
        {
          return context->filesystemDirector.GetInstructionForFileOperation(
              absolutePath, fileAccessMode, createDisposition);
        },
        [&context](PHANDLE fileHandle, POBJECT_ATTRIBUTES objectAttributes, ULONG createDisposition)
            -> NTSTATUS
        {
          Infra::TemporaryString absolutePathToOpen;

          if (nullptr != objectAttributes->RootDirectory)
            absolutePathToOpen << *context->mockFilesystem.GetPathFromHandle(
                                      objectAttributes->RootDirectory)
                               << L'\\';
          absolutePathToOpen << Strings::NtConvertUnicodeStringToStringView(
              *objectAttributes->ObjectName);

          HANDLE newlyOpenedFileHandle = context->mockFilesystem.Open(absolutePathToOpen);
          if (newlyOpenedFileHandle != nullptr)
          {
            *fileHandle = newlyOpenedFileHandle;
            return NtStatus::kSuccess;
          }

          return NtStatus::kObjectNameNotFound;
        });

    TEST_ASSERT_WITH_FAILURE_MESSAGE(
        NtStatus::kSuccess == newFileHandleResult,
        "NTSTATUS = 0x%08x when attempting to open file \"%.*s\".",
        static_cast<unsigned int>(newFileHandleResult),
        static_cast<int>(pathToOpen.length()),
        pathToOpen.data());
    return newlyOpenedFileHandle;
  }

  bool QueryExistsUsingFilesystemExecutor(
      TIntegrationTestContext& context, std::wstring_view pathToQuery, HANDLE rootDirectory)
  {
    UNICODE_STRING pathToQueryUnicodeString =
        Strings::NtConvertStringViewToUnicodeString(pathToQuery);
    OBJECT_ATTRIBUTES pathToQueryObjectAttributes = {
        .Length = sizeof(OBJECT_ATTRIBUTES),
        .RootDirectory = rootDirectory,
        .ObjectName = &pathToQueryUnicodeString};

    NTSTATUS queryResult = FilesystemExecutor::QueryByObjectAttributes(
        __FUNCTIONW__,
        kFunctionRequestIdentifier,
        context->openHandleStore,
        &pathToQueryObjectAttributes,
        FILE_GENERIC_READ,
        [&context](
            std::wstring_view absolutePath,
            FileAccessMode fileAccessMode,
            CreateDisposition createDisposition) -> FileOperationInstruction
        {
          return context->filesystemDirector.GetInstructionForFileOperation(
              absolutePath, fileAccessMode, createDisposition);
        },
        [&context](POBJECT_ATTRIBUTES objectAttributes) -> NTSTATUS
        {
          Infra::TemporaryString absolutePathToQuery;

          if (nullptr != objectAttributes->RootDirectory)
            absolutePathToQuery << *context->mockFilesystem.GetPathFromHandle(
                                       objectAttributes->RootDirectory)
                                << L'\\';
          absolutePathToQuery << Strings::NtConvertUnicodeStringToStringView(
              *objectAttributes->ObjectName);

          if (context->mockFilesystem.Exists(absolutePathToQuery))
            return NtStatus::kSuccess;
          else
            return NtStatus::kObjectNameNotFound;
        });

    return (NtStatus::kSuccess == queryResult);
  }

  void VerifyDirectoryAppearsToContain(
      TIntegrationTestContext& context,
      std::wstring_view directoryAbsolutePath,
      const TFileNameSet& expectedFiles)
  {
    VerifyFilesEnumeratedForDirectory(context, directoryAbsolutePath, expectedFiles);
    VerifyFilesAccessibleByAbsolutePath(context, directoryAbsolutePath, expectedFiles);
  }
} // namespace PathwinderTest
