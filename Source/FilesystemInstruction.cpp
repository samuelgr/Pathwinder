/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file FilesystemInstruction.cpp
 *   Implementation of functionality for querying and manipulating instructions issued by
 *   filesystem directory objects.
 **************************************************************************************************/

#include "FilesystemInstruction.h"

#include <string_view>

#include "DebugAssert.h"

namespace Pathwinder
{
  DirectoryEnumerationInstruction::SingleDirectoryEnumeration::SingleDirectoryEnumeration(void)
      : filePatternSource(),
        filePatternMatchConfig(),
        directoryPathSource(EDirectoryPathSource::None)
  {}

  DirectoryEnumerationInstruction::SingleDirectoryEnumeration::SingleDirectoryEnumeration(
      EDirectoryPathSource directoryPathSource)
      : directoryPathSource(directoryPathSource), filePatternSource(), filePatternMatchConfig()
  {}

  DirectoryEnumerationInstruction::SingleDirectoryEnumeration::SingleDirectoryEnumeration(
      EDirectoryPathSource directoryPathSource,
      const FilesystemRule& filePatternSource,
      bool invertFilePatternMatches)
      : directoryPathSource(directoryPathSource),
        filePatternSource({.singleRule = &filePatternSource}),
        filePatternMatchConfig(
            {.invertMatches = invertFilePatternMatches,
             .filePatternMatchCondition = EFilePatternMatchCondition::SingleRuleOnly})
  {}

  DirectoryEnumerationInstruction::SingleDirectoryEnumeration::SingleDirectoryEnumeration(
      EDirectoryPathSource directoryPathSource,
      const RelatedFilesystemRuleContainer& filePatternSource,
      bool invertFilePatternMatches,
      EFilePatternMatchCondition filePatternMatchCondition)
      : directoryPathSource(directoryPathSource),
        filePatternSource({.multipleRules = &filePatternSource}),
        filePatternMatchConfig(
            {.invertMatches = invertFilePatternMatches,
             .filePatternMatchCondition = filePatternMatchCondition})
  {}

  std::wstring_view
      DirectoryEnumerationInstruction::SingleDirectoryEnumeration::SelectDirectoryPath(
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
            (EFilePatternMatchCondition::SingleRuleOnly !=
             filePatternMatchConfig.filePatternMatchCondition) ||
                (nullptr != filePatternSource.singleRule),
            "Attempting to use a filesystem rule as a directory path source without a singular filesystem rule present.");
        return filePatternSource.singleRule->GetOriginDirectoryFullPath();

      case EDirectoryPathSource::FilePatternSourceTargetDirectory:
        DebugAssert(
            (EFilePatternMatchCondition::SingleRuleOnly !=
             filePatternMatchConfig.filePatternMatchCondition) ||
                (nullptr != filePatternSource.singleRule),
            "Attempting to use a filesystem rule as a directory path source without a singular filesystem rule present.");
        return filePatternSource.singleRule->GetTargetDirectoryFullPath();

      default:
        return std::wstring_view();
    }
  }

  bool DirectoryEnumerationInstruction::SingleDirectoryEnumeration::
      ShouldIncludeInDirectoryEnumeration(std::wstring_view filename) const
  {
    if (EFilePatternMatchCondition::SingleRuleOnly ==
        filePatternMatchConfig.filePatternMatchCondition)
    {
      if (nullptr == filePatternSource.singleRule) return true;
      return (
          filePatternSource.singleRule->FileNameMatchesAnyPattern(filename) !=
          filePatternMatchConfig.invertMatches);
    }
    else
    {
      if (nullptr == filePatternSource.multipleRules) return true;

      const bool matchFoundReturnValue = !filePatternMatchConfig.invertMatches;
      const FilesystemRule* matchingFilesystemRule =
          filePatternSource.multipleRules->RuleMatchingFileName(filename);
      const bool hasMatchingFilesystemRule = (matchingFilesystemRule != nullptr);
      if (false == hasMatchingFilesystemRule) return !matchFoundReturnValue;

      switch (filePatternMatchConfig.filePatternMatchCondition)
      {
        case EFilePatternMatchCondition::MatchAny:
          return matchFoundReturnValue;

        case EFilePatternMatchCondition::MatchByRedirectModeInvertOverlay:
          switch (matchingFilesystemRule->GetRedirectMode())
          {
            case ERedirectMode::Overlay:
              return !matchFoundReturnValue;

            default:
              return matchFoundReturnValue;
          }

        default:
          break;
      }

      DebugAssert(false, "Unrecognized file pattern match condition.");
      return false;
    }
  }
} // namespace Pathwinder
