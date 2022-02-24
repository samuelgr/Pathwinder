/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file FilesystemRuleRegistry.cpp
 *   Implementation of filesystem manipulation and application functionality.
 *****************************************************************************/

#include "FilesystemRuleRegistry.h"
#include "Resolver.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <windows.h>


namespace Pathwinder
{
    // -------- CLASS METHODS ---------------------------------------------- //
    // See "FilesystemRuleRegistry.h" for documentation.

    bool FilesystemRuleRegistry::IsValidDirectoryString(std::wstring_view candidateDirectory)
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

    bool FilesystemRuleRegistry::IsValidFilePatternString(std::wstring_view candidateFilePattern)
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
    // See "FilesystemRuleRegistry.h" for documentation.

    ValueOrError<FilesystemRule*, std::wstring> FilesystemRuleRegistry::CreateRule(std::wstring_view ruleName, std::wstring_view originDirectory, std::wstring_view targetDirectory, std::vector<std::wstring_view>&& filePatterns)
    {
        // Constraints checked by this method for each filesystem rule:
        // - Origin directory must not already be an origin or target directory for another rule.
        // - Target directory must not already be an origin directory for another rule.
        
        if (true == isFinalized)
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Internal error: Attempted to create a new rule in a finalized registry.", ruleName.data()));

        if (true == filesystemRules.contains(ruleName))
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Constraint violation: Rule with the same name already exists.", ruleName.data()));

        for (std::wstring_view filePattern : filePatterns)
        {
            if (false == IsValidFilePatternString(filePattern))
                return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: File pattern: %s: Either empty or contains disallowed characters", ruleName.data(), filePattern.data()));
        }

        // For each of the origin and target directories:
        // 1. Resolve any embedded references.
        // 2. Check for any invalid characters.
        // 3. Transform a possible relative path (possibly including "." and "..") into an absolute path.
        // 4. Verify that the resulting directory is not already in use as an origin or target directory for another filesystem rule.
        // If all operations succeed then the filesystem rule object can be created.

        Resolver::ResolvedStringOrError maybeOriginDirectoryResolvedString = Resolver::ResolveAllReferences(originDirectory);
        if (true == maybeOriginDirectoryResolvedString.HasError())
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Origin directory: %s.", ruleName.data(), maybeOriginDirectoryResolvedString.Error().c_str()));
        if (false == IsValidDirectoryString(maybeOriginDirectoryResolvedString.Value()))
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Origin directory: Either empty or contains disallowed characters.", ruleName.data()));

        TemporaryString originDirectoryFullPath;
        originDirectoryFullPath.UnsafeSetSize(GetFullPathName(maybeOriginDirectoryResolvedString.Value().c_str(), originDirectoryFullPath.Capacity(), originDirectoryFullPath.Data(), nullptr));
        if (true == originDirectoryFullPath.Empty())
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Origin directory: Failed to resolve full path: %s.", ruleName.data(), Strings::SystemErrorCodeString(GetLastError()).AsCString()));
        if (true == originDirectoryFullPath.Overflow())
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Origin directory: Full path exceeds limit of %u characters.", ruleName.data(), originDirectoryFullPath.Capacity()));
        if (true == HasDirectory(originDirectoryFullPath))
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Constraint violation: Origin directory is already in use as either an origin or target directory by another rule.", ruleName.data()));

        Resolver::ResolvedStringOrError maybeTargetDirectoryResolvedString = Resolver::ResolveAllReferences(targetDirectory);
        if (true == maybeTargetDirectoryResolvedString.HasError())
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Target directory: %s.", ruleName.data(), maybeTargetDirectoryResolvedString.Error().c_str()));
        if (false == IsValidDirectoryString(maybeTargetDirectoryResolvedString.Value()))
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Target directory: Either empty or contains disallowed characters.", ruleName.data()));

        TemporaryString targetDirectoryFullPath;
        targetDirectoryFullPath.UnsafeSetSize(GetFullPathName(maybeTargetDirectoryResolvedString.Value().c_str(), targetDirectoryFullPath.Capacity(), targetDirectoryFullPath.Data(), nullptr));
        if (true == targetDirectoryFullPath.Empty())
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Target directory: Failed to resolve full path: %s.", ruleName.data(), Strings::SystemErrorCodeString(GetLastError()).AsCString()));
        if (true == targetDirectoryFullPath.Overflow())
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Target directory: Full path exceeds limit of %u characters.", ruleName.data(), targetDirectoryFullPath.Capacity()));
        if (true == HasOriginDirectory(targetDirectoryFullPath))
            return ValueOrError<FilesystemRule*, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Constraint violation: Target directory is already in use as a target directory by another rule.", ruleName.data()));

        std::wstring_view originDirectoryView = *originDirectories.emplace(originDirectoryFullPath).first;
        std::wstring_view targetDirectoryView = *targetDirectories.emplace(targetDirectoryFullPath).first;
        return ValueOrError<FilesystemRule*, std::wstring>::MakeValue(&filesystemRules.emplace(std::wstring(ruleName), FilesystemRule(originDirectoryView, targetDirectoryView, std::move(filePatterns))).first->second);
    }

    // --------

    ValueOrError<unsigned int, std::wstring> FilesystemRuleRegistry::Finalize(void)
    {
        if (true == filesystemRules.empty())
            return ValueOrError<unsigned int, std::wstring>::MakeError(L"Filesystem rules: Internal error: Attempted to finalize an empty registry.");

        // Constraints checked by this method for each filesystem rule:
        // - Origin and target directories are not root directories (i.e. they both have parent directories).
        // - Origin directory either exists as a real directory or does not exist at all.
        // - Immediate parent of the origin directory either exists as a directory or serves as the origin directory for another rule.

        for (const auto& kFilesystemRuleRecord : filesystemRules)
        {
            const FilesystemRule& kFilesystemRule = kFilesystemRuleRecord.second;

            const DWORD kOriginDirectoryAttributes = GetFileAttributes(kFilesystemRule.GetOriginDirectoryFullPath().data());
            const bool kOriginDirectoryDoesNotExist = (INVALID_FILE_ATTRIBUTES == kOriginDirectoryAttributes);
            const bool kOriginDirectoryExistsAsRealDirectory = (INVALID_FILE_ATTRIBUTES != kOriginDirectoryAttributes) && (0 != (kOriginDirectoryAttributes & FILE_ATTRIBUTE_DIRECTORY));
            if (false == (kOriginDirectoryDoesNotExist || kOriginDirectoryExistsAsRealDirectory))
                return ValueOrError<unsigned int, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Constraint violation: Origin directory must either not exist at all or exist as a real directory.", kFilesystemRuleRecord.first.c_str()));

            if (true == kFilesystemRule.GetTargetDirectoryParent().empty())
                return ValueOrError<unsigned int, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Constraint violation: Target directory cannot be a filesystem root.", kFilesystemRuleRecord.first.c_str()));

            const TemporaryString kOriginDirectoryParent = kFilesystemRule.GetOriginDirectoryParent();
            if (true == kOriginDirectoryParent.Empty())
                return ValueOrError<unsigned int, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Constraint violation: Origin directory cannot be a filesystem root.", kFilesystemRuleRecord.first.c_str()));

            const DWORD kOriginDirectoryParentAttributes = GetFileAttributes(kOriginDirectoryParent.AsCString());
            const bool kOriginDirectoryParentExistsAsRealDirectory = (INVALID_FILE_ATTRIBUTES != kOriginDirectoryParentAttributes) && (0 != (kOriginDirectoryParentAttributes & FILE_ATTRIBUTE_DIRECTORY));
            if ((false == kOriginDirectoryParentExistsAsRealDirectory) && (false == HasOriginDirectory(kOriginDirectoryParent)))
                return ValueOrError<unsigned int, std::wstring>::MakeError(Strings::FormatString(L"Filesystem rule %s: Constraint violation: Parent of origin directory must either exist as a real directory or be the origin directory of another filesystem rule.", kFilesystemRuleRecord.first.c_str()));
        }

        isFinalized = true;
        return ValueOrError<unsigned int, std::wstring>::MakeValue((unsigned int)filesystemRules.size());
    }
}
