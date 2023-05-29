/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemDirector.h
 *   Declaration of objects that hold, manipulate, and apply filesystem rules.
 *****************************************************************************/

#pragma once

#include "FilesystemRule.h"
#include "PrefixIndex.h"
#include "ValueOrError.h"

#include <map>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>


namespace Pathwinder
{
    /// Holds multiple filesystem rules, ensures consistency between them, and applies them together to implement filesystem path redirection.
    class FilesystemDirector
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Flag that specifies whether or not this registry's content has been finalized.
        /// If not finalized, rules can still be added to the registry but cannot be applied to redirect paths.
        /// Once finalized, rules can no longer be added.
        bool isFinalized;

        /// Indexes all filesystem rules by origin directory prefix.
        /// Does not own filesystem rules or directory strings but rather acts as an efficient index into the filesystem rules map.
        PrefixIndex<wchar_t, FilesystemRule> originDirectories;

        /// Stores all absolute paths to target directories used by the filesystem rules contained by this registry.
        std::unordered_set<std::wstring_view> targetDirectories;

        /// Holds all filesystem rules contained within this registry. Maps from rule name to rule object.
        std::map<std::wstring, FilesystemRule, std::less<>> filesystemRules;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        FilesystemDirector(void);


        // -------- OPERATORS ---------------------------------------------- //

        /// Simple check for equality.
        /// Primarily useful during testing.
        /// @param [in] other Object with which to compare.
        /// @return `true` if this object is equal to the other object, `false` otherwise.
        inline bool operator==(const FilesystemDirector& other) const = default;


        // -------- CLASS METHODS ------------------------------------------ //

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

        /// Retrieves a read-only reference to the filesystem rules registry itself.
        /// @return Read-only reference to the container that holds all filesystem rules.
        inline const std::map<std::wstring, FilesystemRule, std::less<>>& AllFilesystemRules(void) const
        {
            return filesystemRules;
        }

        /// Attempts to create a new rule and insert it into this registry. All parameter strings must be null-terminated.
        /// Four constraints are imposed on rules as they are added to this registry object:
        /// (1) Rule name must be unique. It cannot match another existing rule in the registry object.
        /// (2) Origin and target directories are not filesystem root directories (i.e. they both have parent directories).
        /// (3) Origin directory must not already be an origin or target directory for another rule.
        /// (4) Target directory must not already be an origin directory for another rule.
        /// @param [in] ruleName Name of the new rule. Must be unique among rules.
        /// @param [in] originDirectory Origin directory for the new rule. May be relative and contain references to be resolved.
        /// @param [in] targetDirectory Target directory for the new rule. May be relative and contain references to be resolved.
        /// @param [in] filePatterns File patterns to narrow the scope of the new rule. This parameter is optional. Default behavior is to match all files in the origin and target directories.
        /// @return Pointer to the new rule on success, error message on failure.
        ValueOrError<const FilesystemRule*, TemporaryString> CreateRule(std::wstring_view ruleName, std::wstring_view originDirectory, std::wstring_view targetDirectory, std::vector<std::wstring>&& filePatterns = std::vector<std::wstring>());

        /// Attempts to finalize this registry object. Rules cannot be added or modified after this registry object is successfully finalized.
        /// Some constraints that are enforced between rules, such as relationships between directories, cannot be checked until all rules have been added.
        /// Two constraints are imposed on each filesystem rule:
        /// (1) Origin directory either exists as a real directory or does not exist at all (i.e. it does not exist as a file or some other non-directory entity type).
        /// (2) Immediate parent of the origin directory either exists as a directory or serves as the origin directory for another rule.
        /// @return Number of rules contained in the registry on success, or an error message on failure.
        ValueOrError<size_t, TemporaryString> Finalize(void);

        /// Determines if any rule in this registry uses the specified directory as its origin or target directory.
        /// @param [in] directoryFullPath Full path of the directory to check.
        /// @return `true` if so, `false` if not.
        inline bool HasDirectory(std::wstring_view directoryFullPath) const
        {
            return (HasOriginDirectory(directoryFullPath) || HasTargetDirectory(directoryFullPath));
        }

        /// Determines if any rule in this registry uses the specified directory as its origin directory.
        /// @param [in] directoryFullPath Full path of the directory to check.
        /// @return `true` if so, `false` if not.
        inline bool HasOriginDirectory(std::wstring_view directoryFullPath) const
        {
            return originDirectories.Contains(directoryFullPath);
        }

        /// Determines if any rule in this registry uses the specified directory as its target directory.
        /// @param [in] directoryFullPath Full path of the directory to check.
        /// @return `true` if so, `false` if not.
        inline bool HasTargetDirectory(std::wstring_view directoryFullPath) const
        {
            return targetDirectories.contains(directoryFullPath);
        }

        /// Specifies if this registry object has been finalized.
        /// By default registry objects are not final, meaning rules can still be inserted but not applied.
        /// Once successfully finalized, this object can be used to apply filesystem rules to perform path redirection, but no rules can be inserted.
        /// @return `true` if this object is finalized, `false` otherwise.
        inline bool IsFinalized(void) const
        {
            return isFinalized;
        }
    };
}
