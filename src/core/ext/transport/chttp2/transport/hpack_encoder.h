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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_table.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/transport.h"

#define GRPC_CHTTP2_HPACKC_NUM_FILTERS 256
#define GRPC_CHTTP2_HPACKC_NUM_VALUES 256
/* initial table size, per spec */
#define GRPC_CHTTP2_HPACKC_INITIAL_TABLE_SIZE 4096
/* maximum table size we'll actually use */
#define GRPC_CHTTP2_HPACKC_MAX_TABLE_SIZE (1024 * 1024)

extern grpc_core::TraceFlag grpc_http_trace;

namespace grpc_core {
namespace chttp2 {

class HpackEncoder {
 public:
  HpackEncoder();
  ~HpackEncoder();

  void SetMaxTableSize(uint32_t max_table_size);
  void SetMaxUsableSize(uint32_t max_table_size);

  class FrameOptions {
   public:
    grpc_transport_one_way_stats* stats() const { return stats_; }
    size_t max_frame_size() const { return max_frame_size_; }
    bool use_true_binary_metadata() const { return use_true_binary_metadata_; }
    bool is_eof() const { return is_eof_; }

   private:
    bool is_eof_;
    bool use_true_binary_metadata_;
    size_t max_frame_size_;
    grpc_transport_one_way_stats* stats_;
  };

  void EncodeHeader(uint32_t stream_id, const metadata::Collection* md,
                    const FrameOptions& options, grpc_slice_buffer* outbuf);

 private:
  /* if the probability of this item being seen again is < 1/x then don't add
     it to the table */
  static constexpr uint32_t kOneOnAddProbability = 128;

  class Framer;
  class TableIndex {
   public:
    explicit TableIndex(int idx = 0) : idx_(idx) {}

    bool Exists(HpackEncoder* enc) const { return idx_ != 0; }
    uint32_t WireValue(HpackEncoder* enc) const {
      assert(Exists(enc));
      if (idx_ > 0) {
        // dynamic table
        return 1 + GRPC_CHTTP2_LAST_STATIC_ENTRY + enc->tail_remote_index_ +
               enc->table_elems_ - idx_;
      } else if (idx_ < 0) {
        // static table
        return -idx_;
      }
      abort();
    }

    bool operator<(TableIndex rhs) { return idx_ < rhs.idx_; }

   private:
    int idx_;
  };
  struct IndexLookupResult {
    TableIndex idx;
    bool add;
  };

  template <uint32_t kTblSize>
  class FilterTable {
   public:
    FilterTable() {
      sum_ = 0;
      for (uint32_t i = 0; i < kTblSize; i++) values_[i] = 0;
    }

    bool Increment(uint32_t hash) {
      uint32_t idx = hash % kTblSize;
      values_[idx]++;
      if (values_[idx] < 255) {
        ++sum_;
      } else {
        HalveEverything();
      }
      return values_[idx] >= sum_ / kOneOnAddProbability;
    }

   private:
    void HalveEverything() {
      sum_ = 0;
      for (uint32_t i = 0; i < kTblSize; i++) {
        values_[i] /= 2;
        sum_ += values_[i];
      }
    }

    uint8_t values_[kTblSize];
    uint32_t sum_;
  };

  class NoFilter {
   public:
    bool Increment(uint32_t hash) { return true; }
  };

  template <class KV, uint32_t kTblSize, uint32_t kCuckooBit1,
            uint32_t kCuckooBit2>
  class IndexTable {
   public:
    void Add(const KV& kv, uint32_t hash, TableIndex new_index) {
      if (kv.IsStoredEq(elems_[CuckooHash1(hash)])) {
        /* already there: update with new index */
        indices_[CuckooHash1(hash)] = new_index;
      } else if (kv.IsStoredEq(elems_[CuckooHash2(hash)])) {
        /* already there (cuckoo): update with new index */
        indices_[CuckooHash2(hash)] = new_index;
      } else if (KV::IsStoredEmpty(elems_[CuckooHash1(hash)])) {
        /* not there, but a free element: add */
        elems_[CuckooHash1(hash)] = std::move(kv.Stored());
        indices_[CuckooHash1(hash)] = new_index;
      } else if (KV::IsStoredEmpty(elems_[CuckooHash2(hash)])) {
        /* not there (cuckoo), but a free element: add */
        elems_[CuckooHash2(hash)] = std::move(kv.Stored());
        indices_[CuckooHash2(hash)] = new_index;
      } else if (indices_[CuckooHash1(hash)] < indices_[CuckooHash2(hash)]) {
        /* not there: replace oldest */
        elems_[CuckooHash1(hash)] = std::move(kv.Stored());
        indices_[CuckooHash1(hash)] = new_index;
      } else {
        /* not there: replace oldest */
        elems_[CuckooHash2(hash)] = std::move(kv.Stored());
        indices_[CuckooHash2(hash)] = new_index;
      }
    }

    TableIndex Lookup(const KV& kv, uint32_t hash) const {
      if (kv.IsStoredEq(elems_[CuckooHash1(hash)])) {
        return indices_[CuckooHash1(hash)];
      }
      if (kv.IsStoredEq(elems_[CuckooHash2(hash)])) {
        return indices_[CuckooHash2(hash)];
      }
      return TableIndex();
    }

   private:
    static uint32_t CuckooHash1(uint32_t hash) {
      return (hash >> kCuckooBit1) % kTblSize;
    }

    static uint32_t CuckooHash2(uint32_t hash) {
      return (hash >> kCuckooBit2) % kTblSize;
    }

    decltype(static_cast<KV*>(nullptr)->Stored()) elems_[kTblSize];
    TableIndex indices_[kTblSize];
  };

  template <class KV, class TFilterElem, uint32_t kElemTblSize,
            uint32_t kCuckooBit1, uint32_t kCuckooBit2>
  class SingleKeyIndex {
   public:
    IndexLookupResult LookupKeyValue(const KV& kv) {
      if (!kv.HasFastValueHash()) {
        return IndexLookupResult{TableIndex{}, false};
      }
      const uint32_t hash = kv.ValueHash();
      return {index_elem_.Lookup(kv, hash), filter_elem_.Increment(hash)};
    }

    IndexLookupResult LookupKeyOnly(const KV& kv) {
      return IndexLookupResult{key_index_, true};
    }

    void Add(const KV& kv, TableIndex new_index) {
      index_elem_.Add(kv, kv.ValueHash(), new_index);
      key_index_ = new_index;
    }

   private:
    TableIndex key_index_;
    TFilterElem filter_elem_;
    IndexTable<KV, kElemTblSize, kCuckooBit1, kCuckooBit2> index_elem_;
  };

  template <class KV, class TFilterElem, uint32_t kElemTblSize,
            uint32_t kElemCuckooBit1, uint32_t kElemCuckooBit2,
            class TFilterKey, uint32_t kKeyTblSize, uint32_t kKeyCuckooBit1,
            uint32_t kKeyCuckooBit2>
  class MultiKeyIndex {
   public:
    IndexLookupResult LookupKeyValue(const KV& kv) {
      if (!kv.HasFastValueHash()) {
        return IndexLookupResult{TableIndex{}, false};
      }
      const uint32_t hash = GRPC_MDSTR_KV_HASH(kv.KeyHash(), kv.ValueHash());
      return {index_elem_.Lookup(kv, hash), filter_elem_.Increment(hash)};
    }

    IndexLookupResult LookupKeyOnly(const KV& kv) {
      const uint32_t hash = kv.KeyHash();
      return {index_key_.Lookup(kv.Key(), hash), filter_key_.Increment(hash)};
    }

    void Add(const KV& kv, TableIndex new_index) {
      index_elem_.Add(kv, GRPC_MDSTR_KV_HASH(kv.KeyHash(), kv.ValueHash()),
                      new_index);
      index_key_.Add(kv.Key(), kv.KeyHash(), new_index);
    }

   private:
    TFilterElem filter_elem_;
    IndexTable<KV, kElemTblSize, kElemCuckooBit1, kElemCuckooBit2> index_elem_;
    TFilterKey filter_key_;
    IndexTable<decltype(static_cast<KV*>(nullptr)->Key()), kKeyTblSize,
               kKeyCuckooBit1, kKeyCuckooBit2>
        index_key_;
  };

  class PathKV {
   public:
    PathKV(int path) : path_(path) {}
    bool HasFastValueHash() const { return true; }
    bool IsRegularHeader() const { return false; }
    bool IsBinaryHeader() const { return false; }
    bool AllowCompression() const { return true; }
    grpc_slice KeySlice() const { return GRPC_MDSTR_PATH; }
    grpc_slice ValueSlice() const { return metadata::PathSlice(path_); }
    uint32_t ValueHash() const { return path_; }
    size_t SizeInHpackTable() const {
      return 32 + 5 + GRPC_SLICE_LENGTH(metadata::PathSlice(path_));
    }
    int Stored() const { return path_; }
    bool IsStoredEq(int path) const { return path_ == path; }
    static bool IsStoredEmpty(int path) { return path < 0; }

   private:
    const int path_;
  };

  class NamedKeyKV {
   public:
    NamedKeyKV(metadata::NamedKeys key) : key_(key) {}
    metadata::NamedKeys Stored() const { return key_; }
    bool IsStoredEq(metadata::NamedKeys k) const { return k == key_; }
    static bool IsStoredEmpty(metadata::NamedKeys k) {
      return k == metadata::NamedKeys::COUNT;
    }

   private:
    const metadata::NamedKeys key_;
  };

  class NamedKV {
   public:
    NamedKV(metadata::NamedKeys key, grpc_slice value)
        : key_(key), value_(value) {}
    bool HasFastValueHash() const { return grpc_slice_is_interned(value_); }
    bool IsRegularHeader() const { return false; }
    bool IsBinaryHeader() const { return false; }
    bool AllowCompression() const { return true; }
    grpc_slice KeySlice() const { return metadata::NamedKeyKey(key_); }
    grpc_slice ValueSlice() const { return value_; }
    uint32_t ValueHash() const { return grpc_slice_hash(value_); }
    size_t SizeInHpackTable() const {
      return 32 + GRPC_SLICE_LENGTH(metadata::NamedKeyKey(key_)) +
             GRPC_SLICE_LENGTH(value_);
    }
    struct KeyValue {
      KeyValue(metadata::NamedKeys k, grpc_slice v) : key(k), value(v) {}
      KeyValue() = default;
      // use key == COUNT as a terminal value indicating an empty slot
      metadata::NamedKeys key = metadata::NamedKeys::COUNT;
      grpc_slice value = grpc_empty_slice();
    };
    KeyValue Stored() const {
      return KeyValue{key_, grpc_slice_ref_internal(value_)};
    }
    bool IsStoredEq(const KeyValue& other) const {
      return other.key == key_ && grpc_slice_eq(value_, other.value);
    }
    static bool IsStoredEmpty(const KeyValue& p) {
      return p.key == metadata::NamedKeys::COUNT;
    }
    NamedKeyKV Key() const { return NamedKeyKV(key_); }
    uint32_t KeyHash() const { return static_cast<uint8_t>(key_); }

   private:
    const metadata::NamedKeys key_;
    const grpc_slice value_;
  };

  SingleKeyIndex<PathKV, NoFilter, 8, 2, 5> path_index_;
  MultiKeyIndex<NamedKV, FilterTable<64>, 64, 4, 9, NoFilter, 16, 4, 9>
      namedkv_index_;

  // cached hpack table index for various important but simple values
  // naming: <key>_<value>_index_;
  TableIndex method_put_index_;
  TableIndex scheme_grpc_index_;
  TableIndex te_trailers_index_;
  TableIndex content_type_application_slash_grpc_index_;

  void EvictOneEntry();
  TableIndex PrepareTableSpaceForNewElem(size_t elem_size);
  void RebuildElems(uint32_t new_cap);

  template <class KV, class Index>
  void AddToIndex(const KV& kv, Index* index) {
    index->Add(kv, PrepareTableSpaceForNewElem(kv.SizeInHpackTable()));
  }

  static constexpr uint32_t ElemsForBytes(uint32_t bytes) {
    return (bytes + 31) / 32;
  }

  uint32_t max_table_size_ = GRPC_CHTTP2_HPACKC_INITIAL_TABLE_SIZE;
  uint32_t cap_table_elems_ = ElemsForBytes(max_table_size_);
  uint32_t max_table_elems_ = cap_table_elems_;
  /** if true, advertise to the decoder that we'll start using a table
      of this size */
  bool advertise_table_size_change_ = false;
  /** maximum number of bytes we'll use for the decode table (to guard against
      peers ooming us by setting decode table size high) */
  uint32_t max_usable_size_ = GRPC_CHTTP2_HPACKC_INITIAL_TABLE_SIZE;
  /* one before the lowest usable table index */
  uint32_t tail_remote_index_ = 0;
  uint32_t table_size_ = 0;
  uint32_t table_elems_ = 0;

  uint16_t* table_elem_size_;
};

}  // namespace chttp2
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H */
