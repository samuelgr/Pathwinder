/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FileInformationStruct.h
 *   Declaration and partial implementation of manipulation functionality for
 *   the various file information structures that Windows system calls use
 *   as output during directory enumeration operations.
 *****************************************************************************/

#pragma once

#include "ApiWindowsInternal.h"
#include "BufferPool.h"
#include "DebugAssert.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>


namespace Pathwinder
{
    /// Implements a byte-wise buffer for holding one or more file information structures without regard for their type or individual size.
    /// Directory enumeration system calls often produce multiple file information structures, which are placed contiguously in memory.
    /// This class internally allocates and maintains a pool of fixed-size buffers, which can grow as needed up to a pre-determined maximum number of buffers.
    class FileInformationStructBuffer
    {
    public:
        // -------- CONSTANTS ---------------------------------------------- //

        /// Size of each file information structure buffer, in bytes.
        /// Maximum of 64kB can be supported, based on third-party observed behavior of the various directory enumeration system calls.
        static constexpr unsigned int kBytesPerBuffer = 64 * 1024;

        /// Number of buffers to allocate initially and each time the pool is exhausted and more are needed.
        static constexpr unsigned int kBufferAllocationGranularity = 4;

        /// Maximum number of buffers to hold in the pool.
        /// If more buffers are needed beyond this number, for example due to a large number of parallel directory enumeration requests, then they will be deallocated instead of returned to the pool.
        static constexpr unsigned int kBufferPoolSize = 64;


    private:
        // -------- CLASS VARIABLES ---------------------------------------- //

        /// Manages the pool of backing buffers.
        static inline BufferPool<kBytesPerBuffer, kBufferAllocationGranularity, kBufferPoolSize> bufferPool;


        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Pointer to the buffer space.
        uint8_t* buffer;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        inline FileInformationStructBuffer(void) : buffer(bufferPool.Allocate())
        {
            // Nothing to do here.
        }

        /// Default destructor.
        inline ~FileInformationStructBuffer(void)
        {
            if (nullptr != buffer)
                bufferPool.Free(buffer);
        }

        /// Copy constructor. Should never be invoked.
        FileInformationStructBuffer(const FileInformationStructBuffer& other) = delete;

        /// Move constructor.
        inline FileInformationStructBuffer(FileInformationStructBuffer&& other) : buffer(nullptr)
        {
            *this = std::move(other);
        }


        // -------- OPERATORS ---------------------------------------------- //

        /// Move assignment operator.
        inline FileInformationStructBuffer& operator=(FileInformationStructBuffer&& other)
        {
            std::swap(buffer, other.buffer);
            return *this;
        }

        /// Array indexing operator, constant version.
        /// In debug builds this will check that the index is within bounds of the buffer capacity.
        inline const uint8_t& operator[](unsigned int index) const
        {
            DebugAssert(index < Size(), "Index is out of bounds.");
            return Data()[index];
        }

        /// Array indexing operator, mutable version.
        /// In debug builds this will check that the index is within bounds of the buffer capacity.
        inline uint8_t& operator[](unsigned int index)
        {
            DebugAssert(index < Size(), "Index is out of bounds.");
            return Data()[index];
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Retrieves a pointer to the buffer itself, constant version.
        /// @return Pointer to the buffer.
        inline const uint8_t* Data(void) const
        {
            return buffer;
        }

        /// Retrieves a pointer to the buffer itself, mutable version.
        /// @return Pointer to the buffer.
        inline uint8_t* Data(void)
        {
            return buffer;
        }

        /// Retrieves the size of the buffer, in bytes.
        /// @return Size of the buffer, in bytes.
        constexpr inline unsigned int Size(void) const
        {
            return kBytesPerBuffer;
        }
    };

    /// Describes the layout of a file information structure for a given file information class.
    /// Provides reading and writing functionality for fields that are common to all supported file information structure types.
    class FileInformationStructLayout
    {
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Type used to represent the `nextEntryOffset` field of file information structures.
        typedef ULONG TNextEntryOffset;

        /// Type used to represent the `fileNameLength` field of file information structures.
        typedef ULONG TFileNameLength;

        /// Type used to represent the `fileName[1]` field of file information structures.
        typedef WCHAR TFileNameChar;


    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Base size of the entire structure, in bytes, without considering the variable length of the trailing filename field.
        unsigned int structureBaseSizeBytes;

        /// Byte offset of the `nextEntryOffset` field of the file information structure.
        unsigned int offsetOfNextEntryOffset;

        /// Byte offset of the `fileNameLength` field of the file information structure.
        unsigned int offsetOfFileNameLength;

        /// Byte offset of the `fileName[1]` field of the file information structure.
        unsigned int offsetOfFileName;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        constexpr inline FileInformationStructLayout(void) : structureBaseSizeBytes(), offsetOfNextEntryOffset(), offsetOfFileNameLength(), offsetOfFileName()
        {
            // Nothing to do here.
        }

        /// Initialization constructor.
        /// Requires values for most fields. This class is intended for internal use and is not generally intended to be invoked externally.
        constexpr inline FileInformationStructLayout(unsigned int structureBaseSizeBytes, unsigned int offsetOfNextEntryOffset, unsigned int offsetOfFileNameLength, unsigned int offsetOfFileName) : structureBaseSizeBytes(structureBaseSizeBytes), offsetOfNextEntryOffset(offsetOfNextEntryOffset), offsetOfFileNameLength(offsetOfFileNameLength), offsetOfFileName(offsetOfFileName)
        {
            // Nothing to do here.
        }


        // -------- OPERATORS ---------------------------------------------- //

        /// Equality check.
        inline bool operator==(const FileInformationStructLayout& other) const = default;


        // -------- CLASS METHODS ------------------------------------------ //

        /// Maintains a set of layout structures for the various supported file information classes for directory enumeration and returns a layout definition for a given file information class.
        /// @param [in] fileInformationClass File information class whose structure layout is needed.
        /// @return Layout information for the specified file information class, if the file information class is supported.
        static std::optional<FileInformationStructLayout> LayoutForFileInformationClass(FILE_INFORMATION_CLASS fileInformationClass);


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Returns the base size of the file information structure whose layout is represented by this object.
        /// @return Base structure size in bytes.
        inline unsigned int BaseStructureSize(void) const
        {
            return structureBaseSizeBytes;
        }

        /// Generates and returns a pointer to the trailing filename field for the specified file information structure.
        /// Performs no verification on the input pointer or data structure.
        /// @param [in] fileInformationStruct Address of the first byte of the file information structure of interest.
        /// @return Pointer to the file information structure's trailing `fileName[1]` field.
        inline TFileNameChar* FileNamePointer(const void* fileInformationStruct) const
        {
            return reinterpret_cast<TFileNameChar*>(reinterpret_cast<size_t>(fileInformationStruct) + static_cast<size_t>(offsetOfFileName));
        }

        /// Reads the `nextEntryOffset` field from the specified file information structure.
        /// Performs no verification on the input pointer or data structure.
        /// @param [in] fileInformationStruct Address of the first byte of the file information structure of interest.
        /// @return Value of the `nextEntryOffset` field of the file information structure.
        inline TNextEntryOffset ReadNextEntryOffset(const void* fileInformationStruct) const
        {
            return *reinterpret_cast<TNextEntryOffset*>(reinterpret_cast<size_t>(fileInformationStruct) + static_cast<size_t>(offsetOfNextEntryOffset));
        }

        /// Reads the `fileNameLength` field from the specified file information structure.
        /// Performs no verification on the input pointer or data structure.
        /// @param [in] fileInformationStruct Address of the first byte of the file information structure of interest.
        /// @return Value of the `fileNameLength` field of the file information structure.
        inline TFileNameLength ReadFileNameLength(const void* fileInformationStruct) const
        {
            return *reinterpret_cast<TFileNameLength*>(reinterpret_cast<size_t>(fileInformationStruct) + static_cast<size_t>(offsetOfFileNameLength));
        }

        /// Converts the trailing `fileName` field from the specified file information structure into a string view.
        /// Performs no verification on the input pointer or data structure.
        /// @param [in] fileInformationStruct Address of the first byte of the file information structure of interest.
        /// @return String view that can be used to read the trailing `fileName` field of the file information structure.
        std::basic_string_view<TFileNameChar> ReadFileName(const void* fileInformationStruct) const
        {
            return std::wstring_view(FileNamePointer(fileInformationStruct), (ReadFileNameLength(fileInformationStruct) / sizeof(TFileNameChar)));
        }

        /// Computes the size, in bytes, of the specified file information structure including its trailing filename field.
        /// Performs no verification on the input pointer or data structure.
        /// @param [in] fileInformationStruct Address of the first byte of the file information structure of interest.
        /// @return Size, in bytes, of the specified file information structure.
        inline unsigned int SizeOfStruct(const void* fileInformationStruct) const
        {
            return std::max(structureBaseSizeBytes, offsetOfFileName + static_cast<unsigned int>(ReadFileNameLength(fileInformationStruct)));
        }

        /// Writes the `fileNameLength` field for the specified file information structure.
        /// Performs no verification on the input pointer or data structure.
        /// @param [in, out] fileInformationStruct Address of the first byte of the file information structure of interest.
        /// @param [in] newFileNameLength New value to be written to the `fileNameLength` field.
        inline void WriteFileNameLength(void* fileInformationStruct, TFileNameLength newFileNameLength) const
        {
            *(reinterpret_cast<TFileNameLength*>(reinterpret_cast<size_t>(fileInformationStruct) + static_cast<size_t>(offsetOfFileNameLength))) = newFileNameLength;
        }

        /// Writes the trailing `fileName` field for the specified file information structure.
        /// Performs no verification on the input pointer or data structure.
        /// @param [in, out] fileInformationStruct Address of the first byte of the file information structure of interest.
        /// @param [in] newFileName New value to be written to the trailing `fileName` field.
        /// @param [in] bufferCapacityBytes Total capacity of the buffer in which the file information structure itself is located, including the amount of space already occupied by the file information structure.
        inline void WriteFileName(void* fileInformationStruct, std::basic_string_view<TFileNameChar> newFileName, unsigned int bufferCapacityBytes)
        {
            const unsigned int numBytesToWrite = std::min((bufferCapacityBytes - offsetOfFileName), static_cast<unsigned int>(newFileName.length() * sizeof(TFileNameChar)));

            std::memcpy(FileNamePointer(fileInformationStruct), newFileName.data(), numBytesToWrite);
            WriteFileNameLength(fileInformationStruct, numBytesToWrite);
        }
    };
}
