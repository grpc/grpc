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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_TABLE_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_TABLE_H

#include <grpc/slice.h>
#include <grpc/support/port_platform.h>
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/transport/metadata.h"

/* HPACK header table */

namespace grpc_core {
namespace chttp2 {

/* last index in the static table */
constexpr int kLastStaticEntry = 61;
/* Initial table size as per the spec */
constexpr uint32_t kInitialHpackTableSize = 4096;
/* Maximum table size that we'll use */
constexpr uint32_t kMaxHpackTableSize = kInitialHpackTableSize;
/* Per entry overhead bytes as per the spec */
constexpr int kHpackEntryOverhead = 32;

class HpackTable {
 public:
  HpackTable();
  ~HpackTable();

  struct Row {
    Row(const metadata::Key* k, metadata::AnyValue&& v)
        : key(k), value(std::move(v)) {}
    Row() = default;
    const metadata::Key* key = nullptr;
    metadata::AnyValue value;
  };

  void SetMaxBytes(uint32_t max_bytes);
  grpc_error* SetCurrentTableSize(uint32_t bytes);

  /* lookup a table entry based on its hpack index */
  const Row* Lookup(uint32_t index);

  /* add a table entry to the index - may mutate *value */
  grpc_error* Add(const metadata::Key* key, metadata::AnyValue* value);

  uint32_t DynamicIndexCount() const { return num_ents_; }

 private:
  static constexpr uint32_t EntriesForBytes(uint32_t bytes) {
    return (bytes + kHpackEntryOverhead - 1) / kHpackEntryOverhead;
  }

  void EvictOneEntry();
  void RebuildEnts(uint32_t new_cap);

  /* the first used entry in ents */
  uint32_t first_ent_ = 0;
  /* how many entries are in the table */
  uint32_t num_ents_ = 0;
  /* the amount of memory used by the table, according to the hpack algorithm */
  uint32_t mem_used_ = 0;
  /* the max memory allowed to be used by the table, according to the hpack
     algorithm */
  uint32_t max_bytes_ = kInitialHpackTableSize;
  /* the currently agreed size of the table, according to the hpack algorithm */
  uint32_t current_table_bytes_ = kInitialHpackTableSize;
  /* Maximum number of entries we could possibly fit in the table, given defined
     overheads */
  uint32_t max_entries_ = EntriesForBytes(kInitialHpackTableSize);
  /* Number of entries allocated in ents */
  uint32_t cap_entries_ = EntriesForBytes(kInitialHpackTableSize);

  /* a circular buffer of headers - this is stored in the opposite order to
     what hpack specifies, in order to simplify table management a little...
     meaning lookups need to SUBTRACT from the end position */
  Row* ents_;
  Row static_ents_[kLastStaticEntry];
};

}  // namespace chttp2
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_TABLE_H */
