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
#include "FilesystemOperations.h"
#include "FilesystemRule.h"
#include "Message.h"
#include "PrefixIndex.h"
#include "Strings.h"

#include <optional>
#include <string_view>


namespace Pathwinder
{
    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Determines if the specified file operation mode can result in a new file being created.
    /// @param [in] fileOperationMode File operation mode to check.
    /// @return `true` if the file operation can result in a new file being created, `false` otherwise.
    static inline bool CanFileOperationResultInFileCreation(FilesystemDirector::EFileOperationMode fileOperationMode)
    {
        switch (fileOperationMode)
        {
        case FilesystemDirector::EFileOperationMode::CreateNewFile:
        case FilesystemDirector::EFileOperationMode::CreateNewOrOpenExistingFile:
            return true;

        default:
            return false;
        }
    }


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

    FileOperationRedirectInstruction FilesystemDirector::RedirectFileOperation(std::wstring_view absoluteFilePath, EFileOperationMode fileOperationMode) const
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

        std::wstring_view unredirectedPathDirectoryPart;
        std::wstring_view unredirectedPathDirectoryPartWithWindowsNamespacePrefix;
        std::wstring_view unredirectedPathFilePart;
        std::optional<TemporaryString> maybeRedirectedFilePath;

        if (FilesystemRule::EDirectoryCompareResult::Equal == selectedRule->DirectoryCompareWithOrigin(absoluteFilePathTrimmedForQuery))
        {
            // If the input path is exactly equal to the origin directory of the filesystem rule, then the entire input path is one big directory path, and the file part does not exist.
            unredirectedPathDirectoryPart = absoluteFilePathTrimmedForQuery;
            unredirectedPathDirectoryPartWithWindowsNamespacePrefix = absoluteFilePath.substr(0, windowsNamespacePrefix.length() + absoluteFilePathTrimmedForQuery.length());

            maybeRedirectedFilePath = selectedRule->RedirectPathOriginToTarget(unredirectedPathDirectoryPart, unredirectedPathFilePart, windowsNamespacePrefix, extraSuffix);
            if (false == maybeRedirectedFilePath.has_value())
            {
                Message::OutputFormatted(Message::ESeverity::Error, L"File operation redirection query for path \"%.*s\" did not match rule \"%s\" due to an internal error.", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data(), selectedRule->GetName().data());
                return FileOperationRedirectInstruction::NoRedirectionOrInterception();
            }

            Message::OutputFormatted(Message::ESeverity::Info, L"File operation redirection query for path \"%.*s\" is for the origin directory of rule \"%s\" and was redirected to \"%s\".", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data(), selectedRule->GetName().data(), maybeRedirectedFilePath.value().AsCString());
        }
        else
        {
            // If the input path is something else, then it is safe to split it at the last path separator into a directory part and a file part.
            unredirectedPathDirectoryPart = absoluteFilePathTrimmedForQuery.substr(0, lastSeparatorPos);
            unredirectedPathDirectoryPartWithWindowsNamespacePrefix = absoluteFilePath.substr(0, windowsNamespacePrefix.length() + lastSeparatorPos);
            unredirectedPathFilePart = absoluteFilePathTrimmedForQuery.substr(1 + lastSeparatorPos);

            maybeRedirectedFilePath = selectedRule->RedirectPathOriginToTarget(unredirectedPathDirectoryPart, unredirectedPathFilePart, windowsNamespacePrefix, extraSuffix);
            if (false == maybeRedirectedFilePath.has_value())
            {
                Message::OutputFormatted(Message::ESeverity::Info, L"File operation redirection query for path \"%.*s\" did not match rule \"%s\" because a file pattern put it out of the rule's scope.", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data(), selectedRule->GetName().data());
                return FileOperationRedirectInstruction::NoRedirectionOrInterception();
            }

            Message::OutputFormatted(Message::ESeverity::Info, L"File operation redirection query for path \"%.*s\" matched rule \"%s\" and was redirected to \"%s\".", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data(), selectedRule->GetName().data(), maybeRedirectedFilePath.value().AsCString());
        }

        std::wstring_view redirectedFilePath = maybeRedirectedFilePath.value().AsStringView();

        BitSetEnum<FileOperationRedirectInstruction::EExtraPreOperation> extraPreOperations;
        std::wstring_view extraPreOperationOperand;

        if (true == CanFileOperationResultInFileCreation(fileOperationMode))
        {
            // If the filesystem operation can result in file creation, then it must be possible to complete file creation in the target hierarchy if it would also be possible to do so in the origin hierarchy.
            // In this situation it is necessary to ensure that the target-side hierarchy exists up to the directory containing the file that is to be potentially created, if said hierarchy also exists on the origin side.

            if (FilesystemOperations::IsDirectory(unredirectedPathDirectoryPartWithWindowsNamespacePrefix))
            {
                extraPreOperations.insert(static_cast<int>(FileOperationRedirectInstruction::EExtraPreOperation::EnsurePathHierarchyExists));
                extraPreOperationOperand = Strings::RemoveTrailing(redirectedFilePath.substr(0, redirectedFilePath.find_last_of(L'\\')), L'\\');
            }
        }
        else
        {
            // If the filesystem operation cannot result in file creation, then it is possible that the operation is targeting a directory that exists in the origin hierarchy.
            // In this situation it is necessary to ensure that the same directory also exists in the target hierarchy.

            if (FilesystemOperations::IsDirectory(absoluteFilePath))
            {
                extraPreOperations.insert(static_cast<int>(FileOperationRedirectInstruction::EExtraPreOperation::EnsurePathHierarchyExists));
                extraPreOperationOperand = Strings::RemoveTrailing(redirectedFilePath, L'\\');
            }
        }

        return FileOperationRedirectInstruction::RedirectTo(std::move(maybeRedirectedFilePath.value()), FileOperationRedirectInstruction::EAssociateNameWithHandle::Unredirected, std::move(extraPreOperations), extraPreOperationOperand);
    }
}
