/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file BufferPool.h
 *   Implementation of an object that maintains a pool of fixed-size dynamically-allocated
 *   buffers.
 **************************************************************************************************/

#pragma once

#include <array>
#include <cstddef>

#include <Infra/Core/Mutex.h>

namespace Pathwinder
{
  /// Manages a pool of fixed-size dynamically-allocated buffers.
  /// Allocates as many as needed but only holds up to a specified number of buffers once they are
  /// returned. Objects of this class are intended to be long-lived, ideally right up until
  /// program termination. They do not deallocate free buffers in the pool on destruction, nor do
  /// they ever attempt to reclaim buffers that have been allocated but not yet freed.
  /// @tparam kBytesPerBuffer Size of each buffer, in bytes.
  /// @tparam kAllocationGranularity Number of buffers to allocate initially and each time the
  /// pool is exhausted and more are needed.
  /// @tparam kPoolSize Maximum number of buffers to hold in the pool. If more buffers are needed
  /// beyond this number, then they will be deallocated when freed instead of returned to the
  /// pool.
  template <
      unsigned int kBytesPerBuffer,
      unsigned int kAllocationGranularity,
      unsigned int kPoolSize>
  class BufferPool
  {
  public:

    inline BufferPool(void) : availableBuffers(), numAvailableBuffers(0), allocationMutex()
    {
      AllocateMoreBuffers();
    }

    BufferPool(const BufferPool& other) = delete;

    BufferPool(BufferPool&& other) = delete;

    /// Allocates a buffer for the caller to use.
    /// @return Buffer that the caller can use.
    void* Allocate(void)
    {
      std::scoped_lock lock(allocationMutex);

      if (0 == numAvailableBuffers) AllocateMoreBuffers();

      numAvailableBuffers -= 1;
      uint8_t* nextFreeBuffer = availableBuffers[numAvailableBuffers];

      return nextFreeBuffer;
    }

    /// Deallocates a buffer once the caller is finished with it.
    /// @param [in] Previously-allocated buffer that the caller is returning.
    void Free(void* buffer)
    {
      std::scoped_lock lock(allocationMutex);

      if (numAvailableBuffers == kPoolSize)
      {
        delete[] buffer;
      }
      else
      {
        availableBuffers[numAvailableBuffers] = reinterpret_cast<uint8_t*>(buffer);
        numAvailableBuffers += 1;
      }
    }

  private:

    /// Allocates more buffers and places them into the available buffers data structure.
    /// Not concurrency-safe. Intended to be invoked as part of an otherwise-guarded operation.
    void AllocateMoreBuffers(void)
    {
      for (unsigned int i = 0; i < kAllocationGranularity; ++i)
      {
        if (numAvailableBuffers == kPoolSize) break;

        availableBuffers[numAvailableBuffers] = new uint8_t[kBytesPerBuffer];
        numAvailableBuffers += 1;
      }
    }

    /// Holds available buffers that are already allocated and can be distributed as needed.
    std::array<uint8_t*, kPoolSize> availableBuffers;

    /// Number of available buffers.
    unsigned int numAvailableBuffers;

    /// Mutex used to ensure concurrency control over temporary buffer allocation and
    /// deallocation.
    Infra::Mutex allocationMutex;
  };
} // namespace Pathwinder
