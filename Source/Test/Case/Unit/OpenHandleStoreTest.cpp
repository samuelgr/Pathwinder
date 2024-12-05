/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file OpenHandleStoreTest.cpp
 *   Unit tests for open file handle state and metadata storage and manipulation functionality.
 **************************************************************************************************/

#include "TestCase.h"

#include "OpenHandleStore.h"

#include <memory>
#include <string>
#include <string_view>

#include "ApiWindows.h"
#include "FileInformationStruct.h"
#include "MockDirectoryOperationQueue.h"
#include "MockFilesystemOperations.h"

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  // Verifies that a valid handle can be inserted into the open handle store and its associated data
  // successfully retrieved.
  TEST_CASE(OpenHandleStore_InsertHandle_Nominal)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);
    constexpr std::wstring_view kHandleAssociatedPath = L"associated_path";
    constexpr std::wstring_view kHandleRealOpenedPath = L"real_opened_path";

    OpenHandleStore handleStore;

    handleStore.InsertHandle(
        kHandle, std::wstring(kHandleAssociatedPath), std::wstring(kHandleRealOpenedPath));
    TEST_ASSERT(true == handleStore.GetDataForHandle(kHandle).has_value());

    constexpr OpenHandleStore::SHandleDataView expectedHandleData = {
        .associatedPath = kHandleAssociatedPath, .realOpenedPath = kHandleRealOpenedPath};
    const OpenHandleStore::SHandleDataView actualHandleData =
        *handleStore.GetDataForHandle(kHandle);
    TEST_ASSERT(actualHandleData == expectedHandleData);
  }

  // Verifies that a valid handle can be inserted into the open handle store and its associated data
  // retrieved, but that in the event of a duplicate insertion the first insertion's data is kept
  // and the second insertion's data is ignored.
  TEST_CASE(OpenHandleStore_InsertHandle_DuplicateInsertion)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);
    constexpr std::wstring_view kHandleAssociatedPath = L"associated_path";
    constexpr std::wstring_view kHandleRealOpenedPath = L"real_opened_path";
    constexpr std::wstring_view kHandleAssociatedPathDuplicate = L"associated_path_duplicate";
    constexpr std::wstring_view kHandleRealOpenedPathDuplicate = L"real_opened_path_duplicate";

    OpenHandleStore handleStore;

    handleStore.InsertHandle(
        kHandle, std::wstring(kHandleAssociatedPath), std::wstring(kHandleRealOpenedPath));

    // Inserting a duplicate handle is a serious error that can potentially trigger a debug
    // assertion.
    try
    {
      handleStore.InsertHandle(
          kHandle,
          std::wstring(kHandleAssociatedPathDuplicate),
          std::wstring(kHandleRealOpenedPathDuplicate));
    }
    catch (const Infra::DebugAssertionException& assertion)
    {
      TEST_ASSERT(assertion.GetFailureMessage().contains(L"insert a handle"));
    }

    constexpr OpenHandleStore::SHandleDataView expectedHandleData = {
        .associatedPath = kHandleAssociatedPath, .realOpenedPath = kHandleRealOpenedPath};
    const OpenHandleStore::SHandleDataView actualHandleData =
        *handleStore.GetDataForHandle(kHandle);
    TEST_ASSERT(actualHandleData == expectedHandleData);
  }

  // Verifies that a valid handle can be inserted into the open handle store and its associated data
  // retrieved, then further that it can be updated and the updated data retrieved.
  TEST_CASE(OpenHandleStore_InsertOrUpdateHandle_Nominal)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);
    constexpr std::wstring_view kHandleAssociatedPath = L"associated_path";
    constexpr std::wstring_view kHandleRealOpenedPath = L"real_opened_path";

    OpenHandleStore handleStore;

    handleStore.InsertOrUpdateHandle(
        kHandle, std::wstring(kHandleAssociatedPath), std::wstring(kHandleRealOpenedPath));
    TEST_ASSERT(true == handleStore.GetDataForHandle(kHandle).has_value());

    constexpr OpenHandleStore::SHandleDataView expectedHandleData = {
        .associatedPath = kHandleAssociatedPath, .realOpenedPath = kHandleRealOpenedPath};
    const OpenHandleStore::SHandleDataView actualHandleData =
        *handleStore.GetDataForHandle(kHandle);
    TEST_ASSERT(actualHandleData == expectedHandleData);

    constexpr std::wstring_view kHandleAssociatedPathUpdated = L"associated_path_updated";
    constexpr std::wstring_view kHandleRealOpenedPathUpdated = L"real_opened_path_updated";

    handleStore.InsertOrUpdateHandle(
        kHandle,
        std::wstring(kHandleAssociatedPathUpdated),
        std::wstring(kHandleRealOpenedPathUpdated));
    TEST_ASSERT(true == handleStore.GetDataForHandle(kHandle).has_value());

    constexpr OpenHandleStore::SHandleDataView expectedHandleDataUpdated = {
        .associatedPath = kHandleAssociatedPathUpdated,
        .realOpenedPath = kHandleRealOpenedPathUpdated};
    const OpenHandleStore::SHandleDataView actualHandleDataUpdated =
        *handleStore.GetDataForHandle(kHandle);
    TEST_ASSERT(actualHandleDataUpdated == expectedHandleDataUpdated);
  }

  // Verifies that a handle that has not been inserted into the open handle store cannot have its
  // data retrieved.
  TEST_CASE(OpenHandleStore_GetDataForHandle_NonExistentHandle)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);

    OpenHandleStore handleStore;

    TEST_ASSERT(false == handleStore.GetDataForHandle(kHandle).has_value());
  }

  // Verifies that a handle can be inserted and subsequently removed from the open handle store.
  // Upon removal, verifies that the associated data are correctly retrieved.
  TEST_CASE(OpenHandleStore_RemoveHandle_Nominal)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);
    constexpr std::wstring_view kHandleAssociatedPath = L"associated_path";
    constexpr std::wstring_view kHandleRealOpenedPath = L"real_opened_path";

    OpenHandleStore handleStore;

    handleStore.InsertHandle(
        kHandle, std::wstring(kHandleAssociatedPath), std::wstring(kHandleRealOpenedPath));

    constexpr OpenHandleStore::SHandleDataView expectedHandleData = {
        .associatedPath = kHandleAssociatedPath, .realOpenedPath = kHandleRealOpenedPath};
    OpenHandleStore::SHandleData actualHandleData{};

    TEST_ASSERT(true == handleStore.RemoveHandle(kHandle, &actualHandleData));
    TEST_ASSERT(false == handleStore.GetDataForHandle(kHandle).has_value());
    TEST_ASSERT(actualHandleData == expectedHandleData);
  }

  // Similar to the nominal test case for handle removal, except this test case does not request
  // that the associated data be retrieved. Successful removal is still expected.
  TEST_CASE(OpenHandleStore_RemoveHandle_IgnoreAssociatedData)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);
    constexpr std::wstring_view kHandleAssociatedPath = L"associated_path";
    constexpr std::wstring_view kHandleRealOpenedPath = L"real_opened_path";

    OpenHandleStore handleStore;

    handleStore.InsertHandle(
        kHandle, std::wstring(kHandleAssociatedPath), std::wstring(kHandleRealOpenedPath));
    TEST_ASSERT(true == handleStore.RemoveHandle(kHandle, nullptr));
  }

  // Verifies that a handle that has not been inserted into the open handle store cannot be removed.
  TEST_CASE(OpenHandleStore_RemoveHandle_NonExistentHandle)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);

    OpenHandleStore handleStore;

    TEST_ASSERT(false == handleStore.RemoveHandle(kHandle, nullptr));
  }

  // Verifies that a handle representing an open filesystem entity can be inserted into the open
  // handle store and that a subsequent remove-and-close operation on it succeeds.
  TEST_CASE(OpenHandleStore_RemoveAndCloseHandle_Nominal)
  {
    // The actual path represented by the fake handle is unimportant. It just needs to be present in
    // the mock filesystem and have an associated handle opened for it, the value of which is
    // determined internally.
    constexpr std::wstring_view kHandleDirectoryPath = L"C:\\TestDirectory";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kHandleDirectoryPath);

    const HANDLE kHandle = mockFilesystem.Open(kHandleDirectoryPath);

    OpenHandleStore handleStore;
    handleStore.InsertHandle(
        kHandle, std::wstring(kHandleDirectoryPath), std::wstring(kHandleDirectoryPath));

    constexpr OpenHandleStore::SHandleDataView expectedHandleData = {
        .associatedPath = kHandleDirectoryPath, .realOpenedPath = kHandleDirectoryPath};
    OpenHandleStore::SHandleData actualHandleData{};

    TEST_ASSERT(NtStatus::kSuccess == handleStore.RemoveAndCloseHandle(kHandle, &actualHandleData));
    TEST_ASSERT(false == handleStore.GetDataForHandle(kHandle).has_value());
    TEST_ASSERT(actualHandleData == expectedHandleData);
  }

  // Similar to the nominal test case for handle removal, except this test case does not request
  // that the associated data be retrieved. Successful removal is still expected.
  TEST_CASE(OpenHandleStore_RemoveAndCloseHandle_IgnoreAssociatedData)
  {
    // The actual path represented by the fake handle is unimportant. It just needs to be present in
    // the mock filesystem and have an associated handle opened for it, the value of which is
    // determined internally.
    constexpr std::wstring_view kHandleDirectoryPath = L"C:\\TestDirectory";

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.AddDirectory(kHandleDirectoryPath);

    const HANDLE kHandle = mockFilesystem.Open(kHandleDirectoryPath);

    OpenHandleStore handleStore;
    handleStore.InsertHandle(
        kHandle, std::wstring(kHandleDirectoryPath), std::wstring(kHandleDirectoryPath));

    TEST_ASSERT(NtStatus::kSuccess == handleStore.RemoveAndCloseHandle(kHandle, nullptr));
    TEST_ASSERT(false == handleStore.GetDataForHandle(kHandle).has_value());
  }

  // Verifies that a handle that has not been inserted into the open handle store cannot be closed
  // and removed. Attempting to do this is a serious error that could trigger a debug assertion.
  TEST_CASE(OpenHandleStore_RemoveAndCloseHandle_NonExistentHandle)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);

    MockFilesystemOperations mockFilesystem;
    mockFilesystem.SetConfigAllowCloseInvalidHandle(true);

    OpenHandleStore handleStore;

    // Attempting to close a handle that is not open is a serious error that could potentially
    // trigger a debug assertion. If it does not, then at very least the return code should indicate
    // failure.
    try
    {
      NTSTATUS closeInvalidHandleResult = handleStore.RemoveAndCloseHandle(kHandle, nullptr);
      TEST_ASSERT(!NT_SUCCESS(closeInvalidHandleResult));
    }
    catch (const Infra::DebugAssertionException& assertion)
    {
      TEST_ASSERT(assertion.GetFailureMessage().contains(L"close and erase a handle"));
    }
  }

  // Verifies that a directory enumeration state can be associated with a valid, already-stored
  // handle.
  TEST_CASE(OpenHandleStore_AssociateDirectoryEnumerationState_Nominal)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);
    constexpr std::wstring_view kHandleAssociatedPath = L"associated_path";
    constexpr std::wstring_view kHandleRealOpenedPath = L"real_opened_path";
    constexpr FileInformationStructLayout kTestFileInformationStructLayout(
        static_cast<FILE_INFORMATION_CLASS>(100), 200, 300, 400, 500);
    std::unique_ptr<MockDirectoryOperationQueue> testDirectoryOperationQueue =
        std::make_unique<MockDirectoryOperationQueue>();

    OpenHandleStore handleStore;

    handleStore.InsertHandle(
        kHandle, std::wstring(kHandleAssociatedPath), std::wstring(kHandleRealOpenedPath));

    constexpr std::wstring_view expectedAssociatedPath = kHandleAssociatedPath;
    constexpr std::wstring_view expectedRealOpenedPath = kHandleRealOpenedPath;
    const IDirectoryOperationQueue* const expectedDirectoryOperationQueue =
        testDirectoryOperationQueue.get();
    const FileInformationStructLayout& expectedFileInformationStructLayout =
        kTestFileInformationStructLayout;

    handleStore.AssociateDirectoryEnumerationState(
        kHandle, std::move(testDirectoryOperationQueue), kTestFileInformationStructLayout);

    OpenHandleStore::SHandleDataView actualHandleData = *handleStore.GetDataForHandle(kHandle);
    TEST_ASSERT(actualHandleData.directoryEnumeration.has_value());

    const std::wstring_view actualAssociatedPath = actualHandleData.associatedPath;
    TEST_ASSERT(actualAssociatedPath == expectedAssociatedPath);

    const std::wstring_view actualRealOpenedPath = actualHandleData.realOpenedPath;
    TEST_ASSERT(actualRealOpenedPath == expectedRealOpenedPath);

    const IDirectoryOperationQueue* const actualDirectoryOperationQueue =
        (*actualHandleData.directoryEnumeration)->queue.get();
    TEST_ASSERT(actualDirectoryOperationQueue == expectedDirectoryOperationQueue);

    const FileInformationStructLayout actualFileInformationStructLayout =
        (*actualHandleData.directoryEnumeration)->fileInformationStructLayout;
    TEST_ASSERT(actualFileInformationStructLayout == expectedFileInformationStructLayout);
  }

  TEST_CASE(OpenHandleStore_AssociateDirectoryEnumerationState_DuplicateAssociation)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);
    constexpr std::wstring_view kHandleAssociatedPath = L"associated_path";
    constexpr std::wstring_view kHandleRealOpenedPath = L"real_opened_path";
    constexpr FileInformationStructLayout kTestFileInformationStructLayout(
        static_cast<FILE_INFORMATION_CLASS>(100), 200, 300, 400, 500);
    std::unique_ptr<MockDirectoryOperationQueue> testDirectoryOperationQueue =
        std::make_unique<MockDirectoryOperationQueue>();

    OpenHandleStore handleStore;

    handleStore.InsertHandle(
        kHandle, std::wstring(kHandleAssociatedPath), std::wstring(kHandleRealOpenedPath));

    constexpr std::wstring_view expectedAssociatedPath = kHandleAssociatedPath;
    constexpr std::wstring_view expectedRealOpenedPath = kHandleRealOpenedPath;
    const IDirectoryOperationQueue* const expectedDirectoryOperationQueue =
        testDirectoryOperationQueue.get();
    const FileInformationStructLayout& expectedFileInformationStructLayout =
        kTestFileInformationStructLayout;

    handleStore.AssociateDirectoryEnumerationState(
        kHandle, std::move(testDirectoryOperationQueue), kTestFileInformationStructLayout);

    // Attempting to associate a directory enumeration with a handle that already has one is a
    // serious error that could potentially trigger a debug assertion. If it does not, then the
    // execution will continue, but what happens to the associated enumeration queue and file
    // information structure layout is not defined.
    try
    {
      handleStore.AssociateDirectoryEnumerationState(
          kHandle, nullptr, FileInformationStructLayout());
    }
    catch (const Infra::DebugAssertionException& assertion)
    {
      TEST_ASSERT(assertion.GetFailureMessage().contains(L"handle that already has one"));
    }

    OpenHandleStore::SHandleDataView actualHandleData = *handleStore.GetDataForHandle(kHandle);
    TEST_ASSERT(actualHandleData.directoryEnumeration.has_value());

    const std::wstring_view actualAssociatedPath = actualHandleData.associatedPath;
    TEST_ASSERT(actualAssociatedPath == expectedAssociatedPath);

    const std::wstring_view actualRealOpenedPath = actualHandleData.realOpenedPath;
    TEST_ASSERT(actualRealOpenedPath == expectedRealOpenedPath);
  }

  // Verifies that a directory enumeration state cannot be associated with a handle that was not
  // previously stored in the open handle store. Attempting to do this is a serious error that could
  // result in a debug assertion failure.
  TEST_CASE(OpenHandleStore_AssociateDirectoryEnumerationState_NonExistentHandle)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);

    OpenHandleStore handleStore;

    // Attempting to associate a directory enumeration with a handle not in the open handle store is
    // a serious error that could potentially trigger a debug assertion.
    try
    {
      handleStore.AssociateDirectoryEnumerationState(
          kHandle, nullptr, FileInformationStructLayout());
    }
    catch (const Infra::DebugAssertionException& assertion)
    {
      TEST_ASSERT(assertion.GetFailureMessage().contains(L"handle that is not in storage"));
    }
  }
} // namespace PathwinderTest
