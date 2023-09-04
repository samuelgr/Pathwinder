/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file Globals.h
 *   Declaration of a namespace for storing and retrieving global data. Intended for
 *   miscellaneous data elements with no other suitable place.
 **************************************************************************************************/

#pragma once

#include <string>
#include <string_view>

#include "ApiWindows.h"

#ifndef PATHWINDER_SKIP_CONFIG
#include "Configuration.h"
#endif

namespace Pathwinder
{
    namespace Globals
    {
        /// Version information structure.
        struct SVersionInfo
        {
            /// Major version number.
            uint16_t major;

            /// Minor version number.
            uint16_t minor;

            /// Patch level.
            uint16_t patch;

            union
            {
                /// Complete view of the flags element of structured version information.
                uint16_t flags;

                // Per Microsoft documentation, bit fields are ordered from low bit to high bit.
                // See https://docs.microsoft.com/en-us/cpp/cpp/cpp-bit-fields for more information.
                struct
                {
                    /// Whether or not the working directory was dirty when the binary was built.
                    uint16_t isDirty : 1;

                    /// Unused bits, reserved for future use.
                    uint16_t reserved : 3;

                    /// Number of commits since the most recent official version tag.
                    uint16_t commitDistance : 12;
                };
            };

            /// String representation of the version information, including any suffixes.
            /// Guaranteed to be null-terminated.
            std::wstring_view string;
        };

        static_assert(
            sizeof(SVersionInfo) == ((4 * sizeof(uint16_t)) + sizeof(std::wstring_view)),
            "Version information structure size constraint violation."
        );

        /// Configuration data parsed from a configuration file.
        struct SConfigurationData
        {
            /// Whether or not "dry run" mode is enabled. If so, redirection queries are logged but
            /// not actuated.
            bool isDryRunMode;
        };

#ifndef PATHWINDER_SKIP_CONFIG
        /// Retrieves the configuration data after being parsed from a configuration file.
        /// @return Read-only configuration object reference.
        const SConfigurationData& GetConfigurationData(void);
#endif

        /// Retrieves a pseudohandle to the current process.
        /// @return Current process pseudohandle.
        HANDLE GetCurrentProcessHandle(void);

        /// Retrieves the PID of the current process.
        /// @return Current process PID.
        DWORD GetCurrentProcessId(void);

        /// Retrieves the handle of the instance that represents the current running form of this
        /// code.
        /// @return Instance handle for this code.
        HINSTANCE GetInstanceHandle(void);

        /// Retrieves information on the current system. This includes architecture, page size, and
        /// so on.
        /// @return Reference to a read-only structure containing system information.
        const SYSTEM_INFO& GetSystemInformation(void);

        /// Retrieves and returns version information for this running binary.
        /// @return Version information structure.
        SVersionInfo GetVersion(void);

        /// Performs run-time initialization.
        /// This function only performs operations that are safe to perform within a DLL entry
        /// point.
        void Initialize(void);
    }  // namespace Globals
}  // namespace Pathwinder
