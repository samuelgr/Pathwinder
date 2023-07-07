/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file Hooks.h
 *   Declarations for all Windows API hooks used to implement path redirection.
 *****************************************************************************/

#pragma once

#include "ApiWindows.h"
#include "DebugAssert.h"

#include <Hookshot/DynamicHook.h>
#include <type_traits>
#include <winternl.h>


namespace Pathwinder
{
    // -------- TYPE DEFINITIONS ------------------------------------------- //

    namespace _ProtectedDependencyInternal
    {
        /// Primary template for protected dependencies.
        /// Only the specializations are useful.
        template <const char* kFunctionName, typename DynamicHookType, typename T> class ProtectedDependencyImpl
        {
            static_assert(std::is_function<T>::value, "Supplied template argument must map to a function type.");
        };

        /// Protected dependency wrapper.
        /// API functions that are hooked will potentially need to be invoked internally by Pathwinder.
        /// This template specialization provides a single entry point that internal code can invoke to access the original functionality of the API, whether or not the hook has been set successfully.
        /// It is intended to be accessed using the macro interface defined below.
        template <const char* kFunctionName, typename DynamicHookType, typename ReturnType, typename... ArgumentTypes> class ProtectedDependencyImpl<kFunctionName, DynamicHookType, ReturnType(ArgumentTypes...)>
        {
        private:
            inline static const DynamicHookType::TFunctionPtr initialFunctionPointer = static_cast<DynamicHookType::TFunctionPtr>(GetInternalWindowsApiFunctionAddress(kFunctionName));

        public:
            /// Invokes the protected API function.
            /// If the hook has already been set, then the original function is invoked directly from the hook.
            /// Otherwise the initial address of the function, prior to hooking, is invoked.
            static inline ReturnType SafeInvoke(ArgumentTypes... args)
            {
                DebugAssert(nullptr != initialFunctionPointer, "Unable to locate initial address for an API function.");

                if (true == DynamicHookType::IsHookSet())
                    return DynamicHookType::Original(std::forward<ArgumentTypes>(args)...);
                else
                    return initialFunctionPointer(std::forward<ArgumentTypes>(args)...);
            }

            /// Sets the hook for the associated protected API function using the known initial address, which is determined at runtime.
            /// This is a simple wrapper that passes the request along to Hookshot and returns whatever it gets back.
            static Hookshot::EResult SetHook(Hookshot::IHookshot* hookshot)
            {
                DebugAssert(nullptr != initialFunctionPointer, "Unable to locate initial address for an API function.");
                return DynamicHookType::SetHook(hookshot, initialFunctionPointer);
            }
        };
    }
}


// -------- MACROS --------------------------------------------------------- //

/// Creates a Hookshot dynamic hook and defines a protected dependency wrapper for it.
#define PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(funcname, typespec) \
    HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(funcname, typespec); \
    namespace _ProtectedDependencyInternal { inline constexpr char kProtectedDynamicHookName__##funcname[] = #funcname; } \
    namespace ProtectedDependency { using funcname = ::Pathwinder::_ProtectedDependencyInternal::ProtectedDependencyImpl<_ProtectedDependencyInternal::kProtectedDynamicHookName__##funcname, ::Pathwinder::Hooks::DynamicHook_##funcname, ::Pathwinder::Hooks::DynamicHook_##funcname::TFunction>; }


namespace Pathwinder
{
    // -------- HOOKS ------------------------------------------------------ //

    namespace Hooks
    {
        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntclose for more information.
        PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtClose, NTSTATUS(__stdcall)(HANDLE));

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntcreatefile for more information.
        PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtCreateFile, NTSTATUS(__stdcall)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG));

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntopenfile for more information.
        PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtOpenFile, NTSTATUS(__stdcall)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG));

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile for more information.
        PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtQueryDirectoryFile, NTSTATUS(__stdcall)(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS, BOOLEAN, PUNICODE_STRING, BOOLEAN));

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfileex for more information.
        PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtQueryDirectoryFileEx, NTSTATUS(__stdcall)(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS, ULONG, PUNICODE_STRING));

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationbyname for more information.
        PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtQueryInformationByName, NTSTATUS(__stdcall)(POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS));
    }
}