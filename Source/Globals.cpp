/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file Globals.cpp
 *   Implementation of accessors and mutators for global data items.
 *   Intended for miscellaneous data elements with no other suitable place.
 **************************************************************************************************/

#include "Globals.h"

#include "FilesystemDirector.h"
#include "FilesystemDirectorBuilder.h"
#include "GitVersionInfo.h"
#include "Message.h"
#include "Resolver.h"
#include "Strings.h"

#ifndef PATHWINDER_SKIP_CONFIG
#include "Configuration.h"
#include "PathwinderConfigReader.h"
#endif

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace Pathwinder
{
  namespace Globals
  {
    /// Holds all static data that falls under the global category. Used to make sure that
    /// globals are initialized as early as possible so that values are available during dynamic
    /// initialization. Implemented as a singleton object.
    class GlobalData
    {
    public:

      /// Returns a reference to the singleton instance of this class.
      /// @return Reference to the singleton instance.
      static GlobalData& GetInstance(void)
      {
        static GlobalData globalData;
        return globalData;
      }

      /// Pseudohandle of the current process.
      HANDLE gCurrentProcessHandle;

      /// PID of the current process.
      DWORD gCurrentProcessId;

      /// Holds information about the current system, as retrieved from Windows.
      SYSTEM_INFO gSystemInformation;

      /// Handle of the instance that represents the running form of this code.
      HINSTANCE gInstanceHandle;

    private:

      /// Default constructor. Objects cannot be constructed externally.
      GlobalData(void)
          : gCurrentProcessHandle(GetCurrentProcess()),
            gCurrentProcessId(GetProcessId(GetCurrentProcess())),
            gSystemInformation(),
            gInstanceHandle(nullptr)
      {
        GetModuleHandleEx(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (LPCWSTR)&GlobalData::GetInstance,
            &gInstanceHandle);
        GetNativeSystemInfo(&gSystemInformation);
      }

      /// Copy constructor. Should never be invoked.
      GlobalData(const GlobalData& other) = delete;
    };

#ifndef PATHWINDER_SKIP_CONFIG
    /// Reads all filesystem rules from a configuration file and attempts to create all the
    /// required filesystem rule objects and build them into a filesystem director object.
    /// Afterwards, on success, the global singleton filesystem director object is initialized
    /// with the newly-built filesystem director object. This function uses move semantics, so
    /// all sections in the configuration data object that define filesystem rules are extracted
    /// out of it. This has the effect of using the filesystem rules defined in the
    /// configuration file to govern the behavior of file operations globally.
    /// @param [in] configData Read-only reference to a configuration data object.
    static void BuildFilesystemRules(Configuration::ConfigurationData& configData)
    {
      if (true == configData.HasReadErrors()) return;

      auto maybeFilesystemDirector =
          FilesystemDirectorBuilder::BuildFromConfigurationData(configData);

      if (true == maybeFilesystemDirector.has_value())
      {
        FilesystemDirector::Singleton() = std::move(*maybeFilesystemDirector);
      }
      else
      {
        // Errors encountered when creating filesystem rules or building the final
        // filesystem director result in no filesystem director being created at all.
        // Therefore it is unnecessary to take any corrective action, such as automatically
        // enabling dry-run mode. Reporting the error is sufficient.

        if (true == Message::IsLogFileEnabled())
          Message::Output(
              Message::ESeverity::ForcedInteractiveWarning,
              L"Errors were encountered during filesystem rule creation. See log file for more information.");
        else
          Message::Output(
              Message::ESeverity::ForcedInteractiveWarning,
              L"Errors were encountered during filesystem rule creation. Enable logging and see log file for more information.");
      }
    }

    /// Enables the log if it is not already enabled.
    /// Regardless, the minimum severity for output is set based on the parameter.
    /// @param [in] logLevel Logging level to configure as the minimum severity for output.
    static void EnableLog(Message::ESeverity logLevel)
    {
      static std::once_flag enableLogFlag;
      std::call_once(
          enableLogFlag,
          [logLevel]() -> void
          {
            Message::CreateAndEnableLogFile();
          });

      Message::SetMinimumSeverityForOutput(logLevel);
    }

    /// Enables the log, if it is configured in the specified configuration data object.
    /// @param [in] configData Read-only reference to a configuration data object.
    static void EnableLogIfConfigured(const Configuration::ConfigurationData& configData)
    {
      const int64_t logLevel =
          configData
              .GetFirstIntegerValue(
                  Configuration::kSectionNameGlobal, Strings::kStrConfigurationSettingLogLevel)
              .value_or(0);

      if (logLevel > 0)
      {
        // Offset the requested severity so that 0 = disabled, 1 = error, 2 = warning, etc.
        const Message::ESeverity configuredSeverity = (Message::ESeverity)(
            logLevel + (int64_t)Message::ESeverity::LowerBoundConfigurableValue);
        EnableLog(configuredSeverity);
      }
    }

    /// Holds and returns a mutable reference to the parsed configuration data object.
    /// @return Mutable reference to parsed configuration data.
    static SConfigurationData& MutableParsedConfigurationData(void)
    {
      static SConfigurationData configData;
      return configData;
    }

    /// Generates and returns a parsed configuration data structure given the configuration data
    /// object read from a configuration file.
    /// @param [in] configData Read-only reference to a configuration data object.
    /// @return Resulting parsed configuration data structure.
    static SConfigurationData
        ParseConfigurationData(const Configuration::ConfigurationData& configData)
    {
      return {
          .isDryRunMode =
              configData
                  .GetFirstBooleanValue(
                      Configuration::kSectionNameGlobal, Strings::kStrConfigurationSettingDryRun)
                  .value_or(false)};
    }

    /// Reads configuration data from the configuration file and returns the resulting
    /// configuration data object. Enables logging and outputs read errors if any are
    /// encountered.
    /// @return Filled configuration data object.
    static Configuration::ConfigurationData ReadConfigurationFile(void)
    {
      PathwinderConfigReader configReader;
      Configuration::ConfigurationData configData =
          configReader.ReadConfigurationFile(Strings::kStrConfigurationFilename);

      if (true == configData.HasReadErrors())
      {
        EnableLog(Message::ESeverity::Error);

        Message::Output(
            Message::ESeverity::Error,
            L"Errors were encountered during configuration file reading.");
        for (const auto& readErrorMessage : configData.GetReadErrorMessages())
          Message::OutputFormatted(Message::ESeverity::Error, L"    %s", readErrorMessage.c_str());

        configData.ClearReadErrorMessages();

        Message::Output(
            Message::ESeverity::ForcedInteractiveWarning,
            L"Errors were encountered during configuration file reading. See log file on the Desktop for more information.");
      }

      return configData;
    }

    /// Reads configured definitions from the configuration file, if specified, and submits them
    /// to the reference resolution subsystem.
    /// @param [in] configData Read-only reference to a configuration data object.
    static void SetResolverConfiguredDefinitions(Configuration::ConfigurationData& configData)
    {
      auto configuredDefinitionsSectionIter =
          configData.Sections().find(Strings::kStrConfigurationSectionDefinitions);
      if (configData.Sections().end() != configuredDefinitionsSectionIter)
        Resolver::SetConfiguredDefinitionsFromSection(
            configData.ExtractSection(configuredDefinitionsSectionIter).second);
    }
#endif

#ifndef PATHWINDER_SKIP_CONFIG
    const SConfigurationData& GetConfigurationData(void)
    {
      return MutableParsedConfigurationData();
    }
#endif

    HANDLE GetCurrentProcessHandle(void)
    {
      return GlobalData::GetInstance().gCurrentProcessHandle;
    }

    DWORD GetCurrentProcessId(void)
    {
      return GlobalData::GetInstance().gCurrentProcessId;
    }

    HINSTANCE GetInstanceHandle(void)
    {
      return GlobalData::GetInstance().gInstanceHandle;
    }

    const SYSTEM_INFO& GetSystemInformation(void)
    {
      return GlobalData::GetInstance().gSystemInformation;
    }

    SVersionInfo GetVersion(void)
    {
      constexpr uint16_t kVersionStructured[] = {GIT_VERSION_STRUCT};
      static_assert(4 == _countof(kVersionStructured), "Invalid structured version information.");

      return {
          .major = kVersionStructured[0],
          .minor = kVersionStructured[1],
          .patch = kVersionStructured[2],
          .flags = kVersionStructured[3],
          .string = _CRT_WIDE(GIT_VERSION_STRING)};
    }

    void Initialize(void)
    {
#ifndef PATHWINDER_SKIP_CONFIG
      Configuration::ConfigurationData configData = ReadConfigurationFile();
      MutableParsedConfigurationData() = ParseConfigurationData(configData);

      EnableLogIfConfigured(configData);
      SetResolverConfiguredDefinitions(configData);

      if (true == GetConfigurationData().isDryRunMode)
        Message::OutputFormatted(
            Message::ESeverity::Warning,
            L"%s is set to DRY RUN mode. All redirection queries will be logged, but no redirection will actually occur.",
            Strings::kStrProductName.data());

      BuildFilesystemRules(configData);
#endif
    }
  } // namespace Globals
} // namespace Pathwinder
