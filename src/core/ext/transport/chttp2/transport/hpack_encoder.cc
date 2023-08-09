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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"

#include <algorithm>
#include <cstdint>

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_constants.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_table.h"
#include "src/core/ext/transport/chttp2/transport/http_trace.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/timeout_encoding.h"

namespace grpc_core {

namespace {

constexpr size_t kDataFrameHeaderSize = 9;

}  // namespace

// fills p (which is expected to be kDataFrameHeaderSize bytes long)
// with a data frame header
static void FillHeader(uint8_t* p, uint8_t type, uint32_t id, size_t len,
                       uint8_t flags) {
  // len is the current frame size (i.e. for the frame we're finishing).
  // We finish a frame if:
  // 1) We called ensure_space(), (i.e. add_tiny_header_data()) and adding
  //    'need_bytes' to the frame would cause us to exceed max_frame_size.
  // 2) We called add_header_data, and adding the slice would cause us to exceed
  //    max_frame_size.
  // 3) We're done encoding the header.

  // Thus, len is always <= max_frame_size.
  // max_frame_size is derived from GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE,
  // which has a max allowable value of 16777215 (see chttp_transport.cc).
  // Thus, the following assert can be a debug assert.
  GPR_DEBUG_ASSERT(len <= 16777216);
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

void HPackCompressor::Frame(const EncodeHeaderOptions& options,
                            SliceBuffer& raw, grpc_slice_buffer* output) {
  uint8_t frame_type = GRPC_CHTTP2_FRAME_HEADER;
  uint8_t flags = 0;
  // per the HTTP/2 spec:
  //   A HEADERS frame carries the END_STREAM flag that signals the end of a
  //   stream. However, a HEADERS frame with the END_STREAM flag set can be
  //   followed by CONTINUATION frames on the same stream. Logically, the
  //   CONTINUATION frames are part of the HEADERS frame.
  // Thus, we add the END_STREAM flag to the HEADER frame (the first frame).
  if (options.is_end_of_stream) {
    flags |= GRPC_CHTTP2_DATA_FLAG_END_STREAM;
  }
  options.stats->header_bytes += raw.Length();
  while (frame_type == GRPC_CHTTP2_FRAME_HEADER || raw.Length() > 0) {
    // per the HTTP/2 spec:
    //   A HEADERS frame without the END_HEADERS flag set MUST be followed by
    //   a CONTINUATION frame for the same stream.
    // Thus, we add the END_HEADER flag to the last frame.
    size_t len = raw.Length();
    if (len <= options.max_frame_size) {
      flags |= GRPC_CHTTP2_DATA_FLAG_END_HEADERS;
    } else {
      len = options.max_frame_size;
    }
    FillHeader(grpc_slice_buffer_tiny_add(output, kDataFrameHeaderSize),
               frame_type, options.stream_id, len, flags);
    options.stats->framing_bytes += kDataFrameHeaderSize;
    grpc_slice_buffer_move_first(raw.c_slice_buffer(), len, output);

    frame_type = GRPC_CHTTP2_FRAME_CONTINUATION;
    flags = 0;
  }
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

namespace {
struct WireValue {
  WireValue(uint8_t huffman_prefix, bool insert_null_before_wire_value,
            Slice slice)
      : data(std::move(slice)),
        huffman_prefix(huffman_prefix),
        insert_null_before_wire_value(insert_null_before_wire_value),
        length(data.length() + (insert_null_before_wire_value ? 1 : 0)),
        hpack_length(length) {}
  WireValue(uint8_t huffman_prefix, bool insert_null_before_wire_value,
            Slice slice, size_t hpack_length)
      : data(std::move(slice)),
        huffman_prefix(huffman_prefix),
        insert_null_before_wire_value(insert_null_before_wire_value),
        length(data.length() + (insert_null_before_wire_value ? 1 : 0)),
        hpack_length(hpack_length + (insert_null_before_wire_value ? 1 : 0)) {}
  Slice data;
  const uint8_t huffman_prefix;
  const bool insert_null_before_wire_value;
  const size_t length;
  const size_t hpack_length;
};

// Construct a wire value from a slice.
// true_binary_enabled => use the true binary system
// is_bin_hdr => the header is -bin suffixed
WireValue GetWireValue(Slice value, bool true_binary_enabled, bool is_bin_hdr) {
  if (is_bin_hdr) {
    if (true_binary_enabled) {
      return WireValue(0x00, true, std::move(value));
    } else {
      uint32_t hpack_length;
      Slice output(grpc_chttp2_base64_encode_and_huffman_compress(
          value.c_slice(), &hpack_length));
      return WireValue(0x80, false, std::move(output), hpack_length);
    }
  } else {
    // TODO(ctiller): opportunistically compress non-binary headers
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

  uint32_t hpack_length() { return wire_value_.hpack_length; }

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
}  // namespace

namespace hpack_encoder_detail {
void Encoder::EmitIndexed(uint32_t elem_index) {
  VarintWriter<1> w(elem_index);
  w.Write(0x80, output_.AddTiny(w.length()));
}

uint32_t Encoder::EmitLitHdrWithNonBinaryStringKeyIncIdx(Slice key_slice,
                                                         Slice value_slice) {
  auto key_len = key_slice.length();
  auto value_len = value_slice.length();
  StringKey key(std::move(key_slice));
  key.WritePrefix(0x40, output_.AddTiny(key.prefix_length()));
  output_.Append(key.key());
  NonBinaryStringValue emit(std::move(value_slice));
  emit.WritePrefix(output_.AddTiny(emit.prefix_length()));
  // Allocate an index in the hpack table for this newly emitted entry.
  // (we do so here because we know the length of the key and value)
  uint32_t index = compressor_->table_.AllocateIndex(
      key_len + value_len + hpack_constants::kEntryOverhead);
  output_.Append(emit.data());
  return index;
}

void Encoder::EmitLitHdrWithBinaryStringKeyNotIdx(Slice key_slice,
                                                  Slice value_slice) {
  StringKey key(std::move(key_slice));
  key.WritePrefix(0x00, output_.AddTiny(key.prefix_length()));
  output_.Append(key.key());
  BinaryStringValue emit(std::move(value_slice), use_true_binary_metadata_);
  emit.WritePrefix(output_.AddTiny(emit.prefix_length()));
  output_.Append(emit.data());
}

uint32_t Encoder::EmitLitHdrWithBinaryStringKeyIncIdx(Slice key_slice,
                                                      Slice value_slice) {
  auto key_len = key_slice.length();
  StringKey key(std::move(key_slice));
  key.WritePrefix(0x40, output_.AddTiny(key.prefix_length()));
  output_.Append(key.key());
  BinaryStringValue emit(std::move(value_slice), use_true_binary_metadata_);
  emit.WritePrefix(output_.AddTiny(emit.prefix_length()));
  // Allocate an index in the hpack table for this newly emitted entry.
  // (we do so here because we know the length of the key and value)
  uint32_t index = compressor_->table_.AllocateIndex(
      key_len + emit.hpack_length() + hpack_constants::kEntryOverhead);
  output_.Append(emit.data());
  return index;
}

void Encoder::EmitLitHdrWithBinaryStringKeyNotIdx(uint32_t key_index,
                                                  Slice value_slice) {
  BinaryStringValue emit(std::move(value_slice), use_true_binary_metadata_);
  VarintWriter<4> key(key_index);
  uint8_t* data = output_.AddTiny(key.length() + emit.prefix_length());
  key.Write(0x00, data);
  emit.WritePrefix(data + key.length());
  output_.Append(emit.data());
}

void Encoder::EmitLitHdrWithNonBinaryStringKeyNotIdx(Slice key_slice,
                                                     Slice value_slice) {
  StringKey key(std::move(key_slice));
  key.WritePrefix(0x00, output_.AddTiny(key.prefix_length()));
  output_.Append(key.key());
  NonBinaryStringValue emit(std::move(value_slice));
  emit.WritePrefix(output_.AddTiny(emit.prefix_length()));
  output_.Append(emit.data());
}

void Encoder::AdvertiseTableSizeChange() {
  VarintWriter<3> w(compressor_->table_.max_size());
  w.Write(0x20, output_.AddTiny(w.length()));
}

void SliceIndex::EmitTo(absl::string_view key, const Slice& value,
                        Encoder* encoder) {
  auto& table = encoder->hpack_table();
  using It = std::vector<ValueIndex>::iterator;
  It prev = values_.end();
  size_t transport_length =
      key.length() + value.length() + hpack_constants::kEntryOverhead;
  if (transport_length > HPackEncoderTable::MaxEntrySize()) {
    encoder->EmitLitHdrWithNonBinaryStringKeyNotIdx(
        Slice::FromStaticString(key), value.Ref());
    return;
  }
  // Linear scan through previous values to see if we find the value.
  for (It it = values_.begin(); it != values_.end(); ++it) {
    if (value == it->value) {
      // Got a hit... is it still in the decode table?
      if (table.ConvertableToDynamicIndex(it->index)) {
        // Yes, emit the index and proceed to cleanup.
        encoder->EmitIndexed(table.DynamicIndex(it->index));
      } else {
        // Not current, emit a new literal and update the index.
        it->index = encoder->EmitLitHdrWithNonBinaryStringKeyIncIdx(
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
  uint32_t index = encoder->EmitLitHdrWithNonBinaryStringKeyIncIdx(
      Slice::FromStaticString(key), value.Ref());
  values_.emplace_back(value.Ref(), index);
}

void Encoder::Encode(const Slice& key, const Slice& value) {
  if (absl::EndsWith(key.as_string_view(), "-bin")) {
    EmitLitHdrWithBinaryStringKeyNotIdx(key.Ref(), value.Ref());
  } else {
    EmitLitHdrWithNonBinaryStringKeyNotIdx(key.Ref(), value.Ref());
  }
}

void Compressor<HttpSchemeMetadata, HttpSchemeCompressor>::EncodeWith(
    HttpSchemeMetadata, HttpSchemeMetadata::ValueType value, Encoder* encoder) {
  switch (value) {
    case HttpSchemeMetadata::ValueType::kHttp:
      encoder->EmitIndexed(6);  // :scheme: http
      break;
    case HttpSchemeMetadata::ValueType::kHttps:
      encoder->EmitIndexed(7);  // :scheme: https
      break;
    case HttpSchemeMetadata::ValueType::kInvalid:
      Crash("invalid http scheme encoding");
      break;
  }
}

void Compressor<HttpStatusMetadata, HttpStatusCompressor>::EncodeWith(
    HttpStatusMetadata, uint32_t status, Encoder* encoder) {
  if (status == 200) {
    encoder->EmitIndexed(8);  // :status: 200
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
    encoder->EmitIndexed(index);
  } else {
    encoder->EmitLitHdrWithNonBinaryStringKeyNotIdx(
        Slice::FromStaticString(":status"), Slice::FromInt64(status));
  }
}

void Compressor<HttpMethodMetadata, HttpMethodCompressor>::EncodeWith(
    HttpMethodMetadata, HttpMethodMetadata::ValueType method,
    Encoder* encoder) {
  switch (method) {
    case HttpMethodMetadata::ValueType::kPost:
      encoder->EmitIndexed(3);  // :method: POST
      break;
    case HttpMethodMetadata::ValueType::kGet:
      encoder->EmitIndexed(2);  // :method: GET
      break;
    case HttpMethodMetadata::ValueType::kPut:
      // Right now, we only emit PUT as a method for testing purposes, so it's
      // fine to not index it.
      encoder->EmitLitHdrWithNonBinaryStringKeyNotIdx(
          Slice::FromStaticString(":method"), Slice::FromStaticString("PUT"));
      break;
    case HttpMethodMetadata::ValueType::kInvalid:
      Crash("invalid http method encoding");
      break;
  }
}

void Encoder::EncodeAlwaysIndexed(uint32_t* index, absl::string_view key,
                                  Slice value, size_t) {
  if (compressor_->table_.ConvertableToDynamicIndex(*index)) {
    EmitIndexed(compressor_->table_.DynamicIndex(*index));
  } else {
    *index = EmitLitHdrWithNonBinaryStringKeyIncIdx(
        Slice::FromStaticString(key), std::move(value));
  }
}

void Encoder::EncodeIndexedKeyWithBinaryValue(uint32_t* index,
                                              absl::string_view key,
                                              Slice value) {
  if (compressor_->table_.ConvertableToDynamicIndex(*index)) {
    EmitLitHdrWithBinaryStringKeyNotIdx(
        compressor_->table_.DynamicIndex(*index), std::move(value));
  } else {
    *index = EmitLitHdrWithBinaryStringKeyIncIdx(Slice::FromStaticString(key),
                                                 std::move(value));
  }
}

void Encoder::EncodeRepeatingSliceValue(const absl::string_view& key,
                                        const Slice& slice, uint32_t* index,
                                        size_t max_compression_size) {
  if (hpack_constants::SizeForEntry(key.size(), slice.size()) >
      max_compression_size) {
    EmitLitHdrWithBinaryStringKeyNotIdx(Slice::FromStaticString(key),
                                        slice.Ref());
  } else {
    EncodeIndexedKeyWithBinaryValue(index, key, slice.Ref());
  }
}

void TimeoutCompressorImpl::EncodeWith(absl::string_view key,
                                       Timestamp deadline, Encoder* encoder) {
  Timeout timeout = Timeout::FromDuration(deadline - Timestamp::Now());
  auto& table = encoder->hpack_table();
  for (auto it = previous_timeouts_.begin(); it != previous_timeouts_.end();
       ++it) {
    double ratio = timeout.RatioVersus(it->timeout);
    // If the timeout we're sending is shorter than a previous timeout, but
    // within 3% of it, we'll consider sending it.
    if (ratio > -3 && ratio <= 0 &&
        table.ConvertableToDynamicIndex(it->index)) {
      encoder->EmitIndexed(table.DynamicIndex(it->index));
      // Put this timeout to the front of the queue - forces common timeouts to
      // be considered earlier.
      std::swap(*it, *previous_timeouts_.begin());
      return;
    }
  }
  // Clean out some expired timeouts.
  while (!previous_timeouts_.empty() &&
         !table.ConvertableToDynamicIndex(previous_timeouts_.back().index)) {
    previous_timeouts_.pop_back();
  }
  Slice encoded = timeout.Encode();
  uint32_t index = encoder->EmitLitHdrWithNonBinaryStringKeyIncIdx(
      Slice::FromStaticString(key), std::move(encoded));
  previous_timeouts_.push_back(PreviousTimeout{timeout, index});
}

Encoder::Encoder(HPackCompressor* compressor, bool use_true_binary_metadata,
                 SliceBuffer& output)
    : use_true_binary_metadata_(use_true_binary_metadata),
      compressor_(compressor),
      output_(output) {
  if (std::exchange(compressor_->advertise_table_size_change_, false)) {
    AdvertiseTableSizeChange();
  }
}

}  // namespace hpack_encoder_detail
}  // namespace grpc_core
