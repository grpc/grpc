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
#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CHTTP2_SLICE_ALLOCATOR_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CHTTP2_SLICE_ALLOCATOR_H

#include <grpc/support/port_platform.h>

#include <functional>

#include "absl/status/status.h"

#include <grpc/event_engine/slice_allocator.h>

#include "src/core/lib/iomgr/resource_quota.h"

namespace grpc_event_engine {
namespace experimental {

class Chttp2SliceAllocator
    : public grpc_event_engine::experimental::SliceAllocator {
 public:
  /// gRPC-internal constructor. Takes ownership of a resource_user ref from the
  /// caller.
  explicit Chttp2SliceAllocator(grpc_resource_user* user);
  // Not copyable
  Chttp2SliceAllocator(Chttp2SliceAllocator& other) = delete;
  Chttp2SliceAllocator& operator=(const Chttp2SliceAllocator& other) = delete;
  // Not Moveable
  Chttp2SliceAllocator(Chttp2SliceAllocator&& other) = delete;
  Chttp2SliceAllocator& operator=(Chttp2SliceAllocator&& other) = delete;
  ~Chttp2SliceAllocator() override;
  absl::Status Allocate(size_t size, SliceBuffer* dest,
                        SliceAllocator::AllocateCallback cb) override;

 private:
  grpc_resource_user* resource_user_;
};

class Chttp2SliceAllocatorFactory
    : public grpc_event_engine::experimental::SliceAllocatorFactory {
 public:
  // gRPC-internal constructor
  explicit Chttp2SliceAllocatorFactory(grpc_resource_quota* quota);
  // Not copyable
  Chttp2SliceAllocatorFactory(Chttp2SliceAllocatorFactory& other) = delete;
  Chttp2SliceAllocatorFactory& operator=(
      const Chttp2SliceAllocatorFactory& other) = delete;
  // Not Moveable
  Chttp2SliceAllocatorFactory(Chttp2SliceAllocatorFactory&& other) = delete;
  Chttp2SliceAllocatorFactory& operator=(Chttp2SliceAllocatorFactory&& other) =
      delete;
  ~Chttp2SliceAllocatorFactory() override;
  std::unique_ptr<SliceAllocator> CreateSliceAllocator(
      absl::string_view peer_name) override;

 private:
  grpc_resource_quota* resource_quota_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CHTTP2_SLICE_ALLOCATOR_H
