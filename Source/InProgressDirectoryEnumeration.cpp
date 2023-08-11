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

#include "BufferPool.h"
#include "InProgressDirectoryEnumeration.h"
#include "Hooks.h"
#include "MutexWrapper.h"
#include "ValueOrError.h"

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

    /// Opens the specified directory for synchronous enumeration.
    /// @param [in] absoluteDirectoryPath Absolute path to the directory to be opened, including Windows namespace prefix.
    /// @return Handle for the directory file on success, Windows error code on failure.
    static ValueOrError<HANDLE, NTSTATUS> OpenDirectoryForEnumeration(std::wstring_view absoluteDirectoryPath)
    {
        HANDLE directoryHandle = nullptr;

        UNICODE_STRING absoluteDirectoryPathSystemString = Strings::NtConvertStringViewToUnicodeString(absoluteDirectoryPath);
        OBJECT_ATTRIBUTES absoluteDirectoryPathObjectAttributes{};
        InitializeObjectAttributes(&absoluteDirectoryPathObjectAttributes, &absoluteDirectoryPathSystemString, 0, nullptr, nullptr);

        IO_STATUS_BLOCK unusedStatusBlock{};

        NTSTATUS openDirectoryForEnumerationResult = Hooks::ProtectedDependency::NtOpenFile::SafeInvoke(&directoryHandle, (FILE_LIST_DIRECTORY | SYNCHRONIZE), &absoluteDirectoryPathObjectAttributes, &unusedStatusBlock, (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE), (FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT));
        if (!(NT_SUCCESS(openDirectoryForEnumerationResult)))
            return openDirectoryForEnumerationResult;

        return directoryHandle;
    }

    /// Obtains information about the specified file by asking the system to enumerate.
    /// @param [in] absoluteDirectoryPath Absolute path to the directory containing the file to be enumerated, including Windows namespace prefix.
    /// @param [in] fileName Name of the file within the directory. Must not contain any wildcards or backslashes.
    /// @param [in] fileInformationClass Type of information to obtain about the specified file.
    /// @param [in] enumerationBuffer Buffer into which to write the information received about the file.
    /// @param [in] enumerationBufferCapacityBytes Size of the destination buffer, in bytes.
    /// @return Windows error code identifying the result of the operation.
    NTSTATUS QuerySingleFileEnumerationInformation(std::wstring_view absoluteDirectoryPath, std::wstring_view fileName, FILE_INFORMATION_CLASS fileInformationClass, void* enumerationBuffer, unsigned int enumerationBufferCapacityBytes)
    {
        auto maybeDirectoryHandle = OpenDirectoryForEnumeration(absoluteDirectoryPath);
        if (true == maybeDirectoryHandle.HasError())
            return maybeDirectoryHandle.Error();

        UNICODE_STRING fileNameSystemString = Strings::NtConvertStringViewToUnicodeString(fileName);
        IO_STATUS_BLOCK unusedStatusBlock{};

        NTSTATUS directoryEnumResult = Hooks::ProtectedDependency::NtQueryDirectoryFileEx::SafeInvoke(maybeDirectoryHandle.Value(), NULL, NULL, NULL, &unusedStatusBlock, enumerationBuffer, static_cast<ULONG>(enumerationBufferCapacityBytes), fileInformationClass, 0, &fileNameSystemString);
        Hooks::ProtectedDependency::NtClose::SafeInvoke(maybeDirectoryHandle.Value());

        return directoryEnumResult;
    }


    // -------- CONSTRUCTION AND DESTRUCTION ------------------------------- //
    // See "InProgressDirectoryEnumeration.h" for documentation.

    FileInformationStructBuffer::FileInformationStructBuffer(void) : buffer(bufferPool.Allocate())
    {
        // Nothing to do here.
    }

    // --------

    FileInformationStructBuffer::~FileInformationStructBuffer(void)
    {
        if (nullptr != buffer)
            bufferPool.Free(buffer);
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
        auto maybeDirectoryHandle = OpenDirectoryForEnumeration(absoluteDirectoryPath);
        if (true == maybeDirectoryHandle.HasError())
            return maybeDirectoryHandle.Error();

        EnumerationQueue enumerationQueue(filePattern, maybeDirectoryHandle.Value(), fileInformationClass);
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

    unsigned int EnumerationQueue::CopyFront(void* dest, unsigned int capacityBytes)
    {
        const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

        unsigned int numBytesToCopy = std::min(SizeOfFront(), capacityBytes);
        std::memcpy(dest, enumerationEntry, static_cast<size_t>(numBytesToCopy));

        return numBytesToCopy;
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
}
