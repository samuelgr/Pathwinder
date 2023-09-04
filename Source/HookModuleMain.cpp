/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file HookModuleMain.cpp
 *   Entry point when injecting Pathwinder as a hook module.
 **************************************************************************************************/

#include <Hookshot/Hookshot.h>

#include "ApiWindows.h"
#include "Globals.h"
#include "Hooks.h"
#include "Message.h"
#include "Strings.h"

/// Convenience wrapper for instantiating a hook record structure for a protected hook the given
/// named Windows API function.
#define PROTECTED_HOOK_RECORD(windowsApiFunctionName)                                              \
    {                                                                                              \
        .protectedHookSetFunc =                                                                    \
            ::Pathwinder::Hooks::ProtectedDependency::##windowsApiFunctionName::SetHook,           \
        .hookProxy = DynamicHook_##windowsApiFunctionName::GetProxy()                              \
    }

/// Convenience wrapper for instantiating a hook record structure for an unprotected hook given the
/// named Windows API function.
#define UNPROTECTED_HOOK_RECORD(windowsApiFunctionName)                                            \
    {                                                                                              \
        .unprotectedHookOriginalAddress =                                                          \
            GetInternalWindowsApiFunctionAddress(#windowsApiFunctionName),                         \
        .hookProxy = DynamicHook_##windowsApiFunctionName::GetProxy()                              \
    }

namespace Pathwinder
{
    namespace Hooks
    {
        /// Holds together all of the information needed to attempt to set a Hookshot dynamic hook.
        struct DynamicHookRecord
        {
            /// Address of the function that will be invoked to set the hook. Used by protected
            /// hooks only, as unprotected hooks are set via the hook proxy.
            Hookshot::EResult (*protectedHookSetFunc)(Hookshot::IHookshot*);

            /// Address of the original function to be hooked. Used by unprotected hooks only and
            /// passed to the hook proxy.
            void* unprotectedHookOriginalAddress;

            /// Proxy object for the dynamic hook object itself.
            Hookshot::DynamicHookProxy hookProxy;
        };

        /// Attempts to set all required Hookshot hooks.
        /// Terminates this process if any hook fails to be set.
        static void SetAllHooksOrDie(Hookshot::IHookshot* hookshot)
        {
            // References the hooks declared in "Hooks.h" and must contain all of them.
            const DynamicHookRecord hookRecords[] = {
                PROTECTED_HOOK_RECORD(NtClose),
                PROTECTED_HOOK_RECORD(NtCreateFile),
                PROTECTED_HOOK_RECORD(NtDeleteFile),
                PROTECTED_HOOK_RECORD(NtOpenFile),
                PROTECTED_HOOK_RECORD(NtQueryDirectoryFile),
                PROTECTED_HOOK_RECORD(NtQueryDirectoryFileEx),
                PROTECTED_HOOK_RECORD(NtQueryInformationByName),
                PROTECTED_HOOK_RECORD(NtQueryInformationFile),
                PROTECTED_HOOK_RECORD(NtSetInformationFile),
                UNPROTECTED_HOOK_RECORD(NtQueryAttributesFile)};

            Message::OutputFormatted(
                Message::ESeverity::Debug,
                L"Attempting to hook %u Windows API function(s).",
                static_cast<unsigned int>(_countof(hookRecords)));

            for (const auto& hookRecord : hookRecords)
            {
                if (nullptr != hookRecord.protectedHookSetFunc)
                {
                    // Hook is protected.
                    // Setting it must succeed in order for Pathwinder to function correctly.

                    const Hookshot::EResult setHookResult =
                        hookRecord.protectedHookSetFunc(hookshot);

                    if (false == Hookshot::SuccessfulResult(setHookResult))
                    {
                        Message::OutputFormatted(
                            Message::ESeverity::ForcedInteractiveError,
                            L"%.*s failed to set a hook for the Windows API function \"%s\" and cannot function without it.\n\nHookshot::EResult = %u",
                            static_cast<int>(Strings::kStrProductName.length()),
                            Strings::kStrProductName.data(),
                            hookRecord.hookProxy.GetFunctionName(),
                            static_cast<unsigned int>(setHookResult));
                        TerminateProcess(Globals::GetCurrentProcessHandle(), (UINT)-1);
                    }
                }
                else
                {
                    // Hook is unprotected.
                    // Setting it successfully is considered optional.

                    const Hookshot::EResult setHookResult = hookRecord.hookProxy.SetHook(
                        hookshot, hookRecord.unprotectedHookOriginalAddress);

                    if (false == Hookshot::SuccessfulResult(setHookResult))
                    {
                        Message::OutputFormatted(
                            Message::ESeverity::Warning,
                            L"Failed to hook the \"%s\" Windows API function (Hookshot::EResult = %u).",
                            hookRecord.hookProxy.GetFunctionName(),
                            static_cast<unsigned int>(setHookResult));
                        continue;
                    }
                }

                Message::OutputFormatted(
                    Message::ESeverity::Debug,
                    L"Successfully hooked the \"%s\" Windows API function.",
                    hookRecord.hookProxy.GetFunctionName());
            }
        }
    }  // namespace Hooks
}  // namespace Pathwinder

HOOKSHOT_HOOK_MODULE_ENTRY(hookshot)
{
    Pathwinder::Globals::Initialize();
    Pathwinder::Hooks::SetAllHooksOrDie(hookshot);
}
