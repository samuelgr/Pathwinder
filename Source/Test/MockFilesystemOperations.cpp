/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file MockFilesystemOperations.cpp
 *   Implementation of controlled fake filesystem operations that can be used
 *   for testing.
 *****************************************************************************/

#pragma once

#include "MockFilesystemOperations.h"

#include <cwctype>
#include <string>
#include <string_view>


namespace PathwinderTest
{
    // -------- INTERNAL FUNCTIONS ------------------------------------- //

    /// Converts a string to lowercase.
    /// Intended as a step for converting paths before being used with a mock filesystem so that comparisons are all case-insensitive.
    /// @param [in] str String to convert.
    /// @return Input string converted to lowercase.
    static std::wstring ConvertToLowerCase(std::wstring_view str)
    {
        std::wstring convertedStr;
        convertedStr.reserve(1 + str.length());

        for (auto c : str)
            convertedStr.append(1, std::towlower(c));

        return convertedStr;
    }


    // -------- INSTANCE METHODS --------------------------------------- //
    // See "MockFilesystemOperations.h" for documentation.

    void MockFilesystemOperations::AddFilesystemEntityInternal(std::wstring_view absolutePath, EFilesystemEntityType type, unsigned int sizeInBytes)
    {
        const std::wstring absolutePathLowerCase = ConvertToLowerCase(absolutePath);
        std::wstring_view currentPathView = absolutePathLowerCase;

        size_t lastBackslashIndex = currentPathView.find_last_of(L'\\');
        if (std::wstring_view::npos == lastBackslashIndex)
            TEST_FAILED_BECAUSE(L"%s: Missing '\\' in absolute path \"%s\" when adding to a fake filesystem.", __FUNCTIONW__, absolutePathLowerCase.c_str());

        while (lastBackslashIndex != std::wstring_view::npos)
        {
            std::wstring_view directoryPart = currentPathView.substr(0, lastBackslashIndex);
            std::wstring_view filePart = currentPathView.substr(lastBackslashIndex + 1);

            auto directoryIter = filesystemContents.find(directoryPart);
            if (filesystemContents.end() == directoryIter)
                directoryIter = filesystemContents.insert({std::wstring(directoryPart), std::map<std::wstring, SFilesystemEntity, std::less<>>()}).first;

            directoryIter->second.insert({std::wstring(filePart), {.type = type, .sizeInBytes = sizeInBytes}});

            // Only the first thing that is inserted could possibly be a file, all the rest are intermediate directories along the path.
            type = EFilesystemEntityType::Directory;
            sizeInBytes = 0;

            // Continue working backwards through all parent directories and adding them as they are identified.
            currentPathView = directoryPart;
            lastBackslashIndex = currentPathView.find_last_of(L'\\');
        }
    }


    // -------- MOCK INSTANCE METHODS -------------------------------------- //
    // See "FilesystemOperations.h" for documentation.
    
    bool MockFilesystemOperations::Exists(const wchar_t* path)
    {
        const std::wstring pathLowerCase = ConvertToLowerCase(path);
        const std::wstring_view pathLowerCaseView = pathLowerCase;

        size_t lastBackslashIndex = pathLowerCaseView.find_last_of(L'\\');
        if (std::wstring_view::npos == lastBackslashIndex)
            return false;

        std::wstring_view directoryPart = pathLowerCaseView.substr(0, lastBackslashIndex);
        std::wstring_view filePart = pathLowerCaseView.substr(lastBackslashIndex + 1);

        const auto directoryIter = filesystemContents.find(directoryPart);
        if (filesystemContents.cend() == directoryIter)
            return false;

        return (filePart.empty() || directoryIter->second.contains(filePart));
    }

    // --------

    bool MockFilesystemOperations::IsDirectory(const wchar_t* path)
    {
        return filesystemContents.contains(ConvertToLowerCase(path));
    }
}


namespace Pathwinder
{
    namespace FilesystemOperations
    {
        using namespace ::PathwinderTest;


        // -------- MOCK FUNCTIONS ----------------------------------------- //
        // Invocations are forwarded to mock instance methods.

        bool Exists(const wchar_t* path)
        {
            MOCK_FREE_FUNCTION_BODY(MockFilesystemOperations, Exists, path);
        }

        // --------

        bool IsDirectory(const wchar_t* path)
        {
            MOCK_FREE_FUNCTION_BODY(MockFilesystemOperations, IsDirectory, path);
        }
    }
}
