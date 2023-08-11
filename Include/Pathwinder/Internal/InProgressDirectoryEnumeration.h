/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file InProgressDirectoryEnumeration.h
 *   Declaration of objects that assist with tracking the progress of
 *   in-progress directory enumeration operations and hold all required state.
 *****************************************************************************/

#include "ApiWindowsInternal.h"
#include "BufferPool.h"
#include "DebugAssert.h"
#include "Hooks.h"
#include "Strings.h"
#include "ValueOrError.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#pragma once


namespace Pathwinder
{
    /// Implements a byte-wise buffer for holding multiple file information structures without regard for their type or individual size.
    /// Internally allocates and maintains a pool of fixed-size buffers, which can grow as needed.
    class FileInformationStructBuffer
    {
    public:
        // -------- CONSTANTS ---------------------------------------------- //

        /// Size of each file information structure buffer, in bytes.
        /// Maximum of 64kB can be supported, based on observed behavior of the various directory enumeration system calls.
        static constexpr unsigned int kBytesPerFileInformationStructBuffer = 64 * 1024;

        /// Number of buffers to allocate initially and each time the pool is exhausted and more are needed.
        static constexpr unsigned int kBufferAllocationGranularity = 4;

        /// Maximum number of buffers to hold in the pool.
        /// If more buffers are needed beyond this number, for example due to a large number of parallel directory enumeration requests, then they will be deallocated instead of returned to the pool.
        static constexpr unsigned int kBufferPoolMaxSize = 64;


    private:
        // -------- CLASS VARIABLES ---------------------------------------- //

        /// Manages the pool of backing buffers.
        static inline BufferPool<kBytesPerFileInformationStructBuffer, kBufferAllocationGranularity, kBufferPoolMaxSize> bufferPool;


        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Pointer to the buffer space.
        uint8_t* buffer;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        FileInformationStructBuffer(void);

        /// Default destructor.
        ~FileInformationStructBuffer(void);

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
            return kBytesPerFileInformationStructBuffer;
        }
    };

    /// Describes the layout of a file information structure for a given file information class.
    /// Reads relevant fields and performs additional useful computations.
    class FileInformationStructLayout
    {
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Type used to represent the `nextEntryOffset` field of file information structures.
        typedef ULONG TNextEntryOffset;

        /// Type used to represent the `fileNameLength` field of file information structures.
        typedef ULONG TFileNameLength;

        /// Type used to represent the `fileName[1]` field of file information structures.
        typedef WCHAR TFileName;


    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Base size of the entire structure, in bytes, without considering the variable length of the trailing filename field.
        unsigned int structureBaseSizeBytes;

        /// Number of characters that can fit in the trailing filename field without increasing the actual size of the structure beyond its base size.
        unsigned int characterCapacitySlack;

        /// Byte offset of the `nextEntryOffset` field of the file information structure.
        unsigned int offsetOfNextEntryOffset;

        /// Byte offset of the `fileNameLength` field of the file information structure.
        unsigned int offsetOfFileNameLength;

        /// Byte offset of the `fileName[1]` field of the file information structure.
        unsigned int offsetOfFileName;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        constexpr inline FileInformationStructLayout(void) : structureBaseSizeBytes(), characterCapacitySlack(), offsetOfNextEntryOffset(), offsetOfFileNameLength(), offsetOfFileName()
        {
            // Nothing to do here.
        }

        /// Initialization constructor.
        /// Requires values for most fields. This class is intended for internal use and is not generally intended to be invoked externally.
        constexpr inline FileInformationStructLayout(unsigned int structureBaseSizeBytes, unsigned int offsetOfNextEntryOffset, unsigned int offsetOfFileNameLength, unsigned int offsetOfFileName) : structureBaseSizeBytes(structureBaseSizeBytes), characterCapacitySlack((structureBaseSizeBytes - offsetOfFileName) / sizeof(TFileName)), offsetOfNextEntryOffset(offsetOfNextEntryOffset), offsetOfFileNameLength(offsetOfFileNameLength), offsetOfFileName(offsetOfFileName)
        {
            // Nothing to do here.
        }


        // -------- OPERATORS ---------------------------------------------- //

        /// Equality check.
        inline bool operator==(const FileInformationStructLayout& other) const = default;


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Returns the base size of the file information structure whose layout is represented by this object.
        /// @return Base structure size in bytes.
        inline unsigned int BaseStructureSize(void) const
        {
            return structureBaseSizeBytes;
        }

        /// Generates and returns a pointer to the trailing filename field for the specified file information structure.
        /// Performs no verification on the input pointer of data structure.
        /// @param [in] fileInformationStruct Address of the first byte of the file information structure of interest.
        /// @return Pointer to the file information structure's trailing `fileName[1]` field.
        inline TFileName* FileNamePointer(const void* fileInformationStruct) const
        {
            return reinterpret_cast<TFileName*>(reinterpret_cast<size_t>(fileInformationStruct) + static_cast<size_t>(offsetOfFileName));
        }

        /// Reads the `nextEntryOffset` field from the specified file information structure.
        /// Performs no verification on the input pointer of data structure.
        /// @param [in] fileInformationStruct Address of the first byte of the file information structure of interest.
        /// @return Value of the `nextEntryOffset` field of the file information structure.
        inline TNextEntryOffset ReadNextEntryOffset(const void* fileInformationStruct) const
        {
            return *reinterpret_cast<TNextEntryOffset*>(reinterpret_cast<size_t>(fileInformationStruct) + static_cast<size_t>(offsetOfNextEntryOffset));
        }

        /// Reads the `fileNameLength` field from the specified file information structure.
        /// Performs no verification on the input pointer of data structure.
        /// @param [in] fileInformationStruct Address of the first byte of the file information structure of interest.
        /// @return Value of the `fileNameLength` field of the file information structure.
        inline TFileNameLength ReadFileNameLength(const void* fileInformationStruct) const
        {
            return *reinterpret_cast<TFileNameLength*>(reinterpret_cast<size_t>(fileInformationStruct) + static_cast<size_t>(offsetOfFileNameLength));
        }

        /// Computes the size, in bytes, of the specified file information structure including its trailing filename field.
        /// Performs no verification on the input pointer of data structure.
        /// @param [in] fileInformationStruct Address of the first byte of the file information structure of interest.
        /// @return Size, in bytes, of the specified file information structure.
        inline unsigned int SizeOfStruct(const void* fileInformationStruct) const
        {
            return std::max(structureBaseSizeBytes, offsetOfFileName + static_cast<unsigned int>(ReadFileNameLength(fileInformationStruct)));
        }
    };

    /// Holds state and supports enumeration of a single directory within the context of a larger directory enumeration operation.
    /// Not concurrency-safe. Methods should be invoked under external concurrency control, if needed.
    class EnumerationQueue
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Directory handle to be used when querying the system for file information structures.
        HANDLE directoryHandle;

        /// Type of information to request from the system when querying for file information structures.
        FILE_INFORMATION_CLASS fileInformationClass;

        /// File information structure layout information. Computed based on the file information class.
        FileInformationStructLayout fileInformationStructLayout;

        /// Holds one or more file information structures received from the system.
        FileInformationStructBuffer enumerationBuffer;
        
        /// Byte position within the enumeration buffer where the next file information structure should be read.
        unsigned int enumerationBufferBytePosition;

        /// Overall status of the enumeration.
        NTSTATUS enumerationStatus;


        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Not intended for external use. Objects of this class should be created using the factory method.
        EnumerationQueue(std::wstring_view filePattern, HANDLE directoryHandle, FILE_INFORMATION_CLASS fileInformationClass);


    public:
        /// Copy constructor. Should never be invoked.
        EnumerationQueue(const EnumerationQueue& other) = delete;

        /// Move constructor.
        EnumerationQueue(EnumerationQueue&& other);

        /// Default destructor.
        ~EnumerationQueue(void);


        // -------- CLASS METHODS ------------------------------------------ //

        /// Creates an enumeration queue object for the specified file information structure type using an absolute fully-prefixed path for the directory.
        /// Attempts to open a handle to be used for directory enumeration.
        /// @tparam FileInformationStructType Windows internal structure type that is intended to be part of a buffer of contiguous structures of the same type and has a dangling filename field.
        /// @param [in] absoluteDirectoryPath Absolute path to the directory to be enumerated, including Windows namespace prefix.
        /// @param [in] fileInformationClass Type of file information requested for each file that is enumerated.
        /// @param [in] filePattern Optional file pattern for providing the system with a pattern to match against all returned filenames.
        /// @return Enumeration queue object on success, or a Windows error code on failure.
        static ValueOrError<EnumerationQueue, NTSTATUS> CreateEnumerationQueue(std::wstring_view absoluteDirectoryPath, FILE_INFORMATION_CLASS fileInformationClass, std::wstring_view filePattern = std::wstring_view());


    private:
        // -------- INSTANCE METHODS --------------------------------------- //

        /// For internal use only.
        /// Queries the system for more file information structures to be placed in the queue.
        /// Sets this object's enumeration status according to the result.
        /// @param [in] queryFlags Optional query flags to supply along with the underlying system call. Defaults to 0 to specify no flags.
        /// @param [in] filePattern Optional file pattern for providing the system with a pattern to match against all returned filenames. Only important on first invocation, which occurs during construction.
        void AdvanceQueueContentsInternal(ULONG queryFlags = 0, std::wstring_view filePattern = std::wstring_view());


    public:
        /// Copies the first file information structure from the queue to the specified location, up to the specified number of bytes.
        /// @param [in] dest Pointer to the buffer location to receive the first file information structure.
        /// @param [in] capacityBytes Maximum number of bytes to copy to the destination buffer.
        /// @return Number of bytes copied. This will the capacity of the buffer or the size of the first file information structure in the queue, whichever is smaller in value.
        unsigned int CopyFront(void* dest, unsigned int capacityBytes);

        /// Retrieves the status of the enumeration maintained by this object.
        /// @return `STATUS_NO_MORE_FILES` if the enumeration is completed and there are no file information structures left, `STATUS_MORE_ENTRIES` if the enumeration is still in progress and more directory entries are available, or any other status code to indicate that some other error occurred while interacting with the system.
        inline NTSTATUS EnumerationStatus(void) const
        {
            return enumerationStatus;
        }

        /// Retrieves the filename from the first file information structure in the queue.
        /// @return Filename from the first file information structure, or an empty string if there are no file information structures available.
        std::wstring_view FileNameOfFront(void) const;

        /// Determines the size, in bytes, of the first file information structure in the queue.
        /// Because file information structures contain varying-length filenames, even though the type of struct is the same the size may differ from instance to instance.
        /// @return Size, in bytes, of the first file information structure, or 0 if there are no file information structures vailable.
        unsigned int SizeOfFront(void) const;

        /// Removes the first file information structure from the queue.
        void PopFront(void);

        /// Causes the enumeration to be restarted from the beginning.
        void Restart(void);
    };

    /// Holds state and supports insertion of directory names into the output of a larger directory enumeration operation.
    /// Not concurrency-safe. Methods should be invoked under external concurrency control, if needed.
    class NameInsertionQueue
    {
        // TODO
    };

    /// Contains all of the state necessary to represent a directory enumeration operation being executed according to a directory enumeration instruction.
    /// Not concurrency-safe. Methods should be invoked under external concurrency control, if needed.
    class DirectoryEnumerationContext
    {
        // TODO
    };
}