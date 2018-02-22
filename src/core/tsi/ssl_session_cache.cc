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

#include "src/core/tsi/ssl_session_cache.h"

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

namespace grpc_core {

static void cache_key_avl_destroy(void* key, void* unused) {}

static void* cache_key_avl_copy(void* key, void* unused) { return key; }

static long cache_key_avl_compare(void* key1, void* key2, void* unused) {
  return grpc_slice_cmp(*static_cast<grpc_slice*>(key1),
                        *static_cast<grpc_slice*>(key2));
}

static void cache_value_avl_destroy(void* value, void* unused) {}

static void* cache_value_avl_copy(void* value, void* unused) { return value; }

// AVL only stores pointers, ownership belonges to linked list.
static const grpc_avl_vtable cache_avl_vtable = {
    cache_key_avl_destroy,   cache_key_avl_copy,   cache_key_avl_compare,
    cache_value_avl_destroy, cache_value_avl_copy,
};

// BoringSSL and OpenSSL have different behavior regarding TLS ticket
// resumption.
//
// BoringSSL allows SSL_SESSION outlive SSL and SSL_CTX objects which are
// re-created by gRPC on every cert rotation/subchannel creation.
// SSL_SESSION is also immutable in BoringSSL and it's safe to share
// the same session between different threads and connections.
//
// OpenSSL invalidates SSL_SESSION on SSL destruction making it pointless
// to cache sessions. The workaround is to serialize (relatively expensive)
// session into binary blob and re-create it from blob on every handshake.
void SslSessionMayBeDeleter::operator()(SSL_SESSION* session) {
#ifndef OPENSSL_IS_BORINGSSL
  SSL_SESSION_free(session);
#endif
}

class SslSessionLRUCache::Node {
 public:
  Node(const grpc_slice& key, SslSessionPtr session) : key_(key) {
#ifndef OPENSSL_IS_BORINGSSL
    session_ = grpc_empty_slice();
#endif
    SetSession(std::move(session));
  }

  ~Node() {
    grpc_slice_unref(key_);
#ifndef OPENSSL_IS_BORINGSSL
    grpc_slice_unref(session_);
#endif
  }

  // Not copyable nor movable.
  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  void* AvlKey() { return &key_; }

#ifdef OPENSSL_IS_BORINGSSL
  SslSessionGetResult GetSession() const {
    return SslSessionGetResult(session_.get());
  }

  void SetSession(SslSessionPtr session) { session_ = std::move(session); }
#else
  SslSessionGetResult GetSession() const {
    const unsigned char* data = GRPC_SLICE_START_PTR(session_);
    size_t length = GRPC_SLICE_LENGTH(session_);
    SSL_SESSION* session = d2i_SSL_SESSION(nullptr, &data, length);
    if (session == nullptr) {
      return SslSessionGetResult();
    }
    return SslSessionGetResult(session);
  }

  void SetSession(SslSessionPtr session) {
    int size = i2d_SSL_SESSION(session.get(), nullptr);
    GPR_ASSERT(size > 0);
    grpc_slice slice = grpc_slice_malloc(size_t(size));

    unsigned char* start = GRPC_SLICE_START_PTR(slice);
    int second_size = i2d_SSL_SESSION(session.get(), &start);
    GPR_ASSERT(size == second_size);

    grpc_slice_unref(session_);
    session_ = slice;
  }
#endif

 private:
  friend class SslSessionLRUCache;

  grpc_slice key_;
#ifdef OPENSSL_IS_BORINGSSL
  SslSessionPtr session_;
#else
  grpc_slice session_;
#endif

  Node* next_ = nullptr;
  Node* prev_ = nullptr;
};

SslSessionLRUCache::SslSessionLRUCache(size_t capacity) : capacity_(capacity) {
  GPR_ASSERT(capacity > 0);
  gpr_ref_init(&ref_, 1);
  gpr_mu_init(&lock_);

  entry_by_key_ = grpc_avl_create(&cache_avl_vtable);
}

SslSessionLRUCache::~SslSessionLRUCache() {
  Node* node = use_order_list_head_;
  while (node) {
    Node* next = node->next_;
    Delete(node);
    node = next;
  }

  grpc_avl_unref(entry_by_key_, nullptr);
  gpr_mu_destroy(&lock_);
}

void SslSessionLRUCache::PutLocked(const char* key, SslSessionPtr session) {
  Node* node = FindLocked(grpc_slice_from_static_string(key));
  if (node != nullptr) {
    node->SetSession(std::move(session));
    return;
  }

  grpc_slice key_slice = grpc_slice_from_copied_string(key);
  node = New<Node>(key_slice, std::move(session));
  PushFront(node);
  entry_by_key_ = grpc_avl_add(entry_by_key_, node->AvlKey(), node, nullptr);
  AssertInvariants();

  if (use_order_list_size_ > capacity_) {
    GPR_ASSERT(use_order_list_tail_);
    node = use_order_list_tail_;
    Remove(node);

    // Order matters, key is destroyed after deleting node.
    entry_by_key_ = grpc_avl_remove(entry_by_key_, node->AvlKey(), nullptr);
    Delete(node);
    AssertInvariants();
  }
}

SslSessionGetResult SslSessionLRUCache::GetLocked(const char* key) {
  // Key is only used for lookups.
  grpc_slice key_slice = grpc_slice_from_static_string(key);
  Node* node = FindLocked(key_slice);
  if (node == nullptr) {
    return nullptr;
  }

  return node->GetSession();
}

size_t SslSessionLRUCache::Size() {
  mu_guard guard(&lock_);
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

int SslSessionLRUCache::SslExIndex = -1;

void SslSessionLRUCache::InitSslExIndex() {
  SslExIndex = SSL_CTX_get_ex_new_index(
      0, nullptr, nullptr, nullptr,
      [](void* parent, void* ptr, CRYPTO_EX_DATA* ad, int index, long argl,
         void* argp) {
        if (ptr != nullptr) {
          static_cast<SslSessionLRUCache*>(ptr)->Unref();
        }
      });
  GPR_ASSERT(SslExIndex != -1);
}

SslSessionLRUCache* SslSessionLRUCache::GetSelf(SSL* ssl) {
  SSL_CTX* ssl_context = SSL_get_SSL_CTX(ssl);
  if (ssl_context == nullptr) {
    return nullptr;
  }

  return static_cast<SslSessionLRUCache*>(
      SSL_CTX_get_ex_data(ssl_context, SslExIndex));
}

int SslSessionLRUCache::SetNewCallback(SSL* ssl, SSL_SESSION* session) {
  SslSessionLRUCache* self = GetSelf(ssl);
  if (self == nullptr) {
    return 0;
  }

  const char* server_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (server_name == nullptr) {
    return 0;
  }

  mu_guard guard(&self->lock_);
  self->PutLocked(server_name, SslSessionPtr(session));
  // Return 1 to indicate transfered ownership over the given session.
  return 1;
}

void SslSessionLRUCache::InitContext(tsi_ssl_session_cache* cache,
                                     SSL_CTX* ssl_context) {
  auto self = static_cast<SslSessionLRUCache*>(cache);
  GPR_ASSERT(self);
  // SSL_CTX will call Unref on destruction.
  self->Ref();
  SSL_CTX_set_ex_data(ssl_context, SslExIndex, self);
  SSL_CTX_sess_set_new_cb(ssl_context, SetNewCallback);
  SSL_CTX_set_session_cache_mode(ssl_context, SSL_SESS_CACHE_CLIENT);
}

void SslSessionLRUCache::ResumeSession(SSL* ssl) {
  SslSessionLRUCache* self = GetSelf(ssl);
  if (self == nullptr) {
    return;
  }

  const char* server_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (server_name == nullptr) {
    return;
  }

  mu_guard guard(&self->lock_);
  SslSessionGetResult session = self->GetLocked(server_name);
  if (session != nullptr) {
    // SSL_set_session internally increments reference counter.
    SSL_set_session(ssl, session.get());
  }
}

}  // namespace grpc_core

tsi_ssl_session_cache* tsi_ssl_session_cache_create_lru(size_t capacity) {
  return grpc_core::New<grpc_core::SslSessionLRUCache>(capacity);
}

static grpc_core::SslSessionLRUCache* tsi_ssl_session_cache_get_self(
    tsi_ssl_session_cache* cache) {
  return static_cast<grpc_core::SslSessionLRUCache*>(cache);
}

void tsi_ssl_session_cache_ref(tsi_ssl_session_cache* cache) {
  tsi_ssl_session_cache_get_self(cache)->Ref();
}

void tsi_ssl_session_cache_unref(tsi_ssl_session_cache* cache) {
  tsi_ssl_session_cache_get_self(cache)->Unref();
}
