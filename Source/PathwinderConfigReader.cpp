/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file PathwinderConfigReader.cpp
 *   Implementation of Pathwinder-specific configuration reading functionality.
 *****************************************************************************/

#include "Configuration.h"
#include "PathwinderConfigReader.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <windows.h>


namespace Pathwinder
{
    using namespace ::Pathwinder::Configuration;


    // -------- INTERNAL VARIABLES ----------------------------------------- //

    /// Holds the layout of the Pathwinder configuration file that is known statically.
    static Configuration::TConfigurationFileLayout configurationFileLayout = {
        ConfigurationFileLayoutSection(Configuration::kSectionNameGlobal, {
            ConfigurationFileLayoutNameAndValueType(Strings::kStrConfigurationSettingNameLogLevel, Configuration::EValueType::Integer),
        }),
    };


    // -------- CONCRETE INSTANCE METHODS ---------------------------------- //
    // See "Configuration.h" for documentation.

    EAction PathwinderConfigReader::ActionForSection(std::wstring_view section)
    {
        if (false == configurationFileLayout.contains(section))
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
        auto sectionLayout = configurationFileLayout.find(section);
        if (configurationFileLayout.end() == sectionLayout)
            return EValueType::Error;

        auto settingInfo = sectionLayout->second.find(name);
        if (sectionLayout->second.end() == settingInfo)
            return EValueType::Error;

        return settingInfo->second;
    }
}
