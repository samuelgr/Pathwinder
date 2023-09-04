/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 ***********************************************************************************************//**
 * @file TestCase.cpp
 *   Implementation of the test case interface.
 **************************************************************************************************/

#include "TestCase.h"

#include <string_view>

#include "Harness.h"

namespace PathwinderTest
{
    ITestCase::ITestCase(std::wstring_view name)
    {
        Harness::RegisterTestCase(this, name);
    }
}  // namespace PathwinderTest
