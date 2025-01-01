/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2025
 ***********************************************************************************************//**
 * @file Globals.h
 *   Declaration of a namespace for storing and retrieving global data. Intended for
 *   miscellaneous data elements with no other suitable place.
 **************************************************************************************************/

#pragma once

#include <string>
#include <unordered_set>

#include "ApiWindows.h"

#ifndef PATHWINDER_SKIP_CONFIG
#include <Infra/Core/Configuration.h>
#endif

namespace Pathwinder
{
  namespace Globals
  {
    /// Performs run-time initialization. This function only performs operations that are safe to
    /// perform within a DLL entry point.
    void Initialize(void);

    /// Retrieves a reference to a global data structure that holds temporary directory paths to
    /// clean up when Pathwinder is unloaded.
    std::unordered_set<std::wstring>& TemporaryPathsToClean(void);

  } // namespace Globals
} // namespace Pathwinder
