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
#include "FilesystemInstruction.h"
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

    const FilesystemRule* FilesystemDirector::SelectRuleForRedirectionQuery(std::wstring_view absolutePath) const
    {
        // It is possible that multiple rules all have a prefix that matches the directory part of the full file path.
        // We want to pick the most specific one to apply, meaning it has the longest matching prefix.
        // For example, suppose two rules exist with "C:\Dir1\Dir2" and "C:\Dir1" as their respective origin directories.
        // A file having full path "C:\Dir1\Dir2\textfile.txt" would need to use "C:\Dir1\Dir2" even though technically both rules do match.

        auto ruleNode = originDirectoryIndex.LongestMatchingPrefix(absolutePath);
        if (nullptr == ruleNode)
            return nullptr;

        return ruleNode->GetData();
    }

    // --------

    FileOperationRedirectInstruction FilesystemDirector::RedirectFileOperation(std::wstring_view absoluteFilePath) const
    {
        const std::wstring_view windowsNamespacePrefix = Strings::PathGetWindowsNamespacePrefix(absoluteFilePath);
        const std::wstring_view absoluteFilePathWithoutPrefix = absoluteFilePath.substr(windowsNamespacePrefix.length());

        const size_t lastSeparatorPos = absoluteFilePathWithoutPrefix.find_last_of(L'\\');
        if (std::wstring_view::npos == lastSeparatorPos)
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File operation redirection query for path \"%.*s\" does not contain a final path separator and was therefore skipped for redirection.", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data());
            return FileOperationRedirectInstruction::DoNotRedirectFrom(absoluteFilePath);
        }

        const std::wstring_view directoryPart = absoluteFilePathWithoutPrefix.substr(0, lastSeparatorPos);
        const std::wstring_view filePart = ((L'\\' == absoluteFilePathWithoutPrefix.back()) ? L"" : absoluteFilePathWithoutPrefix.substr(1 + lastSeparatorPos));

        const FilesystemRule* const selectedRule = SelectRuleForRedirectionQuery(directoryPart);
        if (nullptr == selectedRule)
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File operation redirection query for path \"%.*s\" did not match any rules.", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data());
            return FileOperationRedirectInstruction::DoNotRedirectFrom(absoluteFilePath);
        }

        std::optional<TemporaryString> maybeRedirectedFilePath = selectedRule->RedirectPathOriginToTarget(directoryPart, filePart, windowsNamespacePrefix);;
        if (false == maybeRedirectedFilePath.has_value())
        {
            Message::OutputFormatted(Message::ESeverity::Error, L"File operation redirection query for path \"%.*s\" matched rule \"%s\" but failed due to an internal error while synthesizing the redirect result.", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data(), selectedRule->GetName().data());
            return FileOperationRedirectInstruction::DoNotRedirectFrom(absoluteFilePath);
        }

        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File operation redirection query for path \"%.*s\" matched rule \"%s\", and was redirected to \"%s\".", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data(), selectedRule->GetName().data(), maybeRedirectedFilePath.value().AsCString());

        std::wstring_view redirectedFilePath = maybeRedirectedFilePath.value().AsStringView();
        return FileOperationRedirectInstruction::RedirectAndTryInOrder({redirectedFilePath}, *selectedRule, std::move(maybeRedirectedFilePath.value()));
    }
}
