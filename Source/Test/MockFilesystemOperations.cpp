/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file MockFilesystemOperations.cpp
 *   Implementation of controlled fake filesystem operations that can be used for testing.
 **************************************************************************************************/

#include "MockFilesystemOperations.h"

#include <cstring>
#include <cwctype>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ApiWindows.h"
#include "FileInformationStruct.h"
#include "Strings.h"
#include "ValueOrError.h"

namespace PathwinderTest
{
  /// Creates a file pattern string object from a given file pattern string view.
  /// @param [in] filePattern File pattern for which a string object is needed.
  /// @return File pattern string object.
  static std::wstring MakeFilePatternString(std::wstring_view filePattern)
  {
    if (true == filePattern.empty()) return std::wstring();

    // File patterns must be in upper-case due to an implementation quirk with the underlying
    // file pattern matching Windows API function.
    std::wstring filePatternString(filePattern);
    for (size_t i = 0; i < filePatternString.size(); ++i)
      filePatternString[i] = std::towupper(filePattern[i]);
    return filePatternString;
  }

  MockFilesystemOperations::MockFilesystemOperations(void)
      : filesystemContents(),
        openDirectoryHandles(),
        inProgressDirectoryEnumerations(),
        nextHandleValue(1000)
  {}

  void MockFilesystemOperations::AddFilesystemEntityInternal(
      std::wstring_view absolutePath, EFilesystemEntityType type, unsigned int sizeInBytes)
  {
    std::wstring_view currentPathView = absolutePath;

    size_t lastBackslashIndex = currentPathView.find_last_of(L'\\');

    switch (type)
    {
      case EFilesystemEntityType::File:
        if (std::wstring_view::npos == lastBackslashIndex)
          TEST_FAILED_BECAUSE(
              L"%s: Missing '\\' in absolute path \"%.*s\" when adding a file to a fake filesystem.",
              __FUNCTIONW__,
              static_cast<int>(absolutePath.length()),
              absolutePath.data());
        break;

      case EFilesystemEntityType::Directory:
        if (false == filesystemContents.contains(currentPathView))
          filesystemContents.emplace(std::wstring(currentPathView), TDirectoryContents());
        break;

      default:
        TEST_FAILED_BECAUSE(
            L"%s: Internal error: Unknown filesystem entity type when adding to a fake filesystem.",
            __FUNCTIONW__);
    }

    while (lastBackslashIndex != std::wstring_view::npos)
    {
      std::wstring_view directoryPart = currentPathView.substr(0, lastBackslashIndex);
      std::wstring_view filePart = currentPathView.substr(lastBackslashIndex + 1);

      auto directoryIter = filesystemContents.find(directoryPart);
      if (filesystemContents.end() == directoryIter)
        directoryIter =
            filesystemContents.insert({std::wstring(directoryPart), TDirectoryContents()}).first;

      directoryIter->second.insert(
          {std::wstring(filePart), {.type = type, .sizeInBytes = sizeInBytes}});

      // Only the first thing that is inserted could possibly be a file, all the rest are
      // intermediate directories along the path.
      type = EFilesystemEntityType::Directory;
      sizeInBytes = 0;

      // Continue working backwards through all parent directories and adding them as they are
      // identified.
      currentPathView = directoryPart;
      lastBackslashIndex = currentPathView.find_last_of(L'\\');
    }
  }

  void MockFilesystemOperations::CloseHandle(HANDLE handle)
  {
    const auto directoryHandleIter = openDirectoryHandles.find(handle);
    if (openDirectoryHandles.cend() == directoryHandleIter)
      TEST_FAILED_BECAUSE(L"%s: Attempting to close a handle that is not open.", __FUNCTIONW__);

    openDirectoryHandles.erase(directoryHandleIter);
  }

  intptr_t MockFilesystemOperations::CreateDirectoryHierarchy(
      std::wstring_view absoluteDirectoryPath)
  {
    TEST_FAILED_BECAUSE(L"%s: Unimplemented mock function called.", __FUNCTIONW__);
  }

  bool MockFilesystemOperations::Exists(std::wstring_view absolutePath)
  {
    size_t lastBackslashIndex = absolutePath.find_last_of(L'\\');
    if (std::wstring_view::npos == lastBackslashIndex) return false;

    std::wstring_view directoryPart = absolutePath.substr(0, lastBackslashIndex);
    std::wstring_view filePart = absolutePath.substr(lastBackslashIndex + 1);

    const auto directoryIter = filesystemContents.find(directoryPart);
    if (filesystemContents.cend() == directoryIter) return false;

    return (filePart.empty() || directoryIter->second.contains(filePart));
  }

  bool MockFilesystemOperations::IsDirectory(std::wstring_view absolutePath)
  {
    return filesystemContents.contains(absolutePath);
  }

  Pathwinder::ValueOrError<HANDLE, NTSTATUS> MockFilesystemOperations::OpenDirectoryForEnumeration(
      std::wstring_view absoluteDirectoryPath)
  {
    const auto directoryIter = filesystemContents.find(absoluteDirectoryPath);
    if (filesystemContents.cend() == directoryIter)
      return Pathwinder::NtStatus::kObjectNameNotFound;

    const HANDLE handleValue = reinterpret_cast<HANDLE>(nextHandleValue++);
    const bool insertWasSuccessful =
        openDirectoryHandles.emplace(handleValue, std::wstring_view(directoryIter->first)).second;

    if (false == insertWasSuccessful)
      TEST_FAILED_BECAUSE(
          "%s: Internal implementation error due to failure to insert a handle value that is expected to be unique.",
          __FUNCTIONW__);

    return handleValue;
  }

  NTSTATUS MockFilesystemOperations::PartialEnumerateDirectoryContents(
      HANDLE directoryHandle,
      FILE_INFORMATION_CLASS fileInformationClass,
      void* enumerationBuffer,
      unsigned int enumerationBufferCapacityBytes,
      ULONG queryFlags,
      std::wstring_view filePattern)
  {
    const auto maybeFileInformationStructLayout =
        Pathwinder::FileInformationStructLayout::LayoutForFileInformationClass(
            fileInformationClass);
    if (false == maybeFileInformationStructLayout.has_value())
      TEST_FAILED_BECAUSE(
          L"%s: Attempting to enumerate a directory using unsupported file information class %zu.",
          __FUNCTIONW__,
          static_cast<size_t>(fileInformationClass));
    const auto& fileInformationStructLayout = *maybeFileInformationStructLayout;

    auto directoryEnumerationStateIter = inProgressDirectoryEnumerations.find(directoryHandle);
    if (inProgressDirectoryEnumerations.cend() == directoryEnumerationStateIter)
    {
      const auto directoryHandleIter = openDirectoryHandles.find(directoryHandle);
      if (openDirectoryHandles.cend() == directoryHandleIter)
        TEST_FAILED_BECAUSE(
            L"%s: Attempting to enumerate a directory using invalid directory handle %zu.",
            __FUNCTIONW__,
            reinterpret_cast<size_t>(directoryHandle));
      std::wstring_view directoryToEnumerate = directoryHandleIter->second;

      const auto directoryContentsIter = filesystemContents.find(directoryToEnumerate);
      if (filesystemContents.cend() == directoryContentsIter)
        TEST_FAILED_BECAUSE(
            L"%s: Internal implementation error due to failure to locate the directory contents for \"%.*s\" even though a valid open handle exists for it.",
            __FUNCTIONW__,
            static_cast<int>(directoryToEnumerate.length()),
            directoryToEnumerate.data());
      const auto& directoryContents = directoryContentsIter->second;

      auto createDirectoryEnumerationStateResult = inProgressDirectoryEnumerations.emplace(
          directoryHandle,
          SDirectoryEnumerationState{
              .filePattern = MakeFilePatternString(filePattern),
              .nextItemIterator = directoryContents.cbegin(),
              .beginIterator = directoryContents.cbegin(),
              .endIterator = directoryContents.cend()});
      if (false == createDirectoryEnumerationStateResult.second)
        TEST_FAILED_BECAUSE(
            "%s: Internal implementation error due to failure to create a new directory enumeration state object.",
            __FUNCTIONW__);

      directoryEnumerationStateIter = createDirectoryEnumerationStateResult.first;
    }

    if (queryFlags & SL_RESTART_SCAN)
    {
      directoryEnumerationStateIter->second.filePattern = MakeFilePatternString(filePattern);
      directoryEnumerationStateIter->second.nextItemIterator =
          directoryEnumerationStateIter->second.beginIterator;
    }

    const unsigned int maxElementsToWrite =
        ((queryFlags & SL_RETURN_SINGLE_ENTRY)
             ? 1
             : std::numeric_limits<unsigned int>::max());
    unsigned int numElementsWritten = 0;
    unsigned int bufferBytePosition = 0;
    void* lastElementWritten = nullptr;

    // Taking references to these iterators ensures they can be updated automatically each
    // iteration of the loop that does the enumeration itself.
    auto& nextItemIterator = directoryEnumerationStateIter->second.nextItemIterator;
    const auto& endIterator = directoryEnumerationStateIter->second.endIterator;

    std::wstring_view enumerationFilePattern = directoryEnumerationStateIter->second.filePattern;

    for (; (nextItemIterator != endIterator) && (numElementsWritten < maxElementsToWrite);
         ++nextItemIterator)
    {
      std::wstring_view currentFileName = nextItemIterator->first;
      if (false ==
          Pathwinder::Strings::FileNameMatchesPattern(currentFileName, enumerationFilePattern))
        continue;

      void* const currentBuffer =
          &reinterpret_cast<uint8_t*>(enumerationBuffer)[bufferBytePosition];
      const unsigned int currentBufferCapacity =
          enumerationBufferCapacityBytes - bufferBytePosition;

      unsigned int bytesNeededForCurrentElement =
          fileInformationStructLayout.HypotheticalSizeForFileNameLength(
              static_cast<unsigned int>(currentFileName.length() * sizeof(currentFileName[0])));
      if (bytesNeededForCurrentElement > currentBufferCapacity)
      {
        // Buffer overflow would occur if another element were written.
        // If no structures were written, then this is an error condition, and a
        // corresponding code needs to be returned.
        if (0 == numElementsWritten) return Pathwinder::NtStatus::kBufferTooSmall;

        break;
      }

      // For testing purposes, it is sufficient to fill the entire file information structure
      // space with a fake value and then overwrite the relevant fields.
      std::memset(currentBuffer, 0, bytesNeededForCurrentElement);
      fileInformationStructLayout.WriteFileName(
          currentBuffer, currentFileName, bytesNeededForCurrentElement);

      numElementsWritten += 1;
      bufferBytePosition += bytesNeededForCurrentElement;
      lastElementWritten = currentBuffer;
    }

    // If at the end of the enumeration loop there were no files enumerated then the reason is
    // that no files were found to be enumerated.
    if (0 == numElementsWritten) return Pathwinder::NtStatus::kNoMoreFiles;

    fileInformationStructLayout.ClearNextEntryOffset(lastElementWritten);
    return Pathwinder::NtStatus::kSuccess;
  }

  NTSTATUS MockFilesystemOperations::QuerySingleFileDirectoryInformation(
      std::wstring_view absoluteDirectoryPath,
      std::wstring_view fileName,
      FILE_INFORMATION_CLASS fileInformationClass,
      void* enumerationBuffer,
      unsigned int enumerationBufferCapacityBytes)
  {
    const auto maybeFileInformationStructLayout =
        Pathwinder::FileInformationStructLayout::LayoutForFileInformationClass(
            fileInformationClass);
    if (false == maybeFileInformationStructLayout.has_value())
      TEST_FAILED_BECAUSE(
          L"%s: Attempting to query for single-file directory information using unsupported file information class %zu.",
          __FUNCTIONW__,
          static_cast<size_t>(fileInformationClass));
    const auto& fileInformationStructLayout = *maybeFileInformationStructLayout;

    const auto directoryContentsIter = filesystemContents.find(absoluteDirectoryPath);
    if (filesystemContents.cend() == directoryContentsIter)
      return Pathwinder::NtStatus::kObjectNameNotFound;
    const auto& directoryContents = directoryContentsIter->second;

    if (directoryContents.cend() == directoryContents.find(fileName))
      return Pathwinder::NtStatus::kObjectNameNotFound;

    // For testing purposes, it is sufficient to fill the entire file information structure
    // space with a fake value and then overwrite the relevant fields.
    unsigned int bytesNeeded = fileInformationStructLayout.HypotheticalSizeForFileNameLength(
        static_cast<unsigned int>(fileName.length() * sizeof(fileName[0])));
    std::memset(enumerationBuffer, 0, bytesNeeded);
    fileInformationStructLayout.WriteFileName(enumerationBuffer, fileName, bytesNeeded);

    if (bytesNeeded > enumerationBufferCapacityBytes) return Pathwinder::NtStatus::kBufferTooSmall;

    return Pathwinder::NtStatus::kSuccess;
  }
} // namespace PathwinderTest

namespace Pathwinder
{
  namespace FilesystemOperations
  {
    using namespace ::PathwinderTest;

    // Invocations are forwarded to mock instance methods.

    void CloseHandle(HANDLE handle)
    {
      MOCK_FREE_FUNCTION_BODY(MockFilesystemOperations, CloseHandle, handle);
    }

    intptr_t CreateDirectoryHierarchy(std::wstring_view absoluteDirectoryPath)
    {
      MOCK_FREE_FUNCTION_BODY(
          MockFilesystemOperations, CreateDirectoryHierarchy, absoluteDirectoryPath);
    }

    bool Exists(std::wstring_view absolutePath)
    {
      MOCK_FREE_FUNCTION_BODY(MockFilesystemOperations, Exists, absolutePath);
    }

    bool IsDirectory(std::wstring_view absolutePath)
    {
      MOCK_FREE_FUNCTION_BODY(MockFilesystemOperations, IsDirectory, absolutePath);
    }

    Pathwinder::ValueOrError<HANDLE, NTSTATUS> OpenDirectoryForEnumeration(
        std::wstring_view absoluteDirectoryPath)
    {
      MOCK_FREE_FUNCTION_BODY(
          MockFilesystemOperations, OpenDirectoryForEnumeration, absoluteDirectoryPath);
    }

    NTSTATUS PartialEnumerateDirectoryContents(
        HANDLE directoryHandle,
        FILE_INFORMATION_CLASS fileInformationClass,
        void* enumerationBuffer,
        unsigned int enumerationBufferCapacityBytes,
        ULONG queryFlags,
        std::wstring_view filePattern)
    {
      MOCK_FREE_FUNCTION_BODY(
          MockFilesystemOperations,
          PartialEnumerateDirectoryContents,
          directoryHandle,
          fileInformationClass,
          enumerationBuffer,
          enumerationBufferCapacityBytes,
          queryFlags,
          filePattern);
    }

    NTSTATUS QuerySingleFileDirectoryInformation(
        std::wstring_view absoluteDirectoryPath,
        std::wstring_view fileName,
        FILE_INFORMATION_CLASS fileInformationClass,
        void* enumerationBuffer,
        unsigned int enumerationBufferCapacityBytes)
    {
      MOCK_FREE_FUNCTION_BODY(
          MockFilesystemOperations,
          QuerySingleFileDirectoryInformation,
          absoluteDirectoryPath,
          fileName,
          fileInformationClass,
          enumerationBuffer,
          enumerationBufferCapacityBytes);
    }
  } // namespace FilesystemOperations
} // namespace Pathwinder
