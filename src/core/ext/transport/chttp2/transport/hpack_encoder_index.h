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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_INDEX_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_INDEX_H

#include <grpc/impl/codegen/port_platform.h>

#include "absl/types/optional.h"

namespace grpc_core {

// A fixed size mapping of a key to a chronologically ordered index
template <typename Key, size_t kNumEntries>
class HPackEncoderIndex {
 public:
  using Index = uint32_t;

  HPackEncoderIndex() : entries_{} {}

  // If key exists in the table, update it to a new index.
  // If it does not and there is an empty slot, add it to the index.
  // Finally, if it does not and there is no empty slot, evict the oldest
  // conflicting member.
  void Insert(const Key& key, Index new_index) {
    auto* const cuckoo_first = first_slot(key);
    if (cuckoo_first->UpdateOrAdd(key, new_index)) return;
    auto* const cuckoo_second = second_slot(key);
    if (cuckoo_second->UpdateOrAdd(key, new_index)) return;
    auto* const clobber = Older(cuckoo_first, cuckoo_second);
    clobber->key = key.stored();
    clobber->index = new_index;
  }

  // Lookup key and return its index, or return empty if it's not in this table.
  absl::optional<Index> Lookup(const Key& key) {
    auto* const cuckoo_first = first_slot(key);
    if (key == cuckoo_first->key) return cuckoo_first->index;
    auto* const cuckoo_second = second_slot(key);
    if (key == cuckoo_second->key) return cuckoo_second->index;
    return {};
  }

 private:
  using StoredKey = typename Key::Stored;

  // One entry in the index
  struct Entry {
    Entry() : key{}, index{} {};

    StoredKey key;
    Index index;

    // Update this entry if it matches key, otherwise if it's empty add it.
    // If neither happens, return false.
    bool UpdateOrAdd(const Key& new_key, Index new_index) {
      if (new_key == key) {
        index = new_index;
        return true;
      } else if (key == StoredKey()) {
        key = new_key.stored();
        index = new_index;
        return true;
      } else {
        return false;
      }
    }
  };

  static Entry* Older(Entry* a, Entry* b) {
    if (a->index < b->index) {
      return a;
    } else {
      return b;
    }
  }

  // Return the first slot in which key could be stored.
  Entry* first_slot(const Key& key) {
    return &entries_[key.hash() % kNumEntries];
  }

  // Return the second slot in which key could be stored.
  Entry* second_slot(const Key& key) {
    return &entries_[(key.hash() / kNumEntries) % kNumEntries];
  }

  // Fixed size entry map.
  // We store each key/value pair in two slots based on it's hash value.
  // They can be evicted individually.
  Entry entries_[kNumEntries];
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_INDEX_H
