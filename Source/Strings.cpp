/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file Strings.cpp
 *   Implementation of functions for manipulating Pathwinder-specific strings.
 *****************************************************************************/

#include "Globals.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <cstdlib>
#include <mutex>
#include <psapi.h>
#include <sal.h>
#include <shlobj.h>
#include <sstream>
#include <string>
#include <string_view>
#include <windows.h>


namespace Pathwinder
{
    namespace Strings
    {
        // -------- INTERNAL CONSTANTS ------------------------------------- //

        /// File extension for a configuration file.
        static constexpr std::wstring_view kStrConfigurationFileExtension = L".ini";

        /// File extension for a log file.
        static constexpr std::wstring_view kStrLogFileExtension = L".log";


        // -------- INTERNAL FUNCTIONS ------------------------------------- //

        /// Generates the value for kStrProductName; see documentation of this run-time constant for more information.
        /// @return Corresponding run-time constant value.
        static const std::wstring& GetProductName(void)
        {
            static std::wstring initString;
            static std::once_flag initFlag;

            std::call_once(initFlag, []() -> void
                {
                    const wchar_t* stringStart = nullptr;
                    int stringLength = LoadString(Globals::GetInstanceHandle(), IDS_PATHWINDER_PRODUCT_NAME, (wchar_t*)&stringStart, 0);

                    while ((stringLength > 0) && (L'\0' == stringStart[stringLength - 1]))
                        stringLength -= 1;

                    if (stringLength > 0)
                        initString.assign(stringStart, &stringStart[stringLength]);
                }
            );

            return initString;
        }

        /// Generates the value for kStrExecutableCompleteFilename; see documentation of this run-time constant for more information.
        /// @return Corresponding run-time constant value.
        static const std::wstring& GetExecutableCompleteFilename(void)
        {
            static std::wstring initString;
            static std::once_flag initFlag;

            std::call_once(initFlag, []() -> void
                {
                    TemporaryBuffer<wchar_t> buf;
                    GetModuleFileName(nullptr, buf, (DWORD)buf.Capacity());

                    initString.assign(buf);
                }
            );

            return initString;
        }

        /// Generates the value for kStrExecutableBaseName; see documentation of this run-time constant for more information.
        /// @return Corresponding run-time constant value.
        static const std::wstring& GetExecutableBaseName(void)
        {
            static std::wstring initString;
            static std::once_flag initFlag;

            std::call_once(initFlag, []() -> void
                {
                    std::wstring_view executableBaseName = GetExecutableCompleteFilename();

                    const size_t lastBackslashPos = executableBaseName.find_last_of(L"\\");
                    if (std::wstring_view::npos != lastBackslashPos)
                        executableBaseName.remove_prefix(1 + lastBackslashPos);

                    initString.assign(executableBaseName);
                }
            );

            return initString;
        }

        /// Generates the value for kStrExecutableDirectoryName; see documentation of this run-time constant for more information.
        /// @return Corresponding run-time constant value.
        static const std::wstring& GetExecutableDirectoryName(void)
        {
            static std::wstring initString;
            static std::once_flag initFlag;

            std::call_once(initFlag, []() -> void
                {
                    std::wstring_view executableDirectoryName = GetExecutableCompleteFilename();

                    const size_t lastBackslashPos = executableDirectoryName.find_last_of(L"\\");
                    if (std::wstring_view::npos != lastBackslashPos)
                    {
                        executableDirectoryName.remove_suffix(executableDirectoryName.length() - lastBackslashPos - 1);
                        initString.assign(executableDirectoryName);
                    }
                }
            );

            return initString;
        }

        /// Generates the value for kStrPathwinderCompleteFilename; see documentation of this run-time constant for more information.
        /// @return Corresponding run-time constant value.
        static const std::wstring& GetPathwinderCompleteFilename(void)
        {
            static std::wstring initString;
            static std::once_flag initFlag;

            std::call_once(initFlag, []() -> void
                {
                    TemporaryBuffer<wchar_t> buf;
                    GetModuleFileName(Globals::GetInstanceHandle(), buf, (DWORD)buf.Capacity());

                    initString.assign(buf);
                }
            );

            return initString;
        }

        /// Generates the value for kStrPathwinderBaseName; see documentation of this run-time constant for more information.
        /// @return Corresponding run-time constant value.
        static const std::wstring& GetPathwinderBaseName(void)
        {
            static std::wstring initString;
            static std::once_flag initFlag;

            std::call_once(initFlag, []() -> void
                {
                    std::wstring_view executableBaseName = GetPathwinderCompleteFilename();

                    const size_t lastBackslashPos = executableBaseName.find_last_of(L"\\");
                    if (std::wstring_view::npos != lastBackslashPos)
                        executableBaseName.remove_prefix(1 + lastBackslashPos);

                    initString.assign(executableBaseName);
                }
            );

            return initString;
        }

        /// Generates the value for kStrPathwinderDirectoryName; see documentation of this run-time constant for more information.
        /// @return Corresponding run-time constant value.
        static const std::wstring& GetPathwinderDirectoryName(void)
        {
            static std::wstring initString;
            static std::once_flag initFlag;

            std::call_once(initFlag, []() -> void
                {
                    std::wstring_view executableDirectoryName = GetPathwinderCompleteFilename();

                    const size_t lastBackslashPos = executableDirectoryName.find_last_of(L"\\");
                    if (std::wstring_view::npos != lastBackslashPos)
                    {
                        executableDirectoryName.remove_suffix(executableDirectoryName.length() - lastBackslashPos - 1);
                        initString.assign(executableDirectoryName);
                    }
                }
            );

            return initString;
        }

        /// Generates the value for kStrConfigurationFilename; see documentation of this run-time constant for more information.
        /// @return Corresponding run-time constant value.
        static const std::wstring& GetConfigurationFilename(void)
        {
            static std::wstring initString;
            static std::once_flag initFlag;

            std::call_once(initFlag, []() -> void
                {
                    std::wstring_view pieces[] = {GetPathwinderDirectoryName(), GetProductName(), kStrConfigurationFileExtension};

                    size_t totalLength = 0;
                    for (int i = 0; i < _countof(pieces); ++i)
                        totalLength += pieces[i].length();

                    initString.reserve(1 + totalLength);

                    for (int i = 0; i < _countof(pieces); ++i)
                        initString.append(pieces[i]);
                }
            );

            return initString;
        }

        /// Generates the value for kStrLogFilename; see documentation of this run-time constant for more information.
        /// @return Corresponding run-time constant value.
        static const std::wstring& GetLogFilename(void)
        {
            static std::wstring initString;
            static std::once_flag initFlag;

            std::call_once(initFlag, []() -> void
                {
                    std::wstringstream logFilename;

                    PWSTR knownFolderPath;
                    const HRESULT result = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &knownFolderPath);

                    if (S_OK == result)
                    {
                        logFilename << knownFolderPath << L'\\';
                        CoTaskMemFree(knownFolderPath);
                    }

                    logFilename << GetProductName().c_str() << L'_' << GetExecutableBaseName().c_str() << L'_' << Globals::GetCurrentProcessId() << kStrLogFileExtension;

                    initString.assign(logFilename.str());
                }
            );

            return initString;
        }


        // -------- RUN-TIME CONSTANTS ------------------------------------- //
        // See "Strings.h" for documentation.

        extern const std::wstring_view kStrProductName(GetProductName());
        extern const std::wstring_view kStrExecutableCompleteFilename(GetExecutableCompleteFilename());
        extern const std::wstring_view kStrExecutableBaseName(GetExecutableBaseName());
        extern const std::wstring_view kStrExecutableDirectoryName(GetExecutableDirectoryName());
        extern const std::wstring_view kStrPathwinderCompleteFilename(GetPathwinderCompleteFilename());
        extern const std::wstring_view kStrPathwinderBaseName(GetPathwinderBaseName());
        extern const std::wstring_view kStrPathwinderDirectoryName(GetPathwinderDirectoryName());
        extern const std::wstring_view kStrConfigurationFilename(GetConfigurationFilename());
        extern const std::wstring_view kStrLogFilename(GetLogFilename());


        // -------- FUNCTIONS ---------------------------------------------- //
        // See "Strings.h" for documentation.

        TemporaryBuffer<wchar_t> FormatString(_Printf_format_string_ const wchar_t* format, ...)
        {
            TemporaryBuffer<wchar_t> buf;

            va_list args;
            va_start(args, format);

            vswprintf_s(buf.Data(), buf.Capacity(), format, args);

            va_end(args);

            return buf;
        }

        // --------

        TemporaryVector<std::wstring_view> SplitString(std::wstring_view stringToSplit, std::wstring_view delimiter)
        {
            TemporaryVector<std::wstring_view> stringPieces;

            auto beginIter = stringToSplit.cbegin();
            auto endIter = ((false == delimiter.empty()) ? beginIter : stringToSplit.cend());

            while ((stringPieces.Size() < stringPieces.Capacity())  && (stringToSplit.cend() != endIter))
            {
                std::wstring_view remainingStringToSplit(endIter, stringToSplit.cend());
                if (true == remainingStringToSplit.starts_with(delimiter))
                {
                    stringPieces.EmplaceBack(beginIter, endIter);
                    endIter += delimiter.length();
                    beginIter = endIter;
                }
                else
                {
                    endIter += 1;
                }
            }

            if (stringPieces.Size() < stringPieces.Capacity())
                stringPieces.EmplaceBack(beginIter, endIter);

            return stringPieces;
        }

        // --------

        std::wstring SystemErrorCodeString(const unsigned long systemErrorCode)
        {
            TemporaryBuffer<wchar_t> systemErrorString;
            DWORD systemErrorLength = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, systemErrorCode, 0, systemErrorString, systemErrorString.Capacity(), nullptr);

            if (0 == systemErrorLength)
            {
                swprintf_s(systemErrorString, systemErrorString.Capacity(), L"System error %u.", (unsigned int)systemErrorCode);
            }
            else
            {
                for (; systemErrorLength > 0; --systemErrorLength)
                {
                    if (L'\0' != systemErrorString[systemErrorLength] && !iswspace(systemErrorString[systemErrorLength]))
                        break;

                    systemErrorString[systemErrorLength] = L'\0';
                }
            }

            return std::wstring(systemErrorString);
        }
    }
}
