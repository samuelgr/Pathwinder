/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file ArrayList.h
 *   Implementation of a list-like container backed by a fixed-size array.
 *   Avoids dynamic allocation and reallocation, and intended to hold a small
 *   number of small objects.
 *****************************************************************************/

#pragma once

#include "DebugAssert.h"
#include "Iterator.h"

#include <cstdint>
#include <initializer_list>


namespace Pathwinder
{
    /// Implements a list-type container backed by a fixed-size array object.
    /// Optimized for efficiency. Performs no boundary checks.
    /// @tparam kCapacity Desired capacity, in number of elements.
    template <typename T, const unsigned int kCapacity> class ArrayList
    {
    public:
        // -------- CONSTANTS ---------------------------------------------- //

        /// Capacity of the entire array list object, in bytes.
        static constexpr unsigned int kCapacityBytes = (sizeof(T) * kCapacity);


        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Iterator type for providing mutable access to the contents of the backing array.
        typedef ContiguousRandomAccessIterator<T> TIterator;

        /// Iterator type for providing read-only access to the contents of the backing array.
        typedef ContiguousRandomAccessConstIterator<T> TConstIterator;


    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Backing array. Holds all values. Implemented in a byte-wise manner to avoid default-constructing objects in empty positions in the array.
        uint8_t backingArray[kCapacityBytes];

        /// Number of elements held by this container.
        unsigned int size;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        constexpr ArrayList(void) : backingArray(), size(0)
        {
            // Nothing to do here.
        }

        /// Initializer list constructor.
        inline ArrayList(std::initializer_list<T> initializers) : ArrayList()
        {
            *this = initializers;
        }

        /// Copy constructor.
        inline ArrayList(const ArrayList& other) : ArrayList()
        {
            *this = other;
        }

        /// Move constructor.
        inline ArrayList(ArrayList&& other) : ArrayList()
        {
            *this = std::move(other);
        }


        // -------- OPERATORS ---------------------------------------------- //

        /// Copy assignment operator.
        inline ArrayList& operator=(const ArrayList& other)
        {
            Clear();

            for (const auto& element : other)
                PushBack(element);

            return *this;
        }

        /// Move assignment operator.
        inline ArrayList& operator=(ArrayList&& other)
        {
            Clear();

            for (auto& element : other)
                PushBack(std::move(element));

            return *this;
        }

        /// Initializer list assignment operator.
        inline ArrayList& operator=(std::initializer_list<T> initializers)
        {
            Clear();

            for (auto& element : initializers)
                PushBack(element);

            return *this;
        }

        /// Equality check.
        inline bool operator==(const ArrayList& other) const
        {
            if (other.size != size)
                return false;

            for (int i = 0; i < size; ++i)
            {
                if (other.backingArray[i] != backingArray[i])
                    return false;
            }

            return true;
        }

        /// Array indexing operator, constant version.
        /// In debug builds this will check that the index is within bounds of the container size.
        constexpr const T& operator[](unsigned int index) const
        {
            DebugAssert(index < Size(), "Index is out of bounds.");
            return Data()[index];
        }

        /// Array indexing operator, mutable version.
        /// In debug builds this will check that the index is within bounds of the buffer capacity.
        constexpr T& operator[](unsigned int index)
        {
            DebugAssert(index < Size(), "Index is out of bounds.");
            return Data()[index];
        }


        // -------- ITERATORS ---------------------------------------------- //

        /// Explicit constant-typed beginning iterator.
        inline TConstIterator cbegin(void) const
        {
            return TConstIterator(this->Data(), 0);
        }

        /// Explicit constant-typed one-past-the-end iterator.
        inline TConstIterator cend(void) const
        {
            return TConstIterator(this->Data(), size);
        }

        /// Implicit constant-typed beginning iterator.
        inline TConstIterator begin(void) const
        {
            return cbegin();
        }

        /// Implicit constant-typed one-past-the-end iterator.
        inline TConstIterator end(void) const
        {
            return cend();
        }

        /// Implicit mutable-typed beginning iterator.
        inline TIterator begin(void)
        {
            return TIterator(this->Data(), 0);
        }

        /// Implicit mutable-typed one-past-the-end iterator.
        inline TIterator end(void)
        {
            return TIterator(this->Data(), size);
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Retrieves the size of the buffer space, in number of elements of type T.
        /// @return Size of the buffer, in T-sized elements.
        constexpr unsigned int Capacity(void) const
        {
            return kCapacity;
        }

        /// Retrieves the size of the buffer space, in bytes.
        /// @return Size of the buffer, in bytes.
        constexpr unsigned int CapacityBytes(void) const
        {
            return kCapacityBytes;
        }

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

        /// Retrieves a properly-typed pointer to the buffer itself, constant version.
        /// @return Typed pointer to the buffer.
        constexpr const T* Data(void) const
        {
            return reinterpret_cast<const T*>(backingArray);
        }

        /// Retrieves a properly-typed pointer to the buffer itself, mutable version.
        /// @return Typed pointer to the buffer.
        constexpr T* Data(void)
        {
            return reinterpret_cast<T*>(backingArray);
        }

        /// Constructs a new element using the specified arguments at the end of this container.
        /// @return Reference to the constructed and inserted element.
        template <typename... Args> inline T& EmplaceBack(Args&&... args)
        {
            DebugAssert(Size() < Capacity(), "Emplacing into an already-full ArrayList object.");

            new (&Data()[size]) T(std::forward<Args>(args)...);
            return Data()[size++];
        }

        /// Specifies if this container contains no elements.
        /// @return `true` if this is container is empty, `false` otherwise.
        inline bool Empty(void) const
        {
            return (0 == Size());
        }

        /// Removes the last element from this container and destroys it.
        inline void PopBack(void)
        {
            DebugAssert(false == Empty(), "Popping from an empty ArrayList object.");

            Data()[size--].~T();
        }

        /// Appends the specified element to the end of this container using copy semantics.
        /// @param [in] value Value to be appended.
        inline void PushBack(const T& value)
        {
            DebugAssert(Size() < Capacity(), "Pushing into an already-full ArrayList object.");

            new (&Data()[size++]) T(value);
        }

        /// Appends the specified element to the end of this container using move semantics.
        /// @param [in] value Value to be appended.
        inline void PushBack(T&& value)
        {
            DebugAssert(Size() < Capacity(), "Pushing into an already-full ArrayList object.");

            new (&Data()[size++]) T(std::move(value));
        }

        /// Retrieves the number of elements held in this container.
        /// @return Number of elements in the container.
        inline unsigned int Size(void) const
        {
            return size;
        }
    };
}
