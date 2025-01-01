/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file ThreadPoolTest.cpp
 *   Unit tests for thread pool functionality.
 **************************************************************************************************/

#include "ThreadPool.h"

#include <atomic>

#include <Infra/Test/TestCase.h>

#include "ApiWindows.h"

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  // Nominally verifies that submitting a single work item to a thread pool results in the work item
  // executing and completing successfully.
  TEST_CASE(ThreadPool_SingleWork)
  {
    bool callbackInvoked = false;

    std::optional<ThreadPool> threadPool = ThreadPool::Create();
    TEST_ASSERT(threadPool.has_value());

    TEST_ASSERT(
        true ==
        threadPool->SubmitWork(
            [](PTP_CALLBACK_INSTANCE, PVOID param) -> void
            {
              *(reinterpret_cast<bool*>(param)) = true;
            },
            &callbackInvoked));

    threadPool->WaitForOutstandingWork();
    TEST_ASSERT(true == callbackInvoked);
  }

  // Verifies that multiple work items can be submitted to the thread pool and that they all execute
  // and complete successfully.
  TEST_CASE(ThreadPool_MultipleWork)
  {
    constexpr int expectedNumCallbacksInvoked = 10000;
    std::atomic<int> actualNumCallbacksInvoked = 0;

    std::optional<ThreadPool> threadPool = ThreadPool::Create();
    TEST_ASSERT(threadPool.has_value());

    for (int i = 0; i < expectedNumCallbacksInvoked; ++i)
    {
      TEST_ASSERT(
          true ==
          threadPool->SubmitWork(
              [](PTP_CALLBACK_INSTANCE, PVOID param) -> void
              {
                *(reinterpret_cast<std::atomic<int>*>(param)) += 1;
              },
              &actualNumCallbacksInvoked));
    }

    threadPool->WaitForOutstandingWork();
    TEST_ASSERT(actualNumCallbacksInvoked == expectedNumCallbacksInvoked);
  }

  // Verifies that thread pool deletion results in outstanding work item requests being terminated.
  TEST_CASE(ThreadPool_CancelAndTerminate)
  {
    constexpr int kNumWorkItemsSubmitted = 10000;
    std::atomic<int> numWorkItemsCompleted = 0;

    do
    {
      std::optional<ThreadPool> threadPool = ThreadPool::Create();
      TEST_ASSERT(threadPool.has_value());

      for (int i = 0; i < kNumWorkItemsSubmitted; ++i)
      {
        threadPool->SubmitWork(
            [](PTP_CALLBACK_INSTANCE, PVOID param) -> void
            {
              Sleep(1);
              *(reinterpret_cast<std::atomic<int>*>(param)) += 1;
            },
            &numWorkItemsCompleted);
      }

      for (int i = 0; i < 10; ++i)
      {
        if (numWorkItemsCompleted > 0) break;
        Sleep(1);
      }
    }
    while (false);

    TEST_ASSERT(numWorkItemsCompleted > 0);
    TEST_ASSERT(numWorkItemsCompleted < kNumWorkItemsSubmitted);
  }

  // Verifies that multiple work items can be submitted to the thread pool and that they all execute
  // and complete successfully, even when the thread pool object is move-assigned in the middle of
  // the work.
  TEST_CASE(ThreadPool_AssignDuringMultipleWork)
  {
    constexpr int expectedNumCallbacksInvoked = 100;
    std::atomic<int> actualNumCallbacksInvoked = 0;

    std::optional<ThreadPool> threadPool = ThreadPool::Create();
    TEST_ASSERT(threadPool.has_value());

    for (int i = 0; i < expectedNumCallbacksInvoked; ++i)
    {
      TEST_ASSERT(
          true ==
          threadPool->SubmitWork(
              [](PTP_CALLBACK_INSTANCE, PVOID param) -> void
              {
                Sleep(1);
                *(reinterpret_cast<std::atomic<int>*>(param)) += 1;
              },
              &actualNumCallbacksInvoked));
    }

    for (int i = 0; i < 10; ++i)
    {
      if (actualNumCallbacksInvoked > 0) break;
      Sleep(1);
    }

    ThreadPool secondThreadPool(std::move(*threadPool));
    threadPool = std::nullopt;

    secondThreadPool.WaitForOutstandingWork();
    TEST_ASSERT(actualNumCallbacksInvoked == expectedNumCallbacksInvoked);
  }
} // namespace PathwinderTest
