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
#include <cstdint>

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_index.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_table.h"
#include "src/core/ext/transport/chttp2/transport/popularity_count.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

extern grpc_core::TraceFlag grpc_http_trace;

namespace grpc_core {

class HPackCompressor {
  class SliceIndex;

 public:
  HPackCompressor() = default;
  ~HPackCompressor() = default;

  // Maximum table size we'll actually use.
  static constexpr uint32_t kMaxTableSize = 1024 * 1024;

  void SetMaxTableSize(uint32_t max_table_size);
  void SetMaxUsableSize(uint32_t max_table_size);

  uint32_t test_only_table_size() const {
    return table_.test_only_table_size();
  }

  struct EncodeHeaderOptions {
    uint32_t stream_id;
    bool is_end_of_stream;
    bool use_true_binary_metadata;
    size_t max_frame_size;
    grpc_transport_one_way_stats* stats;
  };

  template <typename HeaderSet>
  void EncodeHeaders(const EncodeHeaderOptions& options,
                     const HeaderSet& headers, grpc_slice_buffer* output) {
    Framer framer(options, this, output);
    headers.Encode(&framer);
  }

  class Framer {
   public:
    Framer(const EncodeHeaderOptions& options, HPackCompressor* compressor,
           grpc_slice_buffer* output);
    ~Framer() { FinishFrame(true); }

    Framer(const Framer&) = delete;
    Framer& operator=(const Framer&) = delete;

    void Encode(grpc_mdelem md);
    void Encode(PathMetadata, const Slice& value);
    void Encode(AuthorityMetadata, const Slice& value);
    void Encode(StatusMetadata, uint32_t status);
    void Encode(GrpcTimeoutMetadata, grpc_millis deadline);
    void Encode(TeMetadata, TeMetadata::ValueType value);
    void Encode(ContentTypeMetadata, ContentTypeMetadata::ValueType value);
    void Encode(SchemeMetadata, SchemeMetadata::ValueType value);
    void Encode(MethodMetadata, MethodMetadata::ValueType method);
    void Encode(UserAgentMetadata, const Slice& slice);
    void Encode(GrpcStatusMetadata, grpc_status_code status);
    void Encode(GrpcMessageMetadata, const Slice& slice) {
      if (slice.empty()) return;
      EmitLitHdrWithNonBinaryStringKeyNotIdx(
          StaticSlice::FromStaticString("grpc-message").c_slice(),
          slice.c_slice());
    }
    template <typename Which>
    void Encode(Which, const typename Which::ValueType& value) {
      const Slice& slice = MetadataValueAsSlice<Which>(value);
      if (absl::EndsWith(Which::key(), "-bin")) {
        EmitLitHdrWithBinaryStringKeyNotIdx(
            StaticSlice::FromStaticString(Which::key()).c_slice(),
            slice.c_slice());
      } else {
        EmitLitHdrWithNonBinaryStringKeyNotIdx(
            StaticSlice::FromStaticString(Which::key()).c_slice(),
            slice.c_slice());
      }
    }

   private:
    friend class SliceIndex;

    struct FramePrefix {
      // index (in output_) of the header for the frame
      size_t header_idx;
      // number of bytes in 'output' when we started the frame - used to
      // calculate frame length
      size_t output_length_at_start_of_frame;
    };

    FramePrefix BeginFrame();
    void FinishFrame(bool is_header_boundary);
    void EnsureSpace(size_t need_bytes);

    void AdvertiseTableSizeChange();
    void EmitIndexed(uint32_t index);
    void EncodeDynamic(grpc_mdelem elem);
    static GPR_ATTRIBUTE_NOINLINE void Log(grpc_mdelem elem);

    void EmitLitHdrIncIdx(uint32_t key_index, grpc_mdelem elem);
    void EmitLitHdrNotIdx(uint32_t key_index, grpc_mdelem elem);
    void EmitLitHdrWithStringKeyIncIdx(grpc_mdelem elem);
    void EmitLitHdrWithNonBinaryStringKeyIncIdx(const grpc_slice& key_slice,
                                                const grpc_slice& value_slice);
    void EmitLitHdrWithBinaryStringKeyNotIdx(const grpc_slice& key_slice,
                                             const grpc_slice& value_slice);
    void EmitLitHdrWithNonBinaryStringKeyNotIdx(const grpc_slice& key_slice,
                                                const grpc_slice& value_slice);
    void EmitLitHdrWithStringKeyNotIdx(grpc_mdelem elem);

    void EncodeAlwaysIndexed(uint32_t* index, const grpc_slice& key,
                             const grpc_slice& value,
                             uint32_t transport_length);

    size_t CurrentFrameSize() const;
    void Add(grpc_slice slice);
    uint8_t* AddTiny(size_t len);

    // maximum size of a frame
    const size_t max_frame_size_;
    bool is_first_frame_ = true;
    const bool use_true_binary_metadata_;
    const bool is_end_of_stream_;
    // output stream id
    const uint32_t stream_id_;
#ifndef NDEBUG
    // have we seen a regular (non-colon-prefixed) header yet?
    bool seen_regular_header_ = false;
#endif
    grpc_slice_buffer* const output_;
    grpc_transport_one_way_stats* const stats_;
    HPackCompressor* const compressor_;
    FramePrefix prefix_;
  };

 private:
  static constexpr size_t kNumFilterValues = 64;
  static constexpr uint32_t kNumCachedGrpcStatusValues = 16;

  void AddKeyWithIndex(grpc_slice_refcount* key_ref, uint32_t new_index,
                       uint32_t key_hash);
  void AddElemWithIndex(grpc_mdelem elem, uint32_t new_index,
                        uint32_t elem_hash, uint32_t key_hash);
  void AddElem(grpc_mdelem elem, size_t elem_size, uint32_t elem_hash,
               uint32_t key_hash);
  void AddKey(grpc_mdelem elem, size_t elem_size, uint32_t key_hash);

  // maximum number of bytes we'll use for the decode table (to guard against
  // peers ooming us by setting decode table size high)
  uint32_t max_usable_size_ = hpack_constants::kInitialTableSize;
  // if non-zero, advertise to the decoder that we'll start using a table
  // of this size
  bool advertise_table_size_change_ = false;
  HPackEncoderTable table_;

  // filter tables for elems: this tables provides an approximate
  // popularity count for particular hashes, and are used to determine whether
  // a new literal should be added to the compression table or not.
  // They track a single integer that counts how often a particular value has
  // been seen. When that count reaches max (255), all values are halved.
  PopularityCount<kNumFilterValues> filter_elems_;

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
    using Stored = RefCountedPtr<grpc_slice_refcount>;

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

  class SliceIndex {
   public:
    void EmitTo(const grpc_slice& key, const Slice& value, Framer* framer);

   private:
    struct ValueIndex {
      ValueIndex(Slice value, uint32_t index) : value(std::move(value)), index(index) {}
      Slice value;
      uint32_t index;
    };
    std::vector<ValueIndex> values_;
  };

  // entry tables for keys & elems: these tables track values that have been
  // seen and *may* be in the decompressor table
  HPackEncoderIndex<KeyElem, kNumFilterValues> elem_index_;
  HPackEncoderIndex<KeySliceRef, kNumFilterValues> key_index_;
  // Index into table_ for the te:trailers metadata element
  uint32_t te_index_ = 0;
  // Index into table_ for the content-type metadata element
  uint32_t content_type_index_ = 0;
  // Index into table_ for the user-agent metadata element
  uint32_t user_agent_index_ = 0;
  // Cached grpc-status values
  uint32_t cached_grpc_status_[kNumCachedGrpcStatusValues] = {};
  // The user-agent string referred to by user_agent_index_
  Slice user_agent_;
  SliceIndex path_index_;
  SliceIndex authority_index_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H */
