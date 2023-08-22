/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemDirectorBuilder.h
 *   Declaration of functionality for building new filesystem director objects
 *   piece-wise at runtime.
 *****************************************************************************/

#pragma once

#include "Configuration.h"
#include "FilesystemDirector.h"
#include "FilesystemRule.h"
#include "ValueOrError.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>


namespace Pathwinder
{
    /// Encapsulates all functionality for managing a partially-built filesystem director object, ensuring consistency between individual filesystem rules, and building a complete filesystem director object once all rules have been submitted.
    class FilesystemDirectorBuilder
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Indexes all absolute paths to origin directories used by filesystem rules.
        PrefixIndex<wchar_t, FilesystemRule> originDirectories;

        /// Stores all absolute paths to target directories used by filesystem rules.
        std::unordered_set<std::wstring_view> targetDirectories;

        /// Holds all filesystem rules contained within the candidate filesystem director object. Maps from rule name to rule object.
        std::map<std::wstring, FilesystemRule, std::less<>> filesystemRules;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        inline FilesystemDirectorBuilder(void) : originDirectories(L"\\"), targetDirectories(), filesystemRules()
        {
            // Nothing to do here.
        }


        // -------- CLASS METHODS ------------------------------------------ //

        /// Attempts to build a filesystem director object using a configuration data object.
        /// Extracts filesystem rules from the specified configuration data object, one rule per section identified by prefix.
        /// If any errors are encountered then log messages are generated.
        /// @param [in] Mutable reference to a configuration data object containing sections that define filesystem rules.
        /// @return Newly-created filesystem director object if successful.
        static std::optional<FilesystemDirector> BuildFromConfigurationData(Configuration::ConfigurationData& configData);

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

        /// Attempts to create a new rule and insert it into the candidate filesystem director.
        /// Four constraints are imposed on rules as they are added to this registry object:
        /// (1) Rule name must be unique. It cannot match another existing rule in the registry object.
        /// (2) Origin and target directories are not filesystem root directories (i.e. they both have parent directories).
        /// (3) Origin directory must not already be an origin or target directory for another rule.
        /// (4) Target directory must not already be an origin directory for another rule.
        /// @param [in] ruleName Name of the new rule. Must be unique among rules.
        /// @param [in] originDirectory Origin directory for the new rule. May be relative and contain references to be resolved.
        /// @param [in] targetDirectory Target directory for the new rule. May be relative and contain references to be resolved.
        /// @param [in] filePatterns File patterns to narrow the scope of the new rule. This parameter is optional. Default behavior is to match all files in the origin and target directories.
        /// @param [in] redirectMode Redirection mode enumerator for the new rule. Determines how redirections are presented to the application and which files are tried. Default behavior is to use simple redirection mode.
        /// @return Pointer to the new rule on success, error message on failure.
        ValueOrError<const FilesystemRule*, TemporaryString> AddRule(std::wstring_view ruleName, std::wstring_view originDirectory, std::wstring_view targetDirectory, std::vector<std::wstring>&& filePatterns = std::vector<std::wstring>(), FilesystemRule::ERedirectMode redirectMode = FilesystemRule::ERedirectMode::Simple);

        /// Attempts to create a new rule and insert it into the candidate filesystem director, reading settings from a configuration data section.
        /// The same constraints are imposed as when adding a rule by supplying its components manually. Internally this method extracts the rule components and adds the rule using #AddRule.
        /// @param [in] ruleName Name of the new rule. Must be unique among rulse.
        /// @param [in] configSection Configuration section data object that contains the settings that define the new rule.
        /// @return Pointer to the new rule on success, error message on failure.
        ValueOrError<const FilesystemRule*, TemporaryString> AddRuleFromConfigurationSection(std::wstring_view ruleName, Configuration::Section& configSection);

        /// Attempts to build a real filesystem director object using all of the rules added so far. Built filesystem director objects are immutable.
        /// Some constraints that are enforced between rules, such as relationships between directories, cannot be checked until all rules have been added.
        /// Once a new filesystem director object is built this builder object is invalidated and should not be used further.
        /// Some additional constraints are imposed on each filesystem rule at build time:
        /// (1) Origin directory either exists as a real directory or does not exist at all (i.e. it does not exist as a file or some other non-directory entity type).
        /// (2) Immediate parent of the origin directory either exists as a directory or serves as the origin directory for another rule.
        /// (3) Target directory hierarchies are all unrelated. In other words, no target directory has any rule's origin directory or target directory as an ancestor in the filesystem. 
        /// @return Newly-built filesystem director object on success, or an error message on failure.
        ValueOrError<FilesystemDirector, TemporaryString> Build(void);

        /// Retrieves and returns the number of filesystem rules successfully added to this object.
        /// @return Number of filesystem rules.
        inline unsigned int CountOfRules(void) const
        {
            return static_cast<unsigned int>(filesystemRules.size());
        }

        /// Determines if any rule added to this object uses the specified directory as its origin or target directory.
        /// @param [in] directoryFullPath Full path of the directory to check.
        /// @return `true` if so, `false` if not.
        inline bool HasDirectory(std::wstring_view directoryFullPath) const
        {
            return (originDirectories.Contains(directoryFullPath) || targetDirectories.contains(directoryFullPath));
        }

        /// Determines if any rule added to this object uses the specified directory as its origin directory.
        /// @param [in] directoryFullPath Full path of the directory to check.
        /// @return `true` if so, `false` if not.
        inline bool HasOriginDirectory(std::wstring_view directoryFullPath) const
        {
            return originDirectories.Contains(directoryFullPath);
        }

        /// Determines if any rule added to this object uses the specified directory as its target directory.
        /// @param [in] directoryFullPath Full path of the directory to check.
        /// @return `true` if so, `false` if not.
        inline bool HasTargetDirectory(std::wstring_view directoryFullPath) const
        {
            return targetDirectories.contains(directoryFullPath);
        }
    };
}
