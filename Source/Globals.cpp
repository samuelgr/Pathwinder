/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
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
#include <Infra/Core/Resolver.h>
#include <Infra/Core/TemporaryBuffer.h>

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
    /// @param [in] configData Reference to a configuration data object from which to obtain
    /// filesystem rules. On return, the sections containing filesystem rules will have been
    /// extracted.
    static void BuildFilesystemRules(Infra::Configuration::ConfigurationData& configData)
    {
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
        // Therefore it is unnecessary to take any corrective action.

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

    /// Extracts the variable definitions from the specified configuration data object and fills
    /// them into the specified resolver object.
    /// @param [in, out] resolver Resolver object to be filled with the configured definitions.
    /// @param [in, out] configData Reference to a configuration data object from which to obtain
    /// definitions. On return, the associated section will have been extracted.
    /// @return Pre-filled resolver object.
    static void AddConfiguredDefinitionsToResolver(
        Infra::Resolver& resolver, Infra::Configuration::ConfigurationData& configData)
    {
      auto maybeConfiguredDefinitionsSection =
          configData.ExtractSection(Strings::kStrConfigurationSectionDefinitions);
      if (false == maybeConfiguredDefinitionsSection.has_value()) return;
      auto& configuredDefinitionsSection = maybeConfiguredDefinitionsSection->second;

      static Infra::Resolver::TDefinitions configuredDefinitions;
      configuredDefinitions.clear();

      for (auto definitionRecord = configuredDefinitionsSection.ExtractFirst();
           definitionRecord.has_value();
           definitionRecord = configuredDefinitionsSection.ExtractFirst())
      {
        DebugAssert(
            Infra::Configuration::EValueType::String == definitionRecord->second.GetType(),
            "Configured definitions section contains a non-string value.");

        std::wstring definitionName = std::move(definitionRecord->first);
        std::wstring definitionValue = *definitionRecord->second.ExtractFirstString();

        configuredDefinitions.emplace(std::move(definitionName), std::move(definitionValue));
      }

      resolver.RegisterCustomDomain(
          Strings::kStrReferenceDomainConfigDefinition, configuredDefinitions);
      resolver.RegisterCustomDomain(
          Strings::kStrReferenceDomainConfigOptionalDefinition, configuredDefinitions, L"");
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
            Infra::Message::CreateAndEnableLogFile();
          });

      Infra::Message::SetMinimumSeverityForOutput(logLevel);
    }

    /// Enables the log, if it is configured in the specified configuration data object.
    /// @param [in] configData Read-only reference to a configuration data object.
    static void EnableLogIfConfigured(const Infra::Configuration::ConfigurationData& configData)
    {
      const int64_t logLevel = configData[Infra::Configuration::kSectionNameGlobal]
                                         [Strings::kStrConfigurationSettingLogLevel]
                                             .ValueOr(0);

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
    /// @param [in,out] configReader Configuration reader object to control the read operation and
    /// to fill with any error messages that arise.
    /// @return Filled configuration data object, assuming no errors were encountered.
    static Infra::Configuration::ConfigurationData ReadConfigurationFile(
        PathwinderConfigReader& configReader)
    {
      Infra::Configuration::ConfigurationData configData = configReader.ReadConfigurationFile();
      if (true == configReader.HasErrorMessages())
      {
        EnableLog(Infra::Message::ESeverity::Error);

        Infra::Message::Output(
            Infra::Message::ESeverity::Error,
            L"Errors were encountered during configuration file reading.");
        configReader.LogAllErrorMessages();
        Infra::Message::Output(
            Infra::Message::ESeverity::Error,
            L"None of the settings in the configuration file were applied. Fix the errors and restart the application.");

        Infra::Message::Output(
            Infra::Message::ESeverity::ForcedInteractiveWarning,
            L"Errors were encountered during configuration file reading. See log file on the Desktop for more information.");

        configData.Clear();
      }

      return configData;
    }
#endif

    Infra::Resolver& ResolverWithConfiguredDefinitions(void)
    {
      static Infra::Resolver resolver;
      return resolver;
    }

    void Initialize(void)
    {
#ifndef PATHWINDER_SKIP_CONFIG
      PathwinderConfigReader configReader;
      Infra::Configuration::ConfigurationData configData = ReadConfigurationFile(configReader);
      EnableLogIfConfigured(configData);

      if (false == configReader.HasErrorMessages())
      {
        AddConfiguredDefinitionsToResolver(ResolverWithConfiguredDefinitions(), configData);
        BuildFilesystemRules(configData);
      }
#endif
    }

    std::unordered_set<std::wstring>& TemporaryPathsToClean(void)
    {
      static std::unordered_set<std::wstring> temporaryPathsToClean;
      return temporaryPathsToClean;
    }
  } // namespace Globals
} // namespace Pathwinder
