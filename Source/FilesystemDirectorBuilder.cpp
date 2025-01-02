/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
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
#include <variant>

#include <Infra/Core/Configuration.h>
#include <Infra/Core/DebugAssert.h>
#include <Infra/Core/Message.h>
#include <Infra/Core/Strings.h>
#include <Infra/Core/TemporaryBuffer.h>
#include <Infra/Core/ValueOrError.h>

#include "ApiWindows.h"
#include "FilesystemOperations.h"
#include "FilesystemRule.h"
#include "Globals.h"
#include "Resolver.h"
#include "Strings.h"

namespace Pathwinder
{
  /// Resolves a filesystem path that potentially has relative path components ('.' and '..') by
  /// turning it into an absolute path.
  /// @param [in] potentiallyRelativePath Path to be resolved from potentially relative to
  /// absolute.
  /// @param [in] pathDelimiter Delimiter to use when separating components of the path. Defaults
  /// to the Windows standard delimiter of a single backslash.
  /// @return Input path turned into an absolute path or an error message if the process failed.
  static ResolvedStringOrError PathResolveRelativeToAbsolute(
      std::wstring_view potentiallyRelativePath, std::wstring_view pathDelimiter = L"\\")
  {
    const bool hasTrailingPathDelimiter = potentiallyRelativePath.ends_with(pathDelimiter);

    Infra::TemporaryVector<std::wstring_view> resolvedPathComponents;
    size_t resolvedPathLength = 0;

    for (std::wstring_view pathComponent :
         Infra::Strings::Tokenizer(potentiallyRelativePath, pathDelimiter))
    {
      if ((pathComponent.empty()) || (pathComponent == L"."))
      {
        // Current-directory references and empty path components can be skipped. An empty path
        // component indicates either there is a trailing path delimiter or there are multiple
        // consecutive path delimiters somewhere in the middle of the path.

        continue;
      }
      else if (pathComponent == L"..")
      {
        // Parent-directory references need one path component to be popped.

        if (resolvedPathComponents.Size() < 2)
          return ResolvedStringOrError::MakeError(Infra::Strings::Format(
              L"%.*s: Invalid path: Too many \"..\" parent directory references",
              static_cast<int>(potentiallyRelativePath.length()),
              potentiallyRelativePath.data()));

        const size_t resolvedPathLengthToRemove =
            resolvedPathComponents.Back().length() + pathDelimiter.length();
        if (resolvedPathLengthToRemove > resolvedPathLength)
          return ResolvedStringOrError::MakeError(Infra::Strings::Format(
              L"%.*s: Internal error: Removing too many characters while resolving a single \"..\" parent directory reference",
              static_cast<int>(potentiallyRelativePath.length()),
              potentiallyRelativePath.data()));

        resolvedPathComponents.PopBack();
        resolvedPathLength -= resolvedPathLengthToRemove;
      }
      else
      {
        // Any other path components need to be pushed without modification.

        if (resolvedPathComponents.Size() == resolvedPathComponents.Capacity())
          return ResolvedStringOrError::MakeError(Infra::Strings::Format(
              L"%.*s: Invalid path: Hierarchy is too deep, exceeds the limit of %u path components",
              static_cast<int>(potentiallyRelativePath.length()),
              potentiallyRelativePath.data(),
              resolvedPathComponents.Capacity()));
        resolvedPathComponents.PushBack(pathComponent);
        resolvedPathLength += pathComponent.length() + pathDelimiter.length();
      }
    }

    std::wstring resolvedPath;
    if (false == resolvedPathComponents.Empty())
    {
      resolvedPath.reserve(resolvedPathLength);
      for (std::wstring_view resolvedPathComponent : resolvedPathComponents)
      {
        resolvedPath.append(resolvedPathComponent);
        resolvedPath.append(pathDelimiter);
      }

      if (false == hasTrailingPathDelimiter)
        resolvedPath.resize(resolvedPath.size() - pathDelimiter.length());
    }

    return std::move(resolvedPath);
  }

  /// Reads the configured redirection mode from a filesystem rule configuration setting or, if
  /// the redirection mode is not present, returns a default value.
  /// @param [in] configSection Configuration section object containing the filesystem rule's
  /// configuration information.
  /// @return Redirection mode enumerator, if it is either absent from the configuration section
  /// data object or present and maps to a valid enumerator.
  static std::optional<ERedirectMode> RedirectModeFromConfigurationSetting(
      const std::optional<Infra::Configuration::Name>& configSetting)
  {
    constexpr ERedirectMode kDefaultRedirectMode = ERedirectMode::Simple;
    if (false == configSetting.has_value()) return kDefaultRedirectMode;

    static const std::unordered_map<
        std::wstring_view,
        ERedirectMode,
        Infra::Strings::CaseInsensitiveHasher<wchar_t>,
        Infra::Strings::CaseInsensitiveEqualityComparator<wchar_t>>
        kRedirectModeStrings = {
            {L"Simple", ERedirectMode::Simple}, {L"Overlay", ERedirectMode::Overlay}};

    const auto redirectModeIter = kRedirectModeStrings.find((*configSetting)->GetString());
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

  /// Writes the details of a new filesystem rule to the log. Does not print the name, just the
  /// details like origin and target directories, redirect mode, and file patterns.
  /// @param [in] ruleToLog Filesystem rule whose details are to be logged.
  /// @param [in] indentNumSpaces Number of spaces to indent each line of the details.
  static void WriteFilesystemRuleDetailsToLog(
      const FilesystemRule& ruleToLog, unsigned int indentNumSpaces)
  {
    const std::wstring_view redirectModeString = RedirectModeToString(ruleToLog.GetRedirectMode());

    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Info,
        L"%*sRedirection mode = %.*s",
        static_cast<int>(indentNumSpaces),
        L"",
        static_cast<int>(redirectModeString.length()),
        redirectModeString.data());
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Info,
        L"%*sOrigin directory = \"%.*s\"",
        static_cast<int>(indentNumSpaces),
        L"",
        static_cast<int>(ruleToLog.GetOriginDirectoryFullPath().length()),
        ruleToLog.GetOriginDirectoryFullPath().data());
    Infra::Message::OutputFormatted(
        Infra::Message::ESeverity::Info,
        L"%*sTarget directory = \"%.*s\"",
        static_cast<int>(indentNumSpaces),
        L"",
        static_cast<int>(ruleToLog.GetTargetDirectoryFullPath().length()),
        ruleToLog.GetTargetDirectoryFullPath().data());

    if (true == ruleToLog.HasFilePatterns())
    {
      for (const auto& filePattern : ruleToLog.GetFilePatterns())
        Infra::Message::OutputFormatted(
            Infra::Message::ESeverity::Info,
            L"%*sFile pattern = \"%s\"",
            static_cast<int>(indentNumSpaces),
            L"",
            filePattern.c_str());
    }
    else
    {
      Infra::Message::OutputFormatted(
          Infra::Message::ESeverity::Info,
          L"%*sFile pattern = \"*\"",
          static_cast<int>(indentNumSpaces),
          L"");
    }
  }

  std::optional<FilesystemDirector> FilesystemDirectorBuilder::BuildFromConfigurationData(
      Infra::Configuration::ConfigurationData& configData)
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

      Infra::Configuration::Section& filesystemRuleContents =
          filesystemRuleSectionNameAndContents.second;

      auto addFilesystemRuleResult = builder.AddRuleFromConfigurationSection(
          std::wstring(filesystemRuleName), filesystemRuleContents);
      if (addFilesystemRuleResult.HasValue())
      {
        const FilesystemRule& newFilesystemRule = *(addFilesystemRuleResult.Value());
        Infra::Message::OutputFormatted(
            Infra::Message::ESeverity::Info,
            L"Successfully created filesystem rule \"%.*s\".",
            static_cast<int>(newFilesystemRule.GetName().length()),
            newFilesystemRule.GetName().data());
        WriteFilesystemRuleDetailsToLog(newFilesystemRule, 2);
      }
      else
      {
        Infra::Message::Output(
            Infra::Message::ESeverity::Error, addFilesystemRuleResult.Error().AsCString());
        builderHasRuleErrors = true;
      }
    }

    if (true == builderHasRuleErrors) return std::nullopt;

    if (0 == builder.CountOfRules())
    {
      // It is not an error for a configuration data object to contain no filesystem rules.
      // The resulting filesystem director object will simply do nothing.

      Infra::Message::Output(
          Infra::Message::ESeverity::Warning,
          L"Successfully built a filesystem director configuration, but it contains no filesystem rules.");
      return FilesystemDirector();
    }

    auto buildResult = builder.Build();
    if (buildResult.HasValue())
    {
      Infra::Message::OutputFormatted(
          Infra::Message::ESeverity::Info,
          L"Successfully built a filesystem director configuration with %u rules(s).",
          buildResult.Value().CountOfRules());
      return std::move(buildResult.Value());
    }
    else
    {
      Infra::Message::Output(Infra::Message::ESeverity::Error, buildResult.Error().AsCString());
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
    for (std::wstring_view pathComponent : Infra::Strings::Tokenizer(candidateDirectory, L"\\"))
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
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Constraint violation: Rule with the same name already exists.",
          ruleName.c_str());

    for (std::wstring_view filePattern : filePatterns)
    {
      if (false == IsValidFilePatternString(filePattern))
        return Infra::Strings::Format(
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

    ResolvedStringOrError maybeOriginDirectoryResolvedString =
        Globals::ResolverWithConfiguredDefinitions().ResolveAllReferences(originDirectory);
    if (true == maybeOriginDirectoryResolvedString.HasError())
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Origin directory: %s.",
          ruleName.c_str(),
          maybeOriginDirectoryResolvedString.Error().AsCString());
    maybeOriginDirectoryResolvedString =
        PathResolveRelativeToAbsolute(maybeOriginDirectoryResolvedString.Value());
    if (true == maybeOriginDirectoryResolvedString.HasError())
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Origin directory: %s.",
          ruleName.c_str(),
          maybeOriginDirectoryResolvedString.Error().AsCString());
    if (false == IsValidDirectoryString(maybeOriginDirectoryResolvedString.Value()))
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Origin directory: Either empty, relative, or contains disallowed characters.",
          ruleName.c_str());

    std::wstring_view originDirectoryFullPath = Infra::Strings::RemoveTrailing(
        std::wstring_view(maybeOriginDirectoryResolvedString.Value()), L'\\');

    if (false == originDirectoryFullPath.contains(L'\\'))
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Constraint violation: Origin directory cannot be a filesystem root.",
          ruleName.c_str());
    if (true == originDirectoryFullPath.empty())
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Origin directory: Failed to resolve full path: %s",
          ruleName.c_str(),
          Infra::Strings::FromSystemErrorCode(GetLastError()).AsCString());
    if (true == HasTargetDirectory(originDirectoryFullPath))
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Constraint violation: Origin directory is already in use as a target directory by another rule.",
          ruleName.c_str());

    ResolvedStringOrError maybeTargetDirectoryResolvedString =
        Globals::ResolverWithConfiguredDefinitions().ResolveAllReferences(targetDirectory);
    if (true == maybeTargetDirectoryResolvedString.HasError())
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Target directory: %s.",
          ruleName.c_str(),
          maybeTargetDirectoryResolvedString.Error().AsCString());
    maybeTargetDirectoryResolvedString =
        PathResolveRelativeToAbsolute(maybeTargetDirectoryResolvedString.Value());
    if (true == maybeTargetDirectoryResolvedString.HasError())
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Target directory: %s.",
          ruleName.c_str(),
          maybeTargetDirectoryResolvedString.Error().AsCString());

    if (false == IsValidDirectoryString(maybeTargetDirectoryResolvedString.Value()))
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Target directory: Either empty, relative, or contains disallowed characters.",
          ruleName.c_str());

    std::wstring_view targetDirectoryFullPath = Infra::Strings::RemoveTrailing(
        std::wstring_view(maybeTargetDirectoryResolvedString.Value()), L'\\');

    if (false == targetDirectoryFullPath.contains(L'\\'))
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Constraint violation: Target directory cannot be a filesystem root.",
          ruleName.c_str());
    if (true == targetDirectoryFullPath.empty())
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Target directory: Failed to resolve full path: %s",
          ruleName.c_str(),
          Infra::Strings::FromSystemErrorCode(GetLastError()).AsCString());
    if (true == HasOriginDirectory(targetDirectoryFullPath))
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Constraint violation: Target directory is already in use as an origin directory by another rule.",
          ruleName.c_str());
    if (true == HasTargetDirectory(targetDirectoryFullPath))
      return Infra::Strings::Format(
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
      return Infra::Strings::Format(
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
          std::wstring&& ruleName, Infra::Configuration::Section& configSection)
  {
    auto maybeOriginDirectory =
        configSection.Extract(Strings::kStrConfigurationSettingFilesystemRuleOriginDirectory);
    if (false == maybeOriginDirectory.has_value())
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Missing origin directory.",
          ruleName.c_str());

    auto maybeTargetDirectory =
        configSection.Extract(Strings::kStrConfigurationSettingFilesystemRuleTargetDirectory);
    if (false == maybeTargetDirectory.has_value())
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Missing target directory.",
          ruleName.c_str());

    auto maybeRedirectMode = RedirectModeFromConfigurationSetting(
        configSection.Extract(Strings::kStrConfigurationSettingFilesystemRuleRedirectMode));
    if (false == maybeRedirectMode.has_value())
      return Infra::Strings::Format(
          L"Error while creating filesystem rule \"%s\": Invalid redirection mode.",
          ruleName.c_str());

    auto filePatterns =
        configSection.Extract(Strings::kStrConfigurationSettingFilesystemRuleFilePattern)
            .value_or(Infra::Configuration::Name())
            .ExtractAllStrings()
            .value_or(std::vector<std::wstring>());

    return AddRule(
        std::move(ruleName),
        (*maybeOriginDirectory)->GetString(),
        (*maybeTargetDirectory)->GetString(),
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

    auto maybeConstraintViolation = VerifyPreBuildConstraints();
    while (true == maybeConstraintViolation.has_value())
    {
      auto& constraintViolation = *maybeConstraintViolation;
      if (true == std::holds_alternative<SAutoGeneratedRuleSpec>(constraintViolation))
      {
        SAutoGeneratedRuleSpec& ruleToAutoGenerate =
            std::get<SAutoGeneratedRuleSpec>(constraintViolation);
        auto autoGenerateRuleResult = AddRule(
            std::move(ruleToAutoGenerate.ruleName),
            ruleToAutoGenerate.originDirectory,
            ruleToAutoGenerate.targetDirectory);
        if (true == autoGenerateRuleResult.HasError())
          return Infra::Strings::Format(
              L"Error while building a filesystem director configuration: %s",
              autoGenerateRuleResult.Error().AsCString());
        Globals::TemporaryPathsToClean().emplace(
            autoGenerateRuleResult.Value()->GetTargetDirectoryFullPath());
        Infra::Message::OutputFormatted(
            Infra::Message::ESeverity::Info,
            L"Auto-generated filesystem rule \"%.*s\".",
            static_cast<int>(autoGenerateRuleResult.Value()->GetName().length()),
            autoGenerateRuleResult.Value()->GetName().data());
        WriteFilesystemRuleDetailsToLog(*autoGenerateRuleResult.Value(), 2);
      }
      else if (true == std::holds_alternative<Infra::TemporaryString>(constraintViolation))
      {
        return Infra::Strings::Format(
            L"Error while building a filesystem director configuration: %s",
            std::get<Infra::TemporaryString>(constraintViolation).AsCString());
      }
      else
      {
        return L"Error while building a filesystem director configuration: Internal error: Unrecognized constraint validation result.";
      }

      maybeConstraintViolation = VerifyPreBuildConstraints();
    }

    return FilesystemDirector(
        std::move(originDirectories),
        std::move(targetDirectories),
        std::move(filesystemRuleNames),
        std::move(filesystemRulesByOriginDirectory),
        std::move(filesystemRulesByName));
  }

  std::optional<FilesystemDirectorBuilder::TConstraintViolation>
      FilesystemDirectorBuilder::VerifyPreBuildConstraints(void) const
  {
    using TFilesystemRulePrefixTreeByReference = PrefixTree<
        TFilesystemRulePrefixTree::TChar,
        const FilesystemRule*,
        TFilesystemRulePrefixTree::THash,
        TFilesystemRulePrefixTree::TEquals>;

    TFilesystemRulePrefixTreeByReference allDirectories;

    for (const auto& filesystemRuleRecord : filesystemRulesByName)
    {
      const FilesystemRule& filesystemRule = *filesystemRuleRecord.second;

      const bool originExists =
          FilesystemOperations::Exists(filesystemRule.GetOriginDirectoryFullPath().data());
      const bool originIsDirectory =
          FilesystemOperations::IsDirectory(filesystemRule.GetOriginDirectoryFullPath().data());
      if (false == (!originExists || originIsDirectory))
        return Infra::Strings::Format(
            L"Filesystem rule \"%.*s\": Constraint violation: Origin directory must either not exist at all or exist as a real directory.",
            static_cast<int>(filesystemRuleRecord.first.length()),
            filesystemRuleRecord.first.data());

      const Infra::TemporaryString originDirectoryParent =
          filesystemRule.GetOriginDirectoryParent();
      if ((false == FilesystemOperations::IsDirectory(originDirectoryParent.AsCString())) &&
          (false == HasOriginDirectory(originDirectoryParent)))
      {
        // If a filesystem rule's origin directory is neither a real filesystem directory nor the
        // origin directory of another filesystem rule, Pathwinder will need to present it to the
        // application as an illusionary directory. To do that in a consistent way, Pathwinder needs
        // to create a rule that redirects all accesses to a temporary location. This will ensure
        // that requests to open files in the fake parent directory, or to enumerate the contents of
        // the fake parent directory, are handled correctly.

        return SAutoGeneratedRuleSpec{
            .ruleName = std::wstring(Infra::Strings::Format(
                L"%.*s:AddParentOfOriginDirectory:%.*s",
                static_cast<int>(kAutoGeneratedRuleNamePrefix.length()),
                kAutoGeneratedRuleNamePrefix.data(),
                static_cast<int>(
                    filesystemRuleRecord.second->GetOriginDirectoryFullPath().length()),
                filesystemRuleRecord.second->GetOriginDirectoryFullPath().data())),
            .originDirectory = std::wstring(originDirectoryParent),
            .targetDirectory = std::wstring(Strings::UniqueTemporaryDirectory())};
      }

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
        return Infra::Strings::Format(
            L"Filesystem rule \"%.*s\": Internal error: Target directory is improperly properly indexed.",
            static_cast<int>(filesystemRuleRecord.first.length()),
            filesystemRuleRecord.first.data());

      auto ancestorNode = targetDirectoryNode->GetClosestAncestor();
      if (nullptr != ancestorNode)
      {
        std::wstring_view conflictingFilesystemRuleName = ancestorNode->GetData()->GetName();
        return Infra::Strings::Format(
            L"Filesystem rule \"%.*s\": Constraint violation: Target directory must not be a descendent of the origin or target directory of filesystem rule \"%.*s\".",
            static_cast<int>(filesystemRuleRecord.first.length()),
            filesystemRuleRecord.first.data(),
            static_cast<int>(conflictingFilesystemRuleName.length()),
            conflictingFilesystemRuleName.data());
      }
    }

    return std::nullopt;
  }
} // namespace Pathwinder
