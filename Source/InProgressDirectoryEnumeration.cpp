/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file InProgressDirectoryEnumeration.cpp
 *   Implementation of functionality for tracking the progress of in-progress
 *   directory enumeration operations and maintaining all required state.
 *****************************************************************************/

#include "InProgressDirectoryEnumeration.h"
#include "Hooks.h"
#include "MutexWrapper.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_map>


// -------- MACROS --------------------------------------------------------- //

/// Produces a key-value pair definition for a file information class and the associatied structure's layout information.
#define FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(structname) \
    {structname::kFileInformationClass, FileInformationStructLayout(sizeof(structname), offsetof(structname, nextEntryOffset), offsetof(structname, fileNameLength), offsetof(structname, fileName))}


namespace Pathwinder
{
    // -------- INTERNAL CONSTANTS ----------------------------------------- //

    /// Value used for enumeration queue byte positions to represent that there is nothing left in the queue.
    static constexpr unsigned int kInvalidEnumerationBufferBytePosition = static_cast<unsigned int>(-1);


    // -------- INTERNAL TYPES --------------------------------------------- //

    /// Manages allocation and deallocation of the backing buffers used for holding file information structures.
    /// Implemented as a singleton object.
    class FileInformationStructBufferManager
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Holds available buffers that are already allocated and can be distributed as needed.
        std::array<uint8_t*, FileInformationStructBuffer::kBufferPoolMaxSize> availableBuffers;

        /// Number of available buffers.
        unsigned int numAvailableBuffers;

        /// Mutex used to ensure concurrency control over temporary buffer allocation and deallocation.
        Mutex allocationMutex;


        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor. Objects cannot be constructed externally.
        inline FileInformationStructBufferManager(void) : availableBuffers(), numAvailableBuffers(0), allocationMutex()
        {
            AllocateMoreBuffers();
        }

        /// Copy constructor. Should never be invoked.
        FileInformationStructBufferManager(const FileInformationStructBufferManager& other) = delete;


    public:
        // -------- CLASS METHODS ------------------------------------------ //

        /// Returns a reference to the singleton instance of this class.
        /// @return Reference to the singleton instance.
        static FileInformationStructBufferManager& Singleton(void)
        {
            static FileInformationStructBufferManager fileInformationStructBufferManager;
            return fileInformationStructBufferManager;
        }


    private:
        // -------- INSTANCE METHODS --------------------------------------- //

        /// Allocates more buffers and places them into the available buffers data structure.
        /// Not concurrency-safe. Intended to be invoked as part of an otherwise-guarded operation.
        void AllocateMoreBuffers(void)
        {
            for (unsigned int i = 0; i < FileInformationStructBuffer::kBufferAllocationGranularity; ++i)
            {
                if (numAvailableBuffers == FileInformationStructBuffer::kBufferPoolMaxSize)
                    break;

                availableBuffers[numAvailableBuffers] = new uint8_t[FileInformationStructBuffer::kBytesPerFileInformationStructBuffer];
                numAvailableBuffers += 1;
            }
        }

    public:
        /// Allocates a buffer for the caller to use.
        /// @return Buffer that the caller can use.
        uint8_t* Allocate(void)
        {
            std::scoped_lock lock(allocationMutex);

            if (0 == numAvailableBuffers)
                AllocateMoreBuffers();

            numAvailableBuffers -= 1;
            uint8_t* nextFreeBuffer = availableBuffers[numAvailableBuffers];

            return nextFreeBuffer;
        }

        /// Deallocates a buffer once the caller is finished with it.
        /// @param [in] Previously-allocated buffer that the caller is returning.
        void Free(uint8_t* buffer)
        {
            std::scoped_lock lock(allocationMutex);

            if (numAvailableBuffers == FileInformationStructBuffer::kBufferPoolMaxSize)
            {
                delete[] buffer;
            }
            else
            {
                availableBuffers[numAvailableBuffers] = buffer;
                numAvailableBuffers += 1;
            }
        }
    };


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Maintains a set of layout structures for the various supported file information classes for directory enumeration and returns a layout definition for a given file information class.
    /// @param [in] fileInformationClass File information class whose structure layout is needed.
    /// @return Layout information for the specified file information class, or a default-constructed layout structure if the file information class is unsupported and therefore unknown.
    static FileInformationStructLayout LayoutForFileInformationClass(FILE_INFORMATION_CLASS fileInformationClass)
    {
        static const std::unordered_map<FILE_INFORMATION_CLASS, FileInformationStructLayout> kFileInformationStructureLayouts = {
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileDirectoryInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileFullDirectoryInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileBothDirectoryInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileNamesInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileIdBothDirInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileIdFullDirInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileIdGlobalTxDirInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileIdExtdDirInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileIdExtdBothDirInformation)
        };

        auto layoutIter = kFileInformationStructureLayouts.find(fileInformationClass);
        DebugAssert(layoutIter != kFileInformationStructureLayouts.cend(), "Unsupported file information class.");
        if (layoutIter == kFileInformationStructureLayouts.cend())
            return FileInformationStructLayout();

        return layoutIter->second;
    }


    // -------- CONSTRUCTION AND DESTRUCTION ------------------------------- //
    // See "InProgressDirectoryEnumeration.h" for documentation.

    FileInformationStructBuffer::FileInformationStructBuffer(void) : buffer(FileInformationStructBufferManager::Singleton().Allocate())
    {
        // Nothing to do here.
    }

    // --------

    FileInformationStructBuffer::~FileInformationStructBuffer(void)
    {
        if (nullptr != buffer)
            FileInformationStructBufferManager::Singleton().Free(buffer);
    }

    // --------

    EnumerationQueue::EnumerationQueue(HANDLE directoryHandle, FILE_INFORMATION_CLASS fileInformationClass) : directoryHandle(directoryHandle), fileInformationClass(fileInformationClass), fileInformationStructLayout(LayoutForFileInformationClass(fileInformationClass)), enumerationBuffer(), enumerationBufferBytePosition(), enumerationStatus()
    {
        if (FileInformationStructLayout() == fileInformationStructLayout)
            enumerationStatus = NtStatus::kInvalidInfoClass;
        else
            enumerationStatus = AdvanceQueueContentsInternal();
    }

    // --------

    EnumerationQueue::~EnumerationQueue(void)
    {
        // TODO
    }


    // -------- INSTANCE METHODS ------------------------------------------- //
    // See "InProgressDirectoryEnumeration.h" for documentation.

    NTSTATUS EnumerationQueue::AdvanceQueueContentsInternal(void)
    {
        // TODO
        return NtStatus::kSuccess;
    }
}
