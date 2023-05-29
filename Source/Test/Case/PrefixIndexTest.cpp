/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file PrefixIndexTest.cpp
 *   Unit tests for index data structure objects that use prefixes in
 *   delimited strings as a basis for organization.
 *****************************************************************************/

#include "PrefixIndex.h"
#include "TestCase.h"


namespace PathwinderTest
{
    using namespace ::Pathwinder;


    // -------- INTERNAL CONSTANTS ----------------------------------------- //

    /// Test data that can be referenced by prefix index data structures that are created in test cases.
    static constexpr int kTestData[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};


    // -------- INTERNAL TYPES --------------------------------------------- //

    /// Type alias for all tests that exercise the prefix index data structure.
    typedef PrefixIndex<wchar_t, int> TTestPrefixIndex;


    // -------- TEST CASES ------------------------------------------------- //

    // Inserts a few strings into the prefix index.
    // Verifies that only the strings specifically inserted are seen as being contained in the index and that the correct data reference is returned accordingly for queries.
    TEST_CASE(PrefixIndex_QueryContents_Nominal)
    {
        TTestPrefixIndex index(L"\\");

        index.Insert(L"Level1\\Level2\\Level3\\Level4\\Level5", kTestData[5]);
        index.Insert(L"Level1\\Level2", kTestData[2]);

        TEST_ASSERT(false == index.Contains(L"Level1"));
        TEST_ASSERT(true  == index.Contains(L"Level1\\Level2"));
        TEST_ASSERT(false == index.Contains(L"Level1\\Level2\\Level3"));
        TEST_ASSERT(false == index.Contains(L"Level1\\Level2\\Level3\\Level4"));
        TEST_ASSERT(true  == index.Contains(L"Level1\\Level2\\Level3\\Level4\\Level5"));

        TEST_ASSERT(nullptr == index.Find(L"Level1"));
        TEST_ASSERT(nullptr == index.Find(L"Level1\\Level2\\Level3"));
        TEST_ASSERT(nullptr == index.Find(L"Level1\\Level2\\Level3\\Level4"));

        auto level2Node = index.Find(L"Level1\\Level2");
        TEST_ASSERT(nullptr != level2Node);
        TEST_ASSERT(level2Node->GetData() == &kTestData[2]);

        auto level5Node = index.Find(L"Level1\\Level2\\Level3\\Level4\\Level5");
        TEST_ASSERT(nullptr != level5Node);
        TEST_ASSERT(level5Node->GetData() == &kTestData[5]);
    }

    // Inserts a few strings into the prefix index using multiple delimters.
    // Verifies that only the strings specifically inserted are seen as being contained in the index and uses multiple different delimiters when querying.
    TEST_CASE(PrefixIndex_QueryContents_MultipleDelimiters)
    {
        TTestPrefixIndex index({L"\\", L"/"});

        index.Insert(L"Level1\\Level2\\Level3\\Level4", kTestData[4]);
        index.Insert(L"Level1/Level2\\Level3/Level4\\Level5/Level6\\Level7/Level8", kTestData[8]);

        TEST_ASSERT(false == index.Contains(L"Level1"));
        TEST_ASSERT(false == index.Contains(L"Level1/Level2"));
        TEST_ASSERT(false == index.Contains(L"Level1/Level2\\Level3"));
        TEST_ASSERT(true  == index.Contains(L"Level1/Level2\\Level3\\Level4"));
        TEST_ASSERT(false == index.Contains(L"Level1/Level2\\Level3\\Level4/Level5"));
        TEST_ASSERT(false == index.Contains(L"Level1/Level2\\Level3\\Level4/Level5\\Level6"));
        TEST_ASSERT(false == index.Contains(L"Level1/Level2\\Level3\\Level4/Level5\\Level6/Level7"));
        TEST_ASSERT(true  == index.Contains(L"Level1/Level2\\Level3\\Level4/Level5\\Level6/Level7\\Level8"));
    }

    // Inserts the same string into the prefix index multiple times.
    // Verifies that the data value is not overwritten and all subsequent insertion attempts fail.
    TEST_CASE(PrefixIndex_InsertDuplicate)
    {
        TTestPrefixIndex index(L"\\");

        auto insertResult = index.Insert(L"Level1\\Level2\\Level3", kTestData[3]);
        auto level3Node = insertResult.first;
        TEST_ASSERT(true == insertResult.second);

        TEST_ASSERT(std::make_pair(level3Node, false) == index.Insert(L"Level1\\Level2\\Level3", kTestData[6]));
        TEST_ASSERT(std::make_pair(level3Node, false) == index.Insert(L"Level1\\Level2\\Level3", kTestData[7]));
        TEST_ASSERT(std::make_pair(level3Node, false) == index.Insert(L"Level1\\Level2\\Level3", kTestData[8]));

        TEST_ASSERT(level3Node->GetData() == &kTestData[3]);
    }

    // Largely the same as the nominal test case except only checks contents and uses the update operation instead of the insert operation.
    // Update should behave as insert if the string is not contained in the index.
    TEST_CASE(PrefixIndex_QueryContents_UpdateInsteadOfInsert)
    {
        TTestPrefixIndex index(L"\\");

        index.Update(L"Level1\\Level2\\Level3\\Level4\\Level5", kTestData[5]);
        index.Update(L"Level1\\Level2", kTestData[2]);

        TEST_ASSERT(false == index.Contains(L"Level1"));
        TEST_ASSERT(true  == index.Contains(L"Level1\\Level2"));
        TEST_ASSERT(false == index.Contains(L"Level1\\Level2\\Level3"));
        TEST_ASSERT(false == index.Contains(L"Level1\\Level2\\Level3\\Level4"));
        TEST_ASSERT(true  == index.Contains(L"Level1\\Level2\\Level3\\Level4\\Level5"));
    }

    // Inserts a few strings into the prefix index and then updates their data values.
    // Verifies that they have the correct data values before and after the update.
    TEST_CASE(PrefixIndex_InsertAndUpdate_Nominal)
    {
        TTestPrefixIndex index(L"\\");

        index.Insert(L"Level1\\Level2\\Level3\\Level4\\Level5", kTestData[5]);
        index.Insert(L"Level1\\Level2", kTestData[2]);

        auto level2Node = index.Find(L"Level1\\Level2");
        TEST_ASSERT(nullptr != level2Node);
        TEST_ASSERT(level2Node->GetData() == &kTestData[2]);

        auto level5Node = index.Find(L"Level1\\Level2\\Level3\\Level4\\Level5");
        TEST_ASSERT(nullptr != level5Node);
        TEST_ASSERT(level5Node->GetData() == &kTestData[5]);

        TEST_ASSERT(level5Node == index.Update(L"Level1\\Level2\\Level3\\Level4\\Level5", kTestData[10]));
        TEST_ASSERT(level5Node->GetData() == &kTestData[10]);

        TEST_ASSERT(level2Node == index.Update(L"Level1\\Level2", kTestData[14]));
        TEST_ASSERT(level2Node->GetData() == &kTestData[14]);
    }

    // Inserts a few strings into the prefix index and then erases some of them.
    // Verifies that the erased nodes are no longer reported as contained in the index but the others are still there.
    TEST_CASE(PrefixIndex_Erase_Nominal)
    {
        TTestPrefixIndex index(L"\\");

        index.Insert(L"Root\\Level1\\A\\Level2\\Level3", kTestData[3]);
        index.Insert(L"Root\\Level1\\A\\Level2\\Level3\\Level4\\Level5\\Level6", kTestData[6]);
        index.Insert(L"Root\\Level1\\B\\Level7\\Level8\\Level9", kTestData[9]);
        index.Insert(L"Root\\Level1\\B\\Level7\\Level8", kTestData[8]);

        TEST_ASSERT(true  == index.Contains(L"Root\\Level1\\A\\Level2\\Level3"));
        TEST_ASSERT(true  == index.Contains(L"Root\\Level1\\A\\Level2\\Level3\\Level4\\Level5\\Level6"));
        TEST_ASSERT(true  == index.Contains(L"Root\\Level1\\B\\Level7\\Level8\\Level9"));
        TEST_ASSERT(true  == index.Contains(L"Root\\Level1\\B\\Level7\\Level8"));

        TEST_ASSERT(true  == index.Erase(L"Root\\Level1\\A\\Level2\\Level3"));
        TEST_ASSERT(true  == index.Erase(L"Root\\Level1\\B\\Level7\\Level8\\Level9"));

        TEST_ASSERT(false == index.Contains(L"Root\\Level1\\A\\Level2\\Level3"));
        TEST_ASSERT(true  == index.Contains(L"Root\\Level1\\A\\Level2\\Level3\\Level4\\Level5\\Level6"));
        TEST_ASSERT(false == index.Contains(L"Root\\Level1\\B\\Level7\\Level8\\Level9"));
        TEST_ASSERT(true  == index.Contains(L"Root\\Level1\\B\\Level7\\Level8"));
    }

    // Attempts to erase a string not present in the index, which should fail and leave the index untouched.
    TEST_CASE(PrefixIndex_Erase_PrefixNotContained)
    {
        TTestPrefixIndex index(L"\\");

        TEST_ASSERT(true == index.Insert(L"Level1\\Level2\\Level3\\Level4", kTestData[14]).second);

        TEST_ASSERT(false == index.Erase(L"Level1\\Level2"));
        TEST_ASSERT(false == index.Erase(L"Level1\\Level2\\Level3\\Level4\\Level5"));
        
        auto level4Node = index.Find(L"Level1\\Level2\\Level3\\Level4");
        TEST_ASSERT(nullptr != level4Node);
        TEST_ASSERT(level4Node->GetData() == &kTestData[14]);
    }
}
