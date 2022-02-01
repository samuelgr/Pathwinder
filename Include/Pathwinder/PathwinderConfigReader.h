/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file PathwinderConfigReader.h
 *   Declaration of Pathwinder-specific configuration reading functionality.
 *****************************************************************************/

#pragma once

#include "Configuration.h"

#include <string_view>


namespace Pathwinder
{
    using namespace ::Pathwinder::Configuration;


    class PathwinderConfigReader : public ConfigurationFileReader
    {
    protected:
        // -------- CONCRETE INSTANCE METHODS ------------------------------ //
        // See "Configuration.h" for documentation.

        EAction ActionForSection(std::wstring_view section) override;
        EAction ActionForValue(std::wstring_view section, std::wstring_view name, TIntegerView value) override;
        EAction ActionForValue(std::wstring_view section, std::wstring_view name, TBooleanView value) override;
        EAction ActionForValue(std::wstring_view section, std::wstring_view name, TStringView value) override;
        EValueType TypeForValue(std::wstring_view section, std::wstring_view name) override;
    };
}
