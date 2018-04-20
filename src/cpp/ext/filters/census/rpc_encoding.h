/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_RPC_ENCODING_H
#define GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_RPC_ENCODING_H

#include <grpc/support/port_platform.h>

#include <string.h>

#include "absl/base/internal/endian.h"
#include "absl/strings/string_view.h"
#include "opencensus/trace/span_context.h"
#include "opencensus/trace/span_id.h"
#include "opencensus/trace/trace_id.h"

namespace grpc {

// TODO: Rename to GrpcTraceContextV0.
struct GrpcTraceContext {
  GrpcTraceContext() {}

  explicit GrpcTraceContext(const ::opencensus::trace::SpanContext& ctx) {
    ctx.trace_id().CopyTo(trace_id);
    ctx.span_id().CopyTo(span_id);
    ctx.trace_options().CopyTo(trace_options);
  }

  ::opencensus::trace::SpanContext ToSpanContext() const {
    return ::opencensus::trace::SpanContext(
        ::opencensus::trace::TraceId(trace_id),
        ::opencensus::trace::SpanId(span_id),
        ::opencensus::trace::TraceOptions(trace_options));
  }

  // TODO: For performance:
  // uint8_t version;
  // uint8_t trace_id_field_id;
  uint8_t trace_id[::opencensus::trace::TraceId::kSize];
  // uint8_t span_id_field_id;
  uint8_t span_id[::opencensus::trace::SpanId::kSize];
  // uint8_t trace_options_field_id;
  uint8_t trace_options[::opencensus::trace::TraceOptions::kSize];
};

// TraceContextEncoding encapsulates the logic for encoding and decoding of
// trace contexts.
class TraceContextEncoding {
 public:
  // Size of encoded GrpcTraceContext. (16 + 8 + 1 + 4)
  static constexpr size_t kGrpcTraceContextSize = 29;
  // Error value.
  static constexpr size_t kEncodeDecodeFailure = 0;

  // Deserializes a GrpcTraceContext from the incoming buffer. Returns the
  // number of bytes deserialized from the buffer. If the incoming buffer is
  // empty or the encoding version is not supported it will return 0 bytes,
  // currently only version 0 is supported. If an unknown field ID is
  // encountered it will return immediately without parsing the rest of the
  // buffer. Inlined for performance reasons.
  static size_t Decode(absl::string_view buf, GrpcTraceContext* tc) {
    if (buf.empty()) {
      return kEncodeDecodeFailure;
    }
    uint8_t version = buf[kVersionIdOffset];
    // TODO: Support other versions later. Only support version 0 for
    // now.
    if (version != kVersionId) {
      return kEncodeDecodeFailure;
    }

    size_t pos = kVersionIdSize;
    while (pos < buf.size()) {
      size_t bytes_read =
          ParseField(absl::string_view(&buf[pos], buf.size() - pos), tc);
      if (bytes_read == 0) {
        break;
      } else {
        pos += bytes_read;
      }
    }
    return pos;
  }

  // Serializes a GrpcTraceContext into the provided buffer. Returns the number
  // of bytes serialized into the buffer. If the buffer is not of sufficient
  // size (it must be at least kGrpcTraceContextSize bytes) it will drop
  // everything and return 0 bytes serialized. Inlined for performance reasons.
  static size_t Encode(const GrpcTraceContext& tc, char* buf, size_t buf_size) {
    if (buf_size < kGrpcTraceContextSize) {
      return kEncodeDecodeFailure;
    }
    buf[kVersionIdOffset] = kVersionId;
    buf[kTraceIdOffset] = kTraceIdField;
    memcpy(&buf[kTraceIdOffset + 1], tc.trace_id,
           opencensus::trace::TraceId::kSize);
    buf[kSpanIdOffset] = kSpanIdField;
    memcpy(&buf[kSpanIdOffset + 1], tc.span_id,
           opencensus::trace::SpanId::kSize);
    buf[kTraceOptionsOffset] = kTraceOptionsField;
    memcpy(&buf[kTraceOptionsOffset + 1], tc.trace_options,
           opencensus::trace::TraceOptions::kSize);
    return kGrpcTraceContextSize;
  }

 private:
  // Parses the next field from the incoming buffer and stores the parsed value
  // in a GrpcTraceContext struct.  If it does not recognize the field ID it
  // will return 0, otherwise it returns the number of bytes read.
  static size_t ParseField(absl::string_view buf, GrpcTraceContext* tc) {
    // TODO: Add support for multi-byte field IDs.
    if (buf.empty()) {
      return 0;
    }
    // Field ID is always the first byte in a field.
    uint32_t field_id = buf[0];
    size_t bytes_read = kFieldIdSize;
    switch (field_id) {
      case kTraceIdField:
        bytes_read += kTraceIdSize;
        if (bytes_read > buf.size()) {
          return 0;
        }
        memcpy(tc->trace_id, &buf[kFieldIdSize],
               opencensus::trace::TraceId::kSize);
        break;
      case kSpanIdField:
        bytes_read += kSpanIdSize;
        if (bytes_read > buf.size()) {
          return 0;
        }
        memcpy(tc->span_id, &buf[kFieldIdSize],
               opencensus::trace::SpanId::kSize);
        break;
      case kTraceOptionsField:
        bytes_read += kTraceOptionsSize;
        if (bytes_read > buf.size()) {
          return 0;
        }
        memcpy(tc->trace_options, &buf[kFieldIdSize],
               opencensus::trace::TraceOptions::kSize);
        break;
      default:  // Invalid field ID
        return 0;
    }

    return bytes_read;
  }

  // Size of Version ID.
  static constexpr size_t kVersionIdSize = 1;
  // Size of Field ID.
  static constexpr size_t kFieldIdSize = 1;

  // Offset and value for currently supported version ID.
  static constexpr size_t kVersionIdOffset = 0;
  static constexpr size_t kVersionId = 0;

  // Fixed Field ID values:
  enum FieldIdValue {
    kTraceIdField = 0,
    kSpanIdField = 1,
    kTraceOptionsField = 2,
  };

  // Field data sizes in bytes
  enum FieldSize {
    kTraceIdSize = 16,
    kSpanIdSize = 8,
    kTraceOptionsSize = 1,
  };

  // Fixed size offsets for field ID start positions during encoding.  Field
  // data immediately follows.
  enum FieldIdOffset {
    kTraceIdOffset = kVersionIdSize,
    kSpanIdOffset = kTraceIdOffset + kFieldIdSize + kTraceIdSize,
    kTraceOptionsOffset = kSpanIdOffset + kFieldIdSize + kSpanIdSize,
  };

  TraceContextEncoding() = delete;
  TraceContextEncoding(const TraceContextEncoding&) = delete;
  TraceContextEncoding(TraceContextEncoding&&) = delete;
  TraceContextEncoding operator=(const TraceContextEncoding&) = delete;
  TraceContextEncoding operator=(TraceContextEncoding&&) = delete;
};

// TODO: This may not be needed. Check to see if opencensus requires
// a trailing server response.
// RpcServerStatsEncoding encapsulates the logic for encoding and decoding of
// rpc server stats messages. Rpc server stats consists of a uint64_t time
// value (server latency in nanoseconds).
class RpcServerStatsEncoding {
 public:
  // Size of encoded RPC server stats.
  static constexpr size_t kRpcServerStatsSize = 10;
  // Error value.
  static constexpr size_t kEncodeDecodeFailure = 0;

  // Deserializes rpc server stats from the incoming 'buf' into *time.  Returns
  // number of bytes decoded. If the buffer is of insufficient size (it must be
  // at least kRpcServerStatsSize bytes) or the encoding version or field ID are
  // unrecognized, *time will be set to 0 and it will return
  // kEncodeDecodeFailure. Inlined for performance reasons.
  static size_t Decode(absl::string_view buf, uint64_t* time) {
    if (buf.size() < kRpcServerStatsSize) {
      *time = 0;
      return kEncodeDecodeFailure;
    }

    uint8_t version = buf[kVersionIdOffset];
    uint32_t fieldID = buf[kServerElapsedTimeOffset];
    if (version != kVersionId || fieldID != kServerElapsedTimeField) {
      *time = 0;
      return kEncodeDecodeFailure;
    }
    *time = absl::little_endian::Load64(
        &buf[kServerElapsedTimeOffset + kFieldIdSize]);
    return kRpcServerStatsSize;
  }

  // Serializes rpc server stats into the provided buffer.  It returns the
  // number of bytes written to the buffer. If the buffer is smaller than
  // kRpcServerStatsSize bytes it will return kEncodeDecodeFailure. Inlined for
  // performance reasons.
  static size_t Encode(uint64_t time, char* buf, size_t buf_size) {
    if (buf_size < kRpcServerStatsSize) {
      return kEncodeDecodeFailure;
    }

    buf[kVersionIdOffset] = kVersionId;
    buf[kServerElapsedTimeOffset] = kServerElapsedTimeField;
    absl::little_endian::Store64(&buf[kServerElapsedTimeOffset + kFieldIdSize],
                                 time);
    return kRpcServerStatsSize;
  }

 private:
  // Size of Version ID.
  static constexpr size_t kVersionIdSize = 1;
  // Size of Field ID.
  static constexpr size_t kFieldIdSize = 1;

  // Offset and value for currently supported version ID.
  static constexpr size_t kVersionIdOffset = 0;
  static constexpr size_t kVersionId = 0;

  enum FieldIdValue {
    kServerElapsedTimeField = 0,
  };

  enum FieldSize {
    kServerElapsedTimeSize = 8,
  };

  enum FieldIdOffset {
    kServerElapsedTimeOffset = kVersionIdSize,
  };

  RpcServerStatsEncoding() = delete;
  RpcServerStatsEncoding(const RpcServerStatsEncoding&) = delete;
  RpcServerStatsEncoding(RpcServerStatsEncoding&&) = delete;
  RpcServerStatsEncoding operator=(const RpcServerStatsEncoding&) = delete;
  RpcServerStatsEncoding operator=(RpcServerStatsEncoding&&) = delete;
};

}  // namespace grpc

#endif /* GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_RPC_ENCODING_H */
