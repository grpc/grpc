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

class SliceBuffer final {
 public:
  SliceBuffer(grpc_slice_buffer* sb) : sb_(sb) {}
  SliceBuffer(const SliceBuffer& other) : sb_(other.sb_) {}
  SliceBuffer(SliceBuffer&& other) : sb_(other.sb_) { other.sb_ = nullptr; }
  void enumerate(std::function<void(uint8_t*, size_t, size_t)> cb) {
    const size_t cnt = count();
    for (size_t i = 0; i < cnt; i++) {
      auto slice = &sb_->slices[i];
      auto start = GRPC_SLICE_START_PTR(*slice);
      auto len = GRPC_SLICE_LENGTH(*slice);
      cb(start, len, i);
    }
  }
  void add(Slice slice) {
    grpc_slice_buffer_add(sb_, slice.slice_);
    slice.slice_ = grpc_empty_slice();
  }
  // TODO(nnoble): identical to add, ambiguous compilation with
  // `sb.add(Slice(42))`;
  //  void add(Slice&& slice) {
  //   grpc_slice_buffer_add(sb_, slice.slice_);
  //   slice.slice_ = grpc_empty_slice();
  // }
  size_t add_indexed(Slice slice) {
    size_t r = grpc_slice_buffer_add_indexed(sb_, slice.slice_);
    slice.slice_ = grpc_empty_slice();
    return r;
  }
  size_t add_indexed(Slice&& slice) {
    size_t r = grpc_slice_buffer_add_indexed(sb_, slice.slice_);
    slice.slice_ = grpc_empty_slice();
    return r;
  }
  template <size_t len>
  uint8_t* tiny_add() {
    return grpc_slice_buffer_tiny_add(sb_, len);
  }
  uint8_t* tiny_add(size_t len) { return grpc_slice_buffer_tiny_add(sb_, len); }
  void pop() { grpc_slice_buffer_pop(sb_); }
  size_t count() { return sb_->count; }
  void trim_end(size_t n) { grpc_slice_buffer_trim_end(sb_, n, nullptr); }
  void move_first_into_buffer(size_t n, void* dst) {
    grpc_slice_buffer_move_first_into_buffer(sb_, n, dst);
  }
  void clear() { grpc_slice_buffer_reset_and_unref(sb_); }
  Slice take_first() {
    // TODO(nnoble): document take_first on empty SliceBuffer
    grpc_slice slice = grpc_slice_buffer_take_first(sb_);
    return Slice(slice, Slice::STEAL_REF);
  }
  void undo_take_first(Slice slice) {
    grpc_slice_buffer_undo_take_first(sb_, slice.slice_);
    slice.slice_ = grpc_empty_slice();
  }
  void undo_take_first(Slice&& slice) {
    grpc_slice_buffer_undo_take_first(sb_, slice.slice_);
    slice.slice_ = grpc_empty_slice();
  }
  Slice ref(size_t index) {
    if (index >= count()) return Slice();
    return Slice(sb_->slices[index], Slice::ADD_REF);
  }
  size_t length() { return sb_->length; }
  grpc_slice_buffer* raw_slice_buffer() { return sb_; }

 private:
  grpc_slice_buffer* sb_ = nullptr;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPCPP_IMPL_CODEGEN_SLICE_H
