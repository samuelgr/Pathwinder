/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file Globals.cpp
 *   Implementation of accessors and mutators for global data items.
 *   Intended for miscellaneous data elements with no other suitable place.
 **************************************************************************************************/

#include "Globals.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <Infra/Core/Message.h>
#include <Infra/Core/ProcessInfo.h>
#include <Infra/Core/TemporaryBuffer.h>

#include "Resolver.h"
#include "Strings.h"

#ifndef PATHWINDER_SKIP_CONFIG
#include <Infra/Core/Configuration.h>

#include "FilesystemDirector.h"
#include "FilesystemDirectorBuilder.h"
#include "Hooks.h"
#include "PathwinderConfigReader.h"
#endif

INFRA_DEFINE_PRODUCT_NAME_FROM_RESOURCE(
    Infra::ProcessInfo::GetThisModuleInstanceHandle(), IDS_PATHWINDER_PRODUCT_NAME);
INFRA_DEFINE_PRODUCT_VERSION_FROM_GIT_VERSION_INFO();

namespace Pathwinder
{
  namespace Globals
  {
#ifndef PATHWINDER_SKIP_CONFIG
    /// Reads all filesystem rules from a configuration file and attempts to create all the
    /// required filesystem rule objects and build them into a filesystem director object.
    /// Afterwards, on success, the singleton filesystem director object used for hook functions is
    /// initialized with the newly-built filesystem director object. This function uses move
    /// semantics, so all sections in the configuration data object that define filesystem rules are
    /// extracted out of it. This has the effect of using the filesystem rules defined in the
    /// configuration file to govern the behavior of file operations globally.
    /// @param [in] configData Read-only reference to a configuration data object.
    static void BuildFilesystemRules(Infra::Configuration::ConfigurationData& configData)
    {
      if (true == configData.HasReadErrors()) return;

      auto maybeFilesystemDirector =
          FilesystemDirectorBuilder::BuildFromConfigurationData(configData);

      if (true == maybeFilesystemDirector.has_value())
      {
        Hooks::SetFilesystemDirectorInstance(std::move(*maybeFilesystemDirector));
      }
      else
      {
        // Errors encountered when creating filesystem rules or building the final
        // filesystem director result in no filesystem director being created at all.
        // Therefore it is unnecessary to take any corrective action, such as automatically
        // enabling dry-run mode. Reporting the error is sufficient.

        if (true == Infra::Message::IsLogFileEnabled())
          Infra::Message::Output(
              Infra::Message::ESeverity::ForcedInteractiveWarning,
              L"Errors were encountered during filesystem rule creation. See log file for more information.");
        else
          Infra::Message::Output(
              Infra::Message::ESeverity::ForcedInteractiveWarning,
              L"Errors were encountered during filesystem rule creation. Enable logging and see log file for more information.");
      }
    }

    /// Enables the log if it is not already enabled.
    /// Regardless, the minimum severity for output is set based on the parameter.
    /// @param [in] logLevel Logging level to configure as the minimum severity for output.
    static void EnableLog(Infra::Message::ESeverity logLevel)
    {
      static std::once_flag enableLogFlag;
      std::call_once(
          enableLogFlag,
          [logLevel]() -> void
          {
            Infra::Message::CreateAndEnableLogFile(Strings::GetLogFilename());
          });

      Infra::Message::SetMinimumSeverityForOutput(logLevel);
    }

    /// Enables the log, if it is configured in the specified configuration data object.
    /// @param [in] configData Read-only reference to a configuration data object.
    static void EnableLogIfConfigured(const Infra::Configuration::ConfigurationData& configData)
    {
      const int64_t logLevel = configData
                                   .GetFirstIntegerValue(
                                       Infra::Configuration::kSectionNameGlobal,
                                       Strings::kStrConfigurationSettingLogLevel)
                                   .value_or(0);

      if (logLevel > 0)
      {
        // Offset the requested severity so that 0 = disabled, 1 = error, 2 = warning, etc.
        const Infra::Message::ESeverity configuredSeverity = (Infra::Message::ESeverity)(
            logLevel +
            static_cast<int64_t>(Infra::Message::ESeverity::LowerBoundConfigurableValue));
        EnableLog(configuredSeverity);
      }
    }

    /// Reads configuration data from the configuration file and returns the resulting
    /// configuration data object. Enables logging and outputs read errors if any are
    /// encountered.
    /// @return Filled configuration data object.
    static Infra::Configuration::ConfigurationData ReadConfigurationFile(void)
    {
      Infra::TemporaryString configFileName;
      configFileName << Infra::ProcessInfo::GetThisModuleDirectoryName() << L"\\"
                     << Strings::GetConfigurationFilename();

      PathwinderConfigReader configReader;
      Infra::Configuration::ConfigurationData configData =
          configReader.ReadConfigurationFile(configFileName);

      if ((false == configData.HasReadErrors()) &&
          (true ==
           configData
               .GetFirstBooleanValue(
                   Infra::Configuration::kSectionNameGlobal,
                   Strings::kStrConfigurationSettingRedirectConfigToExecutableDirectory)
               .value_or(false)))
      {
        configFileName.Clear();
        configFileName << Infra::ProcessInfo::GetExecutableDirectoryName() << L"\\"
                       << Strings::GetConfigurationFilename();
        configData = configReader.ReadConfigurationFile(configFileName);
      }

      if (true == configData.HasReadErrors())
      {
        EnableLog(Infra::Message::ESeverity::Error);

        Infra::Message::Output(
            Infra::Message::ESeverity::Error,
            L"Errors were encountered during configuration file reading.");
        for (const auto& readErrorMessage : configData.GetReadErrorMessages())
          Infra::Message::OutputFormatted(
              Infra::Message::ESeverity::Error, L"    %s", readErrorMessage.c_str());

        configData.ClearReadErrorMessages();

        Infra::Message::Output(
            Infra::Message::ESeverity::ForcedInteractiveWarning,
            L"Errors were encountered during configuration file reading. See log file on the Desktop for more information.");
      }

      return configData;
    }

    /// Reads configured definitions from the configuration file, if specified, and submits them
    /// to the reference resolution subsystem.
    /// @param [in] configData Read-only reference to a configuration data object.
    static void SetResolverConfiguredDefinitions(
        Infra::Configuration::ConfigurationData& configData)
    {
      auto configuredDefinitionsSectionIter =
          configData.Sections().find(Strings::kStrConfigurationSectionDefinitions);
      if (configData.Sections().end() != configuredDefinitionsSectionIter)
        Resolver::SetConfiguredDefinitionsFromSection(
            configData.ExtractSection(configuredDefinitionsSectionIter).second);
    }
#endif

    void Initialize(void)
    {
#ifndef PATHWINDER_SKIP_CONFIG
      Infra::Configuration::ConfigurationData configData = ReadConfigurationFile();

      EnableLogIfConfigured(configData);
      SetResolverConfiguredDefinitions(configData);
      BuildFilesystemRules(configData);
#endif
    }
  } // namespace Globals
} // namespace Pathwinder
