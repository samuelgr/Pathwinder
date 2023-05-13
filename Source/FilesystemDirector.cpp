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
#include "FilesystemDirector.h"
#include "Resolver.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <map>
#include <set>
#include <string>
#include <string_view>


namespace Pathwinder
{
    // -------- CLASS METHODS ---------------------------------------------- //
    // See "FilesystemDirector.h" for documentation.

    bool FilesystemDirector::IsValidDirectoryString(std::wstring_view candidateDirectory)
    {
        // These characters are disallowed at any position in the directory string.
        // Directory strings cannot contain wildcards but can contain backslashes as separators and colons to identify drives.
        constexpr std::wstring_view kDisallowedCharacters = L"/*?\"<>|";

        // These characters are disallowed as the last character in the directory string.
        constexpr std::wstring_view kDisallowedAsLastCharacter = L"\\";

        if (true == candidateDirectory.empty())
            return false;

        for (wchar_t c : candidateDirectory)
        {
            if ((0 == iswprint(c)) || (kDisallowedCharacters.contains(c)))
                return false;
        }

        if (kDisallowedAsLastCharacter.contains(candidateDirectory.back()))
            return false;

        return true;
    }

    // --------

    bool FilesystemDirector::IsValidFilePatternString(std::wstring_view candidateFilePattern)
    {
        // These characters are disallowed inside file patterns.
        // File patterns identify files within directories and cannot identify subdirectories or drives.
        // Wildcards are allowed, but backslashes and colons are not.
        constexpr std::wstring_view kDisallowedCharacters = L"\\/:\"<>|";

        if (true == candidateFilePattern.empty())
            return false;

        for (wchar_t c : candidateFilePattern)
        {
            if ((0 == iswprint(c)) || (kDisallowedCharacters.contains(c)))
                return false;
        }

        return true;
    }


    // -------- INSTANCE METHODS ------------------------------------------- //
    // See "FilesystemDirector.h" for documentation.

    ValueOrError<const FilesystemRule*, TemporaryString> FilesystemDirector::CreateRule(std::wstring_view ruleName, std::wstring_view originDirectory, std::wstring_view targetDirectory, std::vector<std::wstring_view>&& filePatterns)
    {
        if (true == IsFinalized())
            return Strings::FormatString(L"Filesystem rule %s: Internal error: Attempted to create a new rule in a finalized registry.", ruleName.data());

        if (true == filesystemRules.contains(ruleName))
            return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Rule with the same name already exists.", ruleName.data());

        for (std::wstring_view filePattern : filePatterns)
        {
            if (false == IsValidFilePatternString(filePattern))
                return Strings::FormatString(L"Filesystem rule %s: File pattern: %s: Either empty or contains disallowed characters", ruleName.data(), filePattern.data());
        }

        // For each of the origin and target directories:
        // 1. Resolve any embedded references.
        // 2. Check for any invalid characters.
        // 3. Transform a possible relative path (possibly including "." and "..") into an absolute path.
        // 4. Verify that the resulting directory is not already in use as an origin or target directory for another filesystem rule.
        // If all operations succeed then the filesystem rule object can be created.

        Resolver::ResolvedStringOrError maybeOriginDirectoryResolvedString = Resolver::ResolveAllReferences(originDirectory);
        if (true == maybeOriginDirectoryResolvedString.HasError())
            return Strings::FormatString(L"Filesystem rule %s: Origin directory: %s.", ruleName.data(), maybeOriginDirectoryResolvedString.Error().AsCString());
        if (false == IsValidDirectoryString(maybeOriginDirectoryResolvedString.Value()))
            return Strings::FormatString(L"Filesystem rule %s: Origin directory: Either empty or contains disallowed characters.", ruleName.data());

        TemporaryString originDirectoryFullPath;
        originDirectoryFullPath.UnsafeSetSize(GetFullPathName(maybeOriginDirectoryResolvedString.Value().c_str(), originDirectoryFullPath.Capacity(), originDirectoryFullPath.Data(), nullptr));
        if (true == originDirectoryFullPath.Empty())
            return Strings::FormatString(L"Filesystem rule %s: Origin directory: Failed to resolve full path: %s.", ruleName.data(), Strings::SystemErrorCodeString(GetLastError()).AsCString());
        if (true == originDirectoryFullPath.Overflow())
            return Strings::FormatString(L"Filesystem rule %s: Origin directory: Full path exceeds limit of %u characters.", ruleName.data(), originDirectoryFullPath.Capacity());
        if (true == HasDirectory(originDirectoryFullPath))
            return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Origin directory is already in use as either an origin or target directory by another rule.", ruleName.data());

        Resolver::ResolvedStringOrError maybeTargetDirectoryResolvedString = Resolver::ResolveAllReferences(targetDirectory);
        if (true == maybeTargetDirectoryResolvedString.HasError())
            return Strings::FormatString(L"Filesystem rule %s: Target directory: %s.", ruleName.data(), maybeTargetDirectoryResolvedString.Error().AsCString());
        if (false == IsValidDirectoryString(maybeTargetDirectoryResolvedString.Value()))
            return Strings::FormatString(L"Filesystem rule %s: Target directory: Either empty or contains disallowed characters.", ruleName.data());

        TemporaryString targetDirectoryFullPath;
        targetDirectoryFullPath.UnsafeSetSize(GetFullPathName(maybeTargetDirectoryResolvedString.Value().c_str(), targetDirectoryFullPath.Capacity(), targetDirectoryFullPath.Data(), nullptr));
        if (true == targetDirectoryFullPath.Empty())
            return Strings::FormatString(L"Filesystem rule %s: Target directory: Failed to resolve full path: %s.", ruleName.data(), Strings::SystemErrorCodeString(GetLastError()).AsCString());
        if (true == targetDirectoryFullPath.Overflow())
            return Strings::FormatString(L"Filesystem rule %s: Target directory: Full path exceeds limit of %u characters.", ruleName.data(), targetDirectoryFullPath.Capacity());
        if (true == HasOriginDirectory(targetDirectoryFullPath))
            return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Target directory is already in use as a target directory by another rule.", ruleName.data());

        std::wstring_view originDirectoryView = *originDirectories.emplace(originDirectoryFullPath).first;
        std::wstring_view targetDirectoryView = *targetDirectories.emplace(targetDirectoryFullPath).first;
        return &filesystemRules.emplace(std::wstring(ruleName), FilesystemRule(originDirectoryView, targetDirectoryView, std::move(filePatterns))).first->second;
    }

    // --------

    ValueOrError<size_t, TemporaryString> FilesystemDirector::Finalize(void)
    {
        if (true == filesystemRules.empty())
            return L"Filesystem rules: Internal error: Attempted to finalize an empty registry.";

        for (const auto& filesystemRuleRecord : filesystemRules)
        {
            const FilesystemRule& filesystemRule = filesystemRuleRecord.second;

            const DWORD originDirectoryAttributes = GetFileAttributes(filesystemRule.GetOriginDirectoryFullPath().data());
            const bool originDirectoryDoesNotExist = (INVALID_FILE_ATTRIBUTES == originDirectoryAttributes);
            const bool originDirectoryExistsAsRealDirectory = (INVALID_FILE_ATTRIBUTES != originDirectoryAttributes) && (0 != (originDirectoryAttributes & FILE_ATTRIBUTE_DIRECTORY));
            if (false == (originDirectoryDoesNotExist || originDirectoryExistsAsRealDirectory))
                return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Origin directory must either not exist at all or exist as a real directory.", filesystemRuleRecord.first.c_str());

            if (true == filesystemRule.GetTargetDirectoryParent().empty())
                return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Target directory cannot be a filesystem root.", filesystemRuleRecord.first.c_str());

            const TemporaryString originDirectoryParent = filesystemRule.GetOriginDirectoryParent();
            if (true == originDirectoryParent.Empty())
                return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Origin directory cannot be a filesystem root.", filesystemRuleRecord.first.c_str());

            const DWORD originDirectoryParentAttributes = GetFileAttributes(originDirectoryParent.AsCString());
            const bool originDirectoryParentExistsAsRealDirectory = (INVALID_FILE_ATTRIBUTES != originDirectoryParentAttributes) && (0 != (originDirectoryParentAttributes & FILE_ATTRIBUTE_DIRECTORY));
            if ((false == originDirectoryParentExistsAsRealDirectory) && (false == HasOriginDirectory(originDirectoryParent)))
                Strings::FormatString(L"Filesystem rule %s: Constraint violation: Parent of origin directory must either exist as a real directory or be the origin directory of another filesystem rule.", filesystemRuleRecord.first.c_str());
        }

        isFinalized = true;
        return filesystemRules.size();
    }
}
