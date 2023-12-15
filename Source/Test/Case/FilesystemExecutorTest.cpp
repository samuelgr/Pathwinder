/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file FilesystemExecutorTest.cpp
 *   Unit tests for all functionality related to executing application-requested filesystem
 *   operations under the control of filesystem instructions.
 **************************************************************************************************/

#include "TestCase.h"

#include "FilesystemExecutor.h"

#include <string>
#include <string_view>
#include <unordered_map>

#include "ApiWindows.h"
#include "FilesystemDirector.h"
#include "FilesystemInstruction.h"
#include "MockFilesystemOperations.h"
#include "OpenHandleStore.h"
#include "Strings.h"
#include "ValueOrError.h"

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  /// Function request identifier to be passed to all filesystem executor functions when they are
  /// invoked for testing.
  static unsigned int kFunctionRequestIdentifier = 0;

  /// Creates and returns an object attributes structure for the specified filename and optional
  /// root directory handle.
  /// @param [in] fileName System Unicode string representation of the filename to initialize inside
  /// the object attributes structure.
  /// @param [in] rootDirectory Handle for the root directory. Defaults to no root directory. If
  /// specified, the file name is relative to this open directory.
  /// @return Initialized object attributes data structure.
  static inline OBJECT_ATTRIBUTES CreateObjectAttributes(
      UNICODE_STRING& fileName, HANDLE rootDirectory = NULL)
  {
    return {
        .Length = sizeof(OBJECT_ATTRIBUTES),
        .RootDirectory = rootDirectory,
        .ObjectName = &fileName};
  }

  /// Determines if two `OBJECT_ATTRIBUTES` structures are effectively equal for the purpose of
  /// tests. This function examines length, root directory, object name, and attributes.
  /// @param [in] attributesA First structure to compare.
  /// @param [in] attributesB Second structure to compare.
  /// @return `true` if the contents of the two structures are effectively equal, `false` otherwise.
  static inline bool EqualObjectAttributes(
      const OBJECT_ATTRIBUTES& attributesA, const OBJECT_ATTRIBUTES& attributesB)
  {
    if (attributesA.Length != attributesB.Length) return false;
    if (attributesA.RootDirectory != attributesB.RootDirectory) return false;
    if (Strings::NtConvertUnicodeStringToStringView(*attributesA.ObjectName) !=
        Strings::NtConvertUnicodeStringToStringView(*attributesB.ObjectName))
      return false;
    if (attributesA.Attributes != attributesB.Attributes) return false;

    return true;
  }

  // Verifies file handle closure in the nominal situation of the handle being open and also located
  // in the open file handle store, meaning that Pathwinder has done some redirection on it. In this
  // situation the file handle closure should be intercepted and handled internally via the open
  // handle store, not passed through to the system.
  TEST_CASE(FilesystemExecutor_CloseHandle_Nominal)
  {
    constexpr std::wstring_view kDirectoryName = L"C:\\TestDirectory";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kDirectoryName);

    auto maybeDirectoryHandle = mockFilesystem.OpenDirectoryForEnumeration(kDirectoryName);
    TEST_ASSERT(true == maybeDirectoryHandle.HasValue());

    const HANDLE directoryHandle = maybeDirectoryHandle.Value();
    TEST_ASSERT(kDirectoryName == mockFilesystem.GetDirectoryPathFromHandle(directoryHandle));

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kDirectoryName), std::wstring(kDirectoryName));
    TEST_ASSERT(true == openHandleStore.GetDataForHandle(directoryHandle).has_value());

    NTSTATUS executorResult = FilesystemExecutor::CloseHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        [](HANDLE handleToClose) -> NTSTATUS
        {
          TEST_FAILED_BECAUSE(
              L"Pass-through system call should not be invoked if the handle is open and cached.");
        });

    TEST_ASSERT(NtStatus::kSuccess == executorResult);
    TEST_ASSERT(false == openHandleStore.GetDataForHandle(directoryHandle).has_value());
    TEST_ASSERT(false == mockFilesystem.GetDirectoryPathFromHandle(directoryHandle).has_value());
  }

  // Verifies file handle closure in the passthrough situation whereby a file handle is open with
  // the system but Pathwinder has not done any redirection. In this situation the file handle
  // closure request should be passed through to the system.
  TEST_CASE(FilesystemExecutor_CloseHandle_Passthrough)
  {
    constexpr std::wstring_view kDirectoryName = L"C:\\TestDirectory";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kDirectoryName);

    auto maybeDirectoryHandle = mockFilesystem.OpenDirectoryForEnumeration(kDirectoryName);
    TEST_ASSERT(true == maybeDirectoryHandle.HasValue());

    const HANDLE directoryHandle = maybeDirectoryHandle.Value();
    TEST_ASSERT(kDirectoryName == mockFilesystem.GetDirectoryPathFromHandle(directoryHandle));

    OpenHandleStore openHandleStore;
    TEST_ASSERT(false == openHandleStore.GetDataForHandle(directoryHandle).has_value());

    unsigned int numUnderlyingSystemCalls = 0;
    constexpr NTSTATUS expectedExecutorResult = 5500;
    NTSTATUS actualExecutorResult = FilesystemExecutor::CloseHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        [expectedExecutorResult, &numUnderlyingSystemCalls, &mockFilesystem](
            HANDLE handleToClose) -> NTSTATUS
        {
          mockFilesystem.CloseHandle(handleToClose);
          numUnderlyingSystemCalls += 1;
          return expectedExecutorResult;
        });

    TEST_ASSERT(1 == numUnderlyingSystemCalls);
    TEST_ASSERT(actualExecutorResult == expectedExecutorResult);
    TEST_ASSERT(false == openHandleStore.GetDataForHandle(directoryHandle).has_value());
    TEST_ASSERT(false == mockFilesystem.GetDirectoryPathFromHandle(directoryHandle).has_value());
  }

  // Verifies that requesting an instruction for creating a new file handle maps correctly from the
  // application-requested create disposition to an internal object representation of the same.
  TEST_CASE(FilesystemExecutor_NewFileHandle_CreateDispositionMapping)
  {
    constexpr std::wstring_view kFileName = L"C:\\TestDirectory\\TestFile.txt";
    UNICODE_STRING fileNameUnicodeString = Strings::NtConvertStringViewToUnicodeString(kFileName);
    OBJECT_ATTRIBUTES objectAttributes = CreateObjectAttributes(fileNameUnicodeString);

    const std::unordered_map<ULONG, CreateDisposition> createDispositionMappings = {
        {FILE_CREATE, CreateDisposition::CreateNewFile()},
        {FILE_SUPERSEDE, CreateDisposition::CreateNewOrOpenExistingFile()},
        {FILE_OPEN_IF, CreateDisposition::CreateNewOrOpenExistingFile()},
        {FILE_OVERWRITE_IF, CreateDisposition::CreateNewOrOpenExistingFile()},
        {FILE_OPEN, CreateDisposition::OpenExistingFile()},
        {FILE_OVERWRITE, CreateDisposition::OpenExistingFile()}};

    for (const auto& createDispositionMapping : createDispositionMappings)
    {
      const ULONG testInputCreateDisposition = createDispositionMapping.first;
      const CreateDisposition expectedCreateDisposition = createDispositionMapping.second;

      MockFilesystemOperations mockFilesystem;
      OpenHandleStore openHandleStore;

      FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          nullptr,
          0,
          &objectAttributes,
          0,
          testInputCreateDisposition,
          0,
          [expectedCreateDisposition](
              std::wstring_view,
              FileAccessMode,
              CreateDisposition actualCreateDisposition) -> FileOperationInstruction
          {
            TEST_ASSERT(actualCreateDisposition == expectedCreateDisposition);
            return FileOperationInstruction::NoRedirectionOrInterception();
          },
          [](PHANDLE, POBJECT_ATTRIBUTES, ULONG) -> NTSTATUS
          {
            return NtStatus::kSuccess;
          });
    }
  }

  // Verifies that requesting an instruction for creating a new file handle maps correctly from the
  // application-requested file access mode to an internal object representation of the same.
  TEST_CASE(FilesystemExecutor_NewFileHandle_FileAccessModeMapping)
  {
    constexpr std::wstring_view kFileName = L"C:\\TestDirectory\\TestFile.txt";
    UNICODE_STRING fileNameUnicodeString = Strings::NtConvertStringViewToUnicodeString(kFileName);
    OBJECT_ATTRIBUTES objectAttributes = CreateObjectAttributes(fileNameUnicodeString);

    const std::unordered_map<ACCESS_MASK, FileAccessMode> fileAccessModeMappings = {
        {GENERIC_READ, FileAccessMode::ReadOnly()},
        {FILE_READ_DATA | FILE_READ_ATTRIBUTES, FileAccessMode::ReadOnly()},
        {FILE_EXECUTE, FileAccessMode::ReadOnly()},
        {FILE_LIST_DIRECTORY | FILE_TRAVERSE, FileAccessMode::ReadOnly()},
        {GENERIC_WRITE, FileAccessMode::WriteOnly()},
        {FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES, FileAccessMode::WriteOnly()},
        {FILE_APPEND_DATA, FileAccessMode::WriteOnly()},
        {WRITE_OWNER, FileAccessMode::WriteOnly()},
        {GENERIC_READ | GENERIC_WRITE, FileAccessMode::ReadWrite()},
        {FILE_READ_DATA | FILE_WRITE_DATA, FileAccessMode::ReadWrite()},
        {FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, FileAccessMode::ReadWrite()},
        {DELETE, FileAccessMode::Delete()}};

    for (const auto& fileAccessModeMapping : fileAccessModeMappings)
    {
      const ULONG testInputFileAccessMode = fileAccessModeMapping.first;
      const FileAccessMode expectedFileAccessMode = fileAccessModeMapping.second;

      MockFilesystemOperations mockFilesystem;
      OpenHandleStore openHandleStore;

      FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          nullptr,
          testInputFileAccessMode,
          &objectAttributes,
          0,
          0,
          0,
          [expectedFileAccessMode](
              std::wstring_view,
              FileAccessMode actualFileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            TEST_ASSERT(actualFileAccessMode == expectedFileAccessMode);
            return FileOperationInstruction::NoRedirectionOrInterception();
          },
          [](PHANDLE, POBJECT_ATTRIBUTES, ULONG) -> NTSTATUS
          {
            return NtStatus::kSuccess;
          });
    }
  }

  // Verifies that requests for new file handles are passed through to the system without
  // modification or interception if the filesystem instruction says not to redirect or intercept.
  // This test case exercises the nominal situation in which no root directory handle is specified.
  // Various valid forms of file operation instructions are exercised, even those that are not
  // actually ever produced by a filesystem director.
  TEST_CASE(FilesystemExecutor_NewFileHandle_NoRedirectionOrInterception_Nominal)
  {
    constexpr std::wstring_view kAbsolutePath = L"C:\\TestDirectory\\TestFile.txt";

    // The fundamental parts of a "no-redirect-or-intercept" instruction is that only the
    // unredirected file is tried and that no association is created between the name and the
    // handle. No pre-operations are allowed, so the operand should be ignored.
    const FileOperationInstruction fileOperationInstructionsToTry[] = {
        FileOperationInstruction::NoRedirectionOrInterception(),
        FileOperationInstruction(
            L"C:\\Redirected\\Filename\\IsPresent\\ButShouldBeIgnored.txt",
            ETryFiles::UnredirectedOnly,
            ECreateDispositionPreference::NoPreference,
            EAssociateNameWithHandle::None,
            {},
            L"ExtraPreOperationOperandShouldBeIgnored")};

    UNICODE_STRING unicodeStringAbsolutePath =
        Strings::NtConvertStringViewToUnicodeString(kAbsolutePath);
    OBJECT_ATTRIBUTES objectAttributesAbsolutePath =
        CreateObjectAttributes(unicodeStringAbsolutePath);

    for (const auto& fileOperationInstructionToTry : fileOperationInstructionsToTry)
    {
      const HANDLE expectedHandleValue = reinterpret_cast<HANDLE>(2);
      HANDLE actualHandleValue = NULL;

      MockFilesystemOperations mockFilesystem;
      OpenHandleStore openHandleStore;

      bool instructionSourceWasInvoked = false;

      constexpr NTSTATUS expectedReturnCode = 0x00000004;
      NTSTATUS actualReturnCode = FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &actualHandleValue,
          0,
          &objectAttributesAbsolutePath,
          0,
          0,
          0,
          [kAbsolutePath, &fileOperationInstructionToTry, &instructionSourceWasInvoked](
              std::wstring_view actualAbsolutePath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            std::wstring_view expectedAbsolutePath = kAbsolutePath;
            TEST_ASSERT(actualAbsolutePath == expectedAbsolutePath);

            instructionSourceWasInvoked = true;
            return fileOperationInstructionToTry;
          },
          [expectedHandleValue, expectedReturnCode, &objectAttributesAbsolutePath](
              PHANDLE handle, POBJECT_ATTRIBUTES objectAttributes, ULONG) -> NTSTATUS
          {
            const OBJECT_ATTRIBUTES& expectedObjectAttributes = objectAttributesAbsolutePath;
            const OBJECT_ATTRIBUTES& actualObjectAttributes = *objectAttributes;
            TEST_ASSERT(EqualObjectAttributes(actualObjectAttributes, expectedObjectAttributes));

            *handle = expectedHandleValue;
            return expectedReturnCode;
          });

      TEST_ASSERT(true == instructionSourceWasInvoked);
      TEST_ASSERT(true == openHandleStore.Empty());
      TEST_ASSERT(actualReturnCode == expectedReturnCode);
      TEST_ASSERT(actualHandleValue == expectedHandleValue);
    }
  } // namespace PathwinderTest

  // Verifies that requests for new file handles are passed through to the system without
  // modification or interception if the filesystem instruction says not to redirect or intercept.
  // This test case exercises the situation in which a root directory handle is specified and
  // present in the open handle store cache. The root directory was previously intercepted by
  // another file operation, so the executor should request an instruction using the full, combined,
  // absolute path. Since the result is "no redirection" the request should then be forwarded
  // unmodified to the system. Various valid forms of file operation instructions are exercised,
  // even those that are not actually ever produced by a filesystem director.
  TEST_CASE(FilesystemExecutor_NewFileHandle_NoRedirectionOrInterception_CachedRootDirectory)
  {
    constexpr std::wstring_view kAbsolutePath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kDirectoryName =
        kAbsolutePath.substr(0, kAbsolutePath.find_last_of(L'\\'));
    constexpr std::wstring_view kFileName =
        kAbsolutePath.substr(1 + kAbsolutePath.find_last_of(L'\\'));

    // The fundamental parts of a "no-redirect-or-intercept" instruction is that only the
    // unredirected file is tried and that no association is created between the name and the
    // handle. No pre-operations are allowed, so the operand should be ignored.
    const FileOperationInstruction fileOperationInstructionsToTry[] = {
        FileOperationInstruction::NoRedirectionOrInterception(),
        FileOperationInstruction(
            L"C:\\Redirected\\Filename\\IsPresent\\ButShouldBeIgnored.txt",
            ETryFiles::UnredirectedOnly,
            ECreateDispositionPreference::NoPreference,
            EAssociateNameWithHandle::None,
            {},
            L"ExtraPreOperationOperandShouldBeIgnored")};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFile(kAbsolutePath);

    auto maybeDirectoryHandle = mockFilesystem.OpenDirectoryForEnumeration(kDirectoryName);
    TEST_ASSERT(true == maybeDirectoryHandle.HasValue());

    const HANDLE rootDirectoryHandle = maybeDirectoryHandle.Value();
    TEST_ASSERT(kDirectoryName == mockFilesystem.GetDirectoryPathFromHandle(rootDirectoryHandle));

    UNICODE_STRING unicodeStringRelativePath =
        Strings::NtConvertStringViewToUnicodeString(kFileName);
    OBJECT_ATTRIBUTES objectAttributesRelativePath =
        CreateObjectAttributes(unicodeStringRelativePath, rootDirectoryHandle);

    for (const auto& fileOperationInstructionToTry : fileOperationInstructionsToTry)
    {
      const HANDLE expectedHandleValue = reinterpret_cast<HANDLE>(2);
      HANDLE actualHandleValue = NULL;

      OpenHandleStore openHandleStore;
      openHandleStore.InsertHandle(
          rootDirectoryHandle, std::wstring(kDirectoryName), std::wstring(kDirectoryName));

      bool instructionSourceWasInvoked = false;

      constexpr NTSTATUS expectedReturnCode = 0x00000006;
      NTSTATUS actualReturnCode = FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &actualHandleValue,
          0,
          &objectAttributesRelativePath,
          0,
          0,
          0,
          [kAbsolutePath, &instructionSourceWasInvoked](
              std::wstring_view actualAbsolutePath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            std::wstring_view expectedAbsolutePath = kAbsolutePath;
            TEST_ASSERT(actualAbsolutePath == expectedAbsolutePath);

            instructionSourceWasInvoked = true;
            return FileOperationInstruction::NoRedirectionOrInterception();
          },
          [expectedHandleValue, expectedReturnCode, &objectAttributesRelativePath](
              PHANDLE handle, POBJECT_ATTRIBUTES objectAttributes, ULONG) -> NTSTATUS
          {
            const OBJECT_ATTRIBUTES& expectedObjectAttributes = objectAttributesRelativePath;
            const OBJECT_ATTRIBUTES& actualObjectAttributes = *objectAttributes;
            TEST_ASSERT(EqualObjectAttributes(actualObjectAttributes, expectedObjectAttributes));

            *handle = expectedHandleValue;
            return expectedReturnCode;
          });

      TEST_ASSERT(true == instructionSourceWasInvoked);
      TEST_ASSERT(false == openHandleStore.Empty());
      TEST_ASSERT(actualReturnCode == expectedReturnCode);
      TEST_ASSERT(actualHandleValue == expectedHandleValue);
    }
  }

  // Verifies that requests for new file handles are passed through to the system without
  // modification, but that the new handle is added to cache, if the filesystem instruction says to
  // intercept without redirection. This test case exercises the nominal situation in which no root
  // directory handle is specified and no pre-operations are requested. Various valid forms of file
  // operation instructions are exercised, even those that are not actually ever produced by a
  // filesystem director.
  TEST_CASE(FilesystemExecutor_NewFileHandle_InterceptWithoutRedirection_Nominal)
  {
    constexpr std::wstring_view kAbsolutePath = L"C:\\TestDirectory\\TestFile.txt";

    // The fundamental parts of a "intercept-without-redirection" instruction is that only the
    // unredirected file is tried and that an association is created between the unredirected
    // filename and the new file handle. When pre-operations are not requested the operand should be
    // ignored.
    const FileOperationInstruction fileOperationInstructionsToTry[] = {
        FileOperationInstruction::InterceptWithoutRedirection(
            EAssociateNameWithHandle::Unredirected),
        FileOperationInstruction(
            L"C:\\Redirected\\Filename\\IsPresent\\ButShouldBeIgnored.txt",
            ETryFiles::UnredirectedOnly,
            ECreateDispositionPreference::NoPreference,
            EAssociateNameWithHandle::Unredirected,
            {},
            L"ExtraPreOperationOperandShouldBeIgnored")};

    UNICODE_STRING unicodeStringAbsolutePath =
        Strings::NtConvertStringViewToUnicodeString(kAbsolutePath);
    OBJECT_ATTRIBUTES objectAttributesAbsolutePath =
        CreateObjectAttributes(unicodeStringAbsolutePath);

    for (const auto& fileOperationInstructionToTry : fileOperationInstructionsToTry)
    {
      const HANDLE expectedHandleValue = reinterpret_cast<HANDLE>(3);
      HANDLE actualHandleValue = NULL;

      MockFilesystemOperations mockFilesystem;
      OpenHandleStore openHandleStore;

      bool instructionSourceWasInvoked = false;

      constexpr NTSTATUS expectedReturnCode = 0x0000000a;
      NTSTATUS actualReturnCode = FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &actualHandleValue,
          0,
          &objectAttributesAbsolutePath,
          0,
          0,
          0,
          [kAbsolutePath, &fileOperationInstructionToTry, &instructionSourceWasInvoked](
              std::wstring_view actualAbsolutePath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            std::wstring_view expectedAbsolutePath = kAbsolutePath;
            TEST_ASSERT(actualAbsolutePath == expectedAbsolutePath);

            instructionSourceWasInvoked = true;
            return fileOperationInstructionToTry;
          },
          [expectedHandleValue, expectedReturnCode, &objectAttributesAbsolutePath](
              PHANDLE handle, POBJECT_ATTRIBUTES objectAttributes, ULONG) -> NTSTATUS
          {
            const OBJECT_ATTRIBUTES& expectedObjectAttributes = objectAttributesAbsolutePath;
            const OBJECT_ATTRIBUTES& actualObjectAttributes = *objectAttributes;
            TEST_ASSERT(EqualObjectAttributes(actualObjectAttributes, expectedObjectAttributes));

            *handle = expectedHandleValue;
            return expectedReturnCode;
          });

      constexpr std::optional<OpenHandleStore::SHandleDataView> expectedHandleData =
          OpenHandleStore::SHandleDataView{
              .associatedPath = kAbsolutePath, .realOpenedPath = kAbsolutePath};

      std::optional<OpenHandleStore::SHandleDataView> actualHandleData =
          openHandleStore.GetDataForHandle(expectedHandleValue);

      TEST_ASSERT(1 == openHandleStore.Size());
      TEST_ASSERT(actualHandleData == expectedHandleData);

      TEST_ASSERT(true == instructionSourceWasInvoked);
      TEST_ASSERT(actualReturnCode == expectedReturnCode);
      TEST_ASSERT(actualHandleValue == expectedHandleValue);
    }
  }

  // Verifies that requests for new file handles are passed through to the system without
  // modification, but that the new handle is added to cache, if the filesystem instruction says to
  // intercept without redirection. This test case exercises the situation in which a root
  // directory handle is specified, which already exists in the open handle store, and no
  // pre-operations are requested. Various valid forms of file operation instructions are exercised,
  // even those that are not actually ever produced by a filesystem director.
  TEST_CASE(FilesystemExecutor_NewFileHandle_InterceptWithoutRedirection_CachedRootDirectory)
  {
    constexpr std::wstring_view kAbsolutePath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kDirectoryName =
        kAbsolutePath.substr(0, kAbsolutePath.find_last_of(L'\\'));
    constexpr std::wstring_view kFileName =
        kAbsolutePath.substr(1 + kAbsolutePath.find_last_of(L'\\'));

    // The fundamental parts of a "intercept-without-redirection" instruction is that only the
    // unredirected file is tried and that an association is created between the unredirected
    // filename and the new file handle. When pre-operations are not requested the operand should be
    // ignored.
    const FileOperationInstruction fileOperationInstructionsToTry[] = {
        FileOperationInstruction::InterceptWithoutRedirection(
            EAssociateNameWithHandle::Unredirected),
        FileOperationInstruction(
            L"C:\\Redirected\\Filename\\IsPresent\\ButShouldBeIgnored.txt",
            ETryFiles::UnredirectedOnly,
            ECreateDispositionPreference::NoPreference,
            EAssociateNameWithHandle::Unredirected,
            {},
            L"ExtraPreOperationOperandShouldBeIgnored")};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFile(kAbsolutePath);

    auto maybeDirectoryHandle = mockFilesystem.OpenDirectoryForEnumeration(kDirectoryName);
    TEST_ASSERT(true == maybeDirectoryHandle.HasValue());

    const HANDLE rootDirectoryHandle = maybeDirectoryHandle.Value();
    TEST_ASSERT(kDirectoryName == mockFilesystem.GetDirectoryPathFromHandle(rootDirectoryHandle));

    UNICODE_STRING unicodeStringRelativePath =
        Strings::NtConvertStringViewToUnicodeString(kFileName);
    OBJECT_ATTRIBUTES objectAttributesRelativePath =
        CreateObjectAttributes(unicodeStringRelativePath, rootDirectoryHandle);

    for (const auto& fileOperationInstructionToTry : fileOperationInstructionsToTry)
    {
      const HANDLE expectedHandleValue = reinterpret_cast<HANDLE>(3);
      HANDLE actualHandleValue = NULL;

      OpenHandleStore openHandleStore;
      openHandleStore.InsertHandle(
          rootDirectoryHandle, std::wstring(kDirectoryName), std::wstring(kDirectoryName));

      bool instructionSourceWasInvoked = false;

      constexpr NTSTATUS expectedReturnCode = 0x0000000a;
      NTSTATUS actualReturnCode = FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &actualHandleValue,
          0,
          &objectAttributesRelativePath,
          0,
          0,
          0,
          [kAbsolutePath, &fileOperationInstructionToTry, &instructionSourceWasInvoked](
              std::wstring_view actualAbsolutePath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            std::wstring_view expectedAbsolutePath = kAbsolutePath;
            TEST_ASSERT(actualAbsolutePath == expectedAbsolutePath);

            instructionSourceWasInvoked = true;
            return fileOperationInstructionToTry;
          },
          [expectedHandleValue, expectedReturnCode, &objectAttributesRelativePath](
              PHANDLE handle, POBJECT_ATTRIBUTES objectAttributes, ULONG) -> NTSTATUS
          {
            const OBJECT_ATTRIBUTES& expectedObjectAttributes = objectAttributesRelativePath;
            const OBJECT_ATTRIBUTES& actualObjectAttributes = *objectAttributes;
            TEST_ASSERT(EqualObjectAttributes(actualObjectAttributes, expectedObjectAttributes));

            *handle = expectedHandleValue;
            return expectedReturnCode;
          });

      constexpr std::optional<OpenHandleStore::SHandleDataView> expectedHandleData =
          OpenHandleStore::SHandleDataView{
              .associatedPath = kAbsolutePath, .realOpenedPath = kAbsolutePath};

      std::optional<OpenHandleStore::SHandleDataView> actualHandleData =
          openHandleStore.GetDataForHandle(expectedHandleValue);

      TEST_ASSERT(2 == openHandleStore.Size());
      TEST_ASSERT(actualHandleData == expectedHandleData);

      TEST_ASSERT(true == instructionSourceWasInvoked);
      TEST_ASSERT(actualReturnCode == expectedReturnCode);
      TEST_ASSERT(actualHandleValue == expectedHandleValue);
    }
  }

  // Verifies that requests for new file handles are passed through to the system without
  // modification, and not added to cache, if the filesystem instruction says to intercept with a
  // pre-operation but not add any file handle association. No root directory handle is specified,
  // as this test is just designed to exercise pre-operation execution. The forms of instructions
  // exercised by this test are not generally produced by filesystem director objects.
  TEST_CASE(FilesystemExecutor_NewFileHandle_InterceptWithoutRedirection_PreOperationOnly)
  {
    constexpr std::wstring_view kAbsolutePath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kExtraPreOperationHierarchyToCreate =
        L"C:\\ExtraPreOperation\\Directory\\Hierarchy\\To\\Create";

    // This test case only exercises pre-operations, so no association should be created and hence
    // nothing should be added to the open handle store. The important parts here are the extra
    // pre-operation to ensure a directory hierarchy exists and the operand to that pre-operation.
    const FileOperationInstruction fileOperationInstructionsToTry[] = {
        FileOperationInstruction::InterceptWithoutRedirection(
            EAssociateNameWithHandle::None,
            {static_cast<int>(EExtraPreOperation::EnsurePathHierarchyExists)},
            kExtraPreOperationHierarchyToCreate),
        FileOperationInstruction(
            L"C:\\Redirected\\Filename\\IsPresent\\ButShouldBeIgnored.txt",
            ETryFiles::UnredirectedOnly,
            ECreateDispositionPreference::NoPreference,
            EAssociateNameWithHandle::None,
            {static_cast<int>(EExtraPreOperation::EnsurePathHierarchyExists)},
            kExtraPreOperationHierarchyToCreate)};

    UNICODE_STRING unicodeStringAbsolutePath =
        Strings::NtConvertStringViewToUnicodeString(kAbsolutePath);
    OBJECT_ATTRIBUTES objectAttributesAbsolutePath =
        CreateObjectAttributes(unicodeStringAbsolutePath);

    for (const auto& fileOperationInstructionToTry : fileOperationInstructionsToTry)
    {
      const HANDLE expectedHandleValue = reinterpret_cast<HANDLE>(11);
      HANDLE actualHandleValue = NULL;

      MockFilesystemOperations mockFilesystem;
      OpenHandleStore openHandleStore;

      bool instructionSourceWasInvoked = false;
      TEST_ASSERT(false == mockFilesystem.IsDirectory(kExtraPreOperationHierarchyToCreate));

      constexpr NTSTATUS expectedReturnCode = 0x0000000c;
      NTSTATUS actualReturnCode = FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &actualHandleValue,
          0,
          &objectAttributesAbsolutePath,
          0,
          0,
          0,
          [kAbsolutePath, &fileOperationInstructionToTry, &instructionSourceWasInvoked](
              std::wstring_view actualAbsolutePath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            std::wstring_view expectedAbsolutePath = kAbsolutePath;
            TEST_ASSERT(actualAbsolutePath == expectedAbsolutePath);

            instructionSourceWasInvoked = true;
            return fileOperationInstructionToTry;
          },
          [expectedHandleValue, expectedReturnCode, &objectAttributesAbsolutePath](
              PHANDLE handle, POBJECT_ATTRIBUTES objectAttributes, ULONG) -> NTSTATUS
          {
            const OBJECT_ATTRIBUTES& expectedObjectAttributes = objectAttributesAbsolutePath;
            const OBJECT_ATTRIBUTES& actualObjectAttributes = *objectAttributes;
            TEST_ASSERT(EqualObjectAttributes(actualObjectAttributes, expectedObjectAttributes));

            *handle = expectedHandleValue;
            return expectedReturnCode;
          });

      TEST_ASSERT(true == instructionSourceWasInvoked);
      TEST_ASSERT(true == openHandleStore.Empty());
      TEST_ASSERT(true == mockFilesystem.IsDirectory(kExtraPreOperationHierarchyToCreate));
      TEST_ASSERT(actualReturnCode == expectedReturnCode);
      TEST_ASSERT(actualHandleValue == expectedHandleValue);
    }
  }

  // Verifies that requests for new file handles are passed through to the system without
  // modification or interception if the root directory handle is specified but not cached. In
  // this situation, the root directory would have been declared "uninteresting" by the filesystem
  // director, so the executor should just assume it is still uninteresting and not even ask for a
  // redirection instruction. Request should be passed through unmodified to the system. Various
  // valid forms of file operation instructions are exercised, even those that are not actually
  // ever produced by a filesystem director.
  TEST_CASE(FilesystemExecutor_NewFileHandle_WithoutInstruction_UncachedRootDirectory)
  {
    constexpr std::wstring_view kAbsolutePath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kDirectoryName =
        kAbsolutePath.substr(0, kAbsolutePath.find_last_of(L'\\'));
    constexpr std::wstring_view kFileName =
        kAbsolutePath.substr(1 + kAbsolutePath.find_last_of(L'\\'));

    UNICODE_STRING unicodeStringRelativePath =
        Strings::NtConvertStringViewToUnicodeString(kFileName);
    OBJECT_ATTRIBUTES objectAttributesRelativePath =
        CreateObjectAttributes(unicodeStringRelativePath, reinterpret_cast<HANDLE>(99));

    const HANDLE expectedHandleValue = reinterpret_cast<HANDLE>(2);
    HANDLE actualHandleValue = NULL;

    MockFilesystemOperations mockFilesystem;
    OpenHandleStore openHandleStore;

    constexpr NTSTATUS expectedReturnCode = 0x00000005;
    NTSTATUS actualReturnCode = FilesystemExecutor::NewFileHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        &actualHandleValue,
        0,
        &objectAttributesRelativePath,
        0,
        0,
        0,
        [kAbsolutePath](std::wstring_view actualAbsolutePath, FileAccessMode, CreateDisposition)
            -> FileOperationInstruction
        {
          TEST_FAILED_BECAUSE(
              "Instruction source should not be invoked if the root directory handle is present but uncached.");
        },
        [expectedHandleValue, expectedReturnCode, &objectAttributesRelativePath](
            PHANDLE handle, POBJECT_ATTRIBUTES objectAttributes, ULONG) -> NTSTATUS
        {
          const OBJECT_ATTRIBUTES& expectedObjectAttributes = objectAttributesRelativePath;
          const OBJECT_ATTRIBUTES& actualObjectAttributes = *objectAttributes;
          TEST_ASSERT(EqualObjectAttributes(actualObjectAttributes, expectedObjectAttributes));

          *handle = expectedHandleValue;
          return expectedReturnCode;
        });

    TEST_ASSERT(actualReturnCode == expectedReturnCode);
    TEST_ASSERT(actualHandleValue == expectedHandleValue);
    TEST_ASSERT(true == openHandleStore.Empty());
  }
} // namespace PathwinderTest
