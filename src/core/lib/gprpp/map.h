/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_GPRPP_MAP_H
#define GRPC_CORE_LIB_GPRPP_MAP_H

#include <grpc/support/port_platform.h>

#include <string.h>
#include <functional>
#include <iterator>
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/pair.h"

namespace grpc_core {
struct StringLess {
  bool operator()(const char* a, const char* b) const {
    return strcmp(a, b) < 0;
  }
};

namespace testing {
template <class Key, class T, class Compare>
class MapTester;
}

// Alternative map implementation for grpc_core
template <class Key, class T, class Compare = std::less<Key>>
class Map {
  struct Entry;

 public:
  typedef Key key_type;
  typedef T mapped_type;
  typedef Pair<key_type, mapped_type> value_type;
  typedef Compare key_compare;

  class iterator : public std::iterator<std::input_iterator_tag, value_type,
                                        int32_t, value_type*, value_type&> {
    using GrpcMap = typename ::grpc_core::Map<Key, T, Compare>;
    using GrpcMapEntry = typename ::grpc_core::Map<Key, T, Compare>::Entry;

   public:
    bool operator==(const iterator& rhs) const { return (curr_ == rhs.curr_); }
    bool operator!=(const iterator& rhs) const { return (curr_ != rhs.curr_); }

    iterator& operator++() {
      curr_ = map_->InOrderSuccessor(curr_);
      return *this;
    }

    iterator& operator++(int e) {
      curr_ = map_->InOrderSuccessor(curr_);
      return *this;
    }

    // operator*()
    value_type& operator*() { return curr_->pair; }
    const value_type& operator*() const { return curr_->pair; }

    // operator->()
    value_type* operator->() { return &curr_->pair; }
    value_type const* operator->() const { return &curr_->pair; }

   private:
    iterator(GrpcMap* map, GrpcMapEntry* curr) : curr_(curr), map_(map) {}
    GrpcMapEntry* curr_;
    GrpcMap* map_;
    friend class Map<key_type, mapped_type, Compare>;
  };

  ~Map() { clear(); }

  T& operator[](key_type&& key) {
    if (find(key) == end())
      return insert(MakePair(std::move(key), std::move(T()))).first->second;
    return find(key)->second;
  }

  T& operator[](const key_type& key) {
    if (find(key) == end()) return insert(MakePair(key, T())).first->second;
    return find(key)->second;
  }

  iterator find(const key_type& k) {
    Entry* iter = root_;
    while (iter != nullptr) {
      int comp = CompareKeys(iter->pair.first, k);
      if (!comp) {
        return iterator(this, iter);
      } else if (comp < 0) {
        iter = iter->right;
      } else {
        iter = iter->left;
      }
    }
    return end();
  }

  size_t erase(const key_type& key) {
    if (find(key) == end()) return 0;
    root_ = RemoveRecursive(root_, key);
    return 1;
  }

  // Removes the current entry and points to the next one
  iterator erase(iterator iter) {
    key_type& del_key = iter->first;
    iter++;
    root_ = RemoveRecursive(root_, del_key);
    return iter;
  }

  Pair<iterator, bool> insert(value_type&& pair) {
    iterator ret = find(pair.first);
    bool insertion = false;
    if (ret == end()) {
      Pair<iterator, Entry*> p = InsertRecursive(root_, std::move(pair));
      root_ = p.second;
      ret = p.first;
      insertion = true;
    }
    return MakePair<iterator, bool>(std::move(ret), std::move(insertion));
  }

  Pair<iterator, bool> insert(const value_type& pair) {
    iterator ret = find(pair.first);
    bool insertion = false;
    if (ret == end()) {
      Pair<iterator, Entry*> p = InsertRecursive(root_, pair);
      root_ = p.second;
      ret = p.first;
      insertion = true;
    }
    return MakePair<iterator, bool>(std::move(ret), std::move(insertion));
  }

  template <class... Args>
  Pair<iterator, bool> emplace(Args&&... args) {
    return insert(MakePair<key_type, mapped_type>(std::forward<Args>(args)...));
  }

  bool empty() { return root_ == nullptr; }

  void clear() {
    auto iter = begin();
    while (!empty()) {
      iter = erase(iter);
    }
  }

  iterator begin() {
    Entry* curr = GetMinEntry(root_);
    return iterator(this, curr);
  }

  iterator end() { return iterator(this, nullptr); }

 private:
  struct Entry {
   public:
    explicit Entry(value_type&& pair) : pair(std::move(pair)) {}
    value_type pair;
    Entry* left = nullptr;
    Entry* right = nullptr;
    int32_t height = 1;

    friend class iterator;
    friend class testing::MapTester<Key, T, Compare>;
  };

  int32_t EntryHeight(Entry* e) { return e == nullptr ? 0 : e->height; }

  Entry* GetMinEntry(Entry* e) {
    if (e != nullptr) {
      while (e->left != nullptr) {
        e = e->left;
      }
    }
    return e;
  }

  Entry* InOrderSuccessor(Entry* e) {
    if (e->right != nullptr) {
      return GetMinEntry(e->right);
    }
    Entry* successor = nullptr;
    Entry* iter = root_;
    while (iter != nullptr) {
      int comp = CompareKeys(iter->pair.first, e->pair.first);
      if (comp > 0) {
        successor = iter;
        iter = iter->left;
      } else if (comp < 0) {
        iter = iter->right;
      } else
        break;
    }
    return successor;
  }

  Entry* RotateLeft(Entry* e) {
    Entry* rightChild = e->right;
    Entry* rightLeftChild = rightChild->left;
    rightChild->left = e;
    e->right = rightLeftChild;
    e->height = 1 + GPR_MAX(EntryHeight(e->left), EntryHeight(e->right));
    rightChild->height = 1 + GPR_MAX(EntryHeight(rightChild->left),
                                     EntryHeight(rightChild->right));
    return rightChild;
  }

  Entry* RotateRight(Entry* e) {
    Entry* leftChild = e->left;
    Entry* leftRightChild = leftChild->right;
    leftChild->right = e;
    e->left = leftRightChild;
    e->height = 1 + GPR_MAX(EntryHeight(e->left), EntryHeight(e->right));
    leftChild->height = 1 + GPR_MAX(EntryHeight(leftChild->left),
                                    EntryHeight(leftChild->right));
    return leftChild;
  }

  Entry* RebalanceTreeAfterInsertion(Entry* root, key_type& k) {
    root->height =
        1 + GPR_MAX(EntryHeight(root->left), EntryHeight(root->right));
    int32_t heightDifference =
        EntryHeight(root->left) - EntryHeight(root->right);
    if (heightDifference > 1 && CompareKeys(root->left->pair.first, k) > 0) {
      return RotateRight(root);
    }
    if (heightDifference < -1 && CompareKeys(root->right->pair.first, k) < 0) {
      return RotateLeft(root);
    }
    if (heightDifference > 1 && CompareKeys(root->left->pair.first, k) < 0) {
      root->left = RotateLeft(root->left);
      return RotateRight(root);
    }
    if (heightDifference < -1 && CompareKeys(root->right->pair.first, k) > 0) {
      root->right = RotateRight(root->right);
      return RotateLeft(root);
    }
    return root;
  }

  Entry* RebalanceTreeAfterDeletion(Entry* root) {
    root->height =
        1 + GPR_MAX(EntryHeight(root->left), EntryHeight(root->right));
    int32_t heightDifference =
        EntryHeight(root->left) - EntryHeight(root->right);
    if (heightDifference > 1) {
      int leftHeightDifference =
          EntryHeight(root->left->left) - EntryHeight(root->left->right);
      if (leftHeightDifference < 0) {
        root->left = RotateLeft(root->left);
      }
      return RotateRight(root);
    }
    if (heightDifference < -1) {
      int rightHeightDifference =
          EntryHeight(root->right->left) - EntryHeight(root->right->right);
      if (rightHeightDifference > 0) {
        root->right = RotateRight(root->right);
      }
      return RotateLeft(root);
    }
    return root;
  }

  Pair<iterator, Entry*> InsertRecursive(Entry* root, value_type&& p) {
    if (root == nullptr) {
      Entry* e = New<Entry>(std::move(p));
      return MakePair(iterator(this, e), std::move(e));
    }
    int comp = CompareKeys(root->pair.first, p.first);
    if (comp > 0) {
      Pair<iterator, Entry*> ret = InsertRecursive(root->left, std::move(p));
      root->left = ret.second;
      ret.second = std::move(RebalanceTreeAfterInsertion(root, p.first));
      return ret;
    } else if (comp < 0) {
      Pair<iterator, Entry*> ret = InsertRecursive(root->right, std::move(p));
      root->right = ret.second;
      ret.second = std::move(RebalanceTreeAfterInsertion(root, p.first));
      return ret;
    } else {
      root->pair = std::move(p);
      return MakePair(iterator(this, root), std::move(root));
    }
  }

  Entry* RemoveRecursive(Entry* root, const key_type& k) {
    if (root == nullptr) return root;
    int comp = CompareKeys(root->pair.first, k);
    if (comp > 0) {
      root->left = RemoveRecursive(root->left, k);
    } else if (comp < 0) {
      root->right = RemoveRecursive(root->right, k);
    } else {
      Entry* ret;
      if (root->left == nullptr) {
        ret = root->right;
        grpc_core::Delete(root);
        return ret;
      } else if (root->right == nullptr) {
        ret = root->left;
        grpc_core::Delete(root);
        return ret;
      } else {
        ret = root->right;
        while (ret->left != nullptr) {
          ret = ret->left;
        }
        root->pair.swap(ret->pair);
        root->right = RemoveRecursive(root->right, ret->pair.first);
      }
    }
    return RebalanceTreeAfterDeletion(root);
  }

  /* Return 0 if lhs = rhs
   *        1 if lhs > rhs
   *       -1 if lhs < rhs
   */
  int CompareKeys(const Key& lhs, const Key& rhs) {
    key_compare compare;
    bool left_comparison = compare(lhs, rhs);
    bool right_comparison = compare(rhs, lhs);
    // Both values are equal
    if (!left_comparison && !right_comparison) {
      return 0;
    }
    return left_comparison ? -1 : 1;
  }

  Entry* root_ = nullptr;
  friend class testing::MapTester<Key, T, Compare>;
};
}  // namespace grpc_core
#endif /* GRPC_CORE_LIB_GPRPP_MAP_H */
