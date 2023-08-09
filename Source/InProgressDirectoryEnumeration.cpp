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
#include <cstring>
#include <mutex>
#include <optional>
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

    // Query flags for use with the `NtQueryDirectoryFileEx` function.
    // These constants not defined in header files outside of the Windows driver kit.
    // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfileex for more information.
    namespace QueryFlags
    {
        static constexpr ULONG kRestartScan = 0x00000001;                   ///< `SL_RESTART_SCAN`: The scan will start at the first entry in the directory. If this flag is not set, the scan will resume from where the last query ended.
        static constexpr ULONG kReturnSingleEntry = 0x00000002;             ///< `SL_RETURN_SINGLE_ENTRY`: Normally the return buffer is packed with as many matching directory entries that fit. If this flag is set, the file system will return only one directory entry at a time. This does make the operation less efficient.
    }


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
    /// @return Layout information for the specified file information class, if the file information class is supported.
    static std::optional<FileInformationStructLayout> LayoutForFileInformationClass(FILE_INFORMATION_CLASS fileInformationClass)
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
            return std::nullopt;

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

    EnumerationQueue::EnumerationQueue(std::wstring_view filePattern, HANDLE directoryHandle, FILE_INFORMATION_CLASS fileInformationClass) : directoryHandle(directoryHandle), fileInformationClass(fileInformationClass), fileInformationStructLayout(LayoutForFileInformationClass(fileInformationClass).value_or(FileInformationStructLayout())), enumerationBuffer(), enumerationBufferBytePosition(), enumerationStatus()
    {
        if (FileInformationStructLayout() == fileInformationStructLayout)
            enumerationStatus = NtStatus::kInvalidInfoClass;
        else
            AdvanceQueueContentsInternal(0, filePattern);
    }

    // --------

    EnumerationQueue::EnumerationQueue(EnumerationQueue&& other) : directoryHandle(std::move(other.directoryHandle)), fileInformationClass(std::move(other.fileInformationClass)), fileInformationStructLayout(std::move(other.fileInformationStructLayout)), enumerationBuffer(std::move(other.enumerationBuffer)), enumerationBufferBytePosition(std::move(other.enumerationBufferBytePosition)), enumerationStatus(std::move(other.enumerationStatus))
    {
        other.directoryHandle = NULL;
    }

    // --------

    EnumerationQueue::~EnumerationQueue(void)
    {
        if (NULL != directoryHandle)
            Hooks::ProtectedDependency::NtClose::SafeInvoke(directoryHandle);
    }


    // -------- CLASS METHODS ---------------------------------------------- //
    // See "InProgressDirectoryEnumeration.h" for documentation.

    ValueOrError<EnumerationQueue, NTSTATUS> EnumerationQueue::CreateEnumerationQueue(std::wstring_view absoluteDirectoryPath, FILE_INFORMATION_CLASS fileInformationClass, std::wstring_view filePattern)
    {
        HANDLE directoryHandle = nullptr;

        UNICODE_STRING absoluteDirectoryPathSystemString = Strings::NtConvertStringViewToUnicodeString(absoluteDirectoryPath);
        OBJECT_ATTRIBUTES absoluteDirectoryPathObjectAttributes{};
        InitializeObjectAttributes(&absoluteDirectoryPathObjectAttributes, &absoluteDirectoryPathSystemString, 0, nullptr, nullptr);

        IO_STATUS_BLOCK unusedStatusBlock{};

        NTSTATUS openDirectoryForEnumerationResult = Hooks::ProtectedDependency::NtOpenFile::SafeInvoke(&directoryHandle, (FILE_LIST_DIRECTORY | SYNCHRONIZE), &absoluteDirectoryPathObjectAttributes, &unusedStatusBlock, (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE), (FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT));
        if (!(NT_SUCCESS(openDirectoryForEnumerationResult)))
            return openDirectoryForEnumerationResult;

        EnumerationQueue enumerationQueue(filePattern, directoryHandle, fileInformationClass);
        if (!(NT_SUCCESS(enumerationQueue.EnumerationStatus())))
            return enumerationQueue.EnumerationStatus();

        return enumerationQueue;
    }


    // -------- INSTANCE METHODS ------------------------------------------- //
    // See "InProgressDirectoryEnumeration.h" for documentation.

    void EnumerationQueue::AdvanceQueueContentsInternal(ULONG queryFlags, std::wstring_view filePattern)
    {
        UNICODE_STRING filePatternSystemString{};
        UNICODE_STRING* filePatternSystemStringPtr = nullptr;

        if (false == filePattern.empty())
        {
            filePatternSystemString = Strings::NtConvertStringViewToUnicodeString(filePattern);
            filePatternSystemStringPtr = &filePatternSystemString;

            // The system captures the file pattern on first invocation.
            // It is not necessary to keep it on subsequent invocations, and there is no guarantee the backing string will be available.
            filePattern = std::wstring_view();
        }

        IO_STATUS_BLOCK statusBlock{};

        NTSTATUS directoryEnumerationResult = Hooks::ProtectedDependency::NtQueryDirectoryFileEx::SafeInvoke(directoryHandle, NULL, NULL, NULL, &statusBlock, enumerationBuffer.Data(), enumerationBuffer.Size(), fileInformationClass, queryFlags, filePatternSystemStringPtr);
        if (!(NT_SUCCESS(directoryEnumerationResult)))
        {
            // This failure block includes `STATUS_NO_MORE_FILES` in which case enumeration is complete.
            enumerationBufferBytePosition = kInvalidEnumerationBufferBytePosition;
            enumerationStatus = directoryEnumerationResult;
        }
        else if (statusBlock.Information < fileInformationStructLayout.BaseStructureSize())
        {
            // Not even one complete basic structure was written to the buffer.
            // This is an unexpected condition and should not occur.
            enumerationBufferBytePosition = kInvalidEnumerationBufferBytePosition;
            enumerationStatus = NtStatus::kInternalError;
        }
        else
        {
            // File information structures are available.
            enumerationBufferBytePosition = 0;
            enumerationStatus = NtStatus::kMoreEntries;
        }
    }

    // --------

    std::wstring_view EnumerationQueue::FileNameOfFront(void) const
    {
        const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

        return std::wstring_view(fileInformationStructLayout.FileNamePointer(enumerationEntry), (fileInformationStructLayout.ReadFileNameLength(enumerationEntry) / sizeof(wchar_t)));
    }

    // --------

    unsigned int EnumerationQueue::SizeOfFront(void) const
    {
        const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

        return fileInformationStructLayout.SizeOfStruct(enumerationEntry);
    }

    // --------

    void EnumerationQueue::PopFrontTo(void* dest)
    {
        const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

        if (nullptr != dest)
            std::memcpy(dest, enumerationEntry, static_cast<size_t>(SizeOfFront()));

        FileInformationStructLayout::TNextEntryOffset bytePositionIncrement = fileInformationStructLayout.ReadNextEntryOffset(enumerationEntry);
        if (0 == bytePositionIncrement)
            AdvanceQueueContentsInternal();
        else
            enumerationBufferBytePosition += bytePositionIncrement;
    }

    // --------

    void EnumerationQueue::Restart(void)
    {
        AdvanceQueueContentsInternal(QueryFlags::kRestartScan);
    }
}
