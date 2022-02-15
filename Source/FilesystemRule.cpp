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
#include "Message.h"
#include "Resolver.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <cwctype>
#include <optional>
#include <shlwapi.h>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>


namespace Pathwinder
{
    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Compares the specified candidate directory with a comparison target directory to determine if and how they might be related.
    /// @param [in] candidateDirectory Directory to compare.
    /// @param [in] comparisonTargetDirectory Comparison target directory, which is typically associated with a filesystem rule object.
    /// @return Result of the comparison. See #EDirectoryCompareResult documentation for more information.
    static FilesystemRule::EDirectoryCompareResult DirectoryCompareInternal(std::wstring_view candidateDirectory, std::wstring_view comparisonTargetDirectory)
    {
        if (candidateDirectory.length() == comparisonTargetDirectory.length())
        {
            // Lengths are the same, so the two could be equal if they are related at all.

            if (candidateDirectory == comparisonTargetDirectory)
                return FilesystemRule::EDirectoryCompareResult::Equal;
        }
        else if (candidateDirectory.length() < comparisonTargetDirectory.length())
        {
            // Candidate directory is shorter, so the candidate could be an ancestor or the immediate parent of the comparison target.
            // These two situations can be distinguished based on whether or not the non-matching suffix in the comparison target contains more than one backslash character.

            if ((true == comparisonTargetDirectory.starts_with(candidateDirectory)) && (L'\\' == comparisonTargetDirectory[candidateDirectory.length()]))
            {
                comparisonTargetDirectory.remove_prefix(candidateDirectory.length());

                if (0 == comparisonTargetDirectory.find_last_of(L'\\'))
                    return FilesystemRule::EDirectoryCompareResult::CandidateIsParent;
                else
                    return FilesystemRule::EDirectoryCompareResult::CandidateIsAncestor;
            }
        }
        else
        {
            // Comparison target is shorter, so the candidate could be a descendant or the immediate child of the comparison target.
            // These two situations can be distinguished based on whether or not the non-matching suffix in the candidate directory contains more than one backslash character.

            if ((true == candidateDirectory.starts_with(comparisonTargetDirectory)) && (L'\\' == candidateDirectory[comparisonTargetDirectory.length()]))
            {
                candidateDirectory.remove_prefix(comparisonTargetDirectory.length());

                if (0 == candidateDirectory.find_last_of(L'\\'))
                    return FilesystemRule::EDirectoryCompareResult::CandidateIsChild;
                else
                    return FilesystemRule::EDirectoryCompareResult::CandidateIsDescendant;
            }
        }

        return FilesystemRule::EDirectoryCompareResult::Unrelated;
    }

    /// Determines if the specified filename matches any of the specified file patterns.
    /// Input filename must not contain any backslash separators, as it is intended to represent a file within a directory rather than a path.
    /// @param [in] candidateFileName File name to check for matches with any file pattern.
    /// @param [in] kFilePatterns Patterns against which to compare the candidate file name.
    /// @return `true` if any file pattern produces a match, `false` otherwise.
    static bool FileNameMatchesAnyPatternInternal(const wchar_t* candidateFileName, const std::vector<std::wstring_view>& kFilePatterns)
    {
        if (true == kFilePatterns.empty())
            return true;

        for (const auto& kFilePattern : kFilePatterns)
        {
            if (TRUE == PathMatchSpec(candidateFileName, kFilePattern.data()))
                return true;
        }

        return false;
    }

    /// Computes and returns the result of redirecting from the specified candidate path from one directory to another.
    /// Input candidate path is split into two parts: the directory part, which identifies the absolute directory in which the file is located, and the file part, which identifies the file within its directory.
    /// If the source directory matches the candidate path and a file pattern matches then a redirection can occur to the destination directory.
    /// Otherwise no redirection occurs and no output is produced.
    /// @param [in] candidatePathDirectoryPart Directory portion of the candidate path, which is an absolute path and does not contain a trailing backslash.
    /// @param [in] candidatePathFilePart File portion of the candidate path without any leading backslash. Must be null-terminated.
    /// @return Redirected location as an absolute path, if redirection occurred successfully.
    static std::optional<TemporaryString> RedirectPathInternal(std::wstring_view candidatePathDirectoryPart, const wchar_t* candidatePathFilePart, std::wstring_view fromDirectory, std::wstring_view toDirectory, const std::vector<std::wstring_view>& kFilePatterns)
    {
        if (false == Strings::EqualsCaseInsensitive(fromDirectory, candidatePathDirectoryPart))
            return std::nullopt;

        if (false == FileNameMatchesAnyPatternInternal(candidatePathFilePart, kFilePatterns))
            return std::nullopt;

        TemporaryString redirectedPath;
        redirectedPath << toDirectory << L'\\' << candidatePathFilePart;

        return redirectedPath;
    }


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
        targetDirectoryFullPath.UnsafeSetSize(GetFullPathName(maybeTargetDirectoryResolvedString.Value().c_str(), targetDirectoryFullPath.Capacity(), targetDirectoryFullPath.Data(), nullptr));
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

    bool FilesystemRule::IsValidFilePatternString(std::wstring_view candidateFilePattern)
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
    // See "FilesystemRule.h" for documentation.

    FilesystemRule::EDirectoryCompareResult FilesystemRule::DirectoryCompareWithOrigin(std::wstring_view candidateDirectory) const
    {
        return DirectoryCompareInternal(candidateDirectory, kOriginDirectory);
    }

    // --------

    FilesystemRule::EDirectoryCompareResult FilesystemRule::DirectoryCompareWithTarget(std::wstring_view candidateDirectory) const
    {
        return DirectoryCompareInternal(candidateDirectory, kTargetDirectory);
    }

    // --------

    bool FilesystemRule::FileNameMatchesAnyPattern(const wchar_t* candidateFileName) const
    {
        return FileNameMatchesAnyPatternInternal(candidateFileName, kFilePatterns);
    }

    // --------

    std::optional<TemporaryString> FilesystemRule::RedirectPathOriginToTarget(std::wstring_view candidatePathDirectoryPart, const wchar_t* candidatePathFilePart) const
    {
        return RedirectPathInternal(candidatePathDirectoryPart, candidatePathFilePart, kOriginDirectory, kTargetDirectory, kFilePatterns);
    }

    // --------

    std::optional<TemporaryString> FilesystemRule::RedirectPathTargetToOrigin(std::wstring_view candidatePathDirectoryPart, const wchar_t* candidatePathFilePart) const
    {
        return RedirectPathInternal(candidatePathDirectoryPart, candidatePathFilePart, kTargetDirectory, kOriginDirectory, kFilePatterns);
    }
}
