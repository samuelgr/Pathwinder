/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FileInformationStruct.cpp
 *   Partial implementation of manipulation functionality for the various file
 *   file information structures that Windows system calls use as output
 *   during directory enumeration operations.
 *****************************************************************************/

#include "DebugAssert.h"
#include "FileInformationStruct.h"

#include <optional>
#include <unordered_map>


// -------- MACROS --------------------------------------------------------- //

/// Produces a key-value pair definition for a file information class and the associatied structure's layout information.
#define FILE_INFORMATION_LAYOUT_STRUCT_KEY_VALUE_PAIR_DEFINITION(structname) \
    {structname::kFileInformationClass, FileInformationStructLayout(sizeof(structname), offsetof(structname, nextEntryOffset), offsetof(structname, fileNameLength), offsetof(structname, fileName))}


namespace Pathwinder
{
    // -------- CLASS METHODS ---------------------------------------------- //
    // See "FileInformationStruct.h" for documentation.

    std::optional<FileInformationStructLayout> FileInformationStructLayout::LayoutForFileInformationClass(FILE_INFORMATION_CLASS fileInformationClass)
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
}
