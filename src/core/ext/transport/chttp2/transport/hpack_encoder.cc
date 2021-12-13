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

#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"

#include <assert.h>
#include <string.h>

#include <cstdint>

#include "hpack_constants.h"

/* This is here for grpc_is_binary_header
 * TODO(murgatroid99): Remove this
 */
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/timeout_encoding.h"

namespace grpc_core {

namespace {

/* don't consider adding anything bigger than this to the hpack table */
constexpr size_t kMaxDecoderSpaceUsage = 512;
constexpr size_t kDataFrameHeaderSize = 9;

} /* namespace */

/* fills p (which is expected to be kDataFrameHeaderSize bytes long)
 * with a data frame header */
static void FillHeader(uint8_t* p, uint8_t type, uint32_t id, size_t len,
                       uint8_t flags) {
  /* len is the current frame size (i.e. for the frame we're finishing).
     We finish a frame if:
     1) We called ensure_space(), (i.e. add_tiny_header_data()) and adding
        'need_bytes' to the frame would cause us to exceed max_frame_size.
     2) We called add_header_data, and adding the slice would cause us to exceed
        max_frame_size.
     3) We're done encoding the header.

     Thus, len is always <= max_frame_size.
     max_frame_size is derived from GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE,
     which has a max allowable value of 16777215 (see chttp_transport.cc).
     Thus, the following assert can be a debug assert. */
  GPR_DEBUG_ASSERT(len < 16777316);
  *p++ = static_cast<uint8_t>(len >> 16);
  *p++ = static_cast<uint8_t>(len >> 8);
  *p++ = static_cast<uint8_t>(len);
  *p++ = type;
  *p++ = flags;
  *p++ = static_cast<uint8_t>(id >> 24);
  *p++ = static_cast<uint8_t>(id >> 16);
  *p++ = static_cast<uint8_t>(id >> 8);
  *p++ = static_cast<uint8_t>(id);
}

size_t HPackCompressor::Framer::CurrentFrameSize() const {
  const size_t frame_size =
      output_->length - prefix_.output_length_at_start_of_frame;
  GPR_DEBUG_ASSERT(frame_size <= max_frame_size_);
  return frame_size;
}

// finish a frame - fill in the previously reserved header
void HPackCompressor::Framer::FinishFrame(bool is_header_boundary) {
  const uint8_t type = is_first_frame_ ? GRPC_CHTTP2_FRAME_HEADER
                                       : GRPC_CHTTP2_FRAME_CONTINUATION;
  uint8_t flags = 0;
  // per the HTTP/2 spec:
  //   A HEADERS frame carries the END_STREAM flag that signals the end of a
  //   stream. However, a HEADERS frame with the END_STREAM flag set can be
  //   followed by CONTINUATION frames on the same stream. Logically, the
  //   CONTINUATION frames are part of the HEADERS frame.
  // Thus, we add the END_STREAM flag to the HEADER frame (the first frame).
  if (is_first_frame_ && is_end_of_stream_) {
    flags |= GRPC_CHTTP2_DATA_FLAG_END_STREAM;
  }
  // per the HTTP/2 spec:
  //   A HEADERS frame without the END_HEADERS flag set MUST be followed by
  //   a CONTINUATION frame for the same stream.
  // Thus, we add the END_HEADER flag to the last frame.
  if (is_header_boundary) {
    flags |= GRPC_CHTTP2_DATA_FLAG_END_HEADERS;
  }
  FillHeader(GRPC_SLICE_START_PTR(output_->slices[prefix_.header_idx]), type,
             stream_id_, CurrentFrameSize(), flags);
  stats_->framing_bytes += kDataFrameHeaderSize;
  is_first_frame_ = false;
}

// begin a new frame: reserve off header space, remember how many bytes we'd
// output before beginning
HPackCompressor::Framer::FramePrefix HPackCompressor::Framer::BeginFrame() {
  grpc_slice reserved;
  reserved.refcount = nullptr;
  reserved.data.inlined.length = kDataFrameHeaderSize;
  return FramePrefix{grpc_slice_buffer_add_indexed(output_, reserved),
                     output_->length};
}

// make sure that the current frame is of the type desired, and has sufficient
// space to add at least about_to_add bytes -- finishes the current frame if
// needed
void HPackCompressor::Framer::EnsureSpace(size_t need_bytes) {
  if (GPR_LIKELY(CurrentFrameSize() + need_bytes <= max_frame_size_)) {
    return;
  }
  FinishFrame(false);
  prefix_ = BeginFrame();
}

void HPackCompressor::Framer::Add(grpc_slice slice) {
  const size_t len = GRPC_SLICE_LENGTH(slice);
  if (len == 0) return;
  const size_t remaining = max_frame_size_ - CurrentFrameSize();
  if (len <= remaining) {
    stats_->header_bytes += len;
    grpc_slice_buffer_add(output_, slice);
  } else {
    stats_->header_bytes += remaining;
    grpc_slice_buffer_add(output_, grpc_slice_split_head(&slice, remaining));
    FinishFrame(false);
    prefix_ = BeginFrame();
    Add(slice);
  }
}

uint8_t* HPackCompressor::Framer::AddTiny(size_t len) {
  EnsureSpace(len);
  stats_->header_bytes += len;
  return grpc_slice_buffer_tiny_add(output_, len);
}

void HPackCompressor::Framer::EmitIndexed(uint32_t elem_index) {
  GRPC_STATS_INC_HPACK_SEND_INDEXED();
  VarintWriter<1> w(elem_index);
  w.Write(0x80, AddTiny(w.length()));
}

struct WireValue {
  WireValue(uint8_t huffman_prefix, bool insert_null_before_wire_value,
            const grpc_slice& slice)
      : data(slice),
        huffman_prefix(huffman_prefix),
        insert_null_before_wire_value(insert_null_before_wire_value),
        length(GRPC_SLICE_LENGTH(slice) +
               (insert_null_before_wire_value ? 1 : 0)) {}
  // While wire_value is const from the POV of hpack encoder code, actually
  // adding it to a slice buffer will possibly split the slice.
  const grpc_slice data;
  const uint8_t huffman_prefix;
  const bool insert_null_before_wire_value;
  const size_t length;
};

static WireValue GetWireValue(const grpc_slice& value, bool true_binary_enabled,
                              bool is_bin_hdr) {
  if (is_bin_hdr) {
    if (true_binary_enabled) {
      GRPC_STATS_INC_HPACK_SEND_BINARY();
      return WireValue(0x00, true, grpc_slice_ref_internal(value));
    } else {
      GRPC_STATS_INC_HPACK_SEND_BINARY_BASE64();
      return WireValue(0x80, false,
                       grpc_chttp2_base64_encode_and_huffman_compress(value));
    }
  } else {
    /* TODO(ctiller): opportunistically compress non-binary headers */
    GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
    return WireValue(0x00, false, grpc_slice_ref_internal(value));
  }
}

struct DefinitelyInterned {
  static bool IsBinary(grpc_slice key) {
    return grpc_is_refcounted_slice_binary_header(key);
  }
};
struct UnsureIfInterned {
  static bool IsBinary(grpc_slice key) {
    return grpc_is_binary_header_internal(key);
  }
};

class BinaryStringValue {
 public:
  explicit BinaryStringValue(const grpc_slice& value,
                             bool use_true_binary_metadata)
      : wire_value_(GetWireValue(value, use_true_binary_metadata, true)),
        len_val_(wire_value_.length) {}

  size_t prefix_length() const {
    return len_val_.length() +
           (wire_value_.insert_null_before_wire_value ? 1 : 0);
  }

  void WritePrefix(uint8_t* prefix_data) {
    len_val_.Write(wire_value_.huffman_prefix, prefix_data);
    if (wire_value_.insert_null_before_wire_value) {
      prefix_data[len_val_.length()] = 0;
    }
  }

  const grpc_slice& data() { return wire_value_.data; }

 private:
  WireValue wire_value_;
  VarintWriter<1> len_val_;
};

class NonBinaryStringValue {
 public:
  explicit NonBinaryStringValue(const grpc_slice& value)
      : value_(value), len_val_(GRPC_SLICE_LENGTH(value)) {}

  size_t prefix_length() const { return len_val_.length(); }

  void WritePrefix(uint8_t* prefix_data) { len_val_.Write(0x00, prefix_data); }

  const grpc_slice& data() { return value_; }

 private:
  grpc_slice value_;
  VarintWriter<1> len_val_;
};

class StringKey {
 public:
  explicit StringKey(grpc_slice key)
      : key_(key), len_key_(GRPC_SLICE_LENGTH(key)) {}

  size_t prefix_length() const { return 1 + len_key_.length(); }

  void WritePrefix(uint8_t type, uint8_t* data) {
    data[0] = type;
    len_key_.Write(0x00, data + 1);
  }

  grpc_slice key() const { return key_; }

 private:
  grpc_slice key_;
  VarintWriter<1> len_key_;
};

void HPackCompressor::Framer::EmitLitHdrWithNonBinaryStringKeyIncIdx(
    const grpc_slice& key_slice, const grpc_slice& value_slice) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_INCIDX_V();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  StringKey key(key_slice);
  key.WritePrefix(0x40, AddTiny(key.prefix_length()));
  Add(grpc_slice_ref_internal(key.key()));
  NonBinaryStringValue emit(value_slice);
  emit.WritePrefix(AddTiny(emit.prefix_length()));
  Add(grpc_slice_ref_internal(emit.data()));
}

void HPackCompressor::Framer::EmitLitHdrWithBinaryStringKeyNotIdx(
    const grpc_slice& key_slice, const grpc_slice& value_slice) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX_V();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  StringKey key(key_slice);
  key.WritePrefix(0x00, AddTiny(key.prefix_length()));
  Add(grpc_slice_ref_internal(key.key()));
  BinaryStringValue emit(value_slice, use_true_binary_metadata_);
  emit.WritePrefix(AddTiny(emit.prefix_length()));
  Add(emit.data());
}

void HPackCompressor::Framer::EmitLitHdrWithBinaryStringKeyIncIdx(
    const grpc_slice& key_slice, const grpc_slice& value_slice) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_INCIDX_V();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  StringKey key(key_slice);
  key.WritePrefix(0x40, AddTiny(key.prefix_length()));
  Add(grpc_slice_ref_internal(key.key()));
  BinaryStringValue emit(value_slice, use_true_binary_metadata_);
  emit.WritePrefix(AddTiny(emit.prefix_length()));
  Add(emit.data());
}

void HPackCompressor::Framer::EmitLitHdrWithBinaryStringKeyNotIdx(
    uint32_t key_index, const grpc_slice& value_slice) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  BinaryStringValue emit(value_slice, use_true_binary_metadata_);
  VarintWriter<4> key(key_index);
  uint8_t* data = AddTiny(key.length() + emit.prefix_length());
  key.Write(0x00, data);
  emit.WritePrefix(data + key.length());
  Add(emit.data());
}

void HPackCompressor::Framer::EmitLitHdrWithNonBinaryStringKeyNotIdx(
    const grpc_slice& key_slice, const grpc_slice& value_slice) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX_V();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  StringKey key(key_slice);
  key.WritePrefix(0x00, AddTiny(key.prefix_length()));
  Add(grpc_slice_ref_internal(key.key()));
  NonBinaryStringValue emit(value_slice);
  emit.WritePrefix(AddTiny(emit.prefix_length()));
  Add(grpc_slice_ref_internal(emit.data()));
}

void HPackCompressor::Framer::AdvertiseTableSizeChange() {
  VarintWriter<3> w(compressor_->table_.max_size());
  w.Write(0x20, AddTiny(w.length()));
}

struct EmitIndexedStatus {
  EmitIndexedStatus() = default;
  EmitIndexedStatus(uint32_t elem_hash, bool emitted, bool can_add)
      : elem_hash(elem_hash), emitted(emitted), can_add(can_add) {}
  const uint32_t elem_hash = 0;
  const bool emitted = false;
  const bool can_add = false;
};

void HPackCompressor::SliceIndex::EmitTo(absl::string_view key,
                                         const Slice& value, Framer* framer) {
  auto& table = framer->compressor_->table_;
  using It = std::vector<ValueIndex>::iterator;
  It prev = values_.end();
  uint32_t transport_length =
      key.length() + value.length() + hpack_constants::kEntryOverhead;
  // Linear scan through previous values to see if we find the value.
  for (It it = values_.begin(); it != values_.end(); ++it) {
    if (value == it->value) {
      // Got a hit... is it still in the decode table?
      if (table.ConvertableToDynamicIndex(it->index)) {
        // Yes, emit the index and proceed to cleanup.
        framer->EmitIndexed(table.DynamicIndex(it->index));
      } else {
        // Not current, emit a new literal and update the index.
        it->index = table.AllocateIndex(transport_length);
        framer->EmitLitHdrWithNonBinaryStringKeyIncIdx(
            Slice::FromStaticString(key), value.Ref());
      }
      // Bubble this entry up if we can - ensures that the most used values end
      // up towards the start of the array.
      if (prev != values_.end()) std::swap(*prev, *it);
      // If there are entries at the end of the array, and those entries are no
      // longer in the table, remove them.
      while (!values_.empty() &&
             !table.ConvertableToDynamicIndex(values_.back().index)) {
        values_.pop_back();
      }
      // All done, early out.
      return;
    }
    prev = it;
  }
  // No hit, emit a new literal and add it to the index.
  uint32_t index = table.AllocateIndex(transport_length);
  framer->EmitLitHdrWithNonBinaryStringKeyIncIdx(Slice::FromStaticString(key), value.c_slice());
  values_.emplace_back(value.Ref(), index);
}

void HPackCompressor::Framer::Encode(HttpPathMetadata, const Slice& value) {
  compressor_->path_index_.EmitTo(HttpPathMetadata::key(), value, this);
}

void HPackCompressor::Framer::Encode(HttpAuthorityMetadata,
                                     const Slice& value) {
  compressor_->authority_index_.EmitTo(HttpAuthorityMetadata::key(), value, this);
}

void HPackCompressor::Framer::Encode(TeMetadata, TeMetadata::ValueType value) {
  GPR_ASSERT(value == TeMetadata::ValueType::kTrailers);
  EncodeAlwaysIndexed(
      &compressor_->te_index_, "te", Slice::FromStaticString("trailers"),
      2 /* te */ + 8 /* trailers */ + hpack_constants::kEntryOverhead);
}

void HPackCompressor::Framer::Encode(ContentTypeMetadata,
                                     ContentTypeMetadata::ValueType value) {
  GPR_ASSERT(value == ContentTypeMetadata::ValueType::kApplicationGrpc);
  EncodeAlwaysIndexed(
      &compressor_->content_type_index_, "content-type",
      Slice::FromStaticString("application/grpc"),
      12 /* content-type */ + 16 /* application/grpc */ +
          hpack_constants::kEntryOverhead);
}

void HPackCompressor::Framer::Encode(HttpSchemeMetadata,
                                     HttpSchemeMetadata::ValueType value) {
  switch (value) {
    case HttpSchemeMetadata::ValueType::kHttp:
      EmitIndexed(6);  // :scheme: http
      break;
    case HttpSchemeMetadata::ValueType::kHttps:
      EmitIndexed(7);  // :scheme: https
      break;
    case HttpSchemeMetadata::ValueType::kInvalid:
      GPR_ASSERT(false);
      break;
  }
}

void HPackCompressor::Framer::Encode(GrpcTraceBinMetadata, const Slice& slice) {
  EncodeIndexedKeyWithBinaryValue(&compressor_->grpc_trace_bin_index_,
                                  "grpc-trace-bin", slice.Ref());
}

void HPackCompressor::Framer::Encode(GrpcTagsBinMetadata, const Slice& slice) {
  EncodeIndexedKeyWithBinaryValue(&compressor_->grpc_tags_bin_index_,
                                  "grpc-tags-bin", slice.Ref());
}

void HPackCompressor::Framer::Encode(HttpStatusMetadata, uint32_t status) {
  if (status == 200) {
    EmitIndexed(8);  // :status: 200
    return;
  }
  uint8_t index = 0;
  switch (status) {
    case 204:
      index = 9;  // :status: 204
      break;
    case 206:
      index = 10;  // :status: 206
      break;
    case 304:
      index = 11;  // :status: 304
      break;
    case 400:
      index = 12;  // :status: 400
      break;
    case 404:
      index = 13;  // :status: 404
      break;
    case 500:
      index = 14;  // :status: 500
      break;
  }
  if (GPR_LIKELY(index != 0)) {
    EmitIndexed(index);
  } else {
    char buffer[GPR_LTOA_MIN_BUFSIZE];
    gpr_ltoa(status, buffer);
    EmitLitHdrWithNonBinaryStringKeyIncIdx(
        GRPC_MDSTR_STATUS, Slice::FromCopiedString(buffer).c_slice());
  }
}

void HPackCompressor::Framer::Encode(HttpMethodMetadata,
                                     HttpMethodMetadata::ValueType method) {
  switch (method) {
    case HttpMethodMetadata::ValueType::kGet:
      EmitIndexed(2);  // :method: GET
      break;
    case HttpMethodMetadata::ValueType::kPost:
      EmitIndexed(3);  // :method: POST
      break;
    case HttpMethodMetadata::ValueType::kPut:
      EmitLitHdrWithNonBinaryStringKeyNotIdx(
          Slice::FromStaticString(":method"),
          Slice::FromStaticString("PUT"));
      break;
    case HttpMethodMetadata::ValueType::kInvalid:
      GPR_ASSERT(false);
      break;
  }
}

void HPackCompressor::Framer::EncodeAlwaysIndexed(uint32_t* index,
                                                  absl::string_view key,
                                                  Slice value,
                                                  uint32_t transport_length) {
  if (compressor_->table_.ConvertableToDynamicIndex(*index)) {
    EmitIndexed(compressor_->table_.DynamicIndex(*index));
  } else {
    *index = compressor_->table_.AllocateIndex(transport_length);
    EmitLitHdrWithNonBinaryStringKeyIncIdx(Slice::FromStaticString(key), std::move(value));
  }
}

void HPackCompressor::Framer::EncodeIndexedKeyWithBinaryValue(
    uint32_t* index, absl::string_view key, Slice value) {
  if (compressor_->table_.ConvertableToDynamicIndex(*index)) {
    EmitLitHdrWithBinaryStringKeyNotIdx(
        compressor_->table_.DynamicIndex(*index), std::move(value));
  } else {
    *index = compressor_->table_.AllocateIndex(key.length() +
                                               value.length() +
                                               hpack_constants::kEntryOverhead);
    EmitLitHdrWithBinaryStringKeyIncIdx(Slice::FromStaticString(key), std::move(value));
  }
}

void HPackCompressor::Framer::Encode(GrpcTimeoutMetadata,
                                     grpc_millis deadline) {
  char timeout_str[GRPC_HTTP2_TIMEOUT_ENCODE_MIN_BUFSIZE];
  grpc_mdelem mdelem;
  grpc_http2_encode_timeout(deadline - ExecCtx::Get()->Now(), timeout_str);
  mdelem = grpc_mdelem_from_slices(GRPC_MDSTR_GRPC_TIMEOUT,
                                   UnmanagedMemorySlice(timeout_str));
  EncodeDynamic(mdelem);
  GRPC_MDELEM_UNREF(mdelem);
}

void HPackCompressor::Framer::Encode(UserAgentMetadata, const Slice& slice) {
  if (!slice.is_equivalent(compressor_->user_agent_)) {
    compressor_->user_agent_ = slice.Ref();
    compressor_->user_agent_index_ = 0;
  }
  EncodeAlwaysIndexed(
      &compressor_->user_agent_index_, GRPC_MDSTR_USER_AGENT, slice.c_slice(),
      10 /* user-agent */ + slice.size() + hpack_constants::kEntryOverhead);
}

void HPackCompressor::Framer::Encode(GrpcStatusMetadata,
                                     grpc_status_code status) {
  const uint32_t code = static_cast<uint32_t>(status);
  uint32_t* index = nullptr;
  if (code < kNumCachedGrpcStatusValues) {
    index = &compressor_->cached_grpc_status_[code];
    if (compressor_->table_.ConvertableToDynamicIndex(*index)) {
      EmitIndexed(compressor_->table_.DynamicIndex(*index));
      return;
    }
  }
  char buffer[GPR_LTOA_MIN_BUFSIZE];
  gpr_ltoa(code, buffer);
  grpc_slice key = ExternallyManagedSlice(GrpcStatusMetadata::key().data(),
                                          GrpcStatusMetadata::key().size());
  grpc_slice value = grpc_slice_from_copied_string(buffer);
  const uint32_t transport_length = GRPC_SLICE_LENGTH(key) +
                                    GRPC_SLICE_LENGTH(value) +
                                    hpack_constants::kEntryOverhead;
  if (index != nullptr) {
    *index = compressor_->table_.AllocateIndex(transport_length);
    EmitLitHdrWithNonBinaryStringKeyIncIdx(key, value);
  } else {
    EmitLitHdrWithNonBinaryStringKeyNotIdx(key, value);
  }
}

void HPackCompressor::Framer::Encode(GrpcEncodingMetadata,
                                     grpc_compression_algorithm value) {
  uint32_t* index = nullptr;
  if (value < GRPC_COMPRESS_ALGORITHMS_COUNT) {
    index = &compressor_->cached_grpc_encoding_[static_cast<uint32_t>(value)];
    if (compressor_->table_.ConvertableToDynamicIndex(*index)) {
      EmitIndexed(compressor_->table_.DynamicIndex(*index));
      return;
    }
  }
  auto key = StaticSlice::FromStaticString(GrpcEncodingMetadata::key());
  auto encoded_value = GrpcEncodingMetadata::Encode(value);
  uint32_t transport_length =
      key.length() + encoded_value.length() + hpack_constants::kEntryOverhead;
  if (index != nullptr) {
    *index = compressor_->table_.AllocateIndex(transport_length);
    EmitLitHdrWithNonBinaryStringKeyIncIdx(key.c_slice(),
                                           encoded_value.c_slice());
  } else {
    EmitLitHdrWithNonBinaryStringKeyNotIdx(key.c_slice(),
                                           encoded_value.c_slice());
  }
}

void HPackCompressor::Framer::Encode(GrpcAcceptEncodingMetadata,
                                     CompressionAlgorithmSet value) {
  if (compressor_->grpc_accept_encoding_index_ != 0 &&
      value == compressor_->grpc_accept_encoding_ &&
      compressor_->table_.ConvertableToDynamicIndex(
          compressor_->grpc_accept_encoding_index_)) {
    EmitIndexed(compressor_->table_.DynamicIndex(
        compressor_->grpc_accept_encoding_index_));
    return;
  }
  auto key = StaticSlice::FromStaticString(GrpcAcceptEncodingMetadata::key());
  auto encoded_value = GrpcAcceptEncodingMetadata::Encode(value);
  uint32_t transport_length =
      key.length() + encoded_value.length() + hpack_constants::kEntryOverhead;
  compressor_->grpc_accept_encoding_index_ =
      compressor_->table_.AllocateIndex(transport_length);
  compressor_->grpc_accept_encoding_ = value;
  EmitLitHdrWithNonBinaryStringKeyIncIdx(key.c_slice(),
                                         encoded_value.c_slice());
}

void HPackCompressor::SetMaxUsableSize(uint32_t max_table_size) {
  max_usable_size_ = max_table_size;
  SetMaxTableSize(std::min(table_.max_size(), max_table_size));
}

void HPackCompressor::SetMaxTableSize(uint32_t max_table_size) {
  if (table_.SetMaxSize(std::min(max_usable_size_, max_table_size))) {
    advertise_table_size_change_ = true;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
      gpr_log(GPR_INFO, "set max table size from encoder to %d",
              max_table_size);
    }
  }
}

HPackCompressor::Framer::Framer(const EncodeHeaderOptions& options,
                                HPackCompressor* compressor,
                                grpc_slice_buffer* output)
    : max_frame_size_(options.max_frame_size),
      use_true_binary_metadata_(options.use_true_binary_metadata),
      is_end_of_stream_(options.is_end_of_stream),
      stream_id_(options.stream_id),
      output_(output),
      stats_(options.stats),
      compressor_(compressor),
      prefix_(BeginFrame()) {
  if (absl::exchange(compressor_->advertise_table_size_change_, false)) {
    AdvertiseTableSizeChange();
  }
}

void HPackCompressor::Framer::Encode(grpc_mdelem md) {
  if (GRPC_MDELEM_STORAGE(md) == GRPC_MDELEM_STORAGE_STATIC) {
    const uintptr_t static_index =
        reinterpret_cast<StaticMetadata*>(GRPC_MDELEM_DATA(md))->StaticIndex();
    if (static_index < hpack_constants::kLastStaticEntry) {
      EmitIndexed(static_cast<uint32_t>(static_index + 1));
      return;
    }
  }
  EncodeDynamic(md);
}

}  // namespace grpc_core
