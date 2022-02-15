/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file FilesystemRule.h
 *   Declaration of objects that represent filesystem redirection rules.
 *****************************************************************************/

#pragma once

#include "TemporaryBuffer.h"
#include "ValueOrError.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>


namespace Pathwinder
{
    /// Holds all of the data needed to represent a single filesystem redirection rule.
    /// Implements all of the behavior needed to determine whether and how paths are covered by the rule.
    class FilesystemRule
    {
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Enumerates the possible results of comparing a directory with either the origin or target directory associated with a filesystem rule.
        enum class EDirectoryCompareResult
        {
            Equal,                                                          ///< Candidate directory is exactly equal to the comparison target directory.
            Unrelated,                                                      ///< Candidate directory is not related to the comparison target directory. Paths diverge, and one is not an ancestor or descendant of the other.
            CandidateIsParent,                                              ///< Candidate directory is the immediate parent of the comparison target directory.
            CandidateIsChild,                                               ///< Candidate directory is the immediate child of the comparison target directory.
            CandidateIsAncestor,                                            ///< Candidate directory is an ancestor of the comparison target directory. In other words it is not the immediate parent but it exists higher up in the hierarchy.
            CandidateIsDescendant                                           ///< Candidate directory is a descendant of the comparison target directory. In other words it is not the immediate child but it exists lower down in the hierarchy.
        };

    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Origin or source directory.
        /// From the application's point of view, this is where files covered by this rule appear to exist.
        /// Must be an absolute path, not contain any wildcards, and not end in backslash.
        const std::wstring kOriginDirectory;

        /// Target or destination directory.
        /// This is where files covered by this rule actually exist on the filesystem.
        /// Must be an absolute path, not contain any wildcards, and not end in backslash.
        const std::wstring kTargetDirectory;

        /// Pattern that specifies which files within the origin and target directories are affected by this rule.
        /// Can be used to filter this rule to apply to only specific named files.
        /// If empty, it is assumed that there is no filter and therefore the rule applies to all files in the origin and target directories.
        const std::vector<std::wstring_view> kFilePatterns;


        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Requires all instance variables be set at construction time.
        /// Uses copy semantics for origin and target directory strings.
        /// Not intended to be invoked externally. Objects of this type should be created using a factory method.
        inline FilesystemRule(std::wstring_view originDirectory, std::wstring_view targetDirectory, std::vector<std::wstring_view>&& filePatterns) : kOriginDirectory(originDirectory), kTargetDirectory(targetDirectory), kFilePatterns(std::move(filePatterns))
        {
            // Nothing to do here.
        }

        /// Initialization constructor.
        /// Requires all instance variables be set at construction time.
        /// Uses move semantics for origin and target directory strings.
        /// Not intended to be invoked externally. Objects of this type should be created using a factory method.
        inline FilesystemRule(std::wstring&& originDirectory, std::wstring&& targetDirectory, std::vector<std::wstring_view>&& filePatterns) : kOriginDirectory(std::move(originDirectory)), kTargetDirectory(std::move(targetDirectory)), kFilePatterns(std::move(filePatterns))
        {
            // Nothing to do here.
        }


    public:
        // -------- OPERATORS ---------------------------------------------- //

        /// Simple check for equality.
        /// Primarily useful during testing.
        /// @param [in] other Object with which to compare.
        /// @return `true` if this object is equal to the other object, `false` otherwise.
        inline bool operator==(const FilesystemRule& other) const = default;


        // -------- CLASS METHODS ------------------------------------------ //

        /// Attempts to create a filesystem rule object using the given origin directory, target directory, and file patterns.
        /// @param [in] originDirectory Origin directory string, which may contain embedded references to be resolved.
        /// @param [in] targetDirectory Target directory string, which may contain embedded references to be resolved.
        /// @param [in] filePatterns File patterns to restrict the scope of the rule, defaults to matching all files in the origin and target directories.
        /// @return Filesystem rule object if successful, error message explaining the failure otherwise.
        static ValueOrError<FilesystemRule, std::wstring> Create(std::wstring_view originDirectory, std::wstring_view targetDirectory, std::vector<std::wstring_view>&& filePatterns = std::vector<std::wstring_view>());

        /// Checks if the specified candidate directory string is valid for use as an origin or a target directory.
        /// It must not be empty, must not contain any disallowed characters, and must not end in a backslash.
        /// Intended for internal use but exposed for testing.
        /// @param [in] candidateDirectory Directory string to check.
        /// @return `true` if the directory string is valid, `false` otherwise.
        static bool IsValidDirectoryString(std::wstring_view candidateDirectory);

        /// Checks if the specified candidate file pattern string is valid for use as a file pattern.
        /// It must not be empty and must not contain any disallowed characters.
        /// Intended for internal use but exposed for testing.
        /// @param [in] candidateFilePattern File pattern string to check.
        /// @return `true` if the directory string is valid, `false` otherwise.
        static bool IsValidFilePatternString(std::wstring_view candidateFilePattern);


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Compares the specified directory with the origin directory associated with this object.
        /// @param [in] candidateDirectory Directory to compare with the origin directory.
        /// @return Result of the comparison. See #EDirectoryCompareResult documentation for more information.
        EDirectoryCompareResult DirectoryCompareWithOrigin(std::wstring_view candidateDirectory) const;

        /// Compares the specified directory with the target directory associated with this object.
        /// @param [in] candidateDirectory Directory to compare with the origin directory.
        /// @return Result of the comparison. See #EDirectoryCompareResult documentation for more information.
        EDirectoryCompareResult DirectoryCompareWithTarget(std::wstring_view candidateDirectory) const;

        /// Determines if the specified filename matches any of the file patterns associated with this object.
        /// Input filename must not contain any backslash separators, as it is intended to represent a file within a directory rather than a path.
        /// @param [in] candidateFileName File name to check for matches with any file pattern.
        /// @return `true` if any file pattern produces a match, `false` otherwise.
        bool FileNameMatchesAnyPattern(const wchar_t* candidateFileName) const;

        /// Computes and returns the result of redirecting from the specified candidate path to the target directory associated with this rule.
        /// Input candidate path is split into two parts: the directory part, which identifies the absolute directory in which the file is located, and the file part, which identifies the file within its directory.
        /// If the origin directory matches the candidate path's directory part and a file pattern matches the candidate path's file part then a redirection can occur to the target directory.
        /// Otherwise no redirection occurs and no output is produced.
        /// @param [in] candidatePathDirectoryPart Directory portion of the candidate path, which is an absolute path and does not contain a trailing backslash. Does not need to be null-terminated.
        /// @param [in] candidatePathFilePart File portion of the candidate path without any leading backslash. Must be null-terminated.
        /// @return Redirected location as an absolute path, if redirection occurred successfully.
        std::optional<TemporaryString> RedirectPathOriginToTarget(std::wstring_view candidatePathDirectoryPart, const wchar_t* candidatePathFilePart) const;

        /// Computes and returns the result of redirecting from the specified candidate path to the origin directory associated with this rule.
        /// Input candidate path is split into two parts: the directory part, which identifies the absolute directory in which the file is located, and the file part, which identifies the file within its directory.
        /// If the target directory matches the candidate path's directory part and a file pattern matches the candidate path's file part then a redirection can occur to the origin directory.
        /// Otherwise no redirection occurs and no output is produced.
        /// @param [in] candidatePathDirectoryPart Directory portion of the candidate path, which is an absolute path and does not contain a trailing backslash.
        /// @param [in] candidatePathFilePart File portion of the candidate path without any leading backslash. Must be null-terminated.
        /// @return Redirected location as an absolute path, if redirection occurred successfully.
        std::optional<TemporaryString> RedirectPathTargetToOrigin(std::wstring_view candidatePathDirectoryPart, const wchar_t* candidatePathFilePart) const;
    };
}
