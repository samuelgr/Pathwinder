/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry keys.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file TemporaryBuffer.h
 *   Declaration of temporary buffer management functionality.
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>


namespace Pathwinder
{
    /// Manages a global set of temporary buffers.
    /// These can be used for any purpose and are intended to replace large stack-allocated or heap-allocated buffers.
    /// Instead, memory is allocated statically at load-time and divided up as needed to various parts of the application.
    /// If too many buffers are allocated such that the available static buffers are exhausted, additional objects will allocate heap memory.
    /// All temporary buffer functionality is concurrency-safe and available as early as dynamic initialization.
    /// Do not instantiate this class directly; instead, instantiate the template class below.
    class TemporaryBufferBase
    {
    public:
        // -------- CONSTANTS -------------------------------------------------- //

        /// Specifies the total size of all temporary buffers, in bytes.
        static constexpr unsigned int kBuffersTotalNumBytes = 1 * 1024 * 1024;

        /// Specifies the number of temporary buffers to create statically.
        /// Even once this limit is reached buffers can be allocated but they are dynamically heap-allocated.
        static constexpr unsigned int kBuffersCount = 8;

        /// Specifies the size of each temporary buffer.
        static constexpr unsigned int kBytesPerBuffer = kBuffersTotalNumBytes / kBuffersCount;


    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Pointer to the buffer space.
        uint8_t* buffer;

        /// Specifies if the buffer space is heap-allocated.
        bool isHeapAllocated;


    protected:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        TemporaryBufferBase(void);

        /// Default destructor.
        ~TemporaryBufferBase(void);

        /// Move constructor.
        inline TemporaryBufferBase(TemporaryBufferBase&& other) : buffer(nullptr), isHeapAllocated(false)
        {
            *this = std::move(other);
        }


        // -------- OPERATORS ---------------------------------------------- //

        /// Move assignment operator.
        inline TemporaryBufferBase& operator=(TemporaryBufferBase&& other)
        {
            std::swap(buffer, other.buffer);
            std::swap(isHeapAllocated, other.isHeapAllocated);
            return *this;
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Retrieves the buffer pointer.
        /// @return Buffer pointer.
        inline uint8_t* Buffer(void) const
        {
            return buffer;
        }
    };

    /// Implements type-specific temporary buffer functionality.
    template <typename T> class TemporaryBuffer : public TemporaryBufferBase
    {
    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        inline TemporaryBuffer(void) : TemporaryBufferBase()
        {
            // Nothing to do here.
        }

        /// Move constructor.
        inline TemporaryBuffer(TemporaryBuffer&& other) : TemporaryBufferBase(std::move(other))
        {
            // Nothing to do here.
        }


        // -------- OPERATORS ---------------------------------------------- //

        /// Move assignment operator.
        inline TemporaryBuffer& operator=(TemporaryBuffer&& other)
        {
            TemporaryBufferBase::operator=(std::move(other));
            return *this;
        }

        /// Allows implicit conversion of a temporary buffer to the buffer pointer itself.
        /// Enables objects of this type to be used as if they were pointers to the underlying type.
        inline operator T*(void) const
        {
            return Data();
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Retrieves the size of the buffer space, in number of elements of type T.
        /// @return Size of the buffer, in T-sized elements.
        constexpr inline unsigned int Count(void) const
        {
            return Size() / sizeof(T);
        }

        /// Retrieves a properly-typed pointer to the buffer itself.
        /// @return Typed pointer to the buffer.
        inline T* Data(void) const
        {
            return (T*)Buffer();
        }

        /// Retrieves the size of the buffer space, in bytes.
        /// @return Size of the buffer, in bytes.
        constexpr inline unsigned int Size(void) const
        {
            return kBytesPerBuffer;
        }
    };
}
