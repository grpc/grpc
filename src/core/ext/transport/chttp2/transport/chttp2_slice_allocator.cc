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
#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_slice_allocator.h"

#include <functional>

#include "absl/memory/memory.h"
#include "absl/status/status.h"

#include <grpc/event_engine/slice_allocator.h>

#include "src/core/lib/iomgr/resource_quota.h"

namespace grpc_event_engine {
namespace experimental {

Chttp2SliceAllocator::Chttp2SliceAllocator(grpc_resource_user* user)
    : resource_user_(user) {}

Chttp2SliceAllocator::~Chttp2SliceAllocator() {
  if (resource_user_ != nullptr) {
    grpc_resource_user_unref(resource_user_);
  }
}

absl::Status Chttp2SliceAllocator::Allocate(
    size_t size, SliceBuffer* dest, SliceAllocator::AllocateCallback cb) {
  // TODO(hork): merge the implementation from the uv-ee branch.
  (void)size;
  (void)dest;
  (void)cb;
  return absl::OkStatus();
}

Chttp2SliceAllocatorFactory::Chttp2SliceAllocatorFactory(
    grpc_resource_quota* quota)
    : resource_quota_(quota) {
  grpc_resource_quota_ref_internal(resource_quota_);
}

Chttp2SliceAllocatorFactory::~Chttp2SliceAllocatorFactory() {
  if (resource_quota_ != nullptr) {
    grpc_resource_quota_unref_internal(resource_quota_);
  }
}

std::unique_ptr<SliceAllocator>
Chttp2SliceAllocatorFactory::CreateSliceAllocator(absl::string_view peer_name) {
  return absl::make_unique<Chttp2SliceAllocator>(
      grpc_resource_user_create(resource_quota_, peer_name));
}

}  // namespace experimental
}  // namespace grpc_event_engine
