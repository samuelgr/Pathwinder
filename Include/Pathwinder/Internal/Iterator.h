/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file Iterator.h
 *   Implementation of various types of iterators for internal containers.
 *****************************************************************************/

#pragma once

#include <iterator>


namespace Pathwinder
{
    /// Iterator type used to denote a position within a contiguous array of objects. Support random accesses.
    template <typename T> class ContiguousRandomAccessIterator
    {
    public:
        // -------- TYPE DEFINITIONS ----------------------------------- //

        // Type aliases for compliance with STL random-access iterator specifications.
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = int;
        using pointer = T*;
        using reference = T&;


    private:
        // -------- INSTANCE VARIABLES --------------------------------- //

        /// Pointer directly to the underlying data buffer.
        T* buffer;

        /// Index within the data buffer.
        int index;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION ----------------------- //

        constexpr ContiguousRandomAccessIterator(void) : buffer(), index()
        {
            // Nothing to do here.
        }

        /// Initialization constructor.
        /// Requires a buffer and an index to initialize this iterator.
        constexpr ContiguousRandomAccessIterator(T* buffer, int index) : buffer(buffer), index(index)
        {
            // Nothing to do here.
        }


        // -------- OPERATORS ------------------------------------------ //

        /// Subscripting operator.
        /// Allows arbitrary forwards and backwards movement via the iterator.
        constexpr inline T& operator[](int index) const
        {
            return *(*this + index);
        }

        /// Dereferencing operator.
        /// Allows the underlying data to be accessed directly via the iterator.
        constexpr inline T& operator*(void) const
        {
            return buffer[index];
        }

        /// Member access operator.
        /// Allows the underlying data to be accessed directly via the iterator.
        constexpr inline T* operator->(void) const
        {
            return &buffer[index];
        }

        /// Pre-increment operator.
        constexpr inline ContiguousRandomAccessIterator& operator++(void)
        {
            index += 1;
            return *this;
        }

        /// Post-increment operator.
        constexpr inline ContiguousRandomAccessIterator operator++(int)
        {
            ContiguousRandomAccessIterator orig = *this;
            index += 1;
            return orig;
        }

        /// Pre-decrement operator.
        constexpr inline ContiguousRandomAccessIterator& operator--(void)
        {
            index -= 1;
            return *this;
        }

        /// Post-decrement operator.
        constexpr inline ContiguousRandomAccessIterator operator--(int)
        {
            ContiguousRandomAccessIterator orig = *this;
            index -= 1;
            return orig;
        }

        /// Addition-assignment operator.
        /// Allows arbitrary addition to the index but no changes to the buffer pointer.
        constexpr inline ContiguousRandomAccessIterator& operator+=(int indexIncrement)
        {
            index += indexIncrement;
            return *this;
        }

        /// Addition operator.
        /// Allows arbitrary addition to the index but no changes to the buffer pointer.
        constexpr inline ContiguousRandomAccessIterator operator+(int indexIncrement) const
        {
            return ContiguousRandomAccessIterator(buffer, index + indexIncrement);
        }

        /// Subtraction-assignment operator.
        /// Allows arbitrary subtraction from the index but no changes to the buffer pointer.
        constexpr inline ContiguousRandomAccessIterator& operator-=(int indexIncrement)
        {
            index -= indexIncrement;
            return *this;
        }

        /// Subtraction operator.
        /// Allows arbitrary subtraction from the index but no changes to the buffer pointer.
        constexpr inline ContiguousRandomAccessIterator operator-(int indexIncrement) const
        {
            return ContiguousRandomAccessIterator(buffer, index - indexIncrement);
        }

        /// Subraction operator for iterators.
        /// Computes the distance between two iterators.
        constexpr inline int operator-(const ContiguousRandomAccessIterator& rhs) const
        {
            DebugAssert(buffer == rhs.buffer, "Iterators point to different instances.");
            return index - rhs.index;
        }

        /// Equality comparison operator.
        /// In debug builds this will check that the two iterators reference the same object.
        constexpr inline bool operator==(const ContiguousRandomAccessIterator& other) const
        {
            DebugAssert(buffer == other.buffer, "Iterators point to different instances.");
            return (index == other.index);
        }

        /// Inequality comparison operator.
        /// In debug builds this will check that the two iterators reference the same object.
        constexpr inline bool operator!=(const ContiguousRandomAccessIterator& other) const
        {
            DebugAssert(buffer == other.buffer, "Iterators point to different instances.");
            return (index != other.index);
        }

        /// Less-than comparison operator.
        /// In debug builds this will check that the two iterators reference the same object.
        constexpr inline bool operator<(const ContiguousRandomAccessIterator& rhs) const
        {
            DebugAssert(buffer == rhs.buffer, "Iterators point to different instances.");
            return (index < rhs.index);
        }

        /// Less-or-equal comparison operator.
        /// In debug builds this will check that the two iterators reference the same object.
        constexpr inline bool operator<=(const ContiguousRandomAccessIterator& rhs) const
        {
            DebugAssert(buffer == rhs.buffer, "Iterators point to different instances.");
            return (index <= rhs.index);
        }

        /// Greater-than comparison operator.
        /// In debug builds this will check that the two iterators reference the same object.
        constexpr inline bool operator>(const ContiguousRandomAccessIterator& rhs) const
        {
            DebugAssert(buffer == rhs.buffer, "Iterators point to different instances.");
            return (index > rhs.index);
        }

        /// Greater-or-equal comparison operator.
        /// In debug builds this will check that the two iterators reference the same object.
        constexpr inline bool operator>=(const ContiguousRandomAccessIterator& rhs) const
        {
            DebugAssert(buffer == rhs.buffer, "Iterators point to different instances.");
            return (index >= rhs.index);
        }
    };

    /// Type alias for a constant version of #ContiguousRandomAccessIterator.
    template <typename T> using ContiguousRandomAccessConstIterator = ContiguousRandomAccessIterator<const T>;
}
