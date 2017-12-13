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

   private:
    bool is_eof_;
    bool use_true_binary_metadata_;
    size_t max_frame_size_;
    grpc_transport_one_way_stats* stats_;
  };

  void EncodeHeader(uint32_t stream_id, const metadata::Collection* md,
                    const FrameOptions& options, grpc_slice_buffer* outbuf);

 private:
  class Framer;
  class TableIndex {
   public:
    operator bool() const { return idx_ > 0; }

   private:
    int idx_;
  };
  struct IndexLookupResult {
    TableIndex idx;
    bool add;
  };

  template <size_t kTblSize>
  class FilterTable {};

  class NoFilter {};

  template <size_t kTblSize>
  class IndexTable {};

  template <class KV, class TFilterElem, size_t kElemTblSize>
  class SingleKeyIndex {
   public:
    IndexLookupResult LookupKeyValue(const KV& kv) {
      if (!kv.HasFastValueHash()) {
        return IndexLookupResult{TableIndex{}, false};
      }
      const uint32_t hash = kv.ValueHash();
      filter_elem_.Increment(hash);
      return index_elem_.Lookup(kv, hash);
    }

    IndexLookupResult LookupKeyOnly(const KV& kv) {
      return IndexLookupResult{key_index_, true};
    }

    void Add(const KV& kv);

   private:
    TableIndex key_index_;
    TFilterElem filter_elem_;
    IndexTable<kElemTblSize> index_elem_;
  };

  class PathKV {
   public:
    PathKV(int path) : path_(path) {}
    bool IsRegularHeader() const { return false; }
    bool AllowCompression() const { return true; }
    grpc_slice KeySlice() const { return GRPC_MDSTR_PATH; }
    grpc_slice ValueSlice() const { return metadata::PathSlice(path_); }
    size_t SizeInHpackTable() const {
      return 32 + 5 + GRPC_SLICE_LENGTH(metadata::PathSlice(path_));
    }

   private:
    const int path_;
  };

  SingleKeyIndex<PathKV, NoFilter, 8> path_index_;

  uint32_t filter_elems_sum_;
  uint32_t max_table_size_;
  uint32_t max_table_elems_;
  uint32_t cap_table_elems_;
  /** if true, advertise to the decoder that we'll start using a table
      of this size */
  bool advertise_table_size_change_;
  /** maximum number of bytes we'll use for the decode table (to guard against
      peers ooming us by setting decode table size high) */
  uint32_t max_usable_size_;
  /* one before the lowest usable table index */
  uint32_t tail_remote_index_;
  uint32_t table_size_;
  uint32_t table_elems_;

  /* filter tables for elems: this tables provides an approximate
     popularity count for particular hashes, and are used to determine whether
     a new literal should be added to the compression table or not.
     They track a single integer that counts how often a particular value has
     been seen. When that count reaches max (255), all values are halved. */
  uint8_t filter_elems_[GRPC_CHTTP2_HPACKC_NUM_FILTERS];

  /* entry tables for keys & elems: these tables track values that have been
     seen and *may* be in the decompressor table */
  grpc_slice entries_keys_[GRPC_CHTTP2_HPACKC_NUM_VALUES];
  grpc_mdelem entries_elems_[GRPC_CHTTP2_HPACKC_NUM_VALUES];
  uint32_t indices_keys_[GRPC_CHTTP2_HPACKC_NUM_VALUES];
  uint32_t indices_elems_[GRPC_CHTTP2_HPACKC_NUM_VALUES];

  uint16_t* table_elem_size_;
};

}  // namespace chttp2
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H */
