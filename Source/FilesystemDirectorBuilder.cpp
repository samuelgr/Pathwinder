/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemDirectorBuilder.cpp
 *   Implementation of functionality for building new filesystem director
 *   objects piece-wise at runtime.
 *****************************************************************************/

#include "ApiWindows.h"
#include "DebugAssert.h"
#include "FilesystemDirectorBuilder.h"
#include "FilesystemOperations.h"
#include "Resolver.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>


namespace Pathwinder
{
    // -------- CLASS METHODS ---------------------------------------------- //
    // See "FilesystemDirectorBuilder.h" for documentation.

    bool FilesystemDirectorBuilder::IsValidDirectoryString(std::wstring_view candidateDirectory)
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

    bool FilesystemDirectorBuilder::IsValidFilePatternString(std::wstring_view candidateFilePattern)
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
    // See "FilesystemDirectorBuilder.h" for documentation.

    ValueOrError<const FilesystemRule*, TemporaryString> FilesystemDirectorBuilder::AddRule(std::wstring_view ruleName, std::wstring_view originDirectory, std::wstring_view targetDirectory, std::vector<std::wstring>&& filePatterns)
    {
        if (true == filesystemRules.contains(ruleName))
            return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Rule with the same name already exists.", ruleName.data());

        for (std::wstring_view filePattern : filePatterns)
        {
            if (false == IsValidFilePatternString(filePattern))
                return Strings::FormatString(L"Filesystem rule %s: File pattern: %s: Either empty or contains disallowed characters.", ruleName.data(), filePattern.data());
        }

        // For each of the origin and target directories:
        // 1. Resolve any embedded references.
        // 2. Check for any invalid characters.
        // 3. Transform a possible relative path (possibly including "." and "..") into an absolute path.
        // 4. Verify that the resulting directory is not already in use as an origin or target directory for another filesystem rule.
        // 5. Verify that the resulting directory is not a filesystem root (i.e. it has a parent directory).
        // If all operations succeed then the filesystem rule object can be created.

        Resolver::ResolvedStringOrError maybeOriginDirectoryResolvedString = Resolver::ResolveAllReferences(originDirectory);
        if (true == maybeOriginDirectoryResolvedString.HasError())
            return Strings::FormatString(L"Filesystem rule %s: Origin directory: %s.", ruleName.data(), maybeOriginDirectoryResolvedString.Error().AsCString());
        if (false == IsValidDirectoryString(maybeOriginDirectoryResolvedString.Value()))
            return Strings::FormatString(L"Filesystem rule %s: Origin directory: Either empty or contains disallowed characters.", ruleName.data());

        TemporaryString originDirectoryFullPath;
        originDirectoryFullPath.UnsafeSetSize(GetFullPathName(maybeOriginDirectoryResolvedString.Value().c_str(), originDirectoryFullPath.Capacity(), originDirectoryFullPath.Data(), nullptr));
        while (true == originDirectoryFullPath.AsStringView().ends_with(L'\\'))
            originDirectoryFullPath.RemoveSuffix(1);

        if (false == originDirectoryFullPath.AsStringView().contains(L'\\'))
            return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Origin directory cannot be a filesystem root.", ruleName.data());
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
        while (true == targetDirectoryFullPath.AsStringView().ends_with(L'\\'))
            targetDirectoryFullPath.RemoveSuffix(1);

        if (false == targetDirectoryFullPath.AsStringView().contains(L'\\'))
            return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Target directory cannot be a filesystem root.", ruleName.data());
        if (true == targetDirectoryFullPath.Empty())
            return Strings::FormatString(L"Filesystem rule %s: Target directory: Failed to resolve full path: %s.", ruleName.data(), Strings::SystemErrorCodeString(GetLastError()).AsCString());
        if (true == targetDirectoryFullPath.Overflow())
            return Strings::FormatString(L"Filesystem rule %s: Target directory: Full path exceeds limit of %u characters.", ruleName.data(), targetDirectoryFullPath.Capacity());
        if (true == HasOriginDirectory(targetDirectoryFullPath))
            return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Target directory is already in use as an origin directory by another rule.", ruleName.data());

        const auto createResult = filesystemRules.emplace(std::wstring(ruleName), FilesystemRule(originDirectoryFullPath.AsStringView(), targetDirectoryFullPath.AsStringView(), std::move(filePatterns)));
        DebugAssert(createResult.second, "FilesystemDirectorBuilder consistency check failed due to unsuccessful creation of a supposedly-unique filesystem rule.");

        std::wstring_view newRuleName = createResult.first->first;
        FilesystemRule* newRule = &createResult.first->second;

        newRule->SetName(newRuleName);
        originDirectories.Insert(newRule->GetOriginDirectoryFullPath(), *newRule);
        targetDirectories.emplace(newRule->GetTargetDirectoryFullPath());

        return newRule;
    }

    // --------

    ValueOrError<std::unique_ptr<const FilesystemDirector>, TemporaryString> FilesystemDirectorBuilder::Build(void)
    {
        if (true == filesystemRules.empty())
            return L"Filesystem rules: Internal error: Attempted to finalize an empty registry.";

        for (const auto& filesystemRuleRecord : filesystemRules)
        {
            const FilesystemRule& filesystemRule = filesystemRuleRecord.second;

            const bool originExists = FilesystemOperations::Exists(filesystemRule.GetOriginDirectoryFullPath().data());
            const bool originIsDirectory = FilesystemOperations::IsDirectory(filesystemRule.GetOriginDirectoryFullPath().data());
            if (false == (!originExists || originIsDirectory))
                return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Origin directory must either not exist at all or exist as a real directory.", filesystemRuleRecord.first.c_str());

            const TemporaryString originDirectoryParent = filesystemRule.GetOriginDirectoryParent();
            if ((false == FilesystemOperations::IsDirectory(originDirectoryParent.AsCString())) && (false == HasOriginDirectory(originDirectoryParent)))
                return Strings::FormatString(L"Filesystem rule %s: Constraint violation: Parent of origin directory must either exist as a real directory or be the origin directory of another filesystem rule.", filesystemRuleRecord.first.c_str());
        }

        return std::make_unique<FilesystemDirector>(std::move(filesystemRules), std::move(originDirectories));
    }
}
