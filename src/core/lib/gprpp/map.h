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

#include <functional>
#include <utility>
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/pair.h"
namespace grpc_core {
template <class Key>
struct less {
  bool operator()(Key lhs, Key rhs) const {
    if (lhs < rhs) return true;
  }
};

template <class Key, class T, class Compare>
class MapTester;

// Alternative map implementation for grpc_core
template <class Key, class T, class Compare = less<Key>>
class map {
  typedef Key key_type;
  typedef T mapped_type;
  typedef pair<key_type, mapped_type> value_type;
  typedef Compare key_compare;
  class Entry;

 public:
  class iterator {
   public:
    iterator(map<key_type, mapped_type, Compare>* map,
             ::grpc_core::map<key_type, mapped_type, Compare>::Entry* curr)
        : curr_(curr), map_(map) {}

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
    value_type& operator*() { return curr_->pair(); }
    const value_type& operator*() const { return curr_->pair(); }

    // operator->()
    value_type* operator->() { return &curr_->pair(); }

    value_type const* operator->() const { return &curr_->pair(); }

   private:
    Entry* curr_;
    map* map_;
  };

  ~map() { clear(); }

  T& operator[](key_type&& key) {
    if (find(key) == end())
      return insert(make_pair(std::move(key), std::move(T()))).first->second;
    return find(key)->second;
  }

  T& operator[](const key_type&& key) {
    if (find(key) == end())
      return insert(make_pair(std::move(key), std::move(T()))).first->second;
    return find(key)->second;
  }

  iterator find(key_type k) {
    Entry* iter = root_;
    while (iter != nullptr) {
      int comp = CompareKeys(iter->pair().first, k);
      if (!comp) {
        return iterator(this, iter);
      } else if (comp < 0) {
        iter = iter->right();
      } else {
        iter = iter->left();
      }
    }
    return end();
  }

  int erase(const key_type& key) {
    if (find(key) == end()) return 0;
    root_ = RemoveRecursive(root_, key);
    return 1;
  }

  // Removes the current entry and points to the next one
  iterator erase(iterator iter) {
    key_type delKey = iter->first;
    iter++;
    root_ = RemoveRecursive(root_, delKey);
    return iter;
  }

  pair<iterator, bool> insert(value_type&& pair) {
    iterator ret = find(pair.first);
    bool insertion = false;
    if (ret == end()) {
      root_ = InsertRecursive(root_, std::move(pair));
      ret = find(pair.first);
      insertion = true;
    }
    return make_pair<iterator, bool>(std::move(ret), std::move(insertion));
  }

  pair<iterator, bool> emplace(key_type&& k, mapped_type&& v) {
    return insert(grpc_core::make_pair<key_type, mapped_type>(std::move(k),
                                                              std::move(v)));
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
  class Entry {
   public:
    Entry(value_type pair)
        : pair_(std::move(pair)), left_(nullptr), right_(nullptr), height_(1) {}

    value_type& pair() { return pair_; }
    Entry* left() { return left_; }
    Entry* right() { return right_; }
    long height() { return height_; }
    void set_pair(value_type pair) { pair_ = std::move(pair); }
    void set_left(Entry* left) { left_ = left; }
    void set_right(Entry* right) { right_ = right; }
    void set_height(long height) { height_ = height; }

   private:
    value_type pair_;
    Entry* left_;
    Entry* right_;
    long height_;

    friend class iterator;
    friend class MapTester<Key, T, Compare>;
  };

  Entry* root() { return root_; }

  long EntryHeight(Entry* e) { return e == nullptr ? 0 : e->height(); }

  Entry* GetMinEntry(Entry* e) {
    if (e != nullptr) {
      while (e->left() != nullptr) {
        e = e->left();
      }
    }
    return e;
  }

  Entry* InOrderSuccessor(Entry* e) {
    if (e->right() != nullptr) {
      return GetMinEntry(e->right());
    }
    Entry* successor = nullptr;
    Entry* iter = root();
    while (iter != nullptr) {
      int comp = CompareKeys(iter->pair().first, e->pair().first);
      if (comp > 0) {
        successor = iter;
        iter = iter->left();
      } else if (comp < 0) {
        iter = iter->right();
      } else
        break;
    }
    return successor;
  }

  Entry* RotateLeft(Entry* e) {
    Entry* rightChild = e->right();
    Entry* rightLeftChild = rightChild->left();
    rightChild->set_left(e);
    e->set_right(rightLeftChild);
    e->set_height(1 + GPR_MAX(EntryHeight(e->left()), EntryHeight(e->right())));
    rightChild->set_height(1 + GPR_MAX(EntryHeight(rightChild->left()),
                                       EntryHeight(rightChild->right())));
    return rightChild;
  }

  Entry* RotateRight(Entry* e) {
    Entry* leftChild = e->left();
    Entry* leftRightChild = leftChild->right();
    leftChild->set_right(e);
    e->set_left(leftRightChild);
    e->set_height(1 + GPR_MAX(EntryHeight(e->left()), EntryHeight(e->right())));
    leftChild->set_height(1 + GPR_MAX(EntryHeight(leftChild->left()),
                                      EntryHeight(leftChild->right())));
    return leftChild;
  }

  Entry* RebalanceTreeAfterInsertion(Entry* root, key_type k) {
    root->set_height(
        1 + GPR_MAX(EntryHeight(root->left()), EntryHeight(root->right())));
    int heightDifference =
        EntryHeight(root->left()) - EntryHeight(root->right());
    if (heightDifference > 1 &&
        CompareKeys(root->left()->pair().first, k) > 0) {
      return RotateRight(root);
    }
    if (heightDifference < -1 &&
        CompareKeys(root->right()->pair().first, k) < 0) {
      return RotateLeft(root);
    }
    if (heightDifference > 1 &&
        CompareKeys(root->left()->pair().first, k) < 0) {
      root->set_left(RotateLeft(root->left()));
      return RotateRight(root);
    }
    if (heightDifference < -1 &&
        CompareKeys(root->left()->pair().first, k) > 0) {
      root->set_right(RotateRight(root->right()));
      return RotateLeft(root);
    }
    return root;
  }

  Entry* RebalanceTreeAfterDeletion(Entry* root) {
    root->set_height(
        1 + GPR_MAX(EntryHeight(root->left()), EntryHeight(root->right())));
    int heightDifference =
        EntryHeight(root->left()) - EntryHeight(root->right());
    if (heightDifference > 1) {
      int leftHeightDifference = EntryHeight(root->left()->left()) -
                                 EntryHeight(root->left()->right());
      if (leftHeightDifference < 0) {
        root->set_left(RotateLeft(root->left()));
      }
      return RotateRight(root);
    }
    if (heightDifference < -1) {
      int rightHeightDifference = EntryHeight(root->right()->left()) -
                                  EntryHeight(root->right()->right());
      if (rightHeightDifference > 0) {
        root->set_right(RotateRight(root->right()));
      }
      return RotateLeft(root);
    }
    return root;
  }

  Entry* InsertRecursive(Entry* root, value_type p) {
    if (root == nullptr) {
      return New<Entry>(std::move(p));
    }
    int comp = CompareKeys(root->pair().first, p.first);
    if (comp > 0) {
      root->set_left(InsertRecursive(root->left(), std::move(p)));
    } else if (comp < 0) {
      root->set_right(InsertRecursive(root->right(), std::move(p)));
    } else {
      root->set_pair(std::move(p));
      return root;
    }
    return RebalanceTreeAfterInsertion(root, p.first);
  }

  Entry* RemoveRecursive(Entry* root, key_type k) {
    if (root == nullptr) return root;
    int comp = CompareKeys(root->pair().first, k);
    if (comp > 0) {
      root->set_left(RemoveRecursive(root->left(), k));
    } else if (comp < 0) {
      root->set_right(RemoveRecursive(root->right(), k));
    } else {
      Entry* ret;
      if (root->left() == nullptr) {
        ret = root->right();
        grpc_core::Delete(root);
        return ret;
      } else if (root->right() == nullptr) {
        ret = root->left();
        grpc_core::Delete(root);
        return ret;
      } else {
        ret = root->right();
        while (ret->left() != nullptr) {
          ret = ret->left();
        }
        root->pair().swap(ret->pair());
        root->set_right(RemoveRecursive(root->right(), ret->pair().first));
      }
    }
    return RebalanceTreeAfterDeletion(root);
  }

  /* Return 0 if lhs = rhs
   *        1 if lhs > rhs
   *       -1 if lhs < rhs
   */
  int CompareKeys(Key lhs, Key rhs) {
    // Both values are equal
    if (!key_compare_(lhs, rhs) && !key_compare_(rhs, lhs)) {
      return 0;
    }
    return key_compare_(lhs, rhs) ? -1 : 1;
  }

  key_compare key_compare_;
  Entry* root_ = nullptr;

  friend class MapTester<Key, T, Compare>;
};
}  // namespace grpc_core
#endif  // GRPC_CORE_LIB_GPRPP_MAP_H
