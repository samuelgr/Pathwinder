/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file MockFilesystemOperations.h
 *   Declaration of controlled fake filesystem operations that can be used for testing.
 **************************************************************************************************/

#pragma once

#include "TestCase.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ApiWindows.h"
#include "FilesystemOperations.h"
#include "MockFreeFunctionContext.h"
#include "Strings.h"
#include "TemporaryBuffer.h"
#include "ValueOrError.h"

namespace PathwinderTest
{
  /// Context controlling object that implements mock filesystem operations. Each object supports
  /// creation of a fake filesystem, which is then supplied to test cases via the internal
  /// filesystem operations API.
  MOCK_FREE_FUNCTION_CONTEXT_CLASS(MockFilesystemOperations)
  {
  public:

    /// Enumerates different kinds of filesystem entities that can be part of the mock
    /// filesystem.
    enum class EFilesystemEntityType
    {
      File,
      Directory
    };

    /// Contains the information needed to represent a filesystem entity. This forms the "value"
    /// part of a key-value store representing a filesystem, so the name is not necessary here.
    /// Rather, it is the "key" part.
    struct SFilesystemEntity
    {
      EFilesystemEntityType type;
      unsigned int sizeInBytes;
    };

    /// Type alias for the contents of an individual directory. Key is a filename and value is
    /// the file's metadata. Sorting is required for this type because directory enumeration
    /// operations typically produce data in case-insensitive sorted order by filename.
    using TDirectoryContents = std::map<
        std::wstring,
        SFilesystemEntity,
        Pathwinder::Strings::CaseInsensitiveLessThanComparator<wchar_t>>;

    /// Type alias for the contents of an entire mock filesystem. Key is a directory name and
    /// value is the directory's contents. This is a single-level data structure whereby all
    /// directories of arbitrary depth in the hierarchy are represented by name in this data
    /// structure.
    using TFilesystemContents = std::unordered_map<
        std::wstring,
        TDirectoryContents,
        Pathwinder::Strings::CaseInsensitiveHasher<wchar_t>,
        Pathwinder::Strings::CaseInsensitiveEqualityComparator<wchar_t>>;

    /// Represents the state of an in-progress mock directory enumeration.
    struct SDirectoryEnumerationState
    {
      /// File pattern provided at the start of the directory enumeration operation. Used to
      /// filter filenames that are enumerated.
      std::wstring filePattern;

      /// Iterator for the next item to enumerate.
      TDirectoryContents::const_iterator nextItemIterator;

      /// Iterator that represents the first item in the directory contents.
      TDirectoryContents::const_iterator beginIterator;

      /// Iterator that represents the one-past-the-end item in the directory contents.
      TDirectoryContents::const_iterator endIterator;
    };

    MockFilesystemOperations(void);

    MockFilesystemOperations(const MockFilesystemOperations& other) = delete;

    MockFilesystemOperations(MockFilesystemOperations && other) = default;

    /// Inserts a directory and all its parents into the fake filesystem.
    /// @param [in] absolutePath Absolute path of the directory to insert. Paths are
    /// case-insensitive.
    inline void AddDirectory(std::wstring_view absolutePath)
    {
      AddFilesystemEntityInternal(absolutePath, EFilesystemEntityType::Directory, 0);
    }

    /// Inserts a file and all its parent directories into the fake filesystem.
    /// @param [in] absolutePath Absolute path of the file to insert. Paths are
    /// case-insensitive.
    /// @param [in] sizeInBytes Size, in bytes, of the file being added. Defaults to 0.
    inline void AddFile(std::wstring_view absolutePath, unsigned int fileSizeInBytes = 0)
    {
      AddFilesystemEntityInternal(absolutePath, EFilesystemEntityType::File, fileSizeInBytes);
    }

    /// Retrieves the name of the filesystem entity associated with the specified handle that is
    /// already open.
    /// @param [in] handle Handle to query for the associated directory full path.
    /// @return Full path of the directory, if it is open for enumeration.
    std::optional<std::wstring_view> GetPathFromHandle(HANDLE handle);

    /// Generates a handle and marks a file or directory in the fake filesystem as being open.
    /// @param [in] absolutePath Absolute path of the file or directory to open, which must already
    /// exist in the fake filesystem. Paths are case-insensitive.
    /// @return Handle to the newly-opened file or directory.
    HANDLE Open(std::wstring_view absolutePath);

    // FilesystemOperations
    NTSTATUS CloseHandle(HANDLE handle);
    intptr_t CreateDirectoryHierarchy(std::wstring_view absoluteDirectoryPath);
    bool Exists(std::wstring_view absolutePath);
    bool IsDirectory(std::wstring_view absolutePath);
    Pathwinder::ValueOrError<HANDLE, NTSTATUS> OpenDirectoryForEnumeration(
        std::wstring_view absoluteDirectoryPath);
    NTSTATUS PartialEnumerateDirectoryContents(
        HANDLE directoryHandle,
        FILE_INFORMATION_CLASS fileInformationClass,
        void* enumerationBuffer,
        unsigned int enumerationBufferCapacityBytes,
        ULONG queryFlags,
        std::wstring_view filePattern);
    Pathwinder::ValueOrError<Pathwinder::TemporaryString, NTSTATUS> QueryAbsolutePathByHandle(
        HANDLE fileHandle);
    Pathwinder::ValueOrError<ULONG, NTSTATUS> QueryFileHandleMode(HANDLE fileHandle);
    NTSTATUS QuerySingleFileDirectoryInformation(
        std::wstring_view absoluteDirectoryPath,
        std::wstring_view fileName,
        FILE_INFORMATION_CLASS fileInformationClass,
        void* enumerationBuffer,
        unsigned int enumerationBufferCapacityBytes);

  private:

    /// Inserts a filesystem entity and all of its parent directories into the fake filesystem.
    /// For internal use only.
    /// @param [in] absolutePath Absolute path of the directory to insert. Paths are
    /// case-insensitive.
    /// @param [in] type Type of filesystem entity to be inserted.
    /// @param [in] sizeInBytes Size, in bytes, of the new filesystem entity.
    void AddFilesystemEntityInternal(
        std::wstring_view absolutePath, EFilesystemEntityType type, unsigned int sizeInBytes);

    /// Attempts to generates a handle and marks a file or directory in the fake filesystem as being
    /// open. This method will fail if the requested filesystem entity does not exist in the fake
    /// filesystem.
    /// @param [in] absolutePath Absolute path of the file or directory to open. Paths are
    /// case-insensitive.
    /// @return Handle to the newly-opened file or directory, or `nullptr` if it does not exist.
    HANDLE OpenFilesystemEntityInternal(std::wstring_view absolutePath);

    /// Contents of the mock filesystem. Top-level map key is an absolute directory name and value
    /// is a set of directory contents.
    TFilesystemContents filesystemContents;

    /// Open filesystem handles for files and directories. Maps from handle to directory full path.
    std::unordered_map<HANDLE, std::wstring> openFilesystemHandles;

    /// In-progress directory enumerations.
    /// Maps from handle to directory enumeration state.
    std::unordered_map<HANDLE, SDirectoryEnumerationState> inProgressDirectoryEnumerations;

    /// Next handle value to use when opening a directory handle.
    /// Used to ensure handle values are all locally unique. The actual value is opaque.
    size_t nextHandleValue;
  };
} // namespace PathwinderTest
