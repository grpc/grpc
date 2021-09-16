// Copyright 2021 gRPC authors.
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

#include "src/core/lib/resource_quota/make_slice.h"

#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

namespace {

class SliceRefCount {
 public:
  static void Destroy(void* p) {
    auto* rc = static_cast<SliceRefCount*>(p);
    rc->~SliceRefCount();
    gpr_free(rc);
  }
  SliceRefCount(RefCountedPtr<MemoryAllocator> allocator, size_t size)
      : base_(grpc_slice_refcount::Type::REGULAR, &refs_, Destroy, this,
              &base_),
        allocator_(std::move(allocator)),
        size_(size) {
    // Nothing to do here.
  }
  ~SliceRefCount() { allocator_->Release(size_); }

  grpc_slice_refcount* base_refcount() { return &base_; }

 private:
  grpc_slice_refcount base_;
  RefCount refs_;
  RefCountedPtr<MemoryAllocator> allocator_;
  size_t size_;
};

}  // namespace

grpc_slice MakeSlice(RefCountedPtr<MemoryAllocator> allocator,
                     MemoryRequest request) {
  auto size = allocator->Reserve(request.Increase(sizeof(SliceRefCount)));
  void* p = gpr_malloc(size);
  new (p) SliceRefCount(std::move(allocator), size);
  grpc_slice slice;
  slice.refcount = static_cast<SliceRefCount*>(p)->base_refcount();
  slice.data.refcounted.bytes =
      static_cast<uint8_t*>(p) + sizeof(SliceRefCount);
  slice.data.refcounted.length = size - sizeof(SliceRefCount);
  return slice;
}

}  // namespace grpc_core
