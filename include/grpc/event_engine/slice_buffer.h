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

#ifndef GRPC_EVENT_ENGINE_SLICE_BUFFER_H
#define GRPC_EVENT_ENGINE_SLICE_BUFFER_H

#include <functional>

#include <grpc/event_engine/slice.h>
#include <grpc/slice_buffer.h>

namespace grpc_event_engine {
namespace experimental {

/// Wrapper around \a grpc_slice_buffer pointer.
///
/// A slice buffer holds the memory for a collection of slices.
/// The SliceBuffer object itself is meant to only hide the C-style API,
/// and won't hold the data itself. In terms of lifespan, the
/// grpc_slice_buffer ought to be kept somewhere inside the caller's objects,
/// like a transport or an endpoint.
///
/// This lifespan rule is likely to change in the future, as we may
/// collapse the grpc_slice_buffer structure straight into this class.
///
/// The SliceBuffer API is basically a replica of the grpc_slice_buffer's,
/// and its documentation will move here once we remove the C structure.
class SliceBuffer final {
 public:
  SliceBuffer(grpc_slice_buffer* sb) : sb_(sb) {}
  SliceBuffer(const SliceBuffer& other) : sb_(other.sb_) {}
  SliceBuffer(SliceBuffer&& other) : sb_(other.sb_) { other.sb_ = nullptr; }

  void Enumerate(std::function<void(uint8_t*, size_t, size_t)> cb) {
    const size_t cnt = Count();
    for (size_t i = 0; i < cnt; i++) {
      auto slice = &sb_->slices[i];
      auto start = GRPC_SLICE_START_PTR(*slice);
      auto len = GRPC_SLICE_LENGTH(*slice);
      cb(start, len, i);
    }
  }

  void Add(Slice slice) {
    grpc_slice_buffer_add(sb_, slice.slice_);
    slice.slice_ = grpc_empty_slice();
  }

  size_t AddIndexed(Slice slice) {
    size_t r = grpc_slice_buffer_add_indexed(sb_, slice.slice_);
    slice.slice_ = grpc_empty_slice();
    return r;
  }

  size_t AddIndexed(Slice&& slice) {
    size_t r = grpc_slice_buffer_add_indexed(sb_, slice.slice_);
    slice.slice_ = grpc_empty_slice();
    return r;
  }

  void Pop() { grpc_slice_buffer_pop(sb_); }

  size_t Count() { return sb_->count; }

  void TrimEnd(size_t n) { grpc_slice_buffer_trim_end(sb_, n, nullptr); }

  void MoveFirstIntoBuffer(size_t n, void* dst) {
    grpc_slice_buffer_move_first_into_buffer(sb_, n, dst);
  }

  void Clear() { grpc_slice_buffer_reset_and_unref(sb_); }

  Slice TakeFirst() {
    grpc_slice slice = grpc_slice_buffer_take_first(sb_);
    return Slice(slice, Slice::STEAL_REF);
  }

  void UndoTakeFirst(Slice slice) {
    grpc_slice_buffer_undo_take_first(sb_, slice.slice_);
    slice.slice_ = grpc_empty_slice();
  }

  void UndoTakeFirst(Slice&& slice) {
    grpc_slice_buffer_undo_take_first(sb_, slice.slice_);
    slice.slice_ = grpc_empty_slice();
  }

  Slice Ref(size_t index) {
    if (index >= Count()) return Slice();
    return Slice(sb_->slices[index], Slice::ADD_REF);
  }

  size_t Length() { return sb_->length; }

  grpc_slice_buffer* RawSliceBuffer() { return sb_; }

 private:
  grpc_slice_buffer* sb_ = nullptr;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPCPP_IMPL_CODEGEN_SLICE_H
