/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
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
#include <initializer_list>
#include <type_traits>
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
        constexpr inline unsigned int Capacity(void) const
        {
            return CapacityBytes() / sizeof(T);
        }

        /// Retrieves a properly-typed pointer to the buffer itself.
        /// @return Typed pointer to the buffer.
        inline T* Data(void) const
        {
            return (T*)Buffer();
        }

        /// Retrieves the size of the buffer space, in bytes.
        /// @return Size of the buffer, in bytes.
        constexpr inline unsigned int CapacityBytes(void) const
        {
            return kBytesPerBuffer;
        }
    };

    /// Implements a vector-like container backed by a temporary buffer.
    /// Optimized for efficiency. Performs no boundary checks.
    template <typename T> class TemporaryVector : public TemporaryBuffer<T>
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Number of elements held by this container.
        size_t size;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        inline TemporaryVector(void) : TemporaryBuffer<T>(), size(0)
        {
            // Nothing to do here.
        }

        /// Initializer list constructor.
        inline TemporaryVector(std::initializer_list<T> initializers) : TemporaryVector()
        {
            *this = initializers;
        }

        /// Move constructor.
        inline TemporaryVector(TemporaryVector&& other) : TemporaryVector()
        {
            *this = std::move(other);
        }

        /// Default destructor.
        inline ~TemporaryVector(void)
        {
            Clear();
        }


        // -------- OPERATORS ---------------------------------------------- //

        /// Move assignment operator.
        inline TemporaryVector& operator=(TemporaryVector&& other)
        {
            TemporaryBuffer<T>::operator=(std::move(other));
            std::swap(size, other.size);
            return *this;
        }

        /// Initializer list assigment operator.
        inline TemporaryVector& operator=(std::initializer_list<T> initializers)
        {
            Clear();

            for (auto init = initializers.begin(); init != initializers.end(); ++init)
                PushBack(std::move(*init));

            return *this;
        }

        /// Equality check.
        inline bool operator==(const TemporaryVector& other) const
        {
            if (other.size != size)
                return false;

            for (size_t i = 0; i < size; ++i)
            {
                if (other[i] != (*this)[i])
                    return false;
            }

            return true;
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Removes all elements from this container, destroying each in sequence.
        inline void Clear(void)
        {
            if constexpr (true == std::is_trivially_destructible_v<T>)
            {
                size = 0;
            }
            else
            {
                while (0 != size)
                    PopBack();
            }
        }

        /// Constructs a new element using the specified arguments at the end of this container.
        /// @return Reference to the constructed and inserted element.
        template <typename... Args> inline T& EmplaceBack(Args&&... args)
        {
            new (&((*this)[size])) T(std::forward<Args>(args)...);
            return (*this)[size++];
        }

        /// Removes the last element from this container and destroys it.
        inline void PopBack(void)
        {
            (*this)[size--].~T();
        }

        /// Appends the specified element to the end of this container using copy semantics.
        /// @param [in] value Value to be appended.
        inline void PushBack(const T& value)
        {
            (*this)[size++] = value;
        }

        /// Appends the specified element to the end of this container using move semantics.
        /// @param [in] value Value to be appended.
        inline void PushBack(T&& value)
        {
            (*this)[size++] = std::move(value);
        }

        /// Retrieves the number of elements held in this container.
        /// @return Number of elements in the container.
        inline size_t Size(void) const
        {
            return size;
        }
    };
}
