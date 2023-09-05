/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file FileInformationStruct.cpp
 *   Partial implementation of manipulation functionality for the various file information
 *   structures that Windows system calls use as output during directory enumeration operations.
 **************************************************************************************************/

#include "FileInformationStruct.h"

#include <optional>
#include <unordered_map>

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
} // namespace Pathwinder
