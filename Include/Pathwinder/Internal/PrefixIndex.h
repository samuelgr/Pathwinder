/*****************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2023
 *************************************************************************//**
 * @file PrefixIndex.h
 *   Declaration of an index data structure efficiently traversable using
 *   prefixes in delimited strings.
 *****************************************************************************/

#pragma once

#include "Strings.h"

#include <array>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <unordered_map>


namespace Pathwinder
{
    /// Data structure for indexing objects identified by delimited strings for efficient traversal by prefix.
    /// Implemented as a prefix tree where each level represents a token within the delimited string.
    /// @tparam CharType Type of character in each string, either narrow or wide.
    template <typename CharType, typename DataType> class PrefixIndex
    {
    private:
        // -------- CONSTANTS ---------------------------------------------- //

        /// Maximum number of path delimiter strings allowed in a path prefix tree.
        static constexpr unsigned int kMaxDelimiters = 4;


        // -------- TYPE DEFINITIONS --------------------------------------- //

        /// Individual node within the prefix tree.
        class Node
        {
        private:
            // -------- INSTANCE VARIABLES --------------------------------- //

            /// Optional data associated with the node. If present (not null), the path prefix string up to this point is considered "contained" in the tree data structure.
            const DataType* data;

            /// Parent node, one level up in the tree. Cannot be used to modify the tree.
            Node* const parent;

            /// Key within the parent node's child map that is associated with this node.
            const std::basic_string_view<CharType> parentKey;

            /// Child nodes, stored associatively by path prefix string.
            std::unordered_map<std::basic_string_view<CharType>, Node> children;


        public:
            // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

            /// Initialization constructor.
            /// Requires information about this node's parent.
            inline Node(Node* parent, std::basic_string_view<CharType> parentKey) : data(), parent(parent), parentKey(parentKey), children()
            {
                // Nothing to do here.
            }


            // -------- INSTANCE METHODS ----------------------------------- //

            /// Clears the data associated with this node.
            inline void ClearData(void)
            {
                data = nullptr;
            }

            /// Removes a child of this node.
            /// This will delete not only the child but also all of its children.
            /// @param [in] childKey Path prefix portion to use as a search key for a child node.
            void EraseChild(std::basic_string_view<CharType> childKey)
            {
                auto childIter = children.find(childKey);
                if (children.end() == childIter)
                    return;

                children.erase(childIter);
            }

            /// Locates and returns a pointer to the child node corresponding to the given path prefix portion.
            /// @param [in] childKey Path prefix portion to use as a search key for a child node.
            /// @return Pointer to the child node if it exists, `nullptr` otherwise.
            const Node* FindChild(std::basic_string_view<CharType> childKey) const
            {
                const auto childIter = children.find(childKey);
                if (children.cend() == childIter)
                    return nullptr;

                return &(childIter->second);
            }

            /// Either retrieves and returns a pointer to an existing child node or creates one.
            /// @param [in] childKey Path prefix portion to use as a key for locating or creating a child node.
            /// @return Pointer to the child node.
            inline Node* FindOrEmplaceChild(std::basic_string_view<CharType> childKey)
            {
                return &(children.emplace(childKey, Node(this, childKey)).first->second);
            }

            /// Retrieves a read-only pointer to the optional data contained within this node, which may not exist.
            /// @return Read-only pointer to optional node data, or `nullptr` if no data exist.
            inline const DataType* GetData(void) const
            {
                return data;
            }

            /// Retrieves a read-only pointer to this node's parent, if it exists.
            /// @return Pointer to the parent node, or `nullptr` if no parent node exists.
            inline const Node* GetParent(void) const
            {
                return parent;
            }

            /// Retrieves a mutable pointer to this node's parent, if it exists.
            /// @return Pointer to the parent node, or `nullptr` if no parent node exists.
            inline Node* GetParent(void)
            {
                return parent;
            }

            /// Retrieves the portion of the path that corresponds to the edge from the parent node to this node.
            /// @return Key within the parent node's children data structure that corresponds to this node.
            inline std::basic_string_view<CharType> GetParentKey(void) const
            {
                return parentKey;
            }

            /// Determines if this node has any children.
            /// @return `true` if so, `false` if not.
            inline bool HasChildren(void) const
            {
                return !(children.empty());
            }

            /// Determines if this node contains data.
            /// @return `true` if so, `false` if not.
            inline bool HasData(void) const
            {
                return (nullptr != data);
            }

            /// Determines if this node contains data.
            /// @return `true` if so, `false` if not.
            inline bool HasParent(void) const
            {
                return (nullptr != parent);
            }

            /// Updates the optional data stored within this node.
            /// @param [in] newData New data to be stored within this node.
            inline void SetData(const DataType* newData)
            {
                data = newData;
            }
        };


        // -------- INSTANCE VARIABLES ------------------------------------- //

        /// Root node of the path prefix tree data structure.
        /// Will only ever contain children, no data or parents.
        Node rootNode;

        /// Delimiters that act as delimiters between components of path strings.
        /// Immutable once this object is created.
        std::array<std::basic_string_view<CharType>, kMaxDelimiters> pathDelimiters;

        /// Actual number of path component delimiters.
        /// Immutable once this object is created.
        unsigned int pathDelimiterCount;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Initialization constructor.
        /// Fills path delimiters using an array and a count. An array-out-of-bounds error will occur if the number of delimiters is too high.
        PrefixIndex(const std::basic_string_view<CharType>* pathDelimiterArray, unsigned int pathDelimiterArrayCount) : rootNode(nullptr, std::basic_string_view<CharType>()), pathDelimiters(), pathDelimiterCount(pathDelimiterArrayCount)
        {
            for (unsigned int i = 0; i < pathDelimiterArrayCount; ++i)
                pathDelimiters[i] = pathDelimiterArray[i];
        }

        /// Initialization constructor.
        /// Fills path delimiters using an initializer list.
        inline PrefixIndex(std::initializer_list<std::basic_string_view<CharType>> pathDelimiterInitList) : PrefixIndex(pathDelimiterInitList.begin(), (unsigned int)pathDelimiterInitList.size())
        {
            // Nothing to do here.
        }

        /// Initialization constructor.
        /// Fills a single path delimiter using a string.
        inline PrefixIndex(std::basic_string_view<CharType> pathDelimiter) : PrefixIndex(&pathDelimiter, 1)
        {
            // Nothing to do here.
        }


    private:
        // -------- INSTANCE METHODS --------------------------------------- //

        /// For internal use only.
        /// Attempts to locate the node in the tree that corresponds to the specified path prefix, if it exists and has data.
        /// @param [in] prefix Prefix string for which to search.
        /// @return Pointer to the node if it exists and contains data, `nullptr` otherwise.
        inline Node* MutableFindInternal(std::basic_string_view<CharType> prefix)
        {
            // It is safe to use const_cast here because `this` points to a non-const object.
            return const_cast<Node*>(Find(prefix));
        }

        /// For internal use only.
        /// Creates all nodes needed to ensure the given prefix can be represented by this tree.
        /// @param [in] prefix Prefix string for which a path within the tree needs to exist.
        /// @return Pointer to the node that corresponds to the very last component (i.e. deepest within the tree) of the prefix string.
        Node* PrefixPathCreateInternal(std::basic_string_view<CharType> prefix)
        {
            size_t tokenizeState = 0;
            Node* currentNode = &rootNode;

            for (std::optional<std::basic_string_view<CharType>> maybeNextPathComponent = Strings::TokenizeString<CharType>(tokenizeState, prefix, pathDelimiters.data(), pathDelimiterCount); true == maybeNextPathComponent.has_value(); maybeNextPathComponent = Strings::TokenizeString<CharType>(tokenizeState, prefix, pathDelimiters.data(), pathDelimiterCount))
            {
                if (0 == maybeNextPathComponent.value().length())
                    continue;

                currentNode = currentNode->FindOrEmplaceChild(maybeNextPathComponent.value());
            }

            return currentNode;
        }

        /// For internal use only.
        /// Attempts to traverse the tree to the node that represents the specified prefix.
        /// Nodes returned by this method are not necessarily nodes that are "contained" as prefixes because, while they do exist in the data structure, they may be intermediate nodes (i.e. they may not actually contain any data).
        /// @param [in] prefix Prefix string for which to search.
        /// @return Pointer to the node that corresponds to the very last component (i.e. deepest within the tree) of the prefix string, or `nullptr` if no path exists to the requested prefix.
        const Node* TraverseToInternal(std::basic_string_view<CharType> prefix) const
        {
            size_t tokenizeState = 0;
            const Node* currentNode = &rootNode;

            for (std::optional<std::basic_string_view<CharType>> maybeNextPathComponent = Strings::TokenizeString<CharType>(tokenizeState, prefix, pathDelimiters.data(), pathDelimiterCount); true == maybeNextPathComponent.has_value(); maybeNextPathComponent = Strings::TokenizeString<CharType>(tokenizeState, prefix, pathDelimiters.data(), pathDelimiterCount))
            {
                if (0 == maybeNextPathComponent.value().length())
                    continue;

                const Node* nextPathChildNode = currentNode->FindChild(maybeNextPathComponent.value());
                if (nullptr == nextPathChildNode)
                    return nullptr;

                currentNode = nextPathChildNode;
            }

            return currentNode;
        }

    public:
        /// Determines if the tree contains the specified path prefix.
        /// @param [in] prefix Prefix string for which to search.
        /// @return `true` if a node exists for the given prefix and it contains data, `false` otherwise.
        inline bool Contains(std::basic_string_view<CharType> prefix) const
        {
            return (nullptr != Find(prefix));
        }

        /// Erases the specified path prefix from the tree so that it is no longer considered "contained" within the index.
        /// @param [in] prefix Prefix string to be erased.
        /// @return `true` if a the prefix was located in the index (in which case it was erased by this method), `false` otherwise.
        bool Erase(std::basic_string_view<CharType> prefix)
        {
            Node* node = MutableFindInternal(prefix);
            if (nullptr == node)
                return false;

            node->ClearData();

            while ((false == node->HasData()) && (false == node->HasChildren()) && (true == node->HasParent()))
            {
                std::basic_string_view<CharType> childKeyToErase = node->GetParentKey();
                node = node->GetParent();
                node->EraseChild(childKeyToErase);
            }

            return true;
        }

        /// Attempts to locate the node in the tree that corresponds to the specified path prefix, if it exists and has data.
        /// @param [in] prefix Prefix string for which to search.
        /// @return Pointer to the node if it exists and contains data, `nullptr` otherwise.
        const Node* Find(std::basic_string_view<CharType> prefix) const
        {
            const Node* const node = TraverseToInternal(prefix);

            if ((nullptr == node) || (false == node->HasData()))
                return nullptr;

            return node;
        }

        /// Creates any nodes needed to represent the specified prefix and then inserts a new prefix data element.
        /// No changes are made if the prefix already exists within the tree.
        /// @param [in] prefix Prefix string for which data is to be inserted.
        /// @param [in] data Read-only reference to the data to be associated with the specified prefix.
        /// @return Pair consisting of a pointer to the node that corresponds to the very last component (i.e. deepest within the tree) of the prefix string and a Boolean value (`true` if the tree was modified, `false` if not).
        std::pair<const Node*, bool> Insert(std::basic_string_view<CharType> prefix, const DataType& data)
        {
            Node* const node = PrefixPathCreateInternal(prefix);

            if (true == node->HasData())
                return std::make_pair(node, false);

            node->SetData(&data);
            return std::make_pair(node, true);
        }

        /// Attempts to locate the longest matching prefix within this prefix index tree and returns a pointer to the corresponding node.
        /// @param [in] stringToMatch Delimited string for which the longest matching prefix is desired.
        /// @return Pointer to the node if it exists and contains data, `nullptr` otherwise.
        const Node* LongestMatchingPrefix(std::basic_string_view<CharType> stringToMatch) const
        {
            size_t tokenizeState = 0;

            const Node* currentNode = &rootNode;
            const Node* longestMatchingPrefixNode = nullptr;

            for (std::optional<std::basic_string_view<CharType>> maybeNextPathComponent = Strings::TokenizeString<CharType>(tokenizeState, stringToMatch, pathDelimiters.data(), pathDelimiterCount); true == maybeNextPathComponent.has_value(); maybeNextPathComponent = Strings::TokenizeString<CharType>(tokenizeState, stringToMatch, pathDelimiters.data(), pathDelimiterCount))
            {
                if (0 == maybeNextPathComponent.value().length())
                    continue;

                if (true == currentNode->HasData())
                    longestMatchingPrefixNode = currentNode;

                const Node* nextPathChildNode = currentNode->FindChild(maybeNextPathComponent.value());
                if (nullptr == nextPathChildNode)
                    break;

                currentNode = nextPathChildNode;
            }

            if (true == currentNode->HasData())
                longestMatchingPrefixNode = currentNode;

            return longestMatchingPrefixNode;
        }

        /// Updates the data associated with the specified prefix.
        /// If the prefix does not already exist within the tree it is inserted as if #Insert were invoked.
        /// @param [in] prefix Prefix string for which data is to be inserted.
        /// @param [in] data Read-only reference to the data to be associated with the specified prefix.
        /// @return Pointer to the node that corresponds to the very last component (i.e. deepest within the tree) of the prefix string.
        const Node* Update(std::basic_string_view<CharType> prefix, const DataType& data)
        {
            Node* const node = PrefixPathCreateInternal(prefix);
            node->SetData(&data);
            return node;
        }
    };
}
