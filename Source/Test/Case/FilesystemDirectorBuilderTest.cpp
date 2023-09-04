/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file FilesystemDirectorBuilderTest.cpp
 *   Unit tests for all functionality related to building filesystem director objects and
 *   ensuring consistency between filesystem rules.
 **************************************************************************************************/

#include "TestCase.h"

#include "FilesystemDirectorBuilder.h"

#include <windows.h>

#include <string_view>

#include "Configuration.h"
#include "FilesystemOperations.h"
#include "MockFilesystemOperations.h"
#include "TemporaryBuffer.h"
#include "ValueOrError.h"

namespace PathwinderTest
{
    using namespace ::Pathwinder;

    // Verifies that valid strings for identifying origin and target directories are accepted as
    // such.
    TEST_CASE(FilesystemDirectorBuilder_IsValidDirectoryString_Valid)
    {
        constexpr std::wstring_view kDirectoryStrings[] = {
            L"C:",
            L"C:\\Directory",
            L"C:\\Program Files (x86)\\Games\\Some Game With A Title",
        };

        for (const auto& kDirectoryString : kDirectoryStrings)
            TEST_ASSERT(
                true == FilesystemDirectorBuilder::IsValidDirectoryString(kDirectoryString)
            );
    }

    // Verifies that invalid strings for identifying origin and target directories are rejected.
    TEST_CASE(FilesystemDirectorBuilder_IsValidDirectoryString_Invalid)
    {
        constexpr std::wstring_view kDirectoryStrings[] = {
            L"",
            L"C:\\Program Files <x86>\\Games\\Some Game With A Title",
            L"\"C:\\Program Files (x86)\\Games\\Some Game With A Title\"",
            L"C:\\Program Files*",
            L"C:\\Program Files (???)",
            L"C:\\Program Files\\*",
            L"C:\\Program Files\t(x86)\\Games\\Some Game With A Title",
            L"C:\\Program Files\n(x86)\\Games\\Some Game With A Title",
            L"C:\\Program Files\b(x86)\\Games\\Some Game With A Title",
            L"C:\\Program Files (x86)\\Games\\Some Game With A Title\\..",
            L"C:\\Program Files (x86)\\Games\\Some Game With A Title\\.",
            L"C:\\Somedir\\..\\Somedir",
            L"C:\\.\\.\\Somedir",
            L"\\\\sharepath\\shared folder$\\another shared folder",
            L"AB\\Test",
            L"AB:\\TestDir\\File.txt",
            L"1:\\TestDir\\File.txt",
            L"\\??\\C:",
            L"\\\\.\\C:\\Directory",
            L"\\\\?\\C:\\Program Files (x86)\\Games\\Some Game With A Title",
        };

        for (const auto& kDirectoryString : kDirectoryStrings)
            TEST_ASSERT(
                false == FilesystemDirectorBuilder::IsValidDirectoryString(kDirectoryString)
            );
    }

    // Verifies that valid strings for identifying file patterns within an origin or target
    // directory are accepted as such.
    TEST_CASE(FilesystemDirectorBuilder_IsValidFilePatternString_Valid)
    {
        constexpr std::wstring_view kFilePatternStrings[] = {
            L"*",
            L"?",
            L"***????",
            L"data???.sav",
            L"*.bin",
            L".*",
            L"data???.MyGame.MyPublisher.sav"};

        for (const auto& kFilePatternString : kFilePatternStrings)
            TEST_ASSERT(
                true == FilesystemDirectorBuilder::IsValidFilePatternString(kFilePatternString)
            );
    }

    // Verifies that invalid strings for identifying file patterns within an origin or target
    // directory are rejected.
    TEST_CASE(FilesystemDirectorBuilder_IsValidFilePatternString_Invalid)
    {
        constexpr std::wstring_view kFilePatternStrings[] = {
            L"", L"data000.sav|data001.sav", L"\\*.bin", L"C:*.bin"};

        for (const auto& kFilePatternString : kFilePatternStrings)
            TEST_ASSERT(
                false == FilesystemDirectorBuilder::IsValidFilePatternString(kFilePatternString)
            );
    }

    // Verifies the nominal situation of creating rules that do not overlap and contain no file
    // patterns. Additionally verifies the resulting contents of the filesystem rules that are
    // created. This test exercises the various different redirection modes that are supported.
    TEST_CASE(FilesystemDirectorBuilder_AddRule_Success_Nominal)
    {
        FilesystemDirectorBuilder directorBuilder;

        auto maybeConfigRule1 = directorBuilder.AddRule(
            L"1", L"C:\\OriginDir1", L"C:\\TargetDir1", {}, FilesystemRule::ERedirectMode::Simple
        );
        TEST_ASSERT(maybeConfigRule1.HasValue());
        TEST_ASSERT(
            maybeConfigRule1.Value()->GetRedirectMode() == FilesystemRule::ERedirectMode::Simple
        );
        TEST_ASSERT(maybeConfigRule1.Value()->GetOriginDirectoryFullPath() == L"C:\\OriginDir1");
        TEST_ASSERT(maybeConfigRule1.Value()->GetTargetDirectoryFullPath() == L"C:\\TargetDir1");

        auto maybeConfigRule2 = directorBuilder.AddRule(
            L"2", L"C:\\OriginDir2", L"C:\\TargetDir2", {}, FilesystemRule::ERedirectMode::Overlay
        );
        TEST_ASSERT(maybeConfigRule2.HasValue());
        TEST_ASSERT(
            maybeConfigRule2.Value()->GetRedirectMode() == FilesystemRule::ERedirectMode::Overlay
        );
        TEST_ASSERT(maybeConfigRule2.Value()->GetOriginDirectoryFullPath() == L"C:\\OriginDir2");
        TEST_ASSERT(maybeConfigRule2.Value()->GetTargetDirectoryFullPath() == L"C:\\TargetDir2");
    }

    // Verifies that non-overlapping filesystem rules can be created with file patterns.
    // Additionally verifies the resulting contents, including some file pattern checks, of the
    // filesystem rules that are created.
    TEST_CASE(FilesystemDirectorBuilder_AddRule_Success_WithFilePatterns)
    {
        FilesystemDirectorBuilder directorBuilder;

        auto maybeConfigRule1 = directorBuilder.AddRule(
            L"1", L"C:\\OriginDir1", L"C:\\TargetDir1", {L"file*.txt", L"*.bin"}
        );
        TEST_ASSERT(maybeConfigRule1.HasValue());
        TEST_ASSERT(maybeConfigRule1.Value()->GetOriginDirectoryFullPath() == L"C:\\OriginDir1");
        TEST_ASSERT(maybeConfigRule1.Value()->GetTargetDirectoryFullPath() == L"C:\\TargetDir1");
        TEST_ASSERT(true == maybeConfigRule1.Value()->FileNameMatchesAnyPattern(L"file1.txt"));
        TEST_ASSERT(false == maybeConfigRule1.Value()->FileNameMatchesAnyPattern(L"asdf.txt"));

        auto maybeConfigRule2 = directorBuilder.AddRule(
            L"2", L"C:\\OriginDir2", L"C:\\TargetDir2", {L"log*", L"file???.dat"}
        );
        TEST_ASSERT(maybeConfigRule2.HasValue());
        TEST_ASSERT(maybeConfigRule2.Value()->GetOriginDirectoryFullPath() == L"C:\\OriginDir2");
        TEST_ASSERT(maybeConfigRule2.Value()->GetTargetDirectoryFullPath() == L"C:\\TargetDir2");
        TEST_ASSERT(true == maybeConfigRule2.Value()->FileNameMatchesAnyPattern(L"fileasd.dat"));
        TEST_ASSERT(false == maybeConfigRule2.Value()->FileNameMatchesAnyPattern(L"asdf.txt"));
    }

    // Verifies that non-overlapping filesystem rules can be created but one of the origin
    // directories is a subdirectory of the other. Three rules are used here with the mid-level rule
    // created first to verify that order does not matter.
    TEST_CASE(FilesystemDirectorBuilder_AddRule_Success_OriginIsSubdir)
    {
        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(
            directorBuilder.AddRule(L"2", L"C:\\Level1\\Level2", L"C:\\TargetDir2").HasValue()
        );
        TEST_ASSERT(directorBuilder.AddRule(L"1", L"C:\\Level1", L"C:\\TargetDir1").HasValue());
        TEST_ASSERT(directorBuilder.AddRule(L"3", L"C:\\Level1\\Level2\\Level3", L"C:\\TargetDir3")
                        .HasValue());
    }

    // Verifies that rule creation fails when multiple rules have the same name.
    TEST_CASE(FilesystemDirectorBuilder_AddRule_Failure_DuplicateRuleName)
    {
        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(directorBuilder.AddRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir1").HasValue());
        TEST_ASSERT(directorBuilder.AddRule(L"1", L"C:\\OriginDir2", L"C:\\TargetDir2").HasError());
    }

    // Verifies that rule creation fails if either the origin directory or the target directory is a
    // filesystem root.
    TEST_CASE(FilesystemDirectorBuilder_AddRule_Failure_FilesystemRoot)
    {
        FilesystemDirectorBuilder directorBuilder;

        ValueOrError<const FilesystemRule*, TemporaryString> addRuleResults[] = {
            directorBuilder.AddRule(L"1", L"C:\\", L"D:\\RedirectFromC"),
            directorBuilder.AddRule(L"2", L"C:\\RedirectToD", L"D:\\"),
            directorBuilder.AddRule(L"3", L"C:\\", L"D:\\")};

        for (auto& addRuleResult : addRuleResults)
        {
            TEST_ASSERT(addRuleResult.HasError());
            TEST_ASSERT(addRuleResult.Error().AsStringView().contains(L"filesystem root"));
        }
    }

    // Verifies that rule creation fails if the origin directory is the same as another rule's
    // origin or target directory.
    TEST_CASE(FilesystemDirectorBuilder_AddRule_Failure_OverlappingOrigin)
    {
        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(directorBuilder.AddRule(L"1", L"C:\\OriginDir", L"C:\\TargetDir1").HasValue());
        TEST_ASSERT(directorBuilder.AddRule(L"2", L"C:\\OriginDir", L"C:\\TargetDir2").HasError());
        TEST_ASSERT(directorBuilder.AddRule(L"3", L"C:\\OriginDir3", L"C:\\OriginDir").HasError());
    }

    // Verifies that rule creation fails if the origin directory is the same as another rule's
    // origin or target directory. This variation exercises case insensitivity by changing the case
    // of the origin directory used in various situations.
    TEST_CASE(FilesystemDirectorBuilder_AddRule_Failure_OverlappingOriginDifferentCase)
    {
        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(directorBuilder.AddRule(L"1", L"C:\\OriginDir", L"C:\\TargetDir1").HasValue());
        TEST_ASSERT(directorBuilder.AddRule(L"2", L"C:\\ORIGINDIR", L"C:\\TargetDir2").HasError());
        TEST_ASSERT(directorBuilder.AddRule(L"3", L"C:\\OriginDir3", L"C:\\origindir").HasError());
    }

    // Verifies that rule creation fails if the target directory is the same as another rule's
    // origin directory.
    TEST_CASE(FilesystemDirectorBuilder_AddRule_Failure_OverlappingTarget)
    {
        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(directorBuilder.AddRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir").HasValue());
        TEST_ASSERT(directorBuilder.AddRule(L"2", L"C:\\OriginDir2", L"C:\\OriginDir1").HasError());
    }

    // Verifies that rule creation fails if the target directory is the same as another rule's
    // origin directory. This variation exercises case insensitivy by changing the case of the
    // overlapping target directory.
    TEST_CASE(FilesystemDirectorBuilder_AddRule_Failure_OverlappingTargetDifferentCase)
    {
        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(directorBuilder.AddRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir").HasValue());
        TEST_ASSERT(directorBuilder.AddRule(L"2", L"C:\\OriginDir2", L"C:\\ORIGINDIR1").HasError());
    }

    // Verifies the nominal situation of creating rules that do not overlap and contain no file
    // patterns, but from a configuration data section. Additionally verifies the resulting contents
    // of the filesystem rules that are created. This test exercises the various different
    // redirection modes that are supported.
    TEST_CASE(FilesystemDirectorBuilder_AddRuleFromConfigurationSection_Success_Nominal)
    {
        Configuration::Section configSection1 = {
            {L"OriginDirectory", L"C:\\OriginDir1"},
            {L"TargetDirectory", L"C:\\TargetDir1"},
            {L"RedirectMode", L"Simple"}};

        Configuration::Section configSection2 = {
            {L"OriginDirectory", L"C:\\OriginDir2"},
            {L"TargetDirectory", L"C:\\TargetDir2"},
            {L"RedirectMode", L"Overlay"}};

        FilesystemDirectorBuilder directorBuilder;

        auto maybeConfigRule1 =
            directorBuilder.AddRuleFromConfigurationSection(L"1", configSection1);
        TEST_ASSERT(maybeConfigRule1.HasValue());
        TEST_ASSERT(
            maybeConfigRule1.Value()->GetRedirectMode() == FilesystemRule::ERedirectMode::Simple
        );
        TEST_ASSERT(maybeConfigRule1.Value()->GetOriginDirectoryFullPath() == L"C:\\OriginDir1");
        TEST_ASSERT(maybeConfigRule1.Value()->GetTargetDirectoryFullPath() == L"C:\\TargetDir1");

        auto maybeConfigRule2 =
            directorBuilder.AddRuleFromConfigurationSection(L"2", configSection2);
        TEST_ASSERT(maybeConfigRule2.HasValue());
        TEST_ASSERT(
            maybeConfigRule2.Value()->GetRedirectMode() == FilesystemRule::ERedirectMode::Overlay
        );
        TEST_ASSERT(maybeConfigRule2.Value()->GetOriginDirectoryFullPath() == L"C:\\OriginDir2");
        TEST_ASSERT(maybeConfigRule2.Value()->GetTargetDirectoryFullPath() == L"C:\\TargetDir2");
    }

    // Verifies that non-overlapping filesystem rules can be created with file patterns.
    // Additionally verifies the resulting contents, including some file pattern checks, of the
    // filesystem rules that are created.
    TEST_CASE(FilesystemDirectorBuilder_AddRuleFromConfigurationSection_Success_WithFilePatterns)
    {
        Configuration::Section configSection1 = {
            {L"OriginDirectory", L"C:\\OriginDir1"},
            {L"TargetDirectory", L"C:\\TargetDir1"},
            {L"FilePattern", {L"file*.txt", L"*.bin"}}};

        Configuration::Section configSection2 = {
            {L"OriginDirectory", L"C:\\OriginDir2"},
            {L"TargetDirectory", L"C:\\TargetDir2"},
            {L"FilePattern", {L"log*", L"file???.dat"}}};

        FilesystemDirectorBuilder directorBuilder;

        auto maybeConfigRule1 =
            directorBuilder.AddRuleFromConfigurationSection(L"1", configSection1);
        TEST_ASSERT(maybeConfigRule1.HasValue());
        TEST_ASSERT(maybeConfigRule1.Value()->GetOriginDirectoryFullPath() == L"C:\\OriginDir1");
        TEST_ASSERT(maybeConfigRule1.Value()->GetTargetDirectoryFullPath() == L"C:\\TargetDir1");
        TEST_ASSERT(true == maybeConfigRule1.Value()->FileNameMatchesAnyPattern(L"file1.txt"));
        TEST_ASSERT(false == maybeConfigRule1.Value()->FileNameMatchesAnyPattern(L"asdf.txt"));

        auto maybeConfigRule2 =
            directorBuilder.AddRuleFromConfigurationSection(L"2", configSection2);
        TEST_ASSERT(maybeConfigRule2.HasValue());
        TEST_ASSERT(maybeConfigRule2.Value()->GetOriginDirectoryFullPath() == L"C:\\OriginDir2");
        TEST_ASSERT(maybeConfigRule2.Value()->GetTargetDirectoryFullPath() == L"C:\\TargetDir2");
        TEST_ASSERT(true == maybeConfigRule2.Value()->FileNameMatchesAnyPattern(L"fileasd.dat"));
        TEST_ASSERT(false == maybeConfigRule2.Value()->FileNameMatchesAnyPattern(L"asdf.txt"));
    }

    // Verifies that filesystem rules cannot be created from configuration sections that are missing
    // either an origin or a target directory.
    TEST_CASE(FilesystemDirectorBuilder_AddRuleFromConfigurationSection_Failure_MissingDirectory)
    {
        Configuration::Section configSectionMissingOriginDirectory = {
            {L"TargetDirectory", L"C:\\TargetDir1"},
        };

        Configuration::Section configSectionMissingTargetDirectory = {
            {L"OriginDirectory", L"C:\\OriginDir2"}, {L"FilePattern", {L"log*", L"file???.dat"}}};

        FilesystemDirectorBuilder directorBuilder;

        auto maybeConfigRuleMissingOriginDirectory =
            directorBuilder.AddRuleFromConfigurationSection(
                L"1", configSectionMissingOriginDirectory
            );
        TEST_ASSERT(maybeConfigRuleMissingOriginDirectory.HasError());
        TEST_ASSERT(maybeConfigRuleMissingOriginDirectory.Error().AsStringView().contains(
            L"origin directory"
        ));

        auto maybeConfigRuleMissingTargetDirectory =
            directorBuilder.AddRuleFromConfigurationSection(
                L"2", configSectionMissingTargetDirectory
            );
        TEST_ASSERT(maybeConfigRuleMissingTargetDirectory.HasError());
        TEST_ASSERT(maybeConfigRuleMissingTargetDirectory.Error().AsStringView().contains(
            L"target directory"
        ));
    }

    // Verifies that directory presence is successfully reported when rules exist and is correctly
    // categorized by origin or target.
    TEST_CASE(FilesystemDirectorBuilder_HasDirectory_Nominal)
    {
        FilesystemDirectorBuilder directorBuilder;

        TEST_ASSERT(directorBuilder.AddRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir1").HasValue());
        TEST_ASSERT(directorBuilder.AddRule(L"2", L"C:\\OriginDir2", L"C:\\TargetDir2").HasValue());

        TEST_ASSERT(true == directorBuilder.HasDirectory(L"C:\\OriginDir1"));
        TEST_ASSERT(true == directorBuilder.HasDirectory(L"C:\\OriginDir2"));
        TEST_ASSERT(true == directorBuilder.HasDirectory(L"C:\\TargetDir1"));
        TEST_ASSERT(true == directorBuilder.HasDirectory(L"C:\\TargetDir2"));

        TEST_ASSERT(true == directorBuilder.HasOriginDirectory(L"C:\\OriginDir1"));
        TEST_ASSERT(true == directorBuilder.HasOriginDirectory(L"C:\\OriginDir2"));
        TEST_ASSERT(false == directorBuilder.HasTargetDirectory(L"C:\\OriginDir1"));
        TEST_ASSERT(false == directorBuilder.HasTargetDirectory(L"C:\\OriginDir2"));

        TEST_ASSERT(true == directorBuilder.HasTargetDirectory(L"C:\\TargetDir1"));
        TEST_ASSERT(true == directorBuilder.HasTargetDirectory(L"C:\\TargetDir2"));
        TEST_ASSERT(false == directorBuilder.HasOriginDirectory(L"C:\\TargetDir1"));
        TEST_ASSERT(false == directorBuilder.HasOriginDirectory(L"C:\\TargetDir2"));
    }

    // Verifies that directory presence is correctly reported for those directories explicitly in a
    // hierarchy. This test uses origin directories for that purpose.
    TEST_CASE(FilesystemDirectorBuilder_HasDirectory_Hierarchy)
    {
        FilesystemDirectorBuilder directorBuilder;

        TEST_ASSERT(
            directorBuilder
                .AddRule(L"1", L"C:\\Level1\\Level2\\Level3\\Level4\\Level5", L"C:\\Target1")
                .HasValue()
        );
        TEST_ASSERT(directorBuilder.AddRule(L"2", L"C:\\Level1\\Level2", L"C:\\Target2").HasValue()
        );

        TEST_ASSERT(false == directorBuilder.HasOriginDirectory(L"C:\\Level1"));
        TEST_ASSERT(true == directorBuilder.HasOriginDirectory(L"C:\\Level1\\Level2"));
        TEST_ASSERT(false == directorBuilder.HasOriginDirectory(L"C:\\Level1\\Level2\\Level3"));
        TEST_ASSERT(
            false == directorBuilder.HasOriginDirectory(L"C:\\Level1\\Level2\\Level3\\Level4")
        );
        TEST_ASSERT(
            true ==
            directorBuilder.HasOriginDirectory(L"C:\\Level1\\Level2\\Level3\\Level4\\Level5")
        );
    }

    // Verifies that the filesystem director build process completes successfully in the nominal
    // case of filesystem rules having origin directories that exist and whose parent directories
    // also exist. Performs a few data structure consistency checks on the new filesystem director
    // object to ensure it was build correctly.
    TEST_CASE(FilesystemDirectorBuilder_Build_Success_Nominal)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\OriginDir1");
        mockFilesystem.AddDirectory(L"E:\\OriginDir2");

        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(directorBuilder.AddRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir").HasValue());
        TEST_ASSERT(directorBuilder.AddRule(L"2", L"E:\\OriginDir2", L"E:\\TargetDir2").HasValue());

        auto buildResult = directorBuilder.Build();
        TEST_ASSERT(buildResult.HasValue());

        FilesystemDirector director = std::move(buildResult.Value());

        TEST_ASSERT(nullptr != director.FindRuleByName(L"1"));
        TEST_ASSERT(director.FindRuleByName(L"1")->GetName() == L"1");
        TEST_ASSERT(
            director.FindRuleByName(L"1")->GetOriginDirectoryFullPath() == L"C:\\OriginDir1"
        );
        TEST_ASSERT(
            director.FindRuleByName(L"1") == director.FindRuleByOriginDirectory(L"C:\\OriginDir1")
        );

        TEST_ASSERT(nullptr != director.FindRuleByName(L"2"));
        TEST_ASSERT(director.FindRuleByName(L"2")->GetName() == L"2");
        TEST_ASSERT(
            director.FindRuleByName(L"2")->GetOriginDirectoryFullPath() == L"E:\\OriginDir2"
        );
        TEST_ASSERT(
            director.FindRuleByName(L"2") == director.FindRuleByOriginDirectory(L"E:\\OriginDir2")
        );
    }

    // Verifies that the filesystem director build process completes successfully where rules have
    // origin directories whose parents do not exist but themselves are the origin directories of
    // other rules. No rules have any file patterns.
    TEST_CASE(FilesystemDirectorBuilder_Build_Success_OriginHierarchy)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\OriginBase");

        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(directorBuilder
                        .AddRule(
                            L"1",
                            L"C:\\OriginBase\\OriginSubdir\\Subdir1\\Subdir2",
                            L"C:\\TargetBase\\Target2"
                        )
                        .HasValue());
        TEST_ASSERT(
            directorBuilder
                .AddRule(L"2", L"C:\\OriginBase\\OriginSubdir\\Subdir1", L"C:\\TargetBase\\Target1")
                .HasValue()
        );
        TEST_ASSERT(
            directorBuilder
                .AddRule(L"3", L"C:\\OriginBase\\OriginSubdir", L"C:\\TargetBase\\TargetBase")
                .HasValue()
        );

        auto buildResult = directorBuilder.Build();
        TEST_ASSERT(buildResult.HasValue());
    }

    // Verifies that the filesystem director build process fails when the origin directory path
    // already exists but is not a directory.
    TEST_CASE(FilesystemDirectorBuilder_Build_Failure_OriginExistsNotAsDirectory)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddFile(L"C:\\OriginDir\\File");

        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(
            directorBuilder.AddRule(L"1", L"C:\\OriginDir\\File", L"C:\\TargetDir").HasValue()
        );

        auto buildResult = directorBuilder.Build();
        TEST_ASSERT(buildResult.HasError());
    }

    // Verifies that the filesystem director build process fails when the origin directory's parent
    // does not exist in the filesystem or as another origin directory.
    TEST_CASE(FilesystemDirectorBuilder_Build_Failure_OriginParentMissing)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:");

        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(directorBuilder
                        .AddRule(L"1", L"C:\\OriginDir\\Subdir1", L"C:\\TargetDir\\Subdir1")
                        .HasValue());

        auto buildResult = directorBuilder.Build();
        TEST_ASSERT(buildResult.HasError());
    }

    // Verifies that the filesystem director build process fails when a target directory conflicts
    // with another rule's origin or target directory by virtue of the latter being an ancestor of
    // the former. In this case the conflict is between the target directories of two rules.
    TEST_CASE(FilesystemDirectorBuilder_Build_Failure_TargetHierarchyConflictWithTarget)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\OriginDir1");
        mockFilesystem.AddDirectory(L"C:\\OriginDir2");

        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(directorBuilder.AddRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir1").HasValue());
        TEST_ASSERT(directorBuilder.AddRule(L"2", L"C:\\OriginDir2", L"C:\\TargetDir1\\TargetDir2")
                        .HasValue());

        auto buildResult = directorBuilder.Build();
        TEST_ASSERT(buildResult.HasError());
    }

    // Verifies that the filesystem director build process fails when a target directory conflicts
    // with another rule's origin or target directory by virtue of the latter being an ancestor of
    // the former. In this case the conflict is between the target directory of one rule and the
    // origin directory of another.
    TEST_CASE(FilesystemDirectorBuilder_Build_Failure_TargetHierarchyConflictWithOrigin)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\OriginDir1");
        mockFilesystem.AddDirectory(L"C:\\OriginDir2");

        FilesystemDirectorBuilder directorBuilder;
        TEST_ASSERT(directorBuilder.AddRule(L"1", L"C:\\OriginDir1", L"C:\\TargetDir1").HasValue());
        TEST_ASSERT(directorBuilder.AddRule(L"2", L"C:\\OriginDir2", L"C:\\OriginDir1\\TargetDir2")
                        .HasValue());

        auto buildResult = directorBuilder.Build();
        TEST_ASSERT(buildResult.HasError());
    }

    // Verifies that a filesystem director object can be built from a configuration file in the
    // nominal case of filesystem rules having origin directories that exist and whose parent
    // directories also exist. Performs a few data structure consistency checks on the new
    // filesystem director object to ensure it was build correctly. This test case uses a
    // configuration data object instead of calling builder methods directly.
    TEST_CASE(FilesystemDirectorBuilder_BuildFromConfigurationData_Success_Nominal)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\OriginDir1");
        mockFilesystem.AddDirectory(L"E:\\OriginDir2");

        Configuration::ConfigurationData configData = {
            {L"FilesystemRule:1",
             {{L"OriginDirectory", L"C:\\OriginDir1"}, {L"TargetDirectory", L"C:\\TargetDir"}}},
            {L"FilesystemRule:2",
             {{L"OriginDirectory", L"E:\\OriginDir2"}, {L"TargetDirectory", L"E:\\TargetDir2"}}},
        };

        auto buildResult = FilesystemDirectorBuilder::BuildFromConfigurationData(configData);
        TEST_ASSERT(true == buildResult.has_value());
        TEST_ASSERT(true == configData.IsEmpty());

        FilesystemDirector director = std::move(buildResult.value());

        TEST_ASSERT(nullptr != director.FindRuleByName(L"1"));
        TEST_ASSERT(director.FindRuleByName(L"1")->GetName() == L"1");
        TEST_ASSERT(
            director.FindRuleByName(L"1")->GetOriginDirectoryFullPath() == L"C:\\OriginDir1"
        );
        TEST_ASSERT(
            director.FindRuleByName(L"1") == director.FindRuleByOriginDirectory(L"C:\\OriginDir1")
        );

        TEST_ASSERT(nullptr != director.FindRuleByName(L"2"));
        TEST_ASSERT(director.FindRuleByName(L"2")->GetName() == L"2");
        TEST_ASSERT(
            director.FindRuleByName(L"2")->GetOriginDirectoryFullPath() == L"E:\\OriginDir2"
        );
        TEST_ASSERT(
            director.FindRuleByName(L"2") == director.FindRuleByOriginDirectory(L"E:\\OriginDir2")
        );
    }

    // Verifies that a filesystem director object can be built from a configuration file in the
    // nominal case but modified to add file patterns. Performs a few data structure consistency
    // checks on the new filesystem director object to ensure it was build correctly. This test case
    // uses a configuration data object instead of calling builder methods directly.
    TEST_CASE(FilesystemDirectorBuilder_BuildFromConfigurationData_Success_WithFilePatterns)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\OriginDir1");
        mockFilesystem.AddDirectory(L"E:\\OriginDir2");

        Configuration::ConfigurationData configData = {
            {L"FilesystemRule:1",
             {{L"OriginDirectory", L"C:\\OriginDir1"},
              {L"TargetDirectory", L"C:\\TargetDir"},
              {L"FilePattern", L"*.sav"}}},
            {L"FilesystemRule:2",
             {{L"OriginDirectory", L"E:\\OriginDir2"},
              {L"TargetDirectory", L"E:\\TargetDir2"},
              {L"FilePattern", {L"config????.cfg", L"*.log", L"*.dat", L"file000?", L"*.txt"}}}},
        };

        auto buildResult = FilesystemDirectorBuilder::BuildFromConfigurationData(configData);
        TEST_ASSERT(true == buildResult.has_value());
        TEST_ASSERT(true == configData.IsEmpty());

        FilesystemDirector director = std::move(buildResult.value());

        TEST_ASSERT(nullptr != director.FindRuleByName(L"1"));
        TEST_ASSERT(director.FindRuleByName(L"1")->GetName() == L"1");
        TEST_ASSERT(
            director.FindRuleByName(L"1")->GetOriginDirectoryFullPath() == L"C:\\OriginDir1"
        );
        TEST_ASSERT(
            director.FindRuleByName(L"1") == director.FindRuleByOriginDirectory(L"C:\\OriginDir1")
        );

        TEST_ASSERT(nullptr != director.FindRuleByName(L"2"));
        TEST_ASSERT(director.FindRuleByName(L"2")->GetName() == L"2");
        TEST_ASSERT(
            director.FindRuleByName(L"2")->GetOriginDirectoryFullPath() == L"E:\\OriginDir2"
        );
        TEST_ASSERT(
            director.FindRuleByName(L"2") == director.FindRuleByOriginDirectory(L"E:\\OriginDir2")
        );
    }

    // Verifies that the filesystem director build process fails when the origin directory's parent
    // does not exist in the filesystem or as another origin directory. This test case uses a
    // configuration data object instead of calling builder methods directly.
    TEST_CASE(FilesystemDirectorBuilder_BuildFromConfigurationData_Failure_OriginParentMissing)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:");

        Configuration::ConfigurationData configData = {
            {L"FilesystemRule:1",
             {{L"OriginDirectory", L"C:\\OriginDir\\Subdir1"},
              {L"TargetDirectory", L"C:\\TargetDir\\Subdir1"}}}};

        auto buildResult = FilesystemDirectorBuilder::BuildFromConfigurationData(configData);
        TEST_ASSERT(false == buildResult.has_value());
        TEST_ASSERT(true == configData.IsEmpty());
    }

    // Verifies that filesystem rules cannot be created from configuration sections that are missing
    // either an origin or a target directory. This test case uses a configuration data object
    // instead of calling builder methods directly.
    TEST_CASE(FilesystemDirectorBuilder_BuildFromConfigurationData_Failure_RuleMissingDirectory)
    {
        MockFilesystemOperations mockFilesystem;
        mockFilesystem.AddDirectory(L"C:\\OriginDir2");

        Configuration::ConfigurationData configDataArray[] = {
            {{L"FilesystemRule:1", {{L"TargetDirectory", L"C:\\TargetDir1"}}}},
            {{L"FilesystemRule:2",
              {{L"OriginDirectory", L"C:\\OriginDirectory2"},
               {L"FilePattern", {L"log*", L"file???.dat"}}}}}};

        for (auto& configData : configDataArray)
        {
            auto buildResult = FilesystemDirectorBuilder::BuildFromConfigurationData(configData);
            TEST_ASSERT(false == buildResult.has_value());
            TEST_ASSERT(true == configData.IsEmpty());
        }
    }
}  // namespace PathwinderTest
