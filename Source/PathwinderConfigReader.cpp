/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file PathwinderConfigReader.cpp
 *   Implementation of Pathwinder-specific configuration reading functionality.
 *****************************************************************************/

#include "Configuration.h"
#include "PathwinderConfigReader.h"
#include "Strings.h"

#include <map>
#include <string_view>


namespace Pathwinder
{
    using namespace ::Pathwinder::Configuration;


    // -------- INTERNAL VARIABLES ----------------------------------------- //

    /// Holds the names of Pathwinder configuration file sections that accept arbitrary name-value pairs.
    /// Key is the section name, value is the accepted type.
    static std::map<std::wstring_view, Configuration::EValueType, std::less<>> configurationFileDynamicSections = {
        {Strings::kStrConfigurationSectionDefinitions, Configuration::EValueType::String}
    };

    /// Holds the layout of the Pathwinder configuration file that is known statically.
    static Configuration::TConfigurationFileLayout configurationFileLayout = {
        ConfigurationFileLayoutSection(Configuration::kSectionNameGlobal, {
            ConfigurationFileLayoutNameAndValueType(Strings::kStrConfigurationSettingDryRun, Configuration::EValueType::Boolean),
            ConfigurationFileLayoutNameAndValueType(Strings::kStrConfigurationSettingLogLevel, Configuration::EValueType::Integer),
        }),
    };

    // Holds the layout of Pathwinder configuration sections that define filesystem rules.
    static Configuration::TConfigurationFileSectionLayout filesystemRuleSectionLayout = {
        ConfigurationFileLayoutNameAndValueType(Strings::kStrConfigurationSettingFilesystemRuleOriginDirectory, Configuration::EValueType::String),
        ConfigurationFileLayoutNameAndValueType(Strings::kStrConfigurationSettingFilesystemRuleTargetDirectory, Configuration::EValueType::String),
        ConfigurationFileLayoutNameAndValueType(Strings::kStrConfigurationSettingFilesystemRuleRedirectMode, Configuration::EValueType::String),
        ConfigurationFileLayoutNameAndValueType(Strings::kStrConfigurationSettingFilesystemRuleFilePattern, Configuration::EValueType::StringMultiValue),
    };


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Checks if the specified section name could correspond with a section that defines a filesystem rule.
    /// This is simply based on whether or not the section name begins with the right prefix and contains some text afterwards.
    /// @param [in] section Section name to check.
    /// @return `true` if the section name begins with the filesystem rule section name prefix, `false` otherwise.
    static inline bool IsFilesystemRuleSectionName(std::wstring_view section)
    {
        return ((Strings::kStrConfigurationSectionFilesystemRulePrefix.length() < section.length()) && (section.starts_with(Strings::kStrConfigurationSectionFilesystemRulePrefix)));
    }


    // -------- CONCRETE INSTANCE METHODS ---------------------------------- //
    // See "Configuration.h" for documentation.

    EAction PathwinderConfigReader::ActionForSection(std::wstring_view section)
    {
        if ((true == configurationFileDynamicSections.contains(section)) || (true == configurationFileLayout.contains(section)))
            return EAction::Process;

        if (true == IsFilesystemRuleSectionName(section))
            return EAction::Process;

        return EAction::Error;
    }

    // --------

    EAction PathwinderConfigReader::ActionForValue(std::wstring_view section, std::wstring_view name, TIntegerView value)
    {
        if (value >= 0)
            return EAction::Process;

        return EAction::Error;
    }

    // --------

    EAction PathwinderConfigReader::ActionForValue(std::wstring_view section, std::wstring_view name, TBooleanView value)
    {
        return EAction::Process;
    }

    // --------

    EAction PathwinderConfigReader::ActionForValue(std::wstring_view section, std::wstring_view name, TStringView value)
    {
        return EAction::Process;
    }

    // --------

    EValueType PathwinderConfigReader::TypeForValue(std::wstring_view section, std::wstring_view name)
    {
        if (true == IsFilesystemRuleSectionName(section))
        {
            const auto settingInfo = filesystemRuleSectionLayout.find(name);
            if (filesystemRuleSectionLayout.cend() == settingInfo)
                return EValueType::Error;

            return settingInfo->second;
        }

        const auto dynamicSection = configurationFileDynamicSections.find(section);
        if (dynamicSection != configurationFileDynamicSections.cend())
            return dynamicSection->second;

        const auto sectionLayout = configurationFileLayout.find(section);
        if (configurationFileLayout.cend() == sectionLayout)
            return EValueType::Error;

        const auto settingInfo = sectionLayout->second.find(name);
        if (sectionLayout->second.cend() == settingInfo)
            return EValueType::Error;

        return settingInfo->second;
    }
}
