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

#include "src/core/ext/transport/chttp2/transport/hpack_table.h"

#include <assert.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/support/murmur_hash.h"

extern grpc_core::TraceFlag grpc_http_trace;

namespace grpc_core {
namespace chttp2 {

HpackTable::HpackTable() { ents_ = ArrayNew<Row>(cap_entries_); }
HpackTable::~HpackTable() { ArrayDelete(ents_, cap_entries_); }

const HpackTable::Row* HpackTable::Lookup(uint32_t tbl_index) {
  /* Static table comes first, just return an entry from it */
  if (tbl_index <= kLastStaticEntry) {
    return &static_ents_[tbl_index - 1];
  }
  /* Otherwise, find the value in the list of valid entries */
  tbl_index -= (kLastStaticEntry + 1);
  if (tbl_index < num_ents_) {
    uint32_t offset = (num_ents_ - 1u - tbl_index + first_ent_) % cap_entries_;
    return &ents_[offset];
  }
  /* Invalid entry: return error */
  return nullptr;
}

grpc_error* HpackTable::Add(const metadata::Key* key,
                            metadata::AnyValue* value) {
  /* determine how many bytes of buffer this entry represents */
  size_t elem_bytes = key->SizeInHpackTable(*value);

  if (current_table_bytes_ > max_bytes_) {
    char* msg;
    gpr_asprintf(
        &msg,
        "HPACK max table size reduced to %d but not reflected by hpack "
        "stream (still at %d)",
        max_bytes_, current_table_bytes_);
    grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return err;
  }

  /* we can't add elements bigger than the max table size */
  if (elem_bytes > current_table_bytes_) {
    /* HPACK draft 10 section 4.4 states:
     * If the size of the new entry is less than or equal to the maximum
     * size, that entry is added to the table.  It is not an error to
     * attempt to add an entry that is larger than the maximum size; an
     * attempt to add an entry larger than the entire table causes
     * the table
     * to be emptied of all existing entries, and results in an
     * empty table.
     */
    while (num_ents_) {
      EvictOneEntry();
    }
    return GRPC_ERROR_NONE;
  }

  /* evict entries to ensure no overflow */
  while (elem_bytes > (size_t)current_table_bytes_ - mem_used_) {
    EvictOneEntry();
  }

  /* copy the finalized entry in */
  ents_[(first_ent_ + num_ents_) % cap_entries_] = Row{key, std::move(*value)};

  /* update accounting values */
  num_ents_++;
  mem_used_ += (uint32_t)elem_bytes;
  return GRPC_ERROR_NONE;
}

void HpackTable::EvictOneEntry() {
  Row* row = &ents_[first_ent_];
  uint32_t elem_bytes = row->key->SizeInHpackTable(row->value);
  GPR_ASSERT(elem_bytes <= mem_used_);
  mem_used_ -= elem_bytes;
  first_ent_ = ((first_ent_ + 1) % cap_entries_);
  num_ents_--;
}

void HpackTable::RebuildEnts(uint32_t new_cap) {
  Row* ents = ArrayNew<Row>(new_cap);
  uint32_t i;

  for (i = 0; i < num_ents_; i++) {
    ents[i] = std::move(ents_[(first_ent_ + i) % cap_entries_]);
  }
  ArrayDelete(ents_, cap_entries_);
  ents_ = ents;
  cap_entries_ = new_cap;
  first_ent_ = 0;
}

void HpackTable::SetMaxBytes(uint32_t max_bytes) {
  if (max_bytes_ == max_bytes) {
    return;
  }
  if (grpc_http_trace.enabled()) {
    gpr_log(GPR_DEBUG, "Update hpack parser max size to %d", max_bytes);
  }
  while (mem_used_ > max_bytes) {
    EvictOneEntry();
  }
  max_bytes_ = max_bytes;
}

grpc_error* HpackTable::SetCurrentTableSize(uint32_t bytes) {
  if (current_table_bytes_ == bytes) {
    return GRPC_ERROR_NONE;
  }
  if (bytes > max_bytes_) {
    char* msg;
    gpr_asprintf(&msg,
                 "Attempt to make hpack table %d bytes when max is %d bytes",
                 bytes, max_bytes_);
    grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return err;
  }
  if (grpc_http_trace.enabled()) {
    gpr_log(GPR_DEBUG, "Update hpack parser table size to %d", bytes);
  }
  while (mem_used_ > bytes) {
    EvictOneEntry();
  }
  current_table_bytes_ = bytes;
  max_entries_ = EntriesForBytes(bytes);
  if (max_entries_ > cap_entries_) {
    RebuildEnts(std::max(max_entries_, 2 * cap_entries_));
  } else if (max_entries_ < cap_entries_ / 3) {
    uint32_t new_cap = std::max(max_entries_, 16u);
    if (new_cap != cap_entries_) {
      RebuildEnts(new_cap);
    }
  }
  return GRPC_ERROR_NONE;
}

}  // namespace chttp2
}  // namespace grpc_core
