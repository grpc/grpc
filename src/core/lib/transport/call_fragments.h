// Copyright 2022 gRPC authors.
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

#ifndef GRPC_CORE_LIB_TRANSPORT_CALL_FRAGMENTS_H
#define GRPC_CORE_LIB_TRANSPORT_CALL_FRAGMENTS_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <new>
#include <utility>

#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/status.h>

#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

// TODO(ctiller): eliminate once MetadataHandle is constructable directly.
namespace promise_filter_detail {
class BaseCallData;
}  // namespace promise_filter_detail

class FragmentAllocator;

// Small owned "handle" type to ensure one accessor at a time to metadata.
// The focus here is to get promises to use the syntax we'd like - we'll
// probably substitute some other smart pointer later.
template <typename T>
class FragmentHandle {
 public:
  FragmentHandle() = default;

  FragmentHandle(const FragmentHandle&) = delete;
  FragmentHandle& operator=(const FragmentHandle&) = delete;

  FragmentHandle(FragmentHandle&& other) noexcept
      : handle_(other.handle_),
        allocated_by_allocator_(other.allocated_by_allocator_) {
    other.handle_ = nullptr;
    other.allocated_by_allocator_ = false;
  }
  FragmentHandle& operator=(FragmentHandle&& other) noexcept {
    DestroyHandle();
    handle_ = other.handle_;
    allocated_by_allocator_ = other.allocated_by_allocator_;
    other.handle_ = nullptr;
    other.allocated_by_allocator_ = false;
    return *this;
  }

  explicit FragmentHandle(const absl::Status& status);

  ~FragmentHandle() { DestroyHandle(); }

  T* operator->() const { return handle_; }
  bool has_value() const { return handle_ != nullptr; }
  T* get() const { return handle_; }
  void reset() { *this = FragmentHandle(); }

  static FragmentHandle TestOnlyWrap(T* p) { return FragmentHandle(p, false); }

 private:
  // We restrict access to construction from a pointer to limit the number of
  // cases that need dealing with as this code evolves.
  friend class promise_filter_detail::BaseCallData;
  friend class FragmentAllocator;

  explicit FragmentHandle(T* handle, bool allocated_by_allocator)
      : handle_(handle), allocated_by_allocator_(allocated_by_allocator) {}

  void DestroyHandle();

  T* Unwrap() {
    T* result = handle_;
    handle_ = nullptr;
    return result;
  }

  T* handle_ = nullptr;
  // TODO(ctiller): remove this once promise_based_filter goes away.
  // This bit determines whether the pointer is allocated by a metadata
  // allocator or some other system. If it's held by a metadata allocator, we'll
  // release it back when we're done with it.
  bool allocated_by_allocator_ = false;
};

// Server metadata type
// TODO(ctiller): This should be a bespoke instance of MetadataMap<>
using ServerMetadata = grpc_metadata_batch;
using ServerMetadataHandle = FragmentHandle<ServerMetadata>;

// Client initial metadata type
// TODO(ctiller): This should be a bespoke instance of MetadataMap<>
using ClientMetadata = grpc_metadata_batch;
using ClientMetadataHandle = FragmentHandle<ClientMetadata>;

class Message {
 public:
  Message() = default;
  ~Message() = default;
  Message(SliceBuffer payload, uint32_t flags)
      : payload_(std::move(payload)), flags_(flags) {}
  Message(const Message&) = delete;
  Message& operator=(const Message&) = delete;

  uint32_t flags() const { return flags_; }
  SliceBuffer* payload() { return &payload_; }
  const SliceBuffer* payload() const { return &payload_; }

 private:
  SliceBuffer payload_;
  uint32_t flags_ = 0;
};

using MessageHandle = FragmentHandle<Message>;

// Ok/not-ok check for trailing metadata, so that it can be used as result types
// for TrySeq.
inline bool IsStatusOk(const ServerMetadataHandle& m) {
  return m->get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN) ==
         GRPC_STATUS_OK;
}

// Within a call arena we need metadata at least four times - (client,server) x
// (initial,trailing), and possibly more for early returning promises.
// Since we often don't need these *simultaneously*, we can save memory by
// allocating/releasing them.
// We'd still like the memory to be part of the arena though, so this type
// creates a small free list of metadata objects and a central (call context)
// based place to create/destroy them.
class FragmentAllocator {
 public:
  FragmentAllocator() = default;
  ~FragmentAllocator() = default;
  FragmentAllocator(const FragmentAllocator&) = delete;
  FragmentAllocator& operator=(const FragmentAllocator&) = delete;

  ClientMetadataHandle MakeClientMetadata() {
    auto* node = AllocateNode();
    // TODO(ctiller): once we finish the promise transition, have metadata map
    // know about arena contexts and allocate directly from there.
    // (we could do so before, but there's enough places where we don't have a
    // promise context up that it's too much whackamole)
    new (&node->batch) ClientMetadata(GetContext<Arena>());
    return ClientMetadataHandle(&node->batch, true);
  }

  ServerMetadataHandle MakeServerMetadata() { return MakeClientMetadata(); }

  template <typename... Args>
  MessageHandle MakeMessage(Args&&... args) {
    auto* node = AllocateNode();
    new (&node->message) Message(std::forward<Args>(args)...);
    return MessageHandle(&node->message, true);
  }

 private:
  union Node {
    Node* next_free;
    grpc_metadata_batch batch;
    Message message;
  };

  template <typename T>
  friend class FragmentHandle;

  void Delete(grpc_metadata_batch* p);
  void Delete(Message* m);

  Node* AllocateNode();
  void FreeNode(Node* node);

  Node* free_list_ = nullptr;
};

template <>
struct ContextType<FragmentAllocator> {};

template <typename T>
FragmentHandle<T>::FragmentHandle(const absl::Status& status) {
  // TODO(ctiller): currently we guarantee that MetadataAllocator is only
  // present for promise based calls, and if we're using promise_based_filter
  // it's not present. If we're in a promise based call, the correct thing is to
  // use the metadata allocator to track the memory we need. If we're not, we
  // need to do the hacky thing promise_based_filter does.
  // This all goes away when promise_based_filter goes away, and this code will
  // just assume there's an allocator present and move forward.
  if (auto* allocator = GetContext<FragmentAllocator>()) {
    handle_ = nullptr;
    allocated_by_allocator_ = false;
    *this = allocator->MakeServerMetadata();
  } else {
    handle_ = GetContext<Arena>()->New<T>(GetContext<Arena>());
    allocated_by_allocator_ = false;
  }
  handle_->Set(GrpcStatusMetadata(),
               static_cast<grpc_status_code>(status.code()));
  if (status.ok()) return;
  handle_->Set(GrpcMessageMetadata(),
               Slice::FromCopiedString(status.message()));
}

template <typename T>
void FragmentHandle<T>::DestroyHandle() {
  if (allocated_by_allocator_) {
    GetContext<FragmentAllocator>()->Delete(handle_);
  }
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_TRANSPORT_CALL_FRAGMENTS_H
