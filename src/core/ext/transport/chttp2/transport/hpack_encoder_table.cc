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

#include "src/core/ext/transport/chttp2/transport/hpack_encoder_table.h"

#include <grpc/support/log.h>

namespace grpc_core {

uint32_t HPackEncoderTable::AllocateIndex(size_t element_size) {
  uint32_t new_index = tail_remote_index_ + table_elems_ + 1;
  GPR_DEBUG_ASSERT(element_size <= MaxEntrySize());

  if (element_size > max_table_size_) {
    while (table_size_ > 0) {
      EvictOne();
    }
    return 0;
  }

  // Reserve space for this element in the remote table: if this overflows
  // the current table, drop elements until it fits, matching the decompressor
  // algorithm.
  while (table_size_ + element_size > max_table_size_) {
    EvictOne();
  }
  GPR_ASSERT(table_elems_ < elem_size_.size());
  elem_size_[new_index % elem_size_.size()] =
      static_cast<uint16_t>(element_size);
  table_size_ += element_size;
  table_elems_++;

  return new_index;
}

bool HPackEncoderTable::SetMaxSize(uint32_t max_table_size) {
  if (max_table_size == max_table_size_) {
    return false;
  }
  while (table_size_ > 0 && table_size_ > max_table_size) {
    EvictOne();
  }
  max_table_size_ = max_table_size;
  const size_t max_table_elems =
      hpack_constants::EntriesForBytes(max_table_size);
  // TODO(ctiller): integrate with ResourceQuota to rebuild smaller when we can.
  if (max_table_elems > elem_size_.size()) {
    Rebuild(std::max(max_table_elems, 2 * elem_size_.size()));
  }
  return true;
}

void HPackEncoderTable::EvictOne() {
  tail_remote_index_++;
  GPR_ASSERT(tail_remote_index_ > 0);
  GPR_ASSERT(table_elems_ > 0);
  auto removing_size = elem_size_[tail_remote_index_ % elem_size_.size()];
  GPR_ASSERT(table_size_ >= removing_size);
  table_size_ -= removing_size;
  table_elems_--;
}

void HPackEncoderTable::Rebuild(uint32_t capacity) {
  decltype(elem_size_) new_elem_size(capacity);
  GPR_ASSERT(table_elems_ <= capacity);
  for (uint32_t i = 0; i < table_elems_; i++) {
    uint32_t ofs = tail_remote_index_ + i + 1;
    new_elem_size[ofs % capacity] = elem_size_[ofs % elem_size_.size()];
  }
  elem_size_.swap(new_elem_size);
}

}  // namespace grpc_core
