/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file HookModuleMain.cpp
 *   Entry point when injecting Pathwinder as a hook module.
 *****************************************************************************/

#include "ApiWindows.h"
#include "Globals.h"
#include "Hooks.h"
#include "Message.h"
#include "Strings.h"

#include <Hookshot/Hookshot.h>


// -------- MACROS --------------------------------------------------------- //

/// Convenience wrapper for instantiating a hook record structure for the given named Windows API function.
#define HOOK_RECORD(windowsApiFunctionName)                                 {.setHookFunc = ::Pathwinder::Hooks::ProtectedDependency::##windowsApiFunctionName::SetHook, .hookProxy = DynamicHook_##windowsApiFunctionName::GetProxy()}


namespace Pathwinder
{
    namespace Hooks
    {
        // -------- INTERNAL TYPES ----------------------------------------- //

        /// Holds together all of the information needed to attempt to set a Hookshot dynamic hook.
        struct DynamicHookRecord
        {
            Hookshot::EResult(*setHookFunc)(Hookshot::IHookshot*);          ///< Address of the function that will be invoked to set the hook.
            Hookshot::DynamicHookProxy hookProxy;                           ///< Proxy object for the dynamic hook object itself.
        };

        // -------- INTERNAL FUNCTIONS ------------------------------------- //

        /// Attempts to set all required Hookshot hooks.
        /// Terminates this process if any hook fails to be set.
        static void SetAllHooksOrDie(Hookshot::IHookshot* hookshot)
        {
            // References the hooks declared in "Hooks.h" and must contain all of them.
            const DynamicHookRecord hookRecords[] = {
                HOOK_RECORD(NtClose),
                HOOK_RECORD(NtCreateFile),
                HOOK_RECORD(NtOpenFile),
                HOOK_RECORD(NtQueryDirectoryFile),
                HOOK_RECORD(NtQueryDirectoryFileEx),
                HOOK_RECORD(NtQueryInformationByName)
            };

            Message::OutputFormatted(Message::ESeverity::Debug, L"Attempting to hook %u Windows API function(s).", (unsigned int)_countof(hookRecords));

            for (const auto& hookRecord : hookRecords)
            {
                const Hookshot::EResult setHookResult = hookRecord.setHookFunc(hookshot);

                if (false == Hookshot::SuccessfulResult(setHookResult))
                {
                    Message::OutputFormatted(Message::ESeverity::ForcedInteractiveError, L"%.*s failed to set a hook for the Windows API function \"%s\" and cannot function without it.\n\nHookshot::EResult = %u", (int)Strings::kStrProductName.length(), Strings::kStrProductName.data(), hookRecord.hookProxy.GetFunctionName(), (unsigned int)setHookResult);
                    TerminateProcess(Globals::GetCurrentProcessHandle(), (UINT)-1);
                }

                Message::OutputFormatted(Message::ESeverity::Debug, L"Successfully hooked the \"%s\" Windows API function.", hookRecord.hookProxy.GetFunctionName());
            }
        }
    }
}


// -------- ENTRY POINT ---------------------------------------------------- //

/// Hook module entry point. 
HOOKSHOT_HOOK_MODULE_ENTRY(hookshot)
{
    Pathwinder::Globals::Initialize();
    Pathwinder::Hooks::SetAllHooksOrDie(hookshot);
}
