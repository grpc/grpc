/*
 *
 * Copyright 2021 gRPC authors.
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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_SLICE_ALLOCATOR_H_
#define GRPC_CORE_LIB_EVENT_ENGINE_SLICE_ALLOCATOR_H_

#include <grpc/support/port_platform.h>

#include <functional>

#include "absl/status/status.h"

#include "src/core/lib/event_engine/channel_args.h"
// TODO(hork): should we hide this further? Needs to move out of iomgr for
// certain.
#include "src/core/lib/iomgr/resource_quota.h"

// TODO(hork): copy detailed design from design doc

namespace grpc_io {

// DO NOT SUBMIT - forward declared here, needs work on definitions.
class SliceBuffer;

class SliceAllocator {
 public:
  // TODO(hork): which constructors are required?
  SliceAllocator();
  // TODO(hork): destructor unrefs the resource user
  ~SliceAllocator() = default;
  using AllocateCallback =
      std::function<void(absl::Status, SliceBuffer* buffer)>;
  // Requests `size` bytes from the Resource Quota, and populates `dest` with
  // the allocated slices. Ownership of the `SliceBuffer` is not transferred.
  //
  // Note: thin layer above grpc_resource_user_alloc_slices
  // TODO(hork): explain what happens under resource exhaustion.
  absl::Status Allocate(size_t size, SliceBuffer* dest,
                        SliceAllocator::AllocateCallback cb);

 private:
  grpc_resource_user* resource_user_;
};

class SliceAllocatorFactory {
 public:
  // TODO(hork): which constructors are required?
  SliceAllocatorFactory();
  // TODO(hork): what does the destructor need to do for the quota?
  ~SliceAllocatorFactory() = default;
  SliceAllocator CreateSliceAllocator(absl::string_view name);
  // TODO: destructor unrefs the resource quota
 private:
  grpc_resource_quota* resource_quota_;
};

}  // namespace grpc_io

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_SLICE_ALLOCATOR_H_
