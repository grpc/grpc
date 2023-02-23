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

#include "src/core/ext/transport/chttp2/transport/frame_data.h"

#include <stdlib.h>

#include <initializer_list>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport.h"

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
                             grpc_transport_one_way_stats* stats,
                             grpc_slice_buffer* outbuf) {
  grpc_slice hdr;
  uint8_t* p;
  static const size_t header_size = 9;

  hdr = GRPC_SLICE_MALLOC(header_size);
  p = GRPC_SLICE_START_PTR(hdr);
  GPR_ASSERT(write_bytes < (1 << 24));
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

  grpc_slice_buffer_move_first_no_ref(inbuf, write_bytes, outbuf);

  stats->framing_bytes += header_size;
  stats->data_bytes += write_bytes;
}

grpc_core::Poll<grpc_error_handle> grpc_deframe_unprocessed_incoming_frames(
    grpc_chttp2_stream* s, int64_t* min_progress_size,
    grpc_core::SliceBuffer* stream_out, uint32_t* message_flags) {
  grpc_slice_buffer* slices = &s->frame_storage;
  grpc_error_handle error;

  if (slices->length < 5) {
    if (min_progress_size != nullptr) *min_progress_size = 5 - slices->length;
    return grpc_core::Pending{};
  }

  uint8_t header[5];
  grpc_slice_buffer_copy_first_into_buffer(slices, 5, header);

  switch (header[0]) {
    case 0:
      if (message_flags != nullptr) *message_flags = 0;
      break;
    case 1:
      if (message_flags != nullptr) {
        *message_flags = GRPC_WRITE_INTERNAL_COMPRESS;
      }
      break;
    default:
      error = GRPC_ERROR_CREATE(
          absl::StrFormat("Bad GRPC frame type 0x%02x", header[0]));
      error = grpc_error_set_int(error, grpc_core::StatusIntProperty::kStreamId,
                                 static_cast<intptr_t>(s->id));
      return error;
  }

  size_t length = (static_cast<uint32_t>(header[1]) << 24) |
                  (static_cast<uint32_t>(header[2]) << 16) |
                  (static_cast<uint32_t>(header[3]) << 8) |
                  static_cast<uint32_t>(header[4]);

  if (slices->length < length + 5) {
    if (min_progress_size != nullptr) {
      *min_progress_size = length + 5 - slices->length;
    }
    return grpc_core::Pending{};
  }

  if (min_progress_size != nullptr) *min_progress_size = 0;

  if (stream_out != nullptr) {
    s->stats.incoming.framing_bytes += 5;
    s->stats.incoming.data_bytes += length;
    grpc_slice_buffer_move_first_into_buffer(slices, 5, header);
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
  grpc_chttp2_maybe_complete_recv_message(t, s);

  if (is_last && s->received_last_frame) {
    grpc_chttp2_mark_stream_closed(
        t, s, true, false,
        t->is_client
            ? GRPC_ERROR_CREATE("Data frame with END_STREAM flag received")
            : absl::OkStatus());
  }

  return absl::OkStatus();
}
