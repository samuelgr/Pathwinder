/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file ResolverTest.cpp
 *   Unit tests for resolution of named references contained within a string.
 *****************************************************************************/

#include "Resolver.h"
#include "Strings.h"
#include "TestCase.h"
#include "TemporaryBuffer.h"

#include <optional>
#include <string>
#include <string_view>
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>


namespace PathwinderTest
{
    using namespace ::Pathwinder;
    using namespace ::Pathwinder::Resolver;


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Attempts to resolve an environment variable to a string.
    /// @param [in] name Environment variable name.
    /// @return Resolved string, if resolution succeeded.
    static std::optional<std::wstring> GetEnvironmentVariableString(std::wstring_view name)
    {
        TemporaryBuffer<wchar_t> environmentVariableValue;
        const DWORD kGetEnvironmentVariableResult = GetEnvironmentVariable(std::wstring(name).c_str(), environmentVariableValue, environmentVariableValue.Capacity());

        if ((kGetEnvironmentVariableResult >= environmentVariableValue.Capacity()) || (0 == kGetEnvironmentVariableResult))
            return std::nullopt;

        return std::wstring(environmentVariableValue);
    }

    /// Attempts to resolve a known path identifier to a string representation of its path.
    /// @param [in] knownFolder Known folder identifier.
    /// @return Resolved string, if resolution succeeded.
    static std::optional<std::wstring> GetKnownFolderPathString(const KNOWNFOLDERID& knownFolder)
    {
        std::optional<std::wstring> knownFolderPathString = std::nullopt;

        wchar_t* knownFolderPath = nullptr;
        const HRESULT kGetKnownFolderPathResult = SHGetKnownFolderPath(knownFolder, KF_FLAG_DEFAULT, NULL, &knownFolderPath);

        if (S_OK == kGetKnownFolderPathResult)
            knownFolderPathString = std::wstring(knownFolderPath);

        if (nullptr != knownFolderPath)
            CoTaskMemFree(knownFolderPath);

        return knownFolderPathString;
    }


    // -------- TEST CASES ------------------------------------------------- //

    // Verifies that an environment variable can be resolved correctly in the nominal case that the domain is explicitly specified.
    TEST_CASE(Resolver_SingleReference_EnvironmentVariable_Nominal)
    {
        constexpr std::wstring_view kEnvironmentVariableName = L"COMPUTERNAME";

        const std::optional<std::wstring> kExpectedResolveResult = GetEnvironmentVariableString(kEnvironmentVariableName);
        const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(std::wstring(Strings::kStrReferenceDomainEnvironmentVariable) + std::wstring(Strings::kStrDelimterReferenceDomainVsName) + std::wstring(kEnvironmentVariableName));

        TEST_ASSERT(true == kExpectedResolveResult.has_value());
        TEST_ASSERT(true == kActualResolveResult.HasValue());
        TEST_ASSERT(kActualResolveResult.Value() == kExpectedResolveResult.value());
    }

    // Verifies that an environment variable can be resolved correctly when the domain is not specified.
    // Environment variables are the default domain.
    TEST_CASE(Resolver_SingleReference_EnvironmentVariable_DefaultDomain)
    {
        constexpr std::wstring_view kEnvironmentVariableName = L"COMPUTERNAME";

        const std::optional<std::wstring> kExpectedResolveResult = GetEnvironmentVariableString(kEnvironmentVariableName);
        const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(kEnvironmentVariableName);

        TEST_ASSERT(true == kExpectedResolveResult.has_value());
        TEST_ASSERT(true == kActualResolveResult.HasValue());
        TEST_ASSERT(kActualResolveResult.Value() == kExpectedResolveResult.value());
    }

    // Verifies that an invalid environment variable fails to be resolved when the domain is explicitly specified.
    TEST_CASE(Resolver_SingleReference_EnvironmentVariable_Invalid)
    {
        constexpr std::wstring_view kEnvironmentVariableName = L"ASDF=GH=JKL;";

        const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(std::wstring(Strings::kStrReferenceDomainEnvironmentVariable) + std::wstring(Strings::kStrDelimterReferenceDomainVsName) + std::wstring(kEnvironmentVariableName));
        TEST_ASSERT(true == kActualResolveResult.HasError());
    }

    // Verifies that an invalid environment variable fails to be resolved when the domain is not explicitly specified.
    TEST_CASE(Resolver_SingleReference_EnvironmentVariable_InvalidDefaultDomain)
    {
        constexpr std::wstring_view kEnvironmentVariableName = L"ASDF=GH=JKL;";

        const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(kEnvironmentVariableName);
        TEST_ASSERT(true == kActualResolveResult.HasError());
    }

    // Verifies that known folder identifiers resolve correctly.
    // If the mapping is valid and results in a real path, the same should be true for reference resolution.
    // If not, then the reference resolution should also fail.
    TEST_CASE(Resolver_SingleReference_KnownFolderIdentifier_Nominal)
    {
        constexpr std::pair<std::wstring_view, const KNOWNFOLDERID*> kKnownFolderIdentifierRecords[] = {
            {L"AddNewPrograms", &FOLDERID_AddNewPrograms},
            {L"Desktop", &FOLDERID_Desktop},
            {L"Downloads", &FOLDERID_Downloads},
            {L"Fonts", &FOLDERID_Fonts},
            {L"HomeGroupCurrentUser", &FOLDERID_HomeGroupCurrentUser},
            {L"InternetCache", &FOLDERID_InternetCache},
            {L"NetworkFolder", &FOLDERID_NetworkFolder},
            {L"Pictures", &FOLDERID_Pictures},
            {L"Profile", &FOLDERID_Profile},
            {L"RecycleBinFolder", &FOLDERID_RecycleBinFolder},
            {L"RoamingAppData", &FOLDERID_RoamingAppData},
            {L"SavedGames", &FOLDERID_SavedGames},
            {L"Windows", &FOLDERID_Windows}
        };

        const std::wstring kTestInputPrefix = std::wstring(Strings::kStrReferenceDomainKnownFolderIdentifier) + std::wstring(Strings::kStrDelimterReferenceDomainVsName);

        for (const auto& kKnownFolderIdentifierRecord : kKnownFolderIdentifierRecords)
        {
            const std::wstring kKnownFolderInputString = kTestInputPrefix + std::wstring(kKnownFolderIdentifierRecord.first);
            const KNOWNFOLDERID& kKnownFolderIdentifier = *kKnownFolderIdentifierRecord.second;

            const std::optional<std::wstring> kExpectedResolveResult = GetKnownFolderPathString(kKnownFolderIdentifier);
            const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(kKnownFolderInputString);

            TEST_ASSERT(kActualResolveResult.HasValue() == kExpectedResolveResult.has_value());
            
            if (true == kExpectedResolveResult.has_value())
                TEST_ASSERT(kActualResolveResult.Value() == kExpectedResolveResult.value());
        }
    }

    // Verifies that invalid known folder identifiers fail to resolve.
    // Inputs are as above but with case modifications and leading or trailing whitespace.
    TEST_CASE(Resolver_SingleReference_KnownFolderIdentifier_Invalid)
    {
        constexpr std::pair<std::wstring_view, const KNOWNFOLDERID*> kKnownFolderIdentifierRecords[] = {
            {L"desktop", &FOLDERID_Desktop},
            {L"Downloads ", &FOLDERID_Downloads},
            {L"  Fonts  ", &FOLDERID_Fonts},
            {L" InternetCache", &FOLDERID_InternetCache},
            {L"\tWindows", &FOLDERID_Windows}
        };

        const std::wstring kTestInputPrefix = std::wstring(Strings::kStrReferenceDomainKnownFolderIdentifier) + std::wstring(Strings::kStrDelimterReferenceDomainVsName);

        for (const auto& kKnownFolderIdentifierRecord : kKnownFolderIdentifierRecords)
        {
            const std::wstring kKnownFolderInputString = kTestInputPrefix + std::wstring(kKnownFolderIdentifierRecord.first);

            const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(kKnownFolderInputString);
            TEST_ASSERT(true == kActualResolveResult.HasError());
        }
    }

    // Verifies that a configured definition can be resolved correctly in the nominal case of no embedded references.
    TEST_CASE(Resolver_SingleReference_ConfiguredDefinition_Nominal)
    {
        constexpr std::wstring_view kVariableName = L"W";
        constexpr std::wstring_view kVariableValue = L"This is the evaluated value of W.";

        SetConfigurationFileDefinitions({
            {kVariableName, kVariableValue}
        });

        const std::wstring_view kExpectedResolveResult = kVariableValue;
        const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(std::wstring(Strings::kStrReferenceDomainConfigDefinition) + std::wstring(Strings::kStrDelimterReferenceDomainVsName) + std::wstring(kVariableName));

        TEST_ASSERT(true == kActualResolveResult.HasValue());
        TEST_ASSERT(kActualResolveResult.Value() == kExpectedResolveResult);
    }

    // Verifies that a configured definition can be resolved correctly in the more complex case of embedded references.
    TEST_CASE(Resolver_SingleReference_ConfiguredDefinition_Embedded)
    {
        SetConfigurationFileDefinitions({
            {L"X", L"Value of X"},
            {L"Y", L"Value of Y incorporates value of X: (%CONF::X%)"},
            {L"Z", L"Value of Z incorporates value of Y: (%CONF::Y%)"}
        });

        const std::wstring_view kExpectedResolveResult = L"Value of Z incorporates value of Y: (Value of Y incorporates value of X: (Value of X))";
        const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(std::wstring(Strings::kStrReferenceDomainConfigDefinition) + std::wstring(Strings::kStrDelimterReferenceDomainVsName) + L"Z");

        TEST_ASSERT(true == kActualResolveResult.HasValue());
        TEST_ASSERT(kActualResolveResult.Value() == kExpectedResolveResult);
    }

    // Verifies that a configured definition fails to resolve when it references itself.
    TEST_CASE(Resolver_SingleReference_ConfiguredDefinition_EmbeddedCircularSingle)
    {
        constexpr std::wstring_view kVariableName = L"Invalid";
        constexpr std::wstring_view kVariableValue = L"This is the evaluated value of %CONF::Invalid%.";

        SetConfigurationFileDefinitions({
            {kVariableName, kVariableValue}
        });

        const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(std::wstring(Strings::kStrReferenceDomainConfigDefinition) + std::wstring(Strings::kStrDelimterReferenceDomainVsName) + std::wstring(kVariableName));
        TEST_ASSERT(true == kActualResolveResult.HasError());
    }

    // Verifies that a configured definition fails to resolve when there is a cycle across multiple references.
    TEST_CASE(Resolver_SingleReference_ConfiguredDefinition_EmbeddedCircularMultiple)
    {
        SetConfigurationFileDefinitions({
            {L"Invalid1", L"Value of %CONF::Invalid2%"},
            {L"Invalid2", L"Value of Invalid2 incorporates %CONF::Invalid3%"},
            {L"Invalid3", L"Value of Invalid3 incorporates %CONF::Invalid1%"}
        });

        const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(std::wstring(Strings::kStrReferenceDomainConfigDefinition) + std::wstring(Strings::kStrDelimterReferenceDomainVsName) + L"Invalid2");
        TEST_ASSERT(true == kActualResolveResult.HasError());
    }

    // Verifies that a configured definition referencing an unrecognized variable fails to be resolved.
    TEST_CASE(Resolver_SingleReference_ConfiguredDefinition_Invalid)
    {
        const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(std::wstring(Strings::kStrReferenceDomainConfigDefinition) + std::wstring(Strings::kStrDelimterReferenceDomainVsName) + L"UnknownVariable123456");
        TEST_ASSERT(true == kActualResolveResult.HasError());
    }

    // Verifies that valid references to built-in strings are resolved correctly.
    TEST_CASE(Resolver_SingleReference_Builtin_Nominal)
    {
        const std::pair<std::wstring_view, std::wstring_view> kBuiltinStringTestRecords[] = {
            {L"BUILTIN::ExecutableBaseName", Strings::kStrExecutableBaseName},
            {L"BUILTIN::PathwinderDirectoryName", Strings::kStrPathwinderDirectoryName},
        };

        for (const auto& kBuiltinStringTestRecord : kBuiltinStringTestRecords)
        {
            const std::wstring_view kExpectedResolveResult = kBuiltinStringTestRecord.second;
            const ResolvedStringOrError kActualResolveResult = ResolveSingleReference(kBuiltinStringTestRecord.first);

            TEST_ASSERT(true == kActualResolveResult.HasValue());
            TEST_ASSERT(kActualResolveResult.Value() == kExpectedResolveResult);
        }
    }

    // Verifies that invalid inputs for single-reference resolution cause the resolution to fail.
    // This could be unrecognized domains or unparseable strings.
    TEST_CASE(Resolver_SingleReference_Invalid)
    {
        constexpr std::wstring_view kInvalidInputStrings[] = {
            L"INVALIDDOMAIN::SomeVariable",
            L"ENV::COMPUTERNAME::",
            L"ENV::COMPUTERNAME::extrastuff",
            L"::ENV::COMPUTERNAME",
            L"::",
            L""
        };

        for (const auto& kInvalidInputString : kInvalidInputStrings)
            TEST_ASSERT(true == ResolveSingleReference(kInvalidInputString).HasError());
    }

    // Verifies that valid inputs for all-reference resolution produce the correct successful resolution results.
    TEST_CASE(Resolver_AllReferences_Valid)
    {
        SetConfigurationFileDefinitions({
            {L"BaseDir", L"%FOLDERID::SavedGames%"},
            {L"PercentageComplete", L"56.789"}
        });

        const std::pair<std::wstring_view, std::wstring> kAllReferenceTestRecords[] = {
            {
                L"Selected base directory: %CONF::BaseDir%",
                std::wstring(L"Selected base directory: ") + GetKnownFolderPathString(FOLDERID_SavedGames).value()
            },
            {
                L"You are %CONF::PercentageComplete%%% done!",
                L"You are 56.789% done!"
            },
            {
                L"System is %CONF::PercentageComplete%%% ready to provide your files in %CONF::BaseDir%.",
                std::wstring(L"System is 56.789% ready to provide your files in ") + GetKnownFolderPathString(FOLDERID_SavedGames).value() + std::wstring(L".")
            },
            {
                L"%%%%%%::%%%%::::%%%%",
                L"%%%::%%::::%%"
            }
        };

        for (const auto& kAllReferenceTestRecord : kAllReferenceTestRecords)
        {
            const std::wstring_view kExpectedResolveResult = kAllReferenceTestRecord.second;
            const ResolvedStringOrError kActualResolveResult = ResolveAllReferences(kAllReferenceTestRecord.first);

            TEST_ASSERT(true == kActualResolveResult.HasValue());
            TEST_ASSERT(kActualResolveResult.Value() == kExpectedResolveResult);
        }
    }

    // Verifies that invalid inputs for all-reference resolution cause the resolution to fail.
    TEST_CASE(Resolver_AllReferences_Invalid)
    {
        SetConfigurationFileDefinitions({
            {L"BaseDir", L"%FOLDERID::TotallyUnrecognizedFolderIdentifier%"}
        });

        constexpr std::wstring_view kInvalidInputStrings[] = {
            L"Using computer %COMPUTERNAME% as user %USERNAME%. There is an extra % sign at the end that is not matched.",
            L"Using computer %COMPUTERNAME% as user %CONF::InvalidReference%.",
            L"Selected base directory: %CONF::BaseDir%",
            L"%%%"
        };

        for (const auto& kInvalidInputString : kInvalidInputStrings)
            TEST_ASSERT(true == ResolveAllReferences(kInvalidInputString).HasError());
    }
}
