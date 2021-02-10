/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/ssl/session_cache/ssl_session.h"
#include "src/core/tsi/ssl/session_cache/ssl_session_cache.h"

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

namespace tsi {

static void cache_key_avl_destroy(void* /*key*/, void* /*unused*/) {}

static void* cache_key_avl_copy(void* key, void* /*unused*/) { return key; }

static long cache_key_avl_compare(void* key1, void* key2, void* /*unused*/) {
  return grpc_slice_cmp(*static_cast<grpc_slice*>(key1),
                        *static_cast<grpc_slice*>(key2));
}

static void cache_value_avl_destroy(void* /*value*/, void* /*unused*/) {}

static void* cache_value_avl_copy(void* value, void* /*unused*/) {
  return value;
}

// AVL only stores pointers, ownership belonges to the linked list.
static const grpc_avl_vtable cache_avl_vtable = {
    cache_key_avl_destroy,   cache_key_avl_copy,   cache_key_avl_compare,
    cache_value_avl_destroy, cache_value_avl_copy,
};

/// Node for single cached session.
class SslSessionLRUCache::Node {
 public:
  Node(const grpc_slice& key, SslSessionPtr session) : key_(key) {
    SetSession(std::move(session));
  }

  ~Node() { grpc_slice_unref_internal(key_); }

  // Not copyable nor movable.
  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  void* AvlKey() { return &key_; }

  /// Returns a copy of the node's cache session.
  SslSessionPtr CopySession() const { return session_->CopySession(); }

  /// Set the \a session (which is moved) for the node.
  void SetSession(SslSessionPtr session) {
    session_ = SslCachedSession::Create(std::move(session));
  }

 private:
  friend class SslSessionLRUCache;

  grpc_slice key_;
  std::unique_ptr<SslCachedSession> session_;

  Node* next_ = nullptr;
  Node* prev_ = nullptr;
};

SslSessionLRUCache::SslSessionLRUCache(size_t capacity) : capacity_(capacity) {
  GPR_ASSERT(capacity > 0);
  gpr_mu_init(&lock_);
  entry_by_key_ = grpc_avl_create(&cache_avl_vtable);
}

SslSessionLRUCache::~SslSessionLRUCache() {
  Node* node = use_order_list_head_;
  while (node) {
    Node* next = node->next_;
    delete node;
    node = next;
  }
  grpc_avl_unref(entry_by_key_, nullptr);
  gpr_mu_destroy(&lock_);
}

size_t SslSessionLRUCache::Size() {
  grpc_core::MutexLock lock(&lock_);
  return use_order_list_size_;
}

SslSessionLRUCache::Node* SslSessionLRUCache::FindLocked(
    const grpc_slice& key) {
  void* value =
      grpc_avl_get(entry_by_key_, const_cast<grpc_slice*>(&key), nullptr);
  if (value == nullptr) {
    return nullptr;
  }
  Node* node = static_cast<Node*>(value);
  // Move to the beginning.
  Remove(node);
  PushFront(node);
  AssertInvariants();
  return node;
}

void SslSessionLRUCache::Put(const char* key, SslSessionPtr session) {
  grpc_core::MutexLock lock(&lock_);
  Node* node = FindLocked(grpc_slice_from_static_string(key));
  if (node != nullptr) {
    node->SetSession(std::move(session));
    return;
  }
  grpc_slice key_slice = grpc_slice_from_copied_string(key);
  node = new Node(key_slice, std::move(session));
  PushFront(node);
  entry_by_key_ = grpc_avl_add(entry_by_key_, node->AvlKey(), node, nullptr);
  AssertInvariants();
  if (use_order_list_size_ > capacity_) {
    GPR_ASSERT(use_order_list_tail_);
    node = use_order_list_tail_;
    Remove(node);
    // Order matters, key is destroyed after deleting node.
    entry_by_key_ = grpc_avl_remove(entry_by_key_, node->AvlKey(), nullptr);
    delete node;
    AssertInvariants();
  }
}

SslSessionPtr SslSessionLRUCache::Get(const char* key) {
  grpc_core::MutexLock lock(&lock_);
  // Key is only used for lookups.
  grpc_slice key_slice = grpc_slice_from_static_string(key);
  Node* node = FindLocked(key_slice);
  if (node == nullptr) {
    return nullptr;
  }
  return node->CopySession();
}

void SslSessionLRUCache::Remove(SslSessionLRUCache::Node* node) {
  if (node->prev_ == nullptr) {
    use_order_list_head_ = node->next_;
  } else {
    node->prev_->next_ = node->next_;
  }
  if (node->next_ == nullptr) {
    use_order_list_tail_ = node->prev_;
  } else {
    node->next_->prev_ = node->prev_;
  }
  GPR_ASSERT(use_order_list_size_ >= 1);
  use_order_list_size_--;
}

void SslSessionLRUCache::PushFront(SslSessionLRUCache::Node* node) {
  if (use_order_list_head_ == nullptr) {
    use_order_list_head_ = node;
    use_order_list_tail_ = node;
    node->next_ = nullptr;
    node->prev_ = nullptr;
  } else {
    node->next_ = use_order_list_head_;
    node->next_->prev_ = node;
    use_order_list_head_ = node;
    node->prev_ = nullptr;
  }
  use_order_list_size_++;
}

#ifndef NDEBUG
static size_t calculate_tree_size(grpc_avl_node* node) {
  if (node == nullptr) {
    return 0;
  }
  return 1 + calculate_tree_size(node->left) + calculate_tree_size(node->right);
}

void SslSessionLRUCache::AssertInvariants() {
  size_t size = 0;
  Node* prev = nullptr;
  Node* current = use_order_list_head_;
  while (current != nullptr) {
    size++;
    GPR_ASSERT(current->prev_ == prev);
    void* node = grpc_avl_get(entry_by_key_, current->AvlKey(), nullptr);
    GPR_ASSERT(node == current);
    prev = current;
    current = current->next_;
  }
  GPR_ASSERT(prev == use_order_list_tail_);
  GPR_ASSERT(size == use_order_list_size_);
  GPR_ASSERT(calculate_tree_size(entry_by_key_.root) == use_order_list_size_);
}
#else
void SslSessionLRUCache::AssertInvariants() {}
#endif

}  // namespace tsi
