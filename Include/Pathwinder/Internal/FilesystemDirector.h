/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemDirector.h
 *   Declaration of objects that hold multiple filesystem rules and apply them
 *   together.
 *****************************************************************************/

#pragma once

#include "FilesystemRule.h"
#include "PrefixIndex.h"
#include "Strings.h"

#include <map>
#include <optional>
#include <variant>


namespace Pathwinder
{
    /// Holds multiple filesystem rules and applies them together to implement filesystem path redirection.
    /// Intended to be instantiated by a filesystem director builder or by tests.
    /// Rule set is immutable once this object is constructed.
    class FilesystemDirector
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Holds all filesystem rules contained within the candidate filesystem director object. Maps from rule name to rule object.
        std::map<std::wstring, FilesystemRule, std::less<>> filesystemRules;

        /// Indexes all absolute paths of origin directories used by filesystem rules.
        PrefixIndex<wchar_t, FilesystemRule> originDirectoryIndex;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        inline FilesystemDirector(void) = default;


        /// Initialization constructor.
        /// Move-constructs each individual instance variable. Intended to be invoked either by a filesystem director builder or by tests.
        inline FilesystemDirector(std::map<std::wstring, FilesystemRule, std::less<>>&& filesystemRules, PrefixIndex<wchar_t, FilesystemRule>&& originDirectoryIndex) : filesystemRules(std::move(filesystemRules)), originDirectoryIndex(std::move(originDirectoryIndex))
        {
            // Nothing to do here.
        }

        /// Copy constructor. Should never be invoked.
        FilesystemDirector(const FilesystemDirector&) = delete;

        /// Move constructor.
        inline FilesystemDirector(FilesystemDirector&& other) = default;


        // --------- OPERATORS --------------------------------------------- //

        /// Copy assignment operator. Should never be invoked.
        FilesystemDirector& operator=(const FilesystemDirector&) = delete;

        /// Move assignment operator.
        inline FilesystemDirector& operator=(FilesystemDirector&& other) = default;


        // -------- CLASS METHODS ------------------------------------------ //

        /// Returns a mutable reference to a singleton instance of this class.
        /// Not useful for tests, which instead create their own local instances of filesystem director objects.
        /// @return Mutable reference to a globally-shared filesystem director object.
        static FilesystemDirector& Singleton(void);


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Retrieves and returns the number of filesystem rules held by this object.
        /// @return Number of filesystem rules.
        inline unsigned int CountOfRules(void) const
        {
            return static_cast<unsigned int>(filesystemRules.size());
        }

        /// Searches for the specified rule by name and returns a pointer to the corresponding object, if found.
        /// @param [in] ruleName Name of the rule for which to search.
        /// @return Pointer to the rule, or `nullptr` if no matching rule is found.
        inline const FilesystemRule* FindRuleByName(std::wstring_view ruleName) const
        {
            const auto ruleIt = filesystemRules.find(ruleName);
            if (filesystemRules.cend() == ruleIt)
                return nullptr;

            return &ruleIt->second;
        }

        /// Searches for the specified rule by origin directory and returns a pointer to the corresponding object, if found.
        /// @param [in] ruleOriginDirectoryFullPath Full path of the directory for which to search.
        /// @return Pointer to the rule, or `nullptr` if no matching rule is found.
        inline const FilesystemRule* FindRuleByOriginDirectory(std::wstring_view ruleOriginDirectoryFullPath) const
        {
            const auto ruleNode = originDirectoryIndex.Find(Strings::ToLowercase(ruleOriginDirectoryFullPath));
            if (nullptr == ruleNode)
                return nullptr;

            return ruleNode->GetData();
        }

        /// Determines if the specified file path, which is already absolute and lowercase, exists as a valid prefix for any filesystem rule.
        /// @param [in] fileFullPathLowercase Absolute lowercase path to check.
        /// @return `true` if a rule exists either at or as a descendent in the filesystem hierarchy of the specified path, `false` otherwise.
        inline bool IsPrefixForAnyRule(std::wstring_view fileFullPathLowercase) const
        {
            fileFullPathLowercase.remove_prefix(Strings::PathGetWindowsNamespacePrefix(fileFullPathLowercase).length());

            while (fileFullPathLowercase.ends_with(L'\\'))
                fileFullPathLowercase.remove_suffix(1);

            return originDirectoryIndex.HasPathForPrefix(fileFullPathLowercase);
        }

        /// Determines which rule from among those held by this object should be used to redirect a single filename.
        /// This operation is useful for those filesystem functions that directly operate on a single absolute path.
        /// Primarily intended for internal use but exposed for tests.
        /// @param [in] fileFullPath Full path of the file being queried for possible redirection. Must be null-terminated.
        /// @return Pointer to the rule object that should be used to process the redirection, or `nullptr` if no redirection should occur at all.
        const FilesystemRule* SelectRuleForSingleFile(std::wstring_view fileFullPath) const;

        /// Redirects a single file by selecting an appropriate rule and then using it to change the file's full path.
        /// This operation is useful for those filesystem functions that directly operate on a single absolute path.
        /// @param [in] filePath Path of the file being queried for possible redirection. Typically supplied by an application and need not be absolute. Must be null-terminated.
        /// @return String containing the full path of the result of the redirection, if a redirection occurred.
        std::optional<TemporaryString> RedirectSingleFile(std::wstring_view filePath) const;
    };
}
