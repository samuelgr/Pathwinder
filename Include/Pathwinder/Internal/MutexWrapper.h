/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file MutexWrapper.h
 *   Declaration and implementation of simple wrappers around standard
 *   concurrency control mechanisms for higher performance on Windows.
 *****************************************************************************/

#pragma once

#include "ApiWindows.h"

#include <shared_mutex>


namespace Pathwinder
{
    /// Standard mutex, which is not recursive and must be exclusively locked.
    /// Meets all of the `Lockable` requirements for compatibility with all standard C++ lock objects.
    /// Implemented as a wrapper around `std::shared_mutex` but with the shared features removed for perfect compatibility with `std::mutex`.
    /// On Windows, `std::shared_mutex` is implemented using much more modern and performant mechanisms compared to `std::mutex`.
    /// See https://stackoverflow.com/questions/69990339/why-is-stdmutex-so-much-worse-than-stdshared-mutex-in-visual-c for more information on performance.
    /// The entire interface to this class mirrors that of `std::mutex`. See C++ standard documentation for more information.
    class Mutex
    {
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Native handle type definition.
        using native_handle_type = std::shared_mutex::native_handle_type;


    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Wrapped mutex object.
        std::shared_mutex mutexObject;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        Mutex(void) = default;

        /// Copy constructor. Should never be invoked.
        Mutex(const Mutex& other) = delete;

        /// Default destructor.
        ~Mutex(void) = default;


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Locks this mutex.
        inline void lock(void)
        {
            mutexObject.lock();
        }

        /// Attempts to lock this mutex without blocking.
        inline bool try_lock(void)
        {
            return mutexObject.try_lock();
        }

        /// Unlocks this mutex.
        inline void unlock(void)
        {
            mutexObject.unlock();
        }

        /// Retrieves the native handle for this mutex.
        inline native_handle_type native_handle(void)
        {
            return mutexObject.native_handle();
        }
    };

    /// Recursive mutex, which can be locked multiple times by the same thread as long as it is unlocked the same number of times.
    /// Meets all of the `Lockable` requirements for compatibility with all standard C++ lock objects.
    /// Implemented as a wrapper around a CRITICAL_SECTION object, which is a re-entrant form of concurrency control object.
    class RecursiveMutex
    {
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Native handle type definition.
        using native_handle_type = void*;


    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Wrapped CRITICAL_SECTION object.
        CRITICAL_SECTION criticalSectionObject;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        inline RecursiveMutex(void)
        {
            // Microsoft's own heap manager uses a spin count of 4000, which improves performance for code sequences of short duration.
            // The return value for #InitializeCriticalSectionAndSpinCount is safe to ignore because on modern versions of Windows this function always succeeds and returns non-zero.
            // See https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializecriticalsectionandspincount for more information.
            std::ignore = InitializeCriticalSectionAndSpinCount(&criticalSectionObject, 4000);
        }

        /// Copy constructor. Should never be invoked.
        RecursiveMutex(const RecursiveMutex& other) = delete;

        /// Default destructor.
        inline ~RecursiveMutex(void)
        {
            DeleteCriticalSection(&criticalSectionObject);
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Locks this mutex.
        inline void lock(void)
        {
            EnterCriticalSection(&criticalSectionObject);
        }

        /// Attempts to lock this mutex without blocking.
        inline bool try_lock(void)
        {
            return (0 != TryEnterCriticalSection(&criticalSectionObject));
        }

        /// Unlocks this mutex.
        inline void unlock(void)
        {
            LeaveCriticalSection(&criticalSectionObject);
        }

        /// Retrieves the native handle for this mutex.
        inline native_handle_type native_handle(void)
        {
            return &criticalSectionObject;
        }
    };

    /// Shared mutex, also known as a reader/writer mutex.
    /// The existing Windows implementation uses SRWLock, which is as modern and performant as it gets, so no wrapping is necessary.
    /// See https://stackoverflow.com/questions/69990339/why-is-stdmutex-so-much-worse-than-stdshared-mutex-in-visual-c for more information on performance.
    using SharedMutex = std::shared_mutex;
}
