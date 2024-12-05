/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file PathwinderConfigReader.h
 *   Declaration of Pathwinder-specific configuration reading functionality.
 **************************************************************************************************/

#pragma once

#include <string_view>

#include <Infra/Configuration.h>

namespace Pathwinder
{
  using namespace ::Infra::Configuration;

  class PathwinderConfigReader : public ConfigurationFileReader
  {
  protected:

    // ConfigurationFileReader
    EAction ActionForSection(std::wstring_view section) override;
    EAction ActionForValue(
        std::wstring_view section, std::wstring_view name, TIntegerView value) override;
    EAction ActionForValue(
        std::wstring_view section, std::wstring_view name, TBooleanView value) override;
    EAction ActionForValue(
        std::wstring_view section, std::wstring_view name, TStringView value) override;
    EValueType TypeForValue(std::wstring_view section, std::wstring_view name) override;
  };
} // namespace Pathwinder
