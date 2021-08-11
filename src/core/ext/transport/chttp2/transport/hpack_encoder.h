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

#include <grpc/support/port_platform.h>

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_index.h"
#include "src/core/ext/transport/chttp2/transport/popularity_count.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

// This should be <= 8. We use 6 to save space.
#define GRPC_CHTTP2_HPACKC_NUM_VALUES_BITS 6
#define GRPC_CHTTP2_HPACKC_NUM_VALUES (1 << GRPC_CHTTP2_HPACKC_NUM_VALUES_BITS)
/* initial table size, per spec */
#define GRPC_CHTTP2_HPACKC_INITIAL_TABLE_SIZE 4096
/* maximum table size we'll actually use */
#define GRPC_CHTTP2_HPACKC_MAX_TABLE_SIZE (1024 * 1024)

extern grpc_core::TraceFlag grpc_http_trace;

struct grpc_chttp2_hpack_compressor {
  uint32_t max_table_size;
  uint32_t max_table_elems;
  uint32_t cap_table_elems;
  /** maximum number of bytes we'll use for the decode table (to guard against
      peers ooming us by setting decode table size high) */
  uint32_t max_usable_size;
  /* one before the lowest usable table index */
  uint32_t tail_remote_index;
  uint32_t table_size;
  uint32_t table_elems;
  uint16_t* table_elem_size;
  /** if non-zero, advertise to the decoder that we'll start using a table
      of this size */
  uint8_t advertise_table_size_change;

  /* filter tables for elems: this tables provides an approximate
     popularity count for particular hashes, and are used to determine whether
     a new literal should be added to the compression table or not.
     They track a single integer that counts how often a particular value has
     been seen. When that count reaches max (255), all values are halved. */
  grpc_core::PopularityCount<GRPC_CHTTP2_HPACKC_NUM_VALUES> filter_elems;

  class KeyElem {
   public:
    class Stored {
     public:
      Stored() : elem_(GRPC_MDNULL) {}
      explicit Stored(grpc_mdelem elem) : elem_(GRPC_MDELEM_REF(elem)) {}
      Stored(const Stored& other) : elem_(GRPC_MDELEM_REF(other.elem_)) {}
      Stored& operator=(Stored other) {
        std::swap(elem_, other.elem_);
        return *this;
      }
      ~Stored() { GRPC_MDELEM_UNREF(elem_); }

      const grpc_mdelem& elem() const { return elem_; }

      bool operator==(const Stored& other) const noexcept {
        return elem_.payload == other.elem_.payload;
      }

     private:
      grpc_mdelem elem_;
    };

    KeyElem(grpc_mdelem elem, uint32_t hash) : elem_(elem), hash_(hash) {}
    KeyElem(const KeyElem&);
    KeyElem& operator=(const KeyElem&);

    uint32_t hash() const {
      // TODO(ctiller): unify this with what's in the cc file when we move this
      // code to c++
      return hash_ >> 6;
    }

    Stored stored() const { return Stored(elem_); }

    bool operator==(const Stored& stored) const noexcept {
      return elem_.payload == stored.elem().payload;
    }

   private:
    grpc_mdelem elem_;
    uint32_t hash_;
  };

  class KeySliceRef {
   public:
    using Stored = grpc_core::RefCountedPtr<grpc_slice_refcount>;

    KeySliceRef(grpc_slice_refcount* ref, uint32_t hash)
        : ref_(ref), hash_(hash) {}
    KeySliceRef(const KeySliceRef&) = delete;
    KeySliceRef& operator=(const KeySliceRef&) = delete;

    uint32_t hash() const {
      // TODO(ctiller): unify this with what's in the cc file when we move this
      // code to c++
      return hash_ >> 6;
    }

    Stored stored() const {
      ref_->Ref();
      return Stored(ref_);
    }

    bool operator==(const Stored& stored) const noexcept {
      return ref_ == stored.get();
    }

   private:
    grpc_slice_refcount* ref_;
    uint32_t hash_;
  };

  /* entry tables for keys & elems: these tables track values that have been
     seen and *may* be in the decompressor table */
  grpc_core::ManualConstructor<
      grpc_core::HPackEncoderIndex<KeyElem, GRPC_CHTTP2_HPACKC_NUM_VALUES>>
      elem_table;
  grpc_core::ManualConstructor<
      grpc_core::HPackEncoderIndex<KeySliceRef, GRPC_CHTTP2_HPACKC_NUM_VALUES>>
      key_table;
};

void grpc_chttp2_hpack_compressor_init(grpc_chttp2_hpack_compressor* c);
void grpc_chttp2_hpack_compressor_destroy(grpc_chttp2_hpack_compressor* c);
void grpc_chttp2_hpack_compressor_set_max_table_size(
    grpc_chttp2_hpack_compressor* c, uint32_t max_table_size);
void grpc_chttp2_hpack_compressor_set_max_usable_size(
    grpc_chttp2_hpack_compressor* c, uint32_t max_table_size);

struct grpc_encode_header_options {
  uint32_t stream_id;
  bool is_eof;
  bool use_true_binary_metadata;
  size_t max_frame_size;
  grpc_transport_one_way_stats* stats;
};
void grpc_chttp2_encode_header(grpc_chttp2_hpack_compressor* c,
                               grpc_mdelem** extra_headers,
                               size_t extra_headers_size,
                               grpc_metadata_batch* metadata,
                               const grpc_encode_header_options* options,
                               grpc_slice_buffer* outbuf);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H */
