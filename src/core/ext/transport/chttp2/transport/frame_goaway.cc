// Copyright 2026 gRPC authors.
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

#include "src/core/lib/slice/byte_source.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>

#include "gtest/gtest.h"

grpc_error_handle grpc_chttp2_goaway_parser_begin_frame(
    grpc_chttp2_goaway_parser* p, uint32_t length, uint8_t /*flags*/) {
  if (length < 8) {
    return GRPC_ERROR_CREATE(
        absl::StrFormat("goaway frame too short (%d bytes)", length));
  }

  p->debug_length = length - 8;
  p->debug_slice_buffer.Clear();
  p->state = GRPC_CHTTP2_GOAWAY_LSI0;
  return absl::OkStatus();
}

grpc_error_handle grpc_chttp2_goaway_parser_parse(void* parser,
                                                  grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* /*s*/,
                                                  const grpc_slice& slice,
                                                  int is_last) {
  const uint8_t* const beg = GRPC_SLICE_START_PTR(slice);
  const uint8_t* const end = GRPC_SLICE_END_PTR(slice);
  const uint8_t* cur = beg;
  grpc_chttp2_goaway_parser* p =
      static_cast<grpc_chttp2_goaway_parser*>(parser);

  switch (p->state) {
    case GRPC_CHTTP2_GOAWAY_LSI0:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI0;
        return absl::OkStatus();
      }
      p->last_stream_id = (static_cast<uint32_t>(*cur)) << 24;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_LSI1:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI1;
        return absl::OkStatus();
      }
      p->last_stream_id |= (static_cast<uint32_t>(*cur)) << 16;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_LSI2:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI2;
        return absl::OkStatus();
      }
      p->last_stream_id |= (static_cast<uint32_t>(*cur)) << 8;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_LSI3:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI3;
        return absl::OkStatus();
      }
      p->last_stream_id |= (static_cast<uint32_t>(*cur));
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_ERR0:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR0;
        return absl::OkStatus();
      }
      p->error_code = (static_cast<uint32_t>(*cur)) << 24;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_ERR1:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR1;
        return absl::OkStatus();
      }
      p->error_code |= (static_cast<uint32_t>(*cur)) << 16;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_ERR2:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR2;
        return absl::OkStatus();
      }
      p->error_code |= (static_cast<uint32_t>(*cur)) << 8;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_ERR3:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR3;
        return absl::OkStatus();
      }
      p->error_code |= (static_cast<uint32_t>(*cur));
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_DEBUG:
      if (end != cur) {
        p->debug_slice_buffer.AppendIndexed(grpc_core::Slice::FromCopiedBuffer(
            cur, static_cast<size_t>(end - cur)));
      }
      GRPC_CHECK(p->debug_slice_buffer.Length() <= p->debug_length);
      p->state = GRPC_CHTTP2_GOAWAY_DEBUG;
      if (is_last) {
        std::string debug_str = p->debug_slice_buffer.JoinIntoString();

        grpc_chttp2_add_incoming_goaway(t, p->error_code, p->last_stream_id,
                                        absl::string_view(debug_str));

        t->http2_ztrace_collector.Append(
            [last_stream_id = p->last_stream_id, error_code = p->error_code,
             debug_str = std::move(debug_str)]() mutable {
              return grpc_core::H2GoAwayTrace<true>{last_stream_id, error_code,
                                                    std::move(debug_str)};
            });
        p->debug_slice_buffer.Clear();
      }
      return absl::OkStatus();
  }
  GPR_UNREACHABLE_CODE(return GRPC_ERROR_CREATE("Should never reach here"));
}

}  // namespace
}  // namespace grpc_core
