/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file DirectoryOperationQueue.cpp
 *   Implementation of queue-like objects that produce appropriately-filtered streams of file
 *   information structures as part of directory enumeration operations.
 **************************************************************************************************/

#include "DirectoryOperationQueue.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_map>

#include "ApiWindows.h"
#include "ArrayList.h"
#include "BufferPool.h"
#include "DebugAssert.h"
#include "FileInformationStruct.h"
#include "FilesystemOperations.h"
#include "Strings.h"
#include "ValueOrError.h"

namespace Pathwinder
{
  /// Value used for enumeration queue byte positions to represent that there is nothing left in
  /// the queue.
  static constexpr unsigned int kInvalidEnumerationBufferBytePosition =
      static_cast<unsigned int>(-1);

  EnumerationQueue::EnumerationQueue(
      DirectoryEnumerationInstruction::SingleDirectoryEnumeration matchInstruction,
      std::wstring_view absoluteDirectoryPath,
      FILE_INFORMATION_CLASS fileInformationClass,
      std::wstring_view filePattern)
      : IDirectoryOperationQueue(),
        matchInstruction(matchInstruction),
        directoryHandle(NULL),
        fileInformationClass(fileInformationClass),
        fileInformationStructLayout(
            FileInformationStructLayout::LayoutForFileInformationClass(fileInformationClass)
                .value_or(FileInformationStructLayout())),
        enumerationBuffer(),
        enumerationBufferBytePosition(),
        enumerationStatus()
  {
    if (FileInformationStructLayout() == fileInformationStructLayout)
    {
      enumerationStatus = NtStatus::kInvalidInfoClass;
      return;
    }

    auto maybeDirectoryHandle =
        FilesystemOperations::OpenDirectoryForEnumeration(absoluteDirectoryPath);
    if (true == maybeDirectoryHandle.HasError())
    {
      // It is not an error for the directory not to exist.
      // Anything other error, however, is a directory enumeration error that should be
      // recorded and provided back to the application.
      switch (maybeDirectoryHandle.Error())
      {
        case NtStatus::kNoSuchFile:
        case NtStatus::kObjectNameInvalid:
        case NtStatus::kObjectNameNotFound:
        case NtStatus::kObjectPathInvalid:
        case NtStatus::kObjectPathNotFound:
          break;

        default:
          enumerationStatus = maybeDirectoryHandle.Error();
          return;
      }

      directoryHandle = NULL;
    }
    else
    {
      directoryHandle = maybeDirectoryHandle.Value();
    }

    Restart(filePattern);
  }

  EnumerationQueue::EnumerationQueue(EnumerationQueue&& other)
      : IDirectoryOperationQueue(),
        matchInstruction(std::move(other.matchInstruction)),
        directoryHandle(std::move(other.directoryHandle)),
        fileInformationClass(std::move(other.fileInformationClass)),
        fileInformationStructLayout(std::move(other.fileInformationStructLayout)),
        enumerationBuffer(std::move(other.enumerationBuffer)),
        enumerationBufferBytePosition(std::move(other.enumerationBufferBytePosition)),
        enumerationStatus(std::move(other.enumerationStatus))
  {
    other.directoryHandle = NULL;
  }

  EnumerationQueue::~EnumerationQueue(void)
  {
    if (NULL != directoryHandle) FilesystemOperations::CloseHandle(directoryHandle);
  }

  NameInsertionQueue::NameInsertionQueue(
      TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion>&&
          nameInsertionQueue,
      FILE_INFORMATION_CLASS fileInformationClass,
      std::wstring_view queryFilePattern)
      : IDirectoryOperationQueue(),
        nameInsertionQueue(std::move(nameInsertionQueue)),
        nameInsertionQueuePosition(0),
        fileInformationClass(fileInformationClass),
        fileInformationStructLayout(
            FileInformationStructLayout::LayoutForFileInformationClass(fileInformationClass)
                .value_or(FileInformationStructLayout())),
        enumerationBuffer(),
        enumerationStatus(),
        filePattern()
  {
    if (FileInformationStructLayout() == fileInformationStructLayout)
    {
      enumerationStatus = NtStatus::kInvalidInfoClass;
      return;
    }

    Restart(queryFilePattern);
  }

  MergedFileInformationQueue::MergedFileInformationQueue(TQueuesToMerge&& queuesToMerge)
      : IDirectoryOperationQueue(),
        queuesToMerge(std::move(queuesToMerge)),
        frontElementSourceQueue(nullptr)
  {
    SelectFrontElementSourceQueueInternal();
  }

  void EnumerationQueue::AdvanceQueueContentsInternal(
      ULONG queryFlags, std::wstring_view filePattern)
  {
    if (NULL == directoryHandle)
    {
      // Directory to be enumerated does not exist. This just means no file information
      // structures are available.
      enumerationBufferBytePosition = kInvalidEnumerationBufferBytePosition;
      enumerationStatus = NtStatus::kNoMoreFiles;
      return;
    }

    NTSTATUS directoryEnumerationResult = FilesystemOperations::PartialEnumerateDirectoryContents(
        directoryHandle,
        fileInformationClass,
        enumerationBuffer.Data(),
        enumerationBuffer.Size(),
        queryFlags,
        filePattern);
    if (!(NT_SUCCESS(directoryEnumerationResult)))
    {
      // This failure block includes `STATUS_NO_MORE_FILES` in which case enumeration is
      // complete.
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

  void EnumerationQueue::PopFrontInternal(void)
  {
    const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

    FileInformationStructLayout::TNextEntryOffset bytePositionIncrement =
        fileInformationStructLayout.ReadNextEntryOffset(enumerationEntry);
    if (0 == bytePositionIncrement)
    {
      AdvanceQueueContentsInternal();
    }
    else
    {
      enumerationBufferBytePosition += bytePositionIncrement;
    }
  }

  void EnumerationQueue::SkipNonMatchingItemsInternal(void)
  {
    while ((NT_SUCCESS(enumerationStatus)) &&
           ((false == matchInstruction.ShouldIncludeInDirectoryEnumeration(FileNameOfFront()))))
      PopFrontInternal();
  }

  void NameInsertionQueue::AdvanceQueueContentsInternal(void)
  {
    const DirectoryEnumerationInstruction::SingleDirectoryNameInsertion* nameInsertion = nullptr;
    NTSTATUS nameInsertionQueryResult = NtStatus::kInternalError;

    do
    {
      if (nameInsertionQueuePosition == nameInsertionQueue.Size())
      {
        enumerationStatus = NtStatus::kNoMoreFiles;
        return;
      }

      nameInsertion = &nameInsertionQueue[nameInsertionQueuePosition];

      // If the filename to be inserted does not match the query file pattern then it can
      // simply be skipped.
      if (false == Strings::FileNameMatchesPattern(nameInsertion->FileNameToInsert(), filePattern))
      {
        nameInsertionQueuePosition += 1;
        continue;
      }

      nameInsertionQueryResult = FilesystemOperations::QuerySingleFileDirectoryInformation(
          nameInsertion->DirectoryInformationSourceDirectoryPart(),
          nameInsertion->DirectoryInformationSourceFilePart(),
          fileInformationClass,
          enumerationBuffer.Data(),
          enumerationBuffer.Size());

      // It is not an error for the filesystem entities being queried not to exist and thus be
      // unavailable. They can just be skipped, and the next name insertion tried. Anything
      // other error, however, is a directory enumeration error that should be recorded and
      // provided back to the application.
      switch (nameInsertionQueryResult)
      {
        case NtStatus::kNoSuchFile:
        case NtStatus::kObjectNameInvalid:
        case NtStatus::kObjectNameNotFound:
        case NtStatus::kObjectPathInvalid:
        case NtStatus::kObjectPathNotFound:
          nameInsertionQueuePosition += 1;
          continue;

        default:
          if (NT_SUCCESS(nameInsertionQueryResult)) break;

          enumerationStatus = nameInsertionQueryResult;
          return;
      }
    }
    while (!(NT_SUCCESS(nameInsertionQueryResult)));

    nameInsertionQueuePosition += 1;
    fileInformationStructLayout.WriteFileName(
        enumerationBuffer.Data(), nameInsertion->FileNameToInsert(), enumerationBuffer.Size());

    enumerationStatus = NtStatus::kMoreEntries;
  }

  void MergedFileInformationQueue::SelectFrontElementSourceQueueInternal(void)
  {
    IDirectoryOperationQueue* nextFrontQueueCandidate = nullptr;

    // The next front element will come from whichever queue is present, has more entries, and
    // sorts lowest using case-insensitive sorting. If all queues are already done then there
    // will be no next front element.
    for (const auto& underlyingQueue : queuesToMerge)
    {
      if ((nullptr == underlyingQueue) ||
          (NtStatus::kMoreEntries != underlyingQueue->EnumerationStatus()))
        continue;

      if ((nullptr == nextFrontQueueCandidate) ||
          (Strings::CompareCaseInsensitive(
               underlyingQueue->FileNameOfFront(), nextFrontQueueCandidate->FileNameOfFront()) < 0))
        nextFrontQueueCandidate = underlyingQueue.get();
    }

    frontElementSourceQueue = nextFrontQueueCandidate;
  }

  unsigned int EnumerationQueue::CopyFront(void* dest, unsigned int capacityBytes) const
  {
    const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

    const unsigned int numBytesToCopy = std::min(SizeOfFront(), capacityBytes);
    std::memcpy(dest, enumerationEntry, static_cast<size_t>(numBytesToCopy));

    return numBytesToCopy;
  }

  NTSTATUS EnumerationQueue::EnumerationStatus(void) const
  {
    return enumerationStatus;
  }

  std::wstring_view EnumerationQueue::FileNameOfFront(void) const
  {
    const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

    return fileInformationStructLayout.ReadFileName(enumerationEntry);
  }

  void EnumerationQueue::PopFront(void)
  {
    PopFrontInternal();
    SkipNonMatchingItemsInternal();
  }

  void EnumerationQueue::Restart(std::wstring_view queryFilePattern)
  {
    AdvanceQueueContentsInternal(SL_RESTART_SCAN, queryFilePattern);
    SkipNonMatchingItemsInternal();
  }

  unsigned int EnumerationQueue::SizeOfFront(void) const
  {
    const void* const enumerationEntry = &enumerationBuffer[enumerationBufferBytePosition];

    return fileInformationStructLayout.SizeOfStruct(enumerationEntry);
  }

  unsigned int NameInsertionQueue::CopyFront(void* dest, unsigned int capacityBytes) const
  {
    const unsigned int numBytesToCopy = std::min(SizeOfFront(), capacityBytes);
    std::memcpy(dest, enumerationBuffer.Data(), static_cast<size_t>(numBytesToCopy));

    return numBytesToCopy;
  }

  NTSTATUS NameInsertionQueue::EnumerationStatus(void) const
  {
    return enumerationStatus;
  }

  std::wstring_view NameInsertionQueue::FileNameOfFront(void) const
  {
    return fileInformationStructLayout.ReadFileName(enumerationBuffer.Data());
  }

  void NameInsertionQueue::PopFront(void)
  {
    AdvanceQueueContentsInternal();
  }

  void NameInsertionQueue::Restart(std::wstring_view queryFilePattern)
  {
    if (0 == nameInsertionQueue.Size())
    {
      enumerationStatus = NtStatus::kNoMoreFiles;
      return;
    }

    if (false == queryFilePattern.empty())
    {
      filePattern = std::wstring(queryFilePattern);
      for (size_t i = 0; i < filePattern.size(); ++i)
        filePattern[i] = std::towupper(filePattern[i]);
    }

    nameInsertionQueuePosition = 0;
    AdvanceQueueContentsInternal();
  }

  unsigned int NameInsertionQueue::SizeOfFront(void) const
  {
    return fileInformationStructLayout.SizeOfStruct(enumerationBuffer.Data());
  }

  unsigned int MergedFileInformationQueue::CopyFront(void* dest, unsigned int capacityBytes) const
  {
    return frontElementSourceQueue->CopyFront(dest, capacityBytes);
  }

  NTSTATUS MergedFileInformationQueue::EnumerationStatus(void) const
  {
    // If any queue reports an error then the overall status is that error.
    // Otherwise, it is possible to determine whether or not enumeration is finished based on
    // the front element source queue pointer being `nullptr` or not.
    for (const auto& underlyingQueue : queuesToMerge)
    {
      if (nullptr == underlyingQueue) continue;

      const NTSTATUS underlyingQueueStatus = underlyingQueue->EnumerationStatus();
      switch (underlyingQueue->EnumerationStatus())
      {
        case NtStatus::kMoreEntries:
        case NtStatus::kNoMoreFiles:
          break;

        default:
          if (!(NT_SUCCESS(underlyingQueueStatus))) return underlyingQueueStatus;
          break;
      }
    }

    if (nullptr == frontElementSourceQueue) return NtStatus::kNoMoreFiles;

    return NtStatus::kMoreEntries;
  }

  std::wstring_view MergedFileInformationQueue::FileNameOfFront(void) const
  {
    return frontElementSourceQueue->FileNameOfFront();
  }

  void MergedFileInformationQueue::PopFront(void)
  {
    frontElementSourceQueue->PopFront();
    SelectFrontElementSourceQueueInternal();
  }

  void MergedFileInformationQueue::Restart(std::wstring_view queryFilePattern)
  {
    for (const auto& underlyingQueue : queuesToMerge)
    {
      if (nullptr == underlyingQueue) continue;

      underlyingQueue->Restart(queryFilePattern);
    }

    SelectFrontElementSourceQueueInternal();
  }

  unsigned int MergedFileInformationQueue::SizeOfFront(void) const
  {
    return frontElementSourceQueue->SizeOfFront();
  }
} // namespace Pathwinder
