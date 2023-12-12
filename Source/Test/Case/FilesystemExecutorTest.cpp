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

#include "ApiWindows.h"
#include "MockFilesystemOperations.h"
#include "OpenHandleStore.h"
#include "ValueOrError.h"

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  /// Function request identifier to be passed to all filesystem executor functions when they are
  /// invoked for testing.
  static unsigned int kFunctionRequestIdentifier = 0;

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
} // namespace PathwinderTest
