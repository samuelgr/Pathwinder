/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file DirectoryOperationQueue.h
 *   Declaration of queue-like objects that produce appropriately-filtered streams of file
 *   information structures as part of directory enumeration operations.
 **************************************************************************************************/

#include <array>
#include <memory>
#include <optional>

#include "ApiWindows.h"
#include "FileInformationStruct.h"
#include "FilesystemInstruction.h"
#include "FilesystemRule.h"
#include "TemporaryBuffer.h"

#pragma once

namespace Pathwinder
{
  /// Interface for all queue types, each of which implements a single operation that is part of a
  /// larger directory enumeration. Defines a queue-like interface that can be used to access the
  /// contained file information structures one at a time as they become available.
  class IDirectoryOperationQueue
  {
  public:

    virtual ~IDirectoryOperationQueue(void) = default;

    /// Copies the first file information structure from the queue to the specified location, up
    /// to the specified number of bytes.
    /// @param [in] dest Pointer to the buffer location to receive the first file information
    /// structure.
    /// @param [in] capacityBytes Maximum number of bytes to copy to the destination buffer.
    /// @return Number of bytes copied. This will the capacity of the buffer or the size of the
    /// first file information structure in the queue, whichever is smaller in value.
    virtual unsigned int CopyFront(void* dest, unsigned int capacityBytes) const = 0;

    /// Retrieves the status of the enumeration maintained by this object.
    /// @return `STATUS_NO_MORE_FILES` if the enumeration is completed and there are no file
    /// information structures left, `STATUS_MORE_ENTRIES` if the enumeration is still in
    /// progress and more directory entries are available, or any other status code to indicate
    /// that some other error occurred while interacting with the system.
    virtual NTSTATUS EnumerationStatus(void) const = 0;

    /// Retrieves the filename from the first file information structure in the queue.
    /// @return Filename from the first file information structure, or an empty string if there
    /// are no file information structures available.
    virtual std::wstring_view FileNameOfFront(void) const = 0;

    /// Removes the first file information structure from the queue.
    virtual void PopFront(void) = 0;

    /// Causes the enumeration to be restarted from the beginning.
    /// @param [in] filePattern Optional query file pattern to use for filtering enumerated
    /// entities. Not all subclasses support query file patterns.
    virtual void Restart(std::wstring_view queryFilePattern = std::wstring_view()) = 0;

    /// Determines the size, in bytes, of the first file information structure in the queue.
    /// Because file information structures contain varying-length filenames, even though the
    /// type of struct is the same the size may differ from instance to instance.
    /// @return Size, in bytes, of the first file information structure, or 0 if there are no
    /// file information structures vailable.
    virtual unsigned int SizeOfFront(void) const = 0;
  };

  /// Holds state and supports enumeration of a single directory within the context of a larger
  /// directory enumeration operation. Provides a queue-like interface whereby the entire
  /// enumerated contents of the single directory can be accessed one file information structure
  /// at a time. Fetches up to a single #FileInformationStructBuffer worth of file information
  /// structures from the system at any given time, and automatically fetches the next batch once
  /// the current batch has already been fully popped from the queue. Not concurrency-safe.
  /// Methods should be invoked under external concurrency control, if needed.
  class EnumerationQueue : public IDirectoryOperationQueue
  {
  public:

    /// Attempts to open a handle to be used for directory enumeration.
    EnumerationQueue(
        DirectoryEnumerationInstruction::SingleDirectoryEnumeration matchInstruction,
        std::wstring_view absoluteDirectoryPath,
        FILE_INFORMATION_CLASS fileInformationClass,
        std::wstring_view filePattern = std::wstring_view());

    EnumerationQueue(const EnumerationQueue& other) = delete;

    EnumerationQueue(EnumerationQueue&& other);

    ~EnumerationQueue(void) override;

    /// Retrieves the instruction that this queue object uses to determine which files to include in
    /// the enumeration output. Primarily intended for tests.
    /// @return Single directory enumeration instruction used for determining which files to include
    /// in the enumeration output.
    inline DirectoryEnumerationInstruction::SingleDirectoryEnumeration GetMatchInstruction(
        void) const
    {
      return matchInstruction;
    }

    /// Retrieves the directory handle that was opened by this object for performing directory
    /// enumeration. Primarily intended for tests.
    /// @return Underlying directory handle used for enumeratingn directory contents.
    inline HANDLE GetDirectoryHandle(void) const
    {
      return directoryHandle;
    }

    /// Retrieves the file information class with which this object was created. Primarily intended
    /// for tests.
    /// @return File information class used to query the system during directory enumeration.
    inline FILE_INFORMATION_CLASS GetFileInformationClass(void) const
    {
      return fileInformationClass;
    }

    // IDirectoryOperationQueue
    unsigned int CopyFront(void* dest, unsigned int capacityBytes) const override;
    NTSTATUS EnumerationStatus(void) const override;
    std::wstring_view FileNameOfFront(void) const override;
    void PopFront(void) override;
    void Restart(std::wstring_view queryFilePattern = std::wstring_view()) override;
    unsigned int SizeOfFront(void) const override;

  private:

    /// Queries the system for more file information structures to be placed in the queue.
    /// Sets this object's enumeration status according to the result.
    /// @param [in] queryFlags Optional query flags to supply along with the underlying system
    /// call. Defaults to 0 to specify no flags.
    /// @param [in] filePattern Optional file pattern for providing the system with a pattern to
    /// match against all returned filenames. Only important on first invocation, which occurs
    /// during construction.
    void AdvanceQueueContentsInternal(
        ULONG queryFlags = 0, std::wstring_view filePattern = std::wstring_view());

    /// Pops a single element from the front of the queue.
    void PopFrontInternal(void);

    /// Repeatedly pops non-matching elements from the front of the queue.
    void SkipNonMatchingItemsInternal(void);

    /// Instruction that determines which files should be skipped and which files should be
    /// presented to the application. This is in addition to any matching done by the file
    /// pattern included as part of the original directory enumeration query. System call users
    /// are permitted to specify a file pattern, and match instructions would potentially
    /// include additional file patterns based on how the filesystem rules are set up.
    DirectoryEnumerationInstruction::SingleDirectoryEnumeration matchInstruction;

    /// Directory handle to be used when querying the system for file information structures.
    HANDLE directoryHandle;

    /// Type of information to request from the system when querying for file information
    /// structures.
    FILE_INFORMATION_CLASS fileInformationClass;

    /// File information structure layout information. Computed based on the file information
    /// class.
    FileInformationStructLayout fileInformationStructLayout;

    /// Holds one or more file information structures received from the system.
    FileInformationStructBuffer enumerationBuffer;

    /// Byte position within the enumeration buffer where the next file information structure
    /// should be read.
    unsigned int enumerationBufferBytePosition;

    /// Overall status of the enumeration.
    NTSTATUS enumerationStatus;
  };

  /// Holds state and supports insertion of directory names into the output of a larger directory
  /// enumeration operation. Requires an externally-supplied ordered list of name insertion
  /// instructions, which are offered as file information structures one at a time using a
  /// queue-like interface. Not concurrency-safe. Methods should be invoked under external
  /// concurrency control, if needed.
  class NameInsertionQueue : public IDirectoryOperationQueue
  {
  public:

    NameInsertionQueue(
        TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion>&&
            nameInsertionQueue,
        FILE_INFORMATION_CLASS fileInformationClass,
        std::wstring_view queryFilePattern = std::wstring_view());

    NameInsertionQueue(const NameInsertionQueue& other) = delete;

    NameInsertionQueue(NameInsertionQueue&& other) = default;

    /// Retrieves the file information class with which this object was created. Primarily intended
    /// for tests.
    /// @return File information class used to query the system during directory enumeration.
    inline FILE_INFORMATION_CLASS GetFileInformationClass(void) const
    {
      return fileInformationClass;
    }

    /// Retrieves the file match pattern, used to filter the enumeration output, with which this
    /// object was created. Primarily intended for tests.
    /// @return File match pattern string used to determine whether or not to include a particular
    /// file or directory in the enumeration output.
    inline std::wstring_view GetFilePattern(void) const
    {
      return filePattern;
    }

    /// Retrieves the name insertion instructions that this queue will use to generate directory
    /// enumeration output. Primarily intended for tests.
    /// @return Name insertion instructions.
    inline const TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion>&
        GetNameInsertionInstructions(void) const
    {
      return nameInsertionQueue;
    }

    // IDirectoryOperationQueue
    unsigned int CopyFront(void* dest, unsigned int capacityBytes) const override;
    NTSTATUS EnumerationStatus(void) const override;
    std::wstring_view FileNameOfFront(void) const override;
    void PopFront(void) override;
    void Restart(std::wstring_view queryFilePattern = std::wstring_view()) override;
    unsigned int SizeOfFront(void) const override;

  private:

    /// Queries the system for the next file information structure that should be inserted into
    /// the overall enumeration results.
    void AdvanceQueueContentsInternal(void);

    /// File pattern against which to match all filenames being enumerated.
    std::wstring filePattern;

    /// Name insertions to be performed in order from first element to last element.
    TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion>
        nameInsertionQueue;

    /// Position of the next element of the queue.
    unsigned int nameInsertionQueuePosition;

    /// Type of information to request from the system when querying for file information
    /// structures.
    FILE_INFORMATION_CLASS fileInformationClass;

    /// File information structure layout information. Computed based on the file information
    /// class.
    FileInformationStructLayout fileInformationStructLayout;

    /// Buffer for holding one single file enumeration result at a time.
    FileInformationStructBuffer enumerationBuffer;

    /// Overall status of the enumeration.
    NTSTATUS enumerationStatus;
  };

  /// Maintains multiple directory enumeration queues and merges them into a single stream of file
  /// informaiton structures using a queue-like interface. All underlying queues need to be
  /// created with the same file information class, as this class does not need that information
  /// and is totally agnostic to it. It is assumed that the individual directory enumeration
  /// queues offer file information structures in case-insensitive alphabetical order by filename,
  /// and hence the merge occurs on this basis. However, it is not detrimental to the correctness
  /// of the overall directory enumeration operation if the incoming queues do not provide file
  /// information structures in sorted order, this will just mean that the single outgoing stream
  /// is also not completely sorted. Not concurrency-safe. Methods should be invoked under
  /// external concurrency control, if needed.
  class MergedFileInformationQueue : public IDirectoryOperationQueue
  {
  public:

    /// Maximum number of queues allowed to be merged as part of a directory enumeration
    /// operation. This could theoretically be a template parameter, but avoiding templates
    /// facilitates placing the implementation into a separate source file. Furthermore, only
    /// one value is ever needed project-wide.
    static constexpr unsigned int kNumQueuesToMerge = 3;

    MergedFileInformationQueue(
        std::array<std::unique_ptr<IDirectoryOperationQueue>, kNumQueuesToMerge>&& queuesToMerge);

    /// Retrieves and returns a pointer to the underlying queue at the specified index.
    /// Intended for tests. Provides read-only access.
    /// @param [in] index Index of the underlying queue of interest.
    /// @return Read-only pointer to the corresponding underlying queue object.
    inline const IDirectoryOperationQueue* GetUnderlyingQueue(unsigned int index) const
    {
      DebugAssert(
          static_cast<size_t>(index) < queuesToMerge.size(),
          "Underlying queue index is out of bounds.");
      return queuesToMerge[index].get();
    }

    // IDirectoryOperationQueue
    unsigned int CopyFront(void* dest, unsigned int capacityBytes) const override;
    NTSTATUS EnumerationStatus(void) const override;
    std::wstring_view FileNameOfFront(void) const override;
    void PopFront(void) override;
    void Restart(std::wstring_view queryFilePattern = std::wstring_view()) override;
    unsigned int SizeOfFront(void) const override;

  private:

    /// Selects which of the queues being merged will provide the next element.
    void SelectFrontElementSourceQueueInternal(void);

    /// Queues to be merged.
    std::array<std::unique_ptr<IDirectoryOperationQueue>, kNumQueuesToMerge> queuesToMerge;

    /// Queue which will provide the next element of the merged queues.
    IDirectoryOperationQueue* frontElementSourceQueue;
  };
} // namespace Pathwinder
