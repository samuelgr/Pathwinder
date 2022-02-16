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
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <cwctype>
#include <optional>
#include <shlwapi.h>
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

    /// Extracts the file part from a filesystem path.
    /// File part is the part after the final backslash character.
    /// @param [in] path Path from which the file part is desired.
    /// @return View into the parameter string consisting of the file part. If the entire absolute path is a file part then the returned string equals the parameter.
    static std::wstring_view ExtractFilePart(std::wstring_view path)
    {
        const size_t kLastBackslashPos = path.find_last_of(L'\\');

        if (std::wstring_view::npos != kLastBackslashPos)
            path.remove_prefix(1 + kLastBackslashPos);

        return path;
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


    // -------- CONSTRUCTION AND DESTRUCTION ------------------------------- //
    // See "FilesystemRule.h" for documentation.

    FilesystemRule::FilesystemRule(std::wstring_view originDirectoryFullPath, std::wstring_view targetDirectoryFullPath, std::vector<std::wstring_view>&& filePatterns) : kOriginDirectoryFullPath(originDirectoryFullPath), kOriginDirectoryName(ExtractFilePart(originDirectoryFullPath)), kTargetDirectoryFullPath(targetDirectoryFullPath), kTargetDirectoryName(ExtractFilePart(targetDirectoryFullPath)), kFilePatterns(std::move(filePatterns))
    {
        // Nothing to do here.
    }


    // -------- INSTANCE METHODS ------------------------------------------- //
    // See "FilesystemRule.h" for documentation.

    FilesystemRule::EDirectoryCompareResult FilesystemRule::DirectoryCompareWithOrigin(std::wstring_view candidateDirectory) const
    {
        return DirectoryCompareInternal(candidateDirectory, kOriginDirectoryFullPath);
    }

    // --------

    FilesystemRule::EDirectoryCompareResult FilesystemRule::DirectoryCompareWithTarget(std::wstring_view candidateDirectory) const
    {
        return DirectoryCompareInternal(candidateDirectory, kTargetDirectoryFullPath);
    }

    // --------

    bool FilesystemRule::FileNameMatchesAnyPattern(const wchar_t* candidateFileName) const
    {
        return FileNameMatchesAnyPatternInternal(candidateFileName, kFilePatterns);
    }

    // --------

    std::optional<TemporaryString> FilesystemRule::RedirectPathOriginToTarget(std::wstring_view candidatePathDirectoryPart, const wchar_t* candidatePathFilePart) const
    {
        return RedirectPathInternal(candidatePathDirectoryPart, candidatePathFilePart, kOriginDirectoryFullPath, kTargetDirectoryFullPath, kFilePatterns);
    }

    // --------

    std::optional<TemporaryString> FilesystemRule::RedirectPathTargetToOrigin(std::wstring_view candidatePathDirectoryPart, const wchar_t* candidatePathFilePart) const
    {
        return RedirectPathInternal(candidatePathDirectoryPart, candidatePathFilePart, kTargetDirectoryFullPath, kOriginDirectoryFullPath, kFilePatterns);
    }
}
