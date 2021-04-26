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

#include "grpc/event_engine/slice_allocator.h"

#include <functional>

#include "absl/status/status.h"

#include "src/core/lib/iomgr/resource_quota.h"

namespace grpc_event_engine {
namespace experimental {

SliceAllocator::SliceAllocator(grpc_resource_user* user)
    : resource_user_(user) {
  grpc_resource_user_ref(resource_user_);
};

SliceAllocator::~SliceAllocator() { grpc_resource_user_unref(resource_user_); };

absl::Status SliceAllocator::Allocate(size_t size, SliceBuffer* dest,
                                      SliceAllocator::AllocateCallback cb) {
  // TODO(hork): implement
  (void)size;
  (void)dest;
  (void)cb;
  return absl::OkStatus();
};

SliceAllocatorFactory::SliceAllocatorFactory(grpc_resource_quota* quota)
    : resource_quota_(quota) {
  grpc_resource_quota_ref_internal(resource_quota_);
};

SliceAllocatorFactory::~SliceAllocatorFactory() {
  grpc_resource_quota_unref_internal(resource_quota_);
}

SliceAllocator SliceAllocatorFactory::CreateSliceAllocator(
    absl::string_view peer_name) {
  return SliceAllocator(
      grpc_resource_user_create(resource_quota_, peer_name.data()));
}

}  // namespace experimental
}  // namespace grpc_event_engine
