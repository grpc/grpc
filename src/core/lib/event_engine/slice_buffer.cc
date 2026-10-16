// Copyright 2022 gRPC authors.
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

#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#include "src/core/lib/slice/slice.h"

namespace grpc_event_engine::experimental {

void SliceBuffer::Append(Slice slice) {
  grpc_slice_buffer_add(&slice_buffer_, slice.TakeCSlice());
}

size_t SliceBuffer::AppendIndexed(Slice slice) {
  return grpc_slice_buffer_add_indexed(&slice_buffer_, slice.TakeCSlice());
}

Slice SliceBuffer::TakeFirst() {
  return Slice(grpc_slice_buffer_take_first(&slice_buffer_));
}

void SliceBuffer::Prepend(Slice slice) {
  grpc_slice_buffer_undo_take_first(&slice_buffer_, slice.TakeCSlice());
}

Slice SliceBuffer::RefSlice(size_t index) {
  return Slice(grpc_core::CSliceRef(slice_buffer_.slices[index]));
}

}  // namespace grpc_event_engine::experimental
