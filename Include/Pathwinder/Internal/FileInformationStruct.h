/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file FileInformationStruct.h
 *   Declaration and partial implementation of manipulation functionality for the various file
 *   information structures that Windows system calls use as output during directory enumeration
 *   operations.
 **************************************************************************************************/

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

#include "ApiWindowsInternal.h"
#include "BufferPool.h"
#include "DebugAssert.h"

namespace Pathwinder
{
  /// Implements a byte-wise buffer for holding one or more file information structures without
  /// regard for their type or individual size. Directory enumeration system calls often produce
  /// multiple file information structures, which are placed contiguously in memory. This class
  /// internally allocates and maintains a pool of fixed-size buffers, which can grow as needed up
  /// to a pre-determined maximum number of buffers.
  class FileInformationStructBuffer
  {
  public:

    /// Size of each file information structure buffer, in bytes.
    /// Maximum of 64kB can be supported, based on third-party observed behavior of the various
    /// directory enumeration system calls.
    static constexpr unsigned int kBytesPerBuffer = 64 * 1024;

    /// Number of buffers to allocate initially and each time the pool is exhausted and more are
    /// needed.
    static constexpr unsigned int kBufferAllocationGranularity = 4;

    /// Maximum number of buffers to hold in the pool.
    /// If more buffers are needed beyond this number, for example due to a large number of
    /// parallel directory enumeration requests, then they will be deallocated instead of
    /// returned to the pool.
    static constexpr unsigned int kBufferPoolSize = 64;

    inline FileInformationStructBuffer(void) : buffer(bufferPool.Allocate()) {}

    inline ~FileInformationStructBuffer(void)
    {
      if (nullptr != buffer) bufferPool.Free(buffer);
    }

    FileInformationStructBuffer(const FileInformationStructBuffer& other) = delete;

    inline FileInformationStructBuffer(FileInformationStructBuffer&& other) noexcept
        : buffer(nullptr)
    {
      *this = std::move(other);
    }

    inline FileInformationStructBuffer& operator=(FileInformationStructBuffer&& other) noexcept
    {
      std::swap(buffer, other.buffer);
      return *this;
    }

    inline const uint8_t& operator[](unsigned int index) const
    {
      DebugAssert(index < Size(), "Index is out of bounds.");
      return Data()[index];
    }

    inline uint8_t& operator[](unsigned int index)
    {
      DebugAssert(index < Size(), "Index is out of bounds.");
      return Data()[index];
    }

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
    constexpr unsigned int Size(void) const
    {
      return kBytesPerBuffer;
    }

  private:

    /// Manages the pool of backing buffers.
    static inline BufferPool<kBytesPerBuffer, kBufferAllocationGranularity, kBufferPoolSize>
        bufferPool;

    /// Pointer to the buffer space.
    uint8_t* buffer;
  };

  /// Describes the layout of a file information structure for a given file information class.
  /// Provides reading and writing functionality for fields that are common to all supported file
  /// information structure types.
  class FileInformationStructLayout
  {
  public:

    /// Type used to represent the `nextEntryOffset` field of file information structures.
    using TNextEntryOffset = ULONG;

    /// Type used to represent the `fileNameLength` field of file information structures.
    using TFileNameLength = ULONG;

    /// Type used to represent the `fileName[1]` field of file information structures.
    using TFileNameChar = WCHAR;

    constexpr FileInformationStructLayout(void)
        : fileInformationClass(),
          structureBaseSizeBytes(),
          offsetOfNextEntryOffset(),
          offsetOfFileNameLength(),
          offsetOfFileName()
    {}

    /// This constructor is intended for internal use and is not generally intended to be
    /// invoked externally.
    constexpr FileInformationStructLayout(
        FILE_INFORMATION_CLASS fileInformationClass,
        unsigned int structureBaseSizeBytes,
        unsigned int offsetOfNextEntryOffset,
        unsigned int offsetOfFileNameLength,
        unsigned int offsetOfFileName)
        : fileInformationClass(fileInformationClass),
          structureBaseSizeBytes(structureBaseSizeBytes),
          offsetOfNextEntryOffset(offsetOfNextEntryOffset),
          offsetOfFileNameLength(offsetOfFileNameLength),
          offsetOfFileName(offsetOfFileName)
    {}

    bool operator==(const FileInformationStructLayout& other) const = default;

    /// Maintains a set of layout structures for the various supported file information classes
    /// for directory enumeration and returns a layout definition for a given file information
    /// class.
    /// @param [in] fileInformationClass File information class whose structure layout is
    /// needed.
    /// @return Layout information for the specified file information class, if the file
    /// information class is supported.
    static std::optional<FileInformationStructLayout> LayoutForFileInformationClass(
        FILE_INFORMATION_CLASS fileInformationClass);

    /// Returns the base size of the file information structure whose layout is represented by
    /// this object.
    /// @return Base structure size in bytes.
    inline unsigned int BaseStructureSize(void) const
    {
      return structureBaseSizeBytes;
    }

    /// Sets the `nextEntryOffset` field to 0 for the specified file information structure.
    /// This is useful for file information structures that are the last in a buffer of
    /// contiguous file information structures. Performs no verification on the input pointer or
    /// data structure.
    /// @param [in, out] fileInformationStruct Address of the first byte of the file information
    /// structure of interest.
    inline void ClearNextEntryOffset(void* fileInformationStruct) const
    {
      *(reinterpret_cast<TNextEntryOffset*>(
          reinterpret_cast<size_t>(fileInformationStruct) +
          static_cast<size_t>(offsetOfNextEntryOffset))) = 0;
    }

    /// Retrieves and returns the file information class enumerator that corresponds to the file
    /// informaton structure whose layout is described by this object.
    /// @return File information class enumerator.
    inline FILE_INFORMATION_CLASS FileInformationClass(void) const
    {
      return fileInformationClass;
    }

    /// Generates and returns a pointer to the trailing filename field for the specified file
    /// information structure. Performs no verification on the input pointer or data structure.
    /// @param [in] fileInformationStruct Address of the first byte of the file information
    /// structure of interest.
    /// @return Pointer to the file information structure's trailing `fileName[1]` field.
    inline TFileNameChar* FileNamePointer(const void* fileInformationStruct) const
    {
      return reinterpret_cast<TFileNameChar*>(
          reinterpret_cast<size_t>(fileInformationStruct) + static_cast<size_t>(offsetOfFileName));
    }

    /// Computes the hypothetical size, in bytes, of a file information structure if its
    /// trailing filename field had the specified length.
    /// @param [in] fileNameLengthBytes Hypothetical length of the trailing filename field, in
    /// bytes.
    /// @return Hypothetical size, in bytes, of a file information structure.
    inline unsigned int HypotheticalSizeForFileNameLength(unsigned int fileNameLengthBytes) const
    {
      return std::max(structureBaseSizeBytes, offsetOfFileName + fileNameLengthBytes);
    }

    /// Reads the `nextEntryOffset` field from the specified file information structure.
    /// Performs no verification on the input pointer or data structure.
    /// @param [in] fileInformationStruct Address of the first byte of the file information
    /// structure of interest.
    /// @return Value of the `nextEntryOffset` field of the file information structure.
    inline TNextEntryOffset ReadNextEntryOffset(const void* fileInformationStruct) const
    {
      return *reinterpret_cast<TNextEntryOffset*>(
          reinterpret_cast<size_t>(fileInformationStruct) +
          static_cast<size_t>(offsetOfNextEntryOffset));
    }

    /// Reads the `fileNameLength` field from the specified file information structure.
    /// Performs no verification on the input pointer or data structure.
    /// @param [in] fileInformationStruct Address of the first byte of the file information
    /// structure of interest.
    /// @return Value of the `fileNameLength` field of the file information structure.
    inline TFileNameLength ReadFileNameLength(const void* fileInformationStruct) const
    {
      return *reinterpret_cast<TFileNameLength*>(
          reinterpret_cast<size_t>(fileInformationStruct) +
          static_cast<size_t>(offsetOfFileNameLength));
    }

    /// Converts the trailing `fileName` field from the specified file information structure
    /// into a string view. Performs no verification on the input pointer or data structure.
    /// @param [in] fileInformationStruct Address of the first byte of the file information
    /// structure of interest.
    /// @return String view that can be used to read the trailing `fileName` field of the file
    /// information structure.
    std::basic_string_view<TFileNameChar> ReadFileName(const void* fileInformationStruct) const
    {
      return std::wstring_view(
          FileNamePointer(fileInformationStruct),
          (ReadFileNameLength(fileInformationStruct) / sizeof(TFileNameChar)));
    }

    /// Computes the size, in bytes, of the specified file information structure including its
    /// trailing filename field. Performs no verification on the input pointer or data
    /// structure.
    /// @param [in] fileInformationStruct Address of the first byte of the file information
    /// structure of interest.
    /// @return Size, in bytes, of the specified file information structure.
    inline unsigned int SizeOfStruct(const void* fileInformationStruct) const
    {
      return HypotheticalSizeForFileNameLength(
          static_cast<unsigned int>(ReadFileNameLength(fileInformationStruct)));
    }

    /// Updates the `nextEntryOffset` field for the specified file information structure using
    /// the known size of that structure. Performs no verification on the input pointer or data
    /// structure.
    /// @param [in, out] fileInformationStruct Address of the first byte of the file information
    /// structure of interest.
    inline void UpdateNextEntryOffset(void* fileInformationStruct) const
    {
      *(reinterpret_cast<TNextEntryOffset*>(
          reinterpret_cast<size_t>(fileInformationStruct) +
          static_cast<size_t>(offsetOfNextEntryOffset))) = SizeOfStruct(fileInformationStruct);
    }

    /// Writes the `fileNameLength` field for the specified file information structure and
    /// updates the associated structure field `nextEntryOffset` to maintain consistency.
    /// Performs no verification on the input pointer or data structure.
    /// @param [in, out] fileInformationStruct Address of the first byte of the file information
    /// structure of interest.
    /// @param [in] newFileNameLength New value to be written to the `fileNameLength` field.
    inline void WriteFileNameLength(
        void* fileInformationStruct, TFileNameLength newFileNameLength) const
    {
      *(reinterpret_cast<TFileNameLength*>(
          reinterpret_cast<size_t>(fileInformationStruct) +
          static_cast<size_t>(offsetOfFileNameLength))) = newFileNameLength;
      UpdateNextEntryOffset(fileInformationStruct);
    }

    /// Writes the trailing `fileName` field for the specified file information structure and
    /// updates associated structure fields (`nextEntryOffset` and `fileNameLength`) to maintain
    /// consistency. Performs no verification on the input pointer or data structure.
    /// @param [in, out] fileInformationStruct Address of the first byte of the file information
    /// structure of interest.
    /// @param [in] newFileName New value to be written to the trailing `fileName` field.
    /// @param [in] bufferCapacityBytes Total capacity of the buffer in which the file
    /// information structure itself is located, including the amount of space already occupied
    /// by the file information structure.
    inline void WriteFileName(
        void* fileInformationStruct,
        std::basic_string_view<TFileNameChar> newFileName,
        unsigned int bufferCapacityBytes) const
    {
      const unsigned int numBytesToWrite = std::min(
          (bufferCapacityBytes - offsetOfFileName),
          static_cast<unsigned int>(newFileName.length() * sizeof(TFileNameChar)));

      std::memcpy(FileNamePointer(fileInformationStruct), newFileName.data(), numBytesToWrite);
      WriteFileNameLength(fileInformationStruct, numBytesToWrite);
    }

  private:

    /// File information class enumerator that identifies the structure for which layout
    /// information is being supplied.
    FILE_INFORMATION_CLASS fileInformationClass;

    /// Base size of the entire structure, in bytes, without considering the variable length of
    /// the trailing filename field.
    unsigned int structureBaseSizeBytes;

    /// Byte offset of the `nextEntryOffset` field of the file information structure.
    unsigned int offsetOfNextEntryOffset;

    /// Byte offset of the `fileNameLength` field of the file information structure.
    unsigned int offsetOfFileNameLength;

    /// Byte offset of the `fileName[1]` field of the file information structure.
    unsigned int offsetOfFileName;
  };
} // namespace Pathwinder
