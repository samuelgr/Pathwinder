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

    // Inserts a few strings into the prefix index.
    // Verifies that all internal nodes are accessible by traversal even if they do not represent valid objects that are "contained" in the index.
    TEST_CASE(PrefixIndex_TraverseTo_Nominal)
    {
        TTestPrefixIndex index(L"\\");

        index.Insert(L"Level1\\Level2\\Level3\\Level4\\Level5", kTestData[5]);
        index.Insert(L"Level1\\Level2", kTestData[2]);

        const TTestPrefixIndex::Node* nodeLevel1 = index.TraverseTo(L"Level1");
        const TTestPrefixIndex::Node* nodeLevel2 = index.TraverseTo(L"Level1\\Level2");
        const TTestPrefixIndex::Node* nodeLevel3 = index.TraverseTo(L"Level1\\Level2\\Level3");
        const TTestPrefixIndex::Node* nodeLevel4 = index.TraverseTo(L"Level1\\Level2\\Level3\\Level4");
        const TTestPrefixIndex::Node* nodeLevel5 = index.TraverseTo(L"Level1\\Level2\\Level3\\Level4\\Level5");

        TEST_ASSERT(nullptr != nodeLevel1);
        TEST_ASSERT(L"Level1" == nodeLevel1->GetParentKey());

        TEST_ASSERT(nullptr != nodeLevel2);
        TEST_ASSERT(L"Level2" == nodeLevel2->GetParentKey());
        TEST_ASSERT(nodeLevel1 == nodeLevel2->GetParent());

        TEST_ASSERT(nullptr != nodeLevel3);
        TEST_ASSERT(L"Level3" == nodeLevel3->GetParentKey());
        TEST_ASSERT(nodeLevel2 == nodeLevel3->GetParent());

        TEST_ASSERT(nullptr != nodeLevel4);
        TEST_ASSERT(L"Level4" == nodeLevel4->GetParentKey());
        TEST_ASSERT(nodeLevel3 == nodeLevel4->GetParent());

        TEST_ASSERT(nullptr != nodeLevel5);
        TEST_ASSERT(L"Level5" == nodeLevel5->GetParentKey());
        TEST_ASSERT(nodeLevel4 == nodeLevel5->GetParent());
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

    // Creates a small hierarchy of prefixes, including a common base node for a few sub-nodes.
    // Verifies that the base node is correctly identified as the ancestor when the sub-nodes are queried for their ancestors.
    TEST_CASE(PrefixIndex_QueryForAncestors_AncestorsExist)
    {
        TTestPrefixIndex index(L"\\");

        const TTestPrefixIndex::Node* nodeBase = index.Insert(L"Base", kTestData[0]).first;
        const TTestPrefixIndex::Node* nodeSub2 = index.Insert(L"Base\\Sub\\2", kTestData[2]).first;
        const TTestPrefixIndex::Node* nodeSub3 = index.Insert(L"Base\\Sub\\3", kTestData[3]).first;
        const TTestPrefixIndex::Node* nodeSub4 = index.Insert(L"Base\\Sub\\4", kTestData[4]).first;
        const TTestPrefixIndex::Node* nodeSub5 = index.Insert(L"Base\\Sub\\5", kTestData[5]).first;

        TEST_ASSERT(nullptr != nodeBase);
        TEST_ASSERT(nullptr != nodeSub2);
        TEST_ASSERT(nullptr != nodeSub3);
        TEST_ASSERT(nullptr != nodeSub4);
        TEST_ASSERT(nullptr != nodeSub5);

        TEST_ASSERT(nodeBase == nodeSub2->GetClosestAncestor());
        TEST_ASSERT(true == nodeSub2->HasAncestor());

        TEST_ASSERT(nodeBase == nodeSub3->GetClosestAncestor());
        TEST_ASSERT(true == nodeSub3->HasAncestor());

        TEST_ASSERT(nodeBase == nodeSub4->GetClosestAncestor());
        TEST_ASSERT(true == nodeSub4->HasAncestor());

        TEST_ASSERT(nodeBase == nodeSub5->GetClosestAncestor());
        TEST_ASSERT(true == nodeSub5->HasAncestor());
    }

    // Creates a small hierarchy of prefixes, but all at the same level and with no ancestor.
    // Verifies that the prefix index correctly indicates that none of the nodes have ancestors.
    TEST_CASE(PrefixIndex_QueryForAncestors_AncestorsDoNotExist)
    {
        TTestPrefixIndex index(L"\\");

        const TTestPrefixIndex::Node* nodeSub2 = index.Insert(L"Base\\Sub\\2", kTestData[2]).first;
        const TTestPrefixIndex::Node* nodeSub3 = index.Insert(L"Base\\Sub\\3", kTestData[3]).first;
        const TTestPrefixIndex::Node* nodeSub4 = index.Insert(L"Base\\Sub\\4", kTestData[4]).first;
        const TTestPrefixIndex::Node* nodeSub5 = index.Insert(L"Base\\Sub\\5", kTestData[5]).first;

        TEST_ASSERT(nullptr != nodeSub2);
        TEST_ASSERT(nullptr != nodeSub3);
        TEST_ASSERT(nullptr != nodeSub4);
        TEST_ASSERT(nullptr != nodeSub5);

        TEST_ASSERT(nullptr == nodeSub2->GetClosestAncestor());
        TEST_ASSERT(false == nodeSub2->HasAncestor());

        TEST_ASSERT(nullptr == nodeSub3->GetClosestAncestor());
        TEST_ASSERT(false == nodeSub3->HasAncestor());

        TEST_ASSERT(nullptr == nodeSub4->GetClosestAncestor());
        TEST_ASSERT(false == nodeSub4->HasAncestor());

        TEST_ASSERT(nullptr == nodeSub5->GetClosestAncestor());
        TEST_ASSERT(false == nodeSub5->HasAncestor());
    }
}
