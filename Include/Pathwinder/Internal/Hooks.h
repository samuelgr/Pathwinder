/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file Hooks.h
 *   Declarations for all Windows API hooks used to implement path redirection.
 **************************************************************************************************/

#pragma once

#include <type_traits>

#include <Hookshot/DynamicHook.h>

#include "ApiWindows.h"
#include "DebugAssert.h"
#include "FileInformationStruct.h"
#include "FilesystemDirector.h"

// Creates a Hookshot dynamic hook and defines a protected dependency wrapper for it.
#define PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(funcname, typespec)                          \
  HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(funcname, typespec);                                         \
  namespace _ProtectedDependencyInternal                                                           \
  {                                                                                                \
    inline constexpr char kProtectedDynamicHookName__##funcname[] = #funcname;                     \
  }                                                                                                \
  namespace ProtectedDependency                                                                    \
  {                                                                                                \
    using funcname = ::Pathwinder::_ProtectedDependencyInternal::ProtectedDependencyImpl<          \
        _ProtectedDependencyInternal::kProtectedDynamicHookName__##funcname,                       \
        ::Pathwinder::Hooks::DynamicHook_##funcname,                                               \
        ::Pathwinder::Hooks::DynamicHook_##funcname::TFunction>;                                   \
  }

// Selects an appropriate calling convention for the protected dependency hook template, based on
// whether or not the target is a 32-bit or 64-bit binary. Hard-coding a single calling convention
// works because all of the hooked APIs use the same one. This is also simpler in code than adding a
// generic template to detect it automatically.
#ifdef _WIN64
#define PROTECTED_DEPENDENCY_IMPL_CALLING_CONVENTION
#else
#define PROTECTED_DEPENDENCY_IMPL_CALLING_CONVENTION (__stdcall)
#endif

namespace Pathwinder
{
  namespace _ProtectedDependencyInternal
  {
    /// Primary template for protected dependencies. Only the specializations are useful.
    template <const char* kFunctionName, typename DynamicHookType, typename T>
    class ProtectedDependencyImpl
    {
      static_assert(
          std::is_function<T>::value, "Supplied template argument must map to a function type.");
    };

    /// Protected dependency wrapper. API functions that are hooked will potentially need to be
    /// invoked internally by Pathwinder. This template specialization provides a single entry
    /// point that internal code can invoke to access the original functionality of the API,
    /// whether or not the hook has been set successfully. It is intended to be accessed using
    /// the macro interface defined below.
    template <
        const char* kFunctionName,
        typename DynamicHookType,
        typename ReturnType,
        typename... ArgumentTypes>
    class ProtectedDependencyImpl<
        kFunctionName,
        DynamicHookType,
        ReturnType PROTECTED_DEPENDENCY_IMPL_CALLING_CONVENTION(ArgumentTypes...)>
    {
    private:

      inline static const DynamicHookType::TFunctionPtr initialFunctionPointer =
          static_cast<DynamicHookType::TFunctionPtr>(
              GetInternalWindowsApiFunctionAddress(kFunctionName));

    public:

      ProtectedDependencyImpl(void) = delete;
      ProtectedDependencyImpl(const ProtectedDependencyImpl& other) = delete;
      ProtectedDependencyImpl(ProtectedDependencyImpl&& other) = delete;

      /// Invokes the protected API function. If the hook has already been set, then the
      /// original function is invoked directly from the hook. Otherwise the initial address
      /// of the function, prior to hooking, is invoked.
      static inline ReturnType SafeInvoke(ArgumentTypes... args)
      {
        DebugAssert(
            nullptr != initialFunctionPointer,
            "Unable to locate initial address for an API function.");

        if (DynamicHookType::IsHookSet())
          return DynamicHookType::Original(std::forward<ArgumentTypes>(args)...);
        else
          return initialFunctionPointer(std::forward<ArgumentTypes>(args)...);
      }

      /// Sets the hook for the associated protected API function using the known initial
      /// address, which is determined at runtime. This is a simple wrapper that passes the
      /// request along to Hookshot and returns whatever it gets back.
      static Hookshot::EResult SetHook(Hookshot::IHookshot* hookshot)
      {
        DebugAssert(
            nullptr != initialFunctionPointer,
            "Unable to locate initial address for an API function.");
        return DynamicHookType::SetHook(hookshot, initialFunctionPointer);
      }
    };
  } // namespace _ProtectedDependencyInternal
} // namespace Pathwinder

namespace Pathwinder
{
  namespace Hooks
  {
    /// Sets the filesystem director object instance that will be used to implement filesystem
    /// redirection when hook functions are invoked. Typically this is created during Pathwinder
    /// initialization using a filesystem director builder.
    /// @param [in] filesystemDirector Filesystem director object instance to use.
    void SetFilesystemDirectorInstance(FilesystemDirector&& filesystemDirector);

    // Pathwinder requires these hooks to be set in order to function correctly and may invoke
    // their original versions. These functions are documented parts of Windows, though they may
    // be internal or part of the driver development kit (WDK).

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntclose
    PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(NtClose, NTSTATUS(__stdcall)(HANDLE));

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntcreatefile
    PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(
        NtCreateFile,
        NTSTATUS(__stdcall)(
            PHANDLE,
            ACCESS_MASK,
            POBJECT_ATTRIBUTES,
            PIO_STATUS_BLOCK,
            PLARGE_INTEGER,
            ULONG,
            ULONG,
            ULONG,
            ULONG,
            PVOID,
            ULONG));

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwdeletefile
    PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(
        NtDeleteFile, NTSTATUS(__stdcall)(POBJECT_ATTRIBUTES));

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntopenfile
    PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(
        NtOpenFile,
        NTSTATUS(__stdcall)(
            PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG));

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile
    PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(
        NtQueryDirectoryFile,
        NTSTATUS(__stdcall)(
            HANDLE,
            HANDLE,
            PIO_APC_ROUTINE,
            PVOID,
            PIO_STATUS_BLOCK,
            PVOID,
            ULONG,
            FILE_INFORMATION_CLASS,
            BOOLEAN,
            PUNICODE_STRING,
            BOOLEAN));

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfileex
    PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(
        NtQueryDirectoryFileEx,
        NTSTATUS(__stdcall)(
            HANDLE,
            HANDLE,
            PIO_APC_ROUTINE,
            PVOID,
            PIO_STATUS_BLOCK,
            PVOID,
            ULONG,
            FILE_INFORMATION_CLASS,
            ULONG,
            PUNICODE_STRING));

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationbyname
    PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(
        NtQueryInformationByName,
        NTSTATUS(__stdcall)(
            POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS));

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationfile
    PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(
        NtQueryInformationFile,
        NTSTATUS(__stdcall)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS));

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntsetinformationfile
    PROTECTED_HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(
        NtSetInformationFile,
        NTSTATUS(__stdcall)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS));

    // Pathwinder does not require these hooks to be set in order to function correctly, nor
    // does it ever invoke their original versions. These functions are internal to Windows,
    // potentially undocumented, and not guaranteed to exist in future versions.

    // https://learn.microsoft.com/en-us/windows/win32/devnotes/ntqueryattributesfile
    HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(
        NtQueryAttributesFile, NTSTATUS(__stdcall)(POBJECT_ATTRIBUTES, PFILE_BASIC_INFO));

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwqueryfullattributesfile
    HOOKSHOT_DYNAMIC_HOOK_FROM_TYPESPEC(
        NtQueryFullAttributesFile, NTSTATUS(__stdcall)(POBJECT_ATTRIBUTES, SFileNetworkOpenInformation*));
  } // namespace Hooks
} // namespace Pathwinder
