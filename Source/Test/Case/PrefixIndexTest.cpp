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
#include "TemporaryBuffer.h"
#include "TestCase.h"

#include <unordered_map>
#include <type_traits>


namespace PathwinderTest
{
    using namespace ::Pathwinder;


    // -------- INTERNAL CONSTANTS ----------------------------------------- //

    /// Test data that can be referenced by prefix index data structures that are created in test cases.
    static constexpr int kTestData[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};


    // -------- INTERNAL TYPES --------------------------------------------- //

    /// Type alias for all tests that exercise the prefix index data structure.
    typedef PrefixIndex<wchar_t, int> TTestPrefixIndex;


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Compares the contents of two array-indexable container types for their contents being equal where order is unimportant.
    /// @tparam ArrayIndexableTypeA Type for the first container in the comparison.
    /// @tparam ArrayIndexableTypeB Type for the second container in the comparison.
    /// @param [in] a First container in the comparison.
    /// @param [in] b Second container in the comparison.
    /// @return `true` if the contents of the two containers are the same, regardless of order, or `false` otherwise.
    template <typename ArrayIndexableTypeA, typename ArrayIndexableTypeB> bool UnorderedContentsEqual(const ArrayIndexableTypeA& a, const ArrayIndexableTypeB& b)
    {
        std::unordered_map<std::remove_const_t<std::remove_reference_t<decltype(a[0])>>, int> contentsOfA;
        for (const auto& itemOfA : a)
            contentsOfA[itemOfA] += 1;

        std::unordered_map<std::remove_const_t<std::remove_reference_t<decltype(b[0])>>, int> contentsOfB;
        for (const auto& itemOfB : b)
            contentsOfB[itemOfB] += 1;

        return (contentsOfA == contentsOfB);
    }


    // -------- TEST CASES ------------------------------------------------- //

    // Inserts a few strings into the prefix index.
    // Verifies that only the strings specifically inserted are seen as being contained in the index and that the correct data reference is returned accordingly for queries.
    // Only some of the strings represent valid objects that are "contained" in the index, but all levels should at least be indicated as being valid prefix paths.
    TEST_CASE(PrefixIndex_QueryContents_Nominal)
    {
        TTestPrefixIndex index(L"\\");

        index.Insert(L"Level1\\Level2\\Level3\\Level4\\Level5", kTestData[5]);
        index.Insert(L"Level1\\Level2", kTestData[2]);

        TEST_ASSERT(false == index.Contains(L"Level1"));
        TEST_ASSERT(true == index.HasPathForPrefix(L"Level1"));

        TEST_ASSERT(true  == index.Contains(L"Level1\\Level2"));
        TEST_ASSERT(true == index.HasPathForPrefix(L"Level1\\Level2"));

        TEST_ASSERT(false == index.Contains(L"Level1\\Level2\\Level3"));
        TEST_ASSERT(true == index.HasPathForPrefix(L"Level1\\Level2\\Level3"));

        TEST_ASSERT(false == index.Contains(L"Level1\\Level2\\Level3\\Level4"));
        TEST_ASSERT(true == index.HasPathForPrefix(L"Level1\\Level2\\Level3\\Level4"));

        TEST_ASSERT(true  == index.Contains(L"Level1\\Level2\\Level3\\Level4\\Level5"));
        TEST_ASSERT(true == index.HasPathForPrefix(L"Level1\\Level2\\Level3\\Level4\\Level5"));

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

    // Inserts a few strings into the prefix index and queries the prefix index using all lower-case to test for case insensitivity.
    // Verifies that only the strings specifically inserted are seen as being contained in the index and that the correct data reference is returned accordingly for queries.
    // Only some of the strings represent valid objects that are "contained" in the index, but all levels should at least be indicated as being valid prefix paths.
    TEST_CASE(PrefixIndex_QueryContents_CaseInsensitive)
    {
        TTestPrefixIndex index(L"\\");

        index.Insert(L"Level1\\Level2\\Level3\\Level4\\Level5", kTestData[5]);
        index.Insert(L"Level1\\Level2", kTestData[2]);

        TEST_ASSERT(false == index.Contains(L"level1"));
        TEST_ASSERT(true == index.HasPathForPrefix(L"level1"));

        TEST_ASSERT(true == index.Contains(L"level1\\level2"));
        TEST_ASSERT(true == index.HasPathForPrefix(L"level1\\level2"));

        TEST_ASSERT(false == index.Contains(L"level1\\level2\\level3"));
        TEST_ASSERT(true == index.HasPathForPrefix(L"level1\\level2\\level3"));

        TEST_ASSERT(false == index.Contains(L"level1\\level2\\level3\\level4"));
        TEST_ASSERT(true == index.HasPathForPrefix(L"level1\\level2\\level3\\level4"));

        TEST_ASSERT(true == index.Contains(L"level1\\level2\\level3\\level4\\level5"));
        TEST_ASSERT(true == index.HasPathForPrefix(L"level1\\level2\\level3\\level4\\level5"));

        TEST_ASSERT(nullptr == index.Find(L"level1"));
        TEST_ASSERT(nullptr == index.Find(L"level1\\level2\\level3"));
        TEST_ASSERT(nullptr == index.Find(L"level1\\level2\\level3\\level4"));

        auto level2Node = index.Find(L"level1\\level2");
        TEST_ASSERT(nullptr != level2Node);
        TEST_ASSERT(level2Node->GetData() == &kTestData[2]);

        auto level5Node = index.Find(L"level1\\level2\\level3\\level4\\level5");
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

    // Inserts a few strings into the prefix index, as with the nominal test case but this time with consecutive delimiters.
    // Verifies that only the strings specifically inserted are seen as being contained in the index and that the correct data reference is returned accordingly for queries.
    TEST_CASE(PrefixIndex_QueryContents_ConsecutiveDelimiters)
    {
        TTestPrefixIndex index(L"\\");

        index.Insert(L"Level1\\Level2\\\\Level3\\\\\\Level4\\\\\\\\Level5", kTestData[5]);
        index.Insert(L"Level1\\\\\\\\\\Level2", kTestData[2]);

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

    // Inserts a few strings into the prefix index using multiple delimters, as with the multiple delimiter test case but this time with consecutive delimiters of different types.
    // Verifies that only the strings specifically inserted are seen as being contained in the index and uses multiple different delimiters when querying.
    TEST_CASE(PrefixIndex_QueryContents_ConsecutiveAndMultipleDelimiters)
    {
        TTestPrefixIndex index({L"\\", L"/"});

        index.Insert(L"Level1\\/\\////\\Level2///\\Level3\\Level4", kTestData[4]);
        index.Insert(L"Level1/Level2\\\\Level3\\/\\\\Level4////\\Level5/\\\\\\Level6\\Level7//Level8", kTestData[8]);

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

    // Attempts to locate the longest matching prefix in the nominal situation in which such a prefix exists.
    // Verifies that the correct node is returned from the longest prefix query.
    TEST_CASE(PrefixIndex_LongestMatchingPrefix_Nominal)
    {
        TTestPrefixIndex index(L"\\");

        TEST_ASSERT(true == index.Insert(L"Level1\\Level2\\Level3\\Level4", kTestData[14]).second);

        auto level4Node = index.Find(L"Level1\\Level2\\Level3\\Level4");
        TEST_ASSERT(nullptr != level4Node);

        auto longestMatchingPrefixNode = index.LongestMatchingPrefix(L"Level1\\Level2\\Level3\\Level4\\Level5\\Level6\\Level7\\Level8\\Level9\\Level10");
        TEST_ASSERT(level4Node == longestMatchingPrefixNode);
    }

    // Attempts to locate the longest matching prefix when no match exists in the index.
    // Verifies that no node is returned from the longest prefix query.
    TEST_CASE(PrefixIndex_LongestMatchingPrefix_NoMatch)
    {
        TTestPrefixIndex index(L"\\");

        TEST_ASSERT(true == index.Insert(L"Level1\\Level2\\Level3\\Level4", kTestData[14]).second);

        auto longestMatchingPrefixNode = index.LongestMatchingPrefix(L"A\\B\\C\\D");
        TEST_ASSERT(nullptr == longestMatchingPrefixNode);
    }

    // Attempts to locate the longest matching prefix in the special situation in which the query string exactly matches a string in the index.
    // Verifies that the correct node is returned from the longest prefix query.
    TEST_CASE(PrefixIndex_LongestMatchingPrefix_ExactMatch)
    {
        TTestPrefixIndex index(L"\\");

        TEST_ASSERT(true == index.Insert(L"Level1\\Level2\\Level3\\Level4", kTestData[14]).second);

        auto level4Node = index.Find(L"Level1\\Level2\\Level3\\Level4");
        TEST_ASSERT(nullptr != level4Node);

        auto longestMatchingPrefixNode = index.LongestMatchingPrefix(L"Level1\\Level2\\Level3\\Level4");
        TEST_ASSERT(level4Node == longestMatchingPrefixNode);
    }

    // Attempts to locate the longest matching prefix when a branch exists in the tree such that the branch point is contained in the index.
    // The node for the branch point, also the actual longest matching prefix, should be returned.
    TEST_CASE(PrefixIndex_LongestMatchingPrefix_BranchContained)
    {
        TTestPrefixIndex index(L"\\");

        TEST_ASSERT(true == index.Insert(L"Root\\Level1\\Level2\\Branch\\Level3\\Level4", kTestData[14]).second);
        TEST_ASSERT(true == index.Insert(L"Root\\Level1\\Level2\\Branch\\Level5\\Level6", kTestData[15]).second);
        TEST_ASSERT(true == index.Insert(L"Root\\Level1\\Level2\\Branch", kTestData[0]).second);

        auto branchNode = index.Find(L"Root\\Level1\\Level2\\Branch");
        TEST_ASSERT(nullptr != branchNode);
        
        auto longestMatchingPrefixNode = index.LongestMatchingPrefix(L"Root\\Level1\\Level2\\Branch\\Level7\\Level8");
        TEST_ASSERT(branchNode == longestMatchingPrefixNode);
    }

    // Attempts to locate the longest matching prefix when a branch exists in the tree such that the branch point is not contained in the index.
    // The node for the branch point should not be returned because it is not contained in the index, even though a node for it exists in the index tree.
    TEST_CASE(PrefixIndex_LongestMatchingPrefix_BranchNotContained)
    {
        TTestPrefixIndex index(L"\\");

        TEST_ASSERT(true == index.Insert(L"Root\\Level1\\Level2\\Branch\\Level3\\Level4", kTestData[14]).second);
        TEST_ASSERT(true == index.Insert(L"Root\\Level1\\Level2\\Branch\\Level5\\Level6", kTestData[15]).second);

        auto longestMatchingPrefixNode = index.LongestMatchingPrefix(L"Root\\Level1\\Level2\\Branch\\Level7\\Level8");
        TEST_ASSERT(nullptr == longestMatchingPrefixNode);
    }

    // Creates a few prefix branches and verifies that in all cases the correct set of immediate children is returned.
    // For those queries that do not target an existing prefix branch, verifies that no children are returned.
    TEST_CASE(PrefixIndex_FindAllImmediateChildren_Nominal)
    {
        TTestPrefixIndex index(L"\\");

        index.Insert(L"Base", kTestData[0]);
        index.Insert(L"Base\\BranchA", kTestData[1]);
        index.Insert(L"Base\\BranchB", kTestData[6]);
        index.Insert(L"Base\\BranchC", kTestData[11]);
        const TemporaryVector expectedOutputBase = {index.Find(L"Base\\BranchA"), index.Find(L"Base\\BranchB"), index.Find(L"Base\\BranchC")};

        index.Insert(L"Base\\BranchA\\2", kTestData[2]);
        index.Insert(L"Base\\BranchA\\3", kTestData[3]);
        index.Insert(L"Base\\BranchA\\4", kTestData[4]);
        index.Insert(L"Base\\BranchA\\5", kTestData[5]);
        const TemporaryVector expectedOutputBranchA = {index.Find(L"Base\\BranchA\\2"), index.Find(L"Base\\BranchA\\3"), index.Find(L"Base\\BranchA\\4"), index.Find(L"Base\\BranchA\\5")};

        index.Insert(L"Base\\BranchB\\7", kTestData[7]);
        index.Insert(L"Base\\BranchB\\8", kTestData[8]);
        index.Insert(L"Base\\BranchB\\9", kTestData[9]);
        index.Insert(L"Base\\BranchB\\10", kTestData[10]);
        const TemporaryVector expectedOutputBranchB = {index.Find(L"Base\\BranchB\\7"), index.Find(L"Base\\BranchB\\8"), index.Find(L"Base\\BranchB\\9"), index.Find(L"Base\\BranchB\\10")};

        index.Insert(L"Base\\BranchC\\12", kTestData[12]);
        index.Insert(L"Base\\BranchC\\13", kTestData[13]);
        index.Insert(L"Base\\BranchC\\14", kTestData[14]);
        index.Insert(L"Base\\BranchC\\15", kTestData[15]);
        const TemporaryVector expectedOutputBranchC = {index.Find(L"Base\\BranchC\\12"), index.Find(L"Base\\BranchC\\13"), index.Find(L"Base\\BranchC\\14"), index.Find(L"Base\\BranchC\\15")};

        auto actualOutput = index.FindAllImmediateChildren(L"Base");
        TEST_ASSERT(true == actualOutput.has_value());
        TEST_ASSERT(UnorderedContentsEqual(actualOutput.value(), expectedOutputBase));

        actualOutput = index.FindAllImmediateChildren(L"Base\\BranchA");
        TEST_ASSERT(true == actualOutput.has_value());
        TEST_ASSERT(UnorderedContentsEqual(actualOutput.value(), expectedOutputBranchA));

        actualOutput = index.FindAllImmediateChildren(L"Base\\BranchB");
        TEST_ASSERT(true == actualOutput.has_value());
        TEST_ASSERT(UnorderedContentsEqual(actualOutput.value(), expectedOutputBranchB));

        actualOutput = index.FindAllImmediateChildren(L"Base\\BranchC");
        TEST_ASSERT(true == actualOutput.has_value());
        TEST_ASSERT(UnorderedContentsEqual(actualOutput.value(), expectedOutputBranchC));

        actualOutput = index.FindAllImmediateChildren(L"Base\\BranchD");
        TEST_ASSERT(false == actualOutput.has_value());

        actualOutput = index.FindAllImmediateChildren(L"OtherBase\\BranchA");
        TEST_ASSERT(false == actualOutput.has_value());
    }
}
