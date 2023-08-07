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

#include <cwctype>
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

    /// Determines if the specified filename matches the specified file pattern. An empty file pattern is presumed to match everything.
    /// Input filename must not contain any backslash separators, as it is intended to represent a file within a directory rather than a path.
    /// Input file pattern must be in upper-case.
    /// @param [in] fileName Filename to check.
    /// @param [in] filePatternUpperCase File pattern to be used for comparison with the file name.
    /// @return `true` if the file name matches the supplied pattern or if it is entirely empty, `false` otherwise.
    static bool FileNameMatchesPattern(std::wstring_view fileName, std::wstring_view filePatternUpperCase)
    {
        if (true == filePatternUpperCase.empty())
            return true;

        UNICODE_STRING fileNameString = Strings::NtConvertStringViewToUnicodeString(fileName);
        UNICODE_STRING filePatternString = Strings::NtConvertStringViewToUnicodeString(filePatternUpperCase);

        return (TRUE == WindowsInternal::RtlIsNameInExpression(&filePatternString, &fileNameString, TRUE, nullptr));
    }

    /// Converts the specified read-only string into uppercase.
    /// Userful primarily for application-supplied file patterns that need to be matched in a case-insensitive way and hence need to be upper-case due to an implementation quirk of the matching function.
    /// @param [in] str String to be converted.
    /// @return Same as the input string but with all characters converted to uppercase as appropriate.
    static inline TemporaryString MakeUppercaseString(std::wstring_view str)
    {
        TemporaryString uppercaseStr;

        for (wchar_t c : str)
            uppercaseStr += std::towupper(c);

        return uppercaseStr;
    }

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

    DirectoryEnumerationInstruction FilesystemDirector::GetInstructionForDirectoryEnumeration(std::wstring_view associatedPath, std::wstring_view realOpenedPath, std::wstring_view enumerationQueryFilePattern) const
    {
        associatedPath = Strings::RemoveTrailing(associatedPath, L'\\');
        realOpenedPath = Strings::RemoveTrailing(realOpenedPath, L'\\');

        // If this object is queried for a directory enumeration, then that means a previous file operation resulted in a directory handle being opened using either an unredirected or a redirected path and associated with an unredirected path.
        // There are three parts to a complete directory enumeration operation:
        // (1) Enumerate the contents of the redirected directory path, potentially subject to file pattern matching with a filesystem rule. This captures all of the files in scope of the filesystem rule that exist on the target side.
        // (2) Enumerate the contents of the unredirected directory path, for anything that exists on the origin side but is beyond the scope of the filesystem rule.
        // (3) Insert specific subdirectories into the enumeration results provided back to the application. This captures filesystem rules whose origin directories do not actually exist in the real filesystem on the origin side.
        // Parts 1 and 2 are only interesting if a redirection took place (otherwise there is no reason to worry about merging directory contents on the origin side with directory contents on the target side).
        // Part 3 is only interesting if a redirection did not take place (otherwise the open handle already would be on the target side, and any origin directories that do not really exist on the origin side would potentially exist on the target side anyway and thus be enumerated correctly).

        if (associatedPath == realOpenedPath)
        {
            // No redirection took place when opening the directory handle.
            // It is only necessary to insert potential origin directories into the enumeration result.
            // This is accomplished by traversing the rule tree to the node that represents the path associated internally with the file handle.
            // Children of that node may contain filesystem rules, in which case those origin directories should be inserted into the enumeration results if their corresponding target directories actually exist in the real filesystem.

            std::wstring_view& directoryPath = associatedPath;

            const std::wstring_view directoryPathWindowsNamespacePrefix = Strings::PathGetWindowsNamespacePrefix(directoryPath);
            const std::wstring_view directoryPathTrimmedForQuery = directoryPath.substr(directoryPathWindowsNamespacePrefix.length());

            auto parentOfDirectoriesToInsert = originDirectoryIndex.TraverseTo(directoryPathTrimmedForQuery);
            if (nullptr == parentOfDirectoriesToInsert)
            {
                Message::OutputFormatted(Message::ESeverity::SuperDebug, L"Directory enumeration query for path \"%.*s\" did not match any rules.", static_cast<int>(associatedPath.length()), associatedPath.data());
                return DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
            }

            TemporaryString enumerationQueryFilePatternUpperCase = MakeUppercaseString(enumerationQueryFilePattern);
            std::optional<TemporaryVector<std::wstring_view>> directoryNamesToInsert;
            for (const auto& childItem : parentOfDirectoriesToInsert->GetChildren())
            {
                if (true == childItem.second.HasData())
                {
                    const FilesystemRule& childRule = *(childItem.second.GetData());

                    // Insertion of a rule's origin directory into the enumeration results requires that two things be true:
                    // (1) Origin directory base name matches the application-supplied enumeration quiery file pattern (or the enumeration query file pattern is missing).
                    // (2) Target directory exists as a real directory in the filesystem.

                    const bool originDirectoryBaseNameMatchesEnumerationQuery = FileNameMatchesPattern(childRule.GetOriginDirectoryName(), enumerationQueryFilePatternUpperCase);
                    const bool targetDirectoryExistsInRealFilesystem = FilesystemOperations::IsDirectory(childRule.GetTargetDirectoryFullPath());

                    if (originDirectoryBaseNameMatchesEnumerationQuery && targetDirectoryExistsInRealFilesystem)
                    {
                        if (false == directoryNamesToInsert.has_value())
                            directoryNamesToInsert.emplace();

                        Message::OutputFormatted(Message::ESeverity::Info, L"Directory enumeration query for path \"%.*s\" will insert \"%.*s\", which is the origin directory of rule \"%.*s\", into the output.", static_cast<int>(directoryPath.length()), directoryPath.data(), static_cast<int>(childRule.GetOriginDirectoryName().length()), childRule.GetOriginDirectoryName().data(), static_cast<int>(childRule.GetName().length()), childRule.GetName().data());
                        directoryNamesToInsert.value().PushBack(childRule.GetOriginDirectoryName());
                    }
                }
            }

            if (directoryNamesToInsert.has_value())
                return DirectoryEnumerationInstruction::InsertExtraDirectoryNames(std::move(directoryNamesToInsert).value());
            else
                return DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
        }
        else
        {
            // A redirection took place when opening the directory handle.
            // The open directory handle is already on the target side, so it potentially necessary to do a merge with the origin directory side.
            // Whether or not a merge is needed depends on the scope of the filesystem rule that did the redirection. Any files outside its scope should show up on the origin side, all others should show up only if they exist on the target side.

            std::wstring_view& unredirectedPath = associatedPath;
            std::wstring_view& redirectedPath = realOpenedPath;

            const std::wstring_view unredirectedPathWindowsNamespacePrefix = Strings::PathGetWindowsNamespacePrefix(unredirectedPath);
            const std::wstring_view unredirectedPathTrimmedForQuery = unredirectedPath.substr(unredirectedPathWindowsNamespacePrefix.length());

            const FilesystemRule* directoryEnumerationRedirectRule = SelectRuleForPath(unredirectedPathTrimmedForQuery);
            if (nullptr == directoryEnumerationRedirectRule)
            {
                Message::OutputFormatted(Message::ESeverity::Error, L"Directory enumeration query for path \"%.*s\" did not match any rules due to an internal error.", static_cast<int>(associatedPath.length()), associatedPath.data());
                return DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
            }

            if (false == directoryEnumerationRedirectRule->HasFilePatterns())
            {
                // This is a simplification for one potential common case in which a filesystem rule does not actually define any file patterns and hence matches all files.
                // In this situation the easiest thing to do is just to enumerate the contents of the target side directory directly without worrying about file patterns.
                // Because the directory handle is already open for the target side directory, there is no need to do any further enumeration processing.

                Message::OutputFormatted(Message::ESeverity::Info, L"Directory enumeration query for path \"%.*s\" matches rule \"%.*s\" and will instead enumerate \"%.*s\".", static_cast<int>(unredirectedPath.length()), unredirectedPath.data(), static_cast<int>(directoryEnumerationRedirectRule->GetName().length()), directoryEnumerationRedirectRule->GetName().data(), static_cast<int>(redirectedPath.length()), redirectedPath.data());
                return DirectoryEnumerationInstruction::PassThroughUnmodifiedQuery();
            }
            else
            {
                // If the filesystem rule has one or more file patterns defined then the case is more general.
                // On the target side it is necessary to enumerate whatever files are present that match the rule's file patterns.
                // On the origin side it is necessary to enumerate whatever files are present that do not match the rule's file patterns and hence are beyond the rule's scope.

                Message::OutputFormatted(Message::ESeverity::Info, L"Directory enumeration query for path \"%.*s\" matches rule \"%.*s\" and will merge out-of-scope files in the original query path with in-scope files enumerated from \"%.*s\".", static_cast<int>(unredirectedPath.length()), unredirectedPath.data(), static_cast<int>(directoryEnumerationRedirectRule->GetName().length()), directoryEnumerationRedirectRule->GetName().data(), static_cast<int>(redirectedPath.length()), redirectedPath.data());
                return DirectoryEnumerationInstruction::EnumerateInOrder({
                    DirectoryEnumerationInstruction::SingleDirectoryEnumerator::IncludeOnlyMatchingFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::RealOpenedPath, directoryEnumerationRedirectRule),
                    DirectoryEnumerationInstruction::SingleDirectoryEnumerator::IncludeAllExceptMatchingFilenames(DirectoryEnumerationInstruction::EDirectoryPathSource::AssociatedPath, directoryEnumerationRedirectRule)
                });
            }
        }
    }

    // --------

    FileOperationInstruction FilesystemDirector::GetInstructionForFileOperation(std::wstring_view absoluteFilePath, EFileOperationMode fileOperationMode) const
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
                return FileOperationInstruction::NoRedirectionOrInterception();
            }

            Message::OutputFormatted(Message::ESeverity::Info, L"File operation redirection query for path \"%.*s\" matched rule \"%s\" and was redirected to \"%s\".", static_cast<int>(absoluteFilePath.length()), absoluteFilePath.data(), selectedRule->GetName().data(), maybeRedirectedFilePath.value().AsCString());
        }

        std::wstring_view redirectedFilePath = maybeRedirectedFilePath.value().AsStringView();

        BitSetEnum<FileOperationInstruction::EExtraPreOperation> extraPreOperations;
        std::wstring_view extraPreOperationOperand;

        if (true == CanFileOperationResultInFileCreation(fileOperationMode))
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

        return FileOperationInstruction::RedirectTo(std::move(maybeRedirectedFilePath.value()), FileOperationInstruction::EAssociateNameWithHandle::Unredirected, std::move(extraPreOperations), extraPreOperationOperand);
    }
}
