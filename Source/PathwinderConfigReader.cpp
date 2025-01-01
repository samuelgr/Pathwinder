/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file PathwinderConfigReader.cpp
 *   Implementation of Pathwinder-specific configuration reading functionality.
 **************************************************************************************************/

#include "PathwinderConfigReader.h"

#include <map>
#include <string_view>

#include <Infra/Core/Configuration.h>

#include "Strings.h"

namespace Pathwinder
{
  using namespace ::Infra::Configuration;

  /// Holds the names of Pathwinder configuration file sections that accept arbitrary name-value
  /// pairs. Key is the section name, value is the accepted type.
  static std::map<std::wstring_view, Infra::Configuration::EValueType, std::less<>>
      configurationFileDynamicSections = {
          {Strings::kStrConfigurationSectionDefinitions, Infra::Configuration::EValueType::String}};

  /// Holds the layout of the Pathwinder configuration file that is known statically.
  static Infra::Configuration::TConfigurationFileLayout configurationFileLayout = {
      ConfigurationFileLayoutSection(
          Infra::Configuration::kSectionNameGlobal,
          {
              ConfigurationFileLayoutNameAndValueType(
                  Strings::kStrConfigurationSettingLogLevel,
                  Infra::Configuration::EValueType::Integer),
          }),
  };

  // Holds the layout of Pathwinder configuration sections that define filesystem rules.
  static Infra::Configuration::TConfigurationFileSectionLayout filesystemRuleSectionLayout = {
      ConfigurationFileLayoutNameAndValueType(
          Strings::kStrConfigurationSettingFilesystemRuleOriginDirectory,
          Infra::Configuration::EValueType::String),
      ConfigurationFileLayoutNameAndValueType(
          Strings::kStrConfigurationSettingFilesystemRuleTargetDirectory,
          Infra::Configuration::EValueType::String),
      ConfigurationFileLayoutNameAndValueType(
          Strings::kStrConfigurationSettingFilesystemRuleRedirectMode,
          Infra::Configuration::EValueType::String),
      ConfigurationFileLayoutNameAndValueType(
          Strings::kStrConfigurationSettingFilesystemRuleFilePattern,
          Infra::Configuration::EValueType::StringMultiValue),
  };

  /// Checks if the specified section name could correspond with a section that defines a
  /// filesystem rule. This is simply based on whether or not the section name begins with the
  /// right prefix and contains some text afterwards.
  /// @param [in] section Section name to check.
  /// @return `true` if the section name begins with the filesystem rule section name prefix,
  /// `false` otherwise.
  static inline bool IsFilesystemRuleSectionName(std::wstring_view section)
  {
    return (
        (Strings::kStrConfigurationSectionFilesystemRulePrefix.length() < section.length()) &&
        (section.starts_with(Strings::kStrConfigurationSectionFilesystemRulePrefix)));
  }

  Action PathwinderConfigReader::ActionForSection(std::wstring_view section)
  {
    if ((true == configurationFileDynamicSections.contains(section)) ||
        (true == configurationFileLayout.contains(section)))
      return Action::Process();
    if (true == IsFilesystemRuleSectionName(section)) return Action::Process();
    return Action::Error();
  }

  Action PathwinderConfigReader::ActionForValue(
      std::wstring_view section, std::wstring_view name, TIntegerView value)
  {
    if (value >= 0) return Action::Process();
    return Action::Error();
  }

  Action PathwinderConfigReader::ActionForValue(
      std::wstring_view section, std::wstring_view name, TBooleanView value)
  {
    return Action::Process();
  }

  Action PathwinderConfigReader::ActionForValue(
      std::wstring_view section, std::wstring_view name, TStringView value)
  {
    return Action::Process();
  }

  EValueType PathwinderConfigReader::TypeForValue(std::wstring_view section, std::wstring_view name)
  {
    if (true == IsFilesystemRuleSectionName(section))
    {
      const auto settingInfo = filesystemRuleSectionLayout.find(name);
      if (filesystemRuleSectionLayout.cend() == settingInfo) return EValueType::Error;

      return settingInfo->second;
    }

    const auto dynamicSection = configurationFileDynamicSections.find(section);
    if (dynamicSection != configurationFileDynamicSections.cend()) return dynamicSection->second;

    const auto sectionLayout = configurationFileLayout.find(section);
    if (configurationFileLayout.cend() == sectionLayout) return EValueType::Error;

    const auto settingInfo = sectionLayout->second.find(name);
    if (sectionLayout->second.cend() == settingInfo) return EValueType::Error;

    return settingInfo->second;
  }
} // namespace Pathwinder
