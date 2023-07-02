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
#define HOOK_RECORD(windowsApiFunctionName)                                 {.functionName = #windowsApiFunctionName, .staticFunctionAddress = (void*)&windowsApiFunctionName, .hookProxy = DynamicHook_##windowsApiFunctionName::GetProxy()}


namespace Pathwinder
{
    namespace Hooks
    {
        // -------- INTERNAL TYPES ----------------------------------------- //

        /// Holds together all of the information needed to attempt to set a Hookshot dynamic hook.
        struct DynamicHookRecord
        {
            const char* functionName;                                       ///< Name of the Windows API function being hooked, using narrow-character format.
            void* staticFunctionAddress;                                    ///< Static address of the Windows API function, as determined at compile and link time.
            Hookshot::DynamicHookProxy hookProxy;                           ///< Dynamic hook proxy object.

            /// Convenience method for attempting to set the Hookshot dynamic hook represented by this record.
            inline Hookshot::EResult TrySetHook(Hookshot::IHookshot* hookshot) const
            {
                return hookProxy.SetHook(hookshot, GetWindowsApiFunctionAddress(functionName, staticFunctionAddress));
            }
        };

        // -------- INTERNAL INTERNAL FUNCTIONS ---------------------------- //

        /// Attempts to set all required Hookshot hooks.
        /// Terminates this process if any hook fails to be set.
        static void SetAllHooksOrDie(Hookshot::IHookshot* hookshot)
        {
            const DynamicHookRecord hookRecords[] = {
                HOOK_RECORD(CreateFile2),
                HOOK_RECORD(CreateFileA),
                HOOK_RECORD(CreateFileW)
            };

            Message::OutputFormatted(Message::ESeverity::Debug, L"Attempting to hook %u Windows API function(s).", (unsigned int)_countof(hookRecords));

            for (const auto& hookRecord : hookRecords)
            {
                const Hookshot::EResult setHookResult = hookRecord.TrySetHook(hookshot);

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
