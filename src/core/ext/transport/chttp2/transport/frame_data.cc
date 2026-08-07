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

#include "src/core/ext/transport/chttp2/transport/frame_data.h"

#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <stdlib.h>

#include <algorithm>

#include "src/core/ext/transport/chttp2/transport/call_tracer_wrapper.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/stats.h"
#include "src/core/transport/message_size_service_config.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/status_helper.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

absl::Status grpc_chttp2_data_parser_begin_frame(uint8_t flags,
                                                 uint32_t stream_id,
                                                 grpc_chttp2_stream* s) {
  if (flags & ~GRPC_CHTTP2_DATA_FLAG_END_STREAM) {
    return absl::InternalError(absl::StrFormat(
        "unsupported data flags: 0x%02x stream: %d", flags, stream_id));
  }

  if (flags & GRPC_CHTTP2_DATA_FLAG_END_STREAM) {
    s->received_last_frame = true;
    s->eos_received = true;
  } else {
    s->received_last_frame = false;
  }

  return absl::OkStatus();
}

void grpc_chttp2_encode_data(uint32_t id, grpc_slice_buffer* inbuf,
                             uint32_t write_bytes, int is_eof,
                             grpc_core::CallTracerInterface* call_tracer,
                             grpc_core::Http2ZTraceCollector* ztrace_collector,
                             grpc_slice_buffer* outbuf) {
  grpc_slice hdr;
  uint8_t* p;
  static const size_t header_size = 9;

  hdr = GRPC_SLICE_MALLOC(header_size);
  p = GRPC_SLICE_START_PTR(hdr);
  GRPC_CHECK(write_bytes < (1 << 24));
  *p++ = static_cast<uint8_t>(write_bytes >> 16);
  *p++ = static_cast<uint8_t>(write_bytes >> 8);
  *p++ = static_cast<uint8_t>(write_bytes);
  *p++ = GRPC_CHTTP2_FRAME_DATA;
  *p++ = is_eof ? GRPC_CHTTP2_DATA_FLAG_END_STREAM : 0;
  *p++ = static_cast<uint8_t>(id >> 24);
  *p++ = static_cast<uint8_t>(id >> 16);
  *p++ = static_cast<uint8_t>(id >> 8);
  *p++ = static_cast<uint8_t>(id);
  grpc_slice_buffer_add(outbuf, hdr);

  ztrace_collector->Append(
      grpc_core::H2DataTrace<false>{id, is_eof != 0, write_bytes});

  grpc_slice_buffer_move_first_no_ref(inbuf, write_bytes, outbuf);

  grpc_core::http2_global_stats().IncrementHttp2WriteDataFrameSize(write_bytes);
  call_tracer->RecordOutgoingBytes({header_size, 0, 0});
}

static uint32_t message_length_from_header(
    const uint8_t header[GRPC_HEADER_SIZE_IN_BYTES]) {
  return (static_cast<uint32_t>(header[1]) << 24) |
         (static_cast<uint32_t>(header[2]) << 16) |
         (static_cast<uint32_t>(header[3]) << 8) |
         static_cast<uint32_t>(header[4]);
}

static grpc_error_handle parse_grpc_frame_header(
    grpc_chttp2_stream* s, const uint8_t header[GRPC_HEADER_SIZE_IN_BYTES],
    uint32_t* message_flags) {
  switch (header[0]) {
    case 0:
      if (message_flags != nullptr) *message_flags = 0;
      break;
    case 1:
      if (message_flags != nullptr) {
        *message_flags = GRPC_WRITE_INTERNAL_COMPRESS;
      }
      break;
    default: {
      grpc_error_handle error = GRPC_ERROR_CREATE(
          absl::StrFormat("Bad GRPC frame type 0x%02x", header[0]));
      return grpc_error_set_int(error, grpc_core::StatusIntProperty::kStreamId,
                                static_cast<intptr_t>(s->id));
    }
  }

  return absl::OkStatus();
}

static grpc_error_handle message_too_large_error(grpc_chttp2_stream* s,
                                                 uint32_t message_length,
                                                 uint32_t max_length) {
  grpc_error_handle error = absl::ResourceExhaustedError(absl::StrFormat(
      "%s: Received message larger than max (%u vs. %u)",
      s->t->is_client ? "CLIENT" : "SERVER", message_length, max_length));
  return grpc_error_set_int(error, grpc_core::StatusIntProperty::kStreamId,
                            static_cast<intptr_t>(s->id));
}

static grpc_error_handle check_incoming_message_size(grpc_chttp2_transport* t,
                                                     grpc_chttp2_stream* s,
                                                     const grpc_slice& slice) {
  const std::optional<uint32_t> max_receive_message_length =
      grpc_core::GetMaxRecvSizeFromCallContext(s->arena,
                                               t->max_receive_message_length);
  if (!max_receive_message_length.has_value()) return absl::OkStatus();

  const uint8_t* p = GRPC_SLICE_START_PTR(slice);
  size_t remaining = GRPC_SLICE_LENGTH(slice);
  while (remaining > 0) {
    if (s->incoming_grpc_message_bytes_remaining > 0) {
      const size_t consumed =
          std::min<size_t>(remaining, s->incoming_grpc_message_bytes_remaining);
      s->incoming_grpc_message_bytes_remaining -=
          static_cast<uint32_t>(consumed);
      p += consumed;
      remaining -= consumed;
      continue;
    }
    while (remaining > 0 &&
           s->incoming_grpc_header_bytes < GRPC_HEADER_SIZE_IN_BYTES) {
      s->incoming_grpc_header[s->incoming_grpc_header_bytes++] = *p++;
      --remaining;
    }
    if (s->incoming_grpc_header_bytes < GRPC_HEADER_SIZE_IN_BYTES) break;
    const uint32_t message_length =
        message_length_from_header(s->incoming_grpc_header);
    const bool is_uncompressed = s->incoming_grpc_header[0] == 0;
    s->incoming_grpc_header_bytes = 0;
    if (is_uncompressed && message_length > *max_receive_message_length) {
      return message_too_large_error(s, message_length,
                                     *max_receive_message_length);
    }
    s->incoming_grpc_message_bytes_remaining = message_length;
  }
  return absl::OkStatus();
}

grpc_core::Poll<grpc_error_handle> grpc_deframe_unprocessed_incoming_frames(
    grpc_chttp2_stream* s, int64_t* min_progress_size,
    grpc_core::SliceBuffer* stream_out, uint32_t* message_flags) {
  grpc_slice_buffer* slices = &s->frame_storage;

  if (slices->length < GRPC_HEADER_SIZE_IN_BYTES) {
    if (min_progress_size != nullptr) {
      *min_progress_size = GRPC_HEADER_SIZE_IN_BYTES - slices->length;
    }
    return grpc_core::Pending{};
  }

  uint8_t header[GRPC_HEADER_SIZE_IN_BYTES];
  grpc_slice_buffer_copy_first_into_buffer(slices, GRPC_HEADER_SIZE_IN_BYTES,
                                           header);
  grpc_error_handle error = parse_grpc_frame_header(s, header, message_flags);
  if (!error.ok()) return error;
  const uint32_t message_length = message_length_from_header(header);
  const size_t length = message_length;

  if (slices->length < length + GRPC_HEADER_SIZE_IN_BYTES) {
    if (min_progress_size != nullptr) {
      *min_progress_size = length + GRPC_HEADER_SIZE_IN_BYTES - slices->length;
    }
    return grpc_core::Pending{};
  }

  if (min_progress_size != nullptr) *min_progress_size = 0;

  if (stream_out != nullptr) {
    s->call_tracer_wrapper.RecordIncomingBytes(
        {GRPC_HEADER_SIZE_IN_BYTES, length, 0});
    grpc_slice_buffer_move_first_into_buffer(slices, GRPC_HEADER_SIZE_IN_BYTES,
                                             header);
    grpc_slice_buffer_move_first(slices, length, stream_out->c_slice_buffer());
  }

  return absl::OkStatus();
}

grpc_error_handle grpc_chttp2_data_parser_parse(void* /*parser*/,
                                                grpc_chttp2_transport* t,
                                                grpc_chttp2_stream* s,
                                                const grpc_slice& slice,
                                                int is_last) {
  grpc_core::CSliceRef(slice);
  grpc_slice_buffer_add(&s->frame_storage, slice);
  grpc_error_handle error = check_incoming_message_size(t, s, slice);
  if (!error.ok()) return error;
  grpc_chttp2_maybe_complete_recv_message(t, s);

  if (is_last) {
    t->http2_ztrace_collector.Append(grpc_core::H2DataTrace<true>{
        t->incoming_stream_id,
        (t->incoming_frame_flags & GRPC_CHTTP2_DATA_FLAG_END_STREAM) != 0,
        t->incoming_frame_size});
  }
  if (is_last && s->received_last_frame) {
    grpc_chttp2_mark_stream_closed(
        t, s, true, false,
        t->is_client
            ? GRPC_ERROR_CREATE("Data frame with END_STREAM flag received")
            : absl::OkStatus());
  }

  return absl::OkStatus();
}
