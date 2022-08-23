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

#include <algorithm>
#include <cstdint>
#include <memory>

#include "absl/utility/utility.h"

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_constants.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_table.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/timeout_encoding.h"

namespace grpc_core {

namespace {

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

void HPackCompressor::Framer::Add(Slice slice) {
  while (true) {
    const size_t len = slice.length();
    if (len == 0) return;
    const size_t remaining = max_frame_size_ - CurrentFrameSize();
    if (len <= remaining) {
      stats_->header_bytes += len;
      grpc_slice_buffer_add(output_, slice.TakeCSlice());
      return;
    } else {
      stats_->header_bytes += remaining;
      Slice tail = slice.Split(remaining);
      grpc_slice_buffer_add(output_, slice.TakeCSlice());
      slice = std::move(tail);
      FinishFrame(false);
      prefix_ = BeginFrame();
    }
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
            Slice slice)
      : data(std::move(slice)),
        huffman_prefix(huffman_prefix),
        insert_null_before_wire_value(insert_null_before_wire_value),
        length(data.length() + (insert_null_before_wire_value ? 1 : 0)) {}
  Slice data;
  const uint8_t huffman_prefix;
  const bool insert_null_before_wire_value;
  const size_t length;
};

static WireValue GetWireValue(Slice value, bool true_binary_enabled,
                              bool is_bin_hdr) {
  if (is_bin_hdr) {
    if (true_binary_enabled) {
      GRPC_STATS_INC_HPACK_SEND_BINARY();
      return WireValue(0x00, true, std::move(value));
    } else {
      GRPC_STATS_INC_HPACK_SEND_BINARY_BASE64();
      return WireValue(0x80, false,
                       Slice(grpc_chttp2_base64_encode_and_huffman_compress(
                           value.c_slice())));
    }
  } else {
    /* TODO(ctiller): opportunistically compress non-binary headers */
    GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
    return WireValue(0x00, false, std::move(value));
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
  explicit BinaryStringValue(Slice value, bool use_true_binary_metadata)
      : wire_value_(
            GetWireValue(std::move(value), use_true_binary_metadata, true)),
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

  Slice data() { return std::move(wire_value_.data); }

 private:
  WireValue wire_value_;
  VarintWriter<1> len_val_;
};

class NonBinaryStringValue {
 public:
  explicit NonBinaryStringValue(Slice value)
      : value_(std::move(value)), len_val_(value_.length()) {}

  size_t prefix_length() const { return len_val_.length(); }

  void WritePrefix(uint8_t* prefix_data) { len_val_.Write(0x00, prefix_data); }

  Slice data() { return std::move(value_); }

 private:
  Slice value_;
  VarintWriter<1> len_val_;
};

class StringKey {
 public:
  explicit StringKey(Slice key)
      : key_(std::move(key)), len_key_(key_.length()) {}

  size_t prefix_length() const { return 1 + len_key_.length(); }

  void WritePrefix(uint8_t type, uint8_t* data) {
    data[0] = type;
    len_key_.Write(0x00, data + 1);
  }

  Slice key() { return std::move(key_); }

 private:
  Slice key_;
  VarintWriter<1> len_key_;
};

void HPackCompressor::Framer::EmitLitHdrWithNonBinaryStringKeyIncIdx(
    Slice key_slice, Slice value_slice) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_INCIDX_V();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  StringKey key(std::move(key_slice));
  key.WritePrefix(0x40, AddTiny(key.prefix_length()));
  Add(key.key());
  NonBinaryStringValue emit(std::move(value_slice));
  emit.WritePrefix(AddTiny(emit.prefix_length()));
  Add(emit.data());
}

void HPackCompressor::Framer::EmitLitHdrWithBinaryStringKeyNotIdx(
    Slice key_slice, Slice value_slice) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX_V();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  StringKey key(std::move(key_slice));
  key.WritePrefix(0x00, AddTiny(key.prefix_length()));
  Add(key.key());
  BinaryStringValue emit(std::move(value_slice), use_true_binary_metadata_);
  emit.WritePrefix(AddTiny(emit.prefix_length()));
  Add(emit.data());
}

void HPackCompressor::Framer::EmitLitHdrWithBinaryStringKeyIncIdx(
    Slice key_slice, Slice value_slice) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_INCIDX_V();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  StringKey key(std::move(key_slice));
  key.WritePrefix(0x40, AddTiny(key.prefix_length()));
  Add(key.key());
  BinaryStringValue emit(std::move(value_slice), use_true_binary_metadata_);
  emit.WritePrefix(AddTiny(emit.prefix_length()));
  Add(emit.data());
}

void HPackCompressor::Framer::EmitLitHdrWithBinaryStringKeyNotIdx(
    uint32_t key_index, Slice value_slice) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  BinaryStringValue emit(std::move(value_slice), use_true_binary_metadata_);
  VarintWriter<4> key(key_index);
  uint8_t* data = AddTiny(key.length() + emit.prefix_length());
  key.Write(0x00, data);
  emit.WritePrefix(data + key.length());
  Add(emit.data());
}

void HPackCompressor::Framer::EmitLitHdrWithNonBinaryStringKeyNotIdx(
    Slice key_slice, Slice value_slice) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX_V();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  StringKey key(std::move(key_slice));
  key.WritePrefix(0x00, AddTiny(key.prefix_length()));
  Add(key.key());
  NonBinaryStringValue emit(std::move(value_slice));
  emit.WritePrefix(AddTiny(emit.prefix_length()));
  Add(emit.data());
}

void HPackCompressor::Framer::AdvertiseTableSizeChange() {
  VarintWriter<3> w(compressor_->table_.max_size());
  w.Write(0x20, AddTiny(w.length()));
}

void HPackCompressor::SliceIndex::EmitTo(absl::string_view key,
                                         const Slice& value, Framer* framer) {
  auto& table = framer->compressor_->table_;
  using It = std::vector<ValueIndex>::iterator;
  It prev = values_.end();
  uint32_t transport_length =
      key.length() + value.length() + hpack_constants::kEntryOverhead;
  if (transport_length > HPackEncoderTable::MaxEntrySize()) {
    framer->EmitLitHdrWithNonBinaryStringKeyNotIdx(Slice::FromStaticString(key),
                                                   value.Ref());
    return;
  }
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
  framer->EmitLitHdrWithNonBinaryStringKeyIncIdx(Slice::FromStaticString(key),
                                                 value.Ref());
  values_.emplace_back(value.Ref(), index);
}

void HPackCompressor::Framer::Encode(const Slice& key, const Slice& value) {
  if (absl::EndsWith(key.as_string_view(), "-bin")) {
    EmitLitHdrWithBinaryStringKeyNotIdx(key.Ref(), value.Ref());
  } else {
    EmitLitHdrWithNonBinaryStringKeyNotIdx(key.Ref(), value.Ref());
  }
}

void HPackCompressor::Framer::Encode(HttpPathMetadata, const Slice& value) {
  compressor_->path_index_.EmitTo(HttpPathMetadata::key(), value, this);
}

void HPackCompressor::Framer::Encode(HttpAuthorityMetadata,
                                     const Slice& value) {
  compressor_->authority_index_.EmitTo(HttpAuthorityMetadata::key(), value,
                                       this);
}

void HPackCompressor::Framer::Encode(TeMetadata, TeMetadata::ValueType value) {
  GPR_ASSERT(value == TeMetadata::ValueType::kTrailers);
  EncodeAlwaysIndexed(
      &compressor_->te_index_, "te", Slice::FromStaticString("trailers"),
      2 /* te */ + 8 /* trailers */ + hpack_constants::kEntryOverhead);
}

void HPackCompressor::Framer::Encode(ContentTypeMetadata,
                                     ContentTypeMetadata::ValueType value) {
  if (value != ContentTypeMetadata::ValueType::kApplicationGrpc) {
    gpr_log(GPR_ERROR, "Not encoding bad content-type header");
    return;
  }
  EncodeAlwaysIndexed(&compressor_->content_type_index_, "content-type",
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
  EncodeRepeatingSliceValue(GrpcTraceBinMetadata::key(), slice,
                            &compressor_->grpc_trace_bin_index_,
                            HPackEncoderTable::MaxEntrySize());
}

void HPackCompressor::Framer::Encode(GrpcTagsBinMetadata, const Slice& slice) {
  EncodeRepeatingSliceValue(GrpcTagsBinMetadata::key(), slice,
                            &compressor_->grpc_tags_bin_index_,
                            HPackEncoderTable::MaxEntrySize());
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
    EmitLitHdrWithNonBinaryStringKeyIncIdx(Slice::FromStaticString(":status"),
                                           Slice::FromInt64(status));
  }
}

void HPackCompressor::Framer::Encode(HttpMethodMetadata,
                                     HttpMethodMetadata::ValueType method) {
  switch (method) {
    case HttpMethodMetadata::ValueType::kPost:
      EmitIndexed(3);  // :method: POST
      break;
    case HttpMethodMetadata::ValueType::kGet:
      EmitIndexed(2);  // :method: GET
      break;
    case HttpMethodMetadata::ValueType::kPut:
      // Right now, we only emit PUT as a method for testing purposes, so it's
      // fine to not index it.
      EmitLitHdrWithNonBinaryStringKeyNotIdx(Slice::FromStaticString(":method"),
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
    EmitLitHdrWithNonBinaryStringKeyIncIdx(Slice::FromStaticString(key),
                                           std::move(value));
  }
}

void HPackCompressor::Framer::EncodeIndexedKeyWithBinaryValue(
    uint32_t* index, absl::string_view key, Slice value) {
  if (compressor_->table_.ConvertableToDynamicIndex(*index)) {
    EmitLitHdrWithBinaryStringKeyNotIdx(
        compressor_->table_.DynamicIndex(*index), std::move(value));
  } else {
    *index = compressor_->table_.AllocateIndex(key.length() + value.length() +
                                               hpack_constants::kEntryOverhead);
    EmitLitHdrWithBinaryStringKeyIncIdx(Slice::FromStaticString(key),
                                        std::move(value));
  }
}

void HPackCompressor::Framer::EncodeRepeatingSliceValue(
    const absl::string_view& key, const Slice& slice, uint32_t* index,
    size_t max_compression_size) {
  if (hpack_constants::SizeForEntry(key.size(), slice.size()) >
      max_compression_size) {
    EmitLitHdrWithBinaryStringKeyNotIdx(Slice::FromStaticString(key),
                                        slice.Ref());
  } else {
    EncodeIndexedKeyWithBinaryValue(index, key, slice.Ref());
  }
}

void HPackCompressor::Framer::Encode(GrpcTimeoutMetadata, Timestamp deadline) {
  Timeout timeout = Timeout::FromDuration(deadline - ExecCtx::Get()->Now());
  for (auto it = compressor_->previous_timeouts_.begin();
       it != compressor_->previous_timeouts_.end(); ++it) {
    double ratio = timeout.RatioVersus(it->timeout);
    // If the timeout we're sending is shorter than a previous timeout, but
    // within 3% of it, we'll consider sending it.
    if (ratio > -3 && ratio <= 0 &&
        compressor_->table_.ConvertableToDynamicIndex(it->index)) {
      EmitIndexed(compressor_->table_.DynamicIndex(it->index));
      // Put this timeout to the front of the queue - forces common timeouts to
      // be considered earlier.
      std::swap(*it, *compressor_->previous_timeouts_.begin());
      return;
    }
  }
  // Clean out some expired timeouts.
  while (!compressor_->previous_timeouts_.empty() &&
         !compressor_->table_.ConvertableToDynamicIndex(
             compressor_->previous_timeouts_.back().index)) {
    compressor_->previous_timeouts_.pop_back();
  }
  Slice encoded = timeout.Encode();
  uint32_t index = compressor_->table_.AllocateIndex(
      GrpcTimeoutMetadata::key().length() + encoded.length() +
      hpack_constants::kEntryOverhead);
  compressor_->previous_timeouts_.push_back(PreviousTimeout{timeout, index});
  EmitLitHdrWithNonBinaryStringKeyIncIdx(
      Slice::FromStaticString(GrpcTimeoutMetadata::key()), std::move(encoded));
}

void HPackCompressor::Framer::Encode(UserAgentMetadata, const Slice& slice) {
  if (hpack_constants::SizeForEntry(UserAgentMetadata::key().size(),
                                    slice.size()) >
      HPackEncoderTable::MaxEntrySize()) {
    EmitLitHdrWithNonBinaryStringKeyNotIdx(
        Slice::FromStaticString(UserAgentMetadata::key()), slice.Ref());
    return;
  }
  if (!slice.is_equivalent(compressor_->user_agent_)) {
    compressor_->user_agent_ = slice.Ref();
    compressor_->user_agent_index_ = 0;
  }
  EncodeAlwaysIndexed(&compressor_->user_agent_index_, UserAgentMetadata::key(),
                      slice.Ref(),
                      hpack_constants::SizeForEntry(
                          UserAgentMetadata::key().size(), slice.size()));
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
  Slice key = Slice::FromStaticString(GrpcStatusMetadata::key());
  Slice value = Slice::FromInt64(code);
  const uint32_t transport_length =
      key.length() + value.length() + hpack_constants::kEntryOverhead;
  if (index != nullptr) {
    *index = compressor_->table_.AllocateIndex(transport_length);
    EmitLitHdrWithNonBinaryStringKeyIncIdx(std::move(key), std::move(value));
  } else {
    EmitLitHdrWithNonBinaryStringKeyNotIdx(std::move(key), std::move(value));
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
  auto key = Slice::FromStaticString(GrpcEncodingMetadata::key());
  auto encoded_value = GrpcEncodingMetadata::Encode(value);
  uint32_t transport_length =
      key.length() + encoded_value.length() + hpack_constants::kEntryOverhead;
  if (index != nullptr) {
    *index = compressor_->table_.AllocateIndex(transport_length);
    EmitLitHdrWithNonBinaryStringKeyIncIdx(std::move(key),
                                           std::move(encoded_value));
  } else {
    EmitLitHdrWithNonBinaryStringKeyNotIdx(std::move(key),
                                           std::move(encoded_value));
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
  auto key = Slice::FromStaticString(GrpcAcceptEncodingMetadata::key());
  auto encoded_value = GrpcAcceptEncodingMetadata::Encode(value);
  uint32_t transport_length =
      key.length() + encoded_value.length() + hpack_constants::kEntryOverhead;
  compressor_->grpc_accept_encoding_index_ =
      compressor_->table_.AllocateIndex(transport_length);
  compressor_->grpc_accept_encoding_ = value;
  EmitLitHdrWithNonBinaryStringKeyIncIdx(std::move(key),
                                         std::move(encoded_value));
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

}  // namespace grpc_core
