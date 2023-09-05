/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file FilesystemDirectorBuilder.cpp
 *   Implementation of functionality for building new filesystem director objects piece-wise at
 *   runtime.
 **************************************************************************************************/

#include "FilesystemDirectorBuilder.h"

#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ApiWindows.h"
#include "Configuration.h"
#include "DebugAssert.h"
#include "FilesystemOperations.h"
#include "FilesystemRule.h"
#include "Message.h"
#include "Resolver.h"
#include "Strings.h"
#include "TemporaryBuffer.h"
#include "ValueOrError.h"

namespace Pathwinder
{
  /// Reads the configured redirection mode from a filesystem rule configuration section or, if
  /// the redirection mode is not present, returns a default value.
  /// @param [in] configSection Configuration section object containing the filesystem rule's
  /// configuration information.
  /// @return Redirection mode enumerator, if it is either absent from the configuration section
  /// data object or present and maps to a valid enumerator.
  static std::optional<FilesystemRule::ERedirectMode>
      RedirectModeFromConfigurationSection(const Configuration::Section& configSection)
  {
    constexpr FilesystemRule::ERedirectMode kDefaultRedirectMode =
        FilesystemRule::ERedirectMode::Simple;

    static const std::unordered_map<
        std::wstring_view,
        FilesystemRule::ERedirectMode,
        Strings::CaseInsensitiveHasher<wchar_t>,
        Strings::CaseInsensitiveEqualityComparator<wchar_t>>
        kRedirectModeStrings = {
            {L"Simple", FilesystemRule::ERedirectMode::Simple},
            {L"Overlay", FilesystemRule::ERedirectMode::Overlay},
            {L"OverlayCopyOnWrite", FilesystemRule::ERedirectMode::OverlayCopyOnWrite}};

    if (false ==
        configSection.NameExists(Strings::kStrConfigurationSettingFilesystemRuleRedirectMode))
      return kDefaultRedirectMode;

    const auto redirectModeIter = kRedirectModeStrings.find(*configSection.GetFirstStringValue(
        Strings::kStrConfigurationSettingFilesystemRuleRedirectMode));
    if (kRedirectModeStrings.cend() == redirectModeIter) return std::nullopt;

    return redirectModeIter->second;
  }

  /// Generates a string representation of the specified redirection mode enumerator.
  /// Useful for logging.
  /// @param [in] redirectMode Redirection mode enumerator.
  /// @return String representation of the redirection mode enumerator.
  static std::wstring_view RedirectModeToString(FilesystemRule::ERedirectMode redirectMode)
  {
    switch (redirectMode)
    {
      case FilesystemRule::ERedirectMode::Simple:
        return L"Simple";

      case FilesystemRule::ERedirectMode::Overlay:
        return L"Overlay";

      case FilesystemRule::ERedirectMode::OverlayCopyOnWrite:
        return L"OverlayCopyOnWrite";

      default:
        return L"(unknown)";
    }
  }

  std::optional<FilesystemDirector> FilesystemDirectorBuilder::BuildFromConfigurationData(
      Configuration::ConfigurationData& configData)
  {
    FilesystemDirectorBuilder builder;

    bool builderHasRuleErrors = false;

    // All sections whose names begin with "FilesystemRule:" contain filesystem rule data.
    // The rule name is the part of the section name that comes after the prefix, and the
    // section content defines the rule itself.

    for (auto filesystemRuleIter = configData.Sections().lower_bound(
             Strings::kStrConfigurationSectionFilesystemRulePrefix);
         ((filesystemRuleIter != configData.Sections().end()) &&
          (filesystemRuleIter->first.starts_with(
              Strings::kStrConfigurationSectionFilesystemRulePrefix)));)
    {
      auto nextFilesystemRuleIter = filesystemRuleIter;
      ++nextFilesystemRuleIter;

      auto filesystemRuleSectionNameAndContents = configData.ExtractSection(filesystemRuleIter);
      filesystemRuleIter = nextFilesystemRuleIter;

      std::wstring_view filesystemRuleName =
          static_cast<std::wstring_view>(filesystemRuleSectionNameAndContents.first);
      filesystemRuleName.remove_prefix(
          Strings::kStrConfigurationSectionFilesystemRulePrefix.length());

      Configuration::Section& filesystemRuleContents = filesystemRuleSectionNameAndContents.second;

      auto addFilesystemRuleResult =
          builder.AddRuleFromConfigurationSection(filesystemRuleName, filesystemRuleContents);
      if (addFilesystemRuleResult.HasValue())
      {
        const FilesystemRule& newFilesystemRule = *(addFilesystemRuleResult.Value());
        const std::wstring_view newFilesystemRuleRedirectMode =
            RedirectModeToString(newFilesystemRule.GetRedirectMode());

        Message::OutputFormatted(
            Message::ESeverity::Info,
            L"Successfully created filesystem rule \"%.*s\".",
            static_cast<int>(newFilesystemRule.GetName().length()),
            newFilesystemRule.GetName().data());
        Message::OutputFormatted(
            Message::ESeverity::Info,
            L"  Redirection mode = %.*s",
            static_cast<int>(newFilesystemRuleRedirectMode.length()),
            newFilesystemRuleRedirectMode.data());
        Message::OutputFormatted(
            Message::ESeverity::Info,
            L"  Origin directory = \"%.*s\"",
            static_cast<int>(newFilesystemRule.GetOriginDirectoryFullPath().length()),
            newFilesystemRule.GetOriginDirectoryFullPath().data());
        Message::OutputFormatted(
            Message::ESeverity::Info,
            L"  Target directory = \"%.*s\"",
            static_cast<int>(newFilesystemRule.GetTargetDirectoryFullPath().length()),
            newFilesystemRule.GetTargetDirectoryFullPath().data());

        for (const auto& filePattern : newFilesystemRule.GetFilePatterns())
          Message::OutputFormatted(
              Message::ESeverity::Info, L"  File pattern = \"%s\"", filePattern.c_str());
      }
      else
      {
        Message::Output(Message::ESeverity::Error, addFilesystemRuleResult.Error().AsCString());
        builderHasRuleErrors = true;
      }
    }

    if (true == builderHasRuleErrors) return std::nullopt;

    if (0 == builder.CountOfRules())
    {
      // It is not an error for a configuration data object to contain no filesystem rules.
      // The resulting filesystem director object will simply do nothing.

      Message::Output(
          Message::ESeverity::Warning,
          L"Successfully built a filesystem director configuration, but it contains no filesystem rules.");
      return FilesystemDirector();
    }

    auto buildResult = builder.Build();
    if (buildResult.HasValue())
    {
      Message::OutputFormatted(
          Message::ESeverity::Info,
          L"Successfully built a filesystem director configuration with %u rules(s).",
          buildResult.Value().CountOfRules());
      return std::move(buildResult.Value());
    }
    else
    {
      Message::Output(Message::ESeverity::Error, buildResult.Error().AsCString());
      return std::nullopt;
    }
  }

  bool FilesystemDirectorBuilder::IsValidDirectoryString(std::wstring_view candidateDirectory)
  {
    // These characters are disallowed at any position in the directory string.
    // Directory strings cannot contain wildcards but can contain backslashes as separators and
    // colons to identify drives.
    constexpr std::wstring_view kDisallowedCharacters = L"/*?\"<>|";

    // These characters cannot make up the entirety of a path component.
    // For example, adding '.' here prevents ".", "..", "...", "....", and so on from being
    // valid path components.
    constexpr std::wstring_view kDisallowedCharactersForEntirePathComponent = L".";

    // Valid directory strings must begin with a drive letter and a colon.
    // The length must therefore be at least two.
    if ((candidateDirectory.length() < 2) || (0 == std::iswalpha(candidateDirectory[0])) ||
        (L':' != candidateDirectory[1]))
      return false;

    // Strings containing two back-to-back backslash characters are not valid directory strings.
    if (true == candidateDirectory.contains(L"\\\\")) return false;

    // From this point the candidate directory is separated into individual path components
    // using backslash as a delimiter, and each such component is checked individually.
    for (std::wstring_view pathComponent : Strings::Tokenizer(candidateDirectory, L"\\"))
    {
      if (true == pathComponent.empty()) continue;

      for (wchar_t pathComponentChar : pathComponent)
      {
        if ((0 == std::iswprint(pathComponentChar)) ||
            (kDisallowedCharacters.contains(pathComponentChar)))
          return false;
      }

      for (wchar_t disallowedEntireComponentChar : kDisallowedCharactersForEntirePathComponent)
      {
        if (std::wstring_view::npos ==
            pathComponent.find_first_not_of(disallowedEntireComponentChar))
          return false;
      }
    }

    return true;
  }

  bool FilesystemDirectorBuilder::IsValidFilePatternString(std::wstring_view candidateFilePattern)
  {
    // These characters are disallowed inside file patterns.
    // File patterns identify files within directories and cannot identify subdirectories or
    // drives. Wildcards are allowed, but backslashes and colons are not.
    constexpr std::wstring_view kDisallowedCharacters = L"\\/:\"<>|";

    if (true == candidateFilePattern.empty()) return false;

    for (wchar_t c : candidateFilePattern)
    {
      if ((0 == iswprint(c)) || (kDisallowedCharacters.contains(c))) return false;
    }

    return true;
  }

  ValueOrError<const FilesystemRule*, TemporaryString> FilesystemDirectorBuilder::AddRule(
      std::wstring_view ruleName,
      std::wstring_view originDirectory,
      std::wstring_view targetDirectory,
      std::vector<std::wstring>&& filePatterns,
      FilesystemRule::ERedirectMode redirectMode)
  {
    if (true == filesystemRules.contains(ruleName))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Constraint violation: Rule with the same name already exists.",
          static_cast<int>(ruleName.length()),
          ruleName.data());

    for (std::wstring_view filePattern : filePatterns)
    {
      if (false == IsValidFilePatternString(filePattern))
        return Strings::FormatString(
            L"Error while creating filesystem rule \"%.*s\": File pattern: %s: Either empty or contains disallowed characters.",
            static_cast<int>(ruleName.length()),
            ruleName.data(),
            filePattern.data());
    }

    // For each of the origin and target directories:
    // 1. Resolve any embedded references.
    // 2. Check for any invalid characters.
    // 3. Verify that the resulting directory is not already in use as an origin or target
    // directory for another filesystem rule.
    // 4. Verify that the resulting directory is not a filesystem root (i.e. it has a parent
    // directory). If all operations succeed then the filesystem rule object can be created.

    Resolver::ResolvedStringOrError maybeOriginDirectoryResolvedString =
        Resolver::ResolveAllReferences(originDirectory);
    if (true == maybeOriginDirectoryResolvedString.HasError())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Origin directory: %s.",
          static_cast<int>(ruleName.length()),
          ruleName.data(),
          maybeOriginDirectoryResolvedString.Error().AsCString());
    if (false == IsValidDirectoryString(maybeOriginDirectoryResolvedString.Value()))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Origin directory: Either empty or contains disallowed characters.",
          static_cast<int>(ruleName.length()),
          ruleName.data());

    TemporaryString originDirectoryFullPath = maybeOriginDirectoryResolvedString.Value();
    originDirectoryFullPath.RemoveTrailing(L'\\');

    if (false == originDirectoryFullPath.AsStringView().contains(L'\\'))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Constraint violation: Origin directory cannot be a filesystem root.",
          static_cast<int>(ruleName.length()),
          ruleName.data());
    if (true == originDirectoryFullPath.Empty())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Origin directory: Failed to resolve full path: %s",
          static_cast<int>(ruleName.length()),
          ruleName.data(),
          Strings::SystemErrorCodeString(GetLastError()).AsCString());
    if (true == originDirectoryFullPath.Overflow())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Origin directory: Full path exceeds limit of %u characters.",
          static_cast<int>(ruleName.length()),
          ruleName.data(),
          originDirectoryFullPath.Capacity());
    if (true == HasDirectory(originDirectoryFullPath))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Constraint violation: Origin directory is already in use as either an origin or target directory by another rule.",
          static_cast<int>(ruleName.length()),
          ruleName.data());

    Resolver::ResolvedStringOrError maybeTargetDirectoryResolvedString =
        Resolver::ResolveAllReferences(targetDirectory);
    if (true == maybeTargetDirectoryResolvedString.HasError())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Target directory: %s.",
          static_cast<int>(ruleName.length()),
          ruleName.data(),
          maybeTargetDirectoryResolvedString.Error().AsCString());
    if (false == IsValidDirectoryString(maybeTargetDirectoryResolvedString.Value()))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Target directory: Either empty or contains disallowed characters.",
          static_cast<int>(ruleName.length()),
          ruleName.data());

    TemporaryString targetDirectoryFullPath = maybeTargetDirectoryResolvedString.Value();
    targetDirectoryFullPath.RemoveTrailing(L'\\');

    if (false == targetDirectoryFullPath.AsStringView().contains(L'\\'))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Constraint violation: Target directory cannot be a filesystem root.",
          static_cast<int>(ruleName.length()),
          ruleName.data());
    if (true == targetDirectoryFullPath.Empty())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Target directory: Failed to resolve full path: %s",
          static_cast<int>(ruleName.length()),
          ruleName.data(),
          Strings::SystemErrorCodeString(GetLastError()).AsCString());
    if (true == targetDirectoryFullPath.Overflow())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Target directory: Full path exceeds limit of %u characters.",
          static_cast<int>(ruleName.length()),
          ruleName.data(),
          targetDirectoryFullPath.Capacity());
    if (true == HasOriginDirectory(targetDirectoryFullPath))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Constraint violation: Target directory is already in use as an origin directory by another rule.",
          static_cast<int>(ruleName.length()),
          ruleName.data());

    const auto createResult = filesystemRules.emplace(
        std::wstring(ruleName),
        FilesystemRule(
            originDirectoryFullPath.AsStringView(),
            targetDirectoryFullPath.AsStringView(),
            std::move(filePatterns),
            redirectMode));
    DebugAssert(
        createResult.second,
        "FilesystemDirectorBuilder consistency check failed due to unsuccessful creation of a supposedly-unique filesystem rule.");

    std::wstring_view newRuleName = createResult.first->first;
    FilesystemRule* newRule = &createResult.first->second;

    newRule->SetName(newRuleName);
    originDirectories.Insert(newRule->GetOriginDirectoryFullPath(), *newRule);
    targetDirectories.emplace(newRule->GetTargetDirectoryFullPath());

    return newRule;
  }

  ValueOrError<const FilesystemRule*, TemporaryString>
      FilesystemDirectorBuilder::AddRuleFromConfigurationSection(
          std::wstring_view ruleName, Configuration::Section& configSection)
  {
    auto maybeOriginDirectory = configSection.ExtractFirstStringValue(
        Strings::kStrConfigurationSettingFilesystemRuleOriginDirectory);
    if (false == maybeOriginDirectory.has_value())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Missing origin directory.",
          static_cast<int>(ruleName.length()),
          ruleName.data());

    auto maybeTargetDirectory = configSection.ExtractFirstStringValue(
        Strings::kStrConfigurationSettingFilesystemRuleTargetDirectory);
    if (false == maybeTargetDirectory.has_value())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Missing target directory.",
          static_cast<int>(ruleName.length()),
          ruleName.data());

    auto maybeRedirectMode = RedirectModeFromConfigurationSection(configSection);
    if (false == maybeRedirectMode.has_value())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%.*s\": Invalid redirection mode.",
          static_cast<int>(ruleName.length()),
          ruleName.data());

    auto filePatterns =
        configSection
            .ExtractStringValues(Strings::kStrConfigurationSettingFilesystemRuleFilePattern)
            .value_or(std::vector<std::wstring>());

    return AddRule(
        ruleName,
        std::move(*maybeOriginDirectory),
        std::move(*maybeTargetDirectory),
        std::move(filePatterns),
        std::move(*maybeRedirectMode));
  }

  ValueOrError<FilesystemDirector, TemporaryString> FilesystemDirectorBuilder::Build(void)
  {
    if (true == filesystemRules.empty())
      return L"Error while building a filesystem director configuration: Internal error: Attempted to finalize an empty registry.";

    PrefixIndex<wchar_t, FilesystemRule> allDirectories;

    for (const auto& filesystemRuleRecord : filesystemRules)
    {
      const FilesystemRule& filesystemRule = filesystemRuleRecord.second;

      const bool originExists =
          FilesystemOperations::Exists(filesystemRule.GetOriginDirectoryFullPath().data());
      const bool originIsDirectory =
          FilesystemOperations::IsDirectory(filesystemRule.GetOriginDirectoryFullPath().data());
      if (false == (!originExists || originIsDirectory))
        return Strings::FormatString(
            L"Error while building a filesystem director configuration: Filesystem rule \"%s\": Constraint violation: Origin directory must either not exist at all or exist as a real directory.",
            filesystemRuleRecord.first.c_str());

      const TemporaryString originDirectoryParent = filesystemRule.GetOriginDirectoryParent();
      if ((false == FilesystemOperations::IsDirectory(originDirectoryParent.AsCString())) &&
          (false == HasOriginDirectory(originDirectoryParent)))
        return Strings::FormatString(
            L"Error while building a filesystem director configuration: Filesystem rule \"%s\": Constraint violation: Parent of origin directory must either exist as a real directory or be the origin directory of another filesystem rule.",
            filesystemRuleRecord.first.c_str());

      // Both origin and target directories are added to a prefix index containing all
      // directories. This is done to implement the check for constraint (3). Directory
      // uniqueness should already have been verified at filesystem rule creation time, so it
      // is an internal error indicative of a bug if those checks passed but there is still
      // some non-uniqueness here.
      if (false ==
          allDirectories.Insert(filesystemRule.GetOriginDirectoryFullPath(), filesystemRule).second)
        return Strings::FormatString(
            L"Error while building a filesystem director configuration: Filesystem rule \"%s\": Internal error: Origin directory conflicts with another rule, but this should have been caught already.",
            filesystemRuleRecord.first.c_str());
      if (false ==
          allDirectories.Insert(filesystemRule.GetTargetDirectoryFullPath(), filesystemRule).second)
        return Strings::FormatString(
            L"Error while building a filesystem director configuration: Filesystem rule \"%s\": Internal error: Target directory conflicts with another rule, but this should have been caught already.",
            filesystemRuleRecord.first.c_str());
    }

    // This loop iterates over all rules one more time and checks all the target directories for
    // ancestors in the prefix index. Since the prefix index contains all origin and target
    // directories this check directly implements constraint (3). If an ancestor is located then
    // the configuration violates this constraint and should be rejected.
    for (const auto& filesystemRuleRecord : filesystemRules)
    {
      auto targetDirectoryNode =
          allDirectories.Find(filesystemRuleRecord.second.GetTargetDirectoryFullPath());
      if (nullptr == targetDirectoryNode)
        return Strings::FormatString(
            L"Error while building a filesystem director configuration: Filesystem rule \"%s\": Internal error: Target directory is improperly properly indexed.",
            filesystemRuleRecord.first.c_str());

      auto ancestorNode = targetDirectoryNode->GetClosestAncestor();
      if (nullptr != ancestorNode)
      {
        std::wstring_view conflictingFilesystemRuleName = ancestorNode->GetData()->GetName();
        return Strings::FormatString(
            L"Error while building a filesystem director configuration: Filesystem rule \"%s\": Constraint violation: Target directory must not be a descendent of the origin or target directory of filesystem rule \"%.*s\".",
            filesystemRuleRecord.first.c_str(),
            static_cast<int>(conflictingFilesystemRuleName.length()),
            conflictingFilesystemRuleName.data());
      }
    }

    return FilesystemDirector(std::move(filesystemRules), std::move(originDirectories));
  }
} // namespace Pathwinder
