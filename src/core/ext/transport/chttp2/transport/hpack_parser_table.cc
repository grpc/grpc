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

#include "src/core/ext/transport/chttp2/transport/hpack_constants.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/murmur_hash.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/validate_metadata.h"

extern grpc_core::TraceFlag grpc_http_trace;

namespace grpc_core {

HPackTable::HPackTable() : static_metadata_(GetStaticMementos()) {}

HPackTable::~HPackTable() = default;

/* Evict one element from the table */
void HPackTable::EvictOne() {
  auto first_entry = std::move(entries_[first_entry_]);
  GPR_ASSERT(first_entry.transport_size() <= mem_used_);
  mem_used_ -= first_entry.transport_size();
  first_entry_ = ((first_entry_ + 1) % entries_.size());
  num_entries_--;
}

void HPackTable::Rebuild(uint32_t new_cap) {
  EntriesVec entries;
  entries.resize(new_cap);
  for (size_t i = 0; i < num_entries_; i++) {
    entries[i] = std::move(entries_[(first_entry_ + i) % entries_.size()]);
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

grpc_error_handle HPackTable::Add(Memento md) {
  if (current_table_bytes_ > max_bytes_) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrFormat(
        "HPACK max table size reduced to %d but not reflected by hpack "
        "stream (still at %d)",
        max_bytes_, current_table_bytes_));
  }

  // we can't add elements bigger than the max table size
  if (md.transport_size() > current_table_bytes_) {
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
  while (md.transport_size() >
         static_cast<size_t>(current_table_bytes_) - mem_used_) {
    EvictOne();
  }

  // copy the finalized entry in
  mem_used_ += md.transport_size();
  entries_[(first_entry_ + num_entries_) % entries_.size()] = std::move(md);

  // update accounting values
  num_entries_++;
  return GRPC_ERROR_NONE;
}

namespace {
struct StaticTableEntry {
  const char* key;
  const char* value;
};

const StaticTableEntry kStaticTable[hpack_constants::kLastStaticEntry] = {
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "204"},
    {":status", "206"},
    {":status", "304"},
    {":status", "400"},
    {":status", "404"},
    {":status", "500"},
    {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"range", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"www-authenticate", ""},
};

GPR_ATTRIBUTE_NOINLINE HPackTable::Memento MakeMemento(size_t i) {
  auto sm = kStaticTable[i];
  return grpc_metadata_batch::Parse(
      sm.key, Slice::FromStaticString(sm.value),
      strlen(sm.key) + strlen(sm.value) + hpack_constants::kEntryOverhead,
      [](absl::string_view, const Slice&) {
        abort();  // not expecting to see this
      });
}

}  // namespace

const HPackTable::StaticMementos& HPackTable::GetStaticMementos() {
  static const StaticMementos static_mementos;
  return static_mementos;
}

HPackTable::StaticMementos::StaticMementos() {
  for (uint32_t i = 0; i < hpack_constants::kLastStaticEntry; i++) {
    memento[i] = MakeMemento(i);
  }
}

}  // namespace grpc_core
