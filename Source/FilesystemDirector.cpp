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
    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Traverses a prefix index to select a filesystem rule for the given full file path.
    /// This operation is useful for those filesystem functions that directly operate on a single absolute path.
    /// @param [in] fileFullPathLowerCase Full path of the file being queried for possible redirection. Must be null-terminated and all lower-case.
    /// @return Pointer to the rule object that should be used to process the redirection, or `nullptr` if no redirection should occur at all.
    static const FilesystemRule* SelectRuleForSingleFileInternal(const PrefixIndex<wchar_t, FilesystemRule>& prefixIndexToSearch, std::wstring_view fileFullPathLowerCase)
    {
        // It is possible that multiple rules all have a prefix that matches the directory part of the full file path.
        // We want to pick the most specific one to apply, meaning it has the longest matching prefix.
        // For example, suppose two rules exist with "C:\Dir1\Dir2" and "C:\Dir1" as their respective origin directories.
        // A file having full path "C:\Dir1\Dir2\textfile.txt" would need to use "C:\Dir1\Dir2" even though technically both rules do match.

        auto ruleNode = prefixIndexToSearch.LongestMatchingPrefix(fileFullPathLowerCase);
        if (nullptr == ruleNode)
            return nullptr;

        return ruleNode->GetData();
    }


    // -------- CLASS METHODS ---------------------------------------------- //
    // See "FilesystemDirector.h" for documentation.

    FilesystemDirector& FilesystemDirector::Singleton(void)
    {
        static FilesystemDirector singleton;
        return singleton;
    }


    // -------- INSTANCE METHODS ------------------------------------------- //
    // See "FilesystemDirector.h" for documentation.

    const FilesystemRule* FilesystemDirector::SelectRuleForSingleFile(std::wstring_view fileFullPath) const
    {
        return SelectRuleForSingleFileInternal(originDirectoryIndex, Strings::ToLowercase(fileFullPath));
    }

    // --------

    std::optional<TemporaryString> FilesystemDirector::RedirectSingleFile(std::wstring_view filePath) const
    {
        const std::wstring_view windowsNamespacePrefix = Strings::PathGetWindowsNamespacePrefix(filePath);
        const unsigned int windowsNamespacePrefixLength = static_cast<unsigned int>(windowsNamespacePrefix.length());

        TemporaryString fileFullPath;

        unsigned int resolvedPathLength = 0;

        if (L"\\\\?\\" == windowsNamespacePrefix)
        {
            // The prefix "\\?\" is an instruction to disable all string processing on the associated path.
            // Input is therefore already a full and absolute path and does not need to be resolved.
            // See https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file for more information.

            fileFullPath << filePath;
            resolvedPathLength = static_cast<unsigned int>(filePath.length()) - windowsNamespacePrefixLength;
        }
        else
        {
            // Any other prefix, or no prefix at all, needs to be preserved but skipped during resolution and redirection.
            // Otherwise it might be interpreted as a relative path from the current drive.

            fileFullPath += windowsNamespacePrefix;
            filePath.remove_prefix(windowsNamespacePrefixLength);

            wchar_t* const fileFullPathAfterPrefix = &fileFullPath[windowsNamespacePrefixLength];
            const unsigned int fileFullPathCapacityAfterPrefix = fileFullPath.Capacity() - windowsNamespacePrefixLength;

            resolvedPathLength = GetFullPathName(filePath.data(), fileFullPathCapacityAfterPrefix, fileFullPathAfterPrefix, nullptr);
            fileFullPath.UnsafeSetSize(windowsNamespacePrefixLength + resolvedPathLength);
        }

        if (0 == resolvedPathLength)
        {
            Message::OutputFormatted(Message::ESeverity::Error, L"Filesystem redirection query for path \"%.*s\" failed to resolve full path: %s", (int)filePath.length(), filePath.data(), Strings::SystemErrorCodeString(GetLastError()).AsCString());
            return std::nullopt;
        }

        fileFullPath.ToLowercase();

        std::wstring_view fileFullPathWithoutPrefix = std::wstring_view(&fileFullPath[windowsNamespacePrefixLength], resolvedPathLength);

        const FilesystemRule* const selectedRule = SelectRuleForSingleFileInternal(originDirectoryIndex, fileFullPathWithoutPrefix);
        if (nullptr == selectedRule)
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"Filesystem redirection query for path \"%.*s\" resolved to \"%s\" but did not match any rules.", (int)filePath.length(), filePath.data(), fileFullPath.AsCString());
            return std::nullopt;
        }

        const size_t lastSeparatorPos = fileFullPathWithoutPrefix.find_last_of(L'\\');
        if (std::wstring_view::npos == lastSeparatorPos)
        {
            Message::OutputFormatted(Message::ESeverity::Error, L"Filesystem redirection query for path \"%.*s\" resolved to \"%s\" and matched rule \"%s\" but failed due to an internal error while finding the last path separator.", (int)filePath.length(), filePath.data(), fileFullPath.AsCString(), selectedRule->GetName().data());
            return std::nullopt;
        }

        const std::wstring_view directoryPart = fileFullPathWithoutPrefix.substr(0, lastSeparatorPos);
        const std::wstring_view filePart = ((L'\\' == fileFullPathWithoutPrefix.back()) ? L"" : fileFullPathWithoutPrefix.substr(1 + lastSeparatorPos));

        std::optional<TemporaryString> maybeRedirectedFilePath;

        switch (selectedRule->DirectoryCompareWithOrigin(directoryPart))
        {
        case FilesystemRule::EDirectoryCompareResult::CandidateIsParent:

            // Redirecton query is for the origin directory itself.
            // There is no file part, so the redirection will occur to the target directory itself.

            maybeRedirectedFilePath = selectedRule->RedirectPathOriginToTarget(fileFullPath, L"");
            break;

        default:

            // Redirection query prefix-matched the origin directory.
            // There is a file part present.

            maybeRedirectedFilePath = selectedRule->RedirectPathOriginToTarget(directoryPart, filePart, windowsNamespacePrefix);
            break;
        }
        
        if (false == maybeRedirectedFilePath.has_value())
        {
            Message::OutputFormatted(Message::ESeverity::Error, L"Filesystem redirection query for path \"%.*s\" resolved to \"%s\" and matched rule \"%s\" but failed due to an internal error while synthesizing the redirect result.", (int)filePath.length(), filePath.data(), fileFullPath.AsCString(), selectedRule->GetName().data());
            return std::nullopt;
        }

        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"Filesystem redirection query for path \"%s\" resolved to \"%s\", matched rule \"%s\", and was redirected to \"%s\".", filePath.data(), fileFullPath.AsCString(), selectedRule->GetName().data(), maybeRedirectedFilePath.value().AsCString());
        return std::move(maybeRedirectedFilePath.value());
    }
}
