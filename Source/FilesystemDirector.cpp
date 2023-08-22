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

#include "ApiWindowsInternal.h"
#include "DebugAssert.h"
#include "FilesystemDirector.h"
#include "FilesystemInstruction.h"
#include "FilesystemOperations.h"
#include "FilesystemRule.h"
#include "Message.h"
#include "PrefixIndex.h"
#include "Strings.h"

#include <algorithm>
#include <cwctype>
#include <optional>
#include <string_view>


namespace Pathwinder
{
    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Determines if the specified absolute path begins with a drive letter.
    /// @param [in] absolutePathWithoutWindowsPrefix Absolute path to check, without any Windows namespace prefixes.
    /// @return `true` if the path begins with a drive letter, `false` otherwise.
    static inline bool PathBeginsWithDriveLetter(std::wstring_view absolutePathWithoutWindowsPrefix)
    {
        if (absolutePathWithoutWindowsPrefix.length() < 3)
            return false;

        if ((0 != std::iswalpha(absolutePathWithoutWindowsPrefix[0])) && (L':' == absolutePathWithoutWindowsPrefix[1]) && (L'\\' == absolutePathWithoutWindowsPrefix[2]))
            return true;

        return false;
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

    const FilesystemRule* FilesystemDirector::SelectRuleForPath(std::wstring_view absolutePath) const
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

    DirectoryEnumerationInstruction FilesystemDirector::GetInstructionForDirectoryEnumeration(std::wstring_view associatedPath, std::wstring_view realOpenedPath) const
    {
        associatedPath = Strings::RemoveTrailing(associatedPath, L'\\');
        realOpenedPath = Strings::RemoveTrailing(realOpenedPath, L'\\');

        // This method's implementation takes advantage of, and depends on, the design and implementation of another method for redirecting file operations.
        // If this method is queried for a directory enumeration, then that means a previous file operation resulted in a directory handle being opened using either an unredirected or a redirected path and associated with an unredirected path.
        // There are three parts to a complete directory enumeration operation:
        // (1) Enumerate the contents of the redirected directory path, potentially subject to file pattern matching with a filesystem rule. This captures all of the files in scope of the filesystem rule that exist on the target side.
        // (2) Enumerate the contents of the unredirected directory path, for anything that exists on the origin side but is beyond the scope of the filesystem rule.
        // (3) Insert specific subdirectories into the enumeration results provided back to the application. This captures filesystem rules whose origin directories do not actually exist in the real filesystem on the origin side.
        // Parts 1 and 2 are only interesting if a redirection took place. Otherwise there is no reason to merge directory contents on the origin side with directory contents on the target side.
        // Part 3 is always potentially interesting. Whether or not a redirection took place, the path being queried for directory enumeration may have filesystem rule origin directories as direct children, and these would potentially need to be enumerated.

        std::array<DirectoryEnumerationInstruction::SingleDirectoryEnumeration, 2> directoriesToEnumerate;
        if (associatedPath != realOpenedPath)
        {
            // This block implements parts 1 and 2.
            // A redirection took place when opening the directory handle.
            // The open directory handle is already on the target side, so it is potentially necessary to do a merge with the origin side.
            // Whether or not a merge is needed depends on the scope of the filesystem rule that did the redirection. Any files outside its scope should show up on the origin side, all others should show up on the target side.

            std::wstring_view& redirectedPath = realOpenedPath;
            std::wstring_view& unredirectedPath = associatedPath;

            const std::wstring_view redirectedPathWindowsNamespacePrefix = Strings::PathGetWindowsNamespacePrefix(redirectedPath);
            const std::wstring_view redirectedPathTrimmedForQuery = redirectedPath.substr(redirectedPathWindowsNamespacePrefix.length());

            const std::wstring_view unredirectedPathWindowsNamespacePrefix = Strings::PathGetWindowsNamespacePrefix(unredirectedPath);
            const std::wstring_view unredirectedPathTrimmedForQuery = unredirectedPath.substr(unredirectedPathWindowsNamespacePrefix.length());

            const FilesystemRule* directoryEnumerationRedirectRule = SelectRuleForPath(unredirectedPathTrimmedForQuery);
            if (nullptr == directoryEnumerationRedirectRule)
            {
                Message::OutputFormatted(Message::ESeverity::Error, L"Directory enumeration query for path \"%.*s\" did not match any rules due to an internal error.", static_cast<int>(associatedPath.length()), associatedPath.data());
                return DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
            }

            DebugAssert(FilesystemRule::EDirectoryCompareResult::Unrelated != directoryEnumerationRedirectRule->DirectoryCompareWithOrigin(unredirectedPathTrimmedForQuery), "");
            DebugAssert(FilesystemRule::EDirectoryCompareResult::Unrelated != directoryEnumerationRedirectRule->DirectoryCompareWithTarget(redirectedPathTrimmedForQuery), "");

            if (FilesystemRule::ERedirectMode::Overlay == directoryEnumerationRedirectRule->GetRedirectMode())
            {
                // In overlay mode, the target-side contents that are in the filesystem rule scope are always enumerated and merged with the origin-side contents.
                // If the unredirected directory path is the rule's origin directory then only the contents of the redirected directory path that matches the file pattern can be included.
                // Otherwise the unredirected directory path is a descendent, and it is known that the directory is in scope due to a path component matching the rule's file pattern, so the entire directory should be included.

                Message::OutputFormatted(Message::ESeverity::Info, L"Directory enumeration query for path \"%.*s\" matches rule \"%.*s\" and will overlay the contents of \"%.*s\".", static_cast<int>(unredirectedPath.length()), unredirectedPath.data(), static_cast<int>(directoryEnumerationRedirectRule->GetName().length()), directoryEnumerationRedirectRule->GetName().data(), static_cast<int>(redirectedPath.length()), redirectedPath.data());
                if (FilesystemRule::EDirectoryCompareResult::Equal == directoryEnumerationRedirectRule->DirectoryCompareWithOrigin(unredirectedPathTrimmedForQuery))
                {
                    directoriesToEnumerate = {
                        DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeOnlyMatchingFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::RealOpenedPath, *directoryEnumerationRedirectRule),
                        DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::AssociatedPath)
                    };
                }
                else
                {
                    directoriesToEnumerate = {
                        DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::RealOpenedPath),
                        DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::AssociatedPath)
                    };
                }
            }
            else if ((false == directoryEnumerationRedirectRule->HasFilePatterns()) || (FilesystemRule::EDirectoryCompareResult::Equal != directoryEnumerationRedirectRule->DirectoryCompareWithOrigin(unredirectedPathTrimmedForQuery)))
            {
                // This is a simplification for two potential common cases:
                // (1) Filesystem rule that did the redirection does not actually define any file patterns and hence matches all files.
                // (2) Unredirected directory path is not the rule's origin directory but rather a descendant of it. (It cannot be an ancestor because then rule selection would not have chosen the rule.)
                // In both of these cases the easiest thing to do is just to enumerate the contents of the target side directory directly without worrying about file patterns.
                // Because the directory handle is already open for the target side directory, there is no need to do any further enumeration processing.

                // For case 1, normally if the directory being queried is the rule's origin directory, it would be necessary to merge in-scope target directory files with out-of-scope origin directory files.
                // However, because the rule does not have any file patterns, all files in the target directory are in scope and none of the files in the origin directory are out of scope.
                // Performing two separate enumerations and subsequently comparing the results with file patterns are therefore redundant and can be skipped.

                // For case 2, the directory being queried is a descendant of the rule's origin directory.
                // As an example, suppose that a rule defines an origin directory of C:\ASDF\GHJ. If the directory being enumerated is C:\ASDF\GHJ\KL\QWERTY, then "KL" needs to be checked against the rule's file patterns.
                // Since it is known that a redirection already occurred, it is also known that "KL" matches the rule's file patterns, so no additional checks are needed to verify that fact.

                Message::OutputFormatted(Message::ESeverity::Info, L"Directory enumeration query for path \"%.*s\" matches rule \"%.*s\" and will instead enumerate \"%.*s\".", static_cast<int>(unredirectedPath.length()), unredirectedPath.data(), static_cast<int>(directoryEnumerationRedirectRule->GetName().length()), directoryEnumerationRedirectRule->GetName().data(), static_cast<int>(redirectedPath.length()), redirectedPath.data());
                directoriesToEnumerate = {
                    DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::RealOpenedPath)
                };
            }
            else
            {
                // If the filesystem rule has one or more file patterns defined then the case is more general.
                // On the target side it is necessary to enumerate whatever files are present that match the rule's file patterns.
                // On the origin side it is necessary to enumerate whatever files are present that do not match the rule's file patterns and hence are beyond the rule's scope.

                Message::OutputFormatted(Message::ESeverity::Info, L"Directory enumeration query for path \"%.*s\" matches rule \"%.*s\" and will merge out-of-scope files in the origin hierarchy with in-scope files in the target hierarchy.", static_cast<int>(unredirectedPath.length()), unredirectedPath.data(), static_cast<int>(directoryEnumerationRedirectRule->GetName().length()), directoryEnumerationRedirectRule->GetName().data());
                directoriesToEnumerate = {
                    DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeOnlyMatchingFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::RealOpenedPath, *directoryEnumerationRedirectRule),
                    DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllExceptMatchingFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::AssociatedPath, *directoryEnumerationRedirectRule)
                };
            }
        }
        else
        {
            // If a redirection did not take place then the contents of the requested directory still need to be enumerated as is.
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"Directory enumeration query for path \"%.*s\" does not match any rules.", static_cast<int>(realOpenedPath.length()), realOpenedPath.data());
            directoriesToEnumerate = {
                    DirectoryEnumerationInstruction::SingleDirectoryEnumeration::IncludeAllFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::RealOpenedPath)
            };
        }

        std::optional<TemporaryVector<DirectoryEnumerationInstruction::SingleDirectoryNameInsertion>> directoryNamesToInsert;
        do {
            // This block implements part 3.
            // It is potentially necessary to insert potential origin directories into the enumeration result.
            // To do that, traverse the rule tree to the node that represents the path associated internally with the file handle.
            // Children of that node may contain filesystem rules, in which case those origin directories should be inserted into the enumeration results if their corresponding target directories actually exist in the real filesystem.

            std::wstring_view& directoryPath = associatedPath;

            const std::wstring_view directoryPathWindowsNamespacePrefix = Strings::PathGetWindowsNamespacePrefix(directoryPath);
            const std::wstring_view directoryPathTrimmedForQuery = directoryPath.substr(directoryPathWindowsNamespacePrefix.length());

            auto parentOfDirectoriesToInsert = originDirectoryIndex.TraverseTo(directoryPathTrimmedForQuery);
            if (nullptr != parentOfDirectoriesToInsert)
            {
                for (const auto& childItem : parentOfDirectoriesToInsert->GetChildren())
                {
                    if (true == childItem.second.HasData())
                    {
                        const FilesystemRule& childRule = *(childItem.second.GetData());

                        // Insertion of a rule's origin directory into the enumeration results requires that two things be true:
                        // (1) Origin directory base name matches the application-supplied enumeration quiery file pattern (or the enumeration query file pattern is missing). This check is needed because insertion bypasses the normal mechanism of having the system check for a match during the enumeration system call.
                        // (2) Target directory exists as a real directory in the filesystem.
                        // However, both of these things need to be checked at insertion time, not at instruction creation time.

                        if (false == directoryNamesToInsert.has_value())
                            directoryNamesToInsert.emplace();

                        Message::OutputFormatted(Message::ESeverity::Info, L"Directory enumeration query for path \"%.*s\" will potentially insert \"%.*s\" into the output because it is the origin directory of rule \"%.*s\".", static_cast<int>(directoryPath.length()), directoryPath.data(), static_cast<int>(childRule.GetOriginDirectoryName().length()), childRule.GetOriginDirectoryName().data(), static_cast<int>(childRule.GetName().length()), childRule.GetName().data());

                        directoryNamesToInsert->EmplaceBack(childRule);
                    }
                }
            }

            if (true == directoryNamesToInsert.has_value())
            {
                // Directory enumeration operations often present files in sorted order. To preserve this behavior, the names of directories to be inserted are sorted.
                std::sort(directoryNamesToInsert->begin(), directoryNamesToInsert->end(), [](const DirectoryEnumerationInstruction::SingleDirectoryNameInsertion& a, const DirectoryEnumerationInstruction::SingleDirectoryNameInsertion& b) -> bool
                    {
                        return (Strings::CompareCaseInsensitive(a.FileNameToInsert(), b.FileNameToInsert()) < 0);
                    }
                );
            }
        } while (false);

        return DirectoryEnumerationInstruction(std::move(directoriesToEnumerate), std::move(directoryNamesToInsert));
    }

    // --------

    FileOperationInstruction FilesystemDirector::GetInstructionForFileOperation(std::wstring_view absoluteFilePath, FileAccessMode fileAccessMode, CreateDisposition createDisposition) const
    {
        const std::wstring_view windowsNamespacePrefix = Strings::PathGetWindowsNamespacePrefix(absoluteFilePath);
        const std::wstring_view extraSuffix = ((true == absoluteFilePath.ends_with(L'\\')) ? L"\\" : L"");
        const std::wstring_view absoluteFilePathTrimmedForQuery = Strings::RemoveTrailing(absoluteFilePath.substr(windowsNamespacePrefix.length()), L'\\');

        if (false == PathBeginsWithDriveLetter(absoluteFilePathTrimmedForQuery))
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File operation redirection query for path \"%.*s\" does not begin with a drive letter and was therefore skipped for redirection.", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data());
            return FileOperationInstruction::NoRedirectionOrInterception();
        }

        const size_t lastSeparatorPos = absoluteFilePathTrimmedForQuery.find_last_of(L'\\');
        if (std::wstring_view::npos == lastSeparatorPos)
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File operation redirection query for path \"%.*s\" does not contain a final path separator and was therefore skipped for redirection.", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data());
            return FileOperationInstruction::NoRedirectionOrInterception();
        }

        const FilesystemRule* const selectedRule = SelectRuleForPath(absoluteFilePathTrimmedForQuery);
        if (nullptr == selectedRule)
        {
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"File operation redirection query for path \"%.*s\" did not match any rules.", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data());

            if (true == IsPrefixForAnyRule(absoluteFilePathTrimmedForQuery))
            {
                // If the file path could possibly be a directory path that but exists in the hierarchy as an ancestor of filesystem rules, then it is possible this same path could be a relative root path later on for a something that needs to be redirected.
                // Therefore, if a file handle is being created it needs to be associated with the unredirected path.
                return FileOperationInstruction::InterceptWithoutRedirection(FileOperationInstruction::EAssociateNameWithHandle::Unredirected);
            }
            else
            {
                // Otherwise, an unredirected file path is not interesting and can be safely passed to the system without any further processing.
                // The path specified is totally unrelated to all filesystem rules.
                return FileOperationInstruction::NoRedirectionOrInterception();
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
                return FileOperationInstruction::NoRedirectionOrInterception();
            }

            Message::OutputFormatted(Message::ESeverity::Info, L"File operation redirection query for path \"%.*s\" is for the origin directory of rule \"%s\" and was redirected to \"%s\".", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data(), selectedRule->GetName().data(), maybeRedirectedFilePath->AsCString());
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
                return FileOperationInstruction::NoRedirectionOrInterception();
            }

            Message::OutputFormatted(Message::ESeverity::Info, L"File operation redirection query for path \"%.*s\" matched rule \"%s\" and was redirected to \"%s\".", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data(), selectedRule->GetName().data(), maybeRedirectedFilePath->AsCString());
        }

        std::wstring_view redirectedFilePath = maybeRedirectedFilePath->AsStringView();

        BitSetEnum<FileOperationInstruction::EExtraPreOperation> extraPreOperations;
        std::wstring_view extraPreOperationOperand;

        if (true == createDisposition.AllowsCreateNewFile())
        {
            // If the filesystem operation can result in file creation, then it must be possible to complete file creation in the target hierarchy if it would also be possible to do so in the origin hierarchy.
            // In this situation it is necessary to ensure that the target-side hierarchy exists up to the directory containing the file that is to be potentially created, if said hierarchy also exists on the origin side.

            if (FilesystemOperations::IsDirectory(unredirectedPathDirectoryPartWithWindowsNamespacePrefix))
            {
                extraPreOperations.insert(static_cast<int>(FileOperationInstruction::EExtraPreOperation::EnsurePathHierarchyExists));
                extraPreOperationOperand = Strings::RemoveTrailing(redirectedFilePath.substr(0, redirectedFilePath.find_last_of(L'\\')), L'\\');
            }
        }
        else
        {
            // If the filesystem operation cannot result in file creation, then it is possible that the operation is targeting a directory that exists in the origin hierarchy.
            // In this situation it is necessary to ensure that the same directory also exists in the target hierarchy.

            if (FilesystemOperations::IsDirectory(absoluteFilePath))
            {
                extraPreOperations.insert(static_cast<int>(FileOperationInstruction::EExtraPreOperation::EnsurePathHierarchyExists));
                extraPreOperationOperand = Strings::RemoveTrailing(redirectedFilePath, L'\\');
            }
        }

        switch (selectedRule->GetRedirectMode())
        {
        case FilesystemRule::ERedirectMode::Overlay:
        case FilesystemRule::ERedirectMode::OverlayCopyOnWrite:
            return FileOperationInstruction::OverlayRedirectTo(std::move(*maybeRedirectedFilePath), FileOperationInstruction::EAssociateNameWithHandle::Unredirected, FileOperationInstruction::ECreateDispositionPreference::NoPreference, std::move(extraPreOperations), extraPreOperationOperand);

        default:
            return FileOperationInstruction::SimpleRedirectTo(std::move(*maybeRedirectedFilePath), FileOperationInstruction::EAssociateNameWithHandle::Unredirected, std::move(extraPreOperations), extraPreOperationOperand);
        }
    }
}
