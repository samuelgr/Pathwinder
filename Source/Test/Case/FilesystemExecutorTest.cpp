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
#include "ArrayList.h"
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
    TEST_ASSERT(kDirectoryName == mockFilesystem.GetPathFromHandle(directoryHandle));

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
    TEST_ASSERT(false == mockFilesystem.GetPathFromHandle(directoryHandle).has_value());
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
    TEST_ASSERT(kDirectoryName == mockFilesystem.GetPathFromHandle(directoryHandle));

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
    TEST_ASSERT(false == mockFilesystem.GetPathFromHandle(directoryHandle).has_value());
  }

  // Verifies that whatever new handle value is written by the underlying system call is made
  // visible to the caller via its pointer parameter.
  TEST_CASE(FilesystemExecutor_NewFileHandle_PropagateNewHandleValue)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kRedirectedPath = L"C:\\RedirectedDirectory\\TestFile.txt";

    UNICODE_STRING unicodeStringUnredirectedPath =
        Strings::NtConvertStringViewToUnicodeString(kUnredirectedPath);
    OBJECT_ATTRIBUTES objectAttributesUnredirectedPath =
        CreateObjectAttributes(unicodeStringUnredirectedPath);

    const FileOperationInstruction fileOperationInstructionsToTry[] = {
        FileOperationInstruction::NoRedirectionOrInterception(),
        FileOperationInstruction::InterceptWithoutRedirection(),
        FileOperationInstruction::SimpleRedirectTo(kRedirectedPath),
        FileOperationInstruction::OverlayRedirectTo(kRedirectedPath),
    };

    const HANDLE handleValuesToTry[] = {
        reinterpret_cast<HANDLE>(0),
        reinterpret_cast<HANDLE>(103),
        reinterpret_cast<HANDLE>(204),
        reinterpret_cast<HANDLE>(3050),
        reinterpret_cast<HANDLE>(40600),
    };

    for (const auto& fileOperationInstructionToTry : fileOperationInstructionsToTry)
    {
      for (HANDLE handleValueToTry : handleValuesToTry)
      {
        const HANDLE expectedHandleValue = handleValueToTry;
        HANDLE actualHandleValue = NULL;

        OpenHandleStore openHandleStore;

        FilesystemExecutor::NewFileHandle(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            &actualHandleValue,
            0,
            &objectAttributesUnredirectedPath,
            0,
            0,
            0,
            [&fileOperationInstructionToTry](
                std::wstring_view, FileAccessMode, CreateDisposition) -> FileOperationInstruction
            {
              return fileOperationInstructionToTry;
            },
            [handleValueToTry](PHANDLE handle, POBJECT_ATTRIBUTES, ULONG) -> NTSTATUS
            {
              *handle = handleValueToTry;
              return NtStatus::kSuccess;
            });

        TEST_ASSERT(actualHandleValue == expectedHandleValue);
      }
    }
  }

  // Verifies that the underlying system call return code is propagated to the caller as the result
  // of the executor operation when a new file handle is requested.
  TEST_CASE(FilesystemExecutor_NewFileHandle_PropagateReturnCode)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kRedirectedPath = L"C:\\RedirectedDirectory\\TestFile.txt";

    UNICODE_STRING unicodeStringUnredirectedPath =
        Strings::NtConvertStringViewToUnicodeString(kUnredirectedPath);
    OBJECT_ATTRIBUTES objectAttributesUnredirectedPath =
        CreateObjectAttributes(unicodeStringUnredirectedPath);

    const FileOperationInstruction fileOperationInstructionsToTry[] = {
        FileOperationInstruction::NoRedirectionOrInterception(),
        FileOperationInstruction::InterceptWithoutRedirection(),
        FileOperationInstruction::SimpleRedirectTo(kRedirectedPath),
        FileOperationInstruction::OverlayRedirectTo(kRedirectedPath),
    };

    const NTSTATUS returnCodesToTry[] = {
        NtStatus::kSuccess,
        NtStatus::kBufferOverflow,
        NtStatus::kInvalidInfoClass,
        NtStatus::kInvalidParameter,
        NtStatus::kNoSuchFile,
        NtStatus::kObjectNameInvalid,
        NtStatus::kObjectNameNotFound,
        NtStatus::kObjectPathInvalid,
        NtStatus::kObjectPathNotFound,
        NtStatus::kInternalError,
    };

    for (const auto& fileOperationInstructionToTry : fileOperationInstructionsToTry)
    {
      for (NTSTATUS returnCodeToTry : returnCodesToTry)
      {
        HANDLE unusedHandleValue = NULL;

        OpenHandleStore openHandleStore;

        const NTSTATUS expectedReturnCode = returnCodeToTry;
        const NTSTATUS actualReturnCode = FilesystemExecutor::NewFileHandle(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            &unusedHandleValue,
            0,
            &objectAttributesUnredirectedPath,
            0,
            0,
            0,
            [&fileOperationInstructionToTry](
                std::wstring_view, FileAccessMode, CreateDisposition) -> FileOperationInstruction
            {
              return fileOperationInstructionToTry;
            },
            [expectedReturnCode](PHANDLE, POBJECT_ATTRIBUTES, ULONG) -> NTSTATUS
            {
              return expectedReturnCode;
            });

        TEST_ASSERT(actualReturnCode == expectedReturnCode);
      }
    }
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

  // Verifies that the filesystem executor correctly composes a complete path when requesting a file
  // operation instruction as part of the creation of a new file handle. If no root directory is
  // specified then the requested path is the same as the input path. If the root directory is
  // specified by handle and the handle is cached in the open handle store then the requested path
  // is the root directory path concatenated with the input path. Note that an uncached (but
  // present) root directory is handled by a different test case entirely, as this situation should
  // result in passthrough behavior.
  TEST_CASE(FilesystemExecutor_NewFileHandle_InstructionSourcePathComposition)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kDirectoryName =
        kUnredirectedPath.substr(0, kUnredirectedPath.find_last_of(L'\\'));
    constexpr std::wstring_view kFileName =
        kUnredirectedPath.substr(1 + kUnredirectedPath.find_last_of(L'\\'));

    const HANDLE kRootDirectoryHandleValueTestInput = reinterpret_cast<HANDLE>(2049);

    const struct
    {
      std::optional<std::wstring_view> rootDirectoryName;
      std::wstring_view fileName;
    } instructionSourcePathCompositionTestRecords[] = {
        {.rootDirectoryName = std::nullopt, .fileName = kUnredirectedPath},
        {.rootDirectoryName = kDirectoryName, .fileName = kFileName},
    };

    for (const auto& instructionSourcePathCompositionTestRecord :
         instructionSourcePathCompositionTestRecords)
    {
      UNICODE_STRING unicodeStringFileName = Strings::NtConvertStringViewToUnicodeString(
          instructionSourcePathCompositionTestRecord.fileName);

      OpenHandleStore openHandleStore;

      HANDLE rootDirectoryHandle = NULL;

      if (instructionSourcePathCompositionTestRecord.rootDirectoryName.has_value())
      {
        rootDirectoryHandle = kRootDirectoryHandleValueTestInput;
        openHandleStore.InsertHandle(
            rootDirectoryHandle,
            std::wstring(*instructionSourcePathCompositionTestRecord.rootDirectoryName),
            std::wstring(*instructionSourcePathCompositionTestRecord.rootDirectoryName));
      }

      OBJECT_ATTRIBUTES objectAttributes =
          CreateObjectAttributes(unicodeStringFileName, rootDirectoryHandle);

      FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          nullptr,
          0,
          &objectAttributes,
          0,
          0,
          0,
          [kUnredirectedPath](
              std::wstring_view actualRequestedPath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            std::wstring_view expectedRequestedPath = kUnredirectedPath;
            TEST_ASSERT(actualRequestedPath == expectedRequestedPath);
            return FileOperationInstruction::NoRedirectionOrInterception();
          },
          [](PHANDLE, POBJECT_ATTRIBUTES, ULONG) -> NTSTATUS
          {
            return NtStatus::kSuccess;
          });
    }
  }

  // Verifies that any file attempt preference is honored if it is contained in a file operation
  // instruction when a new file handle is being created. The instructions used in this test case
  // all contain an unredirected and a redirected path, and they supply various enumerators
  // indicating the order in which the files should be tried.
  TEST_CASE(FilesystemExecutor_NewFileHandle_TryFilesOrder)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kRedirectedPath = L"C:\\RedirectedDirectory\\TestFile.txt";

    // Holds paths in the order that they are expected to be tried in invocations of the underlying
    // system call.
    using TExpectedPaths = ArrayList<std::wstring_view, 2>;

    const struct
    {
      ETryFiles tryFilesTestInput;
      TExpectedPaths expectedOrderedPaths;
    } tryFilesTestRecords[] = {
        {.tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .expectedOrderedPaths = {kUnredirectedPath}},
        {.tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .expectedOrderedPaths = {kUnredirectedPath, kRedirectedPath}},
        {.tryFilesTestInput = ETryFiles::RedirectedOnly, .expectedOrderedPaths = {kRedirectedPath}},
        {.tryFilesTestInput = ETryFiles::RedirectedFirst,
         .expectedOrderedPaths = {kRedirectedPath, kUnredirectedPath}},
    };

    UNICODE_STRING unicodeStringUnredirectedPath =
        Strings::NtConvertStringViewToUnicodeString(kUnredirectedPath);
    OBJECT_ATTRIBUTES objectAttributesUnredirectedPath =
        CreateObjectAttributes(unicodeStringUnredirectedPath);

    for (const auto& tryFilesTestRecord : tryFilesTestRecords)
    {
      HANDLE unusedHandleValue = NULL;

      MockFilesystemOperations mockFilesystem;
      OpenHandleStore openHandleStore;

      const FileOperationInstruction testInputFileOperationInstruction(
          kRedirectedPath,
          tryFilesTestRecord.tryFilesTestInput,
          ECreateDispositionPreference::NoPreference,
          EAssociateNameWithHandle::None,
          {},
          L"");

      unsigned int underlyingSystemCallNumInvocations = 0;

      FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &unusedHandleValue,
          0,
          &objectAttributesUnredirectedPath,
          0,
          0,
          0,
          [&testInputFileOperationInstruction](
              std::wstring_view actualUnredirectedPath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            return testInputFileOperationInstruction;
          },
          [&tryFilesTestRecord, &underlyingSystemCallNumInvocations](
              PHANDLE, POBJECT_ATTRIBUTES objectAttributes, ULONG) -> NTSTATUS
          {
            if (underlyingSystemCallNumInvocations >=
                tryFilesTestRecord.expectedOrderedPaths.Size())
              TEST_FAILED_BECAUSE(
                  L"Too many invocations of the underlying system call for try files order enumerator %u.",
                  static_cast<unsigned int>(tryFilesTestRecord.tryFilesTestInput));

            std::wstring_view expectedPathToTry =
                tryFilesTestRecord.expectedOrderedPaths[underlyingSystemCallNumInvocations];
            std::wstring_view actualPathToTry =
                Strings::NtConvertUnicodeStringToStringView(*objectAttributes->ObjectName);
            TEST_ASSERT(actualPathToTry == expectedPathToTry);

            underlyingSystemCallNumInvocations += 1;

            // A failure return code, indicating that the path was not found, is required to cause
            // the next preferred create disposition to be tried. Any other failure code is
            // correctly interpreted to indicate some other I/O error, which would just cause the
            // entire operation to fail with that as the result.
            return NtStatus::kObjectPathNotFound;
          });

      TEST_ASSERT(
          underlyingSystemCallNumInvocations == tryFilesTestRecord.expectedOrderedPaths.Size());
    }
  }

  // Verifies that the correct name is associated with a newly-created file handle, based on
  // whatever name association is specified in the file operation instruction. Various orderings of
  // files to try are also needed here because sometimes the associated name depends on the order in
  // which files are tried.
  TEST_CASE(FilesystemExecutor_NewFileHandle_AssociateNameWithHandle)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kRedirectedPath = L"C:\\RedirectedDirectory\\TestFile.txt";

    UNICODE_STRING unicodeStringUnredirectedPath =
        Strings::NtConvertStringViewToUnicodeString(kUnredirectedPath);
    OBJECT_ATTRIBUTES objectAttributesUnredirectedPath =
        CreateObjectAttributes(unicodeStringUnredirectedPath);

    constexpr std::optional<std::wstring_view> kNoPathShouldSucceed =
        L"Z:\\TotallyInvalidPath\\ThatShouldNotMatchAny\\Inputs.txt";
    constexpr std::optional<std::wstring_view> kAnyPathShouldSucceed = std::nullopt;
    constexpr std::optional<std::wstring_view> kNoPathShouldBeStored = std::nullopt;

    const struct
    {
      EAssociateNameWithHandle associateNameWithHandleTestInput;
      ETryFiles tryFilesTestInput;
      std::optional<std::wstring_view> pathThatShouldSucceed;
      std::optional<std::wstring_view> expectedAssociatedPath;
      std::optional<std::wstring_view> expectedRealOpenedPath;
    } nameAssociationTestRecords[] = {
        //
        // None
        //
        // Regardless of which files are tried and which ultimately succeeds, no name association
        // should happen.
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::None,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kUnredirectedPath,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::None,
         .tryFilesTestInput = ETryFiles::RedirectedFirst,
         .pathThatShouldSucceed = kUnredirectedPath,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::None,
         .tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .pathThatShouldSucceed = kNoPathShouldSucceed,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        //
        // WhicheverWasSuccessful
        //
        // If the file operation is successful (signalled in the test record via the
        // `pathThatShouldSucceed` field) then whichever path succeeded is expected to be associated
        // with the newly-opened file handle.
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kNoPathShouldSucceed,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::RedirectedFirst,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kRedirectedPath,
         .expectedRealOpenedPath = kRedirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .pathThatShouldSucceed = kRedirectedPath,
         .expectedAssociatedPath = kRedirectedPath,
         .expectedRealOpenedPath = kRedirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::RedirectedFirst,
         .pathThatShouldSucceed = kUnredirectedPath,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        //
        // Unredirected
        //
        // If the file operation is successful (signalled in the test record via the
        // `pathThatShouldSucceed` field) then the unredirected path should be associated with the
        // newly-opened file handle. However, on failure, there should be no association. The first
        // test record in this section is the failure case, and all others are success cases.
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Unredirected,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kNoPathShouldSucceed,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Unredirected,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Unredirected,
         .tryFilesTestInput = ETryFiles::RedirectedFirst,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kRedirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Unredirected,
         .tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .pathThatShouldSucceed = kRedirectedPath,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kRedirectedPath},
        //
        // Redirected
        //
        // If the file operation is successful (signalled in the test record via the
        // `pathThatShouldSucceed` field) then the redirected path should be associated with the
        // newly-opened file handle. However, on failure, there should be no association. The first
        // test record in this section is the failure case, and all others are success cases.
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Redirected,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kNoPathShouldSucceed,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Redirected,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kRedirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Redirected,
         .tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kRedirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Redirected,
         .tryFilesTestInput = ETryFiles::RedirectedFirst,
         .pathThatShouldSucceed = kUnredirectedPath,
         .expectedAssociatedPath = kRedirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
    };

    for (const auto& nameAssociationTestRecord : nameAssociationTestRecords)
    {
      const FileOperationInstruction fileOperationInstructionTestInput(
          kRedirectedPath,
          nameAssociationTestRecord.tryFilesTestInput,
          ECreateDispositionPreference::NoPreference,
          nameAssociationTestRecord.associateNameWithHandleTestInput,
          {},
          L"");

      OpenHandleStore openHandleStore;

      HANDLE handleValue = NULL;
      NTSTATUS newFileHandleResult = FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &handleValue,
          0,
          &objectAttributesUnredirectedPath,
          0,
          0,
          0,
          [&fileOperationInstructionTestInput](
              std::wstring_view, FileAccessMode, CreateDisposition) -> FileOperationInstruction
          {
            return fileOperationInstructionTestInput;
          },
          [&nameAssociationTestRecord, kAnyPathShouldSucceed](
              PHANDLE handle, POBJECT_ATTRIBUTES objectAttributes, ULONG) -> NTSTATUS
          {
            if ((nameAssociationTestRecord.pathThatShouldSucceed == kAnyPathShouldSucceed) ||
                (nameAssociationTestRecord.pathThatShouldSucceed ==
                 Strings::NtConvertUnicodeStringToStringView(*objectAttributes->ObjectName)))
            {
              *handle = reinterpret_cast<HANDLE>(1084);
              return NtStatus::kSuccess;
            }

            // A failure return code, indicating that the path was not found, is required to cause
            // the next preferred create disposition to be tried. Any other failure code is
            // correctly interpreted to indicate some other I/O error, which would just cause the
            // entire operation to fail with that as the result.
            return NtStatus::kObjectPathNotFound;
          });

      if (nameAssociationTestRecord.expectedAssociatedPath == kNoPathShouldBeStored)
      {
        TEST_ASSERT(true == openHandleStore.Empty());
      }
      else
      {
        auto maybeHandleData = openHandleStore.GetDataForHandle(handleValue);
        TEST_ASSERT(true == maybeHandleData.has_value());

        std::wstring_view expectedAssociatedPath =
            *nameAssociationTestRecord.expectedAssociatedPath;
        std::wstring_view actualAssociatedPath = maybeHandleData->associatedPath;

        std::wstring_view expectedRealOpenedPath =
            *nameAssociationTestRecord.expectedRealOpenedPath;
        std::wstring_view actualRealOpenedPath = maybeHandleData->realOpenedPath;

        TEST_ASSERT(actualAssociatedPath == expectedAssociatedPath);
        TEST_ASSERT(actualRealOpenedPath == expectedRealOpenedPath);
      }
    }
  }

  // Verifies that create disposition preferences contained in filesystem instructions are
  // honored when creating a new file handle. The test case itself sends in a variety of different
  // create dispositions from the application and encodes several different create disposition
  // preferences in the instruction, then verifies that the actual new file handle creation requests
  // the right sequence of create dispositions. Since only a single filename exists to be tried (the
  // unredirected filename) each create disposition should be tried exactly once.
  TEST_CASE(FilesystemExecutor_NewFileHandle_CreateDispositionPreference_UnredirectedOnly)
  {
    // Holds a single create disposition or forced error code and used to represent what the
    // filesystem executor is expected to do in one particular instance.
    using TCreateDispositionOrForcedError = ValueOrError<ULONG, NTSTATUS>;

    // Holds multiple create dispositions, or forced error codes, in the expected order that they
    // should be tried. If a create disposition is present then it is expected as the parameter,
    // otherwise it is expected as the return code from the filesystem executor function.
    using TExpectedCreateDispositionsOrForcedErrors = ArrayList<TCreateDispositionOrForcedError, 2>;

    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";

    const struct
    {
      ECreateDispositionPreference createDispositionPreferenceTestInput;
      ULONG ntParamCreateDispositionFromApplication;
      TExpectedCreateDispositionsOrForcedErrors expectedOrderedNtParamCreateDisposition;
    } createDispositionTestRecords[] = {
        //
        // NoPreference
        //
        // Create disposition parameters should be passed through to the system exactly as is.
        // Pathwinder does not impose any requirements or preferences in this situation.
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::NoPreference,
         .ntParamCreateDispositionFromApplication = FILE_OPEN_IF,
         .expectedOrderedNtParamCreateDisposition = {TCreateDispositionOrForcedError::MakeValue(
             FILE_OPEN_IF)}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::NoPreference,
         .ntParamCreateDispositionFromApplication = FILE_SUPERSEDE,
         .expectedOrderedNtParamCreateDisposition = {TCreateDispositionOrForcedError::MakeValue(
             FILE_SUPERSEDE)}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::NoPreference,
         .ntParamCreateDispositionFromApplication = FILE_OPEN,
         .expectedOrderedNtParamCreateDisposition = {TCreateDispositionOrForcedError::MakeValue(
             FILE_OPEN)}},
        //
        // PreferCreateNewFile
        //
        // Multiple attempts should be made, and some of the NT paramters should accordingly be
        // modified so that new file creation is attempted first before opening an existing file. If
        // the application already explicitly requires that a new file be created or an existing
        // file be opened, then there is no modification needed.
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::PreferCreateNewFile,
         .ntParamCreateDispositionFromApplication = FILE_CREATE,
         .expectedOrderedNtParamCreateDisposition = {TCreateDispositionOrForcedError::MakeValue(
             FILE_CREATE)}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::PreferCreateNewFile,
         .ntParamCreateDispositionFromApplication = FILE_OPEN,
         .expectedOrderedNtParamCreateDisposition = {TCreateDispositionOrForcedError::MakeValue(
             FILE_OPEN)}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::PreferCreateNewFile,
         .ntParamCreateDispositionFromApplication = FILE_OPEN_IF,
         .expectedOrderedNtParamCreateDisposition =
             {TCreateDispositionOrForcedError::MakeValue(FILE_CREATE),
              TCreateDispositionOrForcedError::MakeValue(FILE_OPEN)}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::PreferCreateNewFile,
         .ntParamCreateDispositionFromApplication = FILE_OVERWRITE_IF,
         .expectedOrderedNtParamCreateDisposition =
             {TCreateDispositionOrForcedError::MakeValue(FILE_CREATE),
              TCreateDispositionOrForcedError::MakeValue(FILE_OVERWRITE)}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::PreferCreateNewFile,
         .ntParamCreateDispositionFromApplication = FILE_SUPERSEDE,
         .expectedOrderedNtParamCreateDisposition =
             {TCreateDispositionOrForcedError::MakeValue(FILE_CREATE),
              TCreateDispositionOrForcedError::MakeValue(FILE_SUPERSEDE)}},
        //
        // PreferOpenExistingFile
        //
        // Multiple attempts should be made, and some of the NT paramters should accordingly be
        // modified so that an existing file is opened before creating a new file. If the
        // application already explicitly requires that a new file be created or an existing file be
        // opened, then there is no modification needed.
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_CREATE,
         .expectedOrderedNtParamCreateDisposition = {TCreateDispositionOrForcedError::MakeValue(
             FILE_CREATE)}},
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_OPEN,
         .expectedOrderedNtParamCreateDisposition = {TCreateDispositionOrForcedError::MakeValue(
             FILE_OPEN)}},
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_OPEN_IF,
         .expectedOrderedNtParamCreateDisposition =
             {TCreateDispositionOrForcedError::MakeValue(FILE_OPEN),
              TCreateDispositionOrForcedError::MakeValue(FILE_CREATE)}},
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_OVERWRITE_IF,
         .expectedOrderedNtParamCreateDisposition =
             {TCreateDispositionOrForcedError::MakeValue(FILE_OVERWRITE),
              TCreateDispositionOrForcedError::MakeValue(FILE_CREATE)}},
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_SUPERSEDE,
         .expectedOrderedNtParamCreateDisposition = {
             TCreateDispositionOrForcedError::MakeValue(FILE_SUPERSEDE)}}};

    UNICODE_STRING unicodeStringUnredirectedPath =
        Strings::NtConvertStringViewToUnicodeString(kUnredirectedPath);
    OBJECT_ATTRIBUTES objectAttributesUnredirectedPath =
        CreateObjectAttributes(unicodeStringUnredirectedPath);

    for (const auto& createDispositionTestRecord : createDispositionTestRecords)
    {
      HANDLE unusedHandleValue = NULL;

      MockFilesystemOperations mockFilesystem;
      OpenHandleStore openHandleStore;

      const FileOperationInstruction testInputFileOperationInstruction(
          std::nullopt,
          ETryFiles::UnredirectedOnly,
          createDispositionTestRecord.createDispositionPreferenceTestInput,
          EAssociateNameWithHandle::None,
          {},
          L"");

      unsigned int underlyingSystemCallNumInvocations = 0;

      NTSTATUS actualReturnCode = FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &unusedHandleValue,
          0,
          &objectAttributesUnredirectedPath,
          0,
          createDispositionTestRecord.ntParamCreateDispositionFromApplication,
          0,
          [&testInputFileOperationInstruction](
              std::wstring_view actualUnredirectedPath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            return testInputFileOperationInstruction;
          },
          [&createDispositionTestRecord, &underlyingSystemCallNumInvocations](
              PHANDLE, POBJECT_ATTRIBUTES, ULONG actualNtParamCreateDisposition) -> NTSTATUS
          {
            if (underlyingSystemCallNumInvocations >=
                createDispositionTestRecord.expectedOrderedNtParamCreateDisposition.Size())
              TEST_FAILED_BECAUSE(
                  L"Too many invocations of the underlying system call for application-supplied create disposition 0x%08x and create disposition preference %u.",
                  static_cast<unsigned int>(
                      createDispositionTestRecord.ntParamCreateDispositionFromApplication),
                  static_cast<unsigned int>(
                      createDispositionTestRecord.createDispositionPreferenceTestInput));

            if (true ==
                createDispositionTestRecord
                    .expectedOrderedNtParamCreateDisposition[underlyingSystemCallNumInvocations]
                    .HasError())
              TEST_FAILED_BECAUSE(
                  "Incorrect invocation of underlying system call when NTSTATUS 0x%08x was expected for application-supplied create disposition 0x%08x and create disposition preference %u.",
                  static_cast<unsigned int>(createDispositionTestRecord
                                                .expectedOrderedNtParamCreateDisposition
                                                    [underlyingSystemCallNumInvocations]
                                                .Error()),
                  static_cast<unsigned int>(
                      createDispositionTestRecord.ntParamCreateDispositionFromApplication),
                  static_cast<unsigned int>(
                      createDispositionTestRecord.createDispositionPreferenceTestInput));

            ULONG expectedNtParamCreateDisposition =
                createDispositionTestRecord
                    .expectedOrderedNtParamCreateDisposition[underlyingSystemCallNumInvocations]
                    .Value();
            TEST_ASSERT(actualNtParamCreateDisposition == expectedNtParamCreateDisposition);

            underlyingSystemCallNumInvocations += 1;

            // A failure return code, indicating that the path was not found, is required to cause
            // the next preferred create disposition to be tried. Any other failure code is
            // correctly interpreted to indicate some other I/O error, which would just cause the
            // entire operation to fail with that as the result.
            return NtStatus::kObjectPathNotFound;
          });

      if (createDispositionTestRecord.expectedOrderedNtParamCreateDisposition.Back().HasValue())
      {
        TEST_ASSERT(
            underlyingSystemCallNumInvocations ==
            createDispositionTestRecord.expectedOrderedNtParamCreateDisposition.Size());
      }
      else
      {
        TEST_ASSERT(
            underlyingSystemCallNumInvocations ==
            createDispositionTestRecord.expectedOrderedNtParamCreateDisposition.Size() - 1);

        NTSTATUS expectedReturnCode =
            createDispositionTestRecord.expectedOrderedNtParamCreateDisposition.Back().Error();
        TEST_ASSERT(actualReturnCode == expectedReturnCode);
      }
    }
  }

  // Verifies that create disposition preferences contained in filesystem instructions are
  // honored when creating a new file handle. The test case itself sends in a variety of different
  // create dispositions from the application and encodes several different create disposition
  // preferences in the instruction, then verifies that the actual new file handle creation requests
  // the right sequence of create dispositions. This test emulates "overlay mode" by supplying a
  // redirected file and requesting that the redirected file be tried first. Where it makes a
  // difference to create disposition and file name order, the test inputs also specify which of the
  // unredirected and redirected paths exist in the mock filesystem.
  TEST_CASE(FilesystemExecutor_NewFileHandle_CreateDispositionPreference_RedirectedFirst)
  {
    // Represents an expected combination of parameters to the underlying system call, combining a
    // create disposition with an absolute path.
    struct SCreateDispositionAndPath
    {
      ULONG ntParamCreateDisposition;
      std::wstring_view absolutePath;
    };

    // Holds a single parameter pair or forced error code and used to represent what the
    // filesystem executor is expected to do in one particular instance.
    using TParametersOrForcedError = ValueOrError<SCreateDispositionAndPath, NTSTATUS>;

    // Holds multiple parameter pairs, or forced error codes, in the expected order that they
    // should be tried. If a parameter pair is present then it is expected as the parameters to the
    // underlying system call, otherwise it is expected as the return code from the filesystem
    // executor function.
    using TExpectedParametersOrForcedErrors = ArrayList<TParametersOrForcedError, 4>;

    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kRedirectedPath = L"C:\\RedirectedDirectory\\TestFile.txt";

    const struct
    {
      ECreateDispositionPreference createDispositionPreferenceTestInput;
      ULONG ntParamCreateDispositionFromApplication;
      TExpectedParametersOrForcedErrors expectedOrderedParameters;
      bool unredirectedPathExists;
      bool redirectedPathExists;
    } createDispositionTestRecords[] = {
        //
        // NoPreference
        //
        // Create disposition parameters should be passed through to the system exactly as is.
        // Pathwinder does not impose any requirements or preferences in this situation.
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::NoPreference,
         .ntParamCreateDispositionFromApplication = FILE_OPEN_IF,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN_IF, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN_IF, .absolutePath = kUnredirectedPath})}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::NoPreference,
         .ntParamCreateDispositionFromApplication = FILE_SUPERSEDE,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kUnredirectedPath})}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::NoPreference,
         .ntParamCreateDispositionFromApplication = FILE_OPEN,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN, .absolutePath = kUnredirectedPath})}},
        //
        // PreferCreateNewFile
        //
        // Multiple attempts should be made, and some of the NT paramters should accordingly be
        // modified so that new file creation is attempted first before opening an existing file. If
        // the application already explicitly requires that a new file be created or an existing
        // file be opened, then there is no modification needed.
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::PreferCreateNewFile,
         .ntParamCreateDispositionFromApplication = FILE_CREATE,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kUnredirectedPath})}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::PreferCreateNewFile,
         .ntParamCreateDispositionFromApplication = FILE_OPEN,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN, .absolutePath = kUnredirectedPath})}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::PreferCreateNewFile,
         .ntParamCreateDispositionFromApplication = FILE_OPEN_IF,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kUnredirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN, .absolutePath = kUnredirectedPath})}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::PreferCreateNewFile,
         .ntParamCreateDispositionFromApplication = FILE_OVERWRITE_IF,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kUnredirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OVERWRITE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OVERWRITE, .absolutePath = kUnredirectedPath})}},
        {.createDispositionPreferenceTestInput = ECreateDispositionPreference::PreferCreateNewFile,
         .ntParamCreateDispositionFromApplication = FILE_SUPERSEDE,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kUnredirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kUnredirectedPath})}},
        //
        // PreferOpenExistingFile
        //
        // Multiple attempts should be made, and some of the NT paramters should accordingly be
        // modified so that an existing file is opened before creating a new file. If the
        // application already explicitly requires that a new file be created or an existing file be
        // opened, then there is no modification needed.
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_CREATE,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kUnredirectedPath})}},
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_OPEN,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN, .absolutePath = kUnredirectedPath})}},
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_OPEN_IF,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OPEN, .absolutePath = kUnredirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kUnredirectedPath})}},
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_OVERWRITE_IF,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OVERWRITE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_OVERWRITE, .absolutePath = kUnredirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_CREATE, .absolutePath = kUnredirectedPath})}},
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_SUPERSEDE,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kUnredirectedPath})},
         .unredirectedPathExists = false,
         .redirectedPathExists = false},
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_SUPERSEDE,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kUnredirectedPath})},
         .unredirectedPathExists = false,
         .redirectedPathExists = true},
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_SUPERSEDE,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kUnredirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kUnredirectedPath})},
         .unredirectedPathExists = true,
         .redirectedPathExists = false},
        {.createDispositionPreferenceTestInput =
             ECreateDispositionPreference::PreferOpenExistingFile,
         .ntParamCreateDispositionFromApplication = FILE_SUPERSEDE,
         .expectedOrderedParameters =
             {TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kUnredirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kRedirectedPath}),
              TParametersOrForcedError::MakeValue(SCreateDispositionAndPath{
                  .ntParamCreateDisposition = FILE_SUPERSEDE, .absolutePath = kUnredirectedPath})},
         .unredirectedPathExists = true,
         .redirectedPathExists = true},
    };

    UNICODE_STRING unicodeStringUnredirectedPath =
        Strings::NtConvertStringViewToUnicodeString(kUnredirectedPath);
    OBJECT_ATTRIBUTES objectAttributesUnredirectedPath =
        CreateObjectAttributes(unicodeStringUnredirectedPath);

    for (const auto& createDispositionTestRecord : createDispositionTestRecords)
    {
      HANDLE unusedHandleValue = NULL;

      MockFilesystemOperations mockFilesystem;
      if (createDispositionTestRecord.unredirectedPathExists)
        mockFilesystem.AddFile(kUnredirectedPath);
      if (createDispositionTestRecord.redirectedPathExists) mockFilesystem.AddFile(kRedirectedPath);

      OpenHandleStore openHandleStore;

      const FileOperationInstruction testInputFileOperationInstruction(
          kRedirectedPath,
          ETryFiles::RedirectedFirst,
          createDispositionTestRecord.createDispositionPreferenceTestInput,
          EAssociateNameWithHandle::None,
          {},
          L"");

      unsigned int underlyingSystemCallNumInvocations = 0;

      NTSTATUS actualReturnCode = FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &unusedHandleValue,
          0,
          &objectAttributesUnredirectedPath,
          0,
          createDispositionTestRecord.ntParamCreateDispositionFromApplication,
          0,
          [&testInputFileOperationInstruction](
              std::wstring_view actualUnredirectedPath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            return testInputFileOperationInstruction;
          },
          [&createDispositionTestRecord, &underlyingSystemCallNumInvocations](
              PHANDLE,
              POBJECT_ATTRIBUTES objectAttributes,
              ULONG actualNtParamCreateDisposition) -> NTSTATUS
          {
            if (underlyingSystemCallNumInvocations >=
                createDispositionTestRecord.expectedOrderedParameters.Size())
              TEST_FAILED_BECAUSE(
                  L"Too many invocations of the underlying system call for application-supplied create disposition 0x%08x and create disposition preference %u.",
                  static_cast<unsigned int>(
                      createDispositionTestRecord.ntParamCreateDispositionFromApplication),
                  static_cast<unsigned int>(
                      createDispositionTestRecord.createDispositionPreferenceTestInput));

            if (true ==
                createDispositionTestRecord
                    .expectedOrderedParameters[underlyingSystemCallNumInvocations]
                    .HasError())
              TEST_FAILED_BECAUSE(
                  "Incorrect invocation of underlying system call when NTSTATUS 0x%08x was expected for application-supplied create disposition 0x%08x and create disposition preference %u.",
                  static_cast<unsigned int>(
                      createDispositionTestRecord
                          .expectedOrderedParameters[underlyingSystemCallNumInvocations]
                          .Error()),
                  static_cast<unsigned int>(
                      createDispositionTestRecord.ntParamCreateDispositionFromApplication),
                  static_cast<unsigned int>(
                      createDispositionTestRecord.createDispositionPreferenceTestInput));

            ULONG expectedNtParamCreateDisposition =
                createDispositionTestRecord
                    .expectedOrderedParameters[underlyingSystemCallNumInvocations]
                    .Value()
                    .ntParamCreateDisposition;
            TEST_ASSERT(actualNtParamCreateDisposition == expectedNtParamCreateDisposition);

            std::wstring_view expectedPathToTry =
                createDispositionTestRecord
                    .expectedOrderedParameters[underlyingSystemCallNumInvocations]
                    .Value()
                    .absolutePath;
            std::wstring_view actualPathToTry =
                Strings::NtConvertUnicodeStringToStringView(*objectAttributes->ObjectName);
            TEST_ASSERT(actualPathToTry == expectedPathToTry);

            underlyingSystemCallNumInvocations += 1;

            // A failure return code, indicating that the path was not found, is required to cause
            // the next preferred create disposition to be tried. Any other failure code is
            // correctly interpreted to indicate some other I/O error, which would just cause the
            // entire operation to fail with that as the result.
            return NtStatus::kObjectPathNotFound;
          });

      if (createDispositionTestRecord.expectedOrderedParameters.Back().HasValue())
      {
        TEST_ASSERT(
            underlyingSystemCallNumInvocations ==
            createDispositionTestRecord.expectedOrderedParameters.Size());
      }
      else
      {
        TEST_ASSERT(
            underlyingSystemCallNumInvocations ==
            createDispositionTestRecord.expectedOrderedParameters.Size() - 1);

        NTSTATUS expectedReturnCode =
            createDispositionTestRecord.expectedOrderedParameters.Back().Error();
        TEST_ASSERT(actualReturnCode == expectedReturnCode);
      }
    }
  }

  // Verifies that a pre-operation request contained in a filesystem operation instruction is
  // executed correctly when creating a new file handle. The file operation instruction only
  // contains a pre-operation and nothing else, and this test case exercises an operation to ensure
  // a path hierarchy exists. The forms of instructions exercised by this test are not generally
  // produced by filesystem director objects but are intended specifically to exercise pre-operation
  // functionality.
  TEST_CASE(FilesystemExecutor_NewFileHandle_PreOperation_EnsurePathHierarchyExists)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kExtraPreOperationHierarchyToCreate =
        L"C:\\ExtraPreOperation\\Directory\\Hierarchy\\To\\Create";

    // This test case only exercises pre-operations, so no association should be created and hence
    // nothing should be added to the open handle store. The important parts here are the extra
    // pre-operation itself and the operand to that pre-operation.
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

    UNICODE_STRING unicodeStringUnredirectedPath =
        Strings::NtConvertStringViewToUnicodeString(kUnredirectedPath);
    OBJECT_ATTRIBUTES objectAttributesUnredirectedPath =
        CreateObjectAttributes(unicodeStringUnredirectedPath);

    for (const auto& fileOperationInstructionToTry : fileOperationInstructionsToTry)
    {
      HANDLE unusedHandleValue = NULL;

      MockFilesystemOperations mockFilesystem;
      OpenHandleStore openHandleStore;

      bool instructionSourceWasInvoked = false;

      // Pre-operation should not have been executed yet because the filesystem executor function
      // was not yet invoked.
      TEST_ASSERT(false == mockFilesystem.IsDirectory(kExtraPreOperationHierarchyToCreate));

      FilesystemExecutor::NewFileHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &unusedHandleValue,
          0,
          &objectAttributesUnredirectedPath,
          0,
          0,
          0,
          [kUnredirectedPath, &fileOperationInstructionToTry, &instructionSourceWasInvoked](
              std::wstring_view, FileAccessMode, CreateDisposition) -> FileOperationInstruction
          {
            instructionSourceWasInvoked = true;
            return fileOperationInstructionToTry;
          },
          [&mockFilesystem, &objectAttributesUnredirectedPath](
              PHANDLE, POBJECT_ATTRIBUTES objectAttributes, ULONG) -> NTSTATUS
          {
            // Checking here for the completion of the pre-operation ensures that it was done prior
            // to the underlying system call being invoked.
            TEST_ASSERT(true == mockFilesystem.IsDirectory(kExtraPreOperationHierarchyToCreate));
            return NtStatus::kSuccess;
          });

      TEST_ASSERT(true == instructionSourceWasInvoked);
      TEST_ASSERT(true == openHandleStore.Empty());
    }
  }

  // Verifies that requests for new file handles are passed through to the system without
  // modification or interception if the root directory handle is specified but not cached. In
  // this situation, the root directory would have been declared "uninteresting" by the filesystem
  // director, so the executor should just assume it is still uninteresting and not even ask for a
  // redirection instruction. Request should be passed through unmodified to the system. Various
  // valid forms of file operation instructions are exercised, even those that are not actually
  // ever produced by a filesystem director.
  TEST_CASE(FilesystemExecutor_NewFileHandle_PassthroughWithoutInstruction_UncachedRootDirectory)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kDirectoryName =
        kUnredirectedPath.substr(0, kUnredirectedPath.find_last_of(L'\\'));
    constexpr std::wstring_view kFileName =
        kUnredirectedPath.substr(1 + kUnredirectedPath.find_last_of(L'\\'));

    UNICODE_STRING unicodeStringRelativePath =
        Strings::NtConvertStringViewToUnicodeString(kFileName);
    OBJECT_ATTRIBUTES objectAttributesRelativePath =
        CreateObjectAttributes(unicodeStringRelativePath, reinterpret_cast<HANDLE>(99));

    HANDLE unusedHandleValue = NULL;

    MockFilesystemOperations mockFilesystem;
    OpenHandleStore openHandleStore;

    FilesystemExecutor::NewFileHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        &unusedHandleValue,
        0,
        &objectAttributesRelativePath,
        0,
        0,
        0,
        [kUnredirectedPath](
            std::wstring_view actualUnredirectedPath,
            FileAccessMode,
            CreateDisposition) -> FileOperationInstruction
        {
          TEST_FAILED_BECAUSE(
              "Instruction source should not be invoked if the root directory handle is present but uncached.");
        },
        [&objectAttributesRelativePath](
            PHANDLE, POBJECT_ATTRIBUTES objectAttributes, ULONG) -> NTSTATUS
        {
          const OBJECT_ATTRIBUTES& expectedObjectAttributes = objectAttributesRelativePath;
          const OBJECT_ATTRIBUTES& actualObjectAttributes = *objectAttributes;
          TEST_ASSERT(EqualObjectAttributes(actualObjectAttributes, expectedObjectAttributes));

          return NtStatus::kSuccess;
        });

    TEST_ASSERT(true == openHandleStore.Empty());
  }

  // Verifies that the underlying system call return code is propagated to the caller as the result
  // of the executor operation when a file is renamed.
  TEST_CASE(FilesystemExecutor_RenameByHandle_PropagateReturnCode)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kRedirectedPath = L"C:\\RedirectedDirectory\\TestFile.txt";

    BytewiseDanglingFilenameStruct<SFileRenameInformation> inputFileRenameInformation(
        SFileRenameInformation{}, kUnredirectedPath);

    const FileOperationInstruction fileOperationInstructionsToTry[] = {
        FileOperationInstruction::NoRedirectionOrInterception(),
        FileOperationInstruction::InterceptWithoutRedirection(),
        FileOperationInstruction::SimpleRedirectTo(kRedirectedPath),
        FileOperationInstruction::OverlayRedirectTo(kRedirectedPath),
    };

    const NTSTATUS returnCodesToTry[] = {
        NtStatus::kSuccess,
        NtStatus::kBufferOverflow,
        NtStatus::kInvalidInfoClass,
        NtStatus::kInvalidParameter,
        NtStatus::kNoSuchFile,
        NtStatus::kObjectNameInvalid,
        NtStatus::kObjectNameNotFound,
        NtStatus::kObjectPathInvalid,
        NtStatus::kObjectPathNotFound,
        NtStatus::kInternalError,
    };

    for (const auto& fileOperationInstructionToTry : fileOperationInstructionsToTry)
    {
      for (NTSTATUS returnCodeToTry : returnCodesToTry)
      {
        HANDLE unusedHandleValue = NULL;

        OpenHandleStore openHandleStore;

        const NTSTATUS expectedReturnCode = returnCodeToTry;
        const NTSTATUS actualReturnCode = FilesystemExecutor::RenameByHandle(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            unusedHandleValue,
            inputFileRenameInformation.GetFileInformationStruct(),
            inputFileRenameInformation.GetFileInformationStructSizeBytes(),
            [&fileOperationInstructionToTry](
                std::wstring_view, FileAccessMode, CreateDisposition) -> FileOperationInstruction
            {
              return fileOperationInstructionToTry;
            },
            [expectedReturnCode](HANDLE, SFileRenameInformation&, ULONG) -> NTSTATUS
            {
              return expectedReturnCode;
            });

        TEST_ASSERT(actualReturnCode == expectedReturnCode);
      }
    }
  }

  // Verifies that the filesystem executor correctly composes a complete path when requesting a file
  // operation instruction as part of renaming an existing open file. This test case only exercises
  // the basic forms of input for path composition, as follows. If no root directory is specified
  // then the requested path is the same as the input path. If the root directory is specified by
  // handle and the handle is cached in the open handle store then the requested path is the root
  // directory path concatenated with the input path. Note that an uncached (but present) root
  // directory is handled by a different test case entirely, as this situation should result in
  // passthrough behavior.
  TEST_CASE(FilesystemExecutor_RenameByHandle_InstructionSourcePathComposition_Nominal)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kDirectoryName =
        kUnredirectedPath.substr(0, kUnredirectedPath.find_last_of(L'\\'));
    constexpr std::wstring_view kFileName =
        kUnredirectedPath.substr(1 + kUnredirectedPath.find_last_of(L'\\'));

    const HANDLE kFileBeingRenamedHandleTestInput = reinterpret_cast<HANDLE>(1);
    const HANDLE kRootDirectoryHandleValueTestInput = reinterpret_cast<HANDLE>(2049);

    const struct
    {
      std::optional<std::wstring_view> rootDirectoryName;
      std::wstring_view fileName;
    } instructionSourcePathCompositionTestRecords[] = {
        {.rootDirectoryName = std::nullopt, .fileName = kUnredirectedPath},
        {.rootDirectoryName = kDirectoryName, .fileName = kFileName},
    };

    for (const auto& instructionSourcePathCompositionTestRecord :
         instructionSourcePathCompositionTestRecords)
    {
      OpenHandleStore openHandleStore;

      HANDLE rootDirectoryHandle = NULL;

      if (instructionSourcePathCompositionTestRecord.rootDirectoryName.has_value())
      {
        rootDirectoryHandle = kRootDirectoryHandleValueTestInput;
        openHandleStore.InsertHandle(
            rootDirectoryHandle,
            std::wstring(*instructionSourcePathCompositionTestRecord.rootDirectoryName),
            std::wstring(*instructionSourcePathCompositionTestRecord.rootDirectoryName));
      }

      BytewiseDanglingFilenameStruct<SFileRenameInformation> fileRenameInformationUnredirectedPath(
          SFileRenameInformation{.rootDirectory = rootDirectoryHandle},
          instructionSourcePathCompositionTestRecord.fileName);

      FilesystemExecutor::RenameByHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          kFileBeingRenamedHandleTestInput,
          fileRenameInformationUnredirectedPath.GetFileInformationStruct(),
          fileRenameInformationUnredirectedPath.GetFileInformationStructSizeBytes(),
          [kUnredirectedPath](
              std::wstring_view actualRequestedPath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            std::wstring_view expectedRequestedPath = kUnredirectedPath;
            TEST_ASSERT(actualRequestedPath == expectedRequestedPath);
            return FileOperationInstruction::NoRedirectionOrInterception();
          },
          [](HANDLE, SFileRenameInformation&, ULONG) -> NTSTATUS
          {
            return NtStatus::kSuccess;
          });
    }
  }

  // Verifies special rename behavior whereby a root directory handle is not specified and the new
  // file name is a relative path, meaning that the file name changes but the directory does not. In
  // this test case, the file being renamed is cached in the open handle store, so when requesting
  // an instruction the path should be composed based on the original associated path in cache.
  TEST_CASE(FilesystemExecutor_RenameByHandle_InstructionSourcePathComposition_CachedRelativeMove)
  {
    constexpr std::wstring_view kDirectoryName = L"C:\\TestDirectory";
    constexpr std::wstring_view kInitialFilename = L"Initial.txt";
    constexpr std::wstring_view kRenamedFilename = L"Subdir\\Renamed.txt";
    constexpr std::wstring_view kInitialPath = L"C:\\TestDirectory\\Initial.txt";
    constexpr std::wstring_view kRenamedPath = L"C:\\TestDirectory\\Subdir\\Renamed.txt";

    const HANDLE kFileBeingRenamedHandleTestInput = reinterpret_cast<HANDLE>(23);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        kFileBeingRenamedHandleTestInput,
        std::wstring(kInitialPath),
        L"C:\\SomeOther\\RealOpenedPath\\Initial.txt");

    BytewiseDanglingFilenameStruct<SFileRenameInformation> fileRenameInformationUnredirectedPath(
        SFileRenameInformation{}, kRenamedFilename);

    FilesystemExecutor::RenameByHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        kFileBeingRenamedHandleTestInput,
        fileRenameInformationUnredirectedPath.GetFileInformationStruct(),
        fileRenameInformationUnredirectedPath.GetFileInformationStructSizeBytes(),
        [kRenamedPath](std::wstring_view actualRequestedPath, FileAccessMode, CreateDisposition)
            -> FileOperationInstruction
        {
          std::wstring_view expectedRequestedPath = kRenamedPath;
          TEST_ASSERT(actualRequestedPath == expectedRequestedPath);
          return FileOperationInstruction::NoRedirectionOrInterception();
        },
        [](HANDLE, SFileRenameInformation&, ULONG) -> NTSTATUS
        {
          return NtStatus::kSuccess;
        });
  }

  // Verifies special rename behavior whereby a root directory handle is not specified and the new
  // file name is a relative path, meaning that the file name changes but the directory does not. In
  // this test case, the file being renamed is not cached in the open handle store, so when
  // requesting an instruction the system itself will need to be consulted for the directory.
  TEST_CASE(FilesystemExecutor_RenameByHandle_InstructionSourcePathComposition_UncachedRelativeMove)
  {
    constexpr std::wstring_view kDirectoryName = L"C:\\TestDirectory";
    constexpr std::wstring_view kInitialFilename = L"Initial.txt";
    constexpr std::wstring_view kRenamedFilename = L"Subdir\\Renamed.txt";
    constexpr std::wstring_view kInitialPath = L"C:\\TestDirectory\\Initial.txt";
    constexpr std::wstring_view kRenamedPath = L"C:\\TestDirectory\\Subdir\\Renamed.txt";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddFile(kInitialPath);

    const HANDLE initialPathHandle = mockFilesystem.Open(kInitialPath);

    OpenHandleStore openHandleStore;

    BytewiseDanglingFilenameStruct<SFileRenameInformation> fileRenameInformationUnredirectedPath(
        SFileRenameInformation{}, kRenamedFilename);

    FilesystemExecutor::RenameByHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        initialPathHandle,
        fileRenameInformationUnredirectedPath.GetFileInformationStruct(),
        fileRenameInformationUnredirectedPath.GetFileInformationStructSizeBytes(),
        [kRenamedPath](std::wstring_view actualRequestedPath, FileAccessMode, CreateDisposition)
            -> FileOperationInstruction
        {
          std::wstring_view expectedRequestedPath = kRenamedPath;
          TEST_ASSERT(actualRequestedPath == expectedRequestedPath);
          return FileOperationInstruction::NoRedirectionOrInterception();
        },
        [](HANDLE, SFileRenameInformation&, ULONG) -> NTSTATUS
        {
          return NtStatus::kSuccess;
        });
  }

  // Verifies that any file attempt preference is honored if it is contained in a file operation
  // instruction when an existing open file is being renamed. The instructions used in this test
  // case all contain an unredirected and a redirected path, and they supply various enumerators
  // indicating the order in which the files should be tried.
  TEST_CASE(FilesystemExecutor_RenameByHandle_TryFilesOrder)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kRedirectedPath = L"C:\\RedirectedDirectory\\TestFile.txt";

    // Holds paths in the order that they are expected to be tried in invocations of the underlying
    // system call.
    using TExpectedPaths = ArrayList<std::wstring_view, 2>;

    const struct
    {
      ETryFiles tryFilesTestInput;
      TExpectedPaths expectedOrderedPaths;
    } tryFilesTestRecords[] = {
        {.tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .expectedOrderedPaths = {kUnredirectedPath}},
        {.tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .expectedOrderedPaths = {kUnredirectedPath, kRedirectedPath}},
        {.tryFilesTestInput = ETryFiles::RedirectedOnly, .expectedOrderedPaths = {kRedirectedPath}},
        {.tryFilesTestInput = ETryFiles::RedirectedFirst,
         .expectedOrderedPaths = {kRedirectedPath, kUnredirectedPath}},
    };

    BytewiseDanglingFilenameStruct<SFileRenameInformation> fileRenameInformationUnredirectedPath(
        SFileRenameInformation{}, kUnredirectedPath);

    for (const auto& tryFilesTestRecord : tryFilesTestRecords)
    {
      HANDLE unusedHandleValue = NULL;

      MockFilesystemOperations mockFilesystem;
      OpenHandleStore openHandleStore;

      const FileOperationInstruction testInputFileOperationInstruction(
          kRedirectedPath,
          tryFilesTestRecord.tryFilesTestInput,
          ECreateDispositionPreference::NoPreference,
          EAssociateNameWithHandle::None,
          {},
          L"");

      unsigned int underlyingSystemCallNumInvocations = 0;

      FilesystemExecutor::RenameByHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          unusedHandleValue,
          fileRenameInformationUnredirectedPath.GetFileInformationStruct(),
          fileRenameInformationUnredirectedPath.GetFileInformationStructSizeBytes(),
          [&testInputFileOperationInstruction](
              std::wstring_view actualUnredirectedPath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            return testInputFileOperationInstruction;
          },
          [&tryFilesTestRecord, &underlyingSystemCallNumInvocations](
              HANDLE, SFileRenameInformation& fileRenameInformation, ULONG) -> NTSTATUS
          {
            if (underlyingSystemCallNumInvocations >=
                tryFilesTestRecord.expectedOrderedPaths.Size())
              TEST_FAILED_BECAUSE(
                  L"Too many invocations of the underlying system call for try files order enumerator %u.",
                  static_cast<unsigned int>(tryFilesTestRecord.tryFilesTestInput));

            std::wstring_view expectedPathToTry =
                tryFilesTestRecord.expectedOrderedPaths[underlyingSystemCallNumInvocations];
            std::wstring_view actualPathToTry = std::wstring_view(
                fileRenameInformation.fileName,
                fileRenameInformation.fileNameLength / sizeof(wchar_t));
            TEST_ASSERT(actualPathToTry == expectedPathToTry);

            underlyingSystemCallNumInvocations += 1;

            // A failure return code, indicating that the path was not found, is required to cause
            // the next preferred create disposition to be tried. Any other failure code is
            // correctly interpreted to indicate some other I/O error, which would just cause the
            // entire operation to fail with that as the result.
            return NtStatus::kObjectPathNotFound;
          });

      TEST_ASSERT(
          underlyingSystemCallNumInvocations == tryFilesTestRecord.expectedOrderedPaths.Size());
    }
  }

  // Verifies that the correct name is associated with a file handle for a file that has just been
  // renamed, based on whatever name association is specified in the file operation instruction.
  // Various orderings of files to try are also needed here because sometimes the associated name
  // depends on the order in which files are tried. In this test case the initial file is open and
  // cached in the open handle store.
  TEST_CASE(FilesystemExecutor_RenameByHandle_AssociateNameWithHandle)
  {
    constexpr std::wstring_view kInitialPath = L"D:\\InitialDirectory\\InitialFile.txt";
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kRedirectedPath = L"C:\\RedirectedDirectory\\TestFile.txt";

    BytewiseDanglingFilenameStruct<SFileRenameInformation> fileRenameInformationUnredirectedPath(
        SFileRenameInformation{}, kUnredirectedPath);

    constexpr std::optional<std::wstring_view> kNoPathShouldSucceed =
        L"Z:\\TotallyInvalidPath\\ThatShouldNotMatchAny\\Inputs.txt";
    constexpr std::optional<std::wstring_view> kAnyPathShouldSucceed = std::nullopt;
    constexpr std::optional<std::wstring_view> kNoPathShouldBeStored = std::nullopt;

    // For a file rename operation, a combination of `kNoPathShouldSucceed` and
    // `kNoPathShouldBeStored` means that the entire operation failed and therefore the open handle
    // store should not be touched. The result is that the open handle store will continue to have
    // an association of the existing file to its initial path.

    const struct
    {
      EAssociateNameWithHandle associateNameWithHandleTestInput;
      ETryFiles tryFilesTestInput;
      std::optional<std::wstring_view> pathThatShouldSucceed;
      std::optional<std::wstring_view> expectedAssociatedPath;
      std::optional<std::wstring_view> expectedRealOpenedPath;
    } nameAssociationTestRecords[] = {
        //
        // None
        //
        // Regardless of which files are tried and which ultimately succeeds, no name association
        // should happen.
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::None,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kUnredirectedPath,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::None,
         .tryFilesTestInput = ETryFiles::RedirectedFirst,
         .pathThatShouldSucceed = kUnredirectedPath,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::None,
         .tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .pathThatShouldSucceed = kNoPathShouldSucceed,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        //
        // WhicheverWasSuccessful
        //
        // If the file operation is successful (signalled in the test record via the
        // `pathThatShouldSucceed` field) then whichever path succeeded is expected to be associated
        // with the newly-opened file handle.
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kNoPathShouldSucceed,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::RedirectedFirst,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kRedirectedPath,
         .expectedRealOpenedPath = kRedirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .pathThatShouldSucceed = kRedirectedPath,
         .expectedAssociatedPath = kRedirectedPath,
         .expectedRealOpenedPath = kRedirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::WhicheverWasSuccessful,
         .tryFilesTestInput = ETryFiles::RedirectedFirst,
         .pathThatShouldSucceed = kUnredirectedPath,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        //
        // Unredirected
        //
        // If the file operation is successful (signalled in the test record via the
        // `pathThatShouldSucceed` field) then the unredirected path should be associated with the
        // newly-opened file handle. However, on failure, there should be no association. The first
        // test record in this section is the failure case, and all others are success cases.
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Unredirected,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kNoPathShouldSucceed,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Unredirected,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Unredirected,
         .tryFilesTestInput = ETryFiles::RedirectedFirst,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kRedirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Unredirected,
         .tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .pathThatShouldSucceed = kRedirectedPath,
         .expectedAssociatedPath = kUnredirectedPath,
         .expectedRealOpenedPath = kRedirectedPath},
        //
        // Redirected
        //
        // If the file operation is successful (signalled in the test record via the
        // `pathThatShouldSucceed` field) then the redirected path should be associated with the
        // newly-opened file handle. However, on failure, there should be no association. The first
        // test record in this section is the failure case, and all others are success cases.
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Redirected,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kNoPathShouldSucceed,
         .expectedAssociatedPath = kNoPathShouldBeStored,
         .expectedRealOpenedPath = kNoPathShouldBeStored},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Redirected,
         .tryFilesTestInput = ETryFiles::UnredirectedOnly,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kRedirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Redirected,
         .tryFilesTestInput = ETryFiles::UnredirectedFirst,
         .pathThatShouldSucceed = kAnyPathShouldSucceed,
         .expectedAssociatedPath = kRedirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
        {.associateNameWithHandleTestInput = EAssociateNameWithHandle::Redirected,
         .tryFilesTestInput = ETryFiles::RedirectedFirst,
         .pathThatShouldSucceed = kUnredirectedPath,
         .expectedAssociatedPath = kRedirectedPath,
         .expectedRealOpenedPath = kUnredirectedPath},
    };

    for (const auto& nameAssociationTestRecord : nameAssociationTestRecords)
    {
      const FileOperationInstruction fileOperationInstructionTestInput(
          kRedirectedPath,
          nameAssociationTestRecord.tryFilesTestInput,
          ECreateDispositionPreference::NoPreference,
          nameAssociationTestRecord.associateNameWithHandleTestInput,
          {},
          L"");

      const HANDLE existingFileHandle = reinterpret_cast<HANDLE>(1084);

      OpenHandleStore openHandleStore;
      openHandleStore.InsertHandle(
          existingFileHandle, std::wstring(kInitialPath), std::wstring(kInitialPath));

      NTSTATUS newFileHandleResult = FilesystemExecutor::RenameByHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          existingFileHandle,
          fileRenameInformationUnredirectedPath.GetFileInformationStruct(),
          fileRenameInformationUnredirectedPath.GetFileInformationStructSizeBytes(),
          [&fileOperationInstructionTestInput](
              std::wstring_view, FileAccessMode, CreateDisposition) -> FileOperationInstruction
          {
            return fileOperationInstructionTestInput;
          },
          [&nameAssociationTestRecord, kAnyPathShouldSucceed](
              HANDLE handle, SFileRenameInformation& fileRenameInformation, ULONG) -> NTSTATUS
          {
            std::wstring_view renameTargetPath = std::wstring_view(
                fileRenameInformation.fileName,
                fileRenameInformation.fileNameLength / sizeof(wchar_t));

            if ((nameAssociationTestRecord.pathThatShouldSucceed == kAnyPathShouldSucceed) ||
                (nameAssociationTestRecord.pathThatShouldSucceed == renameTargetPath))
            {
              return NtStatus::kSuccess;
            }

            // A failure return code, indicating that the path was not found, is required to cause
            // the next preferred create disposition to be tried. Any other failure code is
            // correctly interpreted to indicate some other I/O error, which would just cause the
            // entire operation to fail with that as the result.
            return NtStatus::kObjectPathNotFound;
          });

      if (nameAssociationTestRecord.expectedAssociatedPath == kNoPathShouldBeStored)
      {
        if (nameAssociationTestRecord.pathThatShouldSucceed == kNoPathShouldSucceed)
        {
          // If the entire operation failed and no path is expected to be stored, the open handle
          // store should not have been touched. Therefore, the initial path should continue to be
          // associated with the existing file.

          auto maybeHandleData = openHandleStore.GetDataForHandle(existingFileHandle);
          TEST_ASSERT(true == maybeHandleData.has_value());
          TEST_ASSERT(kInitialPath == maybeHandleData->associatedPath);
          TEST_ASSERT(kInitialPath == maybeHandleData->realOpenedPath);
        }
        else
        {
          // If the entire operation succeeded and no path should be stored, then the open file
          // handle should have been cleared because the existing handle was erased.

          TEST_ASSERT(true == openHandleStore.Empty());
        }
      }
      else
      {
        auto maybeHandleData = openHandleStore.GetDataForHandle(existingFileHandle);
        TEST_ASSERT(true == maybeHandleData.has_value());

        std::wstring_view expectedAssociatedPath =
            *nameAssociationTestRecord.expectedAssociatedPath;
        std::wstring_view actualAssociatedPath = maybeHandleData->associatedPath;

        std::wstring_view expectedRealOpenedPath =
            *nameAssociationTestRecord.expectedRealOpenedPath;
        std::wstring_view actualRealOpenedPath = maybeHandleData->realOpenedPath;

        TEST_ASSERT(actualAssociatedPath == expectedAssociatedPath);
        TEST_ASSERT(actualRealOpenedPath == expectedRealOpenedPath);
      }
    }
  }

  // Verifies that a pre-operation request contained in a filesystem operation instruction is
  // executed correctly when renaming an existing file. The file operation instruction only contains
  // a pre-operation and nothing else, and this test case exercises an operation to ensure a path
  // hierarchy exists. The forms of instructions exercised by this test are not generally produced
  // by filesystem director objects but are intended specifically to exercise pre-operation
  // functionality.
  TEST_CASE(FilesystemExecutor_RenameByHandle_PreOperation_EnsurePathHierarchyExists)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kExtraPreOperationHierarchyToCreate =
        L"C:\\ExtraPreOperation\\Directory\\Hierarchy\\To\\Create";

    // This test case only exercises pre-operations, so no association should be created and hence
    // nothing should be added to the open handle store. The important parts here are the extra
    // pre-operation itself and the operand to that pre-operation.
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

    BytewiseDanglingFilenameStruct<SFileRenameInformation> fileRenameInformationUnredirectedPath(
        SFileRenameInformation{}, kUnredirectedPath);

    for (const auto& fileOperationInstructionToTry : fileOperationInstructionsToTry)
    {
      HANDLE unusedHandleValue = NULL;

      MockFilesystemOperations mockFilesystem;
      OpenHandleStore openHandleStore;

      bool instructionSourceWasInvoked = false;

      // Pre-operation should not have been executed yet because the filesystem executor function
      // was not yet invoked.
      TEST_ASSERT(false == mockFilesystem.IsDirectory(kExtraPreOperationHierarchyToCreate));

      FilesystemExecutor::RenameByHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          unusedHandleValue,
          fileRenameInformationUnredirectedPath.GetFileInformationStruct(),
          fileRenameInformationUnredirectedPath.GetFileInformationStructSizeBytes(),
          [kUnredirectedPath, &fileOperationInstructionToTry, &instructionSourceWasInvoked](
              std::wstring_view, FileAccessMode, CreateDisposition) -> FileOperationInstruction
          {
            instructionSourceWasInvoked = true;
            return fileOperationInstructionToTry;
          },
          [&mockFilesystem, &fileRenameInformationUnredirectedPath](
              HANDLE, SFileRenameInformation&, ULONG) -> NTSTATUS
          {
            // Checking here for the completion of the pre-operation ensures that it was done prior
            // to the underlying system call being invoked.
            TEST_ASSERT(true == mockFilesystem.IsDirectory(kExtraPreOperationHierarchyToCreate));
            return NtStatus::kSuccess;
          });

      TEST_ASSERT(true == instructionSourceWasInvoked);
      TEST_ASSERT(true == openHandleStore.Empty());
    }
  }

  // Verifies that a previously-interesting file that is renamed to a path that is not interesting
  // is erased from the open handle store. This is very similar to the try files order test case,
  // except this is a special case whereby the instruction contains no redirected filename
  // whatsoever.
  TEST_CASE(FilesystemExecutor_RenameByHandle_PreviouslyInterestingFileErased)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";

    const HANDLE existingFileHandle = reinterpret_cast<HANDLE>(3386);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        existingFileHandle, std::wstring(kUnredirectedPath), std::wstring(kUnredirectedPath));

    BytewiseDanglingFilenameStruct<SFileRenameInformation> fileRenameInformationUnredirectedPath(
        SFileRenameInformation{}, kUnredirectedPath);

    FilesystemExecutor::RenameByHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        existingFileHandle,
        fileRenameInformationUnredirectedPath.GetFileInformationStruct(),
        fileRenameInformationUnredirectedPath.GetFileInformationStructSizeBytes(),
        [](std::wstring_view, FileAccessMode, CreateDisposition) -> FileOperationInstruction
        {
          return FileOperationInstruction::NoRedirectionOrInterception();
        },
        [](HANDLE, SFileRenameInformation&, ULONG) -> NTSTATUS
        {
          return NtStatus::kSuccess;
        });

    TEST_ASSERT(true == openHandleStore.Empty());
  }
} // namespace PathwinderTest
