// Copyright 2021 The gRPC Authors
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
#ifndef GRPC_EVENT_ENGINE_SLICE_ALLOCATOR_H
#define GRPC_EVENT_ENGINE_SLICE_ALLOCATOR_H

#include <grpc/support/port_platform.h>

#include <functional>

#include "absl/status/status.h"

// forward-declaring an internal struct, not used publicly.
struct grpc_resource_quota;
struct grpc_resource_user;
struct grpc_slice_buffer;

namespace grpc_event_engine {
namespace experimental {

// TODO(nnoble): needs implementation
class SliceBuffer {
 public:
  SliceBuffer() { abort(); }
  explicit SliceBuffer(grpc_slice_buffer*) { abort(); }

  grpc_slice_buffer* RawSliceBuffer() { return slice_buffer_; }

 private:
  grpc_slice_buffer* slice_buffer_;
};

class SliceAllocator {
 public:
  using AllocateCallback = std::function<void(absl::Status)>;
  virtual ~SliceAllocator() = default;
  /// Requests \a size bytes from gRPC, and populates \a dest with the allocated
  /// slices. Ownership of the \a SliceBuffer is not transferred.
  ///
  /// gRPC provides a ResourceQuota system to cap the amount of memory used by
  /// the library. When a memory limit has been reached, slice allocation is
  /// interrupted to attempt to reclaim memory from participating gRPC
  /// internals. When there is sufficient memory available, slice allocation
  /// proceeds as normal.
  virtual absl::Status Allocate(size_t size, SliceBuffer* dest,
                                SliceAllocator::AllocateCallback cb) = 0;
};

class SliceAllocatorFactory {
 public:
  virtual ~SliceAllocatorFactory() = default;
  /// On Endpoint creation, call \a CreateSliceAllocator with the name of the
  /// endpoint peer (a URI string, most likely).
  virtual std::unique_ptr<SliceAllocator> CreateSliceAllocator(
      absl::string_view peer_name) = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_SLICE_ALLOCATOR_H
