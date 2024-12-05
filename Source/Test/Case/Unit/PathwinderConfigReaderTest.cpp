/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file PathwinderConfigReaderTest.cpp
 *   Unit tests for configuration file reading and parsing functionality.
 **************************************************************************************************/

#include "TestCase.h"

#include "PathwinderConfigReader.h"

#include <string_view>

#include <Infra/Configuration.h>
#include <Infra/TemporaryBuffer.h>

namespace PathwinderTest
{
  using namespace ::Pathwinder;
  using ::Infra::Configuration::ConfigurationData;

  /// Converts the specified configuration data object into a configuration file and then passes
  /// it through a reader object for parsing. Upon completion, verifies that the contents and
  /// error state of the resulting configuration data matches the input object.
  static void TestConfigurationFileRead(const ConfigurationData& expectedConfigurationData)
  {
    Infra::TemporaryString configurationFile =
        expectedConfigurationData.ToConfigurationFileString();
    ConfigurationData actualConfigurationData =
        PathwinderConfigReader().ReadInMemoryConfigurationFile(configurationFile);
    TEST_ASSERT(actualConfigurationData == expectedConfigurationData);
  }

  // Verifies that global section values can be successfully parsed.
  TEST_CASE(PathwinderConfigReader_GlobalSection)
  {
    const ConfigurationData configurationData(
        {{L"",
          {
              {L"LogLevel", 4},
          }}});

    TestConfigurationFileRead(configurationData);
  }

  // Verifies that variable definitions can be successfully parsed. The definition section accepts
  // arbitrary variable names and string values.
  TEST_CASE(PathwinderConfigReader_VariableDefinitions)
  {
    const ConfigurationData configurationData(
        {{L"Definitions",
          {{L"MyUserName", L"%USERNAME%"},
           {L"MyUserProfileDirectory", L"%HOMEDRIVE%%HOMEPATH%"},
           {L"ArbitraryDirectory", L"C:\\SomePath\\ToADirectory\\UsefulAsAVariable"},
           {L"__Another.Variable-value", L"Val?+ue(1[23]4).*"}}}});

    TestConfigurationFileRead(configurationData);
  }

  // Verifies that multiple filesystem rules can be successfully parsed.
  TEST_CASE(PathwinderConfigReader_FilesystemRules)
  {
    ConfigurationData configurationData(
        {{L"FilesystemRule:NoFilePatterns",
          {{L"OriginDirectory", L"C:\\OriginDirectory1"},
           {L"TargetDirectory", L"C:\\TargetDirectory1"}}},
         {L"FilesystemRule:OneFilePattern",
          {{L"OriginDirectory", L"C:\\OriginDirectory2"},
           {L"TargetDirectory", L"C:\\TargetDirectory2"},
           {L"FilePattern", L"*.txt"}}},
         {L"FilesystemRule:MultipleFilePatterns",
          {{L"OriginDirectory", L"C:\\OriginDirectory3"},
           {L"TargetDirectory", L"C:\\TargetDirectory3"},
           {L"FilePattern", {L"*.txt", L"*.bin", L"*.log", L"savedata???.sav"}}}}});

    TestConfigurationFileRead(configurationData);
  }
} // namespace PathwinderTest
