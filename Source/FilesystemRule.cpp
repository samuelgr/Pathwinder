/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file FilesystemRule.cpp
 *   Implementation of filesystem redirection rule functionality.
 *****************************************************************************/

#include "FilesystemRule.h"
#include "Resolver.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <cwctype>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>


namespace Pathwinder
{
    // -------- CLASS METHODS ---------------------------------------------- //
    // See "FilesystemRule.h" for documentation.

    ValueOrError<FilesystemRule, std::wstring> FilesystemRule::Create(std::wstring_view originDirectory, std::wstring_view targetDirectory, std::vector<std::wstring_view>&& filePatterns)
    {
        for (std::wstring_view filePattern : filePatterns)
        {
            if (false == IsValidFilePatternString(filePattern))
                return ValueOrError<FilesystemRule, std::wstring>::MakeError(Strings::FormatString(L"File pattern: %s: Either empty or contains disallowed characters", std::wstring(filePattern).c_str()));
        }

        // For each of the origin and target directories:
        // 1. Resolve any embedded references.
        // 2. Check for any invalid characters.
        // 3. Transform a possible relative path (possibly including "." and "..") into an absolute path.
        // If all operations succeed then the filesystem rule object can be created.

        Resolver::ResolvedStringOrError maybeOriginDirectoryResolvedString = Resolver::ResolveAllReferences(originDirectory);
        if (true == maybeOriginDirectoryResolvedString.HasError())
            return ValueOrError<FilesystemRule, std::wstring>::MakeError(Strings::FormatString(L"Origin directory: %s", maybeOriginDirectoryResolvedString.Error().c_str()));
        if (false == IsValidDirectoryString(maybeOriginDirectoryResolvedString.Value()))
            return ValueOrError<FilesystemRule, std::wstring>::MakeError(Strings::FormatString(L"Origin directory: %s: Either empty or contains disallowed characters", maybeOriginDirectoryResolvedString.Value().c_str()));

        TemporaryString originDirectoryFullPath;
        originDirectoryFullPath.UnsafeSetSize(GetFullPathName(maybeOriginDirectoryResolvedString.Value().c_str(), originDirectoryFullPath.Capacity(), originDirectoryFullPath.Data(), nullptr));
        if (true == originDirectoryFullPath.Empty())
            return ValueOrError<FilesystemRule, std::wstring>::MakeError(Strings::FormatString(L"Origin directory: Failed to resolve full path: %s", Strings::SystemErrorCodeString(GetLastError()).AsCString()));
        if (true == originDirectoryFullPath.Overflow())
            return ValueOrError<FilesystemRule, std::wstring>::MakeError(Strings::FormatString(L"Origin directory: Full path exceeds limit of %u characters", originDirectoryFullPath.Capacity()));

        Resolver::ResolvedStringOrError maybeTargetDirectoryResolvedString = Resolver::ResolveAllReferences(targetDirectory);
        if (true == maybeTargetDirectoryResolvedString.HasError())
            return ValueOrError<FilesystemRule, std::wstring>::MakeError(Strings::FormatString(L"Target directory: %s", maybeTargetDirectoryResolvedString.Error().c_str()));
        if (false == IsValidDirectoryString(maybeTargetDirectoryResolvedString.Value()))
            return ValueOrError<FilesystemRule, std::wstring>::MakeError(Strings::FormatString(L"Target directory: %s: Either empty or contains disallowed characters", maybeTargetDirectoryResolvedString.Value().c_str()));

        TemporaryString targetDirectoryFullPath;
        targetDirectoryFullPath.UnsafeSetSize(GetFullPathName(maybeOriginDirectoryResolvedString.Value().c_str(), targetDirectoryFullPath.Capacity(), targetDirectoryFullPath.Data(), nullptr));
        if (true == targetDirectoryFullPath.Empty())
            return ValueOrError<FilesystemRule, std::wstring>::MakeError(Strings::FormatString(L"Target directory: Failed to resolve full path: %s", Strings::SystemErrorCodeString(GetLastError()).AsCString()));
        if (true == targetDirectoryFullPath.Overflow())
            return ValueOrError<FilesystemRule, std::wstring>::MakeError(Strings::FormatString(L"Target directory: Full path exceeds limit of %u characters", targetDirectoryFullPath.Capacity()));

        return FilesystemRule(originDirectoryFullPath, targetDirectoryFullPath, std::move(filePatterns));
    }

    // --------

    bool FilesystemRule::IsValidDirectoryString(std::wstring_view candidateDirectory)
    {
        // These characters are disallowed at any position in the directory string.
        // Directory strings cannot contain wildcards but can contain backslashes as separators and colons to identify drives.
        constexpr std::wstring_view kDisallowedCharacters = L"/*?\"<>|";

        // These characters are disallowed as the last character in the directory string.
        constexpr std::wstring_view kDisallowedCharactersLast = L"\\";

        if (true == candidateDirectory.empty())
            return false;

        for (wchar_t c : candidateDirectory)
        {
            if ((0 == iswprint(c)) || (kDisallowedCharacters.contains(c)))
                return false;
        }

        if (kDisallowedCharactersLast.contains(candidateDirectory.back()))
            return false;

        return true;
    }

    // --------

    bool FilesystemRule::IsValidFilePatternString(std::wstring_view candidateFilePattern)
    {
        // These characters are disallowed inside file patterns.
        // File patterns identify files within directories and cannot identify subdirectories or drives.
        // Wildcards are allowed, but backslashes and colons are not.
        constexpr std::wstring_view kDisallowedCharacters = L"\\/:\"<>|";

        if (true == candidateFilePattern.empty())
            return true;

        for (wchar_t c : candidateFilePattern)
        {
            if ((0 == iswprint(c)) || (kDisallowedCharacters.contains(c)))
                return false;
        }

        return true;
    }
}
