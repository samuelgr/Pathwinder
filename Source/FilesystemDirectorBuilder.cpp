/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
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

#include <Infra/DebugAssert.h>
#include <Infra/TemporaryBuffer.h>
#include <Infra/ValueOrError.h>

#include "ApiWindows.h"
#include "Configuration.h"
#include "FilesystemOperations.h"
#include "FilesystemRule.h"
#include "Message.h"
#include "Resolver.h"
#include "Strings.h"

namespace Pathwinder
{
  /// Reads the configured redirection mode from a filesystem rule configuration section or, if
  /// the redirection mode is not present, returns a default value.
  /// @param [in] configSection Configuration section object containing the filesystem rule's
  /// configuration information.
  /// @return Redirection mode enumerator, if it is either absent from the configuration section
  /// data object or present and maps to a valid enumerator.
  static std::optional<ERedirectMode> RedirectModeFromConfigurationSection(
      const Configuration::Section& configSection)
  {
    constexpr ERedirectMode kDefaultRedirectMode = ERedirectMode::Simple;

    static const std::unordered_map<
        std::wstring_view,
        ERedirectMode,
        Strings::CaseInsensitiveHasher<wchar_t>,
        Strings::CaseInsensitiveEqualityComparator<wchar_t>>
        kRedirectModeStrings = {
            {L"Simple", ERedirectMode::Simple}, {L"Overlay", ERedirectMode::Overlay}};

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
  static std::wstring_view RedirectModeToString(ERedirectMode redirectMode)
  {
    switch (redirectMode)
    {
      case ERedirectMode::Simple:
        return L"Simple";

      case ERedirectMode::Overlay:
        return L"Overlay";

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

      auto addFilesystemRuleResult = builder.AddRuleFromConfigurationSection(
          std::wstring(filesystemRuleName), filesystemRuleContents);
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

        if (true == newFilesystemRule.HasFilePatterns())
        {
          for (const auto& filePattern : newFilesystemRule.GetFilePatterns())
            Message::OutputFormatted(
                Message::ESeverity::Info, L"  File pattern = \"%s\"", filePattern.c_str());
        }
        else
        {
          Message::Output(Message::ESeverity::Info, L"  File pattern = \"*\"");
        }
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

  Infra::ValueOrError<const FilesystemRule*, Infra::TemporaryString>
      FilesystemDirectorBuilder::AddRule(
          std::wstring&& ruleName,
          std::wstring_view originDirectory,
          std::wstring_view targetDirectory,
          std::vector<std::wstring>&& filePatterns,
          ERedirectMode redirectMode)
  {
    if (true == filesystemRuleNames.contains(ruleName))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Constraint violation: Rule with the same name already exists.",
          ruleName.c_str());

    for (std::wstring_view filePattern : filePatterns)
    {
      if (false == IsValidFilePatternString(filePattern))
        return Strings::FormatString(
            L"Error while creating filesystem rule \"%s\": File pattern: %s: Either empty or contains disallowed characters.",
            ruleName.c_str(),
            filePattern.data());
    }

    // For each of the origin and target directories:
    // 1. Resolve any embedded references.
    // 2. Check for any invalid characters.
    // 3. Verify that constraints are satisfied. Origin directories cannot already be in use as the
    // target directory by another rule, but multiple rules are allowed to have the same origin
    // directory. Target directories cannot be in use as an origin or a target directory by another
    // rule.
    // 4. Verify that the resulting directory is not a filesystem root (i.e. it has a parent
    // directory). If all operations succeed then the filesystem rule object can be created.

    Resolver::ResolvedStringOrError maybeOriginDirectoryResolvedString =
        Resolver::ResolveAllReferences(originDirectory);
    if (true == maybeOriginDirectoryResolvedString.HasError())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Origin directory: %s.",
          ruleName.c_str(),
          maybeOriginDirectoryResolvedString.Error().AsCString());
    maybeOriginDirectoryResolvedString =
        Resolver::ResolveRelativePathComponents(maybeOriginDirectoryResolvedString.Value());
    if (true == maybeOriginDirectoryResolvedString.HasError())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Origin directory: %s.",
          ruleName.c_str(),
          maybeOriginDirectoryResolvedString.Error().AsCString());
    if (false == IsValidDirectoryString(maybeOriginDirectoryResolvedString.Value()))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Origin directory: Either empty, relative, or contains disallowed characters.",
          ruleName.c_str());

    std::wstring_view originDirectoryFullPath = Strings::RemoveTrailing(
        std::wstring_view(maybeOriginDirectoryResolvedString.Value()), L'\\');

    if (false == originDirectoryFullPath.contains(L'\\'))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Constraint violation: Origin directory cannot be a filesystem root.",
          ruleName.c_str());
    if (true == originDirectoryFullPath.empty())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Origin directory: Failed to resolve full path: %s",
          ruleName.c_str(),
          Strings::SystemErrorCodeString(GetLastError()).AsCString());
    if (true == HasTargetDirectory(originDirectoryFullPath))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Constraint violation: Origin directory is already in use as a target directory by another rule.",
          ruleName.c_str());

    Resolver::ResolvedStringOrError maybeTargetDirectoryResolvedString =
        Resolver::ResolveAllReferences(targetDirectory);
    if (true == maybeTargetDirectoryResolvedString.HasError())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Target directory: %s.",
          ruleName.c_str(),
          maybeTargetDirectoryResolvedString.Error().AsCString());
    maybeTargetDirectoryResolvedString =
        Resolver::ResolveRelativePathComponents(maybeTargetDirectoryResolvedString.Value());
    if (true == maybeTargetDirectoryResolvedString.HasError())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Target directory: %s.",
          ruleName.c_str(),
          maybeTargetDirectoryResolvedString.Error().AsCString());

    if (false == IsValidDirectoryString(maybeTargetDirectoryResolvedString.Value()))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Target directory: Either empty, relative, or contains disallowed characters.",
          ruleName.c_str());

    std::wstring_view targetDirectoryFullPath = Strings::RemoveTrailing(
        std::wstring_view(maybeTargetDirectoryResolvedString.Value()), L'\\');

    if (false == targetDirectoryFullPath.contains(L'\\'))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Constraint violation: Target directory cannot be a filesystem root.",
          ruleName.c_str());
    if (true == targetDirectoryFullPath.empty())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Target directory: Failed to resolve full path: %s",
          ruleName.c_str(),
          Strings::SystemErrorCodeString(GetLastError()).AsCString());
    if (true == HasOriginDirectory(targetDirectoryFullPath))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Constraint violation: Target directory is already in use as an origin directory by another rule.",
          ruleName.c_str());
    if (true == HasTargetDirectory(targetDirectoryFullPath))
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Constraint violation: Target directory is already in use as a target directory by another rule.",
          ruleName.c_str());

    const std::wstring_view originDirectoryFullPathOwnedView =
        *originDirectories.emplace(originDirectoryFullPath).first;
    const std::wstring_view targetDirectoryFullPathOwnedView =
        *targetDirectories.emplace(targetDirectoryFullPath).first;

    const auto nameEmplaceResult = filesystemRuleNames.emplace(std::move(ruleName));
    DebugAssert(
        nameEmplaceResult.second,
        "FilesystemDirectorBuilder consistency check failed due to unsuccessful emplacement of a supposedly-unique filesystem rule name string.");

    // It does not matter whether the rule container already exists or not, as long as it can be
    // identified correctly. The new rule will be inserted into the container.
    const auto destinationRelatedRulesContainerNode =
        filesystemRulesByOriginDirectory.Emplace(originDirectoryFullPathOwnedView).first;
    DebugAssert(
        nullptr != destinationRelatedRulesContainerNode,
        "FilesystemDirectorBuilder failed to obtain a container for a rule that is being created.");

    const auto createResult = destinationRelatedRulesContainerNode->Data().EmplaceRule(
        *nameEmplaceResult.first,
        originDirectoryFullPathOwnedView,
        targetDirectoryFullPathOwnedView,
        std::move(filePatterns),
        redirectMode);
    if (nullptr == createResult.first)
    {
      DebugAssert(
          false == createResult.second,
          "FilesystemDirectorBuilder consistency check failed due to successful emplacement of a null filesystem rule.");
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Exceeds the limit of %u filesystem rules per origin directory.",
          nameEmplaceResult.first->c_str(),
          RelatedFilesystemRuleContainer::kMaximumFilesystemRuleCount);
    }
    else
    {
      DebugAssert(
          true == createResult.second,
          "FilesystemDirectorBuilder consistency check failed due to unsuccessful emplacement of a supposedly-unique filesystem rule keyed by name and file pattern count.");
    }

    const FilesystemRule* const newRule = createResult.first;
    filesystemRulesByName.emplace(newRule->GetName(), newRule);

    return newRule;
  }

  Infra::ValueOrError<const FilesystemRule*, Infra::TemporaryString>
      FilesystemDirectorBuilder::AddRuleFromConfigurationSection(
          std::wstring&& ruleName, Configuration::Section& configSection)
  {
    auto maybeOriginDirectory = configSection.ExtractFirstStringValue(
        Strings::kStrConfigurationSettingFilesystemRuleOriginDirectory);
    if (false == maybeOriginDirectory.has_value())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Missing origin directory.",
          ruleName.c_str());

    auto maybeTargetDirectory = configSection.ExtractFirstStringValue(
        Strings::kStrConfigurationSettingFilesystemRuleTargetDirectory);
    if (false == maybeTargetDirectory.has_value())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Missing target directory.",
          ruleName.c_str());

    auto maybeRedirectMode = RedirectModeFromConfigurationSection(configSection);
    if (false == maybeRedirectMode.has_value())
      return Strings::FormatString(
          L"Error while creating filesystem rule \"%s\": Invalid redirection mode.",
          ruleName.c_str());

    auto filePatterns =
        configSection
            .ExtractStringValues(Strings::kStrConfigurationSettingFilesystemRuleFilePattern)
            .value_or(std::vector<std::wstring>());

    return AddRule(
        std::move(ruleName),
        std::move(*maybeOriginDirectory),
        std::move(*maybeTargetDirectory),
        std::move(filePatterns),
        std::move(*maybeRedirectMode));
  }

  Infra::ValueOrError<FilesystemDirector, Infra::TemporaryString> FilesystemDirectorBuilder::Build(
      void)
  {
    using TFilesystemRulePrefixTreeByReference = PrefixTree<
        TFilesystemRulePrefixTree::TChar,
        const FilesystemRule*,
        TFilesystemRulePrefixTree::THash,
        TFilesystemRulePrefixTree::TEquals>;

    TFilesystemRulePrefixTreeByReference allDirectories;

    if (true == filesystemRulesByName.empty())
      return L"Error while building a filesystem director configuration: Internal error: Attempted to finalize an empty registry.";

    for (const auto& filesystemRuleRecord : filesystemRulesByName)
    {
      const FilesystemRule& filesystemRule = *filesystemRuleRecord.second;

      const bool originExists =
          FilesystemOperations::Exists(filesystemRule.GetOriginDirectoryFullPath().data());
      const bool originIsDirectory =
          FilesystemOperations::IsDirectory(filesystemRule.GetOriginDirectoryFullPath().data());
      if (false == (!originExists || originIsDirectory))
        return Strings::FormatString(
            L"Error while building a filesystem director configuration: Filesystem rule \"%.*s\": Constraint violation: Origin directory must either not exist at all or exist as a real directory.",
            static_cast<int>(filesystemRuleRecord.first.length()),
            filesystemRuleRecord.first.data());

      const Infra::TemporaryString originDirectoryParent =
          filesystemRule.GetOriginDirectoryParent();
      if ((false == FilesystemOperations::IsDirectory(originDirectoryParent.AsCString())) &&
          (false == HasOriginDirectory(originDirectoryParent)))
        return Strings::FormatString(
            L"Error while building a filesystem director configuration: Filesystem rule \"%.*s\": Constraint violation: Parent of origin directory must either exist as a real directory or be the origin directory of another filesystem rule.",
            static_cast<int>(filesystemRuleRecord.first.length()),
            filesystemRuleRecord.first.data());

      // Both origin and target directories are added to a prefix index containing all directories.
      // This is done to implement the check for constraint (3). Directory uniqueness should already
      // have been verified at filesystem rule creation time.
      allDirectories.Insert(filesystemRule.GetOriginDirectoryFullPath(), &filesystemRule);
      allDirectories.Insert(filesystemRule.GetTargetDirectoryFullPath(), &filesystemRule);
    }

    // This loop iterates over all rules one more time and checks all the target directories for
    // ancestors in the prefix index. Since the prefix index contains all origin and target
    // directories this check directly implements constraint (3). If an ancestor is located then
    // the configuration violates this constraint and should be rejected.
    for (const auto& filesystemRuleRecord : filesystemRulesByName)
    {
      auto targetDirectoryNode =
          allDirectories.Find(filesystemRuleRecord.second->GetTargetDirectoryFullPath());
      if (nullptr == targetDirectoryNode)
        return Strings::FormatString(
            L"Error while building a filesystem director configuration: Filesystem rule \"%.*s\": Internal error: Target directory is improperly properly indexed.",
            static_cast<int>(filesystemRuleRecord.first.length()),
            filesystemRuleRecord.first.data());

      auto ancestorNode = targetDirectoryNode->GetClosestAncestor();
      if (nullptr != ancestorNode)
      {
        std::wstring_view conflictingFilesystemRuleName = ancestorNode->GetData()->GetName();
        return Strings::FormatString(
            L"Error while building a filesystem director configuration: Filesystem rule \"%.*s\": Constraint violation: Target directory must not be a descendent of the origin or target directory of filesystem rule \"%.*s\".",
            static_cast<int>(filesystemRuleRecord.first.length()),
            filesystemRuleRecord.first.data(),
            static_cast<int>(conflictingFilesystemRuleName.length()),
            conflictingFilesystemRuleName.data());
      }
    }

    return FilesystemDirector(
        std::move(originDirectories),
        std::move(targetDirectories),
        std::move(filesystemRuleNames),
        std::move(filesystemRulesByOriginDirectory),
        std::move(filesystemRulesByName));
  }
} // namespace Pathwinder
