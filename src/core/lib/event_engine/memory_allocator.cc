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

#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_refcount.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

// Reference count for a slice allocated by MemoryAllocator::MakeSlice.
// Takes care of releasing memory back when the slice is destroyed.
class SliceRefCount : public grpc_slice_refcount {
 public:
  SliceRefCount(std::shared_ptr<internal::MemoryAllocatorImpl> allocator,
                size_t size)
      : grpc_slice_refcount(Destroy),
        allocator_(std::move(allocator)),
        size_(size) {
    // Nothing to do here.
  }
  ~SliceRefCount() { allocator_->Release(size_); }

 private:
  static void Destroy(grpc_slice_refcount* p) {
    auto* rc = static_cast<SliceRefCount*>(p);
    rc->~SliceRefCount();
    gpr_free(rc);
  }

  std::shared_ptr<internal::MemoryAllocatorImpl> allocator_;
  size_t size_;
};

}  // namespace

void SliceBuffer::Add(grpc_core::Slice slice) {
  grpc_slice_buffer_add(slice_buffer_, slice.TakeCSlice());
}

size_t SliceBuffer::AddIndexed(grpc_core::Slice slice) {
  return grpc_slice_buffer_add_indexed(slice_buffer_, slice.TakeCSlice());
}

grpc_core::Slice SliceBuffer::TakeFirst() {
  return grpc_core::Slice(grpc_slice_buffer_take_first(slice_buffer_));
}

void SliceBuffer::UndoTakeFirst(grpc_core::Slice slice) {
  grpc_slice_buffer_undo_take_first(slice_buffer_, slice.TakeCSlice());
}

grpc_core::Slice SliceBuffer::RefSlice(size_t index) {
  if (index >= Count()) {
    return grpc_core::Slice(grpc_empty_slice());
  }
  return grpc_core::Slice(
      grpc_slice_ref_internal(slice_buffer_->slices[index]));
}

grpc_slice MemoryAllocator::MakeSlice(MemoryRequest request) {
  auto size = Reserve(request.Increase(sizeof(SliceRefCount)));
  void* p = gpr_malloc(size);
  new (p) SliceRefCount(allocator_, size);
  grpc_slice slice;
  slice.refcount = static_cast<SliceRefCount*>(p);
  slice.data.refcounted.bytes =
      static_cast<uint8_t*>(p) + sizeof(SliceRefCount);
  slice.data.refcounted.length = size - sizeof(SliceRefCount);
  return slice;
}

}  // namespace experimental
}  // namespace grpc_event_engine
