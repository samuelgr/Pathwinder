/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemDirector.cpp
 *   Implementation of filesystem manipulation and application functionality.
 *****************************************************************************/

#include "ApiWindows.h"
#include "DebugAssert.h"
#include "FilesystemDirector.h"
#include "FilesystemRule.h"
#include "Message.h"
#include "PrefixIndex.h"

#include <optional>
#include <string_view>


namespace Pathwinder
{
    // -------- INSTANCE METHODS ------------------------------------------- //
    // See "FilesystemDirector.h" for documentation.

    const FilesystemRule* FilesystemDirector::SelectRuleForSingleFile(std::wstring_view fileFullPath) const
    {
        // It is possible that multiple rules all have a prefix that matches the directory part of the full file path.
        // We want to pick the most specific one to apply, meaning it has the longest matching prefix.
        // For example, suppose two rules exist with "C:\Dir1\Dir2" and "C:\Dir1" as their respective origin directories.
        // A file having full path "C:\Dir1\Dir2\textfile.txt" would need to use "C:\Dir1\Dir2" even though technically both rules do match.

        auto ruleNode = originDirectoryIndex.LongestMatchingPrefix(fileFullPath);
        if (nullptr == ruleNode)
            return nullptr;

        return ruleNode->GetData();
    }

    // --------

    TemporaryString FilesystemDirector::RedirectSingleFile(std::wstring_view filePath) const
    {
        TemporaryString fileFullPath;
        fileFullPath.UnsafeSetSize(GetFullPathName(filePath.data(), fileFullPath.Capacity(), fileFullPath.Data(), nullptr));
        if (true == fileFullPath.Empty())
        {
            fileFullPath = filePath;
            Message::OutputFormatted(Message::ESeverity::Error, L"Filesystem redirection query for path \"%s\" failed to resolve full path: %s", fileFullPath.AsCString(), Strings::SystemErrorCodeString(GetLastError()).AsCString());
            return fileFullPath;
        }

        const FilesystemRule* const selectedRule = SelectRuleForSingleFile(fileFullPath);
        if (nullptr == selectedRule)
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"Filesystem redirection query for path \"%s\" resolved to \"%s\" but did not match any rules.", filePath.data(), fileFullPath.AsCString());
            return fileFullPath;
        }

        const size_t lastSeparatorPos = fileFullPath.AsStringView().find_last_of(L'\\');
        if (std::wstring_view::npos == lastSeparatorPos)
        {
            Message::OutputFormatted(Message::ESeverity::Error, L"Filesystem redirection query for path \"%s\" resolved to \"%s\" and matched rule \"%s\" but failed due to an internal error while finding the last path separator.", filePath.data(), fileFullPath.AsCString(), selectedRule->GetName().data());
            return fileFullPath;
        }

        const std::wstring_view directoryPart = fileFullPath.AsStringView().substr(0, lastSeparatorPos);
        const std::wstring_view filePart = ((L'\\' == fileFullPath.AsStringView().back()) ? L"" : fileFullPath.AsStringView().substr(1 + lastSeparatorPos));

        std::optional<TemporaryString> maybeRedirectedFilePath = selectedRule->RedirectPathOriginToTarget(directoryPart, filePart);
        if (false == maybeRedirectedFilePath.has_value())
        {
            Message::OutputFormatted(Message::ESeverity::Error, L"Filesystem redirection query for path \"%s\" resolved to \"%s\" and matched rule \"%s\" but failed due to an internal error while synthesizing the redirect result.", filePath.data(), fileFullPath.AsCString(), selectedRule->GetName().data());
            return fileFullPath;
        }

        Message::OutputFormatted(Message::ESeverity::Debug, L"Filesystem redirection query for path \"%s\" resolved to \"%s\", matched rule \"%s\", and was redirected to \"%s\".", filePath.data(), fileFullPath.AsCString(), selectedRule->GetName().data(), maybeRedirectedFilePath.value().AsCString());
        return std::move(maybeRedirectedFilePath.value());
    }
}
