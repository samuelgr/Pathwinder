/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemRule.h
 *   Declaration of objects that represent filesystem redirection rules.
 *****************************************************************************/

#pragma once

#include "TemporaryBuffer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>


namespace Pathwinder
{
    /// Holds all of the data needed to represent a single filesystem redirection rule.
    /// Implements all of the behavior needed to determine whether and how paths are covered by the rule.
    /// From the application's point of view, the origin directory is where files covered by each rule appear to exist, and the target directory is where they actually exist.
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
            CandidateIsAncestor,                                            ///< Candidate directory is an ancestor, but not the immediate parent, of the comparison target directory.
            CandidateIsDescendant                                           ///< Candidate directory is a descendant, but not the immediate child, of the comparison target directory.
        };

    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Name of this filesystem rule.
        /// Not used internally for any specific purpose but rather intended as a convenience for rules that are contained in a data structure that identifies them by name.
        std::wstring_view name;

        /// Position within the origin directory absolute path of the final separator between name and parent path.
        /// Initialized using the contents of the origin directory path string and must be declared before it.
        size_t originDirectorySeparator;

        /// Position within the target directory absolute path of the final separator between name and parent path.
        /// Initialized using the contents of the target directory path string and must be declared before it.
        size_t targetDirectorySeparator;

        /// Absolute path to the origin directory.
        std::wstring originDirectoryFullPath;

        /// Absolute path to the target directory.
        std::wstring targetDirectoryFullPath;

        /// Pattern that specifies which files within the origin and target directories are affected by this rule.
        /// Can be used to filter this rule to apply to only specific named files.
        /// If empty, it is assumed that there is no filter and therefore the rule applies to all files in the origin and target directories.
        std::vector<std::wstring> filePatterns;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Requires all instance variables be set at construction time.
        /// File patterns are optional, with default behavior matching all files. This is preferred over a single-element vector containing "*" because file pattern match checking can be skipped entirely.
        FilesystemRule(std::wstring_view originDirectoryFullPath, std::wstring_view targetDirectoryFullPath, std::vector<std::wstring>&& filePatterns = std::vector<std::wstring>());


        // -------- OPERATORS ---------------------------------------------- //

        /// Simple check for equality.
        /// Primarily useful during testing.
        /// @param [in] other Object with which to compare.
        /// @return `true` if this object is equal to the other object, `false` otherwise.
        inline bool operator==(const FilesystemRule& other) const = default;


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
        /// @param [in] candidateFileName File name to check for matches with any file pattern. Must be null-terminated.
        /// @return `true` if any file pattern produces a match, `false` otherwise.
        bool FileNameMatchesAnyPattern(std::wstring_view candidateFileName) const;

        /// Retrieves and returns the name of this filesystem rule.
        /// @return Name of this filesystem rule, or an empty view if no name has been set.
        inline std::wstring_view GetName(void) const
        {
            return name;
        }

        /// Provides read-only access to the file patterns associated with this rule object.
        /// @return Read-only reference to the file patterns data structure.
        inline const std::vector<std::wstring>& GetFilePatterns(void) const
        {
            return filePatterns;
        }

        /// Retrieves and returns the full path of the origin directory associated with this rule.
        /// @return Full path of the origin directory.
        inline std::wstring_view GetOriginDirectoryFullPath(void) const
        {
            return originDirectoryFullPath;
        }

        /// Retrieves and returns the name of the origin directory associated with this rule.
        /// This is otherwise known as the relative path of the origin directory within its parent.
        /// @return Name of the origin directory.
        inline std::wstring_view GetOriginDirectoryName(void) const
        {
            std::wstring_view originDirectoryName = originDirectoryFullPath;

            if (std::wstring_view::npos != originDirectorySeparator)
                originDirectoryName.remove_prefix(1 + originDirectorySeparator);

            return originDirectoryName;
        }

        /// Retrieves and returns the immediate parent directory of the origin directory associated with this rule.
        /// @return Parent of the origin directory.
        inline std::wstring_view GetOriginDirectoryParent(void) const
        {
            if (std::wstring_view::npos == originDirectorySeparator)
                return std::wstring_view();

            std::wstring_view originDirectoryParent = originDirectoryFullPath;
            originDirectoryParent.remove_suffix(originDirectoryParent.length() - originDirectorySeparator);

            return originDirectoryParent;
        }

        /// Retrieves and returns the full path of the target directory associated with this rule.
        /// @return Full path of the target directory.
        inline std::wstring_view GetTargetDirectoryFullPath(void) const
        {
            return targetDirectoryFullPath;
        }

        /// Retrieves and returns the name of the target directory associated with this rule.
        /// This is otherwise known as the relative path of the target directory within its parent.
        /// @return Name of the target directory.
        inline std::wstring_view GetTargetDirectoryName(void) const
        {
            std::wstring_view targetDirectoryName = targetDirectoryFullPath;

            if (std::wstring_view::npos != targetDirectorySeparator)
                targetDirectoryName.remove_prefix(1 + targetDirectorySeparator);

            return targetDirectoryName;
        }

        /// Retrieves and returns the immediate parent directory of the target directory associated with this rule.
        /// @return Parent of the target directory.
        inline std::wstring_view GetTargetDirectoryParent(void) const
        {
            if (std::wstring_view::npos == originDirectorySeparator)
                return std::wstring_view();

            std::wstring_view targetDirectoryParent = targetDirectoryFullPath;
            targetDirectoryParent.remove_suffix(targetDirectoryParent.length() - targetDirectorySeparator);

            return targetDirectoryParent;
        }

        /// Determines if the specified path is in scope for redirection by this rule.
        /// A path is considered "in scope" for redirection if it is a descendant of this rule's origin directory and the immediate child part of the specified path (the part right after the origin directory of this rule) matches this rule's file patterns.
        /// A path is explicitly not considered "in scope" for redirection if it is exactly equal to this rule's origin directory.
        /// @param [in] candidatePath Path to check for being in scope for redirection by this rule.
        /// @return `true` if the specified path is in scope for redireciton by this rule, `false` if not.
        bool IsPathInScope(std::wstring_view candidatePath) const;

        /// Computes and returns the result of redirecting from the specified candidate path to the target directory associated with this rule.
        /// Input candidate path is split into two parts: the directory part, which identifies the absolute directory in which the file is located, and the file part, which identifies the file within its directory.
        /// If the origin directory matches the candidate path's directory part and a file pattern matches the candidate path's file part then a redirection can occur to the target directory.
        /// Otherwise no redirection occurs and no output is produced.
        /// @param [in] candidatePathDirectoryPart Directory portion of the candidate path, which is an absolute path and does not contain a trailing backslash. Does not need to be null-terminated.
        /// @param [in] candidatePathFilePart File portion of the candidate path without any leading backslash. Must be null-terminated.
        /// @param [in] namespacePrefix Windows namespace prefix to be inserted at the beginning of the output. This parameter is optional and defaults to the empty string.
        /// @param [in] extraSuffix Extra suffix to be added to the end of the output. This parameter is optional and defaults to the empty string.
        /// @return Redirected location as an absolute path, if redirection occurred successfully.
        std::optional<TemporaryString> RedirectPathOriginToTarget(std::wstring_view candidatePathDirectoryPart, std::wstring_view candidatePathFilePart, std::wstring_view namespacePrefix = L"", std::wstring_view extraSuffix = L"") const;

        /// Computes and returns the result of redirecting from the specified candidate path to the origin directory associated with this rule.
        /// Input candidate path is split into two parts: the directory part, which identifies the absolute directory in which the file is located, and the file part, which identifies the file within its directory.
        /// If the target directory matches the candidate path's directory part and a file pattern matches the candidate path's file part then a redirection can occur to the origin directory.
        /// Otherwise no redirection occurs and no output is produced.
        /// @param [in] candidatePathDirectoryPart Directory portion of the candidate path, which is an absolute path and does not contain a trailing backslash.
        /// @param [in] candidatePathFilePart File portion of the candidate path without any leading backslash. Must be null-terminated.
        /// @param [in] namespacePrefix Windows namespace prefix to be inserted at the beginning of the output. This parameter is optional and defaults to the empty string.
        /// @param [in] extraSuffix Extra suffix to be added to the end of the output. This parameter is optional and defaults to the empty string.
        /// @return Redirected location as an absolute path, if redirection occurred successfully.
        std::optional<TemporaryString> RedirectPathTargetToOrigin(std::wstring_view candidatePathDirectoryPart, std::wstring_view candidatePathFilePart, std::wstring_view namespacePrefix = L"", std::wstring_view extraSuffix = L"") const;

        /// Sets the name of this rule.
        /// param [in] newName New name for this rule.
        inline void SetName(std::wstring_view newName)
        {
            name = newName;
        }
    };
}
