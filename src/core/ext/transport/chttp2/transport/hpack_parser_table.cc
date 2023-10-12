//
//
// Copyright 2015 gRPC authors.
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
//
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/hpack_parser_table.h"

#include <stdlib.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/hpack_constants.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parse_result.h"
#include "src/core/ext/transport/chttp2/transport/http_trace.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {

void HPackTable::MementoRingBuffer::Put(Memento m) {
  GPR_ASSERT(num_entries_ < max_entries_);
  if (entries_.size() < max_entries_) {
    ++num_entries_;
    return entries_.push_back(std::move(m));
  }
  size_t index = (first_entry_ + num_entries_) % max_entries_;
  entries_[index] = std::move(m);
  ++num_entries_;
}

auto HPackTable::MementoRingBuffer::PopOne() -> Memento {
  GPR_ASSERT(num_entries_ > 0);
  size_t index = first_entry_ % max_entries_;
  ++first_entry_;
  --num_entries_;
  return std::move(entries_[index]);
}

auto HPackTable::MementoRingBuffer::Lookup(uint32_t index) const
    -> const Memento* {
  if (index >= num_entries_) return nullptr;
  uint32_t offset = (num_entries_ - 1u - index + first_entry_) % max_entries_;
  return &entries_[offset];
}

void HPackTable::MementoRingBuffer::Rebuild(uint32_t max_entries) {
  if (max_entries == max_entries_) return;
  max_entries_ = max_entries;
  std::vector<Memento> entries;
  entries.reserve(num_entries_);
  for (size_t i = 0; i < num_entries_; i++) {
    entries.push_back(
        std::move(entries_[(first_entry_ + i) % entries_.size()]));
  }
  first_entry_ = 0;
  entries_.swap(entries);
}

void HPackTable::MementoRingBuffer::ForEach(
    absl::FunctionRef<void(uint32_t, const Memento&)> f) const {
  uint32_t index = 0;
  while (auto* m = Lookup(index++)) {
    f(index, *m);
  }
}

// Evict one element from the table
void HPackTable::EvictOne() {
  auto first_entry = entries_.PopOne();
  GPR_ASSERT(first_entry.md.transport_size() <= mem_used_);
  mem_used_ -= first_entry.md.transport_size();
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

bool HPackTable::SetCurrentTableSize(uint32_t bytes) {
  if (current_table_bytes_ == bytes) return true;
  if (bytes > max_bytes_) return false;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "Update hpack parser table size to %d", bytes);
  }
  while (mem_used_ > bytes) {
    EvictOne();
  }
  current_table_bytes_ = bytes;
  uint32_t new_cap = std::max(hpack_constants::EntriesForBytes(bytes),
                              hpack_constants::kInitialTableEntries);
  entries_.Rebuild(new_cap);
  return true;
}

bool HPackTable::Add(Memento md) {
  if (current_table_bytes_ > max_bytes_) return false;

  // we can't add elements bigger than the max table size
  if (md.md.transport_size() > current_table_bytes_) {
    AddLargerThanCurrentTableSize();
    return true;
  }

  // evict entries to ensure no overflow
  while (md.md.transport_size() >
         static_cast<size_t>(current_table_bytes_) - mem_used_) {
    EvictOne();
  }

  // copy the finalized entry in
  mem_used_ += md.md.transport_size();
  entries_.Put(std::move(md));
  return true;
}

void HPackTable::AddLargerThanCurrentTableSize() {
  // HPACK draft 10 section 4.4 states:
  // If the size of the new entry is less than or equal to the maximum
  // size, that entry is added to the table.  It is not an error to
  // attempt to add an entry that is larger than the maximum size; an
  // attempt to add an entry larger than the entire table causes
  // the table to be emptied of all existing entries, and results in an
  // empty table.
  while (entries_.num_entries()) {
    EvictOne();
  }
}

std::string HPackTable::TestOnlyDynamicTableAsString() const {
  std::string out;
  entries_.ForEach([&out](uint32_t i, const Memento& m) {
    if (m.parse_status == nullptr) {
      absl::StrAppend(&out, i, ": ", m.md.DebugString(), "\n");
    } else {
      absl::StrAppend(&out, i, ": ", m.parse_status->Materialize().ToString(),
                      "\n");
    }
  });
  return out;
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

HPackTable::Memento MakeMemento(size_t i) {
  auto sm = kStaticTable[i];
  return HPackTable::Memento{
      grpc_metadata_batch::Parse(
          sm.key, Slice::FromStaticString(sm.value), true,
          strlen(sm.key) + strlen(sm.value) + hpack_constants::kEntryOverhead,
          [](absl::string_view, const Slice&) {
            abort();  // not expecting to see this
          }),
      nullptr};
}

}  // namespace

HPackTable::StaticMementos::StaticMementos() {
  for (uint32_t i = 0; i < hpack_constants::kLastStaticEntry; i++) {
    memento[i] = MakeMemento(i);
  }
}

}  // namespace grpc_core
