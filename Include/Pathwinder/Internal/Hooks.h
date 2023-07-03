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
#include "FilesystemDirector.h"
#include "Globals.h"
#include "Message.h"
#include "TemporaryBuffer.h"

#include <Hookshot/DynamicHook.h>
#include <optional>


// -------- MACROS --------------------------------------------------------- //

/// Function body for hook functions that take a single path as a parameter to be used as input for redirection.
/// The path parameter is expected to be a pointer to a narrow-character string and is conditionally overwritten with the result of a filesystem redirection query.
#define HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_NARROW(charStringPtrParamName) \
    auto maybeRedirectedFileName = FilesystemDirector::Singleton().RedirectSingleFile(Strings::ConvertStringNarrowToWide(charStringPtrParamName)); \
    TemporaryBuffer<char> redirectedFileName; \
    if (true == maybeRedirectedFileName.has_value()) \
    { \
        if (Message::WillOutputMessageOfSeverity(Message::ESeverity::Info)) \
            Message::OutputFormatted(Message::ESeverity::Info, L"%s: Invoked with path \"%s\" which was redirected to \"%s\".", GetFunctionName(), Strings::ConvertStringNarrowToWide(charStringPtrParamName).AsCString(), maybeRedirectedFileName.value().Data()); \
        if (false == Globals::GetConfigurationData().isDryRunMode) { \
            redirectedFileName = Strings::ConvertStringWideToNarrow(maybeRedirectedFileName.value().AsCString()); \
            maybeRedirectedFileName.reset(); \
            charStringPtrParamName = redirectedFileName.Data(); \
        } \
    } \
    else \
    { \
        if (Message::WillOutputMessageOfSeverity(Message::ESeverity::SuperDebug)) \
            Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s: Invoked with path \"%s\" which was not redirected.", GetFunctionName(), Strings::ConvertStringNarrowToWide(charStringPtrParamName).AsCString()); \
    }

/// Function body for hook functions that take a single path as a parameter to be used as input for redirection.
/// The path parameter is expected to be a pointer to a wide-character string and is conditionally overwritten with the result of a filesystem redirection query.
#define HOOK_FUNCTION_BODY_LOG_AND_REDIRECT_PARAM_WIDE(wcharStringPtrParamName) \
    auto maybeRedirectedFileName = FilesystemDirector::Singleton().RedirectSingleFile(wcharStringPtrParamName); \
    if (true == maybeRedirectedFileName.has_value()) \
    { \
        Message::OutputFormatted(Message::ESeverity::Info, L"%s: Invoked with path \"%s\" which was redirected to \"%s\".", GetFunctionName(), wcharStringPtrParamName, maybeRedirectedFileName.value().AsCString()); \
        if (false == Globals::GetConfigurationData().isDryRunMode) \
            wcharStringPtrParamName = maybeRedirectedFileName.value().AsCString(); \
    } \
    else \
    { \
        Message::OutputFormatted(Message::ESeverity::SuperDebug, L"%s: Invoked with path \"%s\" which was not redirected.", GetFunctionName(), wcharStringPtrParamName); \
    }


namespace Pathwinder
{
    namespace Hooks
    {
        // -------- HOOKS -------------------------------------------------- //
        // See original Windows API functions and Hookshot documentation for details.
        // Each hook function is implemented in its own source file.

        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(CreateFile2);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(CreateFileA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(CreateFileW);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(CreateFileTransactedA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(CreateFileTransactedW);

        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(CreateDirectoryA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(CreateDirectoryW);

        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(FindFirstFileA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(FindFirstFileW);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(FindFirstFileExA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(FindFirstFileExW);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(FindFirstFileTransactedA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(FindFirstFileTransactedW);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(FindNextFileA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(FindNextFileW);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(FindClose);

        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(GetFileAttributesA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(GetFileAttributesW);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(GetFileAttributesExA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(GetFileAttributesExW);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(GetFileAttributesTransactedA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(GetFileAttributesTransactedW);

        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(OpenFile);

        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(SetFileAttributesA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(SetFileAttributesW);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(SetFileAttributesTransactedA);
        HOOKSHOT_DYNAMIC_HOOK_FROM_FUNCTION(SetFileAttributesTransactedW);
    }
}
