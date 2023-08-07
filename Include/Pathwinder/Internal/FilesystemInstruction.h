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

#include <array>
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
    public:
        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Enumerates the possible ways of obtaining a directory path to enumerate.
        /// Directory enumeration operations are requested using an open directory handle, which would have been subject to file operation redirection.
        /// Therefore the possible valid sources of a directory path include the path associated internally with the handle and the path actually submitted to the system and used to open the handle.
        enum class EDirectoryPathSource : uint8_t
        {
            None,                                                           ///< No directory path source. Indicates that no directory should be enumerated.
            AssociatedPath,                                                 ///< Path internally associated with the handle.
            RealOpenedPath,                                                 ///< Path actually submitted to the system call used to open the handle.
            Count                                                           ///< Not used as a value. Identifies the number of enumerators present in this enumeration.
        };

        /// Holds the information needed to describe how to enumerate a single directory as part of a larger directory enumeration operation.
        /// Immutable once constructed.
        class SingleDirectoryEnumeration
        {
        private:
            // -------- INSTANCE VARIABLES --------------------------------- //

            /// Filesystem rule to query for a file pattern match.
            /// If `nullptr` then everything is presumed to match.
            const FilesystemRule* filePatternSource;

            /// Whether or not the match result should be inverted.
            /// Only meaningful if the source filesystem rule is present.
            bool invertFilePatternMatches;

            /// Enumerator to specify how to obtain the path of the directory to be enumerated.
            EDirectoryPathSource directoryPathSource;


        public:
            // -------- CONSTRUCTION AND DESTRUCTION ----------------------- //

            /// Default constructor.
            /// Creates an enumerator that represents no enumeration to be done.
            inline SingleDirectoryEnumeration(void) : filePatternSource(nullptr), invertFilePatternMatches(false), directoryPathSource(EDirectoryPathSource::None)
            {
                // Nothing to do here.
            }

            /// Initialization constructor.
            /// Requires values for all fields.
            /// Not intended to be invoked externally. Objects should typically be created using either the default constructor or one of the suppleid factory methods.
            inline SingleDirectoryEnumeration(EDirectoryPathSource directoryPathSource, const FilesystemRule* filePatternSource, bool invertFilePatternMatches) : directoryPathSource(directoryPathSource), filePatternSource(filePatternSource), invertFilePatternMatches(invertFilePatternMatches)
            {
                // Nothing to do here.
            }


            // -------- OPERATORS ------------------------------------------ //

            /// Equality check. Primarily useful for tests.
            inline bool operator==(const SingleDirectoryEnumeration& other) const = default;


            // -------- CLASS METHODS -------------------------------------- //

            /// Creates an instance of this class that represents a no-op (i.e. not doing any directory enumeration).
            /// @return Instance of this class that represents no enumeration to be done.
            static inline SingleDirectoryEnumeration NoEnumeration(void)
            {
                return SingleDirectoryEnumeration();
            }

            /// Creates an instance of this class that unconditionally includes all filenames.
            /// @param [in] directoryPathSource Enumerator that describes how to obtain the absolute path of the directory to be enumerated.
            /// @return Instance of this class that represents an enumeration to be done and will return `true` unconditionally when the #ShouldIncludeInDirectoryEnumeration method is invoked.
            static inline SingleDirectoryEnumeration IncludeAllFilenames(EDirectoryPathSource directoryPathSource)
            {
                return SingleDirectoryEnumeration(directoryPathSource, nullptr, false);
            }

            /// Creates an instance of this class that includes only those filenames that match one of the file patterns associated with the specified rule.
            /// @param [in] directoryPathSource Enumerator that describes how to obtain the absolute path of the directory to be enumerated.
            /// @param [in] filePatternSource Pointer to the filesystem rule used to determine whether or not a filename should be included in the enumeration result. 
            /// @return Instance of this class that represents an enumeration to be done and will return `true` when the #ShouldIncludeInDirectoryEnumeration method is invoked only for those filenames that match a file pattern associated with the specified rule and `false` otherwise.
            static inline SingleDirectoryEnumeration IncludeOnlyMatchingFilenames(EDirectoryPathSource directoryPathSource, const FilesystemRule* filePatternSource)
            {
                return SingleDirectoryEnumeration(directoryPathSource, filePatternSource, false);
            }

            /// Creates an instance of this class that includes only those filenames that do not match one of the file patterns associated with the specified rule.
            /// @param [in] directoryPathSource Enumerator that describes how to obtain the absolute path of the directory to be enumerated.
            /// @param [in] filePatternSource Pointer to the filesystem rule used to determine whether or not a filename should be included in the enumeration result.
            /// @return Instance of this class that represents an enumeration to be done and will return `false` when the #ShouldIncludeInDirectoryEnumeration method is invoked with filenames that match a file pattern associated with the specified rule and `true` otherwise.
            static inline SingleDirectoryEnumeration IncludeAllExceptMatchingFilenames(EDirectoryPathSource directoryPathSource, const FilesystemRule* filePatternSource)
            {
                return SingleDirectoryEnumeration(directoryPathSource, filePatternSource, true);
            }


            // -------- INSTANCE METHODS ----------------------------------- //

            /// Retrieves and returns the enumerator that identifies the directory path source for this directory enumeration operation.
            /// @return Directory path source enumerator.
            inline EDirectoryPathSource GetDirectoryPathSource(void) const
            {
                return directoryPathSource;
            }

            /// Determines whether or not the specified filename should be included in a directory enumeration.
            /// If a filesystem rule is present then it is checked for a file pattern match and the result is either inverted or not, as appropriate.
            /// Otherwise it is presumed that there is no restriction on the files to include.
            /// @param [in] filename Filename to check for inclusion. This is the "file part" of an absolute path, so just the part after the final backslash.
            /// @return `true` if the filename should be included in the enumeration, `false` otherwise.
            inline bool ShouldIncludeInDirectoryEnumeration(std::wstring_view filename)
            {
                if (nullptr == filePatternSource)
                    return true;

                return (filePatternSource->FileNameMatchesAnyPattern(filename) != invertFilePatternMatches);
            }
        };

        /// Holds the information needed to describe how to insert a single directory name into the enumeration result as part of a larger directory enumeration operation.
        /// Immutable once constructed.
        class SingleDirectoryNameInsertion
        {
        private:
            // -------- INSTANCE VARIABLES --------------------------------- //

            /// Filesystem rule that will be queried to determine how the directory name insertion should occur.
            /// Queries would be for information about the origin and target directories.
            const FilesystemRule* filesystemRule;


        public:
            // -------- CONSTRUCTION AND DESTRUCTION ----------------------- //

            /// Initialization constructor.
            /// Requires a filesystem rule.
            inline SingleDirectoryNameInsertion(const FilesystemRule& filesystemRule) : filesystemRule(&filesystemRule)
            {
                // Nothing to do here.
            }


            // -------- OPERATORS ------------------------------------------ //

            /// Equality check. Primarily useful for tests.
            inline bool operator==(const SingleDirectoryNameInsertion& other) const = default;


            // -------- INSTANCE METHODS ----------------------------------- //

            /// Retrieves and returns the absolute path of the directory whose information should be used to fill in the non-filename fields in the relevant file information structures being supplied back to the application.
            /// @return Absolute path of the directory to use for file information.
            inline std::wstring_view DirectoryInformationSourceAbsolutePath(void) const
            {
                return filesystemRule->GetTargetDirectoryFullPath();
            }

            /// Retrieves and returns the directory part of the absolute path of the directory whose information should be used to fill in the non-filename fields in the relevant file information structures being supplied back to the application.
            /// This is otherwise known as the absolute path of the parent of the directory whose information is needed.
            /// @return Directory part of the absolute path of the directory to use for file information.
            inline std::wstring_view DirectoryInformationSourceDirectoryPart(void) const
            {
                return filesystemRule->GetTargetDirectoryParent();
            }

            /// Retrieves and returns the file part of the absolute path of the directory whose information should be used to fill in the non-filename fields in the relevant file information structures being supplied back to the application.
            /// This is otherwise known as the base name of the directory whose information is needed.
            /// @return File part of the absolute path of the directory to use for file information.
            inline std::wstring_view DirectoryInformationSourceFilePart(void) const
            {
                return filesystemRule->GetTargetDirectoryName();
            }

            /// Retrieves and returns the filename to be inserted into the enumeration results.
            /// This only affects the filename fields in the relevant file information structures being supplied back to the application.
            /// @return Name of the file to be inserted into the enumeration results.
            inline std::wstring_view FileNameToInsert(void) const
            {
                return filesystemRule->GetOriginDirectoryName();
            }
        };


    private:
        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Descriptions of how to enumerate the directories that need to be enumerated as the execution of this directory enumeration instruction.
        std::array<SingleDirectoryEnumeration, 2> directoriesToEnumerate;

        /// Base names of any directories that should be inserted into the enumeration result.
        /// These are not subject to any additional file pattern matching.
        /// If not present then no additional names need to be inserted.
        std::optional<TemporaryVector<SingleDirectoryNameInsertion>> directoryNamesToInsert;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Requires values for all fields.
        inline DirectoryEnumerationInstruction(std::array<SingleDirectoryEnumeration, 2>&& directoriesToEnumerate, std::optional<TemporaryVector<SingleDirectoryNameInsertion>>&& directoryNamesToInsert) : directoriesToEnumerate(std::move(directoriesToEnumerate)), directoryNamesToInsert(std::move(directoryNamesToInsert))
        {
            // Nothing to do here.
        }


        // -------- OPERATORS ---------------------------------------------- //

        /// Equality check. Primarily useful for tests.
        inline bool operator==(const DirectoryEnumerationInstruction& other) const = default;


        // -------- CLASS METHODS ------------------------------------------ //

        /// Creates a directory enumeration instruction that specifies to do nothing but pass through the original enumeration query without any modifications.
        /// @return Directory enumeration instruction encoded to pass the original query through to the system without modification.
        static inline DirectoryEnumerationInstruction PassThroughUnmodifiedQuery(void)
        {
            return DirectoryEnumerationInstruction(
                {
                    SingleDirectoryEnumeration::IncludeAllFilenames(EDirectoryPathSource::RealOpenedPath),
                    SingleDirectoryEnumeration::NoEnumeration()
                },
                std::nullopt);
        }

        /// Creates a directory enumeration instruction that specifies a specific set of up to two directories to enumerate in order.
        /// The enumeration result provided back to the application will include the results of enumerating all of the directories in the supplied set.
        /// @param [in] directoriesToEnumerate Directories to be enumerated in the supplied order.
        /// @return Directory enumeration instruction encoded to request enumeration of the directories in the order provided.
        static inline DirectoryEnumerationInstruction EnumerateInOrder(std::array<SingleDirectoryEnumeration, 2>&& directoriesToEnumerate)
        {
            return DirectoryEnumerationInstruction(std::move(directoriesToEnumerate), std::nullopt);
        }

        /// Creates a directory enumeration instruction that specifies a specific set of individual directory names to be inserted into the enumeration results.
        /// The enumeration result provided back to the application will be the result of the original query with all of the supplied names inserted as directories.
        /// @param [in] directoryNamesToInsert Individual directory names to be inserted into the enumeration result.
        /// @return Directory enumeration encoded to request insertion of the specific supplied directory names into the enumeration result.
        static inline DirectoryEnumerationInstruction InsertRuleOriginDirectoryNames(TemporaryVector<SingleDirectoryNameInsertion>&& directoryNamesToInsert)
        {
            return DirectoryEnumerationInstruction(
                {
                    SingleDirectoryEnumeration::IncludeAllFilenames(EDirectoryPathSource::RealOpenedPath),
                    SingleDirectoryEnumeration::NoEnumeration()
                },
                std::move(directoryNamesToInsert));
        }

        /// Creates a directory enumeration instruction that specifies a specific set of up to two directories to enumerate in order along with a set of directory names to be inserted into the enumeration result.
        /// The enumeration result provided back to the application will include the results of enumerating all of the directories in the supplied set and with the supplied names inserted as directories.
        /// @param [in] directoriesToEnumerate Directories to be enumerated in the supplied order.
        /// @param [in] directoryNamesToInsert Individual directory names to be inserted into the enumeration result.
        /// @return Directory enumeration instruction encoded to request both enumeration of the directories in the order provided and insertion of the specific supplied directory names into the enumeration result.
        static inline DirectoryEnumerationInstruction EnumerateInOrderAndInsertRuleOriginDirectoryNames(std::array<SingleDirectoryEnumeration, 2>&& directoriesToEnumerate, TemporaryVector<SingleDirectoryNameInsertion>&& directoryNamesToInsert)
        {
            return DirectoryEnumerationInstruction(std::move(directoriesToEnumerate), std::move(directoryNamesToInsert));
        }


        // -------- INSTANCE METHODS --------------------------------------- //

        /// Obtains a read-only reference to the container of directories to be enumerated.
        /// @return Read-only reference to the container of directories to be enumerated.
        inline const std::array<SingleDirectoryEnumeration, 2>& GetDirectoriesToEnumerate(void) const
        {
            return directoriesToEnumerate;
        }

        /// Obtains a read-only reference to the container of directory names to be inserted into the enumeration result.
        /// Does not check that directory names are actually present.
        /// @return Read-only reference to the container of directory names to insert.
        inline const TemporaryVector<SingleDirectoryNameInsertion>& GetDirectoryNamesToInsert(void) const
        {
            return *directoryNamesToInsert;
        }

        /// Determines if this instruction indicates that directory names should be inserted into the enumeration result.
        /// @return `true` if so, `false` if not.
        inline bool HasDirectoryNamesToInsert(void) const
        {
            return directoryNamesToInsert.has_value();
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


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Requires values for all fields.
        /// Not intended to be invoked externally. Objects should generally be created using factory methods.
        inline FileOperationInstruction(std::optional<TemporaryString>&& redirectedFilename, ETryFiles filenamesToTry, EAssociateNameWithHandle filenameHandleAssociation, BitSetEnum<EExtraPreOperation>&& extraPreOperations, std::wstring_view extraPreOperationOperand) : redirectedFilename(std::move(redirectedFilename)), filenamesToTry(filenamesToTry), filenameHandleAssociation(filenameHandleAssociation), extraPreOperations(std::move(extraPreOperations)), extraPreOperationOperand(extraPreOperationOperand)
        {
            // Nothing to do here.
        }


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
            return *redirectedFilename;
        }

        /// Checks whether or not this object has a redirected filename.
        /// @return `true` if a redirected filename is present, `false` otherwise.
        inline bool HasRedirectedFilename(void) const
        {
            return redirectedFilename.has_value();
        }
    };

#ifdef _WIN64
    static_assert(sizeof(DirectoryEnumerationInstruction::SingleDirectoryEnumeration) <= 16, "Data structure size constraint violation.");
    static_assert(sizeof(DirectoryEnumerationInstruction::SingleDirectoryNameInsertion) <= 16, "Data structure size constraint violation.");
    static_assert(sizeof(DirectoryEnumerationInstruction) <= 64, "Data structure size constraint violation.");
    static_assert(sizeof(FileOperationInstruction) <= 64, "Data structure size constraint violation.");
#else
    static_assert(sizeof(DirectoryEnumerationInstruction::SingleDirectoryEnumeration) <= 8, "Data structure size constraint violation.");
    static_assert(sizeof(DirectoryEnumerationInstruction::SingleDirectoryNameInsertion) <= 8, "Data structure size constraint violation.");
    static_assert(sizeof(DirectoryEnumerationInstruction) <= 32, "Data structure size constraint violation.");
    static_assert(sizeof(FileOperationInstruction) <= 32, "Data structure size constraint violation.");
#endif
}
