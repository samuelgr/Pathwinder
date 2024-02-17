/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file FilesystemRule.cpp
 *   Implementation of filesystem redirection rule functionality.
 **************************************************************************************************/

#include "FilesystemRule.h"

#include <optional>
#include <string_view>
#include <vector>

#include "ApiWindows.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

namespace Pathwinder
{
  /// Compares the specified candidate directory with a comparison target directory to determine
  /// if and how they might be related.
  /// @param [in] candidateDirectory Directory to compare.
  /// @param [in] comparisonTargetDirectory Comparison target directory, which is typically
  /// associated with a filesystem rule object.
  /// @return Result of the comparison. See #EDirectoryCompareResult documentation for more
  /// information.
  static EDirectoryCompareResult DirectoryCompareInternal(
      std::wstring_view candidateDirectory, std::wstring_view comparisonTargetDirectory)
  {
    if (candidateDirectory.length() == comparisonTargetDirectory.length())
    {
      // Lengths are the same, so the two could be equal if they are related at all.

      if (Strings::EqualsCaseInsensitive(candidateDirectory, comparisonTargetDirectory))
        return EDirectoryCompareResult::Equal;
    }
    else if (candidateDirectory.length() < comparisonTargetDirectory.length())
    {
      // Candidate directory is shorter, so the candidate could be an ancestor or the
      // immediate parent of the comparison target. These two situations can be distinguished
      // based on whether or not the non-matching suffix in the comparison target contains
      // more than one backslash character.

      if ((true ==
           Strings::StartsWithCaseInsensitive(comparisonTargetDirectory, candidateDirectory)) &&
          (L'\\' == comparisonTargetDirectory[candidateDirectory.length()]))
      {
        comparisonTargetDirectory.remove_prefix(candidateDirectory.length());

        if (0 == comparisonTargetDirectory.find_last_of(L'\\'))
          return EDirectoryCompareResult::CandidateIsParent;
        else
          return EDirectoryCompareResult::CandidateIsAncestor;
      }
    }
    else
    {
      // Comparison target is shorter, so the candidate could be a descendant or the immediate
      // child of the comparison target. These two situations can be distinguished based on
      // whether or not the non-matching suffix in the candidate directory contains more than
      // one backslash character.

      if ((true ==
           Strings::StartsWithCaseInsensitive(candidateDirectory, comparisonTargetDirectory)) &&
          (L'\\' == candidateDirectory[comparisonTargetDirectory.length()]))
      {
        candidateDirectory.remove_prefix(comparisonTargetDirectory.length());

        if (0 == candidateDirectory.find_last_of(L'\\'))
          return EDirectoryCompareResult::CandidateIsChild;
        else
          return EDirectoryCompareResult::CandidateIsDescendant;
      }
    }

    return EDirectoryCompareResult::Unrelated;
  }

  /// Determines the position of the final separator character in a filesystem path.
  /// @param [in] path Path for which the final separator position is desired.
  /// @return Index of the final separator character, or -1 if no final separator character
  /// exists.
  static inline size_t FinalSeparatorPosition(std::wstring_view path)
  {
    return path.find_last_of(L'\\');
  }

  /// Determines if the specified filename matches any of the specified file patterns.
  /// Input filename must not contain any backslash separators, as it is intended to represent a
  /// file within a directory rather than a path.
  /// @param [in] candidateFileName File name to check for matches with any file pattern.
  /// @param [in] filePatterns Patterns against which to compare the candidate file name.
  /// @return `true` if any file pattern produces a match, `false` otherwise.
  static bool FileNameMatchesAnyPatternInternal(
      std::wstring_view candidateFileName, const std::vector<std::wstring>& filePatterns)
  {
    if (true == filePatterns.empty()) return true;

    for (const auto& filePattern : filePatterns)
    {
      if (true == Strings::FileNameMatchesPattern(candidateFileName, filePattern)) return true;
    }

    return false;
  }

  /// Computes and returns the result of redirecting from the specified candidate path from one
  /// directory to another. Input candidate path is split into two parts: the directory part,
  /// which identifies the absolute directory in which the file is located, and the file part,
  /// which identifies the file within its directory. If the source directory matches the
  /// candidate path and a file pattern matches then a redirection can occur to the destination
  /// directory. Otherwise no redirection occurs and no output is produced.
  /// @param [in] candidatePathDirectoryPart Directory portion of the candidate path, which is an
  /// absolute path and does not contain a trailing backslash.
  /// @param [in] candidatePathFilePart File portion of the candidate path without any leading
  /// backslash.
  /// @param [in] fromDirectory Origin directory of the redirection. Typically this comes from a
  /// filesystem rule.
  /// @param [in] toDirectory Target directory of the redirection. Typically this comes from a
  /// filesystem rule.
  /// @param [in] filePatterns File patterns against which to check the file part of the
  /// redirection query.
  /// @param [in] namespacePrefix Windows namespace prefix to be prepended to the output string,
  /// if one is generated.
  /// @param [in] extraSuffix Additional suffix to add to the end of the output string, if one is
  /// generated.
  /// @return Redirected location as an absolute path, if redirection occurred successfully.
  static std::optional<TemporaryString> RedirectPathInternal(
      std::wstring_view candidatePathDirectoryPart,
      std::wstring_view candidatePathFilePart,
      std::wstring_view fromDirectory,
      std::wstring_view toDirectory,
      const std::vector<std::wstring>& filePatterns,
      std::wstring_view namespacePrefix,
      std::wstring_view extraSuffix)
  {
    switch (DirectoryCompareInternal(candidatePathDirectoryPart, fromDirectory))
    {
      case EDirectoryCompareResult::Equal:
      {
        // Candidate directory and origin directory are equal. This is the simplest case.
        // If the candidate file part is either empty or matches the redirection file
        // patterns then a redirection can occur. For example, if we are asked to redirect
        // "C:\Dir1\file.txt" from "C:\Dir1" to "C:\Dir5000" then the result would be
        // "C:\Dir5000\file.txt" but only if "file.txt" matches the file patterns for
        // redirection.

        if ((false == candidatePathFilePart.empty()) &&
            (false == FileNameMatchesAnyPatternInternal(candidatePathFilePart, filePatterns)))
          return std::nullopt;
        break;
      }

      case EDirectoryCompareResult::CandidateIsChild:
      case EDirectoryCompareResult::CandidateIsDescendant:
      {
        // Candidate directory is a descendent of the origin directory. This case is
        // slightly more complicated. Since an entire directory hierarchy could be involved,
        // we need to extract the part of the candidate directory that represents the
        // immediate child of the origin directory. If that immediate child matches a
        // redirection file pattern (here we ignore the candidate path file part) then the
        // redirection can occur. For example, if we are asked to redirect
        // "C:\Dir1\Dir2\file.txt" from "C:\Dir1" to "C:\Dir5000" then the result would be
        // "C:\Dir5000\Dir2\file.txt" but only if "Dir2" matches any of this rule's file
        // patterns.

        std::wstring_view immediateChildOfFromDirectory(candidatePathDirectoryPart);
        immediateChildOfFromDirectory.remove_prefix(1 + fromDirectory.length());

        const size_t lastBackslashPos = immediateChildOfFromDirectory.find_first_of(L'\\');
        if (std::wstring_view::npos != lastBackslashPos)
          immediateChildOfFromDirectory.remove_suffix(
              immediateChildOfFromDirectory.length() -
              immediateChildOfFromDirectory.find_first_of(L'\\'));

        if (false == FileNameMatchesAnyPatternInternal(immediateChildOfFromDirectory, filePatterns))
          return std::nullopt;

        break;
      }

      default:
        return std::nullopt;
    }

    candidatePathDirectoryPart.remove_prefix(fromDirectory.length());

    TemporaryString redirectedPath;
    redirectedPath << namespacePrefix << toDirectory << candidatePathDirectoryPart;
    if (false == candidatePathFilePart.empty()) redirectedPath << L'\\' << candidatePathFilePart;
    if (false == extraSuffix.empty()) redirectedPath << extraSuffix;

    return redirectedPath;
  }

  FilesystemRule::FilesystemRule(
      std::wstring_view originDirectoryFullPath,
      std::wstring_view targetDirectoryFullPath,
      std::vector<std::wstring>&& filePatterns,
      ERedirectMode redirectMode)
      : redirectMode(redirectMode),
        originDirectorySeparator(FinalSeparatorPosition(originDirectoryFullPath)),
        targetDirectorySeparator(FinalSeparatorPosition(targetDirectoryFullPath)),
        originDirectoryFullPath(originDirectoryFullPath),
        targetDirectoryFullPath(targetDirectoryFullPath),
        filePatterns(std::move(filePatterns)),
        name()
  {
    // The specific implementation used for comparing file names to file patterns requires that
    // all pattern strings be uppercase. Comparisons remain case-insensitive. This is just a
    // documented implementation detail of the comparison function itself.
    for (auto& filePattern : this->filePatterns)
    {
      for (size_t i = 0; i < filePattern.size(); ++i)
        filePattern[i] = std::towupper(filePattern[i]);
    }
  }

  EDirectoryCompareResult FilesystemRule::DirectoryCompareWithOrigin(
      std::wstring_view candidateDirectory) const
  {
    return DirectoryCompareInternal(candidateDirectory, originDirectoryFullPath);
  }

  EDirectoryCompareResult FilesystemRule::DirectoryCompareWithTarget(
      std::wstring_view candidateDirectory) const
  {
    return DirectoryCompareInternal(candidateDirectory, targetDirectoryFullPath);
  }

  bool FilesystemRule::FileNameMatchesAnyPattern(std::wstring_view candidateFileName) const
  {
    return FileNameMatchesAnyPatternInternal(candidateFileName, filePatterns);
  }

  std::optional<TemporaryString> FilesystemRule::RedirectPathOriginToTarget(
      std::wstring_view candidatePathDirectoryPart,
      std::wstring_view candidatePathFilePart,
      std::wstring_view namespacePrefix,
      std::wstring_view extraSuffix) const
  {
    return RedirectPathInternal(
        candidatePathDirectoryPart,
        candidatePathFilePart,
        originDirectoryFullPath,
        targetDirectoryFullPath,
        filePatterns,
        namespacePrefix,
        extraSuffix);
  }

  std::optional<TemporaryString> FilesystemRule::RedirectPathTargetToOrigin(
      std::wstring_view candidatePathDirectoryPart,
      std::wstring_view candidatePathFilePart,
      std::wstring_view namespacePrefix,
      std::wstring_view extraSuffix) const
  {
    return RedirectPathInternal(
        candidatePathDirectoryPart,
        candidatePathFilePart,
        targetDirectoryFullPath,
        originDirectoryFullPath,
        filePatterns,
        namespacePrefix,
        extraSuffix);
  }
} // namespace Pathwinder
