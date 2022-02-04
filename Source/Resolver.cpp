/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022
 *************************************************************************//**
 * @file Resolver.cpp
 *   Implementation of named reference resolution functionality.
 *****************************************************************************/

#include "Resolver.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#ifndef PATHWINDER_SKIP_CONFIG
#include "Configuration.h"
#include "Globals.h"
#endif

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>


namespace Pathwinder
{
    namespace Resolver
    {
        // -------- INTERNAL CONSTANTS ------------------------------------- //

        /// Default reference domain to use when none are specified.
        static constexpr std::wstring_view kReferenceDefaultDomain = Strings::kStrReferenceDomainEnvironmentVariable;


        // -------- INTERNAL TYPES ----------------------------------------- //

        /// Type for all functions that attempt to resolve a specific type of reference.
        typedef ResolvedStringOrError(*TResolveReferenceFunc)(std::wstring_view);


        // -------- INTERNAL FUNCTIONS ------------------------------------- //

        /// Resolves an environment variable.
        /// @param [in] name Name of the environment variable to resolve.
        /// @return Resolved value on success, error message on failure.
        static ResolvedStringOrError ResolveEnvironmentVariable(std::wstring_view name)
        {
            TemporaryBuffer<wchar_t> environmentVariableValue;
            const DWORD kGetEnvironmentVariableResult = GetEnvironmentVariable(std::wstring(name).c_str(), environmentVariableValue, environmentVariableValue.Capacity());

            if (kGetEnvironmentVariableResult >= environmentVariableValue.Capacity())
                return ResolvedStringOrError::MakeError(Strings::FormatString(L"%s: Failed to obtain environment variable value: Value is too long", std::wstring(name).c_str()));
            else if (0 == kGetEnvironmentVariableResult)
                return ResolvedStringOrError::MakeError(Strings::FormatString(L"%s: Failed to obtain environment variable value: %s", std::wstring(name).c_str(), (wchar_t*)Strings::SystemErrorCodeString(GetLastError())));

            return ResolvedStringOrError::MakeValue(environmentVariableValue);
        }

        /// Resolves a known folder identifier.
        /// Known folder identifier names are documented at https://docs.microsoft.com/en-us/windows/win32/shell/knownfolderid and should be passed without the FOLDERID prefix.
        /// @param [in] name Name of the known folder identifier to resolve.
        /// @return Resolved value on success, error message on failure.
        static ResolvedStringOrError ResolveKnownFolderIdentifier(std::wstring_view name)
        {
            static const std::unordered_map<std::wstring_view, const KNOWNFOLDERID*> kKnownFolderIdentifiers = {
                {L"NetworkFolder", &FOLDERID_NetworkFolder},
                {L"ComputerFolder", &FOLDERID_ComputerFolder},
                {L"InternetFolder", &FOLDERID_InternetFolder},
                {L"ControlPanelFolder", &FOLDERID_ControlPanelFolder},
                {L"PrintersFolder", &FOLDERID_PrintersFolder},
                {L"SyncManagerFolder", &FOLDERID_SyncManagerFolder},
                {L"SyncSetupFolder", &FOLDERID_SyncSetupFolder},
                {L"ConflictFolder", &FOLDERID_ConflictFolder},
                {L"SyncResultsFolder", &FOLDERID_SyncResultsFolder},
                {L"RecycleBinFolder", &FOLDERID_RecycleBinFolder},
                {L"ConnectionsFolder", &FOLDERID_ConnectionsFolder},
                {L"Fonts", &FOLDERID_Fonts},
                {L"Desktop", &FOLDERID_Desktop},
                {L"Startup", &FOLDERID_Startup},
                {L"Programs", &FOLDERID_Programs},
                {L"StartMenu", &FOLDERID_StartMenu},
                {L"Recent", &FOLDERID_Recent},
                {L"SendTo", &FOLDERID_SendTo},
                {L"Documents", &FOLDERID_Documents},
                {L"Favorites", &FOLDERID_Favorites},
                {L"NetHood", &FOLDERID_NetHood},
                {L"PrintHood", &FOLDERID_PrintHood},
                {L"Templates", &FOLDERID_Templates},
                {L"CommonStartup", &FOLDERID_CommonStartup},
                {L"CommonPrograms", &FOLDERID_CommonPrograms},
                {L"CommonStartMenu", &FOLDERID_CommonStartMenu},
                {L"PublicDesktop", &FOLDERID_PublicDesktop},
                {L"ProgramData", &FOLDERID_ProgramData},
                {L"CommonTemplates", &FOLDERID_CommonTemplates},
                {L"PublicDocuments", &FOLDERID_PublicDocuments},
                {L"RoamingAppData", &FOLDERID_RoamingAppData},
                {L"LocalAppData", &FOLDERID_LocalAppData},
                {L"LocalAppDataLow", &FOLDERID_LocalAppDataLow},
                {L"InternetCache", &FOLDERID_InternetCache},
                {L"Cookies", &FOLDERID_Cookies},
                {L"History", &FOLDERID_History},
                {L"System", &FOLDERID_System},
                {L"SystemX86", &FOLDERID_SystemX86},
                {L"Windows", &FOLDERID_Windows},
                {L"Profile", &FOLDERID_Profile},
                {L"Pictures", &FOLDERID_Pictures},
                {L"ProgramFilesX86", &FOLDERID_ProgramFilesX86},
                {L"ProgramFilesCommonX86", &FOLDERID_ProgramFilesCommonX86},
                {L"ProgramFilesX64", &FOLDERID_ProgramFilesX64},
                {L"ProgramFilesCommonX64", &FOLDERID_ProgramFilesCommonX64},
                {L"ProgramFiles", &FOLDERID_ProgramFiles},
                {L"ProgramFilesCommon", &FOLDERID_ProgramFilesCommon},
                {L"UserProgramFiles", &FOLDERID_UserProgramFiles},
                {L"UserProgramFilesCommon", &FOLDERID_UserProgramFilesCommon},
                {L"AdminTools", &FOLDERID_AdminTools},
                {L"CommonAdminTools", &FOLDERID_CommonAdminTools},
                {L"Music", &FOLDERID_Music},
                {L"Videos", &FOLDERID_Videos},
                {L"Ringtones", &FOLDERID_Ringtones},
                {L"PublicPictures", &FOLDERID_PublicPictures},
                {L"PublicMusic", &FOLDERID_PublicMusic},
                {L"PublicVideos", &FOLDERID_PublicVideos},
                {L"PublicRingtones", &FOLDERID_PublicRingtones},
                {L"ResourceDir", &FOLDERID_ResourceDir},
                {L"LocalizedResourcesDir", &FOLDERID_LocalizedResourcesDir},
                {L"CommonOEMLinks", &FOLDERID_CommonOEMLinks},
                {L"CDBurning", &FOLDERID_CDBurning},
                {L"UserProfiles", &FOLDERID_UserProfiles},
                {L"Playlists", &FOLDERID_Playlists},
                {L"SamplePlaylists", &FOLDERID_SamplePlaylists},
                {L"SampleMusic", &FOLDERID_SampleMusic},
                {L"SamplePictures", &FOLDERID_SamplePictures},
                {L"SampleVideos", &FOLDERID_SampleVideos},
                {L"PhotoAlbums", &FOLDERID_PhotoAlbums},
                {L"Public", &FOLDERID_Public},
                {L"ChangeRemovePrograms", &FOLDERID_ChangeRemovePrograms},
                {L"AppUpdates", &FOLDERID_AppUpdates},
                {L"AddNewPrograms", &FOLDERID_AddNewPrograms},
                {L"Downloads", &FOLDERID_Downloads},
                {L"PublicDownloads", &FOLDERID_PublicDownloads},
                {L"SavedSearches", &FOLDERID_SavedSearches},
                {L"QuickLaunch", &FOLDERID_QuickLaunch},
                {L"Contacts", &FOLDERID_Contacts},
                {L"SidebarParts", &FOLDERID_SidebarParts},
                {L"SidebarDefaultParts", &FOLDERID_SidebarDefaultParts},
                {L"PublicGameTasks", &FOLDERID_PublicGameTasks},
                {L"GameTasks", &FOLDERID_GameTasks},
                {L"SavedGames", &FOLDERID_SavedGames},
                {L"Games", &FOLDERID_Games},
                {L"SEARCH_MAPI", &FOLDERID_SEARCH_MAPI},
                {L"SEARCH_CSC", &FOLDERID_SEARCH_CSC},
                {L"Links", &FOLDERID_Links},
                {L"UsersFiles", &FOLDERID_UsersFiles},
                {L"UsersLibraries", &FOLDERID_UsersLibraries},
                {L"SearchHome", &FOLDERID_SearchHome},
                {L"OriginalImages", &FOLDERID_OriginalImages},
                {L"DocumentsLibrary", &FOLDERID_DocumentsLibrary},
                {L"MusicLibrary", &FOLDERID_MusicLibrary},
                {L"PicturesLibrary", &FOLDERID_PicturesLibrary},
                {L"VideosLibrary", &FOLDERID_VideosLibrary},
                {L"RecordedTVLibrary", &FOLDERID_RecordedTVLibrary},
                {L"HomeGroup", &FOLDERID_HomeGroup},
                {L"HomeGroupCurrentUser", &FOLDERID_HomeGroupCurrentUser},
                {L"DeviceMetadataStore", &FOLDERID_DeviceMetadataStore},
                {L"Libraries", &FOLDERID_Libraries},
                {L"PublicLibraries", &FOLDERID_PublicLibraries},
                {L"UserPinned", &FOLDERID_UserPinned},
                {L"ImplicitAppShortcuts", &FOLDERID_ImplicitAppShortcuts},
                {L"AccountPictures", &FOLDERID_AccountPictures},
                {L"PublicUserTiles", &FOLDERID_PublicUserTiles},
                {L"AppsFolder", &FOLDERID_AppsFolder},
                {L"StartMenuAllPrograms", &FOLDERID_StartMenuAllPrograms},
                {L"CommonStartMenuPlaces", &FOLDERID_CommonStartMenuPlaces},
                {L"ApplicationShortcuts", &FOLDERID_ApplicationShortcuts},
                {L"RoamingTiles", &FOLDERID_RoamingTiles},
                {L"RoamedTileImages", &FOLDERID_RoamedTileImages},
                {L"Screenshots", &FOLDERID_Screenshots},
                {L"CameraRoll", &FOLDERID_CameraRoll},
                {L"SkyDrive", &FOLDERID_SkyDrive},
                {L"OneDrive", &FOLDERID_OneDrive},
                {L"SkyDriveDocuments", &FOLDERID_SkyDriveDocuments},
                {L"SkyDrivePictures", &FOLDERID_SkyDrivePictures},
                {L"SkyDriveMusic", &FOLDERID_SkyDriveMusic},
                {L"SkyDriveCameraRoll", &FOLDERID_SkyDriveCameraRoll},
                {L"SearchHistory", &FOLDERID_SearchHistory},
                {L"SearchTemplates", &FOLDERID_SearchTemplates},
                {L"CameraRollLibrary", &FOLDERID_CameraRollLibrary},
                {L"SavedPictures", &FOLDERID_SavedPictures},
                {L"SavedPicturesLibrary", &FOLDERID_SavedPicturesLibrary},
                {L"RetailDemo", &FOLDERID_RetailDemo},
                {L"Device", &FOLDERID_Device},
                {L"DevelopmentFiles", &FOLDERID_DevelopmentFiles},
                {L"Objects3D", &FOLDERID_Objects3D},
                {L"AppCaptures", &FOLDERID_AppCaptures},
                {L"LocalDocuments", &FOLDERID_LocalDocuments},
                {L"LocalPictures", &FOLDERID_LocalPictures},
                {L"LocalVideos", &FOLDERID_LocalVideos},
                {L"LocalMusic", &FOLDERID_LocalMusic},
                {L"LocalDownloads", &FOLDERID_LocalDownloads},
                {L"RecordedCalls", &FOLDERID_RecordedCalls},
                {L"AllAppMods", &FOLDERID_AllAppMods},
                {L"CurrentAppMods", &FOLDERID_CurrentAppMods},
                {L"AppDataDesktop", &FOLDERID_AppDataDesktop},
                {L"AppDataDocuments", &FOLDERID_AppDataDocuments},
                {L"AppDataFavorites", &FOLDERID_AppDataFavorites},
                {L"AppDataProgramData", &FOLDERID_AppDataProgramData},
                {L"LocalStorage", &FOLDERID_LocalStorage}
            };

            const auto knownFolderIter = kKnownFolderIdentifiers.find(name);
            if (kKnownFolderIdentifiers.cend() == knownFolderIter)
                return ResolvedStringOrError::MakeError(Strings::FormatString(L"%s: Unrecognized known folder identifier", std::wstring(name).c_str()));

            wchar_t* knownFolderPath = nullptr;
            const HRESULT kGetKnownFolderPathResult = SHGetKnownFolderPath(*knownFolderIter->second, KF_FLAG_DEFAULT, NULL, &knownFolderPath);

            if (S_OK != kGetKnownFolderPathResult)
            {
                if (nullptr != knownFolderPath)
                    CoTaskMemFree(knownFolderPath);

                return ResolvedStringOrError::MakeError(Strings::FormatString(L"%s: Failed to obtain known folder path (0x%08lx)", std::wstring(name).c_str(), (unsigned long)kGetKnownFolderPathResult));
            }
            else
            {
                std::wstring knownFolderPathString(knownFolderPath);

                if (nullptr != knownFolderPath)
                    CoTaskMemFree(knownFolderPath);

                return ResolvedStringOrError::MakeValue(std::move(knownFolderPathString));
            }
        }

#ifndef PATHWINDER_SKIP_CONFIG
        static ResolvedStringOrError ResolveVariable(std::wstring_view name)
        {
            static std::unordered_set<std::wstring_view> resolutionsInProgress;

            const Configuration::ConfigurationData& configData = Globals::GetConfigurationData();

            if (false == configData.SectionNamePairExists(Strings::kStrConfigurationSectionVariables, name))
                return ResolvedStringOrError::MakeError(Strings::FormatString(L"%s: Unrecognized variable name", std::wstring(name).c_str()));

            std::pair resolutionInProgress = resolutionsInProgress.emplace(name);
            if (false == resolutionInProgress.second)
                return ResolvedStringOrError::MakeError(Strings::FormatString(L"%s: Circular variable reference", std::wstring(name).c_str()));

            ResolvedStringOrError resolvedVariable = ResolveAllReferences(configData[Strings::kStrConfigurationSectionVariables][name].FirstValue().GetStringValue());
            resolutionsInProgress.erase(resolutionInProgress.first);

            return resolvedVariable;
        }
#endif

        // -------- FUNCTIONS ---------------------------------------------- //
        // See "Resolver.h" for documentation.

        ResolvedStringOrError ResolveSingleReference(std::wstring_view str)
        {
            static const std::unordered_map<std::wstring_view, TResolveReferenceFunc> kResolversByDomain = {
                {L"", &ResolveEnvironmentVariable},
                {Strings::kStrReferenceDomainEnvironmentVariable, &ResolveEnvironmentVariable},
                {Strings::kStrReferenceDomainKnownFolderIdentifier, &ResolveKnownFolderIdentifier},
#ifndef PATHWINDER_SKIP_CONFIG
                {Strings::kStrReferenceDomainVariable, &ResolveVariable}
#endif
            };

            static std::map<std::wstring, std::wstring, std::less<void>> previouslyResolvedReferences;

            const auto previouslyResolvedIter = previouslyResolvedReferences.find(str);
            if (previouslyResolvedReferences.cend() != previouslyResolvedIter)
                return ResolvedStringOrError::MakeValue(previouslyResolvedIter->second);

            TemporaryVector<std::wstring_view> strParts = Strings::SplitString(str, Strings::kStrDelimterReferenceDomainVsName);
            std::wstring_view strPartReferenceDomain;
            std::wstring_view strPartReferenceName;

            switch (strParts.Size())
            {
            case 1:
                strPartReferenceDomain = kReferenceDefaultDomain;
                strPartReferenceName = strParts[0];
                break;

            case 2:
                strPartReferenceDomain = strParts[0];
                strPartReferenceName = strParts[1];
                break;

            default:
                return ResolvedStringOrError::MakeError(Strings::FormatString(L"%s: Unparseable reference", std::wstring(str).c_str()));
            }

            const auto resolverByDomainIter = kResolversByDomain.find(strPartReferenceDomain);
            if (kResolversByDomain.cend() == resolverByDomainIter)
                return ResolvedStringOrError::MakeError(Strings::FormatString(L"%s: Unrecognized reference domain", std::wstring(str).c_str()));

            ResolvedStringOrError resolveResult = resolverByDomainIter->second(strPartReferenceName);

            if (resolveResult.HasValue())
                previouslyResolvedReferences.emplace(str, resolveResult.Value());

            return resolveResult;
        }

        // --------

        ResolvedStringOrError ResolveAllReferences(std::wstring_view str)
        {
            std::wstringstream resolvedStr;
            TemporaryVector<std::wstring_view> strParts = Strings::SplitString(str, Strings::kStrDelimterReferenceDomainVsName);

            if (1 != (strParts.Size() % 2))
                return ResolvedStringOrError::MakeError(Strings::FormatString(L"%s: Unmatched '%s' delimiters", std::wstring(str).c_str(), Strings::kStrDelimiterReferenceVsLiteral.data()));

            resolvedStr << strParts[0];

            for (size_t i = 1; i < strParts.Size(); i += 2)
            {
                resolvedStr << strParts[i];

                if (true == strParts[i + 1].empty())
                {
                    resolvedStr << Strings::kStrDelimiterReferenceVsLiteral;
                }
                else
                {
                    ResolvedStringOrError resolvedReferenceResult = ResolveSingleReference(strParts[i + 1]);
                    if (true == resolvedReferenceResult.HasError())
                        return ResolvedStringOrError::MakeError(Strings::FormatString(L"%s: Failed to resolve reference: %s", std::wstring(str).c_str(), resolvedReferenceResult.Error().c_str()));

                    resolvedStr << resolvedReferenceResult.Value();
                }
            }

            return ResolvedStringOrError::MakeValue(resolvedStr.str());
        }
    }
}
