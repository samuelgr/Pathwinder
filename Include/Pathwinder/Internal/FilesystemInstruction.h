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

#include "ApiBitSet.h"
#include "FilesystemRule.h"
#include "TemporaryBuffer.h"

#include <cstdint>
#include <optional>
#include <string_view>


namespace Pathwinder
{
    /// Contains all of the information needed to execute a directory enumeration complete with potential path redirection.
    /// Execution steps described by an instruction are in addition to performing the original enumeration requested by the application, with the caveat that any filenames enumerated by following this instruction must be removed from the original enumeration result.
    /// Instances of this class would typically be created by consulting filesystem rules and consumed by whatever functions interact with both the application (to receive file operation requests) and the system (to submit file operation requests).
    class DirectoryEnumerationInstruction
    {
    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Filesystem rule to be consulted for any file pattern matching that is needed when enumerating the additional directory.
        /// May be `nullptr`, in which case no file pattern matching needs to be done.
        const FilesystemRule* additionalDirectoryFilePatternSource;

        /// Absolute path of the directory whose contents should additionally be enumerated in servicing the directory enumeration request.
        /// Typically this would be the result of a filesystem rule indicating that the directory being enumerated should be redirected.
        std::optional<TemporaryString> additionalDirectoryToEnumerate;

        /// Base names of any directories that should be inserted into the enumeration result.
        /// These are not subject to any additional file pattern matching.
        /// If not present then no additional names need to be inserted.
        std::optional<TemporaryVector<std::wstring_view>> directoryNamesToInsert;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor.
        inline DirectoryEnumerationInstruction(void) = default;

        /// Initialization constructor.
        /// Requires values for all fields.
        constexpr inline DirectoryEnumerationInstruction(const FilesystemRule* additionalDirectoryFilePatternSource, std::optional<TemporaryString>&& additionalDirectoryToEnumerate, std::optional<TemporaryVector<std::wstring_view>>&& directoryNamesToInsert) : additionalDirectoryFilePatternSource(additionalDirectoryFilePatternSource), additionalDirectoryToEnumerate(std::move(additionalDirectoryToEnumerate)), directoryNamesToInsert(std::move(directoryNamesToInsert))
        {
            // Nothing to do here.
        }


        // -------- OPERATORS ---------------------------------------------- //

        /// Equality check. Primarily useful for tests.
        inline bool operator==(const DirectoryEnumerationInstruction& other) const = default;


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Obtains the absolute path to the additional directory to be enumerated as part of this directory enumeration operation.
        /// Does not check that an additional directory path is actually present.
        /// @return Absolute path to the additional directory to be enumerated.
        inline std::wstring_view GetAdditionalDirectoryToEnumerate(void) const
        {
            return additionalDirectoryToEnumerate.value().AsStringView();
        }

        /// Obtains a read-only reference to the container of directory names to be inserted into the enumeration result.
        /// Does not check that directory names are actually present.
        /// @return Read-only reference to the container of directory names to insert.
        inline const TemporaryVector<std::wstring_view>& GetDirectoryNamesToInsert(void) const
        {
            return directoryNamesToInsert.value();
        }

        /// Determines if this instruction indicates that an additional directory should be enumerated and the results inserted into the enumeration result.
        /// @return `true` if so, `false` if not.
        inline bool HasAdditionalDirectoryToEnumerate(void) const
        {
            return additionalDirectoryToEnumerate.has_value();
        }

        /// Determines if this instruction indicates that directory names should be inserted into the enumeration result.
        /// @return `true` if so, `false` if not.
        inline bool HasDirectoryNamesToInsert(void) const
        {
            return directoryNamesToInsert.has_value();
        }

        /// Determines if the specified filename should be included in the enumeration result when enumerating the contents of an additional directory.
        /// @param [in] candidateFileName File name to check for inclusion.
        /// @return `true` if the file name should be included in the enumeration result, `false` otherwise.
        inline bool ShouldEnumerateFileNameInAdditionalDirectory(std::wstring_view candidateFileName) const
        {
            if (nullptr == additionalDirectoryFilePatternSource)
                return true;

            return additionalDirectoryFilePatternSource->FileNameMatchesAnyPattern(candidateFileName);
        }
    };


    /// Contains all of the information needed to execute a file operation complete with potential path redirection.
    /// Instances of this class would typically be created by consulting filesystem rules and consumed by whatever functions interact with both the application (to receive file operation requests) and the system (to submit file operation requests).
    class FileOperationInstruction
    {
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Enumerates possible modes for submitting a file operation to the underlying system call.
        enum class ETryFiles : uint8_t
        {
            UnredirectedOnly,                                               ///< Only try submitting the unredirected filename.
            UnredirectedFirst,                                              ///< First try submitting the unredirected filename. If the operation fails, then try submitting the redirected filename.
            RedirectedFirst,                                                ///< First try submitting the redirected filename. If the operation fails, then try submitting the unredirected filename.
            RedirectedOnly,                                                 ///< Only try submitting the redirected filename.
            Count                                                           ///< Not used as a value. Identifies the number of enumerators present in this enumeration.
        };

        /// Enumerates possible ways of associating a filename with a newly-created file handle.
        enum class EAssociateNameWithHandle : uint8_t
        {
            None,                                                           ///< Do not associate any filename with the newly-created file handle. The filename used to create the handle is not interesting.
            WhicheverWasSuccessful,                                         ///< Associate with the handle whichever filename resulted in its successful creation.
            Unredirected,                                                   ///< Associate the unredirected filename with the newly-created file handle.
            Redirected,                                                     ///< Associate the redirected filename with the newly-created file handle.
            Count                                                           ///< Not used as a value. Identifies the number of enumerators present in this enumeration.
        };

        /// Possible additional operations that should be performed prior to submitting a file operation to the underlying system call.
        /// Each filesystem operation can require multiple such pre-operations, but order of execution is not important.
        enum class EExtraPreOperation : uint8_t
        {
            EnsurePathHierarchyExists,                                      ///< Ensure all directories in path hierarchy exist up to the directory that is specified as an extra operand.
            Count                                                           ///< Not used as a value. Identifies the number of enumerators present in this enumeration.
        };


    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Redirected filename. This would result from a file operation redirection query that matches a rule and ends up being redirected.
        /// If not present, then no redirection occurred.
        std::optional<TemporaryString> redirectedFilename;

        /// Filenames to try when submitting a file operation to the underlying system call.
        ETryFiles filenamesToTry;

        /// Filename to associate with a newly-created file handle that results from successful execution of the file operation.
        EAssociateNameWithHandle filenameHandleAssociation;

        /// Extra operations to perform before submitting the filesystem operation to the underlying system call.
        BitSetEnum<EExtraPreOperation> extraPreOperations;

        /// Operand to be used as a parameter for extra pre-operations.
        std::wstring_view extraPreOperationOperand;


    private:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Requires values for all fields.
        /// Not intended to be invoked externally. Objects should be created using factory methods.
        constexpr inline FileOperationInstruction(std::optional<TemporaryString>&& redirectedFilename, ETryFiles filenamesToTry, EAssociateNameWithHandle filenameHandleAssociation, BitSetEnum<EExtraPreOperation>&& extraPreOperations, std::wstring_view extraPreOperationOperand) : redirectedFilename(std::move(redirectedFilename)), filenamesToTry(filenamesToTry), filenameHandleAssociation(filenameHandleAssociation), extraPreOperations(std::move(extraPreOperations)), extraPreOperationOperand(extraPreOperationOperand)
        {
            // Nothing to do here.
        }


    public:
        // -------- OPERATORS ---------------------------------------------- //

        /// Equality check. Primarily useful for tests.
        inline bool operator==(const FileOperationInstruction& other) const = default;


        // -------- CLASS METHODS ------------------------------------------ //

        /// Creates a filesystem operation redirection instruction that indicates the request should be passed directly to the underlying system call without redirection or interception of any kind.
        /// @return File operation redirection instruction encoded to indicate that there should be no processing whatsoever.
        static inline FileOperationInstruction NoRedirectionOrInterception(void)
        {
            return FileOperationInstruction(std::nullopt, ETryFiles::UnredirectedOnly, EAssociateNameWithHandle::None, {}, std::wstring_view());
        }

        /// Creates a filesystem operation redirection instruction that indicates the request should not be redirected but should be intercepted for additional processing
        /// @param [in] filenameHandleAssociation How to associate a filename with a potentially newly-created filesystem handle.
        /// @param [in] extraPreOperations Any extra pre-operations to be performed before the file operation is attempted. Optional, defaults to none.
        /// @param [in] extraPreOperationOperand Additional operand for any extra pre-operations. Optional, defaults to none.
        /// @return File operation redirection instruction encoded to indicate some additional processing needed but without redirection.
        static inline FileOperationInstruction InterceptWithoutRedirection(EAssociateNameWithHandle filenameHandleAssociation, BitSetEnum<EExtraPreOperation>&& extraPreOperations = {}, std::wstring_view extraPreOperationOperand = std::wstring_view())
        {
            return FileOperationInstruction(std::nullopt, ETryFiles::UnredirectedOnly, filenameHandleAssociation, std::move(extraPreOperations), extraPreOperationOperand);
        }

        /// Creates a filesystem operation redirection instruction that indicates the request should be redirected.
        /// @param [in] redirectedFilename String representing the absolute redirected filename, including Windows namespace prefix.
        /// @param [in] filenameHandleAssociation How to associate a filename with a potentially newly-created filesystem handle. Optional, defaults to no association.
        /// @param [in] extraPreOperations Any extra pre-operations to be performed before the file operation is attempted. Optional, defaults to none.
        /// @param [in] extraPreOperationOperand Additional operand for any extra pre-operations. Optional, defaults to none.
        /// @return File operation redirection instruction encoded to indicate redirection plus optionally some additional processing.
        static inline FileOperationInstruction RedirectTo(TemporaryString&& redirectedFilename, EAssociateNameWithHandle filenameHandleAssociation = EAssociateNameWithHandle::None, BitSetEnum<EExtraPreOperation>&& extraPreOperations = {}, std::wstring_view extraPreOperationOperand = std::wstring_view())
        {
            return FileOperationInstruction(std::move(redirectedFilename), ETryFiles::RedirectedOnly, filenameHandleAssociation, std::move(extraPreOperations), extraPreOperationOperand);
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Retrieves and returns the set of extra pre-operations.
        /// @return Set of extra pre-operations.
        inline BitSetEnum<EExtraPreOperation> GetExtraPreOperations(void) const
        {
            return extraPreOperations;
        }

        /// Retrieves and returns the operand for extra pre-operations.
        /// @return Operand for extra pre-operations.
        inline std::wstring_view GetExtraPreOperationOperand(void) const
        {
            return extraPreOperationOperand;
        }

        /// Retrieves and returns the filenames to be tried.
        /// @return Filenames to try.
        inline ETryFiles GetFilenamesToTry(void) const
        {
            return filenamesToTry;
        }

        /// Retrieves and returns the filename to be associated with a newly-created filesystem handle.
        /// @return Filename to associate with the newly-created filesystem handle.
        inline EAssociateNameWithHandle GetFilenameHandleAssociation(void) const
        {
            return filenameHandleAssociation;
        }

        /// Retrieves and returns the redirected filename. Does not verify that such a name exists.
        /// @return Redirected filename.
        inline std::wstring_view GetRedirectedFilename(void) const
        {
            return redirectedFilename.value();
        }

        /// Checks whether or not this object has a redirected filename.
        /// @return `true` if a redirected filename is present, `false` otherwise.
        inline bool HasRedirectedFilename(void) const
        {
            return redirectedFilename.has_value();
        }
    };
}
