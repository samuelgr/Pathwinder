/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file FileInformationStruct.cpp
 *   Partial implementation of manipulation functionality for the various file information
 *   structures that Windows system calls use as output during directory enumeration operations.
 **************************************************************************************************/

#include "FileInformationStruct.h"

#include <cwchar>
#include <optional>
#include <unordered_map>
#include <utility>

#include "DebugAssert.h"

/// Produces a key-value pair definition for a file information class and the associatied
/// structure's layout information.
#define FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(structname)                       \
  {                                                                                                \
    structname::kFileInformationClass,                                                             \
        FileInformationStructLayout(                                                               \
            structname::kFileInformationClass,                                                     \
            sizeof(structname),                                                                    \
            offsetof(structname, nextEntryOffset),                                                 \
            offsetof(structname, fileNameLength),                                                  \
            offsetof(structname, fileName))                                                        \
  }

namespace Pathwinder
{
  std::optional<FileInformationStructLayout>
      FileInformationStructLayout::LayoutForFileInformationClass(
          FILE_INFORMATION_CLASS fileInformationClass)
  {
    static const std::unordered_map<FILE_INFORMATION_CLASS, FileInformationStructLayout>
        kFileInformationStructureLayouts = {
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileDirectoryInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileFullDirectoryInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileBothDirectoryInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(SFileNamesInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(
                SFileIdBothDirectoryInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(
                SFileIdFullDirectoryInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(
                SFileIdGlobalTxDirectoryInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(
                SFileIdExtdDirectoryInformation),
            FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(
                SFileIdExtdBothDirectoryInformation)};

    auto layoutIter = kFileInformationStructureLayouts.find(fileInformationClass);

    DebugAssert(
        layoutIter != kFileInformationStructureLayouts.cend(),
        "Unsupported file information class.");

    if (layoutIter == kFileInformationStructureLayouts.cend()) return std::nullopt;

    return layoutIter->second;
  }

  FileInformationStructLayout::TFileNameChar* FileInformationStructLayout::FileNamePointerInternal(
      const void* fileInformationStruct, unsigned int offsetOfFileName)
  {
    return reinterpret_cast<TFileNameChar*>(
        reinterpret_cast<size_t>(fileInformationStruct) + static_cast<size_t>(offsetOfFileName));
  }

  unsigned int FileInformationStructLayout::HypotheticalSizeForFileNameLengthInternal(
      unsigned int structureBaseSizeBytes,
      unsigned int offsetOfFileName,
      unsigned int fileNameLengthBytes)
  {
    return std::max(structureBaseSizeBytes, offsetOfFileName + fileNameLengthBytes);
  }

  std::wstring_view FileInformationStructLayout::ReadFileNameInternal(
      const void* fileInformationStruct,
      unsigned int offsetOfFileName,
      unsigned int offsetOfFileNameLength)
  {
    return std::wstring_view(
        FileNamePointerInternal(fileInformationStruct, offsetOfFileName),
        (ReadFileNameLengthInternal(fileInformationStruct, offsetOfFileNameLength) /
         sizeof(TFileNameChar)));
  }

  FileInformationStructLayout::TFileNameLength
      FileInformationStructLayout::ReadFileNameLengthInternal(
          const void* fileInformationStruct, unsigned int offsetOfFileNameLength)
  {
    return *reinterpret_cast<TFileNameLength*>(
        reinterpret_cast<size_t>(fileInformationStruct) +
        static_cast<size_t>(offsetOfFileNameLength));
  }

  void FileInformationStructLayout::WriteFileNameInternal(
      void* fileInformationStruct,
      std::basic_string_view<TFileNameChar> newFileName,
      unsigned int bufferCapacityBytes,
      unsigned int offsetOfFileName,
      unsigned int offsetOfFileNameLength)
  {
    wchar_t* const filenameBuffer =
        FileNamePointerInternal(fileInformationStruct, offsetOfFileName);
    const unsigned int filenameBufferCapacityChars =
        (bufferCapacityBytes - offsetOfFileName) / static_cast<unsigned int>(sizeof(TFileNameChar));

    const size_t filenameNumberOfCharsToWrite =
        std::min(static_cast<size_t>(filenameBufferCapacityChars), newFileName.length());

    std::wmemcpy(filenameBuffer, newFileName.data(), filenameNumberOfCharsToWrite);
    WriteFileNameLengthInternal(
        fileInformationStruct,
        static_cast<TFileNameLength>(filenameNumberOfCharsToWrite * sizeof(TFileNameChar)),
        offsetOfFileNameLength);
  }

  void FileInformationStructLayout::WriteFileNameLengthInternal(
      void* fileInformationStruct,
      TFileNameLength newFileNameLength,
      unsigned int offsetOfFileNameLength)
  {
    *(reinterpret_cast<TFileNameLength*>(
        reinterpret_cast<size_t>(fileInformationStruct) +
        static_cast<size_t>(offsetOfFileNameLength))) = newFileNameLength;
  }
} // namespace Pathwinder
