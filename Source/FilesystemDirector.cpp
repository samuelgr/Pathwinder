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
#include "Strings.h"

#include <optional>
#include <string_view>


namespace Pathwinder
{
    // -------- CLASS METHODS ---------------------------------------------- //
    // See "FilesystemDirector.h" for documentation.

    FilesystemDirector& FilesystemDirector::Singleton(void)
    {
        static FilesystemDirector* const singleton = new FilesystemDirector;
        return *singleton;
    }


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

    const FilesystemRule* FilesystemDirector::SelectRuleForDirectoryEnumeration(std::wstring_view absoluteDirectoryPath) const
    {
        // For directory enumeration redirection an exact path match is needed.
        // Directory enumeration operates on handles to directories that have already been opened.
        // Therefore, to make it appear that the origin directory's contents are actually the target directory's contents, the actual enumeration needs to be redirected even though the open handle is for the origin directory.

        auto ruleNode = originDirectoryIndex.Find(absoluteDirectoryPath);
        if (nullptr == ruleNode)
            return nullptr;

        return ruleNode->GetData();
    }

    // --------

    std::optional<TemporaryString> FilesystemDirector::RedirectDirectoryEnumeration(std::wstring_view absoluteDirectoryPath) const
    {
        const std::wstring_view windowsNamespacePrefix = Strings::PathGetWindowsNamespacePrefix(absoluteDirectoryPath);
        const std::wstring_view absoluteDirectoryPathWithoutPrefix = absoluteDirectoryPath.substr(windowsNamespacePrefix.length());

        const FilesystemRule* const selectedRule = SelectRuleForDirectoryEnumeration(absoluteDirectoryPathWithoutPrefix);
        if (nullptr == selectedRule)
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"Directory redirection query for path \"%.*s\" did not match any rules.", static_cast<int>(absoluteDirectoryPath.length()), absoluteDirectoryPath.data());
            return std::nullopt;
        }

        std::optional<TemporaryString> maybeRedirectedDirectoryPath = selectedRule->RedirectPathOriginToTarget(absoluteDirectoryPathWithoutPrefix, L"", windowsNamespacePrefix);
        if (false == maybeRedirectedDirectoryPath.has_value())
        {
            Message::OutputFormatted(Message::ESeverity::Error, L"Directory redirection query for path \"%.*s\" matched rule \"%s\" but failed due to an internal error while synthesizing the redirect result.", static_cast<int>(absoluteDirectoryPath.length()), absoluteDirectoryPath.data(), selectedRule->GetName().data());
            return std::nullopt;
        }

        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"Directory redirection query for path \"%.*s\", matched rule \"%s\", and was redirected to \"%s\".", static_cast<int>(absoluteDirectoryPath.length()), absoluteDirectoryPath.data(), selectedRule->GetName().data(), maybeRedirectedDirectoryPath.value().AsCString());
        return std::move(maybeRedirectedDirectoryPath.value());
    }

    // --------

    std::optional<TemporaryString> FilesystemDirector::RedirectSingleFile(std::wstring_view filePath) const
    {
        const std::wstring_view windowsNamespacePrefix = Strings::PathGetWindowsNamespacePrefix(filePath);
        const unsigned int windowsNamespacePrefixLength = static_cast<unsigned int>(windowsNamespacePrefix.length());

        TemporaryString fileFullPath;

        unsigned int resolvedPathLength = 0;

        if (false == windowsNamespacePrefix.empty())
        {
            // The presence of a namespace prefix generally indicates that the path is already full and absolute.

            fileFullPath << filePath;
            resolvedPathLength = static_cast<unsigned int>(filePath.length()) - windowsNamespacePrefixLength;
        }
        else
        {
            // If there is no prefix at all then the path could be relative and needs to be resolved more fully.
            // Because the hooking implementation targets the Nt family of functions, which tend to receive absolute pathnames with a prefix, in general this branch will execute rarely if at all.
            // The input path is not guaranteed to be null-terminated, but the function that resolves the full path name expects null termination.

            TemporaryString nullTerminatedFilePath = filePath.substr(windowsNamespacePrefixLength);

            fileFullPath += windowsNamespacePrefix;

            wchar_t* const fileFullPathAfterPrefix = &fileFullPath[windowsNamespacePrefixLength];
            const unsigned int fileFullPathCapacityAfterPrefix = fileFullPath.Capacity() - windowsNamespacePrefixLength;

            resolvedPathLength = GetFullPathName(nullTerminatedFilePath.AsCString(), fileFullPathCapacityAfterPrefix, fileFullPathAfterPrefix, nullptr);
            fileFullPath.UnsafeSetSize(windowsNamespacePrefixLength + resolvedPathLength);
        }

        if (0 == resolvedPathLength)
        {
            Message::OutputFormatted(Message::ESeverity::Error, L"File redirection query for path \"%.*s\" failed to resolve full path: %s", static_cast<int>(filePath.length()), filePath.data(), Strings::SystemErrorCodeString(GetLastError()).AsCString());
            return std::nullopt;
        }

        std::wstring_view fileFullPathWithoutPrefix = std::wstring_view(&fileFullPath[windowsNamespacePrefixLength], resolvedPathLength);

        const size_t lastSeparatorPos = fileFullPathWithoutPrefix.find_last_of(L'\\');
        if (std::wstring_view::npos == lastSeparatorPos)
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File redirection query for path \"%.*s\" resolved to \"%s\" but does not contain a final path separator and was therefore skipped for redirection.", static_cast<int>(filePath.length()), filePath.data(), fileFullPath.AsCString());
            return std::nullopt;
        }

        const std::wstring_view directoryPart = fileFullPathWithoutPrefix.substr(0, lastSeparatorPos);
        const std::wstring_view filePart = ((L'\\' == fileFullPathWithoutPrefix.back()) ? L"" : fileFullPathWithoutPrefix.substr(1 + lastSeparatorPos));

        const FilesystemRule* const selectedRule = SelectRuleForSingleFile(directoryPart);
        if (nullptr == selectedRule)
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File redirection query for path \"%.*s\" resolved to \"%s\" but did not match any rules.", static_cast<int>(filePath.length()), filePath.data(), fileFullPath.AsCString());
            return std::nullopt;
        }

        std::optional<TemporaryString> maybeRedirectedFilePath = selectedRule->RedirectPathOriginToTarget(directoryPart, filePart, windowsNamespacePrefix);;
        if (false == maybeRedirectedFilePath.has_value())
        {
            Message::OutputFormatted(Message::ESeverity::Error, L"File redirection query for path \"%.*s\" resolved to \"%s\" and matched rule \"%s\" but failed due to an internal error while synthesizing the redirect result.", static_cast<int>(filePath.length()), filePath.data(), fileFullPath.AsCString(), selectedRule->GetName().data());
            return std::nullopt;
        }

        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File redirection query for path \"%.*s\" resolved to \"%s\", matched rule \"%s\", and was redirected to \"%s\".", static_cast<int>(filePath.length()), filePath.data(), fileFullPath.AsCString(), selectedRule->GetName().data(), maybeRedirectedFilePath.value().AsCString());
        return std::move(maybeRedirectedFilePath.value());
    }
}
