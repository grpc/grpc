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

namespace grpc_event_engine {
namespace experimental {

// TODO(nnoble): forward declared here, needs definition.
class SliceBuffer;

class SliceAllocator {
 public:
  // gRPC-internal constructor
  explicit SliceAllocator(grpc_resource_user* user);
  // Not copyable
  SliceAllocator(SliceAllocator& other) = delete;
  SliceAllocator& operator=(const SliceAllocator& other) = delete;
  // Moveable
  SliceAllocator(SliceAllocator&& other) = default;
  SliceAllocator& operator=(SliceAllocator&& other) = default;
  ~SliceAllocator();

  using AllocateCallback =
      std::function<void(absl::Status, SliceBuffer* buffer)>;
  // TODO(hork): explain what happens under resource exhaustion.
  /// Requests \a size bytes from gRPC, and populates \a dest with the allocated
  /// slices. Ownership of the \a SliceBuffer is not transferred.
  absl::Status Allocate(size_t size, SliceBuffer* dest,
                        SliceAllocator::AllocateCallback cb);

 private:
  grpc_resource_user* resource_user_;
};

class SliceAllocatorFactory {
 public:
  // gRPC-internal constructor
  explicit SliceAllocatorFactory(grpc_resource_quota* quota);
  // Not copyable
  SliceAllocatorFactory(SliceAllocatorFactory& other) = delete;
  SliceAllocatorFactory& operator=(const SliceAllocatorFactory& other) = delete;
  // Moveable
  SliceAllocatorFactory(SliceAllocatorFactory&& other) = default;
  SliceAllocatorFactory& operator=(SliceAllocatorFactory&& other) = default;
  ~SliceAllocatorFactory();

  /// On Endpoint creation, call \a CreateSliceAllocator with the name of the
  /// endpoint peer (a URI string, most likely). Note: \a peer_name must outlive
  /// the Endpoint.
  SliceAllocator CreateSliceAllocator(absl::string_view peer_name);

 private:
  grpc_resource_quota* resource_quota_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_SLICE_ALLOCATOR_H
