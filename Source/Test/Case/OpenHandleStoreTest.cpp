/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file OpenHandleStoreTest.cpp
 *   Unit tests for open file handle state and metadata storage and manipulation functionality.
 **************************************************************************************************/

#include "TestCase.h"

#include "OpenHandleStore.h"

#include <string>
#include <string_view>

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  // Verifies that a valid handle can be inserted into the open handle store and its associated data
  // successfully retrieved.
  TEST_CASE(OpenHandleStore_InsertAndGetDataForHandle_Nominal)
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
  TEST_CASE(OpenHandleStore_InsertAndGetDataForHandle_DuplicateInsertion)
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
      ScopedExpectDebugAssertion();
      handleStore.InsertHandle(
          kHandle,
          std::wstring(kHandleAssociatedPathDuplicate),
          std::wstring(kHandleRealOpenedPathDuplicate));
    }
    catch (DebugAssertionException assertion)
    {
      TEST_ASSERT(assertion.GetFailureMessage().contains("insert a handle"));
    }

    constexpr OpenHandleStore::SHandleDataView expectedHandleData = {
        .associatedPath = kHandleAssociatedPath, .realOpenedPath = kHandleRealOpenedPath};
    const OpenHandleStore::SHandleDataView actualHandleData =
        *handleStore.GetDataForHandle(kHandle);
    TEST_ASSERT(actualHandleData == expectedHandleData);
  }

  // Verifies that a handle that has not been inserted into the open handle store cannot have its
  // data retrieved.
  TEST_CASE(OpenHandleStore_InsertAndGetDataForHandle_NonExistentHandle)
  {
    const HANDLE kHandle = reinterpret_cast<HANDLE>(0x12345678);

    OpenHandleStore handleStore;

    TEST_ASSERT(false == handleStore.GetDataForHandle(kHandle).has_value());
  }
} // namespace PathwinderTest
