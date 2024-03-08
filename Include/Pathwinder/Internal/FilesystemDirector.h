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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "FilesystemInstruction.h"
#include "FilesystemRule.h"
#include "PrefixIndex.h"
#include "Strings.h"

namespace Pathwinder
{
  /// Maximum number of filesystem rules that can have the same origin directory and are therefore
  /// held in the same filesystem rule container. The higher this number, the more overhead gets
  /// introduced when executing filesystem requests that involve merging of contents using multiple
  /// filesystem rules (for example, directory enumeration).
  inline constexpr unsigned int kMaxFilesystemRulesPerOriginDirectory = 3;

  /// Type alias for holding owned strings for deduplication and organization. Contained strings are
  /// compared case-insensitively.
  using TCaseInsensitiveStringSet = std::unordered_set<
      std::wstring,
      Strings::CaseInsensitiveHasher<wchar_t>,
      Strings::CaseInsensitiveEqualityComparator<wchar_t>>;

  /// Type alias for holding a map from filesystem rule name to filesystem rule object. All
  /// filesystem rules are uniquely identified by name, and the names are considered case
  /// insensitive.
  using TFilesystemRuleMapByName = std::
      map<std::wstring_view, FilesystemRule, Strings::CaseInsensitiveLessThanComparator<wchar_t>>;

  /// Type alias for holding a map from origin directory to a container of filesystem objects all
  /// having the same origin directory.
  using TFilesystemRuleMapByOriginDirectory = std::unordered_map<
      std::wstring_view,
      FilesystemRuleContainer<kMaxFilesystemRulesPerOriginDirectory>,
      Strings::CaseInsensitiveHasher<wchar_t>,
      Strings::CaseInsensitiveEqualityComparator<wchar_t>>;

  /// Type alias for holding an index that can identify filesystem rules by directory prefix.
  using TPrefixDirectoryIndex = PrefixIndex<wchar_t, FilesystemRule>;

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
        TCaseInsensitiveStringSet&& filesystemRuleNames,
        TPrefixDirectoryIndex&& originDirectoryIndex,
        TFilesystemRuleMapByName&& filesystemRules)
        : originDirectories(std::move(originDirectories)),
          targetDirectories(std::move(targetDirectories)),
          filesystemRuleNames(std::move(filesystemRuleNames)),
          originDirectoryIndex(std::move(originDirectoryIndex)),
          filesystemRules(std::move(filesystemRules))
    {}

    /// Move-constructs each individual instance variable, but with the understanding that all
    /// strings used in filesystem rules are owned externally and should not be owned by this
    /// object. Intended to be invoked by tests, which tend to use string literals when creating
    /// filesystem rules and hence ownership does not need to be transferred to this object.
    inline FilesystemDirector(
        TPrefixDirectoryIndex&& originDirectoryIndex, TFilesystemRuleMapByName&& filesystemRules)
        : FilesystemDirector(
              TCaseInsensitiveStringSet(),
              TCaseInsensitiveStringSet(),
              TCaseInsensitiveStringSet(),
              std::move(originDirectoryIndex),
              std::move(filesystemRules))
    {}

    FilesystemDirector(const FilesystemDirector&) = delete;

    FilesystemDirector(FilesystemDirector&& other) = default;

    FilesystemDirector& operator=(const FilesystemDirector&) = delete;

    FilesystemDirector& operator=(FilesystemDirector&& other) = default;

    /// Retrieves and returns the number of filesystem rules held by this object.
    /// @return Number of filesystem rules.
    inline unsigned int CountOfRules(void) const
    {
      return static_cast<unsigned int>(filesystemRules.size());
    }

    /// Searches for the specified rule by name and returns a pointer to the corresponding
    /// object, if found.
    /// @param [in] ruleName Name of the rule for which to search.
    /// @return Pointer to the rule, or `nullptr` if no matching rule is found.
    inline const FilesystemRule* FindRuleByName(std::wstring_view ruleName) const
    {
      const auto ruleIt = filesystemRules.find(ruleName);
      if (filesystemRules.cend() == ruleIt) return nullptr;

      return &ruleIt->second;
    }

    /// Searches for the specified rule by origin directory and returns a pointer to the
    /// corresponding object, if found.
    /// @param [in] ruleOriginDirectoryFullPath Full path of the directory for which to search.
    /// @return Pointer to the rule, or `nullptr` if no matching rule is found.
    inline const FilesystemRule* FindRuleByOriginDirectory(
        std::wstring_view ruleOriginDirectoryFullPath) const
    {
      const auto ruleNode = originDirectoryIndex.Find(ruleOriginDirectoryFullPath);
      if (nullptr == ruleNode) return nullptr;

      return ruleNode->GetData();
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
      return originDirectoryIndex.HasPathForPrefix(absoluteFilePathTrimmed);
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
    TCaseInsensitiveStringSet filesystemRuleNames;

    /// Indexes all absolute paths of origin directories used by filesystem rules.
    TPrefixDirectoryIndex originDirectoryIndex;

    /// Holds all filesystem rules contained within the candidate filesystem director object.
    /// Maps from rule name to rule object.
    TFilesystemRuleMapByName filesystemRules;
  };
} // namespace Pathwinder
