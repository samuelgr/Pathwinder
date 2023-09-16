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
#include <type_traits>

#include "ApiWindows.h"
#include "BufferPool.h"
#include "DebugAssert.h"

namespace Pathwinder
{
  // Determines whether or not the specified type is one of the known Windows internal file
  // information structures defined earlier in this file.
  template <typename FileInformationStructType> concept IsFileInformationStruct = std::is_same_v<
      const FILE_INFORMATION_CLASS,
      decltype(FileInformationStructType::kFileInformationClass)>;

  // Determines whether or not the specified type has a dangling filename field.
  template <typename FileInformationStructType> concept HasDanglingFilenameField =
      std::is_same_v<ULONG, decltype(FileInformationStructType::fileNameLength)> &&
      std::is_same_v<WCHAR[1], decltype(FileInformationStructType::fileName)>;

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

    /// Retrieves the stored filename from within one of the many structures that uses a dangling
    /// filename field, whose type must be known at compile-time.
    /// @tparam FileInformationStructType Windows internal structure type that uses a wide-character
    /// dangling filename field.
    /// @param [in] fileInformationStruct Read-only reference to a structure with a wide-character
    /// dangling filename field.
    /// @return String view representation of the wide-character dangling filename field.
    template <typename FileInformationStructType>
      requires IsFileInformationStruct<FileInformationStructType> &&
        HasDanglingFilenameField<FileInformationStructType>
    static constexpr std::wstring_view ReadFileNameByType(
        const FileInformationStructType& fileInformationStruct)
    {
      return ReadFileNameInternal(
          &fileInformationStruct,
          offsetof(FileInformationStructType, fileName),
          offsetof(FileInformationStructType, fileNameLength));
    }

    /// Computes the size, in bytes, of one of the many structures that uses a dangling filename
    /// field, whose type must be known at compile-time.
    /// @tparam FileInformationStructType Windows internal structure type that uses a wide-character
    /// dangling filename field.
    /// @param [in] fileInformationStruct Read-only reference to a structure with a wide-character
    /// dangling filename field.
    /// @return Total size, in bytes, of the specified structure.
    template <typename FileInformationStructType>
      requires IsFileInformationStruct<FileInformationStructType> &&
        HasDanglingFilenameField<FileInformationStructType>
    static inline unsigned int SizeOfStructByType(
        const FileInformationStructType& fileInformationStruct)
    {
      return HypotheticalSizeForFileNameLengthInternal(
          sizeof(FileInformationStructType),
          offsetof(FileInformationStructType, fileName),
          ReadFileNameLengthInternal(
              &fileInformationStruct, offsetof(FileInformationStructType, fileNameLength)));
    }

    /// Changes the stored filename within one of the many structures that uses a dangling filename
    /// field. On output, the filename member is updated to represent the specified filename string,
    /// but only up to whatever number of characters fit in the buffer containing the structure.
    /// Regardless, the length field is updated to represent the number of characters needed to
    /// represent the entire string.
    /// @tparam FileInformationStructType Windows internal structure type that uses a wide-character
    /// dangling filename field.
    /// @param [in, out] fileInformationStruct Mutable reference to a structure with a
    /// wide-character dangling filename field.
    /// @param [in] bufferCapacityBytes Total capacity of the buffer containing the file information
    /// structure, in bytes.
    /// @param [in] newFileName Filename to be set in the file information structure.
    template <typename FileInformationStructType>
      requires IsFileInformationStruct<FileInformationStructType> &&
        HasDanglingFilenameField<FileInformationStructType>
    static inline void WriteFileNameByType(
        FileInformationStructType& fileInformationStruct,
        unsigned int bufferCapacityBytes,
        std::wstring_view newFileName)
    {
      return WriteFileNameInternal(
          &fileInformationStruct,
          newFileName,
          bufferCapacityBytes,
          offsetof(FileInformationStructType, fileName),
          offsetof(FileInformationStructType, fileNameLength));
    }

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
      return FileNamePointerInternal(fileInformationStruct, offsetOfFileName);
    }

    /// Computes the hypothetical size, in bytes, of a file information structure if its
    /// trailing filename field had the specified length.
    /// @param [in] fileNameLengthBytes Hypothetical length of the trailing filename field, in
    /// bytes.
    /// @return Hypothetical size, in bytes, of a file information structure.
    inline unsigned int HypotheticalSizeForFileNameLength(unsigned int fileNameLengthBytes) const
    {
      return HypotheticalSizeForFileNameLengthInternal(
          structureBaseSizeBytes, offsetOfFileName, fileNameLengthBytes);
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
      return ReadFileNameLengthInternal(fileInformationStruct, offsetOfFileNameLength);
    }

    /// Converts the trailing `fileName` field from the specified file information structure
    /// into a string view. Performs no verification on the input pointer or data structure.
    /// @param [in] fileInformationStruct Address of the first byte of the file information
    /// structure of interest.
    /// @return String view that can be used to read the trailing `fileName` field of the file
    /// information structure.
    std::basic_string_view<TFileNameChar> ReadFileName(const void* fileInformationStruct) const
    {
      return ReadFileNameInternal(fileInformationStruct, offsetOfFileName, offsetOfFileNameLength);
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
      WriteFileNameLengthInternal(fileInformationStruct, newFileNameLength, offsetOfFileNameLength);
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
      WriteFileNameInternal(
          fileInformationStruct,
          newFileName,
          bufferCapacityBytes,
          offsetOfFileName,
          offsetOfFileNameLength);
      UpdateNextEntryOffset(fileInformationStruct);
    }

  private:

    /// Internal implementation of #HypotheticalSizeForFileNameLength.
    /// Relies on parameters instead of reading from instance variables.
    static unsigned int HypotheticalSizeForFileNameLengthInternal(
        unsigned int structureBaseSizeBytes,
        unsigned int offsetOfFileName,
        unsigned int fileNameLengthBytes);

    /// Internal implementation of #FileNamePointer.
    /// Relies on parameters instead of reading from instance variables.
    static TFileNameChar* FileNamePointerInternal(
        const void* fileInformationStruct, unsigned int offsetOfFileName);

    /// Internal implementation of #ReadFileName.
    /// Relies on parameters instead of reading from instance variables.
    static std::wstring_view ReadFileNameInternal(
        const void* fileInformationStruct,
        unsigned int offsetOfFileName,
        unsigned int offsetOfFileNameLength);

    /// Internal implementation of #ReadFileNameLength.
    /// Relies on parameters instead of reading from instance variables.
    static TFileNameLength ReadFileNameLengthInternal(
        const void* fileInformationStruct, unsigned int offsetOfFileNameLength);

    /// Internal implementation of #WriteFileName.
    /// Relies on parameters instead of reading from instance variables.
    static void WriteFileNameInternal(
        void* fileInformationStruct,
        std::basic_string_view<TFileNameChar> newFileName,
        unsigned int bufferCapacityBytes,
        unsigned int offsetOfFileName,
        unsigned int offsetOfFileNameLength);

    /// Internal implementation of #WriteFileNameLength.
    /// Relies on parameters instead of reading from instance variables.
    static void WriteFileNameLengthInternal(
        void* fileInformationStruct,
        TFileNameLength newFileNameLength,
        unsigned int offsetOfFileNameLength);

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

  // Structures defined below are file information structures found in the Windows driver kit. They
  // are defined here to avoid an explicit dependency on the Windows driver kit. These internal
  // definitions can be used with the various file information structure types and functions defined
  // earlier in this file.

  /// Contains information about a file in a directory. Same layout as
  /// `FILE_DIRECTORY_INFORMATION` from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_directory_information
  struct SFileDirectoryInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(1);

    ULONG nextEntryOffset;
    ULONG fileIndex;
    LARGE_INTEGER creationTime;
    LARGE_INTEGER lastAccessTime;
    LARGE_INTEGER lastWriteTime;
    LARGE_INTEGER changeTime;
    LARGE_INTEGER endOfFile;
    LARGE_INTEGER allocationSize;
    ULONG fileAttributes;
    ULONG fileNameLength;
    WCHAR fileName[1];
  };

  /// Contains information about a file in a directory. Same layout as `FILE_FULL_DIR_INFORMATION`
  /// from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_full_dir_information
  struct SFileFullDirectoryInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(2);

    ULONG nextEntryOffset;
    ULONG fileIndex;
    LARGE_INTEGER creationTime;
    LARGE_INTEGER lastAccessTime;
    LARGE_INTEGER lastWriteTime;
    LARGE_INTEGER changeTime;
    LARGE_INTEGER endOfFile;
    LARGE_INTEGER allocationSize;
    ULONG fileAttributes;
    ULONG fileNameLength;
    ULONG eaSize;
    WCHAR fileName[1];
  };

  /// Contains information about a file in a directory. Same layout as `FILE_BOTH_DIR_INFORMATION`
  /// from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_both_dir_information
  struct SFileBothDirectoryInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(3);

    ULONG nextEntryOffset;
    ULONG fileIndex;
    LARGE_INTEGER creationTime;
    LARGE_INTEGER lastAccessTime;
    LARGE_INTEGER lastWriteTime;
    LARGE_INTEGER changeTime;
    LARGE_INTEGER endOfFile;
    LARGE_INTEGER allocationSize;
    ULONG fileAttributes;
    ULONG fileNameLength;
    ULONG eaSize;
    CCHAR shortNameLength;
    WCHAR shortName[12];
    WCHAR fileName[1];
  };

  /// Contains information about a file. Same layout as `FILE_BASIC_INFORMATION` from the Windows
  /// driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_file_basic_information
  struct SFileBasicInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(4);

    LARGE_INTEGER creationTime;
    LARGE_INTEGER lastAccessTime;
    LARGE_INTEGER lastWriteTime;
    LARGE_INTEGER changeTime;
    ULONG fileAttributes;
  };

  /// Contains information about a file. Same layout as `FILE_STANDARD_INFORMATION` from the
  /// Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_file_standard_information
  struct SFileStandardInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(5);

    LARGE_INTEGER allocationSize;
    LARGE_INTEGER endOfFile;
    ULONG numberOfLinks;
    BOOLEAN deletePending;
    BOOLEAN directory;
  };

  /// Contains information about a file. Same layout as `FILE_INTERNAL_INFORMATION` from the
  /// Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_internal_information
  struct SFileInternalInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(6);

    LARGE_INTEGER indexNumber;
  };

  /// Contains information about a file. Same layout as `FILE_EA_INFORMATION` from the Windows
  /// driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_ea_information
  struct SFileExtendedAttributeInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(7);

    ULONG eaSize;
  };

  /// Contains information about a file. Same layout as `FILE_ACCESS_INFORMATION` from the Windows
  /// driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_access_information
  struct SFileAccessInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(8);

    ACCESS_MASK accessFlags;
  };

  /// Contains information about a file. Same layout as `FILE_NAME_INFORMATION` from the Windows
  /// driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_name_information
  struct SFileNameInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(9);

    ULONG fileNameLength;
    WCHAR fileName[1];
  };

  /// Specifies a file rename operation. Same layout as `FILE_RENAME_INFORMATION` from the Windows
  /// driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information
  struct SFileRenameInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(10);

    union
    {
      BOOLEAN replaceIfExists;
      ULONG flags;
    };

    HANDLE rootDirectory;
    ULONG fileNameLength;
    WCHAR fileName[1];
  };

  /// Contains information about a file in a directory. Same layout as `FILE_NAMES_INFORMATION`
  /// from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_names_information
  struct SFileNamesInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(12);

    ULONG nextEntryOffset;
    ULONG fileIndex;
    ULONG fileNameLength;
    WCHAR fileName[1];
  };

  /// Specifies file deletion behavior when open handles to it are closed. Same layout as
  /// `FILE_DISPOSITION_INFORMATION` from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_disposition_information
  struct SFileDispositionInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(13);

    BOOLEAN deleteFile;
  };

  /// Contains information about a file. Same layout as `FILE_POSITION_INFORMATION` from the
  /// Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_file_position_information
  struct SFilePositionInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(14);

    LARGE_INTEGER currentByteOffset;
  };

  /// Contains information about a file. Same layout as `FILE_MODE_INFORMATION` from the Windows
  /// driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_mode_information
  struct SFileModeInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(16);

    ULONG mode;
  };

  /// Contains information about a file. Same layout as `FILE_ALIGNMENT_INFORMATION` from the
  /// Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_alignment_information
  struct SFileAlignmentInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(17);

    ULONG alignmentRequirement;
  };

  /// Contains information about a file. Same layout as `FILE_ALL_INFORMATION` from the Windows
  /// driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_alignment_information
  struct SFileAllInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(18);

    SFileBasicInformation basicInformation;
    SFileStandardInformation standardInformation;
    SFileInternalInformation internalInformation;
    SFileExtendedAttributeInformation eaInformation;
    SFileAccessInformation accessInformation;
    SFilePositionInformation positionInformation;
    SFileModeInformation modeInformation;
    SFileAlignmentInformation alignmentInformation;
    SFileNameInformation nameInformation;
  };

  /// Contains information about a file in a directory. Same layout as
  /// `FILE_ID_BOTH_DIR_INFORMATION` from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_both_dir_information
  struct SFileIdBothDirectoryInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(37);

    ULONG nextEntryOffset;
    ULONG fileIndex;
    LARGE_INTEGER creationTime;
    LARGE_INTEGER lastAccessTime;
    LARGE_INTEGER lastWriteTime;
    LARGE_INTEGER changeTime;
    LARGE_INTEGER endOfFile;
    LARGE_INTEGER allocationSize;
    ULONG fileAttributes;
    ULONG fileNameLength;
    ULONG eaSize;
    CCHAR shortNameLength;
    WCHAR shortName[12];
    LARGE_INTEGER fileId;
    WCHAR fileName[1];
  };

  /// Contains information about a file in a directory. Same layout as
  /// `FILE_ID_FULL_DIR_INFORMATION` from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_full_dir_information
  struct SFileIdFullDirectoryInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(38);

    ULONG nextEntryOffset;
    ULONG fileIndex;
    LARGE_INTEGER creationTime;
    LARGE_INTEGER lastAccessTime;
    LARGE_INTEGER lastWriteTime;
    LARGE_INTEGER changeTime;
    LARGE_INTEGER endOfFile;
    LARGE_INTEGER allocationSize;
    ULONG fileAttributes;
    ULONG fileNameLength;
    ULONG eaSize;
    LARGE_INTEGER fileId;
    WCHAR fileName[1];
  };

  /// Contains information about a file in a directory. Same layout as
  /// `FILE_ID_GLOBAL_TX_DIR_INFORMATION` from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_global_tx_dir_information
  struct SFileIdGlobalTxDirectoryInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(50);

    ULONG nextEntryOffset;
    ULONG fileIndex;
    LARGE_INTEGER creationTime;
    LARGE_INTEGER lastAccessTime;
    LARGE_INTEGER lastWriteTime;
    LARGE_INTEGER changeTime;
    LARGE_INTEGER endOfFile;
    LARGE_INTEGER allocationSize;
    ULONG fileAttributes;
    ULONG fileNameLength;
    LARGE_INTEGER fileId;
    GUID lockingTransactionId;
    ULONG txInfoFlags;
    WCHAR fileName[1];
  };

  /// Contains information about a file in a directory. Same layout as
  /// `FILE_ID_EXTD_DIR_INFORMATION` from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-file_id_extd_dir_information
  struct SFileIdExtdDirectoryInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(60);

    ULONG nextEntryOffset;
    ULONG fileIndex;
    LARGE_INTEGER creationTime;
    LARGE_INTEGER lastAccessTime;
    LARGE_INTEGER lastWriteTime;
    LARGE_INTEGER changeTime;
    LARGE_INTEGER endOfFile;
    LARGE_INTEGER allocationSize;
    ULONG fileAttributes;
    ULONG fileNameLength;
    ULONG eaSize;
    ULONG reparsePointTag;
    FILE_ID_128 fileId;
    WCHAR fileName[1];
  };

  /// Contains information about a file in a directory. Same layout as
  /// `FILE_ID_EXTD_BOTH_DIR_INFORMATION` from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_extd_both_dir_information
  struct SFileIdExtdBothDirectoryInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(63);

    ULONG nextEntryOffset;
    ULONG fileIndex;
    LARGE_INTEGER creationTime;
    LARGE_INTEGER lastAccessTime;
    LARGE_INTEGER lastWriteTime;
    LARGE_INTEGER changeTime;
    LARGE_INTEGER endOfFile;
    LARGE_INTEGER allocationSize;
    ULONG fileAttributes;
    ULONG fileNameLength;
    ULONG eaSize;
    ULONG reparsePointTag;
    FILE_ID_128 fileId;
    CCHAR shortNameLength;
    WCHAR shortName[12];
    WCHAR fileName[1];
  };

  /// Specifies file deletion behavior when open handles to it are closed. Same layout as
  /// `FILE_DISPOSITION_INFORMATION_EX` from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_disposition_information_ex
  struct SFileDispositionInformationEx
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(64);

    ULONG flags;
  };

  /// Contains file metadata. Corresponds to `FILE_STAT_INFORMATION` from the Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_stat_information
  struct SFileStatInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(68);

    LARGE_INTEGER fileId;
    LARGE_INTEGER creationTime;
    LARGE_INTEGER lastAccessTime;
    LARGE_INTEGER lastWriteTime;
    LARGE_INTEGER changeTime;
    LARGE_INTEGER allocationSize;
    LARGE_INTEGER endOfFile;
    ULONG fileAttributes;
    ULONG reparseTag;
    ULONG numberOfLinks;
    ACCESS_MASK effectiveAccess;
  };

  /// Specifies a hard link creation operation. Corresponds to `FILE_LINK_INFORMATION` from the
  /// Windows driver kit.
  /// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_link_information
  struct SFileLinkInformation
  {
    static constexpr FILE_INFORMATION_CLASS kFileInformationClass =
        static_cast<FILE_INFORMATION_CLASS>(72);

    union
    {
      BOOLEAN replaceIfExists;
      ULONG flags;
    };

    HANDLE rootDirectory;
    ULONG fileNameLength;
    WCHAR fileName[1];
  };
} // namespace Pathwinder
