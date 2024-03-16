/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file FilesystemDirector.h
 *   Declaration of objects that hold multiple filesystem rules and apply them together.
 **************************************************************************************************/

#pragma once

#include <bitset>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "FilesystemInstruction.h"
#include "FilesystemRule.h"
#include "PrefixTree.h"
#include "Strings.h"

namespace Pathwinder
{
  /// Holds multiple filesystem rules together in a container such that they can be organized by
  /// some property they have in common and queried conveniently by file pattern.
  /// @tparam kMaxRuleCount Maximum number of rules that can be contained in this container type.
  template <const unsigned int kMaxRuleCount> class RelatedFilesystemRuleContainer
  {
  public:

    /// Comparator function object type for establishing the ordering of filesystem rules. If the
    /// number of file patterns is the same, then filesystem rules are ordered by name. Otherwise,
    /// the filesystem rules are ordered in descending order by number of file patterns. That way, 0
    /// file patterns is guaranteed to be at the end of the ordering, so that filesystem rules with
    /// no file patterns can act as a "catch-all" default, whereas more specific rules are given
    /// have precedence.
    struct OrderedFilesystemRuleLessThanComparator
    {
      inline bool operator()(const FilesystemRule& lhs, const FilesystemRule& rhs) const
      {
        if (lhs.GetFilePatterns().size() == rhs.GetFilePatterns().size())
          return (Strings::CompareCaseInsensitive(lhs.GetName(), rhs.GetName()) < 0);
        return (lhs.GetFilePatterns().size() > rhs.GetFilePatterns().size());
      }
    };

    /// Type alias for the internal container type that holds the filesystem rules themselves.
    using TFilesystemRules = std::set<FilesystemRule, OrderedFilesystemRuleLessThanComparator>;

    RelatedFilesystemRuleContainer(void) = default;

    bool operator==(const RelatedFilesystemRuleContainer& other) const = default;

    /// Provides read-only access to the rules held by this object. Intended for iterating in a
    /// loop over all of them.
    /// @return Read-only reference to the underlying container.
    inline const TFilesystemRules& AllRules(void) const
    {
      return filesystemRules;
    }

    /// Attempts to insert a filesystem rule into this container by constructing it in place.
    /// Insertion will fail if this container is already full, in which case the pointer returned
    /// will be `nullptr`.
    /// @param [in] ruleToInsert Rule to be inserted.
    /// @return Pair containing a pointer to the newly-constructed rule if a new rule was created
    /// and `true` if the insertion was successful, `false` otherwise.
    template <typename... Args> inline std::pair<const FilesystemRule*, bool> EmplaceRule(
        Args&&... args)
    {
      if (kMaxRuleCount == static_cast<unsigned int>(filesystemRules.size()))
        return std::make_pair(nullptr, false);
      auto emplaceResult = filesystemRules.emplace(std::forward<Args>(args)...);
      return std::make_pair(&(*emplaceResult.first), emplaceResult.second);
    }

    /// Attempts to insert a filesystem rule into this container using copy semantics. Insertion
    /// will fail if this container is already full, in which case the pointer returned
    /// will be `nullptr`.
    /// @param [in] ruleToInsert Rule to be inserted.
    /// @return Pair containing a pointer to the newly-inserted rule if a new rule was inserted
    /// and `true` if the insertion was successful, `false` otherwise.
    std::pair<const FilesystemRule*, bool> InsertRule(const FilesystemRule& ruleToInsert)
    {
      return EmplaceRule(ruleToInsert);
    }

    /// Attempts to insert a filesystem rule into this container using move semantics. Insertion
    /// will fail if this container is already full, in which case the pointer returned
    /// will be `nullptr`.
    /// @param [in] ruleToInsert Rule to be inserted.
    /// @return Pair containing a pointer to the newly-inserted rule if a new rule was inserted
    /// and `true` if the insertion was successful, `false` otherwise.
    std::pair<const FilesystemRule*, bool> InsertRule(FilesystemRule&& ruleToInsert)
    {
      return EmplaceRule(std::move(ruleToInsert));
    }

    /// Determines if the specified filename matches any of the file patterns associated with
    /// any of the rules held by this object. Input filename must not contain any backslash
    /// separators, as it is intended to represent a file within a directory rather than a path.
    /// @param [in] candidateFileName File name to check for matches with any file pattern.
    /// @return `true` if any file pattern produces a match, `false` otherwise.
    inline bool HasRuleMatchingFileName(std::wstring_view candidateFileName) const
    {
      return (nullptr != RuleMatchingFileName(candidateFileName));
    }

    /// Identifies the first filesystem rule found such that the specified filename matches any of
    /// the file patterns associated with that rule. Input filename must not contain any backslash
    /// separators, as it is intended to represent a file within a directory rather than a path.
    /// @param [in] candidateFileName File name to check for matches with any file pattern.
    /// @return Pointer to the first rule that matches, or `nullptr` if none exist.
    inline const FilesystemRule* RuleMatchingFileName(std::wstring_view candidateFileName) const
    {
      for (const auto& filesystemRule : filesystemRules)
        if (filesystemRule.FileNameMatchesAnyPattern(candidateFileName)) return &filesystemRule;

      return nullptr;
    }

  private:

    /// Storage for all filesystem rule objects owned by this container.
    TFilesystemRules filesystemRules;
  };

  /// Maximum number of filesystem rules that can have the same origin directory. Providing each
  /// filesystem rule with different file patterns enables differently-named files to be redirected
  /// to different locations. The value is constrained by how many underlying directory enumerations
  /// are allowed to be merged together, because one such underlying enumeration may be required for
  /// each filesystem rule that has the same origin directory as the directory being enumerated.
  inline constexpr unsigned int kMaxFilesystemRulesPerOriginDirectory =
      DirectoryEnumerationInstruction::kMaxMergedDirectoryEnumerations - 1;

  /// Type alias for holding owned strings for deduplication and organization. Contained strings are
  /// compared case-insensitively.
  using TCaseInsensitiveStringSet = std::unordered_set<
      std::wstring,
      Strings::CaseInsensitiveHasher<wchar_t>,
      Strings::CaseInsensitiveEqualityComparator<wchar_t>>;

  /// Type alias for holding owned strings for deduplication and organization. Contained strings are
  /// compared case-sensitively.
  using TCaseSensitiveStringSet = std::unordered_set<std::wstring>;

  /// Type alias for holding an index that can identify filesystem rules by directory prefix.
  using TFilesystemRulePrefixTree = PrefixTree<
      wchar_t,
      RelatedFilesystemRuleContainer<kMaxFilesystemRulesPerOriginDirectory>,
      Strings::CaseInsensitiveHasher<wchar_t>,
      Strings::CaseInsensitiveEqualityComparator<wchar_t>>;

  /// Type alias for holding a map from filesystem rule name to filesystem rule object. All
  /// filesystem rules are uniquely identified by name, and the names are considered case
  /// sensitive.
  using TFilesystemRuleIndexByName = std::map<std::wstring_view, const FilesystemRule*>;

  /// Identifies a create disposition setting based on what types of file accesses are allowed.
  /// Immutable once constructed.
  class CreateDisposition
  {
  public:

    /// Enumerates supported create disposition settings. These can exist in combination.
    enum class ECreateDisposition : uint8_t
    {
      /// Specifies that a new file can be created.
      CreateNewFile,

      /// Specifies that an existing file can be opened.
      OpenExistingFile,

      /// Not used as a value. Identifies the number of enumerators present in this
      /// enumeration.
      Count
    };

    constexpr CreateDisposition(bool canCreateNewFile, bool canOpenExistingFile)
        : createDispositionBits()
    {
      createDispositionBits[static_cast<size_t>(ECreateDisposition::CreateNewFile)] =
          canCreateNewFile;
      createDispositionBits[static_cast<size_t>(ECreateDisposition::OpenExistingFile)] =
          canOpenExistingFile;
    }

    constexpr bool operator==(const CreateDisposition&) const = default;

    /// Creates an object of this class type that encodes only allowing creation of a new file.
    /// @return Initialized object of this class.
    static constexpr CreateDisposition CreateNewFile(void)
    {
      return CreateDisposition(true, false);
    }

    /// Creates an object of this class type that encodes allowing both creation of a new file
    /// or opening of an existing file.
    /// @return Initialized object of this class.
    static constexpr CreateDisposition CreateNewOrOpenExistingFile(void)
    {
      return CreateDisposition(true, true);
    }

    /// Creates an object of this class type that encodes only allowing opening of an
    /// existingfile.
    /// @return Initialized object of this class.
    static constexpr CreateDisposition OpenExistingFile(void)
    {
      return CreateDisposition(false, true);
    }

    /// Determines whether or not the represented create disposition allows a new file to be
    /// created.
    /// @return `true` if so, `false` if not.
    constexpr bool AllowsCreateNewFile(void) const
    {
      return createDispositionBits[static_cast<size_t>(ECreateDisposition::CreateNewFile)];
    }

    /// Determines whether or not the represented create disposition allows an existing file to
    /// be opened.
    /// @return `true` if so, `false` if not.
    constexpr bool AllowsOpenExistingFile(void) const
    {
      return createDispositionBits[static_cast<size_t>(ECreateDisposition::OpenExistingFile)];
    }

  private:

    /// Holds the create disposition possibilities themselves.
    std::bitset<static_cast<size_t>(ECreateDisposition::Count)> createDispositionBits;
  };

  /// Identifies a file access mode based on what types of operations are allowed.
  /// Immutable once constructed.
  class FileAccessMode
  {
  public:

    /// Enumerates supported file access modes. These can exist in combination.
    enum class EFileAccessMode : uint8_t
    {
      /// Application is requesting to be able to read from the file.
      Read,

      /// Application is requesting to be able to write from the file.
      Write,

      /// Application is requesting to be able to delete the file.
      Delete,

      /// Not used as a value. Identifies the number of enumerators present in this
      /// enumeration.
      Count
    };

    constexpr FileAccessMode(bool canRead, bool canWrite, bool canDelete) : accessModeBits()
    {
      accessModeBits[static_cast<size_t>(EFileAccessMode::Read)] = canRead;
      accessModeBits[static_cast<size_t>(EFileAccessMode::Write)] = canWrite;
      accessModeBits[static_cast<size_t>(EFileAccessMode::Delete)] = canDelete;
    }

    constexpr bool operator==(const FileAccessMode&) const = default;

    /// Creates an object of this class that encodes only allowing read access to a file.
    /// @return Initialized object of this class.
    static constexpr FileAccessMode ReadOnly(void)
    {
      return FileAccessMode(true, false, false);
    }

    /// Creates an object of this class that encodes only allowing write access to a file.
    /// @return Initialized object of this class.
    static constexpr FileAccessMode WriteOnly(void)
    {
      return FileAccessMode(false, true, false);
    }

    /// Creates an object of this class that encodes allowing read and write access to a file.
    /// @return Initialized object of this class.
    static constexpr FileAccessMode ReadWrite(void)
    {
      return FileAccessMode(true, true, false);
    }

    /// Creates an object of this class that encodes allowing delete access to a file.
    /// @return Initialized object of this class.
    static constexpr FileAccessMode Delete(void)
    {
      return FileAccessMode(false, false, true);
    }

    /// Determines whether or not the represented file access mode allows reading.
    /// @return `true` if so, `false` if not.
    constexpr bool AllowsRead(void) const
    {
      return accessModeBits[static_cast<size_t>(EFileAccessMode::Read)];
    }

    /// Determines whether or not the represented file access mode allows writing.
    /// @return `true` if so, `false` if not.
    constexpr bool AllowsWrite(void) const
    {
      return accessModeBits[static_cast<size_t>(EFileAccessMode::Write)];
    }

    /// Determines whether or not the represented file access mode allows deletion.
    /// @return `true` if so, `false` if not.
    constexpr bool AllowsDelete(void) const
    {
      return accessModeBits[static_cast<size_t>(EFileAccessMode::Delete)];
    }

  private:

    /// Holds the requested file access modes themselves.
    std::bitset<static_cast<size_t>(EFileAccessMode::Count)> accessModeBits;
  };

  /// Holds multiple filesystem rules and applies them together to implement filesystem path
  /// redirection. Intended to be instantiated by a filesystem director builder or by tests. Rule
  /// set is immutable once this object is constructed.
  class FilesystemDirector
  {
  public:

    FilesystemDirector(void) = default;

    /// Move-constructs each individual instance variable. Does not validate any inputs or perform
    /// any consistency checks. Intended to be invoked by a filesystem director builder.
    inline FilesystemDirector(
        TCaseInsensitiveStringSet&& originDirectories,
        TCaseInsensitiveStringSet&& targetDirectories,
        TCaseSensitiveStringSet&& filesystemRuleNames,
        TFilesystemRulePrefixTree&& filesystemRulesByOriginDirectory,
        TFilesystemRuleIndexByName&& filesystemRulesByName)
        : originDirectories(std::move(originDirectories)),
          targetDirectories(std::move(targetDirectories)),
          filesystemRuleNames(std::move(filesystemRuleNames)),
          filesystemRulesByOriginDirectory(std::move(filesystemRulesByOriginDirectory)),
          filesystemRulesByName(std::move(filesystemRulesByName))
    {}

    /// Move-constructs each individual instance variable, but with the understanding that all
    /// strings used in filesystem rules are owned externally and should not be owned by this
    /// object. Intended to be invoked by tests, which tend to use string literals when creating
    /// filesystem rules and hence ownership does not need to be transferred to this object.
    inline FilesystemDirector(
        TFilesystemRulePrefixTree&& filesystemRulesByOriginDirectory,
        TFilesystemRuleIndexByName&& filesystemRulesByName)
        : FilesystemDirector(
              TCaseInsensitiveStringSet(),
              TCaseInsensitiveStringSet(),
              TCaseSensitiveStringSet(),
              std::move(filesystemRulesByOriginDirectory),
              std::move(filesystemRulesByName))
    {}

    FilesystemDirector(const FilesystemDirector&) = delete;

    FilesystemDirector(FilesystemDirector&& other) = default;

    FilesystemDirector& operator=(const FilesystemDirector&) = delete;

    FilesystemDirector& operator=(FilesystemDirector&& other) = default;

    /// Retrieves and returns the number of filesystem rules held by this object.
    /// @return Number of filesystem rules.
    inline unsigned int CountOfRules(void) const
    {
      return static_cast<unsigned int>(filesystemRulesByName.size());
    }

    /// Searches for the specified rule by name and returns a pointer to the corresponding
    /// object, if found.
    /// @param [in] ruleName Name of the rule for which to search.
    /// @return Pointer to the rule, or `nullptr` if no matching rule is found.
    inline const FilesystemRule* FindRuleByName(std::wstring_view ruleName) const
    {
      const auto ruleIt = filesystemRulesByName.find(ruleName);
      if (filesystemRulesByName.cend() == ruleIt) return nullptr;

      return ruleIt->second;
    }

    /// Generates an instruction for how to execute a directory enumeration, which involves
    /// listing the contents of a directory. Directory enumeration operations operate on
    /// directory handles that were previously opened by a file operation, which would itself
    /// have been executed under control of another method of this class. Arbitrary inputs are
    /// not well-supported by this method as an implementation simplification. Both supplied
    /// paths must be related to a filesystem rule in some way, either part of the origin
    /// hierarchy or the target hierarchy.
    /// @param [in] associatedPath Path associated internally with the open directory handle.
    /// @param [in] realOpenedPath Path actually submitted to the system when the directory
    /// handle was opened.
    /// @return Instruction that provides information on how to execute the directory
    /// enumeration.
    DirectoryEnumerationInstruction GetInstructionForDirectoryEnumeration(
        std::wstring_view associatedPath, std::wstring_view realOpenedPath) const;

    /// Generates an instruction for how to execute a file operation, such as opening, creating,
    /// or querying information about an individual file.
    /// @param [in] absoluteFilePath Path of the file being queried for possible redirection. A
    /// Windows namespace prefix is optional.
    /// @param [in] fileAccessMode Type of access or accesses to be performed on the file.
    /// @param [in] createDisposition Create disposition for the requsted file operation, which
    /// specifies whether a new file should be created, an existing file opened, or either.
    /// @return Instruction that provides information on how to execute the file operation
    /// redirection.
    FileOperationInstruction GetInstructionForFileOperation(
        std::wstring_view absoluteFilePath,
        FileAccessMode fileAccessMode,
        CreateDisposition createDisposition) const;

    /// Determines if the specified file path, which is already absolute, exists as a valid
    /// prefix for any filesystem rule. The input file path must not contain any leading Windows
    /// namespace prefixes and must not have any trailing backslash characters. Primarily
    /// intended for internal use but exposed for tests.
    /// @param [in] absoluteFilePathTrimmed Absolute path to check.
    /// @return `true` if a rule exists either at or as a descendent in the filesystem hierarchy
    /// of the specified path, `false` otherwise.
    inline bool IsPrefixForAnyRule(std::wstring_view absoluteFilePathTrimmed) const
    {
      return filesystemRulesByOriginDirectory.HasPathForPrefix(absoluteFilePathTrimmed);
    }

    /// Determines which rule from among those held by this object should be used for a
    /// particular input path. Primarily intended for internal use but exposed for tests.
    /// @param [in] absolutePath Absolute path for which to search for a rule for possible
    /// redirection.
    /// @return Pointer to the rule object that should be used with the specified path, or
    /// `nullptr` if no rule is applicable.
    const FilesystemRule* SelectRuleForPath(std::wstring_view absolutePath) const;

  private:

    /// Stores all absolute paths to origin directories used by filesystem rules.
    TCaseInsensitiveStringSet originDirectories;

    /// Stores all absolute paths to target directories used by filesystem rules.
    TCaseInsensitiveStringSet targetDirectories;

    /// Stores all filesystem rule names.
    TCaseSensitiveStringSet filesystemRuleNames;

    /// Indexes all absolute paths of origin directories used by filesystem rules.
    TFilesystemRulePrefixTree filesystemRulesByOriginDirectory;

    /// Holds all filesystem rules contained within the candidate filesystem director object.
    /// Maps from rule name to rule object.
    TFilesystemRuleIndexByName filesystemRulesByName;
  };
} // namespace Pathwinder
