/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file ThreadPool.cpp
 *   Implementation of thread pool functionality for asynchronously and concurrently managing and
 *   processing multiple work items.
 **************************************************************************************************/

#include "ThreadPool.h"

#include <utility>

#include <Infra/Core/Mutex.h>

#include "ApiWindows.h"

namespace Pathwinder
{
  ThreadPool::ThreadPool(PTP_POOL threadPool, PTP_CLEANUP_GROUP threadPoolCleanupGroup)
      : workItemMutex(),
        threadPool(threadPool),
        threadPoolCleanupGroup(threadPoolCleanupGroup),
        threadPoolEnvironment()
  {
    InitializeThreadpoolEnvironment(&threadPoolEnvironment);
    SetThreadpoolCallbackPool(&threadPoolEnvironment, threadPool);
    SetThreadpoolCallbackCleanupGroup(&threadPoolEnvironment, threadPoolCleanupGroup, nullptr);
  }

  ThreadPool::ThreadPool(ThreadPool&& other) noexcept
      : workItemMutex(),
        threadPool(nullptr),
        threadPoolCleanupGroup(nullptr),
        threadPoolEnvironment()
  {
    std::unique_lock lock(other.workItemMutex);

    std::swap(threadPool, other.threadPool);
    std::swap(threadPoolCleanupGroup, other.threadPoolCleanupGroup);
    std::swap(threadPoolEnvironment, other.threadPoolEnvironment);
  }

  ThreadPool::~ThreadPool(void)
  {
    std::unique_lock lock(workItemMutex);

    DestroyThreadpoolEnvironment(&threadPoolEnvironment);

    if (nullptr != threadPoolCleanupGroup)
    {
      CloseThreadpoolCleanupGroupMembers(threadPoolCleanupGroup, TRUE, nullptr);
      CloseThreadpoolCleanupGroup(threadPoolCleanupGroup);
    }

    if (nullptr != threadPool)
    {
      CloseThreadpool(threadPool);
    }
  }

  std::optional<ThreadPool> ThreadPool::Create(void)
  {
    PTP_POOL newThreadPool = CreateThreadpool(nullptr);

    if (nullptr != newThreadPool)
    {
      PTP_CLEANUP_GROUP newCleanupGroup = CreateThreadpoolCleanupGroup();

      if (nullptr != newCleanupGroup)
        return ThreadPool(newThreadPool, newCleanupGroup);
      else
        CloseThreadpool(newThreadPool);
    }

    return std::nullopt;
  }

  bool ThreadPool::SubmitWork(PTP_SIMPLE_CALLBACK functionToInvoke, void* contextParam)
  {
    std::shared_lock lock(workItemMutex, std::try_to_lock);
    if (false == lock.owns_lock()) return false;

    return (
        TRUE ==
        TrySubmitThreadpoolCallback(functionToInvoke, contextParam, &threadPoolEnvironment));
  }

  void ThreadPool::WaitForOutstandingWork(void)
  {
    std::shared_lock lock(workItemMutex, std::try_to_lock);
    if (false == lock.owns_lock()) return;

    CloseThreadpoolCleanupGroupMembers(threadPoolCleanupGroup, FALSE, nullptr);
  }
} // namespace Pathwinder
