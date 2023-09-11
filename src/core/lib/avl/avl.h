// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GRPC_SRC_CORE_LIB_AVL_AVL_H
#define GRPC_SRC_CORE_LIB_AVL_AVL_H

#include <grpc/support/port_platform.h>

#include <stdlib.h>

#include <algorithm>  // IWYU pragma: keep
#include <cstdint>
#include <utility>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

template <class K, class V = void>
class AVL {
 public:
  AVL() {}

  AVL Add(K key, V value) const {
    return AVL(AddKey(root_, std::move(key), std::move(value)));
  }
  template <typename SomethingLikeK>
  AVL Remove(const SomethingLikeK& key) const {
    return AVL(RemoveKey(root_, key));
  }
  template <typename SomethingLikeK>
  const V* Lookup(const SomethingLikeK& key) const {
    NodePtr n = Get(root_, key);
    return n != nullptr ? &n->value : nullptr;
  }

  const std::pair<K, V>* LookupBelow(const K& key) const {
    NodePtr n = GetBelow(root_, *key);
    return n != nullptr ? &n->kv : nullptr;
  }

  bool Empty() const { return root_ == nullptr; }

  template <class F>
  void ForEach(F&& f) const {
    ForEachImpl(root_.get(), std::forward<F>(f));
  }

  bool SameIdentity(const AVL& avl) const { return root_ == avl.root_; }

  friend int QsortCompare(const AVL& left, const AVL& right) {
    if (left.root_.get() == right.root_.get()) return 0;
    Iterator a(left.root_);
    Iterator b(right.root_);
    for (;;) {
      Node* p = a.current();
      Node* q = b.current();
      if (p != q) {
        if (p == nullptr) return -1;
        if (q == nullptr) return 1;
        const int key = QsortCompare(p->key, q->key);
        if (key != 0) return key;
        const int value = QsortCompare(p->value, q->value);
        if (value != 0) return value;
      } else if (p == nullptr) {
        return 0;
      }
      a.MoveNext();
      b.MoveNext();
    }
  }

  bool operator==(const AVL& other) const {
    return QsortCompare(*this, other) == 0;
  }

  bool operator<(const AVL& other) const {
    return QsortCompare(*this, other) < 0;
  }

  size_t Height() const {
    if (root_ == nullptr) return 0;
    return root_->height;
  }

  AVL UnionWith(AVL other) const {
    if (Empty()) return other;
    if (other.Empty()) return *this;
    if (Height() <= other.Height()) {
      ForEach([&other](const K& key, const V& value) {
        other = other.Add(key, value);
      });
      return other;
    } else {
      auto result = *this;
      other.ForEach([&result](const K& key, const V& value) {
        if (result.Lookup(key) == nullptr) {
          result = result.Add(key, value);
        }
      });
      return result;
    }
  }

  AVL FuzzingReferenceUnionWith(AVL other) const {
    // DO NOT OPTIMIZE THIS!!
    ForEach([&other](const K& key, const V& value) {
      other = other.Add(key, value);
    });
    return other;
  }

 private:
  struct Node;

  typedef RefCountedPtr<Node> NodePtr;
  struct Node : public RefCounted<Node, NonPolymorphicRefCount> {
    Node(K k, V v, NodePtr l, NodePtr r, int h)
        : key(std::move(k)),
          height(h),
          value(std::move(v)),
          left(std::move(l)),
          right(std::move(r)) {}
    const K key;
    const int height;
    const V value;
    const NodePtr left;
    const NodePtr right;
  };
  NodePtr root_;

  class IteratorStack {
   public:
    void Push(Node* n) {
      nodes_[depth_] = n;
      ++depth_;
    }

    Node* Pop() {
      --depth_;
      return nodes_[depth_];
    }

    Node* Back() const { return nodes_[depth_ - 1]; }

    bool Empty() const { return depth_ == 0; }

   private:
    size_t depth_{0};
    // 32 is the maximum depth we can accept, and corresponds to ~4billion nodes
    // - which ought to suffice our use cases.
    Node* nodes_[32];
  };

  class Iterator {
   public:
    explicit Iterator(const NodePtr& root) {
      auto* n = root.get();
      while (n != nullptr) {
        stack_.Push(n);
        n = n->left.get();
      }
    }
    Node* current() const { return stack_.Empty() ? nullptr : stack_.Back(); }
    void MoveNext() {
      auto* n = stack_.Pop();
      if (n->right != nullptr) {
        n = n->right.get();
        while (n != nullptr) {
          stack_.Push(n);
          n = n->left.get();
        }
      }
    }

   private:
    IteratorStack stack_;
  };

  explicit AVL(NodePtr root) : root_(std::move(root)) {}

  template <class F>
  static void ForEachImpl(const Node* n, F&& f) {
    if (n == nullptr) return;
    ForEachImpl(n->left.get(), std::forward<F>(f));
    f(const_cast<const K&>(n->key), const_cast<const V&>(n->value));
    ForEachImpl(n->right.get(), std::forward<F>(f));
  }

  static int Height(const NodePtr& n) { return n != nullptr ? n->height : 0; }

  static NodePtr MakeNode(K key, V value, const NodePtr& left,
                          const NodePtr& right) {
    return MakeRefCounted<Node>(std::move(key), std::move(value), left, right,
                                1 + std::max(Height(left), Height(right)));
  }

  template <typename SomethingLikeK>
  static NodePtr Get(const NodePtr& node, const SomethingLikeK& key) {
    if (node == nullptr) {
      return nullptr;
    }

    if (node->key > key) {
      return Get(node->left, key);
    } else if (node->key < key) {
      return Get(node->right, key);
    } else {
      return node;
    }
  }

  static NodePtr GetBelow(const NodePtr& node, const K& key) {
    if (!node) return nullptr;
    if (node->key > key) {
      return GetBelow(node->left, key);
    } else if (node->key < key) {
      NodePtr n = GetBelow(node->right, key);
      if (n == nullptr) n = node;
      return n;
    } else {
      return node;
    }
  }

  static NodePtr RotateLeft(K key, V value, const NodePtr& left,
                            const NodePtr& right) {
    return MakeNode(
        right->key, right->value,
        MakeNode(std::move(key), std::move(value), left, right->left),
        right->right);
  }

  static NodePtr RotateRight(K key, V value, const NodePtr& left,
                             const NodePtr& right) {
    return MakeNode(
        left->key, left->value, left->left,
        MakeNode(std::move(key), std::move(value), left->right, right));
  }

  static NodePtr RotateLeftRight(K key, V value, const NodePtr& left,
                                 const NodePtr& right) {
    // rotate_right(..., rotate_left(left), right)
    return MakeNode(
        left->right->key, left->right->value,
        MakeNode(left->key, left->value, left->left, left->right->left),
        MakeNode(std::move(key), std::move(value), left->right->right, right));
  }

  static NodePtr RotateRightLeft(K key, V value, const NodePtr& left,
                                 const NodePtr& right) {
    // rotate_left(..., left, rotate_right(right))
    return MakeNode(
        right->left->key, right->left->value,
        MakeNode(std::move(key), std::move(value), left, right->left->left),
        MakeNode(right->key, right->value, right->left->right, right->right));
  }

  static NodePtr Rebalance(K key, V value, const NodePtr& left,
                           const NodePtr& right) {
    switch (Height(left) - Height(right)) {
      case 2:
        if (Height(left->left) - Height(left->right) == -1) {
          return RotateLeftRight(std::move(key), std::move(value), left, right);
        } else {
          return RotateRight(std::move(key), std::move(value), left, right);
        }
      case -2:
        if (Height(right->left) - Height(right->right) == 1) {
          return RotateRightLeft(std::move(key), std::move(value), left, right);
        } else {
          return RotateLeft(std::move(key), std::move(value), left, right);
        }
      default:
        return MakeNode(key, value, left, right);
    }
  }

  static NodePtr AddKey(const NodePtr& node, K key, V value) {
    if (node == nullptr) {
      return MakeNode(std::move(key), std::move(value), nullptr, nullptr);
    }
    if (node->key < key) {
      return Rebalance(node->key, node->value, node->left,
                       AddKey(node->right, std::move(key), std::move(value)));
    }
    if (key < node->key) {
      return Rebalance(node->key, node->value,
                       AddKey(node->left, std::move(key), std::move(value)),
                       node->right);
    }
    return MakeNode(std::move(key), std::move(value), node->left, node->right);
  }

  static NodePtr InOrderHead(NodePtr node) {
    while (node->left != nullptr) {
      node = node->left;
    }
    return node;
  }

  static NodePtr InOrderTail(NodePtr node) {
    while (node->right != nullptr) {
      node = node->right;
    }
    return node;
  }

  template <typename SomethingLikeK>
  static NodePtr RemoveKey(const NodePtr& node, const SomethingLikeK& key) {
    if (node == nullptr) {
      return nullptr;
    }
    if (key < node->key) {
      return Rebalance(node->key, node->value, RemoveKey(node->left, key),
                       node->right);
    } else if (node->key < key) {
      return Rebalance(node->key, node->value, node->left,
                       RemoveKey(node->right, key));
    } else {
      if (node->left == nullptr) {
        return node->right;
      } else if (node->right == nullptr) {
        return node->left;
      } else if (node->left->height < node->right->height) {
        NodePtr h = InOrderHead(node->right);
        return Rebalance(h->key, h->value, node->left,
                         RemoveKey(node->right, h->key));
      } else {
        NodePtr h = InOrderTail(node->left);
        return Rebalance(h->key, h->value, RemoveKey(node->left, h->key),
                         node->right);
      }
    }
    abort();
  }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_AVL_AVL_H
