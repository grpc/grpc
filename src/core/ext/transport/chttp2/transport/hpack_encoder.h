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
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_table.h"
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

    void Encode(const Slice& key, const Slice& value);
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
    void Encode(GrpcEncodingMetadata, grpc_compression_algorithm value);
    void Encode(GrpcAcceptEncodingMetadata, CompressionAlgorithmSet value);
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
    void EmitLitHdrWithNonBinaryStringKeyIncIdx(const grpc_slice& key_slice,
                                                const grpc_slice& value_slice);
    void EmitLitHdrWithBinaryStringKeyNotIdx(const grpc_slice& key_slice,
                                             const grpc_slice& value_slice);
    void EmitLitHdrWithNonBinaryStringKeyNotIdx(const grpc_slice& key_slice,
                                                const grpc_slice& value_slice);

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

  // maximum number of bytes we'll use for the decode table (to guard against
  // peers ooming us by setting decode table size high)
  uint32_t max_usable_size_ = hpack_constants::kInitialTableSize;
  // if non-zero, advertise to the decoder that we'll start using a table
  // of this size
  bool advertise_table_size_change_ = false;
  HPackEncoderTable table_;

  class SliceRef {
   public:
    using Stored = Slice;

    explicit SliceRef(const Slice* slice) : ref_(slice) {}
    SliceRef(const SliceRef&) = delete;
    SliceRef& operator=(const SliceRef&) = delete;

    uint32_t hash() const { return grpc_slice_hash_internal(ref_->c_slice()); }
    Stored stored() const { return ref_->Ref(); }

    bool operator==(const Stored& stored) const noexcept {
      return *ref_ == stored;
    }

   private:
    const Slice* const ref_;
  };

  class SliceIndex {
   public:
    void EmitTo(const grpc_slice& key, const Slice& value, Framer* framer);

   private:
    HPackEncoderIndex<SliceRef, kNumFilterValues> index_;
  };

  // Index into table_ for the te:trailers metadata element
  uint32_t te_index_ = 0;
  // Index into table_ for the content-type metadata element
  uint32_t content_type_index_ = 0;
  // Index into table_ for the user-agent metadata element
  uint32_t user_agent_index_ = 0;
  // Cached grpc-status values
  uint32_t cached_grpc_status_[kNumCachedGrpcStatusValues] = {};
  // Cached grpc-encoding values
  uint32_t cached_grpc_encoding_[GRPC_COMPRESS_ALGORITHMS_COUNT] = {};
  // Cached grpc-accept-encoding value
  uint32_t grpc_accept_encoding_index_ = 0;
  // The grpc-accept-encoding string referred to by grpc_accept_encoding_index_
  CompressionAlgorithmSet grpc_accept_encoding_;
  // The user-agent string referred to by user_agent_index_
  Slice user_agent_;
  SliceIndex path_index_;
  SliceIndex authority_index_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H */
