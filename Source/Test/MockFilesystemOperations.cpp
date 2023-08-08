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

#include "MockFilesystemOperations.h"

#include <cwctype>
#include <string>
#include <string_view>
#include <unordered_map>


namespace PathwinderTest
{
    // -------- INSTANCE METHODS --------------------------------------- //
    // See "MockFilesystemOperations.h" for documentation.

    void MockFilesystemOperations::AddFilesystemEntityInternal(std::wstring_view absolutePath, EFilesystemEntityType type, unsigned int sizeInBytes)
    {
        std::wstring_view currentPathView = absolutePath;

        size_t lastBackslashIndex = currentPathView.find_last_of(L'\\');

        switch (type)
        {
        case EFilesystemEntityType::File:
            do {
                if (std::wstring_view::npos == lastBackslashIndex)
                    TEST_FAILED_BECAUSE(L"%s: Missing '\\' in absolute path \"%.*s\" when adding a file to a fake filesystem.", __FUNCTIONW__, static_cast<int>(absolutePath.length()), absolutePath.data());
            } while (false);
            break;

        case EFilesystemEntityType::Directory:
            do {
                auto directoryIter = filesystemContents.find(currentPathView);
                if (filesystemContents.end() == directoryIter)
                    directoryIter = filesystemContents.insert({std::wstring(currentPathView), TDirectoryContents()}).first;
            } while (false);
            break;

        default:
            TEST_FAILED_BECAUSE(L"%s: Internal error: Unknown filesystem entity type when adding to a fake filesystem.", __FUNCTIONW__);
        }

        while (lastBackslashIndex != std::wstring_view::npos)
        {
            std::wstring_view directoryPart = currentPathView.substr(0, lastBackslashIndex);
            std::wstring_view filePart = currentPathView.substr(lastBackslashIndex + 1);

            auto directoryIter = filesystemContents.find(directoryPart);
            if (filesystemContents.end() == directoryIter)
                directoryIter = filesystemContents.insert({std::wstring(directoryPart), TDirectoryContents()}).first;

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

    intptr_t MockFilesystemOperations::CreateDirectoryHierarchy(std::wstring_view absoluteDirectoryPath)
    {
        TEST_FAILED_BECAUSE(L"%s: Unimplemented mock function called.", __FUNCTIONW__);
    }

    // --------

    bool MockFilesystemOperations::Exists(std::wstring_view absolutePath)
    {        
        size_t lastBackslashIndex = absolutePath.find_last_of(L'\\');
        if (std::wstring_view::npos == lastBackslashIndex)
            return false;

        std::wstring_view directoryPart = absolutePath.substr(0, lastBackslashIndex);
        std::wstring_view filePart = absolutePath.substr(lastBackslashIndex + 1);

        const auto directoryIter = filesystemContents.find(directoryPart);
        if (filesystemContents.cend() == directoryIter)
            return false;

        return (filePart.empty() || directoryIter->second.contains(filePart));
    }

    // --------

    bool MockFilesystemOperations::IsDirectory(std::wstring_view absolutePath)
    {
        return filesystemContents.contains(absolutePath);
    }
}


namespace Pathwinder
{
    namespace FilesystemOperations
    {
        using namespace ::PathwinderTest;


        // -------- MOCK FUNCTIONS ----------------------------------------- //
        // Invocations are forwarded to mock instance methods.

        intptr_t CreateDirectoryHierarchy(std::wstring_view absoluteDirectoryPath)
        {
            MOCK_FREE_FUNCTION_BODY(MockFilesystemOperations, CreateDirectoryHierarchy, absoluteDirectoryPath);
        }

        // --------

        bool Exists(std::wstring_view absolutePath)
        {
            MOCK_FREE_FUNCTION_BODY(MockFilesystemOperations, Exists, absolutePath);
        }

        // --------

        bool IsDirectory(std::wstring_view absolutePath)
        {
            MOCK_FREE_FUNCTION_BODY(MockFilesystemOperations, IsDirectory, absolutePath);
        }
    }
}
