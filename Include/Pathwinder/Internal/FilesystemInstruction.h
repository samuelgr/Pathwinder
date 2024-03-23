/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file FilesystemInstruction.h
 *   Declaration of data structures for representing instructions issued by filesystem director
 *   objects on how to perform a redirection operation.
 **************************************************************************************************/

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ApiBitSet.h"
#include "FilesystemRule.h"
#include "TemporaryBuffer.h"

namespace Pathwinder
{
  /// Possible ways of associating a filename with a newly-created file handle.
  enum class EAssociateNameWithHandle : uint8_t
  {
    /// Do not associate any filename with the newly-created file handle. The filename used
    /// to create the handle is not interesting.
    None,

    /// Associate with the handle whichever filename resulted in its successful creation.
    WhicheverWasSuccessful,

    /// Associate the unredirected filename with the newly-created file handle.
    Unredirected,

    /// Associate the redirected filename with the newly-created file handle.
    Redirected,

    /// Not used as a value. Identifies the number of enumerators present in this
    /// enumeration.
    Count
  };

  /// Possible preferences for creating a new file or opening an existing file.
  enum class ECreateDispositionPreference : uint8_t
  {
    /// No preference on creating a new file or opening an existing file. Do whatever the
    /// application requests and accept whatever is successful first.
    NoPreference,

    /// If the application is willing to create a new file or open an existing file, prefer to
    /// create a new file.
    PreferCreateNewFile,

    /// If the application is willing to create a new file or open an existing file, prefer to open
    /// an existing file.
    PreferOpenExistingFile,

    /// Not used as a value. Identifies the number of enumerators present in this enumeration.
    Count
  };

  /// Possible ways of obtaining a directory path to enumerate. Directory enumeration operations are
  /// requested using an open directory handle, which would have been subject to file operation
  /// redirection, and optionally subject to the influence of one or more filesystem rules that act
  /// as a source of filename matching via their file patterns.
  enum class EDirectoryPathSource : uint8_t
  {
    /// No directory path source. Indicates that no directory should be enumerated.
    None,

    /// Path internally associated with the handle.
    AssociatedPath,

    /// Path actually submitted to the system call used to open the handle.
    RealOpenedPath,

    /// Origin directory associated with the file pattern source.
    FilePatternSourceOriginDirectory,

    /// Target directory associated with the file pattern source.
    FilePatternSourceTargetDirectory,

    /// Not used as a value. Identifies the number of enumerators present in this enumeration.
    Count
  };

  /// Possible additional operations that should be performed prior to executing a file operation.
  /// Each filesystem operation can require multiple such pre-operations, but order of execution is
  /// not important.
  enum class EExtraPreOperation : uint8_t
  {
    /// Ensure all directories in path hierarchy exist up to the directory that is specified as an
    /// extra operand.
    EnsurePathHierarchyExists,

    /// Not used as a value. Identifies the number of enumerators present in this enumeration.
    Count
  };

  /// Possible ways of submitting filenames to the underlying system call as part of a file
  /// operation.
  enum class ETryFiles : uint8_t
  {
    /// Only try submitting the unredirected filename.
    UnredirectedOnly,

    /// First try submitting the unredirected filename. If the operation fails, then try
    /// submitting the redirected filename.
    UnredirectedFirst,

    /// First try submitting the redirected filename. If the operation fails, then try
    /// submitting the unredirected filename.
    RedirectedFirst,

    /// Only try submitting the redirected filename.
    RedirectedOnly,

    /// Not used as a value. Identifies the number of enumerators present in this
    /// enumeration.
    Count
  };

  /// Contains all of the information needed to execute a directory enumeration complete with
  /// potential path redirection. Execution steps described by an instruction are in addition to
  /// performing the original enumeration requested by the application, with the caveat that any
  /// filenames enumerated by following this instruction must be removed from the original
  /// enumeration result. Instances of this class would typically be created by consulting
  /// filesystem rules and consumed by whatever functions interact with both the application (to
  /// receive file operation requests) and the system (to submit file operation requests).
  class DirectoryEnumerationInstruction
  {
  public:

    /// Holds the information needed to describe how to enumerate a single directory as part of
    /// a larger directory enumeration operation. Immutable once constructed.
    class SingleDirectoryEnumeration
    {
    public:

      inline SingleDirectoryEnumeration(void)
          : filePatternSource(),
            filePatternMatchConfig(),
            directoryPathSource(EDirectoryPathSource::None)
      {}

      bool operator==(const SingleDirectoryEnumeration& other) const = default;

      /// Creates an instance of this class that unconditionally includes all filenames.
      /// @param [in] directoryPathSource Enumerator that describes how to obtain the absolute
      /// path of the directory to be enumerated.
      /// @return Instance of this class that represents an enumeration to be done and will
      /// return `true` unconditionally when the #ShouldIncludeInDirectoryEnumeration method
      /// is invoked.
      static inline SingleDirectoryEnumeration IncludeAllFilenames(
          EDirectoryPathSource directoryPathSource)
      {
        return SingleDirectoryEnumeration(directoryPathSource);
      }

      /// Creates an instance of this class that includes only those filenames that match one
      /// of the file patterns associated with the specified rule.
      /// @param [in] directoryPathSource Enumerator that describes how to obtain the absolute
      /// path of the directory to be enumerated.
      /// @param [in] filePatternSource Filesystem rule used to determine whether or not a
      /// filename should be included in the enumeration result.
      /// @return Instance of this class that represents an enumeration to be done and will
      /// return `true` when the #ShouldIncludeInDirectoryEnumeration method is invoked only
      /// for those filenames that match a file pattern associated with the specified rule and
      /// `false` otherwise.
      static inline SingleDirectoryEnumeration IncludeOnlyMatchingFilenames(
          EDirectoryPathSource directoryPathSource, const FilesystemRule& filePatternSource)
      {
        return SingleDirectoryEnumeration(directoryPathSource, filePatternSource, false);
      }

      /// Creates an instance of this class that includes only those filenames that match one
      /// of the file patterns associated with any of the specified rules.
      /// @param [in] directoryPathSource Enumerator that describes how to obtain the absolute
      /// path of the directory to be enumerated.
      /// @param [in] filePatternSource Filesystem rule container used to determine whether or not a
      /// filename should be included in the enumeration result.
      /// @param [in] queryRuleSelectionMode Filtering mode for determining which rules in the
      /// container should be checked. By default, all rules are checked.
      /// @return Instance of this class that represents an enumeration to be done and will
      /// return `true` when the #ShouldIncludeInDirectoryEnumeration method is invoked only
      /// for those filenames that match a file pattern associated with the specified rule and
      /// `false` otherwise.
      static inline SingleDirectoryEnumeration IncludeOnlyMatchingFilenames(
          EDirectoryPathSource directoryPathSource,
          const RelatedFilesystemRuleContainer& filePatternSource,
          EQueryRuleSelectionMode queryRuleSelectionMode = EQueryRuleSelectionMode::All)
      {
        return SingleDirectoryEnumeration(
            directoryPathSource, filePatternSource, false, queryRuleSelectionMode);
      }

      /// Creates an instance of this class that includes only those filenames that do not
      /// match one of the file patterns associated with the specified rule.
      /// @param [in] directoryPathSource Enumerator that describes how to obtain the absolute
      /// path of the directory to be enumerated.
      /// @param [in] filePatternSource Filesystem rule used to determine whether or not a
      /// filename should be included in the enumeration result.
      /// @return Instance of this class that represents an enumeration to be done and will
      /// return `false` when the #ShouldIncludeInDirectoryEnumeration method is invoked with
      /// filenames that match a file pattern associated with the specified rule and `true`
      /// otherwise.
      static inline SingleDirectoryEnumeration IncludeAllExceptMatchingFilenames(
          EDirectoryPathSource directoryPathSource, const FilesystemRule& filePatternSource)
      {
        return SingleDirectoryEnumeration(directoryPathSource, filePatternSource, true);
      }

      /// Creates an instance of this class that includes only those filenames that do not match any
      /// of the file patterns associated with any of the specified rules.
      /// @param [in] directoryPathSource Enumerator that describes how to obtain the absolute
      /// path of the directory to be enumerated.
      /// @param [in] filePatternSource Filesystem rule container used to determine whether or not a
      /// filename should be included in the enumeration result.
      /// @param [in] queryRuleSelectionMode Filtering mode for determining which rules in the
      /// container should be checked. By default, all rules are checked.
      /// @return Instance of this class that represents an enumeration to be done and will
      /// return `true` when the #ShouldIncludeInDirectoryEnumeration method is invoked only
      /// for those filenames that match a file pattern associated with the specified rule and
      /// `false` otherwise.
      static inline SingleDirectoryEnumeration IncludeAllExceptMatchingFilenames(
          EDirectoryPathSource directoryPathSource,
          const RelatedFilesystemRuleContainer& filePatternSource,
          EQueryRuleSelectionMode queryRuleSelectionMode = EQueryRuleSelectionMode::All)
      {
        return SingleDirectoryEnumeration(
            directoryPathSource, filePatternSource, true, queryRuleSelectionMode);
      }

      /// Retrieves and returns the enumerator that identifies the directory path source for
      /// this directory enumeration operation.
      /// @return Directory path source enumerator.
      inline EDirectoryPathSource GetDirectoryPathSource(void) const
      {
        return directoryPathSource;
      }

      /// Selects a directory path from among those provided as input, using the directory
      /// path source enumerator to make the decision.
      /// @param [in] associatedPath Absolute path internally associated with the handle to
      /// the directory that is open for enumeration.
      /// @param [in] realOpenedPath Absolute path that was actually opened when creating the
      /// handle to the directory that is open for enumeration.
      /// @return One of the two parameters, based on the directory path source enumerator, or
      /// an empty string if the enumerator is invalid.
      inline std::wstring_view SelectDirectoryPath(
          std::wstring_view associatedPath, std::wstring_view realOpenedPath) const
      {
        switch (GetDirectoryPathSource())
        {
          case EDirectoryPathSource::AssociatedPath:
            return associatedPath;

          case EDirectoryPathSource::RealOpenedPath:
            return realOpenedPath;

          case EDirectoryPathSource::FilePatternSourceOriginDirectory:
            DebugAssert(
                (false == filePatternMatchConfig.containsMultipleRules) &&
                    (nullptr != filePatternSource.singleRule),
                "Attempting to use a filesystem rule as a directory path source without a singular filesystem rule present.");
            return filePatternSource.singleRule->GetOriginDirectoryFullPath();

          case EDirectoryPathSource::FilePatternSourceTargetDirectory:
            DebugAssert(
                (false == filePatternMatchConfig.containsMultipleRules) &&
                    (nullptr != filePatternSource.singleRule),
                "Attempting to use a filesystem rule as a directory path source without a singular filesystem rule present.");
            return filePatternSource.singleRule->GetTargetDirectoryFullPath();

          default:
            return std::wstring_view();
        }
      }

      /// Determines whether or not the specified filename should be included in a directory
      /// enumeration. If a filesystem rule is present then it is checked for a file pattern
      /// match and the result is either inverted or not, as appropriate. Otherwise it is
      /// presumed that there is no restriction on the files to include.
      /// @param [in] filename Filename to check for inclusion. This is the "file part" of an
      /// absolute path, so just the part after the final backslash.
      /// @return `true` if the filename should be included in the enumeration, `false`
      /// otherwise.
      inline bool ShouldIncludeInDirectoryEnumeration(std::wstring_view filename) const
      {
        if (true == filePatternMatchConfig.containsMultipleRules)
        {
          if (nullptr == filePatternSource.multipleRules) return true;
          return (
              filePatternSource.multipleRules->HasRuleMatchingFileName(
                  filename, filePatternMatchConfig.multiRuleSelectionMode) !=
              filePatternMatchConfig.invertMatches);
        }
        else
        {
          if (nullptr == filePatternSource.singleRule) return true;
          return (
              filePatternSource.singleRule->FileNameMatchesAnyPattern(filename) !=
              filePatternMatchConfig.invertMatches);
        }
      }

    private:

      /// Holds a pointer to the object used to check for a match with file patterns when
      /// enumerating directory contents. Used to determine if a particular filename should be
      /// included or excluded.
      union UFilePatternSource
      {
        /// Pointer to the single filesystem rule whose file patterns should be consulted, if only
        /// a single filesystem rule is present.
        const FilesystemRule* singleRule;

        /// Pointer to a container of multiple filesystem rules whose file patterns should be
        /// consulted, if multiple filesystem rules are present.
        const RelatedFilesystemRuleContainer* multipleRules;

        inline bool operator==(const UFilePatternSource& other) const
        {
          return (singleRule == other.singleRule);
        }
      };

      /// Holds configuration settings for how to check the file pattern source for file pattern
      /// matches when enumerating the present directory.
      struct SFilePatternMatchConfig
      {
        /// Whether or not matches should be inverted.
        bool invertMatches : 1;

        /// Whether or not the file pattern source contains multiple rules.
        bool containsMultipleRules : 1;

        /// If the file pattern source contains multiple rules, how to choose the correct rules to
        /// use when querying for a file pattern match.
        EQueryRuleSelectionMode multiRuleSelectionMode : 6;

        bool operator==(const SFilePatternMatchConfig& other) const = default;
      };

      static_assert(sizeof(void*) == sizeof(UFilePatternSource));
      static_assert(
          1 == sizeof(SFilePatternMatchConfig), "Data structure size constraint violation.");

      inline SingleDirectoryEnumeration(EDirectoryPathSource directoryPathSource)
          : directoryPathSource(directoryPathSource), filePatternSource(), filePatternMatchConfig()
      {}

      inline SingleDirectoryEnumeration(
          EDirectoryPathSource directoryPathSource,
          const FilesystemRule& filePatternSource,
          bool invertFilePatternMatches)
          : directoryPathSource(directoryPathSource),
            filePatternSource({.singleRule = &filePatternSource}),
            filePatternMatchConfig({.invertMatches = invertFilePatternMatches})
      {}

      inline SingleDirectoryEnumeration(
          EDirectoryPathSource directoryPathSource,
          const RelatedFilesystemRuleContainer& filePatternSource,
          bool invertFilePatternMatches,
          EQueryRuleSelectionMode queryRuleSelectionMode)
          : directoryPathSource(directoryPathSource),
            filePatternSource({.multipleRules = &filePatternSource}),
            filePatternMatchConfig(
                {.invertMatches = invertFilePatternMatches,
                 .containsMultipleRules = true,
                 .multiRuleSelectionMode = queryRuleSelectionMode})
      {}

      /// File pattern source object, if any is present.
      UFilePatternSource filePatternSource;

      /// Configuration settings for how to consult the file pattern source object for matches
      /// during enumeration.
      SFilePatternMatchConfig filePatternMatchConfig;

      static_assert(
          1 == sizeof(filePatternMatchConfig), "Data structure size constraint violation.");

      /// Enumerator to specify how to obtain the path of the directory to be enumerated.
      EDirectoryPathSource directoryPathSource;
    };

    /// Holds the information needed to describe how to insert a single directory name into the
    /// enumeration result as part of a larger directory enumeration operation. Immutable once
    /// constructed.
    class SingleDirectoryNameInsertion
    {
    public:

      inline SingleDirectoryNameInsertion(const FilesystemRule& filesystemRule)
          : filesystemRule(&filesystemRule)
      {}

      bool operator==(const SingleDirectoryNameInsertion& other) const = default;

      /// Retrieves and returns the absolute path of the directory whose information should be
      /// used to fill in the non-filename fields in the relevant file information structures
      /// being supplied back to the application.
      /// @return Absolute path of the directory to use for file information. Not guaranteed
      /// to begin with a Windows namespace prefix.
      inline std::wstring_view DirectoryInformationSourceAbsolutePath(void) const
      {
        return filesystemRule->GetTargetDirectoryFullPath();
      }

      /// Retrieves and returns the directory part of the absolute path of the directory whose
      /// information should be used to fill in the non-filename fields in the relevant file
      /// information structures being supplied back to the application. This is otherwise
      /// known as the absolute path of the parent of the directory whose information is
      /// needed.
      /// @return Directory part of the absolute path of the directory to use for file
      /// information. Not guaranteed to begin with a Windows namespace prefix.
      inline std::wstring_view DirectoryInformationSourceDirectoryPart(void) const
      {
        return filesystemRule->GetTargetDirectoryParent();
      }

      /// Retrieves and returns the file part of the absolute path of the directory whose
      /// information should be used to fill in the non-filename fields in the relevant file
      /// information structures being supplied back to the application. This is otherwise
      /// known as the base name of the directory whose information is needed.
      /// @return File part of the absolute path of the directory to use for file information.
      inline std::wstring_view DirectoryInformationSourceFilePart(void) const
      {
        return filesystemRule->GetTargetDirectoryName();
      }

      /// Retrieves and returns the filename to be inserted into the enumeration results.
      /// This only affects the filename fields in the relevant file information structures
      /// being supplied back to the application.
      /// @return Name of the file to be inserted into the enumeration results.
      inline std::wstring_view FileNameToInsert(void) const
      {
        return filesystemRule->GetOriginDirectoryName();
      }

    private:

      /// Filesystem rule that will be queried to determine how the directory name insertion
      /// should occur. Queries would be for information about the origin and target
      /// directories.
      const FilesystemRule* filesystemRule;
    };

    /// Type alias for holding instructions for individual directory enumerations that need to be
    /// merged together as part of an application-requested directory enumeration operation.
    using TDirectoriesToEnumerate = std::vector<SingleDirectoryEnumeration>;

    /// Type alias for holding individual directory name insertions that need to be merged into
    /// the result of an application-requested directory enumeration operation.
    using TDirectoryNamesToInsert = TemporaryVector<SingleDirectoryNameInsertion>;

    inline DirectoryEnumerationInstruction(
        TDirectoriesToEnumerate&& directoriesToEnumerate,
        std::optional<TDirectoryNamesToInsert>&& directoryNamesToInsert)
        : directoriesToEnumerate(std::move(directoriesToEnumerate)),
          directoryNamesToInsert(std::move(directoryNamesToInsert))
    {}

    bool operator==(const DirectoryEnumerationInstruction& other) const = default;

    /// Creates a directory enumeration instruction that specifies to do nothing but pass
    /// through the original enumeration query without any modifications.
    /// @return Directory enumeration instruction encoded to pass the original query through to
    /// the system without modification.
    static inline DirectoryEnumerationInstruction PassThroughUnmodifiedQuery(void)
    {
      return DirectoryEnumerationInstruction(
          {SingleDirectoryEnumeration::IncludeAllFilenames(EDirectoryPathSource::RealOpenedPath)},
          std::nullopt);
    }

    /// Creates a directory enumeration instruction that specifies a specific set of up to two
    /// directories to enumerate in order. The enumeration result provided back to the
    /// application will include the results of enumerating all of the directories in the
    /// supplied set.
    /// @param [in] directoriesToEnumerate Directories to be enumerated in the supplied order.
    /// @return Directory enumeration instruction encoded to request enumeration of the
    /// directories in the order provided.
    static inline DirectoryEnumerationInstruction EnumerateDirectories(
        TDirectoriesToEnumerate&& directoriesToEnumerate)
    {
      return DirectoryEnumerationInstruction(std::move(directoriesToEnumerate), std::nullopt);
    }

    /// Creates a directory enumeration instruction that specifies a specific set of individual
    /// directory names to be used as the entire enumeration content. The enumeration result
    /// provided back to the application will consist only of these directory names.
    /// @param [in] directoryNamesToInsert Individual directory names to be inserted into the
    /// enumeration result.
    /// @return Directory enumeration encoded to request enumeration of the specific supplied
    /// directory names and nothing else.
    static inline DirectoryEnumerationInstruction UseOnlyRuleOriginDirectoryNames(
        TDirectoryNamesToInsert&& directoryNamesToInsert)
    {
      return DirectoryEnumerationInstruction({}, std::move(directoryNamesToInsert));
    }

    /// Creates a directory enumeration instruction that specifies a specific set of individual
    /// directory names to be inserted into the enumeration results. The enumeration result
    /// provided back to the application will be the result of the original query with all of
    /// the supplied names inserted as directories.
    /// @param [in] directoryNamesToInsert Individual directory names to be inserted into the
    /// enumeration result.
    /// @return Directory enumeration encoded to request insertion of the specific supplied
    /// directory names into the enumeration result.
    static inline DirectoryEnumerationInstruction InsertRuleOriginDirectoryNames(
        TDirectoryNamesToInsert&& directoryNamesToInsert)
    {
      return DirectoryEnumerationInstruction(
          {SingleDirectoryEnumeration::IncludeAllFilenames(EDirectoryPathSource::RealOpenedPath)},
          std::move(directoryNamesToInsert));
    }

    /// Creates a directory enumeration instruction that specifies a specific set of up to two
    /// directories to enumerate in order along with a set of directory names to be inserted
    /// into the enumeration result. The enumeration result provided back to the application
    /// will include the results of enumerating all of the directories in the supplied set and
    /// with the supplied names inserted as directories.
    /// @param [in] directoriesToEnumerate Directories to be enumerated in the supplied order.
    /// @param [in] directoryNamesToInsert Individual directory names to be inserted into the
    /// enumeration result.
    /// @return Directory enumeration instruction encoded to request both enumeration of the
    /// directories in the order provided and insertion of the specific supplied directory names
    /// into the enumeration result.
    static inline DirectoryEnumerationInstruction
        EnumerateDirectoriesAndInsertRuleOriginDirectoryNames(
            TDirectoriesToEnumerate&& directoriesToEnumerate,
            TDirectoryNamesToInsert&& directoryNamesToInsert)
    {
      return DirectoryEnumerationInstruction(
          std::move(directoriesToEnumerate), std::move(directoryNamesToInsert));
    }

    /// Extracts the container of directory names to be inserted into the enumeration result.
    /// Does not check that directory names are actually present.
    /// @return Container of directory names to insert, extracted using move semantics.
    inline TDirectoryNamesToInsert ExtractDirectoryNamesToInsert(void)
    {
      TDirectoryNamesToInsert extractedDirectoryNamesToInsert(std::move(*directoryNamesToInsert));
      directoryNamesToInsert.reset();
      return extractedDirectoryNamesToInsert;
    }

    /// Obtains a read-only reference to the container of directories to be enumerated.
    /// @return Read-only reference to the container of directories to be enumerated.
    inline const TDirectoriesToEnumerate& GetDirectoriesToEnumerate(void) const
    {
      return directoriesToEnumerate;
    }

    /// Obtains a read-only reference to the container of directory names to be inserted into
    /// the enumeration result. Does not check that directory names are actually present.
    /// @return Read-only reference to the container of directory names to insert.
    inline const TDirectoryNamesToInsert& GetDirectoryNamesToInsert(void) const
    {
      return *directoryNamesToInsert;
    }

    /// Determines if this instruction indicates that directory names should be inserted into
    /// the enumeration result.
    /// @return `true` if so, `false` if not.
    inline bool HasDirectoryNamesToInsert(void) const
    {
      return directoryNamesToInsert.has_value();
    }

  private:

    /// Descriptions of how to enumerate the directories that need to be enumerated as the
    /// execution of this directory enumeration instruction.
    TDirectoriesToEnumerate directoriesToEnumerate;

    /// Base names of any directories that should be inserted into the enumeration result.
    /// These are not subject to any additional file pattern matching.
    /// If not present then no additional names need to be inserted.
    std::optional<TDirectoryNamesToInsert> directoryNamesToInsert;
  };

  /// Contains all of the information needed to execute a file operation complete with potential
  /// path redirection. Instances of this class would typically be created by consulting
  /// filesystem rules and consumed by whatever functions interact with both the application (to
  /// receive file operation requests) and the system (to submit file operation requests).
  class FileOperationInstruction
  {
  public:

    /// Not intended to be invoked externally. Objects should generally be created using factory
    /// methods.
    inline FileOperationInstruction(
        std::optional<TemporaryString>&& redirectedFilename,
        ETryFiles filenamesToTry,
        ECreateDispositionPreference createDispositionPreference,
        EAssociateNameWithHandle filenameHandleAssociation,
        BitSetEnum<EExtraPreOperation>&& extraPreOperations,
        std::wstring_view extraPreOperationOperand)
        : redirectedFilename(std::move(redirectedFilename)),
          filenamesToTry(filenamesToTry),
          createDispositionPreference(createDispositionPreference),
          filenameHandleAssociation(filenameHandleAssociation),
          extraPreOperations(std::move(extraPreOperations)),
          extraPreOperationOperand(extraPreOperationOperand)
    {}

    bool operator==(const FileOperationInstruction& other) const = default;

    /// Creates a filesystem operation redirection instruction that indicates the request should
    /// be passed directly to the underlying system call without redirection or interception of
    /// any kind.
    /// @return File operation redirection instruction encoded to indicate that there should be
    /// no processing whatsoever.
    static inline FileOperationInstruction NoRedirectionOrInterception(void)
    {
      return FileOperationInstruction(
          std::nullopt,
          ETryFiles::UnredirectedOnly,
          ECreateDispositionPreference::NoPreference,
          EAssociateNameWithHandle::None,
          {},
          std::wstring_view());
    }

    /// Creates a filesystem operation redirection instruction that indicates the request should
    /// not be redirected but should be intercepted for additional processing, either via
    /// pre-operation, handle association, or both.
    /// @param [in] filenameHandleAssociation How to associate a filename with a potentially
    /// newly-created filesystem handle. Optional, defaults to whichever path was successfully
    /// opened (which necessarily must be the unredirected path).
    /// @param [in] extraPreOperations Any extra pre-operations to be performed before the file
    /// operation is attempted. Optional, defaults to none.
    /// @param [in] extraPreOperationOperand Additional operand for any extra pre-operations.
    /// Optional, defaults to none.
    /// @return File operation redirection instruction encoded to indicate some additional
    /// processing needed but without redirection.
    static inline FileOperationInstruction InterceptWithoutRedirection(
        EAssociateNameWithHandle filenameHandleAssociation =
            EAssociateNameWithHandle::WhicheverWasSuccessful,
        BitSetEnum<EExtraPreOperation>&& extraPreOperations = {},
        std::wstring_view extraPreOperationOperand = std::wstring_view())
    {
      return FileOperationInstruction(
          std::nullopt,
          ETryFiles::UnredirectedOnly,
          ECreateDispositionPreference::NoPreference,
          filenameHandleAssociation,
          std::move(extraPreOperations),
          extraPreOperationOperand);
    }

    /// Creates a filesystem operation redirection instruction that indicates the request should
    /// be redirected in simple mode. This means that only the redirected file is tried.
    /// @param [in] redirectedFilename String representing the absolute redirected filename,
    /// including Windows namespace prefix.
    /// @param [in] filenameHandleAssociation How to associate a filename with a potentially
    /// newly-created filesystem handle. Optional, defaults to no association.
    /// @param [in] extraPreOperations Any extra pre-operations to be performed before the file
    /// operation is attempted. Optional, defaults to none.
    /// @param [in] extraPreOperationOperand Additional operand for any extra pre-operations.
    /// Optional, defaults to none.
    /// @return File operation redirection instruction encoded to indicate redirection plus
    /// optionally some additional processing.
    static inline FileOperationInstruction SimpleRedirectTo(
        TemporaryString&& redirectedFilename,
        EAssociateNameWithHandle filenameHandleAssociation = EAssociateNameWithHandle::None,
        BitSetEnum<EExtraPreOperation>&& extraPreOperations = {},
        std::wstring_view extraPreOperationOperand = std::wstring_view())
    {
      return FileOperationInstruction(
          std::move(redirectedFilename),
          ETryFiles::RedirectedOnly,
          ECreateDispositionPreference::NoPreference,
          filenameHandleAssociation,
          std::move(extraPreOperations),
          extraPreOperationOperand);
    }

    /// Creates a filesystem operation redirection instruction that indicates the request should
    /// be redirected in overlay mode This means that the redirected file is tried first
    /// followed by the unredirected file, thus giving priority to the former if it exists.
    /// @param [in] redirectedFilename String representing the absolute redirected filename,
    /// including Windows namespace prefix.
    /// @param [in] filenameHandleAssociation How to associate a filename with a potentially
    /// newly-created filesystem handle. Optional, defaults to no association.
    /// @param [in] createDispositionPreference Whether to prefer creating a new file or opening
    /// an existing file, if the application is willing to accept both. Optional, defaults to no
    /// preference.
    /// @param [in] extraPreOperations Any extra pre-operations to be performed before the file
    /// operation is attempted. Optional, defaults to none.
    /// @param [in] extraPreOperationOperand Additional operand for any extra pre-operations.
    /// Optional, defaults to none.
    /// @return File operation redirection instruction encoded to indicate redirection plus
    /// optionally some additional processing.
    static inline FileOperationInstruction OverlayRedirectTo(
        TemporaryString&& redirectedFilename,
        EAssociateNameWithHandle filenameHandleAssociation = EAssociateNameWithHandle::None,
        ECreateDispositionPreference createDispositionPreference =
            ECreateDispositionPreference::NoPreference,
        BitSetEnum<EExtraPreOperation>&& extraPreOperations = {},
        std::wstring_view extraPreOperationOperand = std::wstring_view())
    {
      return FileOperationInstruction(
          std::move(redirectedFilename),
          ETryFiles::RedirectedFirst,
          createDispositionPreference,
          filenameHandleAssociation,
          std::move(extraPreOperations),
          extraPreOperationOperand);
    }

    /// Retrieves and returns the preferred way of handling a create disposition requested by
    /// the application.
    /// @return Preference for how to handle create disposition parameters.
    inline ECreateDispositionPreference GetCreateDispositionPreference(void) const
    {
      return createDispositionPreference;
    }

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

    /// Retrieves and returns the filename to be associated with a newly-created filesystem
    /// handle.
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

  private:

    /// Redirected filename. This would result from a file operation redirection query that
    /// matches a rule and ends up being redirected. If not present, then no redirection
    /// occurred.
    std::optional<TemporaryString> redirectedFilename;

    /// Filenames to try when submitting a file operation to the underlying system call.
    ETryFiles filenamesToTry;

    /// Whether to prefer creating a new file or opening an existing file, if the application is
    /// willing to accept both.
    ECreateDispositionPreference createDispositionPreference;

    /// Filename to associate with a newly-created file handle that results from successful
    /// execution of the file operation.
    EAssociateNameWithHandle filenameHandleAssociation;

    /// Extra operations to perform before submitting the filesystem operation to the underlying
    /// system call.
    BitSetEnum<EExtraPreOperation> extraPreOperations;

    /// Operand to be used as a parameter for extra pre-operations.
    std::wstring_view extraPreOperationOperand;
  };

#ifdef _WIN64
  static_assert(
      sizeof(DirectoryEnumerationInstruction::SingleDirectoryEnumeration) <= 16,
      "Data structure size constraint violation.");
  static_assert(
      sizeof(DirectoryEnumerationInstruction::SingleDirectoryNameInsertion) <= 8,
      "Data structure size constraint violation.");
  static_assert(
      sizeof(DirectoryEnumerationInstruction) <= 64, "Data structure size constraint violation.");
  static_assert(
      sizeof(FileOperationInstruction) <= 64, "Data structure size constraint violation.");
#else
  static_assert(
      sizeof(DirectoryEnumerationInstruction::SingleDirectoryEnumeration) <= 8,
      "Data structure size constraint violation.");
  static_assert(
      sizeof(DirectoryEnumerationInstruction::SingleDirectoryNameInsertion) <= 4,
      "Data structure size constraint violation.");
  static_assert(
      sizeof(DirectoryEnumerationInstruction) <= 32, "Data structure size constraint violation.");
  static_assert(
      sizeof(FileOperationInstruction) <= 32, "Data structure size constraint violation.");
#endif
} // namespace Pathwinder
