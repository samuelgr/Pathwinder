/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file FilesystemInstruction.cpp
 *   Implementation of functionality for querying and manipulating instructions issued by
 *   filesystem directory objects.
 **************************************************************************************************/

#include "FilesystemInstruction.h"

#include <string_view>

#include <Infra/Core/DebugAssert.h>

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
      EFilePatternMatchCondition filePatternMatchCondition,
      RelatedFilesystemRuleContainer::TFilesystemRulesIndex filePatternMatchRuleIndex)
      : directoryPathSource(directoryPathSource),
        filePatternSource({.multipleRules = &filePatternSource}),
        filePatternMatchConfig(
            {.invertMatches = invertFilePatternMatches,
             .filePatternMatchCondition = filePatternMatchCondition,
             .filePatternMatchRuleIndex = filePatternMatchRuleIndex})
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
        if (EFilePatternMatchCondition::SingleRuleOnly ==
            filePatternMatchConfig.filePatternMatchCondition)
          return filePatternSource.singleRule->GetOriginDirectoryFullPath();
        else
          return filePatternSource.multipleRules
              ->GetRuleByIndex(filePatternMatchConfig.filePatternMatchRuleIndex)
              ->GetOriginDirectoryFullPath();

      case EDirectoryPathSource::FilePatternSourceTargetDirectory:
        if (EFilePatternMatchCondition::SingleRuleOnly ==
            filePatternMatchConfig.filePatternMatchCondition)
          return filePatternSource.singleRule->GetTargetDirectoryFullPath();
        else
          return filePatternSource.multipleRules
              ->GetRuleByIndex(filePatternMatchConfig.filePatternMatchRuleIndex)
              ->GetTargetDirectoryFullPath();

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
      const auto matchingFilesystemRuleAndPosition =
          filePatternSource.multipleRules->RuleMatchingFileName(
              filename, filePatternMatchConfig.filePatternMatchRuleIndex);

      const FilesystemRule* matchingFilesystemRule = matchingFilesystemRuleAndPosition.first;
      const RelatedFilesystemRuleContainer::TFilesystemRulesIndex matchingFilesystemRulePosition =
          matchingFilesystemRuleAndPosition.second;

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

        case EFilePatternMatchCondition::MatchByPositionInvertAllPriorToSelected:
          return (
              (matchingFilesystemRulePosition == filePatternMatchConfig.filePatternMatchRuleIndex)
                  ? matchFoundReturnValue
                  : !matchFoundReturnValue);

        default:
          break;
      }

      DebugAssert(false, "Unrecognized file pattern match condition.");
      return false;
    }
  }
} // namespace Pathwinder
