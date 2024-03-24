/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file FilesystemRule.h
 *   Declaration of objects that represent filesystem redirection rules.
 **************************************************************************************************/

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "ArrayList.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

namespace Pathwinder
{
  /// Enumerates the possible results of comparing a directory with either the origin or
  /// target directory associated with a filesystem rule.
  enum class EDirectoryCompareResult
  {
    /// Candidate directory is exactly equal to the comparison target directory.
    Equal,

    /// Candidate directory is not related to the comparison target directory. Paths
    /// diverge, and one is not an ancestor or descendant of the other.
    Unrelated,

    /// Candidate directory is the immediate parent of the comparison target directory.
    CandidateIsParent,

    /// Candidate directory is the immediate child of the comparison target directory.
    CandidateIsChild,

    /// Candidate directory is an ancestor, but not the immediate parent, of the comparison
    /// target directory.
    CandidateIsAncestor,

    /// Candidate directory is a descendant, but not the immediate child, of the comparison
    /// target directory.
    CandidateIsDescendant,

    /// Not used as a value. Identifies the number of enumerators present in this enumeration.
    Count
  };

  /// Enumerates the different types of redirection modes that can be configured for each
  /// filesystem rule.
  enum class ERedirectMode
  {
    /// Simple redirection mode. Either a file operation is redirected or it is not, and
    /// only one file name is tried. Redirected files either exist on the target side or do
    /// not exist at all. Non-redirected files either exist on the origin side or do not
    /// exist at all.
    Simple,

    /// Overlay redirection mode. File operations merge the target side with the origin
    /// side, with files on the target side given priority.
    Overlay,

    /// Not used as a value. Identifies the number of enumerators present in this enumeration.
    Count
  };

  /// Holds all of the data needed to represent a single filesystem redirection rule.
  /// Implements all of the behavior needed to determine whether and how paths are covered by the
  /// rule. From the application's point of view, the origin directory is where files covered by
  /// each rule appear to exist, and the target directory is where they actually exist.
  class FilesystemRule
  {
  public:

    /// Constructs a fully-functional filesystem rule. File patterns are optional, with default
    /// behavior matching all files. This is preferred over a single-element vector containing "*"
    /// because file pattern match checking can be skipped entirely.
    FilesystemRule(
        std::wstring_view name,
        std::wstring_view originDirectoryFullPath,
        std::wstring_view targetDirectoryFullPath,
        std::vector<std::wstring>&& filePatterns = std::vector<std::wstring>(),
        ERedirectMode redirectMode = ERedirectMode::Simple);

    bool operator==(const FilesystemRule& other) const = default;

    /// Compares the specified directory with the origin directory associated with this object.
    /// @param [in] candidateDirectory Directory to compare with the origin directory.
    /// @return Result of the comparison. See #EDirectoryCompareResult documentation for more
    /// information.
    EDirectoryCompareResult DirectoryCompareWithOrigin(std::wstring_view candidateDirectory) const;

    /// Compares the specified directory with the target directory associated with this object.
    /// @param [in] candidateDirectory Directory to compare with the origin directory.
    /// @return Result of the comparison. See #EDirectoryCompareResult documentation for more
    /// information.
    EDirectoryCompareResult DirectoryCompareWithTarget(std::wstring_view candidateDirectory) const;

    /// Determines if the specified filename matches any of the file patterns associated with
    /// this object. Input filename must not contain any backslash separators, as it is intended
    /// to represent a file within a directory rather than a path.
    /// @param [in] candidateFileName File name to check for matches with any file pattern.
    /// @return `true` if any file pattern produces a match, `false` otherwise.
    bool FileNameMatchesAnyPattern(std::wstring_view candidateFileName) const;

    /// Retrieves and returns the name of this filesystem rule.
    /// @return Name of this filesystem rule, or an empty view if no name has been set.
    inline std::wstring_view GetName(void) const
    {
      return name;
    }

    /// Provides read-only access to the file patterns associated with this rule object.
    /// @return Read-only reference to the file patterns data structure.
    inline const std::vector<std::wstring>& GetFilePatterns(void) const
    {
      return filePatterns;
    }

    /// Retrieves and returns the full path of the origin directory associated with this rule.
    /// @return Full path of the origin directory.
    inline std::wstring_view GetOriginDirectoryFullPath(void) const
    {
      return originDirectoryFullPath;
    }

    /// Retrieves and returns the name of the origin directory associated with this rule.
    /// This is otherwise known as the relative path of the origin directory within its parent.
    /// @return Name of the origin directory.
    inline std::wstring_view GetOriginDirectoryName(void) const
    {
      std::wstring_view originDirectoryName = originDirectoryFullPath;

      if (std::wstring_view::npos != originDirectorySeparator)
        originDirectoryName.remove_prefix(1 + originDirectorySeparator);

      return originDirectoryName;
    }

    /// Retrieves and returns the immediate parent directory of the origin directory associated
    /// with this rule.
    /// @return Parent of the origin directory.
    inline std::wstring_view GetOriginDirectoryParent(void) const
    {
      if (std::wstring_view::npos == originDirectorySeparator) return std::wstring_view();

      std::wstring_view originDirectoryParent = originDirectoryFullPath;
      originDirectoryParent.remove_suffix(
          originDirectoryParent.length() - originDirectorySeparator);

      return originDirectoryParent;
    }

    /// Retrieves and returns the full path of the target directory associated with this rule.
    /// @return Full path of the target directory.
    inline std::wstring_view GetTargetDirectoryFullPath(void) const
    {
      return targetDirectoryFullPath;
    }

    /// Retrieves and returns the name of the target directory associated with this rule.
    /// This is otherwise known as the relative path of the target directory within its parent.
    /// @return Name of the target directory.
    inline std::wstring_view GetTargetDirectoryName(void) const
    {
      std::wstring_view targetDirectoryName = targetDirectoryFullPath;

      if (std::wstring_view::npos != targetDirectorySeparator)
        targetDirectoryName.remove_prefix(1 + targetDirectorySeparator);

      return targetDirectoryName;
    }

    /// Retrieves and returns the immediate parent directory of the target directory associated
    /// with this rule.
    /// @return Parent of the target directory.
    inline std::wstring_view GetTargetDirectoryParent(void) const
    {
      if (std::wstring_view::npos == originDirectorySeparator) return std::wstring_view();

      std::wstring_view targetDirectoryParent = targetDirectoryFullPath;
      targetDirectoryParent.remove_suffix(
          targetDirectoryParent.length() - targetDirectorySeparator);

      return targetDirectoryParent;
    }

    /// Retrieves and returns the redirection mode configured for this rule.
    /// @return Redirection mode enumerator configured for this rule.
    inline ERedirectMode GetRedirectMode(void) const
    {
      return redirectMode;
    }

    inline bool HasFilePatterns(void) const
    {
      return (false == filePatterns.empty());
    }

    /// Computes and returns the result of redirecting from the specified candidate path to the
    /// target directory associated with this rule. Input candidate path is split into two
    /// parts: the directory part, which identifies the absolute directory in which the file is
    /// located, and the file part, which identifies the file within its directory. If the
    /// origin directory matches the candidate path's directory part and a file pattern matches
    /// the candidate path's file part then a redirection can occur to the target directory.
    /// Otherwise no redirection occurs and no output is produced.
    /// @param [in] candidatePathDirectoryPart Directory portion of the candidate path, which is
    /// an absolute path and does not contain a trailing backslash.
    /// @param [in] candidatePathFilePart File portion of the candidate path without any leading
    /// backslash.
    /// @param [in] namespacePrefix Windows namespace prefix to be inserted at the beginning of
    /// the output. This parameter is optional and defaults to the empty string.
    /// @param [in] extraSuffix Extra suffix to be added to the end of the output. This
    /// parameter is optional and defaults to the empty string.
    /// @return Redirected location as an absolute path, if redirection occurred successfully.
    std::optional<TemporaryString> RedirectPathOriginToTarget(
        std::wstring_view candidatePathDirectoryPart,
        std::wstring_view candidatePathFilePart,
        std::wstring_view namespacePrefix = std::wstring_view(),
        std::wstring_view extraSuffix = std::wstring_view()) const;

    /// Computes and returns the result of redirecting from the specified candidate path to the
    /// origin directory associated with this rule. Input candidate path is split into two
    /// parts: the directory part, which identifies the absolute directory in which the file is
    /// located, and the file part, which identifies the file within its directory. If the
    /// target directory matches the candidate path's directory part and a file pattern matches
    /// the candidate path's file part then a redirection can occur to the origin directory.
    /// Otherwise no redirection occurs and no output is produced.
    /// @param [in] candidatePathDirectoryPart Directory portion of the candidate path, which is
    /// an absolute path and does not contain a trailing backslash.
    /// @param [in] candidatePathFilePart File portion of the candidate path without any leading
    /// backslash.
    /// @param [in] namespacePrefix Windows namespace prefix to be inserted at the beginning of
    /// the output. This parameter is optional and defaults to the empty string.
    /// @param [in] extraSuffix Extra suffix to be added to the end of the output. This
    /// parameter is optional and defaults to the empty string.
    /// @return Redirected location as an absolute path, if redirection occurred successfully.
    std::optional<TemporaryString> RedirectPathTargetToOrigin(
        std::wstring_view candidatePathDirectoryPart,
        std::wstring_view candidatePathFilePart,
        std::wstring_view namespacePrefix = std::wstring_view(),
        std::wstring_view extraSuffix = std::wstring_view()) const;

  private:

    /// Name of this filesystem rule. This field is considered metadata only. It is not used
    /// internally for any specific purpose.
    std::wstring_view name;

    /// Redirection mode for this filesystem rule.
    ERedirectMode redirectMode;

    /// Position within the origin directory absolute path of the final separator between name
    /// and parent path. Initialized using the contents of the origin directory path string and
    /// must be declared before it.
    size_t originDirectorySeparator;

    /// Position within the target directory absolute path of the final separator between name
    /// and parent path. Initialized using the contents of the target directory path string and
    /// must be declared before it.
    size_t targetDirectorySeparator;

    /// Absolute path to the origin directory.
    std::wstring_view originDirectoryFullPath;

    /// Absolute path to the target directory.
    std::wstring_view targetDirectoryFullPath;

    /// Pattern that specifies which files within the origin and target directories are affected
    /// by this rule. Can be used to filter this rule to apply to only specific named files. If
    /// empty, it is assumed that there is no filter and therefore the rule applies to all files
    /// in the origin and target directories.
    std::vector<std::wstring> filePatterns;
  };

  /// Holds multiple filesystem rules together in a container such that they can be organized by
  /// some property they have in common and queried conveniently by file pattern.
  class RelatedFilesystemRuleContainer
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

    /// Provides read-only access to any single rule held by this object. Intended for obtaining
    /// access to whatever property is shared by all rules held in this container such that they are
    /// considered "related" to one another in some way.
    /// @return Read-only reference to a single contained rule.
    inline const FilesystemRule& AnyRule(void) const
    {
      return *filesystemRules.cbegin();
    }

    /// Retrieves and returns the number of rules contained in this object.
    /// @return Number of rules contained in this object.
    inline unsigned int CountOfRules(void) const
    {
      return static_cast<unsigned int>(filesystemRules.size());
    }

    /// Attempts to insert a filesystem rule into this container by constructing it in place.
    /// @param [in] ruleToInsert Rule to be inserted.
    /// @return Pair containing a pointer to the newly-constructed rule if a new rule was created
    /// and `true` if the insertion was successful, `false` otherwise.
    template <typename... Args> inline std::pair<const FilesystemRule*, bool> EmplaceRule(
        Args&&... args)
    {
      auto emplaceResult = filesystemRules.emplace(std::forward<Args>(args)...);
      return std::make_pair(&(*emplaceResult.first), emplaceResult.second);
    }

    /// Attempts to insert a filesystem rule into this container using copy semantics.
    /// @param [in] ruleToInsert Rule to be inserted.
    /// @return Pair containing a pointer to the newly-inserted rule if a new rule was inserted
    /// and `true` if the insertion was successful, `false` otherwise.
    std::pair<const FilesystemRule*, bool> InsertRule(const FilesystemRule& ruleToInsert)
    {
      return EmplaceRule(ruleToInsert);
    }

    /// Attempts to insert a filesystem rule into this container using move semantics.
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
} // namespace Pathwinder
