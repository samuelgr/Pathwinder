/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file FilesystemInstruction.h
 *   Declaration of data structures for representing instructions issued by
 *   filesystem director objects on how to perform a redirection operation.
 *****************************************************************************/

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <span>

#include "FilesystemRule.h"
#include "TemporaryBuffer.h"


namespace Pathwinder
{
    /// Base class for all filesystem redirection instructions.
    /// Contains all common functionality.
    class FilesystemInstruction
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Pointer to the filesystem rule object that was consulted when creating this instruction.
        /// May be `nullptr` if the instruction has no associated rule.
        const FilesystemRule* filesystemRule;


    protected:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        constexpr inline FilesystemInstruction(void) : filesystemRule()
        {
            // Nothing to do here.
        }

        /// Initialization constructor.
        /// Requires a pointer to a filesystem rule.
        constexpr inline FilesystemInstruction(const FilesystemRule* filesystemRule) : filesystemRule(filesystemRule)
        {
            // Nothing to do here.
        }


    public:
        // -------- OPERATORS ---------------------------------------------- //

        /// Simple check for equality.
        /// Primarily useful for tests.
        /// @param [in] other Object with which to compare.
        /// @return `true` if this object is equal to the other object, `false` otherwise.
        constexpr inline bool operator==(const FilesystemInstruction& other) const
        {
            if (false == HasFilesystemRule())
                return (false == other.HasFilesystemRule());
            else
                return ((true == other.HasFilesystemRule()) && (GetFilesystemRule() == other.GetFilesystemRule()));
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Determines whether or not this object has an associated filesystem rule.
        /// @return `true` if so, `false` if not.
        constexpr inline bool HasFilesystemRule(void) const
        {
            return (nullptr != filesystemRule);
        }

        /// Returns a reference to the filesystem rule object that was consulted when creating this instruction.
        /// Should only be invoked if this object actually has an associated filesystem rule.
        /// @return Read-only reference to the associated filesystem rule.
        constexpr inline const FilesystemRule& GetFilesystemRule(void) const
        {
            return *filesystemRule;
        }
    };

    /// Describes how a file operation redirection should occur.
    /// File operations refer to actions taken on individual files identified by filename, such as creating a file, opening a file, or querying information about a file.
    /// In response to a file operation there will either be one or two files to try, in order, stopping at whichever file is first found on the real filesystem.
    class FileOperationRedirectInstruction : public FilesystemInstruction
    {
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Container type for representing absolute paths to try.
        typedef std::array<std::wstring_view, 2> TAbsolutePathsContainer;


    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Absolute paths to try submitting to the underlying system call, in order from first to last.
        /// Only non-empty paths are considered "valid" and should be tried.
        TAbsolutePathsContainer absolutePathsToTry;

        /// Possible string storage buffer for one of the absolute paths to try.
        /// If present, one of the string view objects in #absolutePathsToTry will refer to it.
        std::optional<TemporaryString> maybeAbsolutePathBuffer;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        constexpr inline FileOperationRedirectInstruction(void) : absolutePathsToTry(), maybeAbsolutePathBuffer()
        {
            // Nothing to do here.
        }

    protected:
        /// Initialization constructor.
        /// Requires one or two absolute paths to try, a filesystem rule pointer, and optionally a string object to be moved into this object.
        /// For internal use only. Objects of this class should be created using the provided factory methods.
        constexpr inline FileOperationRedirectInstruction(TAbsolutePathsContainer&& absolutePathsToTry, const FilesystemRule* filesystemRule, std::optional<TemporaryString>&& maybeAbsolutePathBuffer) : FilesystemInstruction(filesystemRule), absolutePathsToTry(std::move(absolutePathsToTry)), maybeAbsolutePathBuffer(std::move(maybeAbsolutePathBuffer))
        {
            // Nothing to do here.
        }


    public:
        // -------- OPERATORS ---------------------------------------------- //

        /// Simple check for equality.
        /// Primarily useful for tests.
        /// Compares the absolute path strings only without regard for buffer ownership.
        /// @param [in] other Object with which to compare.
        /// @return `true` if this object is equal to the other object, `false` otherwise.
        constexpr inline bool operator==(const FileOperationRedirectInstruction& other) const
        {
            return (this->FilesystemInstruction::operator==(other)) && (absolutePathsToTry == other.absolutePathsToTry);
        }


        // -------- CLASS METHODS ------------------------------------------ //

        /// Convenience factory method for creating an instruction to represent no redirection taking place.
        /// This type of instruction indicates that only the original, unredirected absolute path should be tried. There is no filesystem rule associated with this type of instruction.
        /// @param [in] unredirectedAbsolutePath Absolute path of the original query.
        /// @param [in] maybeAbsolutePathBuffer Buffer that owns the original query's absolute path, if such a buffer is needed. Optional.
        /// @return Filesystem instruction representing no redirection.
        constexpr static FileOperationRedirectInstruction DoNotRedirectFrom(std::wstring_view unredirectedAbsolutePath, std::optional<TemporaryString>&& maybeAbsolutePathBuffer = std::nullopt)
        {
            return FileOperationRedirectInstruction({unredirectedAbsolutePath}, nullptr, std::move(maybeAbsolutePathBuffer));
        }

        /// Convenience factory method for creating an instruction to represent a redirection taking place.
        /// This type of instruction indicates that one or more files should be tried in order and that a filesystem rule was consulted.
        /// @param [in] absolutePathsToTry Absolute paths that should be tried in the order they appear.
        /// @param [in] filesystemRule Read-only reference to the filesystem rule that was consulted in order to make the decision to do the redirection.
        /// @param [in] maybeAbsolutePathBuffer Buffer that owns the one of the absolute path strings, if such a buffer is needed. Optional.
        constexpr static FileOperationRedirectInstruction RedirectAndTryInOrder(TAbsolutePathsContainer&& absolutePathsToTry, const FilesystemRule& filesystemRule, std::optional<TemporaryString>&& maybeAbsolutePathBuffer = std::nullopt)
        {
            return FileOperationRedirectInstruction(std::move(absolutePathsToTry), &filesystemRule, std::move(maybeAbsolutePathBuffer));
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Returns a read-only reference to the container of absolute paths to try.
        /// Intended to be used in a range-based loop for iterating over all of them.
        /// @return Read-only reference to the container of absolute paths to try, in order.
        constexpr inline const std::array<std::wstring_view, 2>& AbsolutePathsToTry(void) const
        {
            return absolutePathsToTry;
        }

        /// Determines whether or not this instruction indicates that a filesystem operation is being redirected.
        /// @return `true` if so, `false` if not.
        constexpr inline bool IsFilesystemOperationRedirected(void) const
        {
            return HasFilesystemRule();
        }
    };
}
