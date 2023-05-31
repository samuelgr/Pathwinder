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

#include <map>
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
        const std::map<std::wstring, FilesystemRule, std::less<>> filesystemRules;

        /// Indexes all absolute paths of origin directories used by filesystem rules.
        const PrefixIndex<wchar_t, FilesystemRule> originDirectoryIndex;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Move-constructs each individual instance variable. Intended to be invoked either by a filesystem director builder or by tests.
        inline FilesystemDirector(std::map<std::wstring, FilesystemRule, std::less<>>&& filesystemRules, PrefixIndex<wchar_t, FilesystemRule>&& originDirectoryIndex) : filesystemRules(std::move(filesystemRules)), originDirectoryIndex(std::move(originDirectoryIndex))
        {
            // Nothing to do here.
        }


        // -------- INSTANCE METHODS --------------------------------------- //

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
            const auto ruleNode = originDirectoryIndex.Find(ruleOriginDirectoryFullPath);
            if (nullptr == ruleNode)
                return nullptr;

            return ruleNode->GetData();
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
        /// @return String containing the full path of the result of the redirection. If no redirection occurred then the returned string is the absolute full path equivalent of the input string, unless it failed to resolve to a full path in which case the return value is a copy of the input string.
        TemporaryString RedirectSingleFile(std::wstring_view filePath) const;
    };
}
