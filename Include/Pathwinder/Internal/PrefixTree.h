/***************************************************************************************************
 * Pathwinder
 *   Path redirection for files, directories, and registry entries.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2022-2024
 ***********************************************************************************************//**
 * @file PrefixTree.h
 *   Declaration and implementation of a data structure efficiently traversable using prefixes
 *   in delimited strings.
 **************************************************************************************************/

#pragma once

#include <initializer_list>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <Infra/ArrayList.h>
#include <Infra/Strings.h>
#include <Infra/TemporaryBuffer.h>

namespace Pathwinder
{
  /// Data structure for indexing objects identified by delimited strings for efficient traversal
  /// by prefix. Implemented as a prefix tree where each level represents a token within the
  /// delimited string.
  /// @tparam CharType Type of character in each string, either narrow or wide.
  /// @tparam DataType Type of data that can be stored at each node in the prefix tree.
  /// @tparam Hasher Optional hasher for strings stored internally. Defaults to the standard
  /// case-sensitive hasher for string view objects.
  /// @tparam EqualityComparator Optional equality comparator for strings stored internally.
  /// Defaults to the standard case-sensitive equality comparator for string view objects.
  template <
      typename CharType,
      typename DataType,
      typename Hasher = std::hash<std::basic_string_view<CharType>>,
      typename EqualityComparator = std::equal_to<std::basic_string_view<CharType>>>
  class PrefixTree
  {
  public:

    /// Convenience type aliases.
    using TChar = CharType;
    using TStringView = std::basic_string_view<CharType>;
    using TData = DataType;
    using TDataPtr = DataType*;
    using TDataRef = DataType&;
    using THash = Hasher;
    using TEquals = EqualityComparator;

    /// Individual node within the prefix tree.
    class Node
    {
    public:

      /// Type alias for the container that holds this node's children.
      using TChildrenContainer = std::unordered_map<TStringView, Node, THash, TEquals>;

      inline Node(Node* parent, TStringView parentKey)
          : data(), parent(parent), parentKey(parentKey), children()
      {}

      Node(const Node&) = delete;

      inline Node(Node&& other) noexcept
          : data(std::move(other.data)),
            parent(std::move(other.parent)),
            parentKey(std::move(other.parentKey)),
            children(std::move(other.children))
      {}

      Node& operator=(const Node&) = delete;

      Node& operator=(Node&& other) = default;

      /// Clears the data associated with this node.
      inline void ClearData(void)
      {
        data = std::nullopt;
      }

      /// Provides access to the data contained within this node without first verifying that it
      /// exists. This method is only for changing the stored data. It cannot be used for clearing
      /// it entirely or adding data to a node that does not already contain data.
      /// @return Mutable reference to the stored node data.
      inline TData& Data(void) const
      {
        return *data;
      }

      /// Updates the optional data stored within this node by constructing a new data object in
      /// place using perfect forwarding.
      template <typename... Args> inline void EmplaceData(Args&&... args)
      {
        data.emplace(std::forward<Args>(args)...);
      }

      /// Removes a child of this node.
      /// This will delete not only the child but also all of its children.
      /// @param [in] childKey Path prefix portion to use as a search key for a child node.
      void EraseChild(TStringView childKey)
      {
        auto childIter = children.find(childKey);
        if (children.end() == childIter) return;

        children.erase(childIter);
      }

      /// Locates and returns a pointer to the child node corresponding to the given path
      /// prefix portion.
      /// @param [in] childKey Path prefix portion to use as a search key for a child node.
      /// @return Pointer to the child node if it exists, `nullptr` otherwise.
      const Node* FindChild(TStringView childKey) const
      {
        const auto childIter = children.find(childKey);
        if (children.cend() == childIter) return nullptr;

        return &(childIter->second);
      }

      /// Either retrieves and returns a pointer to an existing child node or creates one.
      /// @param [in] childKey Path prefix portion to use as a key for locating or creating a
      /// child node.
      /// @return Pointer to the child node.
      inline Node* FindOrEmplaceChild(TStringView childKey)
      {
        return &(children.emplace(childKey, Node(this, childKey)).first->second);
      }

      /// Traverses up the tree via parent node pointers and checks all the nodes encountered
      /// for whether or not they contain any data. Returns a pointer to the first node
      /// encountered that contains data.
      /// @return Pointer to the closest ancestor node, or `nullptr` if no such node exists.
      const Node* GetClosestAncestor(void) const
      {
        for (const Node* currentNode = GetParent(); nullptr != currentNode;
             currentNode = currentNode->GetParent())
        {
          if (currentNode->HasData()) return currentNode;
        }

        return nullptr;
      }

      /// Retrieves a read-only reference to the container holding all of this node's
      /// children.
      /// @return Read-only reference to this node's children.
      inline const TChildrenContainer& GetChildren(void) const
      {
        return children;
      }

      /// Provides read-only access to the data contained within this node without first verifying
      /// that it exists.
      /// @return Read-only reference to stored node data.
      inline const TData& GetData(void) const
      {
        return *data;
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

      /// Retrieves the portion of the path that corresponds to the edge from the parent node
      /// to this node.
      /// @return Key within the parent node's children data structure that corresponds to
      /// this node.
      inline TStringView GetParentKey(void) const
      {
        return parentKey;
      }

      /// Determines if this node has any ancestors.
      /// Traverses up the tree via parent node pointers and checks all the nodes encountered
      /// for whether or not they contain any data.
      /// @return `true` if a node is encountered containing data, `false` otherwise.
      inline bool HasAncestor(void) const
      {
        return (nullptr != GetClosestAncestor());
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
        return data.has_value();
      }

      /// Determines if this node contains data.
      /// @return `true` if so, `false` if not.
      inline bool HasParent(void) const
      {
        return (nullptr != parent);
      }

      /// Updates the optional data stored within this node using copy semantics.
      /// @param [in] newData New data to be stored within this node.
      inline void SetData(const TData& newData)
      {
        data = newData;
      }

      /// Updates the optional data stored within this node using move semantics.
      /// @param [in] newData New data to be stored within this node.
      inline void SetData(TData&& newData)
      {
        data = std::move(newData);
      }

    private:

      /// Optional data associated with the node. If present, the path prefix string up to this
      /// point is considered "contained" in the tree data structure. Can be mutated even when the
      /// node itself is constant because no part of the data structure depends on the value of the
      /// data. Methods that allow access to this field properly ensure data structure clients
      /// cannot create or clear the data but can update the value stored, if present.
      mutable std::optional<TData> data;

      /// Parent node, one level up in the tree. Cannot be used to modify the tree.
      Node* parent;

      /// Key within the parent node's child map that is associated with this node.
      TStringView parentKey;

      /// Child nodes, stored associatively by path prefix string.
      TChildrenContainer children;
    };

    /// Maximum number of path delimiter strings allowed in a path prefix tree.
    static constexpr unsigned int kMaxDelimiters = 4;

    /// Default constructor uses a standard backslash delimiter for filesystem paths.
    inline PrefixTree(void) : PrefixTree(L"\\") {}

    /// Fills path delimiters using an array and a count. An array-out-of-bounds error will
    /// occur if the number of delimiters is too high.
    PrefixTree(const TStringView* pathDelimiterArray, unsigned int pathDelimiterArrayCount)
        : rootNode(nullptr, TStringView()),
          pathDelimiters(pathDelimiterArray, pathDelimiterArrayCount)
    {}

    inline PrefixTree(std::initializer_list<TStringView> pathDelimiterInitList)
        : PrefixTree(
              pathDelimiterInitList.begin(),
              static_cast<unsigned int>(pathDelimiterInitList.size()))
    {}

    inline PrefixTree(TStringView pathDelimiter) : PrefixTree(&pathDelimiter, 1) {}

    PrefixTree(const PrefixTree&) = delete;

    PrefixTree(PrefixTree&& other) = default;

    PrefixTree& operator=(const PrefixTree&) = delete;

    PrefixTree& operator=(PrefixTree&& other) = default;

    /// Determines if the tree contains the specified path prefix.
    /// @param [in] prefix Prefix string for which to search.
    /// @return `true` if a node exists for the given prefix and it contains data, `false`
    /// otherwise.
    inline bool Contains(TStringView prefix) const
    {
      return (nullptr != Find(prefix));
    }

    /// Creates any nodes needed to represent the specified prefix and then inserts a new prefix
    /// data element by constructing it in place using perfect forwarding. No changes are made, and
    /// no construction takes place, if the prefix already exists within the tree.
    /// @param [in] prefix Prefix string for which data is to be inserted.
    /// @param [in] data Reference to the data to be associated with the specified prefix.
    /// @return Pair consisting of a pointer to the node that corresponds to the very last
    /// component (i.e. deepest within the tree) of the prefix string and a Boolean value
    /// (`true` if the tree was modified, `false` if not).
    template <typename... Args> std::pair<const Node*, bool> Emplace(
        TStringView prefix, Args&&... args)
    {
      Node* const node = PrefixPathCreateInternal(prefix);
      if (true == node->HasData()) return std::make_pair(node, false);
      node->EmplaceData(std::forward<Args>(args)...);
      return std::make_pair(node, true);
    }

    /// Erases the specified path prefix from the tree so that it is no longer considered
    /// "contained" within the index.
    /// @param [in] prefix Prefix string to be erased.
    /// @return `true` if a the prefix was located in the index (in which case it was erased by
    /// this method), `false` otherwise.
    bool Erase(TStringView prefix)
    {
      Node* node = MutableFindInternal(prefix);
      if (nullptr == node) return false;

      node->ClearData();

      while ((false == node->HasData()) && (false == node->HasChildren()) &&
             (true == node->HasParent()))
      {
        TStringView childKeyToErase = node->GetParentKey();
        node = node->GetParent();
        node->EraseChild(childKeyToErase);
      }

      return true;
    }

    /// Attempts to locate the node in the tree that corresponds to the specified path prefix,
    /// if it exists and has data.
    /// @param [in] prefix Prefix string for which to search.
    /// @return Pointer to the node if it exists and contains data, `nullptr` otherwise.
    const Node* Find(TStringView prefix) const
    {
      const Node* const node = TraverseTo(prefix);

      if ((nullptr == node) || (false == node->HasData())) return nullptr;

      return node;
    }

    /// Determines if the specified prefix exists as a valid path in the prefix index.
    /// If this method returns `true` then objects exist in this index beginning with, but not
    /// necessarily existing exactly at, the specified prefix.
    /// @param [in] prefix Prefix string for which to search.
    /// @return `true` if a path exists with the specified prefix, `false` otherwise.
    inline bool HasPathForPrefix(TStringView prefix) const
    {
      return (nullptr != TraverseTo(prefix));
    }

    /// Creates any nodes needed to represent the specified prefix and then inserts a new prefix
    /// data element using copy semantics. No changes are made if the prefix already exists within
    /// the tree.
    /// @param [in] prefix Prefix string for which data is to be inserted.
    /// @param [in] data Reference to the data to be associated with the specified prefix.
    /// @return Pair consisting of a pointer to the node that corresponds to the very last
    /// component (i.e. deepest within the tree) of the prefix string and a Boolean value
    /// (`true` if the tree was modified, `false` if not).
    std::pair<const Node*, bool> Insert(TStringView prefix, const TData& data)
    {
      Node* const node = PrefixPathCreateInternal(prefix);
      if (true == node->HasData()) return std::make_pair(node, false);
      node->SetData(data);
      return std::make_pair(node, true);
    }

    /// Creates any nodes needed to represent the specified prefix and then inserts a new prefix
    /// data element using move semantics. No changes are made if the prefix already exists within
    /// the tree.
    /// @param [in] prefix Prefix string for which data is to be inserted.
    /// @param [in] data Reference to the data to be associated with the specified prefix.
    /// @return Pair consisting of a pointer to the node that corresponds to the very last
    /// component (i.e. deepest within the tree) of the prefix string and a Boolean value
    /// (`true` if the tree was modified, `false` if not).
    std::pair<const Node*, bool> Insert(TStringView prefix, TData&& data)
    {
      Node* const node = PrefixPathCreateInternal(prefix);
      if (true == node->HasData()) return std::make_pair(node, false);
      node->SetData(std::move(data));
      return std::make_pair(node, true);
    }

    /// Attempts to locate the longest matching prefix within this prefix index tree and returns
    /// a pointer to the corresponding node.
    /// @param [in] stringToMatch Delimited string for which the longest matching prefix is
    /// desired.
    /// @return Pointer to the node if it exists and contains data, `nullptr` otherwise.
    const Node* LongestMatchingPrefix(TStringView stringToMatch) const
    {
      size_t tokenizeState = 0;

      const Node* currentNode = &rootNode;
      const Node* longestMatchingPrefixNode = nullptr;

      for (std::optional<TStringView> maybeNextPathComponent = Infra::Strings::Tokenize<TChar>(
               tokenizeState, stringToMatch, pathDelimiters.Data(), pathDelimiters.Size());
           true == maybeNextPathComponent.has_value();
           maybeNextPathComponent = Infra::Strings::Tokenize<TChar>(
               tokenizeState, stringToMatch, pathDelimiters.Data(), pathDelimiters.Size()))
      {
        if (0 == maybeNextPathComponent->length()) continue;

        if (true == currentNode->HasData()) longestMatchingPrefixNode = currentNode;

        const Node* nextPathChildNode = currentNode->FindChild(*maybeNextPathComponent);
        if (nullptr == nextPathChildNode) break;

        currentNode = nextPathChildNode;
      }

      if (true == currentNode->HasData()) longestMatchingPrefixNode = currentNode;

      return longestMatchingPrefixNode;
    }

    /// Attempts to traverse the tree to the node that represents the specified prefix.
    /// Nodes returned by this method are not necessarily nodes that are "contained" as prefixes
    /// because, while they do exist in the data structure, they may be intermediate nodes (i.e.
    /// they may not actually contain any data).
    /// @param [in] prefix Prefix string for which to search.
    /// @return Pointer to the node that corresponds to the very last component (i.e. deepest
    /// within the tree) of the prefix string, or `nullptr` if no path exists to the requested
    /// prefix.
    const Node* TraverseTo(TStringView prefix) const
    {
      const Node* currentNode = &rootNode;

      for (TStringView pathComponent :
           Infra::Strings::Tokenizer(prefix, pathDelimiters.Data(), pathDelimiters.Size()))
      {
        if (0 == pathComponent.length()) continue;

        const Node* nextPathChildNode = currentNode->FindChild(pathComponent);
        if (nullptr == nextPathChildNode) return nullptr;

        currentNode = nextPathChildNode;
      }

      return currentNode;
    }

    /// Updates the data associated with the specified prefix using copy semantics. If the prefix
    /// does not already exist within the tree then it is inserted, otherwise it is updated with the
    /// new data.
    /// @param [in] prefix Prefix string for which data is to be inserted.
    /// @param [in] data Reference to the data to be associated with the specified prefix.
    /// @return Pointer to the node that corresponds to the very last component (i.e. deepest
    /// within the tree) of the prefix string.
    const Node* Update(TStringView prefix, const TData& data)
    {
      Node* const node = PrefixPathCreateInternal(prefix);
      node->SetData(data);
      return node;
    }

    /// Updates the data associated with the specified prefix using move semantics. If the prefix
    /// does not already exist within the tree then it is inserted, otherwise it is updated with the
    /// new data.
    /// @param [in] prefix Prefix string for which data is to be inserted.
    /// @param [in] data Reference to the data to be associated with the specified prefix.
    /// @return Pointer to the node that corresponds to the very last component (i.e. deepest
    /// within the tree) of the prefix string.
    const Node* Update(TStringView prefix, TData&& data)
    {
      Node* const node = PrefixPathCreateInternal(prefix);
      node->SetData(std::move(data));
      return node;
    }

  private:

    /// Attempts to locate the node in the tree that corresponds to the specified path prefix,
    /// if it exists and has data.
    /// @param [in] prefix Prefix string for which to search.
    /// @return Pointer to the node if it exists and contains data, `nullptr` otherwise.
    inline Node* MutableFindInternal(TStringView prefix)
    {
      // It is safe to use const_cast here because `this` points to a non-const object.
      return const_cast<Node*>(Find(prefix));
    }

    /// Creates all nodes needed to ensure the given prefix can be represented by this tree.
    /// @param [in] prefix Prefix string for which a path within the tree needs to exist.
    /// @return Pointer to the node that corresponds to the very last component (i.e. deepest
    /// within the tree) of the prefix string.
    Node* PrefixPathCreateInternal(TStringView prefix)
    {
      Node* currentNode = &rootNode;

      for (TStringView pathComponent :
           Infra::Strings::Tokenizer(prefix, pathDelimiters.Data(), pathDelimiters.Size()))
      {
        if (true == pathComponent.empty()) continue;

        currentNode = currentNode->FindOrEmplaceChild(pathComponent);
      }

      return currentNode;
    }

    /// Root node of the path prefix tree data structure. Will only ever contain children, no data
    /// or parents.
    Node rootNode;

    /// Delimiters that act as delimiters between components of path strings. Immutable once this
    /// object is created.
    Infra::ArrayList<TStringView, kMaxDelimiters> pathDelimiters;
  };
} // namespace Pathwinder
