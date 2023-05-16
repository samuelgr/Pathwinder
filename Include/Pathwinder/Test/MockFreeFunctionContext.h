/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file MockFreeFunctionContext.h
 *   Common functionality for mocking the behavior of free functions using a
 *   local object instance that controls their behavior.
 *****************************************************************************/

#pragma once

#include "TestCase.h"

#include <array>
#include <cstddef>
#include <shared_mutex>
#include <string_view>


namespace PathwinderTest
{
    /// Base class for objects that control the behavior of mock versions of free functions.
    /// Instances of subclasses are intended to be created locally within each test case.
    /// Subclasses should be declared using the macro interface provided rather than directly.
    /// Instance methods on subclasses should mirror the free functions they are mocking, and actual free functions simply need to retrieve an instance and then call the associated instance method.
    /// @tparam MockObjectType Object type that controls the behavior of associated free functions. Must be a subclass.
    /// @tparam kMockObjectTypeName String representation of the name of the mock object type.
    /// @tparam kNumContexts Number of contexts to create. This can be used to mock free functions that accept some sort of identifier as a parameter. Defaults to 1.
    template <typename MockObjectType, const wchar_t* kMockObjectTypeName, size_t kNumContexts = 1> class MockFreeFunctionContext
    {
    private:
        // -------- CLASS VARIABLES ---------------------------------------- //

        /// Holds the addresses of the mock objects that are managing the present test case context.
        /// Calls to the corresponding free functions are directed towards these specific instances.
        static inline std::array<MockObjectType*, kNumContexts> contexts;

        /// Concurrency control mutexes, one per possible context.
        /// A shared lock is taken whenever a free function is invoked, and a unique lock is taken whenever the array of current contexts is to be updated.
        static inline std::array<std::shared_mutex, kNumContexts> contextGuards;


        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Index within the contexts array that corresponds to this instance.
        size_t contextIndex;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor, can be used as a default constructor.
        /// Requires a context index, which defaults to 0.
        MockFreeFunctionContext(size_t index = 0) : contextIndex(index)
        {
            static_assert(std::derived_from<MockObjectType, std::remove_reference_t<decltype(*this)>>, "Class hierarchy constraint violation.");

            if (index > contexts.size())
                TEST_FAILED_BECAUSE(L"Out-of-bounds creation attempt for instance %z of mock free function context object %s.", index, kMockObjectTypeName);

            std::unique_lock lock(contextGuards[contextIndex]);

            if (nullptr != contexts[index])
            {
                if constexpr (kNumContexts > 1)
                    TEST_FAILED_BECAUSE(L"Multiple instances for instance %z of mock free function context object %s.", index, kMockObjectTypeName);
                else
                    TEST_FAILED_BECAUSE(L"Multiple instances of mock free function context object %s.", kMockObjectTypeName);
            }

            contexts[index] = static_cast<MockObjectType*>(this);
        }

        /// Default destructor.
        /// Unregisters this object as the one that controls the present context.
        ~MockFreeFunctionContext(void)
        {
            std::unique_lock lock(contextGuards[contextIndex]);
            contexts[contextIndex] = nullptr;
        }


        // -------- CLASS METHODS ------------------------------------------ //

        /// Retrieves the object currently controlling the context for the specified index, which defaults to 0.
        /// If the instance is missing then the associated test case will fail.
        /// Intended to be used by mock implementations of free functions.
        /// @param [in] index Index of the desired context. Defaults to 0.
        /// @return Mutable reference to the object that currently controls the specified context.
        static MockObjectType& GetContext(size_t index = 0)
        {
            if (index > contexts.size())
                TEST_FAILED_BECAUSE(L"Out-of-bounds request for instance %z of mock free function context object %s.", index, kMockObjectTypeName);

            if (nullptr == contexts[index])
            {
                if constexpr (kNumContexts > 1)
                    TEST_FAILED_BECAUSE(L"Missing instance %z of mock free function context object %s.", index, kMockObjectTypeName);
                else
                    TEST_FAILED_BECAUSE(L"Missing instance of mock free function context object %s.", kMockObjectTypeName);
            }

            return *(contexts[index]);
        }

        /// Locks the context at the specified index so that the associated object can be accessed.
        /// @param [in] index Index of the desired context. Defaults to 0.
        /// @return Shared lock that is held for the corresponding context object.
        static std::shared_lock<std::shared_mutex> LockContext(size_t index = 0)
        {
            if (index > contexts.size())
                TEST_FAILED_BECAUSE(L"Out-of-bounds lock attempt for instance %z of mock free function context object %s.", index, kMockObjectTypeName);

            return std::shared_lock(contextGuards[index]);
        }
    };
}

/// Recommended way of declaring a class that implements a mock context for free functions.
/// Requires a class name and the number of contexts to create.
#define MOCK_FREE_FUNCTION_MULTICONTEXT_CLASS(classname, numcontexts) \
    inline constexpr wchar_t kMockFreeFunctionContext__##classname[] = L#classname; \
    template <typename MockObjectType> using MockFreeFunctionContextInternalAlias = MockFreeFunctionContext<MockObjectType, kMockFreeFunctionContext__##classname, numcontexts>;\
    class classname : public MockFreeFunctionContextInternalAlias<classname>

/// Recommended way of declaring a class that implements a mock context for free functions when only one context is required.
/// Requires a class name.
#define MOCK_FREE_FUNCTION_CONTEXT_CLASS(classname)                         MOCK_FREE_FUNCTION_MULTICONTEXT_CLASS(classname, 1)

/// Recommended way of implementing a free function whose invocation is fowarded to a mock object controlling the behavior context.
/// Requires a class name, method name, context index, and names of all the parameters to be forwarded.
#define MOCK_FREE_FUNCTION_MULTICONTEXT_BODY(classname, methodname, index, ...) \
    std::shared_lock lock(MockFreeFunctionContextInternalAlias<classname>::LockContext(index)); \
    return MockFreeFunctionContextInternalAlias<classname>::GetContext(index).methodname(__VA_ARGS__);

/// Recommended way of implementing a free function whose invocation is fowarded to a mock object controlling the behavior context, when only one context exists.
/// Requires a class name, method name, and names of all the parameters to be forwarded.
#define MOCK_FREE_FUNCTION_BODY(classname, methodname, ...)                 MOCK_FREE_FUNCTION_MULTICONTEXT_BODY(classname, methodname, 0, __VA_ARGS__)
