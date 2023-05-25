/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemRule.cpp
 *   Implementation of filesystem redirection rule functionality.
 *****************************************************************************/

#include "FilesystemRule.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <optional>
#include <shlwapi.h>
#include <string_view>
#include <vector>


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

            if (Strings::EqualsCaseInsensitive(candidateDirectory, comparisonTargetDirectory))
                return FilesystemRule::EDirectoryCompareResult::Equal;
        }
        else if (candidateDirectory.length() < comparisonTargetDirectory.length())
        {
            // Candidate directory is shorter, so the candidate could be an ancestor or the immediate parent of the comparison target.
            // These two situations can be distinguished based on whether or not the non-matching suffix in the comparison target contains more than one backslash character.

            if ((true == Strings::StartsWithCaseInsensitive(comparisonTargetDirectory, candidateDirectory)) && (L'\\' == comparisonTargetDirectory[candidateDirectory.length()]))
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

            if ((true == Strings::StartsWithCaseInsensitive(candidateDirectory, comparisonTargetDirectory)) && (L'\\' == candidateDirectory[comparisonTargetDirectory.length()]))
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

    /// Determines the position of the final separator character in a filesystem path.
    /// @param [in] path Path for which the final separator position is desired.
    /// @return Index of the final separator character, or -1 if no final separator character exists.
    static inline size_t FinalSeparatorPosition(std::wstring_view path)
    {
        return path.find_last_of(L'\\');
    }

    /// Determines if the specified filename matches any of the specified file patterns.
    /// Input filename must not contain any backslash separators, as it is intended to represent a file within a directory rather than a path.
    /// @param [in] candidateFileName File name to check for matches with any file pattern. Must be null-terminated.
    /// @param [in] filePatterns Patterns against which to compare the candidate file name. Must be null-terminated.
    /// @return `true` if any file pattern produces a match, `false` otherwise.
    static bool FileNameMatchesAnyPatternInternal(std::wstring_view candidateFileName, const std::vector<std::wstring>& filePatterns)
    {
        if (true == filePatterns.empty())
            return true;

        for (const auto& filePattern : filePatterns)
        {
            if (TRUE == PathMatchSpec(candidateFileName.data(), filePattern.data()))
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
    /// @param [in] fromDirectory Origin directory of the redirection. Typically this comes from a filesystem rule.
    /// @param [in] toDirectory Target directory of the redirection. Typically this comes from a filesystem rule.
    /// @return Redirected location as an absolute path, if redirection occurred successfully.
    static std::optional<TemporaryString> RedirectPathInternal(std::wstring_view candidatePathDirectoryPart, std::wstring_view candidatePathFilePart, std::wstring_view fromDirectory, std::wstring_view toDirectory, const std::vector<std::wstring>& kFilePatterns)
    {
        switch (DirectoryCompareInternal(candidatePathDirectoryPart, fromDirectory))
        {
        case FilesystemRule::EDirectoryCompareResult::Equal:

            // Candidate directory and origin directory are equal. This is the simplest case.
            // If the candidate file part matches the redirection file patterns then a redirection can occur.
            // For example, if we are asked to redirect "C:\Dir1\file.txt" from "C:\Dir1" to "C:\Dir5000" then the result would be "C:\Dir5000\file.txt" but only if "file.txt" matches the file patterns for redirection.

            if (false == FileNameMatchesAnyPatternInternal(candidatePathFilePart, kFilePatterns))
                return std::nullopt;
            break;

        case FilesystemRule::EDirectoryCompareResult::CandidateIsChild:
        case FilesystemRule::EDirectoryCompareResult::CandidateIsDescendant:

            // Candidate directory is a descendent of the origin directory. This case is slightly more complicated.
            // Since an entire directory hierarchy could be involved, we need to extract the part of the candidate directory that represents the immediate child of the origin directory.
            // If that immediate child matches a redirection file pattern (here we ignore the candidate path file part) then the redirection can occur.
            // For example, if we are asked to redirect "C:\Dir1\Dir2\file.txt" from "C:\Dir1" to "C:\Dir5000" then the result would be "C:\Dir5000\Dir2\file.txt" but only if "Dir2" matches the file patterns for redirection.

            do {
                std::wstring_view immediateChildOfFromDirectory(candidatePathDirectoryPart);
                immediateChildOfFromDirectory.remove_prefix(1 + fromDirectory.length());
                immediateChildOfFromDirectory.remove_suffix(immediateChildOfFromDirectory.length() - immediateChildOfFromDirectory.find_first_of(L'\\'));

                if (false == FileNameMatchesAnyPatternInternal(immediateChildOfFromDirectory, kFilePatterns))
                    return std::nullopt;
            } while (false);
            break;

        default:
            return std::nullopt;
        }

        candidatePathDirectoryPart.remove_prefix(fromDirectory.length());

        TemporaryString redirectedPath;
        redirectedPath << toDirectory << candidatePathDirectoryPart << L'\\' << candidatePathFilePart;

        return redirectedPath;
    }


    // -------- CONSTRUCTION AND DESTRUCTION ------------------------------- //
    // See "FilesystemRule.h" for documentation.

    FilesystemRule::FilesystemRule(std::wstring&& originDirectoryFullPath, std::wstring&& targetDirectoryFullPath, std::vector<std::wstring>&& filePatterns) : kOriginDirectorySeparator(FinalSeparatorPosition(originDirectoryFullPath)), kTargetDirectorySeparator(FinalSeparatorPosition(targetDirectoryFullPath)), kOriginDirectoryFullPath(std::move(originDirectoryFullPath)), kTargetDirectoryFullPath(std::move(targetDirectoryFullPath)), kFilePatterns(std::move(filePatterns))
    {
        // Nothing to do here.
    }

    // --------

    FilesystemRule::FilesystemRule(std::wstring_view originDirectoryFullPath, std::wstring_view targetDirectoryFullPath, std::vector<std::wstring>&& filePatterns) : FilesystemRule(std::wstring(originDirectoryFullPath), std::wstring(targetDirectoryFullPath), std::move(filePatterns))
    {
        // Nothing to do here.
    }

    // --------

    FilesystemRule::FilesystemRule(std::wstring_view originDirectoryFullPath, std::wstring_view targetDirectoryFullPath, const std::vector<std::wstring>& filePatterns) : FilesystemRule(std::wstring(originDirectoryFullPath), std::wstring(targetDirectoryFullPath), std::vector<std::wstring>(filePatterns))
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

    bool FilesystemRule::FileNameMatchesAnyPattern(std::wstring_view candidateFileName) const
    {
        return FileNameMatchesAnyPatternInternal(candidateFileName, kFilePatterns);
    }

    // --------

    std::optional<TemporaryString> FilesystemRule::RedirectPathOriginToTarget(std::wstring_view candidatePathDirectoryPart, std::wstring_view candidatePathFilePart) const
    {
        return RedirectPathInternal(candidatePathDirectoryPart, candidatePathFilePart, kOriginDirectoryFullPath, kTargetDirectoryFullPath, kFilePatterns);
    }

    // --------

    std::optional<TemporaryString> FilesystemRule::RedirectPathTargetToOrigin(std::wstring_view candidatePathDirectoryPart, std::wstring_view candidatePathFilePart) const
    {
        return RedirectPathInternal(candidatePathDirectoryPart, candidatePathFilePart, kTargetDirectoryFullPath, kOriginDirectoryFullPath, kFilePatterns);
    }
}
