/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/hpack_parser_table.h"

#include <assert.h>
#include <string.h>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/murmur_hash.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/static_metadata.h"

extern grpc_core::TraceFlag grpc_http_trace;

namespace grpc_core {

HPackTable::HPackTable() = default;

HPackTable::~HPackTable() {
  for (size_t i = 0; i < num_entries_; i++) {
    GRPC_MDELEM_UNREF(entries_[(first_entry_ + i) % entries_.size()]);
  }
}

/* Evict one element from the table */
void HPackTable::EvictOne() {
  grpc_mdelem first_entry = entries_[first_entry_];
  size_t elem_bytes = GRPC_SLICE_LENGTH(GRPC_MDKEY(first_entry)) +
                      GRPC_SLICE_LENGTH(GRPC_MDVALUE(first_entry)) +
                      hpack_constants::kEntryOverhead;
  GPR_ASSERT(elem_bytes <= mem_used_);
  mem_used_ -= static_cast<uint32_t>(elem_bytes);
  first_entry_ = ((first_entry_ + 1) % entries_.size());
  num_entries_--;
  GRPC_MDELEM_UNREF(first_entry);
}

void HPackTable::Rebuild(uint32_t new_cap) {
  EntriesVec entries;
  entries.resize(new_cap);
  for (size_t i = 0; i < num_entries_; i++) {
    entries[i] = entries_[(first_entry_ + i) % entries_.size()];
  }
  first_entry_ = 0;
  entries_.swap(entries);
}

void HPackTable::SetMaxBytes(uint32_t max_bytes) {
  if (max_bytes_ == max_bytes) {
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "Update hpack parser max size to %d", max_bytes);
  }
  while (mem_used_ > max_bytes) {
    EvictOne();
  }
  max_bytes_ = max_bytes;
}

grpc_error_handle HPackTable::SetCurrentTableSize(uint32_t bytes) {
  if (current_table_bytes_ == bytes) {
    return GRPC_ERROR_NONE;
  }
  if (bytes > max_bytes_) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrFormat(
        "Attempt to make hpack table %d bytes when max is %d bytes", bytes,
        max_bytes_));
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "Update hpack parser table size to %d", bytes);
  }
  while (mem_used_ > bytes) {
    EvictOne();
  }
  current_table_bytes_ = bytes;
  max_entries_ = hpack_constants::EntriesForBytes(bytes);
  if (max_entries_ > entries_.size()) {
    Rebuild(max_entries_);
  } else if (max_entries_ < entries_.size() / 3) {
    // TODO(ctiller): move to resource quota system, only shrink under memory
    // pressure
    uint32_t new_cap =
        std::max(max_entries_, static_cast<uint32_t>(kInlineEntries));
    if (new_cap != entries_.size()) {
      Rebuild(new_cap);
    }
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle HPackTable::Add(grpc_mdelem md) {
  /* determine how many bytes of buffer this entry represents */
  size_t elem_bytes = GRPC_SLICE_LENGTH(GRPC_MDKEY(md)) +
                      GRPC_SLICE_LENGTH(GRPC_MDVALUE(md)) +
                      hpack_constants::kEntryOverhead;

  if (current_table_bytes_ > max_bytes_) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrFormat(
        "HPACK max table size reduced to %d but not reflected by hpack "
        "stream (still at %d)",
        max_bytes_, current_table_bytes_));
  }

  // we can't add elements bigger than the max table size
  if (elem_bytes > current_table_bytes_) {
    // HPACK draft 10 section 4.4 states:
    // If the size of the new entry is less than or equal to the maximum
    // size, that entry is added to the table.  It is not an error to
    // attempt to add an entry that is larger than the maximum size; an
    // attempt to add an entry larger than the entire table causes
    // the table to be emptied of all existing entries, and results in an
    // empty table.
    while (num_entries_) {
      EvictOne();
    }
    return GRPC_ERROR_NONE;
  }

  // evict entries to ensure no overflow
  while (elem_bytes > static_cast<size_t>(current_table_bytes_) - mem_used_) {
    EvictOne();
  }

  // copy the finalized entry in
  entries_[(first_entry_ + num_entries_) % entries_.size()] =
      GRPC_MDELEM_REF(md);

  // update accounting values
  num_entries_++;
  mem_used_ += static_cast<uint32_t>(elem_bytes);
  return GRPC_ERROR_NONE;
}

}  // namespace grpc_core
