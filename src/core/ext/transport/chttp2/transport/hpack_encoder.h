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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "hpack_encoder_table.h"

#include <grpc/impl/compression_types.h>
#include <grpc/slice.h>
#include <grpc/status.h>

#include "src/core/ext/transport/chttp2/transport/hpack_constants.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_table.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/timeout_encoding.h"
#include "src/core/lib/transport/transport.h"

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
    SliceBuffer raw;
    Encoder encoder(this, options.use_true_binary_metadata, raw);
    headers.Encode(&encoder);
    Frame(options, raw, output);
  }

  template <typename HeaderSet>
  void EncodeRawHeaders(const HeaderSet& headers, SliceBuffer& output) {
    Encoder encoder(this, true, output);
    headers.Encode(&encoder);
  }

 private:
  class Encoder;

  template <typename MetadataTrait, typename CompressonTraits>
  class Compressor;

  template <typename MetadataTrait>
  class Compressor<MetadataTrait, NoCompressionCompressor> {
   public:
    void EncodeWith(MetadataTrait trait,
                    const typename MetadataTrait::ValueType& value,
                    Encoder* encoder) {
      const Slice& slice = MetadataValueAsSlice<MetadataTrait>(value);
      if (absl::EndsWith(MetadataTrait::key(), "-bin")) {
        encoder->EmitLitHdrWithBinaryStringKeyNotIdx(
            Slice::FromStaticString(MetadataTrait::key()), slice.Ref());
      } else {
        encoder->EmitLitHdrWithNonBinaryStringKeyNotIdx(
            Slice::FromStaticString(MetadataTrait::key()), slice.Ref());
      }
    }
  };

  template <typename MetadataTrait>
  class Compressor<MetadataTrait, FrequentKeyWithNoValueCompressionCompressor> {
   public:
    void EncodeWith(MetadataTrait trait,
                    const typename MetadataTrait::ValueType& value,
                    Encoder* encoder) {
      const Slice& slice = MetadataValueAsSlice<MetadataTrait>(value);
      encoder->EncodeRepeatingSliceValue(MetadataTrait::key(), slice,
                                         &some_sent_value_,
                                         HPackEncoderTable::MaxEntrySize());
    }

   private:
    // Some previously sent value with this tag.
    uint32_t some_sent_value_ = 0;
  };

  class StableValueCompressorImpl {
   public:
    void EncodeWith(absl::string_view key, const Slice& value,
                    Encoder* encoder) {
      if (hpack_constants::SizeForEntry(key.size(), value.size()) >
          HPackEncoderTable::MaxEntrySize()) {
        encoder->EmitLitHdrWithNonBinaryStringKeyNotIdx(
            Slice::FromStaticString(key), value.Ref());
        return;
      }
      if (!value.is_equivalent(previously_sent_value_)) {
        previously_sent_value_ = value.Ref();
        previously_sent_index_ = 0;
      }
      encoder->EncodeAlwaysIndexed(
          &previously_sent_index_, key, value.Ref(),
          hpack_constants::SizeForEntry(key.size(), value.size()));
    }

   private:
    // Previously sent value
    Slice previously_sent_value_;
    // And its index in the table
    uint32_t previously_sent_index_ = 0;
  };

  template <typename MetadataTrait>
  class Compressor<MetadataTrait, StableValueCompressor>
      : public StableValueCompressorImpl {
   public:
    void EncodeWith(MetadataTrait,
                    const typename MetadataTrait::ValueType& value,
                    Encoder* encoder) {
      StableValueCompressorImpl::EncodeWith(
          MetadataTrait::key(), MetadataValueAsSlice<MetadataTrait>(value),
          encoder);
    }
  };

  template <typename MetadataTrait,
            typename MetadataTrait::ValueType known_value>
  class Compressor<
      MetadataTrait,
      KnownValueCompressor<typename MetadataTrait::ValueType, known_value>> {
   public:
    void EncodeWith(MetadataTrait,
                    const typename MetadataTrait::ValueType& value,
                    Encoder* encoder) {
      if (value != known_value) {
        gpr_log(GPR_ERROR, absl::StrCat("Not encoding bad ",
                                        MetadataTrait::key(), " header"));
        return;
      }
      encoder->EncodeAlwaysIndexed(
          &previously_sent_index_, MetadataTrait::key(),
          MetadataTrait::Encode(known_value),
          MetadataTrait::key().size() +
              MetadataTrait::Encode(known_value).Length() +
              hpack_constants::kEntryOverhead);
    }

   private:
    uint32_t previously_sent_index_ = 0;
  };
  template <typename MetadataTrait, size_t N>
  class Compressor<MetadataTrait, SmallIntegralValuesCompressor<N>> {
   public:
    void EncodeWith(MetadataTrait,
                    const typename MetadataTrait::ValueType& value,
                    Encoder* encoder) {
      uint32_t* index = nullptr;
      if (value < N) {
        index = &previously_sent_[static_cast<uint32_t>(value)];
        if (encoder->EncodeIndexed(index)) return;
      }
      auto key = Slice::FromStaticString(MetadataTrait::key());
      auto encoded_value = GrpcEncodingMetadata::Encode(value);
      size_t transport_length = key.length() + encoded_value.length() +
                                hpack_constants::kEntryOverhead;
      if (index != nullptr) {
        *index = compressor_->table_.AllocateIndex(transport_length);
        EmitLitHdrWithNonBinaryStringKeyIncIdx(std::move(key),
                                               std::move(encoded_value));
      } else {
        EmitLitHdrWithNonBinaryStringKeyNotIdx(std::move(key),
                                               std::move(encoded_value));
      }
    }

   private:
    uint32_t previously_sent_[N] = {};
  };

  class SliceIndex {
   public:
    void EmitTo(absl::string_view key, const Slice& value, Encoder* encoder);

   private:
    struct ValueIndex {
      ValueIndex(Slice value, uint32_t index)
          : value(std::move(value)), index(index) {}
      Slice value;
      uint32_t index;
    };
    std::vector<ValueIndex> values_;
  };

  template <typename MetadataTrait>
  class Compressor<MetadataTrait, SmallSetOfValuesCompressor> {
   public:
    void EncodeWith(MetadataTrait, const Slice& value, Encoder* encoder);

   private:
    SliceIndex index_;
  };

  struct PreviousTimeout {
    Timeout timeout;
    uint32_t index;
  };

  class TimeoutCompressorImpl {
   public:
    void EncodeWith(absl::string_view key, Duration value, Encoder* encoder);
  };

  template <typename MetadataTrait>
  class Compressor<MetadataTrait, TimeoutCompressor>
      : public TimeoutCompressorImpl {
   public:
    void EncodeWith(MetadataTrait,
                    const typename MetadataTrait::ValueType& value,
                    Encoder* encoder) {
      TimeoutCompressorImpl::EncodeWith(MetadataTrait::key(), value, encoder);
    }

   private:
    std::vector<PreviousTimeout> previous_timeouts_;
  };

  template <>
  class Compressor<HttpStatusMetadata, HttpStatusCompressor> {
   public:
    void EncodeWith(HttpStatusMetadata,
                    const HttpStatusMetadata::ValueType& value,
                    Encoder* encoder);
  };

  template <>
  class Compressor<HttpMethodMetadata, HttpMethodCompressor> {
   public:
    void EncodeWith(HttpStatusMetadata,
                    const HttpStatusMetadata::ValueType& value,
                    Encoder* encoder);
  };

  template <>
  class Compressor<HttpSchemeMetadata, HttpSchemeCompressor> {
   public:
    void EncodeWith(HttpStatusMetadata,
                    const HttpStatusMetadata::ValueType& value,
                    Encoder* encoder);
  };

  class Encoder {
   public:
    Encoder(HPackCompressor* compressor, bool use_true_binary_metadata,
            SliceBuffer& output);

    void Encode(const Slice& key, const Slice& value);
    template <typename MetadataTrait>
    void Encode(MetadataTrait, const typename MetadataTrait::ValueType& value) {
      compressor_->compression_state_.EncodeWith(MetadataTrait(), value, this);
    }

    /*
    void Encode(HttpPathMetadata, const Slice& value);
    void Encode(HttpAuthorityMetadata, const Slice& value);
    void Encode(HttpStatusMetadata, uint32_t status);
    void Encode(GrpcTimeoutMetadata, Timestamp deadline);
    void Encode(TeMetadata, TeMetadata::ValueType value);
    void Encode(ContentTypeMetadata, ContentTypeMetadata::ValueType value);
    void Encode(HttpSchemeMetadata, HttpSchemeMetadata::ValueType value);
    void Encode(HttpMethodMetadata, HttpMethodMetadata::ValueType method);
    void Encode(UserAgentMetadata, const Slice& slice);
    void Encode(GrpcStatusMetadata, grpc_status_code status);
    void Encode(GrpcEncodingMetadata, grpc_compression_algorithm value);
    void Encode(GrpcAcceptEncodingMetadata, CompressionAlgorithmSet value);
    void Encode(GrpcTagsBinMetadata, const Slice& slice);
    void Encode(GrpcTraceBinMetadata, const Slice& slice);
    void Encode(GrpcMessageMetadata, const Slice& slice) {
      if (slice.empty()) return;
      EmitLitHdrWithNonBinaryStringKeyNotIdx(
          Slice::FromStaticString("grpc-message"), slice.Ref());
    }
    template <typename Which>
    void Encode(Which, const typename Which::ValueType& value) {
    }
*/

    void AdvertiseTableSizeChange();
    void EmitIndexed(uint32_t index);
    void EmitLitHdrWithNonBinaryStringKeyIncIdx(Slice key_slice,
                                                Slice value_slice);
    void EmitLitHdrWithBinaryStringKeyIncIdx(Slice key_slice,
                                             Slice value_slice);
    void EmitLitHdrWithBinaryStringKeyNotIdx(Slice key_slice,
                                             Slice value_slice);
    void EmitLitHdrWithBinaryStringKeyNotIdx(uint32_t key_index,
                                             Slice value_slice);
    void EmitLitHdrWithNonBinaryStringKeyNotIdx(Slice key_slice,
                                                Slice value_slice);

    void EncodeAlwaysIndexed(uint32_t* index, absl::string_view key,
                             Slice value, size_t transport_length);
    void EncodeIndexedKeyWithBinaryValue(uint32_t* index, absl::string_view key,
                                         Slice value);

    void EncodeRepeatingSliceValue(const absl::string_view& key,
                                   const Slice& slice, uint32_t* index,
                                   size_t max_compression_size);

   private:
    const bool use_true_binary_metadata_;
    HPackCompressor* const compressor_;
    SliceBuffer& output_;
  };

  using CompressionState = grpc_metadata_batch::StatefulCompressor<Compressor>;
  CompressionState compression_state_;

  static constexpr size_t kNumFilterValues = 64;
  static constexpr uint32_t kNumCachedGrpcStatusValues = 16;

  void Frame(const EncodeHeaderOptions& options, SliceBuffer& raw,
             grpc_slice_buffer* output);

  // maximum number of bytes we'll use for the decode table (to guard against
  // peers ooming us by setting decode table size high)
  uint32_t max_usable_size_ = hpack_constants::kInitialTableSize;
  // if non-zero, advertise to the decoder that we'll start using a table
  // of this size
  bool advertise_table_size_change_ = false;
  HPackEncoderTable table_;

#if 0

  struct PreviousTimeout {
    Timeout timeout;
    uint32_t index;
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
  // Index of something that was sent with grpc-tags-bin
  uint32_t grpc_tags_bin_index_ = 0;
  // Index of something that was sent with grpc-trace-bin
  uint32_t grpc_trace_bin_index_ = 0;
  // The user-agent string referred to by user_agent_index_
  Slice user_agent_;
  SliceIndex path_index_;
  SliceIndex authority_index_;
  std::vector<PreviousTimeout> previous_timeouts_;
#endif
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H
