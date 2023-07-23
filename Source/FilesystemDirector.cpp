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
        const std::wstring_view extraSuffix = ((true == absoluteFilePath.ends_with(L'\\')) ? L"\\" : L"");
        const std::wstring_view absoluteFilePathTrimmedForQuery = Strings::RemoveTrailing(absoluteFilePath.substr(windowsNamespacePrefix.length()), L'\\');

        const size_t lastSeparatorPos = absoluteFilePathTrimmedForQuery.find_last_of(L'\\');
        if (std::wstring_view::npos == lastSeparatorPos)
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File operation redirection query for path \"%.*s\" does not contain a final path separator and was therefore skipped for redirection.", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data());
            return FileOperationRedirectInstruction::NoRedirectionOrInterception();
        }

        const std::wstring_view directoryPart = absoluteFilePathTrimmedForQuery.substr(0, lastSeparatorPos);
        const std::wstring_view filePart = absoluteFilePathTrimmedForQuery.substr(1 + lastSeparatorPos);

        const FilesystemRule* const selectedRule = SelectRuleForRedirectionQuery(absoluteFilePathTrimmedForQuery);
        if (nullptr == selectedRule)
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File operation redirection query for path \"%.*s\" did not match any rules.", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data());

            if (true == IsPrefixForAnyRule(absoluteFilePathTrimmedForQuery))
            {
                // If the file path could possibly be a directory path that but exists in the hierarchy as an ancestor of filesystem rules, then it is possible this same path could be a relative root path later on for a something that needs to be redirected.
                // Therefore, if a file handle is being created it needs to be associated with the unredirected path.
                return FileOperationRedirectInstruction::InterceptWithoutRedirection(FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected);
            }
            else
            {
                // Otherwise, an unredirected file path is not interesting and can be safely passed to the system without any further processing.
                // The path specified is totally unrelated to all filesystem rules.
                return FileOperationRedirectInstruction::NoRedirectionOrInterception();
            }
        }

        std::optional<TemporaryString> maybeRedirectedFilePath = std::nullopt;
        FileOperationRedirectInstruction::EAssociateNameWithHandle filenameHandleAssociation = FileOperationRedirectInstruction::EAssociateNameWithHandle::None;
        BitSetEnum<FileOperationRedirectInstruction::EExtraPreOperation> extraPreOperations;
        std::wstring_view extraPreOperationOperand;

        if (FilesystemRule::EDirectoryCompareResult::Equal == selectedRule->DirectoryCompareWithOrigin(absoluteFilePathTrimmedForQuery))
        {
            // TODO: Depending on the operation, ensure that either the actual target directory exists or just up to its parent.

            maybeRedirectedFilePath = selectedRule->RedirectPathOriginToTarget(absoluteFilePathTrimmedForQuery, L"", windowsNamespacePrefix, extraSuffix);
            filenameHandleAssociation = FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected;
            extraPreOperations.insert(static_cast<int>(FileOperationRedirectInstruction::EExtraPreOperation::EnsurePathHierarchyExists));
            extraPreOperationOperand = selectedRule->GetTargetDirectoryFullPath();
        }
        else
        {
            maybeRedirectedFilePath = selectedRule->RedirectPathOriginToTarget(directoryPart, filePart, windowsNamespacePrefix, extraSuffix);
            filenameHandleAssociation = FileOperationRedirectInstruction::EAssociateNameWithHandle::Redirected;
        }

        if (false == maybeRedirectedFilePath.has_value())
        {
            Message::OutputFormatted(Message::ESeverity::Error, L"File operation redirection query for path \"%.*s\" matched rule \"%s\" but failed due to an internal error while synthesizing the redirect result.", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data(), selectedRule->GetName().data());
            return FileOperationRedirectInstruction::NoRedirectionOrInterception();
        }

        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File operation redirection query for path \"%.*s\" matched rule \"%s\", and was redirected to \"%s\".", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data(), selectedRule->GetName().data(), maybeRedirectedFilePath.value().AsCString());
        return FileOperationRedirectInstruction::RedirectTo(std::move(maybeRedirectedFilePath.value()), filenameHandleAssociation, std::move(extraPreOperations), extraPreOperationOperand);
    }
}
