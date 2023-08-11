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

#include "ApiWindowsInternal.h"
#include "BufferPool.h"
#include "FileInformationStruct.h"
#include "FilesystemOperations.h"
#include "InProgressDirectoryEnumeration.h"
#include "MutexWrapper.h"
#include "Strings.h"
#include "ValueOrError.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_map>


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


    // -------- CONSTRUCTION AND DESTRUCTION ------------------------------- //
    // See "InProgressDirectoryEnumeration.h" for documentation.

    EnumerationQueue::EnumerationQueue(std::wstring_view absoluteDirectoryPath, FILE_INFORMATION_CLASS fileInformationClass, std::wstring_view filePattern) : directoryHandle(NULL), fileInformationClass(fileInformationClass), fileInformationStructLayout(FileInformationStructLayout::LayoutForFileInformationClass(fileInformationClass).value_or(FileInformationStructLayout())), enumerationBuffer(), enumerationBufferBytePosition(), enumerationStatus()
    {
        if (FileInformationStructLayout() == fileInformationStructLayout)
        {
            enumerationStatus = NtStatus::kInvalidInfoClass;
            return;
        }
        
        auto maybeDirectoryHandle = FilesystemOperations::OpenDirectoryForEnumeration(absoluteDirectoryPath);
        if (true == maybeDirectoryHandle.HasError())
        {
            enumerationStatus = maybeDirectoryHandle.Error();
            return;
        }

        directoryHandle = maybeDirectoryHandle.Value();
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
            FilesystemOperations::CloseHandle(directoryHandle);
    }

    // --------

    NameInsertionQueue::NameInsertionQueue(TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion>&& nameInsertionQueue, FILE_INFORMATION_CLASS fileInformationClass) : nameInsertionQueue(std::move(nameInsertionQueue)), nameInsertionQueuePosition(0), fileInformationClass(fileInformationClass), fileInformationStructLayout(FileInformationStructLayout::LayoutForFileInformationClass(fileInformationClass).value_or(FileInformationStructLayout())), enumerationBuffer(), enumerationStatus()
    {
        if (FileInformationStructLayout() == fileInformationStructLayout)
        {
            enumerationStatus = NtStatus::kInvalidInfoClass;
            return;
        }

        Restart();
    }


    // -------- INSTANCE METHODS ------------------------------------------- //
    // See "InProgressDirectoryEnumeration.h" for documentation.

    void EnumerationQueue::AdvanceQueueContentsInternal(ULONG queryFlags, std::wstring_view filePattern)
    {
        NTSTATUS directoryEnumerationResult = FilesystemOperations::PartialEnumerateDirectoryContents(directoryHandle, fileInformationClass, enumerationBuffer.Data(), enumerationBuffer.Size(), queryFlags, filePattern);
        if (!(NT_SUCCESS(directoryEnumerationResult)))
        {
            // This failure block includes `STATUS_NO_MORE_FILES` in which case enumeration is complete.
            enumerationBufferBytePosition = kInvalidEnumerationBufferBytePosition;
            enumerationStatus = directoryEnumerationResult;
        }
        else
        {
            // File information structures are available.
            enumerationBufferBytePosition = 0;
            enumerationStatus = NtStatus::kMoreEntries;
        }
    }

    // --------

    void NameInsertionQueue::AdvanceQueueContentsInternal(void)
    {
        const DirectoryEnumerationInstruction::SingleDirectoryNameInsertion& nameInsertion = nameInsertionQueue[nameInsertionQueuePosition];

        NTSTATUS nameInsertionQueryResult = FilesystemOperations::QuerySingleFileDirectoryInformation(nameInsertion.DirectoryInformationSourceDirectoryPart(), nameInsertion.DirectoryInformationSourceFilePart(), fileInformationClass, enumerationBuffer.Data(), enumerationBuffer.Size());
        if (!(NT_SUCCESS(nameInsertionQueryResult)))
            enumerationStatus = nameInsertionQueryResult;

        fileInformationStructLayout.WriteFileName(enumerationBuffer.Data(), nameInsertion.FileNameToInsert(), enumerationBuffer.Size());
        nameInsertionQueuePosition += 1;

        if (nameInsertionQueuePosition == nameInsertionQueue.Size())
            enumerationStatus = NtStatus::kNoMoreFiles;
        else
            enumerationStatus = NtStatus::kMoreEntries;
    }


    // -------- CONCRETE INSTANCE METHODS ---------------------------------- //
    // See "InProgressDirectoryEnumeration.h" for documentation.

    unsigned int EnumerationQueue::CopyFront(void* dest, unsigned int capacityBytes)
    {
        const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

        const unsigned int numBytesToCopy = std::min(SizeOfFront(), capacityBytes);
        std::memcpy(dest, enumerationEntry, static_cast<size_t>(numBytesToCopy));

        return numBytesToCopy;
    }

    // --------

    NTSTATUS EnumerationQueue::EnumerationStatus(void) const
    {
        return enumerationStatus;
    }

    // --------

    std::wstring_view EnumerationQueue::FileNameOfFront(void) const
    {
        const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

        return fileInformationStructLayout.ReadFileName(enumerationEntry);
    }

    // --------

    unsigned int EnumerationQueue::SizeOfFront(void) const
    {
        const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

        return fileInformationStructLayout.SizeOfStruct(enumerationEntry);
    }

    // --------

    void EnumerationQueue::PopFront(void)
    {
        const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

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

    // --------

    unsigned int NameInsertionQueue::CopyFront(void* dest, unsigned int capacityBytes)
    {
        const unsigned int numBytesToCopy = std::min(SizeOfFront(), capacityBytes);
        std::memcpy(dest, enumerationBuffer.Data(), static_cast<size_t>(numBytesToCopy));

        return numBytesToCopy;
    }

    // --------

    NTSTATUS NameInsertionQueue::EnumerationStatus(void) const
    {
        return enumerationStatus;
    }

    // --------

    std::wstring_view NameInsertionQueue::FileNameOfFront(void) const
    {
        return fileInformationStructLayout.ReadFileName(enumerationBuffer.Data());
    }

    // --------

    unsigned int NameInsertionQueue::SizeOfFront(void) const
    {
        return fileInformationStructLayout.SizeOfStruct(enumerationBuffer.Data());
    }

    // --------

    void NameInsertionQueue::PopFront(void)
    {
        AdvanceQueueContentsInternal();
    }

    // --------

    void NameInsertionQueue::Restart(void)
    {
        if (0 == nameInsertionQueue.Size())
        {
            enumerationStatus = NtStatus::kNoMoreFiles;
            return;
        }

        nameInsertionQueuePosition = 0;
        AdvanceQueueContentsInternal();
    }
}
