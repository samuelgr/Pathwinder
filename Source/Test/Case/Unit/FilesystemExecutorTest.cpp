/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file FilesystemExecutorTest.cpp
 *   Unit tests for all functionality related to executing application-requested filesystem
 *   operations under the control of filesystem instructions.
 **************************************************************************************************/

#include "TestCase.h"

#include "FilesystemExecutor.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include <Infra/ArrayList.h>
#include <Infra/TemporaryBuffer.h>
#include <Infra/ValueOrError.h>

#include "ApiWindows.h"
#include "FilesystemDirector.h"
#include "FilesystemInstruction.h"
#include "FilesystemRule.h"
#include "MockDirectoryOperationQueue.h"
#include "MockFilesystemOperations.h"
#include "OpenHandleStore.h"
#include "Strings.h"

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  /// Function request identifier to be passed to all filesystem executor functions when they are
  /// invoked for testing.
  static constexpr unsigned int kFunctionRequestIdentifier = 0;

  /// Record type for viewing and comparing in-progress directory enumeration state data structures.
  /// Fields are as in the original structure but modified to avoid ownership. Intended for
  /// comparing real records to one another during tests.
  struct SDirectoryEnumerationStateSnapshot
  {
    IDirectoryOperationQueue* queue;
    FileInformationStructLayout fileInformationStructLayout;
    std::set<std::wstring, Strings::CaseInsensitiveLessThanComparator<wchar_t>> enumeratedFilenames;
    bool isFirstInvocation;

    inline SDirectoryEnumerationStateSnapshot(
        const OpenHandleStore::SInProgressDirectoryEnumeration& inProgressDirectoryEnumeration)
        : queue(inProgressDirectoryEnumeration.queue.get()),
          fileInformationStructLayout(inProgressDirectoryEnumeration.fileInformationStructLayout),
          enumeratedFilenames(inProgressDirectoryEnumeration.enumeratedFilenames),
          isFirstInvocation(inProgressDirectoryEnumeration.isFirstInvocation)
    {}

    bool operator==(const SDirectoryEnumerationStateSnapshot& other) const = default;

    static SDirectoryEnumerationStateSnapshot GetForHandle(
        HANDLE handle, OpenHandleStore& openHandleStore)
    {
      return *(*(openHandleStore.GetDataForHandle(handle)->directoryEnumeration));
    }
  };

  /// Determines if a directory operation queue object is of the specified type.
  /// @tparam DirectoryOperationQueueType Type of directory operation queue to check for a match
  /// with the parameter object.
  /// @param [in] queueToCheck Directory operation queue to check.
  /// @return `true` if the directory operation queue to check is type-compatible with the specified
  /// type.
  template <typename DirectoryOperationQueueType> static bool DirectoryOperationQueueTypeIs(
      const IDirectoryOperationQueue& queueToCheck)
  {
    return (nullptr != dynamic_cast<const DirectoryOperationQueueType*>(&queueToCheck));
  }

  /// Initializes an I/O status block before it is used and updated in tests.
  /// @return Status block object initialized with values that should not persist after the
  /// invocation of a filesystem executor function.
  static IO_STATUS_BLOCK InitializeIoStatusBlock(void)
  {
    return {.Status = static_cast<NTSTATUS>(0xcdcdcdcd), .Information = 0xefefefef};
  }

  /// Verifies that the specified queue was created as an enumeration queue object and matches the
  /// specifications determined by the other parameters.
  /// @param [in] queueToCheck Queue object to be checked.
  /// @param [in] mockFilesystem Mock filesystem controller object that will be queried as part of
  /// the verification process.
  /// @param [in] matchInstruction File matching instruction that should have been used to create
  /// the enumeration queue.
  /// @param [in] absoluteDirectoryPath Absolute directory path of the directory that the
  /// enumeration queue is enumerating.
  /// @param [in] fileInformationClass File information class that the queue will use when querying
  /// the system for directory contents.
  /// @param [in] filePattern File matching pattern that the queue is expected to use to filter
  /// outputs of the enumeration.
  static void VerifyIsEnumerationQueueAndMatchesSpec(
      const IDirectoryOperationQueue* queueToCheck,
      const MockFilesystemOperations& mockFilesystem,
      DirectoryEnumerationInstruction::SingleDirectoryEnumeration matchInstruction,
      std::wstring_view absoluteDirectoryPath,
      FILE_INFORMATION_CLASS fileInformationClass,
      std::wstring_view filePattern = std::wstring_view())
  {
    TEST_ASSERT(DirectoryOperationQueueTypeIs<EnumerationQueue>(*queueToCheck));
    const EnumerationQueue& enumerationQueueToCheck =
        *(static_cast<const EnumerationQueue*>(queueToCheck));

    HANDLE enumeratedDirectoryHandle = enumerationQueueToCheck.GetDirectoryHandle();

    TEST_ASSERT(enumerationQueueToCheck.GetMatchInstruction() == matchInstruction);
    TEST_ASSERT(
        mockFilesystem.GetPathFromHandle(enumeratedDirectoryHandle) == absoluteDirectoryPath);
    TEST_ASSERT(enumerationQueueToCheck.GetFileInformationClass() == fileInformationClass);
    TEST_ASSERT(mockFilesystem.GetFilePatternForDirectoryEnumeration(enumeratedDirectoryHandle)
                    .has_value());
    TEST_ASSERT(Strings::EqualsCaseInsensitive(
        *mockFilesystem.GetFilePatternForDirectoryEnumeration(enumeratedDirectoryHandle),
        filePattern));
  }

  /// Verifies that the specified queue was created as a name insertion object and matches the
  /// specifications determined by the other parameters.
  /// @param [in] queueToCheck Queue object to be checked.
  /// @param [in] nameInsertionInstructions Name insertion instructions that the queue should use to
  /// generate enumeration output.
  /// @param [in] fileInformationClass File information class that the queue will use when querying
  /// the system for directory contents.
  /// @param [in] filePattern File matching pattern that the queue is expected to use to filter
  /// outputs of the enumeration.
  static void VerifyIsNameInsertionQueueAndMatchesSpec(
      const IDirectoryOperationQueue* queueToCheck,
      const Infra::TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion>&
          nameInsertionInstructions,
      FILE_INFORMATION_CLASS fileInformationClass,
      std::wstring_view filePattern = std::wstring_view())
  {
    TEST_ASSERT(DirectoryOperationQueueTypeIs<NameInsertionQueue>(*queueToCheck));
    const NameInsertionQueue& nameInsertionQueueToCheck =
        *(static_cast<const NameInsertionQueue*>(queueToCheck));

    TEST_ASSERT(
        nameInsertionQueueToCheck.GetNameInsertionInstructions() == nameInsertionInstructions);
    TEST_ASSERT(nameInsertionQueueToCheck.GetFileInformationClass() == fileInformationClass);
    TEST_ASSERT(
        Strings::EqualsCaseInsensitive(nameInsertionQueueToCheck.GetFilePattern(), filePattern));
  }

  /// Copies a string to the dangling filename field of a file name information structure. Intended
  /// to be used to implement tests that query for file name information. Updates the length field
  /// and additionally honors buffer size constraints.
  /// @param [in] stringToCopy String to be written to the file name information structure.
  /// @param [out] fileNameInformation File name information structure to be filled.
  /// @param [in] fileNameInformationBufferCapacity Buffer capacity, in bytes, of the file name
  /// information structure, including its dangling filename field.
  static NTSTATUS CopyStringToFileNameInformation(
      std::wstring_view stringToCopy,
      SFileNameInformation* fileNameInformation,
      size_t fileNameInformationBufferCapacity)
  {
    const size_t characterSpaceAvailable = (static_cast<size_t>(fileNameInformationBufferCapacity) -
                                            offsetof(SFileNameInformation, fileName)) /
        sizeof(wchar_t);

    const size_t characterSpaceRequired = stringToCopy.length();

    std::wmemcpy(
        fileNameInformation->fileName,
        stringToCopy.data(),
        std::min(characterSpaceRequired, characterSpaceAvailable));

    fileNameInformation->fileNameLength =
        static_cast<ULONG>(characterSpaceRequired * sizeof(wchar_t));

    if (characterSpaceRequired <= characterSpaceAvailable)
      return NtStatus::kSuccess;
    else
      return NtStatus::kBufferOverflow;
  }

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

    const HANDLE directoryHandle = mockFilesystem.Open(
        kDirectoryName, MockFilesystemOperations::EOpenHandleMode::Asynchronous);
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

    const HANDLE directoryHandle = mockFilesystem.Open(kDirectoryName);
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

  // Verifies the nominal case of directory enumeration advancement whereby file information
  // structures are copied to a buffer large enough to hold all of them. Checks that the file
  // information structures are copied correctly and that all of them are copied.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationAdvance_Nominal)
  {
    constexpr std::wstring_view kTestDirectory = L"X:\\Test\\Directory";

    constexpr FILE_INFORMATION_CLASS kFileNamesInformationClass =
        SFileNamesInformation::kFileInformationClass;
    const FileInformationStructLayout fileNameStructLayout =
        *FileInformationStructLayout::LayoutForFileInformationClass(kFileNamesInformationClass);

    const MockDirectoryOperationQueue::TFileNamesToEnumerate expectedEnumeratedFilenames = {
        L"file1.txt", L"00file2.txt", L"FILE3.log", L"app1.exe", L"binfile.bin"};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kTestDirectory);

    const HANDLE directoryHandle = mockFilesystem.Open(kTestDirectory);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kTestDirectory), std::wstring(kTestDirectory));
    openHandleStore.AssociateDirectoryEnumerationState(
        directoryHandle,
        std::make_unique<MockDirectoryOperationQueue>(
            fileNameStructLayout,
            MockDirectoryOperationQueue::TFileNamesToEnumerate(expectedEnumeratedFilenames)),
        fileNameStructLayout);

    Infra::TemporaryVector<uint8_t> enumerationOutputBytes;
    IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();

    const NTSTATUS expectedReturnCode = NtStatus::kSuccess;
    const NTSTATUS actualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        nullptr,
        nullptr,
        nullptr,
        &ioStatusBlock,
        enumerationOutputBytes.Data(),
        enumerationOutputBytes.CapacityBytes(),
        kFileNamesInformationClass,
        0,
        nullptr);

    TEST_ASSERT(actualReturnCode == expectedReturnCode);
    TEST_ASSERT(ioStatusBlock.Status == expectedReturnCode);

    MockDirectoryOperationQueue::TFileNamesToEnumerate actualEnumeratedFilenames;
    const unsigned int expectedBytesWritten = static_cast<unsigned int>(ioStatusBlock.Information);
    unsigned int actualBytesWritten = 0;

    size_t enumeratedOutputBytePosition = 0;
    while (enumeratedOutputBytePosition <
           std::min(expectedBytesWritten, enumerationOutputBytes.CapacityBytes()))
    {
      const SFileNamesInformation* enumeratedFileInformation =
          reinterpret_cast<const SFileNamesInformation*>(
              &enumerationOutputBytes.Data()[enumeratedOutputBytePosition]);

      actualEnumeratedFilenames.emplace(
          fileNameStructLayout.ReadFileName(enumeratedFileInformation));
      actualBytesWritten += fileNameStructLayout.SizeOfStruct(enumeratedFileInformation);

      if (0 == enumeratedFileInformation->nextEntryOffset) break;
      enumeratedOutputBytePosition +=
          static_cast<size_t>(enumeratedFileInformation->nextEntryOffset);
    }

    TEST_ASSERT(actualEnumeratedFilenames == expectedEnumeratedFilenames);
    TEST_ASSERT(actualBytesWritten == expectedBytesWritten);
  }

  // Verifies the nominal case of directory enumeration advancement whereby file information
  // structures are copied to a buffer large enough to hold all of them. Checks that the file
  // information structures are copied correctly and that all of them are copied. This version of
  // the test case uses asynchronous directory enumeration synchronized by an event.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationAdvance_NominalAsyncEvent)
  {
    constexpr std::wstring_view kTestDirectory = L"X:\\Test\\Directory";

    constexpr FILE_INFORMATION_CLASS kFileNamesInformationClass =
        SFileNamesInformation::kFileInformationClass;
    const FileInformationStructLayout fileNameStructLayout =
        *FileInformationStructLayout::LayoutForFileInformationClass(kFileNamesInformationClass);

    const MockDirectoryOperationQueue::TFileNamesToEnumerate expectedEnumeratedFilenames = {
        L"file1.txt", L"00file2.txt", L"FILE3.log", L"app1.exe", L"binfile.bin"};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kTestDirectory);

    const HANDLE directoryHandle = mockFilesystem.Open(
        kTestDirectory, MockFilesystemOperations::EOpenHandleMode::Asynchronous);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kTestDirectory), std::wstring(kTestDirectory));
    openHandleStore.AssociateDirectoryEnumerationState(
        directoryHandle,
        std::make_unique<MockDirectoryOperationQueue>(
            fileNameStructLayout,
            MockDirectoryOperationQueue::TFileNamesToEnumerate(expectedEnumeratedFilenames)),
        fileNameStructLayout);

    Infra::TemporaryVector<uint8_t> enumerationOutputBytes;
    IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();

    HANDLE syncEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    TEST_ASSERT(nullptr != syncEvent);

    const NTSTATUS expectedReturnCode = NtStatus::kPending;
    const NTSTATUS actualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        syncEvent,
        nullptr,
        nullptr,
        &ioStatusBlock,
        enumerationOutputBytes.Data(),
        enumerationOutputBytes.CapacityBytes(),
        kFileNamesInformationClass,
        0,
        nullptr);

    TEST_ASSERT(actualReturnCode == expectedReturnCode);

    TEST_ASSERT(WAIT_OBJECT_0 == WaitForSingleObject(syncEvent, 100));

    const NTSTATUS expectedResult = NtStatus::kSuccess;
    const NTSTATUS actualResult = ioStatusBlock.Status;
    TEST_ASSERT(actualResult == expectedResult);

    MockDirectoryOperationQueue::TFileNamesToEnumerate actualEnumeratedFilenames;
    const unsigned int expectedBytesWritten = static_cast<unsigned int>(ioStatusBlock.Information);
    unsigned int actualBytesWritten = 0;

    size_t enumeratedOutputBytePosition = 0;
    while (enumeratedOutputBytePosition <
           std::min(expectedBytesWritten, enumerationOutputBytes.CapacityBytes()))
    {
      const SFileNamesInformation* enumeratedFileInformation =
          reinterpret_cast<const SFileNamesInformation*>(
              &enumerationOutputBytes.Data()[enumeratedOutputBytePosition]);

      actualEnumeratedFilenames.emplace(
          fileNameStructLayout.ReadFileName(enumeratedFileInformation));
      actualBytesWritten += fileNameStructLayout.SizeOfStruct(enumeratedFileInformation);

      if (0 == enumeratedFileInformation->nextEntryOffset) break;
      enumeratedOutputBytePosition +=
          static_cast<size_t>(enumeratedFileInformation->nextEntryOffset);
    }

    TEST_ASSERT(actualEnumeratedFilenames == expectedEnumeratedFilenames);
    TEST_ASSERT(actualBytesWritten == expectedBytesWritten);
  }

  // Verifies the nominal case of directory enumeration advancement whereby file information
  // structures are copied to a buffer large enough to hold all of them. Checks that the file
  // information structures are copied correctly and that all of them are copied. This version of
  // the test case uses an APC routine.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationAdvance_NominalAsyncApcRoutine)
  {
    struct STestApcData
    {
      DWORD threadId;
      PIO_STATUS_BLOCK ioStatusBlockPtr;

      inline bool operator==(const STestApcData& other) const = default;
    };

    constexpr std::wstring_view kTestDirectory = L"X:\\Test\\Directory";

    constexpr FILE_INFORMATION_CLASS kFileNamesInformationClass =
        SFileNamesInformation::kFileInformationClass;
    const FileInformationStructLayout fileNameStructLayout =
        *FileInformationStructLayout::LayoutForFileInformationClass(kFileNamesInformationClass);

    const MockDirectoryOperationQueue::TFileNamesToEnumerate expectedEnumeratedFilenames = {
        L"file1.txt", L"00file2.txt", L"FILE3.log", L"app1.exe", L"binfile.bin"};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kTestDirectory);
    mockFilesystem.SetConfigAllowCloseInvalidHandle(true);

    const HANDLE directoryHandle = mockFilesystem.Open(
        kTestDirectory, MockFilesystemOperations::EOpenHandleMode::Asynchronous);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kTestDirectory), std::wstring(kTestDirectory));
    openHandleStore.AssociateDirectoryEnumerationState(
        directoryHandle,
        std::make_unique<MockDirectoryOperationQueue>(
            fileNameStructLayout,
            MockDirectoryOperationQueue::TFileNamesToEnumerate(expectedEnumeratedFilenames)),
        fileNameStructLayout);

    Infra::TemporaryVector<uint8_t> enumerationOutputBytes;
    IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();

    const STestApcData expectedApcData = {
        .threadId = GetCurrentThreadId(), .ioStatusBlockPtr = &ioStatusBlock};
    STestApcData actualApcData{};

    const NTSTATUS expectedReturnCode = NtStatus::kPending;
    const NTSTATUS actualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        nullptr,
        [](PVOID context, PIO_STATUS_BLOCK ioStatusBlock, ULONG reserved) -> void
        {
          *reinterpret_cast<STestApcData*>(context) = {
              .threadId = GetCurrentThreadId(), .ioStatusBlockPtr = ioStatusBlock};
        },
        &actualApcData,
        &ioStatusBlock,
        enumerationOutputBytes.Data(),
        enumerationOutputBytes.CapacityBytes(),
        kFileNamesInformationClass,
        0,
        nullptr);

    TEST_ASSERT(actualReturnCode == expectedReturnCode);

    TEST_ASSERT(WAIT_IO_COMPLETION == SleepEx(10, TRUE));
    TEST_ASSERT(actualApcData == expectedApcData);

    const NTSTATUS expectedResult = NtStatus::kSuccess;
    const NTSTATUS actualResult = ioStatusBlock.Status;
    TEST_ASSERT(actualResult == expectedResult);

    MockDirectoryOperationQueue::TFileNamesToEnumerate actualEnumeratedFilenames;
    const unsigned int expectedBytesWritten = static_cast<unsigned int>(ioStatusBlock.Information);
    unsigned int actualBytesWritten = 0;

    size_t enumeratedOutputBytePosition = 0;
    while (enumeratedOutputBytePosition <
           std::min(expectedBytesWritten, enumerationOutputBytes.CapacityBytes()))
    {
      const SFileNamesInformation* enumeratedFileInformation =
          reinterpret_cast<const SFileNamesInformation*>(
              &enumerationOutputBytes.Data()[enumeratedOutputBytePosition]);

      actualEnumeratedFilenames.emplace(
          fileNameStructLayout.ReadFileName(enumeratedFileInformation));
      actualBytesWritten += fileNameStructLayout.SizeOfStruct(enumeratedFileInformation);

      if (0 == enumeratedFileInformation->nextEntryOffset) break;
      enumeratedOutputBytePosition +=
          static_cast<size_t>(enumeratedFileInformation->nextEntryOffset);
    }

    TEST_ASSERT(actualEnumeratedFilenames == expectedEnumeratedFilenames);
    TEST_ASSERT(actualBytesWritten == expectedBytesWritten);
  }

  // Verifies that, after all files are enumerated, subsequent invocations should result in no bytes
  // being written and a status code indicating no more files being available.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationAdvance_IndicateNoMoreFiles)
  {
    constexpr std::wstring_view kTestDirectory = L"X:\\Test\\Directory";

    constexpr FILE_INFORMATION_CLASS kFileNamesInformationClass =
        SFileNamesInformation::kFileInformationClass;
    const FileInformationStructLayout fileNameStructLayout =
        *FileInformationStructLayout::LayoutForFileInformationClass(kFileNamesInformationClass);

    const MockDirectoryOperationQueue::TFileNamesToEnumerate expectedEnumeratedFilenames = {
        L"file1.txt", L"00file2.txt", L"FILE3.log", L"app1.exe", L"binfile.bin"};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kTestDirectory);

    const HANDLE directoryHandle = mockFilesystem.Open(kTestDirectory);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kTestDirectory), std::wstring(kTestDirectory));
    openHandleStore.AssociateDirectoryEnumerationState(
        directoryHandle,
        std::make_unique<MockDirectoryOperationQueue>(
            fileNameStructLayout,
            MockDirectoryOperationQueue::TFileNamesToEnumerate(expectedEnumeratedFilenames)),
        fileNameStructLayout);

    Infra::TemporaryVector<uint8_t> enumerationOutputBytes;
    IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();

    const NTSTATUS expectedReturnCode = NtStatus::kSuccess;
    const NTSTATUS actualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        nullptr,
        nullptr,
        nullptr,
        &ioStatusBlock,
        enumerationOutputBytes.Data(),
        enumerationOutputBytes.CapacityBytes(),
        kFileNamesInformationClass,
        0,
        nullptr);

    TEST_ASSERT(actualReturnCode == expectedReturnCode);
    TEST_ASSERT(ioStatusBlock.Status == expectedReturnCode);

    // The function is expected to indicate no more files are available no matter how many times it
    // is invoked after the enumeration finishes.
    for (int i = 0; i < 10; ++i)
    {
      IO_STATUS_BLOCK finalIoStatusBlock = InitializeIoStatusBlock();
      const NTSTATUS finalExpectedReturnCode = NtStatus::kNoMoreFiles;
      const NTSTATUS finalActualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          directoryHandle,
          nullptr,
          nullptr,
          nullptr,
          &finalIoStatusBlock,
          enumerationOutputBytes.Data(),
          enumerationOutputBytes.CapacityBytes(),
          kFileNamesInformationClass,
          0,
          nullptr);

      TEST_ASSERT(finalActualReturnCode == finalExpectedReturnCode);
      TEST_ASSERT(finalIoStatusBlock.Status == finalExpectedReturnCode);

      const unsigned int finalExpectedBytesWritten = 0;
      const unsigned int finalActualBytesWritten =
          static_cast<unsigned int>(finalIoStatusBlock.Information);
      TEST_ASSERT(finalActualBytesWritten == finalExpectedBytesWritten);
    }
  }

  // Verifies that, if no files match the specified directory enumeration query, on first invocation
  // the return code is that no files match, and on subsequent invocations the return code indicates
  // no more files available.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationAdvance_IndicateNoMatchingFilesThenNoMoreFiles)
  {
    constexpr std::wstring_view kTestDirectory = L"X:\\Test\\Directory";

    constexpr FILE_INFORMATION_CLASS kFileNamesInformationClass =
        SFileNamesInformation::kFileInformationClass;
    const FileInformationStructLayout fileNameStructLayout =
        *FileInformationStructLayout::LayoutForFileInformationClass(kFileNamesInformationClass);

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kTestDirectory);

    const HANDLE directoryHandle = mockFilesystem.Open(kTestDirectory);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kTestDirectory), std::wstring(kTestDirectory));
    openHandleStore.AssociateDirectoryEnumerationState(
        directoryHandle,
        std::make_unique<MockDirectoryOperationQueue>(NtStatus::kNoMoreFiles),
        fileNameStructLayout);

    Infra::TemporaryVector<uint8_t> enumerationOutputBytes;
    IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();

    const NTSTATUS expectedReturnCode = NtStatus::kNoSuchFile;
    const NTSTATUS actualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        nullptr,
        nullptr,
        nullptr,
        &ioStatusBlock,
        enumerationOutputBytes.Data(),
        enumerationOutputBytes.CapacityBytes(),
        kFileNamesInformationClass,
        0,
        nullptr);

    TEST_ASSERT(actualReturnCode == expectedReturnCode);
    TEST_ASSERT(ioStatusBlock.Status == expectedReturnCode);

    const unsigned int expectedBytesWritten = 0;
    const unsigned int actualBytesWritten = static_cast<unsigned int>(ioStatusBlock.Information);
    TEST_ASSERT(actualBytesWritten == expectedBytesWritten);

    // The function is expected to indicate no more files are available no matter how many times it
    // is invoked after the enumeration finishes.
    for (int i = 0; i < 10; ++i)
    {
      IO_STATUS_BLOCK finalIoStatusBlock = InitializeIoStatusBlock();
      const NTSTATUS finalExpectedReturnCode = NtStatus::kNoMoreFiles;
      const NTSTATUS finalActualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          directoryHandle,
          nullptr,
          nullptr,
          nullptr,
          &finalIoStatusBlock,
          enumerationOutputBytes.Data(),
          enumerationOutputBytes.CapacityBytes(),
          kFileNamesInformationClass,
          0,
          nullptr);

      TEST_ASSERT(finalActualReturnCode == finalExpectedReturnCode);
      TEST_ASSERT(finalIoStatusBlock.Status == finalExpectedReturnCode);

      const unsigned int finalExpectedBytesWritten = 0;
      const unsigned int finalActualBytesWritten =
          static_cast<unsigned int>(finalIoStatusBlock.Information);
      TEST_ASSERT(finalActualBytesWritten == finalExpectedBytesWritten);
    }
  }

  // Verifies that, after all files are enumerated, restarting the enumeration results in them being
  // properly enumerated all over again.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationAdvance_RestartEnumeration)
  {
    constexpr std::wstring_view kTestDirectory = L"X:\\Test\\Directory";

    constexpr FILE_INFORMATION_CLASS kFileNamesInformationClass =
        SFileNamesInformation::kFileInformationClass;
    const FileInformationStructLayout fileNameStructLayout =
        *FileInformationStructLayout::LayoutForFileInformationClass(kFileNamesInformationClass);

    const MockDirectoryOperationQueue::TFileNamesToEnumerate expectedEnumeratedFilenames = {
        L"file1.txt", L"00file2.txt", L"FILE3.log", L"app1.exe", L"binfile.bin"};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kTestDirectory);

    const HANDLE directoryHandle = mockFilesystem.Open(kTestDirectory);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kTestDirectory), std::wstring(kTestDirectory));
    openHandleStore.AssociateDirectoryEnumerationState(
        directoryHandle,
        std::make_unique<MockDirectoryOperationQueue>(
            fileNameStructLayout,
            MockDirectoryOperationQueue::TFileNamesToEnumerate(expectedEnumeratedFilenames)),
        fileNameStructLayout);

    Infra::TemporaryVector<uint8_t> enumerationOutputBytes;
    IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();

    const NTSTATUS expectedReturnCode = NtStatus::kSuccess;
    const NTSTATUS actualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        nullptr,
        nullptr,
        nullptr,
        &ioStatusBlock,
        enumerationOutputBytes.Data(),
        enumerationOutputBytes.CapacityBytes(),
        kFileNamesInformationClass,
        0,
        nullptr);

    TEST_ASSERT(actualReturnCode == expectedReturnCode);
    TEST_ASSERT(ioStatusBlock.Status == expectedReturnCode);

    IO_STATUS_BLOCK finalIoStatusBlock = InitializeIoStatusBlock();
    const NTSTATUS finalExpectedReturnCode = NtStatus::kSuccess;
    const NTSTATUS finalActualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        nullptr,
        nullptr,
        nullptr,
        &finalIoStatusBlock,
        enumerationOutputBytes.Data(),
        enumerationOutputBytes.CapacityBytes(),
        kFileNamesInformationClass,
        SL_RESTART_SCAN,
        nullptr);

    TEST_ASSERT(finalActualReturnCode == finalExpectedReturnCode);
    TEST_ASSERT(finalIoStatusBlock.Status == finalExpectedReturnCode);

    // Because the preceding enumeration restarted the query, all of the files should be enumerated
    // once again. The same checks below apply as in the nominal test case.

    MockDirectoryOperationQueue::TFileNamesToEnumerate actualEnumeratedFilenames;
    const unsigned int expectedBytesWritten = static_cast<unsigned int>(ioStatusBlock.Information);
    unsigned int actualBytesWritten = 0;

    size_t enumeratedOutputBytePosition = 0;
    while (enumeratedOutputBytePosition <
           std::min(expectedBytesWritten, enumerationOutputBytes.CapacityBytes()))
    {
      const SFileNamesInformation* enumeratedFileInformation =
          reinterpret_cast<const SFileNamesInformation*>(
              &enumerationOutputBytes.Data()[enumeratedOutputBytePosition]);

      actualEnumeratedFilenames.emplace(
          fileNameStructLayout.ReadFileName(enumeratedFileInformation));
      actualBytesWritten += fileNameStructLayout.SizeOfStruct(enumeratedFileInformation);

      if (0 == enumeratedFileInformation->nextEntryOffset) break;
      enumeratedOutputBytePosition +=
          static_cast<size_t>(enumeratedFileInformation->nextEntryOffset);
    }

    TEST_ASSERT(actualEnumeratedFilenames == expectedEnumeratedFilenames);
    TEST_ASSERT(actualBytesWritten == expectedBytesWritten);
  }

  // Verifies that queues are properly restarted with a new file pattern if the application
  // specifies that the scan is to be restarted.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationAdvance_RestartWithQueryFilePattren)
  {
    constexpr std::wstring_view kTestDirectory = L"X:\\Test\\Directory";
    constexpr std::wstring_view kTestFilePattern = L"file*.txt";

    constexpr FILE_INFORMATION_CLASS kFileNamesInformationClass =
        SFileNamesInformation::kFileInformationClass;
    const FileInformationStructLayout fileNameStructLayout =
        *FileInformationStructLayout::LayoutForFileInformationClass(kFileNamesInformationClass);

    const MockDirectoryOperationQueue::TFileNamesToEnumerate expectedEnumeratedFilenames = {
        L"file1.txt", L"00file2.txt", L"FILE3.log", L"app1.exe", L"binfile.bin"};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kTestDirectory);

    const HANDLE directoryHandle = mockFilesystem.Open(kTestDirectory);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kTestDirectory), std::wstring(kTestDirectory));
    openHandleStore.AssociateDirectoryEnumerationState(
        directoryHandle,
        std::make_unique<MockDirectoryOperationQueue>(
            fileNameStructLayout,
            MockDirectoryOperationQueue::TFileNamesToEnumerate(expectedEnumeratedFilenames)),
        fileNameStructLayout);

    const MockDirectoryOperationQueue* const directoryOperationQueue =
        static_cast<const MockDirectoryOperationQueue*>(
            SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore)
                .queue);

    Infra::TemporaryVector<uint8_t> enumerationOutputBytes;
    IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();
    UNICODE_STRING queryFilePatternUnicodeString =
        Strings::NtConvertStringViewToUnicodeString(kTestFilePattern);

    TEST_ASSERT(directoryOperationQueue->GetLastRestartedQueryFilePattern().empty());

    FilesystemExecutor::DirectoryEnumerationAdvance(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        nullptr,
        nullptr,
        nullptr,
        &ioStatusBlock,
        enumerationOutputBytes.Data(),
        enumerationOutputBytes.CapacityBytes(),
        kFileNamesInformationClass,
        SL_RESTART_SCAN,
        &queryFilePatternUnicodeString);

    TEST_ASSERT(directoryOperationQueue->GetLastRestartedQueryFilePattern() == kTestFilePattern);
  }

  // Verifies that files enumerated are deduplicated. The output should be the same as in the
  // nominal case, but in this situation the input is three queues all providing identical file
  // names.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationAdvance_Deduplicate)
  {
    constexpr std::wstring_view kTestDirectory = L"X:\\Test\\Directory";

    constexpr FILE_INFORMATION_CLASS kFileNamesInformationClass =
        SFileNamesInformation::kFileInformationClass;
    const FileInformationStructLayout fileNameStructLayout =
        *FileInformationStructLayout::LayoutForFileInformationClass(kFileNamesInformationClass);

    const MockDirectoryOperationQueue::TFileNamesToEnumerate expectedEnumeratedFilenames = {
        L"file1.txt", L"00file2.txt", L"FILE3.log", L"app1.exe", L"binfile.bin"};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kTestDirectory);

    const HANDLE directoryHandle = mockFilesystem.Open(kTestDirectory);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kTestDirectory), std::wstring(kTestDirectory));
    openHandleStore.AssociateDirectoryEnumerationState(
        directoryHandle,
        std::make_unique<MergedFileInformationQueue>(MergedFileInformationQueue::Create<3>({
            std::make_unique<MockDirectoryOperationQueue>(
                fileNameStructLayout,
                MockDirectoryOperationQueue::TFileNamesToEnumerate(expectedEnumeratedFilenames)),
            std::make_unique<MockDirectoryOperationQueue>(
                fileNameStructLayout,
                MockDirectoryOperationQueue::TFileNamesToEnumerate(expectedEnumeratedFilenames)),
            std::make_unique<MockDirectoryOperationQueue>(
                fileNameStructLayout,
                MockDirectoryOperationQueue::TFileNamesToEnumerate(expectedEnumeratedFilenames)),
        })),
        fileNameStructLayout);

    Infra::TemporaryVector<uint8_t> enumerationOutputBytes;
    IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();

    const NTSTATUS expectedReturnCode = NtStatus::kSuccess;
    const NTSTATUS actualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        nullptr,
        nullptr,
        nullptr,
        &ioStatusBlock,
        enumerationOutputBytes.Data(),
        enumerationOutputBytes.CapacityBytes(),
        kFileNamesInformationClass,
        0,
        nullptr);

    TEST_ASSERT(actualReturnCode == expectedReturnCode);
    TEST_ASSERT(ioStatusBlock.Status == expectedReturnCode);

    MockDirectoryOperationQueue::TFileNamesToEnumerate actualEnumeratedFilenames;
    const unsigned int expectedBytesWritten = static_cast<unsigned int>(ioStatusBlock.Information);
    unsigned int actualBytesWritten = 0;

    size_t enumeratedOutputBytePosition = 0;
    while (enumeratedOutputBytePosition <
           std::min(expectedBytesWritten, enumerationOutputBytes.CapacityBytes()))
    {
      const SFileNamesInformation* enumeratedFileInformation =
          reinterpret_cast<const SFileNamesInformation*>(
              &enumerationOutputBytes.Data()[enumeratedOutputBytePosition]);

      actualEnumeratedFilenames.emplace(
          fileNameStructLayout.ReadFileName(enumeratedFileInformation));
      actualBytesWritten += fileNameStructLayout.SizeOfStruct(enumeratedFileInformation);

      if (0 == enumeratedFileInformation->nextEntryOffset) break;
      enumeratedOutputBytePosition +=
          static_cast<size_t>(enumeratedFileInformation->nextEntryOffset);
    }

    TEST_ASSERT(actualEnumeratedFilenames == expectedEnumeratedFilenames);
    TEST_ASSERT(actualBytesWritten == expectedBytesWritten);
  }

  // Verifies single-stepped directory enumeration advancement whereby one file information
  // structure is copied to the output buffer each invocation. Checks that the file information
  // structures are copied correctly and that all of them are copied.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationAdvance_SingleEntry)
  {
    constexpr std::wstring_view kTestDirectory = L"X:\\Test\\Directory";

    constexpr FILE_INFORMATION_CLASS kFileNamesInformationClass =
        SFileNamesInformation::kFileInformationClass;
    const FileInformationStructLayout fileNameStructLayout =
        *FileInformationStructLayout::LayoutForFileInformationClass(kFileNamesInformationClass);

    const MockDirectoryOperationQueue::TFileNamesToEnumerate expectedEnumeratedFilenames = {
        L"file1.txt", L"00file2.txt", L"FILE3.log", L"app1.exe", L"binfile.bin"};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kTestDirectory);

    const HANDLE directoryHandle = mockFilesystem.Open(kTestDirectory);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kTestDirectory), std::wstring(kTestDirectory));
    openHandleStore.AssociateDirectoryEnumerationState(
        directoryHandle,
        std::make_unique<MockDirectoryOperationQueue>(
            fileNameStructLayout,
            MockDirectoryOperationQueue::TFileNamesToEnumerate(expectedEnumeratedFilenames)),
        fileNameStructLayout);

    Infra::TemporaryVector<uint8_t> enumerationOutputBytes;
    MockDirectoryOperationQueue::TFileNamesToEnumerate actualEnumeratedFilenames;

    for (size_t i = 0; i < expectedEnumeratedFilenames.size(); ++i)
    {
      IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();

      const NTSTATUS expectedReturnCode = NtStatus::kSuccess;
      const NTSTATUS actualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          directoryHandle,
          nullptr,
          nullptr,
          nullptr,
          &ioStatusBlock,
          enumerationOutputBytes.Data(),
          enumerationOutputBytes.CapacityBytes(),
          kFileNamesInformationClass,
          SL_RETURN_SINGLE_ENTRY,
          nullptr);

      TEST_ASSERT(actualReturnCode == expectedReturnCode);
      TEST_ASSERT(ioStatusBlock.Status == expectedReturnCode);

      const unsigned int expectedBytesWritten =
          static_cast<unsigned int>(ioStatusBlock.Information);
      unsigned int actualBytesWritten =
          fileNameStructLayout.SizeOfStruct(enumerationOutputBytes.Data());
      TEST_ASSERT(actualBytesWritten == expectedBytesWritten);

      actualEnumeratedFilenames.emplace(
          fileNameStructLayout.ReadFileName(enumerationOutputBytes.Data()));
    }

    TEST_ASSERT(actualEnumeratedFilenames == expectedEnumeratedFilenames);
  }

  // Verifies that, if the output buffer is too small for the complete filename (but will fit the
  // base structure itself), a partial write occurs and an appropriate status code is returned.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationAdvance_BufferTooSmallForCompleteStruct)
  {
    constexpr std::wstring_view kTestDirectory = L"X:\\Test\\Directory";
    constexpr std::wstring_view kTestCompleteFileName = L"aVeryVeryLongFileNameGoesHere.txt";
    constexpr std::wstring_view kTestPartialFileName = L"aVeryVeryLong";

    constexpr FILE_INFORMATION_CLASS kFileNamesInformationClass =
        SFileNamesInformation::kFileInformationClass;
    const FileInformationStructLayout fileNameStructLayout =
        *FileInformationStructLayout::LayoutForFileInformationClass(kFileNamesInformationClass);

    const MockDirectoryOperationQueue::TFileNamesToEnumerate filesToEnumerate = {
        std::wstring(kTestCompleteFileName)};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kTestDirectory);

    const HANDLE directoryHandle = mockFilesystem.Open(kTestDirectory);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kTestDirectory), std::wstring(kTestDirectory));
    openHandleStore.AssociateDirectoryEnumerationState(
        directoryHandle,
        std::make_unique<MockDirectoryOperationQueue>(
            fileNameStructLayout,
            MockDirectoryOperationQueue::TFileNamesToEnumerate(filesToEnumerate)),
        fileNameStructLayout);

    Infra::TemporaryVector<uint8_t> enumerationOutputBytes;

    // Initial invocations with the buffer too small to hold a complete output structure. A partial
    // write is expected, along with a buffer overflow return code.
    for (int i = 0; i < 10; ++i)
    {
      IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();

      const NTSTATUS expectedReturnCode = NtStatus::kBufferOverflow;
      const NTSTATUS actualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          directoryHandle,
          nullptr,
          nullptr,
          nullptr,
          &ioStatusBlock,
          enumerationOutputBytes.Data(),
          fileNameStructLayout.HypotheticalSizeForFileName(kTestPartialFileName),
          kFileNamesInformationClass,
          0,
          nullptr);

      TEST_ASSERT(actualReturnCode == expectedReturnCode);

      const unsigned int expectedBytesWritten =
          fileNameStructLayout.HypotheticalSizeForFileName(kTestPartialFileName);
      unsigned int actualBytesWritten = static_cast<unsigned int>(ioStatusBlock.Information);

      TEST_ASSERT(actualBytesWritten == expectedBytesWritten);

      // The file information structure is expected to indicate the length of the filename itself,
      // irrespective of what portion of it was able to be written into the buffer provided.
      const size_t expectedFileNameLength = kTestCompleteFileName.length() * sizeof(wchar_t);
      const size_t actualFileNameLength = static_cast<size_t>(
          fileNameStructLayout.ReadFileNameLength(enumerationOutputBytes.Data()));

      TEST_ASSERT(actualFileNameLength == expectedFileNameLength);

      // Actual partial write content is not as simple as reading from the structure because the
      // structure is expected to contain a file name length field indicating the length of the
      // actual filename, in bytes, even though only part of it could fit into the supplied buffer.
      const std::wstring_view expectedPartialWriteFileName = kTestPartialFileName;
      const std::wstring_view actualPartialWriteFileName = std::wstring_view(
          fileNameStructLayout.ReadFileName(enumerationOutputBytes.Data()).data(),
          (actualBytesWritten - offsetof(SFileNamesInformation, fileName)) / sizeof(wchar_t));

      TEST_ASSERT(actualPartialWriteFileName == expectedPartialWriteFileName);
    }

    // Subsequent invocation, this time with a buffer that is large enough to hold the entire
    // structure. Success is expected, and enumeration progress is expected to advance.
    do
    {
      IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();

      const NTSTATUS expectedReturnCode = NtStatus::kSuccess;
      const NTSTATUS actualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          directoryHandle,
          nullptr,
          nullptr,
          nullptr,
          &ioStatusBlock,
          enumerationOutputBytes.Data(),
          enumerationOutputBytes.CapacityBytes(),
          kFileNamesInformationClass,
          0,
          nullptr);

      TEST_ASSERT(actualReturnCode == expectedReturnCode);

      const unsigned int expectedBytesWritten =
          fileNameStructLayout.HypotheticalSizeForFileName(kTestCompleteFileName);
      unsigned int actualBytesWritten = static_cast<unsigned int>(ioStatusBlock.Information);

      TEST_ASSERT(actualBytesWritten == expectedBytesWritten);

      const std::wstring_view expectedFileName = kTestCompleteFileName;
      const std::wstring_view actualFileName =
          fileNameStructLayout.ReadFileName(enumerationOutputBytes.Data());

      TEST_ASSERT(actualFileName == expectedFileName);
    }
    while (false);

    // Additional subsequent invocations, which are used to verify that enumeration progress has
    // advanced. There should be no files left.
    for (int i = 0; i < 10; ++i)
    {
      IO_STATUS_BLOCK ioStatusBlock = InitializeIoStatusBlock();

      const NTSTATUS expectedReturnCode = NtStatus::kNoMoreFiles;
      const NTSTATUS actualReturnCode = FilesystemExecutor::DirectoryEnumerationAdvance(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          directoryHandle,
          nullptr,
          nullptr,
          nullptr,
          &ioStatusBlock,
          enumerationOutputBytes.Data(),
          enumerationOutputBytes.CapacityBytes(),
          kFileNamesInformationClass,
          0,
          nullptr);

      TEST_ASSERT(actualReturnCode == expectedReturnCode);

      const unsigned int expectedBytesWritten = 0;
      unsigned int actualBytesWritten = static_cast<unsigned int>(ioStatusBlock.Information);

      TEST_ASSERT(actualBytesWritten == expectedBytesWritten);
    }
  }

  // Verifies that the correct paths for the provided directory handle are provided to the
  // instruction source function when preparing to start a directory enumeration operation.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_InstructionSourcePathSelection)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    std::array<uint8_t, 256> unusedBuffer{};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));

    bool instructionSourceFuncInvoked = false;

    FilesystemExecutor::DirectoryEnumerationPrepare(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        directoryHandle,
        unusedBuffer.data(),
        static_cast<ULONG>(unusedBuffer.size()),
        SFileNamesInformation::kFileInformationClass,
        nullptr,
        [&instructionSourceFuncInvoked](
            std::wstring_view associatedPath,
            std::wstring_view realOpenedPath) -> DirectoryEnumerationInstruction
        {
          TEST_ASSERT(associatedPath == kAssociatedPath);
          TEST_ASSERT(realOpenedPath == kRealOpenedPath);

          instructionSourceFuncInvoked = true;
          return DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
        });

    TEST_ASSERT(true == instructionSourceFuncInvoked);
  }

  // Verifies the nominal situation of preparing for directory enumeration, which is expected to
  // succeed. A few different handle modes are tried, and all are expected to succeed.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_NominalWithMultipleIoModes)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    constexpr MockFilesystemOperations::EOpenHandleMode kOpenHandleModesToTry[] = {
        MockFilesystemOperations::EOpenHandleMode::SynchronousIoNonAlert,
        MockFilesystemOperations::EOpenHandleMode::SynchronousIoAlert,
        MockFilesystemOperations::EOpenHandleMode::Asynchronous};

    for (const auto openHandleModeToTry : kOpenHandleModesToTry)
    {
      std::array<uint8_t, 256> unusedBuffer{};

      MockFilesystemOperations mockFilesystem;
      mockFilesystem.AddDirectory(kAssociatedPath);
      mockFilesystem.AddDirectory(kRealOpenedPath);

      const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath, openHandleModeToTry);

      OpenHandleStore openHandleStore;
      openHandleStore.InsertHandle(
          directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));
      openHandleStore.AssociateDirectoryEnumerationState(
          directoryHandle,
          std::make_unique<MockDirectoryOperationQueue>(),
          FileInformationStructLayout());

      const SDirectoryEnumerationStateSnapshot expectedDirectoryEnumerationState =
          SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore);

      const std::optional<NTSTATUS> expectedReturnValue = NtStatus::kSuccess;
      const std::optional<NTSTATUS> actualReturnValue =
          FilesystemExecutor::DirectoryEnumerationPrepare(
              TestCaseName().data(),
              kFunctionRequestIdentifier,
              openHandleStore,
              directoryHandle,
              unusedBuffer.data(),
              static_cast<ULONG>(unusedBuffer.size()),
              SFileNamesInformation::kFileInformationClass,
              nullptr,
              [](std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
              {
                TEST_FAILED_BECAUSE(L"Unexpected invocation of instruction source function.");
              });

      // Preparation is expected to succeed so that the directory enumeration takes place inside
      // Pathwinder using the prepared data structures.
      TEST_ASSERT(actualReturnValue == expectedReturnValue);

      const SDirectoryEnumerationStateSnapshot actualDirectoryEnumerationState =
          SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore);

      TEST_ASSERT(actualDirectoryEnumerationState == expectedDirectoryEnumerationState);
    }
  }

  // Verifies that preparing for a directory enumeration is idempotent. Once a directory enumeration
  // state data structure is associated with an object it remains unchanged even after a subsequent
  // call to the directory enumeration preparation function.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_Idempotent)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    std::array<uint8_t, 256> unusedBuffer{};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));
    openHandleStore.AssociateDirectoryEnumerationState(
        directoryHandle,
        std::make_unique<MockDirectoryOperationQueue>(),
        FileInformationStructLayout());

    const SDirectoryEnumerationStateSnapshot expectedDirectoryEnumerationState =
        SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore);

    // Since this invocation is expected to be idempotent the number of times it occurs is not
    // important.
    for (int i = 0; i < 10; ++i)
    {
      const std::optional<NTSTATUS> expectedReturnValue = NtStatus::kSuccess;
      const std::optional<NTSTATUS> actualReturnValue =
          FilesystemExecutor::DirectoryEnumerationPrepare(
              TestCaseName().data(),
              kFunctionRequestIdentifier,
              openHandleStore,
              directoryHandle,
              unusedBuffer.data(),
              static_cast<ULONG>(unusedBuffer.size()),
              SFileNamesInformation::kFileInformationClass,
              nullptr,
              [](std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
              {
                TEST_FAILED_BECAUSE(L"Unexpected invocation of instruction source function.");
              });

      // Preparation is expected to succeed so that the directory enumeration takes place inside
      // Pathwinder using the prepared data structures.
      TEST_ASSERT(actualReturnValue == expectedReturnValue);
    }

    const SDirectoryEnumerationStateSnapshot actualDirectoryEnumerationState =
        SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore);

    TEST_ASSERT(actualDirectoryEnumerationState == expectedDirectoryEnumerationState);
  }

  // Verifies that an application-provided buffer that is not large enough to hold the base
  // structure itself is rejected with the correct return code.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_BufferTooSmallForBaseStruct)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    std::array<uint8_t, sizeof(SFileNamesInformation) - 1> tooSmallBuffer{};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));

    const std::optional<NTSTATUS> expectedReturnValue = NtStatus::kInfoLengthMismatch;
    const std::optional<NTSTATUS> actualReturnValue =
        FilesystemExecutor::DirectoryEnumerationPrepare(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            directoryHandle,
            tooSmallBuffer.data(),
            static_cast<ULONG>(tooSmallBuffer.size()),
            SFileNamesInformation::kFileInformationClass,
            nullptr,
            [](std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
            {
              TEST_FAILED_BECAUSE(L"Unexpected invocation of instruction source function.");
            });

    TEST_ASSERT(actualReturnValue == expectedReturnValue);
  }

  // Verifies that directory enumeration operations are passed through to the system if the
  // directory enumeration says to pass through the query without modification.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_PassthroughByInstruction)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    std::array<uint8_t, 256> unusedBuffer{};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));

    bool instructionSourceFuncInvoked = false;

    const std::optional<NTSTATUS> expectedReturnValue = std::nullopt;
    const std::optional<NTSTATUS> actualReturnValue =
        FilesystemExecutor::DirectoryEnumerationPrepare(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            directoryHandle,
            unusedBuffer.data(),
            static_cast<ULONG>(unusedBuffer.size()),
            SFileNamesInformation::kFileInformationClass,
            nullptr,
            [&instructionSourceFuncInvoked](
                std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
            {
              instructionSourceFuncInvoked = true;
              return DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
            });

    TEST_ASSERT(true == instructionSourceFuncInvoked);
    TEST_ASSERT(actualReturnValue == expectedReturnValue);
  }

  // Verifies that directory enumeration operations are passed through to the system if the file
  // information class is not recognized as one that Pathwinder can intercept.
  TEST_CASE(
      FilesystemExecutor_DirectoryEnumerationPrepare_PassthroughUnsupportedFileInformationClass)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    std::array<uint8_t, 256> unusedBuffer{};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));

    const std::optional<NTSTATUS> expectedReturnValue = std::nullopt;
    const std::optional<NTSTATUS> actualReturnValue =
        FilesystemExecutor::DirectoryEnumerationPrepare(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            directoryHandle,
            unusedBuffer.data(),
            static_cast<ULONG>(unusedBuffer.size()),
            SFileBasicInformation::kFileInformationClass,
            nullptr,
            [](std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
            {
              TEST_FAILED_BECAUSE(L"Unexpected invocation of instruction source function.");
            });

    TEST_ASSERT(actualReturnValue == expectedReturnValue);
  }

  // Verifies that directory enumeration operations are passed through to the system if the provided
  // handle is not one that is cached in the open handle store.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_PassthroughUncachedHandle)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    std::array<uint8_t, 256> unusedBuffer{};

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;

    const std::optional<NTSTATUS> expectedReturnValue = std::nullopt;
    const std::optional<NTSTATUS> actualReturnValue =
        FilesystemExecutor::DirectoryEnumerationPrepare(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            directoryHandle,
            unusedBuffer.data(),
            static_cast<ULONG>(unusedBuffer.size()),
            SFileNamesInformation::kFileInformationClass,
            nullptr,
            [](std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
            {
              TEST_FAILED_BECAUSE(L"Unexpected invocation of instruction source function.");
            });

    TEST_ASSERT(actualReturnValue == expectedReturnValue);
  }

  // Verifies that the correct type of directory enumeration queues are created when the instruction
  // specifies to merge two directory enumerations.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_MergeTwoDirectories)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    std::array<uint8_t, 256> unusedBuffer{};

    // Expected result is two enumeration queues being merged together, the first for the associated
    // path and the second for the real opened path.
    const DirectoryEnumerationInstruction::SingleDirectoryEnumeration
        singleEnumerationInstructions[] = {
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                EDirectoryPathSource::AssociatedPath),
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                EDirectoryPathSource::RealOpenedPath)};
    const DirectoryEnumerationInstruction testInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectories(
            {singleEnumerationInstructions[0], singleEnumerationInstructions[1]});

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));

    const std::optional<NTSTATUS> expectedReturnValue = NtStatus::kSuccess;
    const std::optional<NTSTATUS> actualReturnValue =
        FilesystemExecutor::DirectoryEnumerationPrepare(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            directoryHandle,
            unusedBuffer.data(),
            static_cast<ULONG>(unusedBuffer.size()),
            SFileNamesInformation::kFileInformationClass,
            nullptr,
            [&testInstruction](
                std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
            {
              return testInstruction;
            });

    TEST_ASSERT(actualReturnValue == expectedReturnValue);
    TEST_ASSERT(
        openHandleStore.GetDataForHandle(directoryHandle)->directoryEnumeration.has_value());

    const SDirectoryEnumerationStateSnapshot directoryEnumerationState =
        SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore);

    // Created queues are examined in detail at this point. The specific checks used here are based
    // on the expected result, which is documented along with the directory enumeration instruction
    // used in this test case.
    TEST_ASSERT(DirectoryOperationQueueTypeIs<MergedFileInformationQueue>(
        *directoryEnumerationState.queue));

    MergedFileInformationQueue* topLevelMergeQueue =
        static_cast<MergedFileInformationQueue*>(directoryEnumerationState.queue);

    TEST_ASSERT(2 == topLevelMergeQueue->GetUnderlyingQueueCount());
    VerifyIsEnumerationQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(0),
        mockFilesystem,
        singleEnumerationInstructions[0],
        kAssociatedPath,
        SFileNamesInformation::kFileInformationClass);
    VerifyIsEnumerationQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(1),
        mockFilesystem,
        singleEnumerationInstructions[1],
        kRealOpenedPath,
        SFileNamesInformation::kFileInformationClass);
  }

  // Verifies that the correct type of directory enumeration queues are created when the instruction
  // specifies to merge two directory enumerations. This test case models a situation in which a
  // filesystem rule that affects the enumeration uses a scope-determining file pattern and hence
  // will modify the underlying enumeration operations to either match or not match the filesystem
  // rule's file pattern.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_MergeTwoDirectoriesWithFilePatternSource)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    std::array<uint8_t, 256> unusedBuffer{};

    const FilesystemRule testRule(
        L"", kAssociatedPath, kRealOpenedPath, {L"*.txt", L"*.bin", L"*.log"});

    // Expected result is two enumeration queues being merged together, the first for the associated
    // path and the second for the real opened path.
    const DirectoryEnumerationInstruction::SingleDirectoryEnumeration
        singleEnumerationInstructions[] = {
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                IncludeOnlyMatchingFilenames(EDirectoryPathSource::AssociatedPath, testRule),
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
                IncludeAllExceptMatchingFilenames(EDirectoryPathSource::RealOpenedPath, testRule)};
    const DirectoryEnumerationInstruction testInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectories(
            {singleEnumerationInstructions[0], singleEnumerationInstructions[1]});

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));

    const std::optional<NTSTATUS> expectedReturnValue = NtStatus::kSuccess;
    const std::optional<NTSTATUS> actualReturnValue =
        FilesystemExecutor::DirectoryEnumerationPrepare(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            directoryHandle,
            unusedBuffer.data(),
            static_cast<ULONG>(unusedBuffer.size()),
            SFileNamesInformation::kFileInformationClass,
            nullptr,
            [&testInstruction](
                std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
            {
              return testInstruction;
            });

    TEST_ASSERT(actualReturnValue == expectedReturnValue);
    TEST_ASSERT(
        openHandleStore.GetDataForHandle(directoryHandle)->directoryEnumeration.has_value());

    const SDirectoryEnumerationStateSnapshot directoryEnumerationState =
        SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore);

    // Created queues are examined in detail at this point. The specific checks used here are based
    // on the expected result, which is documented along with the directory enumeration instruction
    // used in this test case.
    TEST_ASSERT(DirectoryOperationQueueTypeIs<MergedFileInformationQueue>(
        *directoryEnumerationState.queue));

    MergedFileInformationQueue* topLevelMergeQueue =
        static_cast<MergedFileInformationQueue*>(directoryEnumerationState.queue);

    TEST_ASSERT(2 == topLevelMergeQueue->GetUnderlyingQueueCount());
    VerifyIsEnumerationQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(0),
        mockFilesystem,
        singleEnumerationInstructions[0],
        kAssociatedPath,
        SFileNamesInformation::kFileInformationClass);
    VerifyIsEnumerationQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(1),
        mockFilesystem,
        singleEnumerationInstructions[1],
        kRealOpenedPath,
        SFileNamesInformation::kFileInformationClass);
  }

  // Verifies that the correct type of directory enumeration queues are created when the instruction
  // specifies to merge two directory enumerations and a file pattern is used to filter the
  // enumeration output. This test models the situation in which application specified a file
  // pattern, meaning that it is expected to be associated with the open directory handle and used
  // to filter enumeration output.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_MergeTwoDirectoriesWithQueryFilePattern)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    std::array<uint8_t, 256> unusedBuffer{};

    // Expected result is two enumeration queues being merged together, the first for the associated
    // path and the second for the real opened path.
    const DirectoryEnumerationInstruction::SingleDirectoryEnumeration
        singleEnumerationInstructions[] = {
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                EDirectoryPathSource::AssociatedPath),
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                EDirectoryPathSource::RealOpenedPath)};
    const DirectoryEnumerationInstruction testInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectories(
            {singleEnumerationInstructions[0], singleEnumerationInstructions[1]});

    constexpr std::wstring_view kQueryFilePattern = L"*.txt";
    UNICODE_STRING filePatternUnicodeString =
        Strings::NtConvertStringViewToUnicodeString(kQueryFilePattern);

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));

    const std::optional<NTSTATUS> expectedReturnValue = NtStatus::kSuccess;
    const std::optional<NTSTATUS> actualReturnValue =
        FilesystemExecutor::DirectoryEnumerationPrepare(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            directoryHandle,
            unusedBuffer.data(),
            static_cast<ULONG>(unusedBuffer.size()),
            SFileNamesInformation::kFileInformationClass,
            &filePatternUnicodeString,
            [&testInstruction](
                std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
            {
              return testInstruction;
            });

    TEST_ASSERT(actualReturnValue == expectedReturnValue);
    TEST_ASSERT(
        openHandleStore.GetDataForHandle(directoryHandle)->directoryEnumeration.has_value());

    const SDirectoryEnumerationStateSnapshot directoryEnumerationState =
        SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore);

    // Created queues are examined in detail at this point. The specific checks used here are based
    // on the expected result, which is documented along with the directory enumeration instruction
    // used in this test case.
    TEST_ASSERT(DirectoryOperationQueueTypeIs<MergedFileInformationQueue>(
        *directoryEnumerationState.queue));

    MergedFileInformationQueue* topLevelMergeQueue =
        static_cast<MergedFileInformationQueue*>(directoryEnumerationState.queue);

    TEST_ASSERT(2 == topLevelMergeQueue->GetUnderlyingQueueCount());
    VerifyIsEnumerationQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(0),
        mockFilesystem,
        singleEnumerationInstructions[0],
        kAssociatedPath,
        SFileNamesInformation::kFileInformationClass,
        kQueryFilePattern);
    VerifyIsEnumerationQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(1),
        mockFilesystem,
        singleEnumerationInstructions[1],
        kRealOpenedPath,
        SFileNamesInformation::kFileInformationClass,
        kQueryFilePattern);
  }

  // Verifies that the correct type of directory enumeration queue is created when the instruction
  // specifies to enumerate a specific set of directories as the entire output of the enumeration.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_NameInsertion)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    std::array<uint8_t, 256> unusedBuffer{};

    // Expected result is a single name insertion queue.
    const FilesystemRule filesystemRules[] = {
        FilesystemRule(L"", kAssociatedPath, kRealOpenedPath)};
    const DirectoryEnumerationInstruction::SingleDirectoryNameInsertion
        singleNameInsertionInstructions[] = {
            DirectoryEnumerationInstruction::SingleDirectoryNameInsertion(filesystemRules[0])};
    const DirectoryEnumerationInstruction testInstruction =
        DirectoryEnumerationInstruction::UseOnlyRuleOriginDirectoryNames(
            {singleNameInsertionInstructions[0]});

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));

    const std::optional<NTSTATUS> expectedReturnValue = NtStatus::kSuccess;
    const std::optional<NTSTATUS> actualReturnValue =
        FilesystemExecutor::DirectoryEnumerationPrepare(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            directoryHandle,
            unusedBuffer.data(),
            static_cast<ULONG>(unusedBuffer.size()),
            SFileNamesInformation::kFileInformationClass,
            nullptr,
            [&testInstruction](
                std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
            {
              return testInstruction;
            });

    TEST_ASSERT(actualReturnValue == expectedReturnValue);
    TEST_ASSERT(
        openHandleStore.GetDataForHandle(directoryHandle)->directoryEnumeration.has_value());

    const SDirectoryEnumerationStateSnapshot directoryEnumerationState =
        SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore);

    // Created queues are examined in detail at this point. The specific checks used here are based
    // on the expected result, which is documented along with the directory enumeration instruction
    // used in this test case.
    VerifyIsNameInsertionQueueAndMatchesSpec(
        directoryEnumerationState.queue,
        {singleNameInsertionInstructions[0]},
        SFileNamesInformation::kFileInformationClass);
  }

  // Verifies that the correct type of directory enumeration queue is created when the instruction
  // specifies to enumerate a specific set of directories as the entire output of the enumeration.
  // This test models the situation in which application specified a file pattern, meaning that it
  // is expected to be associated with the open directory handle and used to filter enumeration
  // output.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_NameInsertionWithQueryFilePattern)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";

    std::array<uint8_t, 256> unusedBuffer{};

    // Expected result is a single name insertion queue.
    const FilesystemRule filesystemRules[] = {
        FilesystemRule(L"", kAssociatedPath, kRealOpenedPath)};
    const DirectoryEnumerationInstruction::SingleDirectoryNameInsertion
        singleNameInsertionInstructions[] = {
            DirectoryEnumerationInstruction::SingleDirectoryNameInsertion(filesystemRules[0])};
    const DirectoryEnumerationInstruction testInstruction =
        DirectoryEnumerationInstruction::UseOnlyRuleOriginDirectoryNames(
            {singleNameInsertionInstructions[0]});

    constexpr std::wstring_view kQueryFilePattern = L"*.txt";
    UNICODE_STRING filePatternUnicodeString =
        Strings::NtConvertStringViewToUnicodeString(kQueryFilePattern);

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));

    const std::optional<NTSTATUS> expectedReturnValue = NtStatus::kSuccess;
    const std::optional<NTSTATUS> actualReturnValue =
        FilesystemExecutor::DirectoryEnumerationPrepare(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            directoryHandle,
            unusedBuffer.data(),
            static_cast<ULONG>(unusedBuffer.size()),
            SFileNamesInformation::kFileInformationClass,
            &filePatternUnicodeString,
            [&testInstruction](
                std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
            {
              return testInstruction;
            });

    TEST_ASSERT(actualReturnValue == expectedReturnValue);
    TEST_ASSERT(
        openHandleStore.GetDataForHandle(directoryHandle)->directoryEnumeration.has_value());

    const SDirectoryEnumerationStateSnapshot directoryEnumerationState =
        SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore);

    // Created queues are examined in detail at this point. The specific checks used here are based
    // on the expected result, which is documented along with the directory enumeration instruction
    // used in this test case.
    VerifyIsNameInsertionQueueAndMatchesSpec(
        directoryEnumerationState.queue,
        {singleNameInsertionInstructions[0]},
        SFileNamesInformation::kFileInformationClass,
        kQueryFilePattern);
  }

  // Verifies that the correct type of directory enumeration queues are created when the instruction
  // specifies both directory enumeration and name insertion.
  TEST_CASE(FilesystemExecutor_DirectoryEnumerationPrepare_CombinedNameInsertionAndEnumeration)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";
    constexpr std::wstring_view kOriginDirectory = L"E:\\OriginPath1";
    constexpr std::wstring_view kTargetDirectory = L"E:\\TargetPath2";

    std::array<uint8_t, 256> unusedBuffer{};

    // Expected result is a single name insertion queue.
    const FilesystemRule filesystemRules[] = {
        FilesystemRule(L"", kOriginDirectory, kTargetDirectory)};
    const DirectoryEnumerationInstruction::SingleDirectoryEnumeration
        singleEnumerationInstructions[] = {
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                EDirectoryPathSource::AssociatedPath),
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                EDirectoryPathSource::RealOpenedPath)};
    const DirectoryEnumerationInstruction::SingleDirectoryNameInsertion
        singleNameInsertionInstructions[] = {
            DirectoryEnumerationInstruction::SingleDirectoryNameInsertion(filesystemRules[0])};
    const DirectoryEnumerationInstruction testInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectoriesAndInsertRuleOriginDirectoryNames(
            {singleEnumerationInstructions[0], singleEnumerationInstructions[1]},
            {singleNameInsertionInstructions[0]});

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);
    mockFilesystem.AddDirectory(kOriginDirectory);
    mockFilesystem.AddDirectory(kTargetDirectory);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));

    const std::optional<NTSTATUS> expectedReturnValue = NtStatus::kSuccess;
    const std::optional<NTSTATUS> actualReturnValue =
        FilesystemExecutor::DirectoryEnumerationPrepare(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            directoryHandle,
            unusedBuffer.data(),
            static_cast<ULONG>(unusedBuffer.size()),
            SFileNamesInformation::kFileInformationClass,
            nullptr,
            [&testInstruction](
                std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
            {
              return testInstruction;
            });

    TEST_ASSERT(actualReturnValue == expectedReturnValue);
    TEST_ASSERT(
        openHandleStore.GetDataForHandle(directoryHandle)->directoryEnumeration.has_value());

    const SDirectoryEnumerationStateSnapshot directoryEnumerationState =
        SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore);

    // Created queues are examined in detail at this point. The specific checks used here are based
    // on the expected result, which is documented along with the directory enumeration instruction
    // used in this test case.
    TEST_ASSERT(DirectoryOperationQueueTypeIs<MergedFileInformationQueue>(
        *directoryEnumerationState.queue));

    MergedFileInformationQueue* topLevelMergeQueue =
        static_cast<MergedFileInformationQueue*>(directoryEnumerationState.queue);

    TEST_ASSERT(3 == topLevelMergeQueue->GetUnderlyingQueueCount());
    VerifyIsEnumerationQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(0),
        mockFilesystem,
        singleEnumerationInstructions[0],
        kAssociatedPath,
        SFileNamesInformation::kFileInformationClass);
    VerifyIsEnumerationQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(1),
        mockFilesystem,
        singleEnumerationInstructions[1],
        kRealOpenedPath,
        SFileNamesInformation::kFileInformationClass);
    VerifyIsNameInsertionQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(2),
        {singleNameInsertionInstructions[0]},
        SFileNamesInformation::kFileInformationClass);
  }

  // Verifies that the correct type of directory enumeration queues are created when the instruction
  // specifies both directory enumeration and name insertion. This test models the situation in
  // which application specified a file pattern, meaning that it is expected to be associated with
  // the open directory handle and used to filter enumeration output.
  TEST_CASE(
      FilesystemExecutor_DirectoryEnumerationPrepare_CombinedNameInsertionAndEnumerationWithQueryFilePattern)
  {
    constexpr std::wstring_view kAssociatedPath = L"C:\\AssociatedPathDirectory";
    constexpr std::wstring_view kRealOpenedPath = L"D:\\RealOpenedPath\\Directory";
    constexpr std::wstring_view kOriginDirectory = L"E:\\OriginPath1";
    constexpr std::wstring_view kTargetDirectory = L"E:\\TargetPath2";

    std::array<uint8_t, 256> unusedBuffer{};

    // Expected result is a single name insertion queue.
    const FilesystemRule filesystemRules[] = {
        FilesystemRule(L"", kOriginDirectory, kTargetDirectory)};
    const DirectoryEnumerationInstruction::SingleDirectoryEnumeration
        singleEnumerationInstructions[] = {
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                EDirectoryPathSource::AssociatedPath),
            DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(
                EDirectoryPathSource::RealOpenedPath)};
    const DirectoryEnumerationInstruction::SingleDirectoryNameInsertion
        singleNameInsertionInstructions[] = {
            DirectoryEnumerationInstruction::SingleDirectoryNameInsertion(filesystemRules[0])};
    const DirectoryEnumerationInstruction testInstruction =
        DirectoryEnumerationInstruction::EnumerateDirectoriesAndInsertRuleOriginDirectoryNames(
            {singleEnumerationInstructions[0], singleEnumerationInstructions[1]},
            {singleNameInsertionInstructions[0]});

    constexpr std::wstring_view kQueryFilePattern = L"*.txt";
    UNICODE_STRING filePatternUnicodeString =
        Strings::NtConvertStringViewToUnicodeString(kQueryFilePattern);

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kAssociatedPath);
    mockFilesystem.AddDirectory(kRealOpenedPath);
    mockFilesystem.AddDirectory(kOriginDirectory);
    mockFilesystem.AddDirectory(kTargetDirectory);

    const HANDLE directoryHandle = mockFilesystem.Open(kRealOpenedPath);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        directoryHandle, std::wstring(kAssociatedPath), std::wstring(kRealOpenedPath));

    const std::optional<NTSTATUS> expectedReturnValue = NtStatus::kSuccess;
    const std::optional<NTSTATUS> actualReturnValue =
        FilesystemExecutor::DirectoryEnumerationPrepare(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            directoryHandle,
            unusedBuffer.data(),
            static_cast<ULONG>(unusedBuffer.size()),
            SFileNamesInformation::kFileInformationClass,
            &filePatternUnicodeString,
            [&testInstruction](
                std::wstring_view, std::wstring_view) -> DirectoryEnumerationInstruction
            {
              return testInstruction;
            });

    TEST_ASSERT(actualReturnValue == expectedReturnValue);
    TEST_ASSERT(
        openHandleStore.GetDataForHandle(directoryHandle)->directoryEnumeration.has_value());

    const SDirectoryEnumerationStateSnapshot directoryEnumerationState =
        SDirectoryEnumerationStateSnapshot::GetForHandle(directoryHandle, openHandleStore);

    // Created queues are examined in detail at this point. The specific checks used here are based
    // on the expected result, which is documented along with the directory enumeration instruction
    // used in this test case.
    TEST_ASSERT(DirectoryOperationQueueTypeIs<MergedFileInformationQueue>(
        *directoryEnumerationState.queue));

    MergedFileInformationQueue* topLevelMergeQueue =
        static_cast<MergedFileInformationQueue*>(directoryEnumerationState.queue);

    TEST_ASSERT(3 == topLevelMergeQueue->GetUnderlyingQueueCount());
    VerifyIsEnumerationQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(0),
        mockFilesystem,
        singleEnumerationInstructions[0],
        kAssociatedPath,
        SFileNamesInformation::kFileInformationClass,
        kQueryFilePattern);
    VerifyIsEnumerationQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(1),
        mockFilesystem,
        singleEnumerationInstructions[1],
        kRealOpenedPath,
        SFileNamesInformation::kFileInformationClass,
        kQueryFilePattern);
    VerifyIsNameInsertionQueueAndMatchesSpec(
        topLevelMergeQueue->GetUnderlyingQueue(2),
        {singleNameInsertionInstructions[0]},
        SFileNamesInformation::kFileInformationClass,
        kQueryFilePattern);
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
    using TExpectedPaths = Infra::ArrayList<std::wstring_view, 2>;

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
    using TCreateDispositionOrForcedError = Infra::ValueOrError<ULONG, NTSTATUS>;

    // Holds multiple create dispositions, or forced error codes, in the expected order that they
    // should be tried. If a create disposition is present then it is expected as the parameter,
    // otherwise it is expected as the return code from the filesystem executor function.
    using TExpectedCreateDispositionsOrForcedErrors =
        Infra::ArrayList<TCreateDispositionOrForcedError, 2>;

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
    using TParametersOrForcedError = Infra::ValueOrError<SCreateDispositionAndPath, NTSTATUS>;

    // Holds multiple parameter pairs, or forced error codes, in the expected order that they
    // should be tried. If a parameter pair is present then it is expected as the parameters to the
    // underlying system call, otherwise it is expected as the return code from the filesystem
    // executor function.
    using TExpectedParametersOrForcedErrors = Infra::ArrayList<TParametersOrForcedError, 4>;

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
          [&mockFilesystem, kExtraPreOperationHierarchyToCreate](
              PHANDLE, POBJECT_ATTRIBUTES, ULONG) -> NTSTATUS
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
    using TExpectedPaths = Infra::ArrayList<std::wstring_view, 2>;

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
          [&mockFilesystem, kExtraPreOperationHierarchyToCreate](
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

  // Verifies that the underlying system call return code is propagated to the caller as the result
  // of the executor operation when file information is queried by object attributes.
  TEST_CASE(FilesystemExecutor_QueryByObjectAttributes_PropagateReturnCode)
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
        const NTSTATUS actualReturnCode = FilesystemExecutor::QueryByObjectAttributes(
            TestCaseName().data(),
            kFunctionRequestIdentifier,
            openHandleStore,
            &objectAttributesUnredirectedPath,
            GENERIC_READ,
            [&fileOperationInstructionToTry](
                std::wstring_view, FileAccessMode, CreateDisposition) -> FileOperationInstruction
            {
              return fileOperationInstructionToTry;
            },
            [expectedReturnCode](POBJECT_ATTRIBUTES) -> NTSTATUS
            {
              return expectedReturnCode;
            });

        TEST_ASSERT(actualReturnCode == expectedReturnCode);
      }
    }
  }

  // Verifies that the filesystem executor correctly composes a complete path when requesting a file
  // operation instruction as part of querying for file information by object attributes. If no root
  // directory is specified then the requested path is the same as the input path. If the root
  // directory is specified by handle and the handle is cached in the open handle store then the
  // requested path is the root directory path concatenated with the input path. Note that an
  // uncached (but present) root directory is handled by a different test case entirely, as this
  // situation should result in passthrough behavior.
  TEST_CASE(FilesystemExecutor_QueryByObjectAttributes_InstructionSourcePathComposition)
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

      FilesystemExecutor::QueryByObjectAttributes(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &objectAttributes,
          GENERIC_READ,
          [kUnredirectedPath](
              std::wstring_view actualRequestedPath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            std::wstring_view expectedRequestedPath = kUnredirectedPath;
            TEST_ASSERT(actualRequestedPath == expectedRequestedPath);
            return FileOperationInstruction::NoRedirectionOrInterception();
          },
          [](POBJECT_ATTRIBUTES) -> NTSTATUS
          {
            return NtStatus::kSuccess;
          });
    }
  }

  // Verifies that any file attempt preference is honored if it is contained in a file operation
  // instruction when file information is being queried by object attributes. The instructions used
  // in this test case all contain an unredirected and a redirected path, and they supply various
  // enumerators indicating the order in which the files should be tried.
  TEST_CASE(FilesystemExecutor_QueryByObjectAttributes_TryFilesOrder)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kRedirectedPath = L"C:\\RedirectedDirectory\\TestFile.txt";

    // Holds paths in the order that they are expected to be tried in invocations of the underlying
    // system call.
    using TExpectedPaths = Infra::ArrayList<std::wstring_view, 2>;

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

      FilesystemExecutor::QueryByObjectAttributes(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &objectAttributesUnredirectedPath,
          GENERIC_READ,
          [&testInputFileOperationInstruction](
              std::wstring_view actualUnredirectedPath,
              FileAccessMode,
              CreateDisposition) -> FileOperationInstruction
          {
            return testInputFileOperationInstruction;
          },
          [&tryFilesTestRecord, &underlyingSystemCallNumInvocations](
              POBJECT_ATTRIBUTES objectAttributes) -> NTSTATUS
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

  // Verifies that a pre-operation request contained in a filesystem operation instruction is
  // executed correctly when querying for file information by object attributes. The file operation
  // instruction only contains a pre-operation and nothing else, and this test case exercises an
  // operation to ensure a path hierarchy exists. The forms of instructions exercised by this test
  // are not generally produced by filesystem director objects but are intended specifically to
  // exercise pre-operation functionality.
  TEST_CASE(FilesystemExecutor_QueryByObjectAttributes_PreOperation_EnsurePathHierarchyExists)
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

      FilesystemExecutor::QueryByObjectAttributes(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          &objectAttributesUnredirectedPath,
          GENERIC_READ,
          [kUnredirectedPath, &fileOperationInstructionToTry, &instructionSourceWasInvoked](
              std::wstring_view, FileAccessMode, CreateDisposition) -> FileOperationInstruction
          {
            instructionSourceWasInvoked = true;
            return fileOperationInstructionToTry;
          },
          [&mockFilesystem, kExtraPreOperationHierarchyToCreate](POBJECT_ATTRIBUTES) -> NTSTATUS
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

  // Verifies that queries for file information by object attributes are passed through to the
  // system without modification or interception if the root directory handle is specified but not
  // cached. In this situation, the root directory would have been declared "uninteresting" by the
  // filesystem director, so the executor should just assume it is still uninteresting and not even
  // ask for a redirection instruction. Request should be passed through unmodified to the system.
  // Various valid forms of file operation instructions are exercised, even those that are not
  // actually ever produced by a filesystem director.
  TEST_CASE(
      FilesystemExecutor_QueryByObjectAttributes_PassthroughWithoutInstruction_UncachedRootDirectory)
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

    FilesystemExecutor::QueryByObjectAttributes(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        &objectAttributesRelativePath,
        GENERIC_READ,
        [kUnredirectedPath](
            std::wstring_view actualUnredirectedPath,
            FileAccessMode,
            CreateDisposition) -> FileOperationInstruction
        {
          TEST_FAILED_BECAUSE(
              "Instruction source should not be invoked if the root directory handle is present but uncached.");
        },
        [&objectAttributesRelativePath](POBJECT_ATTRIBUTES objectAttributes) -> NTSTATUS
        {
          const OBJECT_ATTRIBUTES& expectedObjectAttributes = objectAttributesRelativePath;
          const OBJECT_ATTRIBUTES& actualObjectAttributes = *objectAttributes;
          TEST_ASSERT(EqualObjectAttributes(actualObjectAttributes, expectedObjectAttributes));

          return NtStatus::kSuccess;
        });

    TEST_ASSERT(true == openHandleStore.Empty());
  }

  // Verifies that the underlying system call return code is propagated to the caller as the result
  // of the executor operation when file information is queried using a handle.
  TEST_CASE(FilesystemExecutor_QueryByHandle_PropagateReturnCode)
  {
    constexpr std::wstring_view kUnredirectedPath = L"C:\\TestDirectory\\TestFile.txt";
    constexpr std::wstring_view kRedirectedPath = L"C:\\RedirectedDirectory\\TestFile.txt";

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

    for (NTSTATUS returnCodeToTry : returnCodesToTry)
    {
      HANDLE unusedHandleValue = NULL;
      IO_STATUS_BLOCK statusBlock{};
      BytewiseDanglingFilenameStruct<SFileNameInformation> unusedFileNameInformation;

      OpenHandleStore openHandleStore;

      const NTSTATUS expectedReturnCode = returnCodeToTry;
      const NTSTATUS actualReturnCode = FilesystemExecutor::QueryByHandle(
          TestCaseName().data(),
          kFunctionRequestIdentifier,
          openHandleStore,
          unusedHandleValue,
          &statusBlock,
          &unusedFileNameInformation.GetFileInformationStruct(),
          unusedFileNameInformation.GetFileInformationStructSizeBytes(),
          SFileNameInformation::kFileInformationClass,
          [expectedReturnCode](
              HANDLE, PIO_STATUS_BLOCK ioStatusBlock, PVOID, ULONG, FILE_INFORMATION_CLASS)
              -> NTSTATUS
          {
            ioStatusBlock->Status = expectedReturnCode;
            return expectedReturnCode;
          });

      TEST_ASSERT(actualReturnCode == expectedReturnCode);
      TEST_ASSERT(statusBlock.Status == expectedReturnCode);
    }
  }

  // Verifies that a filename request by handle is passed through to the system without modification
  // if the handle is not cached in the open handle store. This situation indicates that the open
  // handle could not have been the result of a redirection.
  TEST_CASE(FilesystemExecutor_QueryByHandle_UncachedHandlePathNotReplaced)
  {
    constexpr std::wstring_view kSystemReturnedPath = L"C:\\A\\File.txt";

    HANDLE unusedHandleValue = NULL;
    IO_STATUS_BLOCK statusBlock{};

    uint8_t fileNameInformationBuffer[32] = {};
    SFileNameInformation& fileNameInformation =
        *reinterpret_cast<SFileNameInformation*>(fileNameInformationBuffer);

    OpenHandleStore openHandleStore;

    FilesystemExecutor::QueryByHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        unusedHandleValue,
        &statusBlock,
        &fileNameInformation,
        sizeof(fileNameInformationBuffer),
        SFileNameInformation::kFileInformationClass,
        [kSystemReturnedPath](
            HANDLE,
            PIO_STATUS_BLOCK ioStatusBlock,
            PVOID fileInformation,
            ULONG length,
            FILE_INFORMATION_CLASS) -> NTSTATUS
        {
          ioStatusBlock->Status = 55;
          ioStatusBlock->Information = 6666;
          return CopyStringToFileNameInformation(
              kSystemReturnedPath,
              reinterpret_cast<SFileNameInformation*>(fileInformation),
              length);
        });

    const std::wstring_view expectedQueryResultPath = kSystemReturnedPath;
    const std::wstring_view actualQueryResultPath = std::wstring_view(
        fileNameInformation.fileName, fileNameInformation.fileNameLength / sizeof(wchar_t));
    TEST_ASSERT(actualQueryResultPath == expectedQueryResultPath);
    TEST_ASSERT(55 == statusBlock.Status);
    TEST_ASSERT(6666 == statusBlock.Information);
  }

  // Verifies that a filename request by handle is replaced with the associated path if the handle
  // is cached in the open handle store. This situation indicates that the open handle might be the
  // result of a redirection and that Pathwinder knows the path that should be supplied to the
  // application.
  TEST_CASE(FilesystemExecutor_QueryByHandle_CachedHandleNameReplaced)
  {
    constexpr std::wstring_view kSystemReturnedPath = L"C:\\A\\File.txt";
    constexpr std::wstring_view kCachedAssociatedPath = L"D:\\E\\File.txt";

    HANDLE handleValue = reinterpret_cast<HANDLE>(3033345);
    IO_STATUS_BLOCK statusBlock{};

    uint8_t fileNameInformationBuffer[32] = {};
    SFileNameInformation& fileNameInformation =
        *reinterpret_cast<SFileNameInformation*>(fileNameInformationBuffer);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        handleValue, std::wstring(kCachedAssociatedPath), std::wstring(kSystemReturnedPath));

    const NTSTATUS expectedReturnCode = NtStatus::kSuccess;
    const NTSTATUS actualReturnCode = FilesystemExecutor::QueryByHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        handleValue,
        &statusBlock,
        &fileNameInformation,
        sizeof(fileNameInformationBuffer),
        SFileNameInformation::kFileInformationClass,
        [kSystemReturnedPath](
            HANDLE,
            PIO_STATUS_BLOCK ioStatusBlock,
            PVOID fileInformation,
            ULONG length,
            FILE_INFORMATION_CLASS) -> NTSTATUS
        {
          SFileNameInformation* const fileNameInformation =
              reinterpret_cast<SFileNameInformation*>(fileInformation);
          ioStatusBlock->Status =
              CopyStringToFileNameInformation(kSystemReturnedPath, fileNameInformation, length);
          ioStatusBlock->Information = std::min(
              static_cast<ULONG_PTR>(length),
              static_cast<ULONG_PTR>(
                  FileInformationStructLayout::SizeOfStructByType<SFileNameInformation>(
                      *fileNameInformation)));
          return ioStatusBlock->Status;
        });

    TEST_ASSERT(actualReturnCode == expectedReturnCode);
    TEST_ASSERT(statusBlock.Status == expectedReturnCode);
    TEST_ASSERT(
        statusBlock.Information ==
        (kCachedAssociatedPath.length() * sizeof(wchar_t)) + sizeof(SFileNameInformation) -
            offsetof(SFileNameInformation, fileName));

    const std::wstring_view expectedQueryResultPath = kCachedAssociatedPath;
    const std::wstring_view actualQueryResultPath = std::wstring_view(
        fileNameInformation.fileName, fileNameInformation.fileNameLength / sizeof(wchar_t));
    TEST_ASSERT(actualQueryResultPath == expectedQueryResultPath);
  }

  // Verifies that a filename request by handle is replaced with the associated path if the handle
  // is cached in the open handle store. This situation indicates that the open handle might be the
  // result of a redirection and that Pathwinder knows the path that should be supplied to the
  // application. In this case the file information class specifies a compound structure that
  // includes more information than just the file name itself, and other parts of the structure
  // should not be touched.
  TEST_CASE(FilesystemExecutor_QueryByHandle_CachedHandleNameReplacedInCompoundStruct)
  {
    constexpr std::wstring_view kSystemReturnedPath = L"C:\\A\\File.txt";
    constexpr std::wstring_view kCachedAssociatedPath = L"D:\\E\\F\\G\\File.txt";
    static_assert(
        kCachedAssociatedPath.length() > kSystemReturnedPath.length(),
        "A longer cached associated path is needed for this test case.");

    constexpr uint8_t kGuardBufferByte = 0xfe;

    HANDLE handleValue = reinterpret_cast<HANDLE>(3033345);
    IO_STATUS_BLOCK statusBlock{};

    uint8_t expectedFileAllInformationBuffer[256] = {};
    SFileAllInformation& expectedFileAllInformation =
        *reinterpret_cast<SFileAllInformation*>(expectedFileAllInformationBuffer);
    std::memset(
        expectedFileAllInformationBuffer,
        kGuardBufferByte,
        sizeof(expectedFileAllInformationBuffer));
    FileInformationStructLayout::WriteFileNameByType<SFileNameInformation>(
        expectedFileAllInformation.nameInformation,
        sizeof(expectedFileAllInformationBuffer) - offsetof(SFileAllInformation, nameInformation),
        kCachedAssociatedPath);
    const size_t expectedBytesWritten =
        FileInformationStructLayout::SizeOfStructByType<SFileNameInformation>(
            expectedFileAllInformation.nameInformation) +
        offsetof(SFileAllInformation, nameInformation);

    uint8_t actualFileAllInformationBuffer[_countof(expectedFileAllInformationBuffer)] = {};
    SFileAllInformation& actualFileAllInformation =
        *reinterpret_cast<SFileAllInformation*>(actualFileAllInformationBuffer);
    std::memset(
        actualFileAllInformationBuffer, ~kGuardBufferByte, sizeof(actualFileAllInformationBuffer));

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        handleValue, std::wstring(kCachedAssociatedPath), std::wstring(kSystemReturnedPath));

    const NTSTATUS expectedReturnCode = NtStatus::kSuccess;
    const NTSTATUS actualReturnCode = FilesystemExecutor::QueryByHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        handleValue,
        &statusBlock,
        &actualFileAllInformation,
        sizeof(actualFileAllInformationBuffer),
        SFileAllInformation::kFileInformationClass,
        [kSystemReturnedPath](
            HANDLE,
            PIO_STATUS_BLOCK ioStatusBlock,
            PVOID fileInformation,
            ULONG length,
            FILE_INFORMATION_CLASS) -> NTSTATUS
        {
          SFileAllInformation* const fileAllInformation =
              reinterpret_cast<SFileAllInformation*>(fileInformation);
          std::memset(
              fileAllInformation,
              kGuardBufferByte,
              std::min(
                  static_cast<size_t>(length), offsetof(SFileAllInformation, nameInformation)));
          ioStatusBlock->Status = CopyStringToFileNameInformation(
              kSystemReturnedPath, &fileAllInformation->nameInformation, length);
          ioStatusBlock->Information = std::min(
              static_cast<ULONG_PTR>(length),
              static_cast<ULONG_PTR>(
                  offsetof(SFileAllInformation, nameInformation) +
                  FileInformationStructLayout::SizeOfStructByType<SFileNameInformation>(
                      fileAllInformation->nameInformation)));
          return ioStatusBlock->Status;
        });

    TEST_ASSERT(actualReturnCode == expectedReturnCode);
    TEST_ASSERT(statusBlock.Status == expectedReturnCode);
    TEST_ASSERT(statusBlock.Information == expectedBytesWritten);
    TEST_ASSERT(
        0 ==
        std::memcmp(
            actualFileAllInformationBuffer,
            expectedFileAllInformationBuffer,
            expectedBytesWritten));
  }

  // Verifies that a filename request by handle is replaced with the associated path if the handle
  // is cached in the open handle store and, further, that the optional filename transformation
  // function is invoked if it is supplied.
  TEST_CASE(FilesystemExecutor_QueryByHandle_CachedHandleNameReplacedAndTransformed)
  {
    constexpr std::wstring_view kSystemReturnedPath = L"C:\\A\\File.txt";
    constexpr std::wstring_view kCachedAssociatedPath = L"D:\\E\\File.txt";
    constexpr std::wstring_view kOutputTransformedPath = L"Z:\\T\\File.txt";

    HANDLE handleValue = reinterpret_cast<HANDLE>(3033345);
    IO_STATUS_BLOCK statusBlock{};

    uint8_t fileNameInformationBuffer[32] = {};
    SFileNameInformation& fileNameInformation =
        *reinterpret_cast<SFileNameInformation*>(fileNameInformationBuffer);

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        handleValue, std::wstring(kCachedAssociatedPath), std::wstring(kSystemReturnedPath));

    const NTSTATUS expectedReturnCode = NtStatus::kSuccess;
    const NTSTATUS actualReturnCode = FilesystemExecutor::QueryByHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        handleValue,
        &statusBlock,
        &fileNameInformation,
        sizeof(fileNameInformationBuffer),
        SFileNameInformation::kFileInformationClass,
        [kSystemReturnedPath](
            HANDLE,
            PIO_STATUS_BLOCK ioStatusBlock,
            PVOID fileInformation,
            ULONG length,
            FILE_INFORMATION_CLASS) -> NTSTATUS
        {
          SFileNameInformation* const fileNameInformation =
              reinterpret_cast<SFileNameInformation*>(fileInformation);
          ioStatusBlock->Status =
              CopyStringToFileNameInformation(kSystemReturnedPath, fileNameInformation, length);
          ioStatusBlock->Information = std::min(
              static_cast<ULONG_PTR>(length),
              static_cast<ULONG_PTR>(
                  FileInformationStructLayout::SizeOfStructByType<SFileNameInformation>(
                      *fileNameInformation)));
          return ioStatusBlock->Status;
        },
        [](std::wstring_view) -> std::wstring_view
        {
          return kOutputTransformedPath;
        });

    TEST_ASSERT(actualReturnCode == expectedReturnCode);
    TEST_ASSERT(statusBlock.Status == expectedReturnCode);
    TEST_ASSERT(
        statusBlock.Information ==
        (kCachedAssociatedPath.length() * sizeof(wchar_t)) + sizeof(SFileNameInformation) -
            offsetof(SFileNameInformation, fileName));

    const std::wstring_view expectedQueryResultPath = kOutputTransformedPath;
    const std::wstring_view actualQueryResultPath = std::wstring_view(
        fileNameInformation.fileName, fileNameInformation.fileNameLength / sizeof(wchar_t));
    TEST_ASSERT(actualQueryResultPath == expectedQueryResultPath);
  }

  // Verifies that a filename request by handle is replaced with the associated path if the handle
  // is cached in the open handle store. However, in this case the buffer was too small for the
  // system-returned filename but large enough for the replacement filename. This should succeed
  // transparently because the replacement filename fits, and that is all that matters to the
  // calling application.
  TEST_CASE(FilesystemExecutor_QueryByHandle_BufferTooSmallForSystemButFitsReplacement)
  {
    constexpr std::wstring_view kSystemReturnedPath =
        L"C:\\AVeryLong\\LongFilePath\\ThatDefinitelyWontFit\\File.txt";
    constexpr std::wstring_view kCachedAssociatedPath = L"D:\\E\\File.txt";

    HANDLE handleValue = reinterpret_cast<HANDLE>(3033345);
    IO_STATUS_BLOCK statusBlock{};

    uint8_t fileNameInformationBuffer[32] = {};
    SFileNameInformation& fileNameInformation =
        *reinterpret_cast<SFileNameInformation*>(fileNameInformationBuffer);

    static_assert(
        (kSystemReturnedPath.length() * sizeof(wchar_t)) > sizeof(fileNameInformationBuffer),
        "Path is not long enough to exceed the supplied buffer space.");

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        handleValue, std::wstring(kCachedAssociatedPath), std::wstring(kSystemReturnedPath));

    const NTSTATUS expectedReturnCode = NtStatus::kSuccess;
    const NTSTATUS actualReturnCode = FilesystemExecutor::QueryByHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        handleValue,
        &statusBlock,
        &fileNameInformation,
        sizeof(fileNameInformationBuffer),
        SFileNameInformation::kFileInformationClass,
        [kSystemReturnedPath](
            HANDLE,
            PIO_STATUS_BLOCK ioStatusBlock,
            PVOID fileInformation,
            ULONG length,
            FILE_INFORMATION_CLASS) -> NTSTATUS
        {
          SFileNameInformation* const fileNameInformation =
              reinterpret_cast<SFileNameInformation*>(fileInformation);
          ioStatusBlock->Status =
              CopyStringToFileNameInformation(kSystemReturnedPath, fileNameInformation, length);
          ioStatusBlock->Information = std::min(
              static_cast<ULONG_PTR>(length),
              static_cast<ULONG_PTR>(
                  FileInformationStructLayout::SizeOfStructByType<SFileNameInformation>(
                      *fileNameInformation)));
          return ioStatusBlock->Status;
        });

    TEST_ASSERT(actualReturnCode == expectedReturnCode);
    TEST_ASSERT(statusBlock.Status == expectedReturnCode);
    TEST_ASSERT(
        statusBlock.Information ==
        (kCachedAssociatedPath.length() * sizeof(wchar_t)) + sizeof(SFileNameInformation) -
            offsetof(SFileNameInformation, fileName));

    const std::wstring_view expectedQueryResultPath = kCachedAssociatedPath;
    const std::wstring_view actualQueryResultPath = std::wstring_view(
        fileNameInformation.fileName, fileNameInformation.fileNameLength / sizeof(wchar_t));
    TEST_ASSERT(actualQueryResultPath == expectedQueryResultPath);
  }

  // Verifies that a filename request by handle is replaced with the associated path if the handle
  // is cached in the open handle store. However, in this case the buffer was large enough for the
  // system-returned filename but not large enough for the replacement filename. The filesystem
  // executor is expected to write as many characters as will fit and set the filename length field
  // to indicate how much space is needed.
  TEST_CASE(FilesystemExecutor_QueryByHandle_BufferFitsSystemButTooSmallForReplacement)
  {
    constexpr std::wstring_view kSystemReturnedPath = L"C:\\A\\File.txt";
    constexpr std::wstring_view kCachedAssociatedPath =
        L"D:\\E\\SomeVeryLong\\LongPathThat\\CannotFitIn\\TheBufferProvided\\File.txt";

    HANDLE handleValue = reinterpret_cast<HANDLE>(3033345);
    IO_STATUS_BLOCK statusBlock{};

    uint8_t fileNameInformationBuffer[40] = {};
    SFileNameInformation& fileNameInformation =
        *reinterpret_cast<SFileNameInformation*>(fileNameInformationBuffer);

    // The buffer can hold 40 bytes, but only 32 are allowed to be used. The remaining should not be
    // touched and should stay equal to the value of the guard byte.
    constexpr uint8_t kGuardBufferByte = 0xfe;
    constexpr size_t fileNameInformationBufferAllowedBytes = 32;
    std::memset(fileNameInformationBuffer, kGuardBufferByte, sizeof(fileNameInformationBuffer));

    static_assert(
        (kCachedAssociatedPath.length() * sizeof(wchar_t)) > fileNameInformationBufferAllowedBytes,
        "Path is not long enough to exceed the supplied buffer space.");

    OpenHandleStore openHandleStore;
    openHandleStore.InsertHandle(
        handleValue, std::wstring(kCachedAssociatedPath), std::wstring(kSystemReturnedPath));

    const NTSTATUS expectedReturnCode = NtStatus::kBufferOverflow;
    const NTSTATUS actualReturnCode = FilesystemExecutor::QueryByHandle(
        TestCaseName().data(),
        kFunctionRequestIdentifier,
        openHandleStore,
        handleValue,
        &statusBlock,
        &fileNameInformation,
        fileNameInformationBufferAllowedBytes,
        SFileNameInformation::kFileInformationClass,
        [kSystemReturnedPath](
            HANDLE,
            PIO_STATUS_BLOCK ioStatusBlock,
            PVOID fileInformation,
            ULONG length,
            FILE_INFORMATION_CLASS) -> NTSTATUS
        {
          SFileNameInformation* const fileNameInformation =
              reinterpret_cast<SFileNameInformation*>(fileInformation);
          ioStatusBlock->Status =
              CopyStringToFileNameInformation(kSystemReturnedPath, fileNameInformation, length);
          ioStatusBlock->Information = std::min(
              static_cast<ULONG_PTR>(length),
              static_cast<ULONG_PTR>(
                  FileInformationStructLayout::SizeOfStructByType<SFileNameInformation>(
                      *fileNameInformation)));
          return ioStatusBlock->Status;
        });

    TEST_ASSERT(actualReturnCode == expectedReturnCode);
    TEST_ASSERT(statusBlock.Status == expectedReturnCode);
    TEST_ASSERT(statusBlock.Information == fileNameInformationBufferAllowedBytes);

    // Since the buffer capacity is too small, the required amount of buffer space is expected to be
    // placed into the file name length field.
    const size_t expectedFileNameLength = kCachedAssociatedPath.length() * sizeof(wchar_t);
    const size_t actualFileNameLength = static_cast<size_t>(fileNameInformation.fileNameLength);
    TEST_ASSERT(actualFileNameLength == expectedFileNameLength);

    const size_t writtenFileNamePortionLengthBytes =
        (fileNameInformationBufferAllowedBytes - offsetof(SFileNameInformation, fileName));
    const size_t writtenFileNamePortionLengthChars =
        writtenFileNamePortionLengthBytes / sizeof(wchar_t);

    // Only a portion of the correct file name should have been written, whatever will fit into the
    // buffer.
    const std::wstring_view expectedWrittenFileNamePortion =
        std::wstring_view(kCachedAssociatedPath.data(), writtenFileNamePortionLengthChars);
    const std::wstring_view actualWrittenFileNamePortion =
        std::wstring_view(fileNameInformation.fileName, writtenFileNamePortionLengthChars);
    TEST_ASSERT(actualWrittenFileNamePortion == expectedWrittenFileNamePortion);

    // This loop verifies that no bytes past the end of the buffer's allowed region have been
    // modified.
    for (size_t guardByteIdx = fileNameInformationBufferAllowedBytes;
         guardByteIdx < sizeof(fileNameInformationBuffer);
         ++guardByteIdx)
    {
      TEST_ASSERT(kGuardBufferByte == fileNameInformationBuffer[guardByteIdx]);
    }
  }
} // namespace PathwinderTest
