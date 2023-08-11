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
#include "FileInformationStruct.h"
#include "FilesystemInstruction.h"
#include "FilesystemRule.h"
#include "TemporaryBuffer.h"

#pragma once


namespace Pathwinder
{
    /// Interface for all queue types, each of which implements a single operation that is part of a larger directory enumeration.
    /// Defines a queue-like interface that can be used to access the contained file information structures one at a time as they become available.
    class ISingleOperationQueue
    {
    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default destructor.
        virtual ~ISingleOperationQueue(void) = default;


        // -------- ABSTRACT INSTANCE METHODS ------------------------------ //

        /// Copies the first file information structure from the queue to the specified location, up to the specified number of bytes.
        /// @param [in] dest Pointer to the buffer location to receive the first file information structure.
        /// @param [in] capacityBytes Maximum number of bytes to copy to the destination buffer.
        /// @return Number of bytes copied. This will the capacity of the buffer or the size of the first file information structure in the queue, whichever is smaller in value.
        virtual unsigned int CopyFront(void* dest, unsigned int capacityBytes) = 0;

        /// Retrieves the status of the enumeration maintained by this object.
        /// @return `STATUS_NO_MORE_FILES` if the enumeration is completed and there are no file information structures left, `STATUS_MORE_ENTRIES` if the enumeration is still in progress and more directory entries are available, or any other status code to indicate that some other error occurred while interacting with the system.
        virtual NTSTATUS EnumerationStatus(void) const = 0;

        /// Retrieves the filename from the first file information structure in the queue.
        /// @return Filename from the first file information structure, or an empty string if there are no file information structures available.
        virtual std::wstring_view FileNameOfFront(void) const = 0;

        /// Determines the size, in bytes, of the first file information structure in the queue.
        /// Because file information structures contain varying-length filenames, even though the type of struct is the same the size may differ from instance to instance.
        /// @return Size, in bytes, of the first file information structure, or 0 if there are no file information structures vailable.
        virtual unsigned int SizeOfFront(void) const = 0;

        /// Removes the first file information structure from the queue.
        virtual void PopFront(void) = 0;

        /// Causes the enumeration to be restarted from the beginning.
        virtual void Restart(void) = 0;
    };

    /// Holds state and supports enumeration of a single directory within the context of a larger directory enumeration operation.
    /// Provides a queue-like interface whereby the entire enumerated contents of the single directory can be accessed one file information structure at a time.
    /// Fetches up to a single #FileInformationStructBuffer worth of file information structures from the system at any given time, and automatically fetches the next batch once the current batch has already been fully popped from the queue.
    /// Not concurrency-safe. Methods should be invoked under external concurrency control, if needed.
    class EnumerationQueue : public ISingleOperationQueue
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


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Attempts to open a handle to be used for directory enumeration.
        /// Requires an absolute path to the directory to be enumerated (including Windows namespace prefix) and a file information class to specify the type of file information sought.
        /// Optionally supports a file pattern to limit the enumeration to just those files that match.
        EnumerationQueue(std::wstring_view absoluteDirectoryPath, FILE_INFORMATION_CLASS fileInformationClass, std::wstring_view filePattern = std::wstring_view());

        /// Copy constructor. Should never be invoked.
        EnumerationQueue(const EnumerationQueue& other) = delete;

        /// Move constructor.
        EnumerationQueue(EnumerationQueue&& other);

        /// Default destructor.
        ~EnumerationQueue(void) override;


    private:
        // -------- INSTANCE METHODS --------------------------------------- //

        /// For internal use only.
        /// Queries the system for more file information structures to be placed in the queue.
        /// Sets this object's enumeration status according to the result.
        /// @param [in] queryFlags Optional query flags to supply along with the underlying system call. Defaults to 0 to specify no flags.
        /// @param [in] filePattern Optional file pattern for providing the system with a pattern to match against all returned filenames. Only important on first invocation, which occurs during construction.
        void AdvanceQueueContentsInternal(ULONG queryFlags = 0, std::wstring_view filePattern = std::wstring_view());


    public:
        // -------- CONCRETE INSTANCE METHODS ------------------------------ //
        // See above for documentation.

        unsigned int CopyFront(void* dest, unsigned int capacityBytes) override;
        NTSTATUS EnumerationStatus(void) const override;
        std::wstring_view FileNameOfFront(void) const override;
        unsigned int SizeOfFront(void) const override;
        void PopFront(void) override;
        void Restart(void) override;
    };

    /// Holds state and supports insertion of directory names into the output of a larger directory enumeration operation.
    /// Requires an externally-supplied ordered list of name insertion instructions, which are offered as file information structures one at a time using a queue-like interface.
    /// Not concurrency-safe. Methods should be invoked under external concurrency control, if needed.
    class NameInsertionQueue : public ISingleOperationQueue
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Name insertions to be performed in order from first element to last element.
        TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion> nameInsertionQueue;

        /// Position of the next element of the queue.
        unsigned int nameInsertionQueuePosition;

        /// Type of information to request from the system when querying for file information structures.
        FILE_INFORMATION_CLASS fileInformationClass;

        /// File information structure layout information. Computed based on the file information class.
        FileInformationStructLayout fileInformationStructLayout;

        /// Buffer for holding one single file enumeration result at a time.
        FileInformationStructBuffer enumerationBuffer;

        /// Overall status of the enumeration.
        NTSTATUS enumerationStatus;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Requires a pre-existing queue of name insertions that can be moved into this object and a file information class to specify the type of information to insert for each directory name.
        NameInsertionQueue(TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion>&& nameInsertionQueue, FILE_INFORMATION_CLASS fileInformationClass);

        /// Copy constructor. Should never be invoked.
        NameInsertionQueue(const NameInsertionQueue& other) = delete;

        /// Move constructor.
        NameInsertionQueue(NameInsertionQueue&& other) = default;


    private:
        // -------- INSTANCE METHODS --------------------------------------- //

        /// For internal use only.
        /// Queries the system for the next file information structure that should be inserted into the overall enumeration results.
        void AdvanceQueueContentsInternal(void);


    public:
        // -------- CONCRETE INSTANCE METHODS ------------------------------ //
        // See above for documentation.

        unsigned int CopyFront(void* dest, unsigned int capacityBytes) override;
        NTSTATUS EnumerationStatus(void) const override;
        std::wstring_view FileNameOfFront(void) const override;
        unsigned int SizeOfFront(void) const override;
        void PopFront(void) override;
        void Restart(void) override;
    };

    /// Contains all of the state necessary to represent a directory enumeration operation being executed according to a directory enumeration instruction.
    /// A directory enumeration instruction can specify that one or more real directories be enumerated and that a specific set of filenames additionally be inserted into the overall directory enumeration results.
    /// Objects of this class maintain multiple directory enumeration queues, one or more for real directory enumeration and one for filename insertion, and merges them into a single stream of file informaiton structures using a queue-like interface.
    /// It is assumed that the individual directory enumeration queues offer file information structures in case-insensitive alphabetical order by filename, and hence the merge occurs on this basis.
    /// However, it is not detrimental to the correctness of the overall directory enumeration operation if the incoming queues do not provide file information structures in sorted order, this will just mean that the single outgoing stream is also not completely sorted.
    /// Not concurrency-safe. Methods should be invoked under external concurrency control, if needed.
    class DirectoryEnumerationContext
    {
        // TODO
    };
}
