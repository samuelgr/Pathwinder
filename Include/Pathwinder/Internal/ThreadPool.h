/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file ThreadPool.h
 *   Declaration of thread pool functionality for asynchronously and concurrently managing and
 *   processing multiple work items.
 **************************************************************************************************/

#pragma once

#include <optional>

#include <Infra/Core/Mutex.h>

#include "ApiWindows.h"

namespace Pathwinder
{
  /// Simple wrapper class around the Windows thread pool API.
  class ThreadPool
  {
  public:

    ~ThreadPool(void);

    ThreadPool(const ThreadPool& other) = delete;

    ThreadPool(ThreadPool&& other) noexcept;

    ThreadPool& operator=(const ThreadPool& other) = delete;

    ThreadPool& operator=(ThreadPool&& other) = default;

    /// Attempts to create a thread pool and, on success, returns the resulting object.
    /// @return Newly-created thread pool object, if successful.
    static std::optional<ThreadPool> Create(void);

    /// Attempts to submit a work item to this thread pool.
    /// @param [in] functionToInvoke Callback function to invoke when processing this work item.
    /// @param [in] contextParam Context parameter to pass to the work item's callback function.
    /// @return `true` if successful, `false` otherwise.
    bool SubmitWork(PTP_SIMPLE_CALLBACK functionToInvoke, void* contextParam);

    /// Waits for all outstanding work items to be completed. Calling this method does not prevent
    /// new work items from being submitted.
    void WaitForOutstandingWork(void);

  private:

    ThreadPool(PTP_POOL threadPool, PTP_CLEANUP_GROUP threadPoolCleanupGroup);

    /// Ensures proper concurrency control of the thread pool itself.
    Infra::SharedMutex workItemMutex;

    /// Underlying thread pool object. Refer to the Windows thread pool API documentation for more
    /// information.
    PTP_POOL threadPool;

    /// Underlying thread pool cleanup group object. Refer to the Windows thread pool API
    /// documentation for more information.
    PTP_CLEANUP_GROUP threadPoolCleanupGroup;

    /// Underlying thread pool environment object. Refer to the Windows thread pool API
    /// documentation for more information.
    TP_CALLBACK_ENVIRON threadPoolEnvironment;
  };
} // namespace Pathwinder
