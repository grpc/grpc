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

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/transport/error_utils.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

namespace grpc_event_engine {
namespace experimental {

SliceAllocator::SliceAllocator(grpc_resource_user* user)
    : resource_user_(user) {
  grpc_resource_user_ref(resource_user_);
  slice_allocator_ = new grpc_resource_user_slice_allocator;
  grpc_resource_user_slice_allocator_init(slice_allocator_, resource_user_,
                                          OnAllocated, this);
};

SliceAllocator::~SliceAllocator() {
  if (resource_user_ != nullptr) {
    grpc_resource_user_unref(resource_user_);
  }
  delete slice_allocator_;
};

SliceAllocator::SliceAllocator(SliceAllocator&& other) noexcept
    : resource_user_(other.resource_user_) {
  GPR_ASSERT(!other.allocations_in_flight_);
  grpc_resource_user_ref(resource_user_);
  slice_allocator_ = new grpc_resource_user_slice_allocator;
  grpc_resource_user_slice_allocator_init(slice_allocator_, resource_user_,
                                          OnAllocated, this);
}

SliceAllocator& SliceAllocator::operator=(SliceAllocator&& other) noexcept {
  if (this == &other) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_INFO,
              "SliceAllocator move assignment operator called on itself.");
    }
    return *this;
  }
  GPR_ASSERT(!allocations_in_flight_ && !other.allocations_in_flight_);
  if (resource_user_ != nullptr) {
    grpc_resource_user_unref(resource_user_);
  }
  delete slice_allocator_;
  resource_user_ = other.resource_user_;
  grpc_resource_user_ref(resource_user_);
  slice_allocator_ = new grpc_resource_user_slice_allocator;
  grpc_resource_user_slice_allocator_init(slice_allocator_, resource_user_,
                                          OnAllocated, this);
  return *this;
}

absl::Status SliceAllocator::Allocate(size_t size, SliceBuffer* dest,
                                      AllocateCallback cb) {
  cb_ = cb;
  dest->clear();
  if (grpc_resource_user_alloc_slices(slice_allocator_, size, 1,
                                      dest->raw_slice_buffer())) {
    // allocated inline
    cb(absl::OkStatus());
    return absl::OkStatus();
  }
  allocations_in_flight_ = true;
  return absl::ResourceExhaustedError("Allocating asynchronously.");
};

void SliceAllocator::OnAllocated(void* arg, grpc_error_handle error) {
  auto self = static_cast<SliceAllocator*>(arg);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "SliceAllocator:%p read_allocation_done: %s", self,
            grpc_error_std_string(error).c_str());
  }
  self->cb_(grpc_error_to_absl_status(error));
  self->allocations_in_flight_ = false;
}

SliceAllocatorFactory::SliceAllocatorFactory(grpc_resource_quota* quota)
    : resource_quota_(quota) {
  grpc_resource_quota_ref_internal(resource_quota_);
};

SliceAllocatorFactory::~SliceAllocatorFactory() {
  if (resource_quota_ != nullptr) {
    grpc_resource_quota_unref_internal(resource_quota_);
  }
}

SliceAllocatorFactory::SliceAllocatorFactory(
    SliceAllocatorFactory&& other) noexcept
    : resource_quota_(other.resource_quota_) {
  other.resource_quota_ = nullptr;
}

SliceAllocatorFactory& SliceAllocatorFactory::operator=(
    SliceAllocatorFactory&& other) noexcept {
  resource_quota_ = other.resource_quota_;
  other.resource_quota_ = nullptr;
  return *this;
}

SliceAllocator SliceAllocatorFactory::CreateSliceAllocator(
    absl::string_view peer_name) {
  return SliceAllocator(
      grpc_resource_user_create(resource_quota_, peer_name.data()));
}

}  // namespace experimental
}  // namespace grpc_event_engine
