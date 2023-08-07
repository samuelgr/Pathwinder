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

#include "FilesystemInstruction.h"
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
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Enumerates the different modes of file operations that an application can request.
        enum class EFileOperationMode
        {
            CreateNewFile,                                                  ///< Application has requested that a new file be created. The system call will fail if the file already exists.
            OpenExistingFile,                                               ///< Application has requested that an existing file be opened. The system call will fail if the file does not exist.
            CreateNewOrOpenExistingFile,                                    ///< Application has requested that the file be opened if it exists or be created as a new file if it does not exist.
            Count                                                           ///< Not used as a value. Identifies the number of enumerators present in this enumeration.
        };


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
            const auto ruleNode = originDirectoryIndex.Find(ruleOriginDirectoryFullPath);
            if (nullptr == ruleNode)
                return nullptr;

            return ruleNode->GetData();
        }

        /// Generates an instruction for how to execute a directory enumeration, which involves listing the contents of a directory.
        /// Directory enumeration operations operate on directory handles that were previously opened by a file operation, which would itself have been executed under control of another method of this class.
        /// Arbitrary inputs are not well-supported by this method as an implementation simplification. Both supplied paths must be related to a filesystem rule in some way, either part of the origin hierarchy or the target hierarchy.
        /// @param [in] associatedPath Path associated internally with the open directory handle.
        /// @param [in] realOpenedPath Path actually submitted to the system when the directory handle was opened.
        /// @param [in] enumerationQueryFilePattern Optional file pattern provided along with the directory enumeration query. This would be obtained from the application's query and has the effect of limiting the output of the query to only those filenames that match. Optional and defaults to an empty file pattern, which matches all filenames.
        /// @return Instruction that provides information on how to execute the directory enumeration.
        DirectoryEnumerationInstruction GetInstructionForDirectoryEnumeration(std::wstring_view associatedPath, std::wstring_view realOpenedPath, std::wstring_view enumerationQueryFilePattern = std::wstring_view()) const;

        /// Generates an instruction for how to execute a file operation, such as opening, creating, or querying information about an individual file.
        /// @param [in] absoluteFilePath Path of the file being queried for possible redirection. A Windows namespace prefix is optional.
        /// @param [in] fileOperationMode Type of file operation requested by the application.
        /// @return Instruction that provides information on how to execute the file operation redirection.
        FileOperationInstruction GetInstructionForFileOperation(std::wstring_view absoluteFilePath, EFileOperationMode fileOperationMode) const;

        /// Determines if the specified file path, which is already absolute, exists as a valid prefix for any filesystem rule.
        /// The input file path must not contain any leading Windows namespace prefixes and must not have any trailing backslash characters.
        /// Primarily intended for internal use but exposed for tests.
        /// @param [in] absoluteFilePathTrimmed Absolute path to check.
        /// @return `true` if a rule exists either at or as a descendent in the filesystem hierarchy of the specified path, `false` otherwise.
        inline bool IsPrefixForAnyRule(std::wstring_view absoluteFilePathTrimmed) const
        {
            return originDirectoryIndex.HasPathForPrefix(absoluteFilePathTrimmed);
        }

        /// Determines which rule from among those held by this object should be used for a particular input path.
        /// Primarily intended for internal use but exposed for tests.
        /// @param [in] absolutePath Absolute path for which to search for a rule for possible redirection.
        /// @return Pointer to the rule object that should be used with the specified path, or `nullptr` if no rule is applicable.
        const FilesystemRule* SelectRuleForPath(std::wstring_view absolutePath) const;
    };
}
