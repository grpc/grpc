//
//
// Copyright 2018 gRPC authors.
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
//
//

#include "src/core/tsi/ssl/session_cache/ssl_session_cache.h"

#include "absl/log/check.h"
#include "absl/log/log.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/ssl/session_cache/ssl_session.h"

namespace tsi {

/// Node for single cached session.
class SslSessionLRUCache::Node {
 public:
  Node(const std::string& key, SslSessionPtr session) : key_(key) {
    SetSession(std::move(session));
  }

  // Not copyable nor movable.
  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  const std::string& key() const { return key_; }

  /// Returns a copy of the node's cache session.
  SslSessionPtr CopySession() const { return session_->CopySession(); }

  /// Set the \a session (which is moved) for the node.
  void SetSession(SslSessionPtr session) {
    session_ = SslCachedSession::Create(std::move(session));
  }

 private:
  friend class SslSessionLRUCache;

  std::string key_;
  std::unique_ptr<SslCachedSession> session_;

  Node* next_ = nullptr;
  Node* prev_ = nullptr;
};

SslSessionLRUCache::SslSessionLRUCache(size_t capacity) : capacity_(capacity) {
  if (capacity == 0) {
    LOG(ERROR) << "SslSessionLRUCache capacity is zero. SSL sessions cannot be "
                  "resumed.";
  }
}

SslSessionLRUCache::~SslSessionLRUCache() {
  Node* node = use_order_list_head_;
  while (node) {
    Node* next = node->next_;
    delete node;
    node = next;
  }
}

size_t SslSessionLRUCache::Size() {
  grpc_core::MutexLock lock(&lock_);
  return use_order_list_size_;
}

SslSessionLRUCache::Node* SslSessionLRUCache::FindLocked(
    const std::string& key) {
  auto it = entry_by_key_.find(key);
  if (it == entry_by_key_.end()) {
    return nullptr;
  }
  Node* node = it->second;
  // Move to the beginning.
  Remove(node);
  PushFront(node);
  AssertInvariants();
  return node;
}

void SslSessionLRUCache::Put(const char* key, SslSessionPtr session) {
  if (session == nullptr) {
    LOG(ERROR) << "Attempted to put null SSL session in session cache.";
    return;
  }
  grpc_core::MutexLock lock(&lock_);
  Node* node = FindLocked(key);
  if (node != nullptr) {
    node->SetSession(std::move(session));
    return;
  }
  node = new Node(key, std::move(session));
  PushFront(node);
  entry_by_key_.emplace(key, node);
  AssertInvariants();
  if (use_order_list_size_ > capacity_) {
    CHECK(use_order_list_tail_);
    node = use_order_list_tail_;
    Remove(node);
    // Order matters, key is destroyed after deleting node.
    entry_by_key_.erase(node->key());
    delete node;
    AssertInvariants();
  }
}

SslSessionPtr SslSessionLRUCache::Get(const char* key) {
  grpc_core::MutexLock lock(&lock_);
  // Key is only used for lookups.
  Node* node = FindLocked(key);
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
  CHECK_GE(use_order_list_size_, 1u);
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
void SslSessionLRUCache::AssertInvariants() {
  size_t size = 0;
  Node* prev = nullptr;
  Node* current = use_order_list_head_;
  while (current != nullptr) {
    size++;
    CHECK(current->prev_ == prev);
    auto it = entry_by_key_.find(current->key());
    CHECK(it != entry_by_key_.end());
    CHECK(it->second == current);
    prev = current;
    current = current->next_;
  }
  CHECK(prev == use_order_list_tail_);
  CHECK(size == use_order_list_size_);
  CHECK(entry_by_key_.size() == use_order_list_size_);
}
#else
void SslSessionLRUCache::AssertInvariants() {}
#endif

}  // namespace tsi
