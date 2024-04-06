/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file IntegrationTestSupport.h
 *   Declaration of types and functions that facilitate the creation of integration tests by
 *   encapsulating much of the boilerplate and common logic for setting up data structures and
 *   interacting with the filesystem executor.
 **************************************************************************************************/

#pragma once

#include <memory>
#include <set>
#include <string_view>

#include "ApiWindows.h"
#include "FileInformationStruct.h"
#include "FilesystemDirector.h"
#include "FilesystemExecutor.h"
#include "MockFilesystemOperations.h"
#include "OpenHandleStore.h"

namespace PathwinderTest
{
  using namespace ::Pathwinder;

  /// Function request identifier to be passed to all filesystem executor functions when they are
  /// invoked for testing.
  inline constexpr unsigned int kFunctionRequestIdentifier = 9999;

  /// Holds all of the data structures needed to invoke filesystem executor functions as part of an
  /// integration test.
  struct SIntegrationTestContext
  {
    MockFilesystemOperations& mockFilesystem;
    FilesystemDirector filesystemDirector;
    OpenHandleStore openHandleStore;

    inline SIntegrationTestContext(
        MockFilesystemOperations& mockFilesystem, FilesystemDirector&& filesystemDirector)
        : mockFilesystem(mockFilesystem),
          filesystemDirector(std::move(filesystemDirector)),
          openHandleStore(OpenHandleStore())
    {}
  };

  /// Type alias for sets that hold compile-time constant filenames.
  using TFileNameSet = std::set<std::wstring_view>;

  /// Type alias for holding heap-allocated integration test contexts.
  using TIntegrationTestContext = std::unique_ptr<SIntegrationTestContext>;

  /// Attempts to create all of the data structures needed to support an integration test using
  /// the specified configuration file string. Before calling this function, a mock filesystem must
  /// already exist and be pre-populated with the desired files and directories.
  /// @param [in] mockFilesystem Fake filesystem object, created and maintained by the
  /// calling test case and pre-populated with the desired filesystem contents.
  /// @param [in] configurationFile String representation of a Pathwinder configuration file. The
  /// test will fail if there is an error in the configuration file.
  /// @return Heap-allocated integration test context object.
  TIntegrationTestContext CreateIntegrationTestContext(
      MockFilesystemOperations& mockFilesystem, std::wstring_view configurationFile);

  /// Uses the filesystem executor subsystem to close an open handle. If the operation fails, this
  /// function causes a test failure.
  /// @param [in] context Integration test context object returned from
  /// #CreateIntegrationTestContext.
  /// @param [in] handleToClose Previously-opened handle that should be closed.
  void CloseHandleUsingFilesystemExecutor(TIntegrationTestContext& context, HANDLE handleToClose);

  /// Uses the filesystem executor subsystem to create a new directory and add it to the mock
  /// filesystem.
  /// @param [in] context Integration test context object returned from
  /// #CreateIntegrationTestContext.
  /// @param [in] absolutePathToCreate Absolute path of the file or directory to be created.
  void CreateDirectoryUsingFilesystemExecutor(
      TIntegrationTestContext& context, std::wstring_view absolutePathToCreate);

  /// Uses the filesystem executor subsystem to create a new file and add it to the mock filesystem.
  /// @param [in] context Integration test context object returned from
  /// #CreateIntegrationTestContext.
  /// @param [in] absolutePathToCreate Absolute path of the file or directory to be created.
  void CreateFileUsingFilesystemExecutor(
      TIntegrationTestContext& context, std::wstring_view absolutePathToCreate);

  /// Uses the filesystem executor subsystem to open a file or directory for the specified absolute
  /// file path. If the operation fails, this function causes a test failure.
  /// @param [in] context Integration test context object returned from
  /// #CreateIntegrationTestContext.
  /// @param [in] absolutePathToOpen Absolute path of the file or directory to be opened. In order
  /// for this function to succeed the file or directory must exist in the fake filesystem.
  /// @return Handle to the newly-opened file.
  HANDLE OpenUsingFilesystemExecutor(
      TIntegrationTestContext& context, std::wstring_view absolutePathToOpen);

  /// Verifies that a directory appears to contain exactly the specified set of files and
  /// subdirectories, both by enumerating the contents of the directory and by directly attempting
  /// to open each expected file and subdirectory by its absolute path.
  /// @param [in] context Integration test context object returned from
  /// #CreateIntegrationTestContext.
  /// @param [in] expectedFiles Filenames that are expected to be enumerated.
  /// @param [in] directoryAbsolutePath Absolute path of the directory, with no trailing
  /// backslash, that should be enumerated.
  void VerifyDirectoryAppearsToContain(
      TIntegrationTestContext& context,
      std::wstring_view directoryAbsolutePath,
      const TFileNameSet& expectedFiles);
} // namespace PathwinderTest
