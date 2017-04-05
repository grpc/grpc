/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/ext/transport/chttp2/transport/frame_ping.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

static bool g_disable_ping_ack = false;

grpc_slice grpc_chttp2_ping_create(uint8_t ack, uint64_t opaque_8bytes) {
  grpc_slice slice = grpc_slice_malloc(9 + 8);
  uint8_t *p = GRPC_SLICE_START_PTR(slice);

  *p++ = 0;
  *p++ = 0;
  *p++ = 8;
  *p++ = GRPC_CHTTP2_FRAME_PING;
  *p++ = ack ? 1 : 0;
  *p++ = 0;
  *p++ = 0;
  *p++ = 0;
  *p++ = 0;
  *p++ = (uint8_t)(opaque_8bytes >> 56);
  *p++ = (uint8_t)(opaque_8bytes >> 48);
  *p++ = (uint8_t)(opaque_8bytes >> 40);
  *p++ = (uint8_t)(opaque_8bytes >> 32);
  *p++ = (uint8_t)(opaque_8bytes >> 24);
  *p++ = (uint8_t)(opaque_8bytes >> 16);
  *p++ = (uint8_t)(opaque_8bytes >> 8);
  *p++ = (uint8_t)(opaque_8bytes);

  return slice;
}

grpc_error *grpc_chttp2_ping_parser_begin_frame(grpc_chttp2_ping_parser *parser,
                                                uint32_t length,
                                                uint8_t flags) {
  if (flags & 0xfe || length != 8) {
    char *msg;
    gpr_asprintf(&msg, "invalid ping: length=%d, flags=%02x", length, flags);
    grpc_error *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return error;
  }
  parser->byte = 0;
  parser->is_ack = flags;
  parser->opaque_8bytes = 0;
  return GRPC_ERROR_NONE;
}

grpc_error *grpc_chttp2_ping_parser_parse(grpc_exec_ctx *exec_ctx, void *parser,
                                          grpc_chttp2_transport *t,
                                          grpc_chttp2_stream *s,
                                          grpc_slice slice, int is_last) {
  uint8_t *const beg = GRPC_SLICE_START_PTR(slice);
  uint8_t *const end = GRPC_SLICE_END_PTR(slice);
  uint8_t *cur = beg;
  grpc_chttp2_ping_parser *p = parser;

  while (p->byte != 8 && cur != end) {
    p->opaque_8bytes |= (((uint64_t)*cur) << (56 - 8 * p->byte));
    cur++;
    p->byte++;
  }

  if (p->byte == 8) {
    GPR_ASSERT(is_last);
    if (p->is_ack) {
      grpc_chttp2_ack_ping(exec_ctx, t, p->opaque_8bytes);
    } else {
      if (!t->is_client) {
        gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
        gpr_timespec next_allowed_ping =
            gpr_time_add(t->ping_recv_state.last_ping_recv_time,
                         t->ping_policy.min_ping_interval_without_data);

        if (t->keepalive_permit_without_calls == 0 &&
            grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
          /* According to RFC1122, the interval of TCP Keep-Alive is default to
             no less than two hours. When there is no outstanding streams, we
             restrict the number of PINGS equivalent to TCP Keep-Alive. */
          next_allowed_ping =
              gpr_time_add(t->ping_recv_state.last_ping_recv_time,
                           gpr_time_from_seconds(7200, GPR_TIMESPAN));
        }

        if (gpr_time_cmp(next_allowed_ping, now) > 0) {
          grpc_chttp2_add_ping_strike(exec_ctx, t);
        }

        t->ping_recv_state.last_ping_recv_time = now;
      }
      if (!g_disable_ping_ack) {
        if (t->ping_ack_count == t->ping_ack_capacity) {
          t->ping_ack_capacity = GPR_MAX(t->ping_ack_capacity * 3 / 2, 3);
          t->ping_acks = gpr_realloc(
              t->ping_acks, t->ping_ack_capacity * sizeof(*t->ping_acks));
        }
        t->ping_acks[t->ping_ack_count++] = p->opaque_8bytes;
        grpc_chttp2_initiate_write(exec_ctx, t, false, "ping response");
      }
    }
  }

  return GRPC_ERROR_NONE;
}

void grpc_set_disable_ping_ack(bool disable_ping_ack) {
  g_disable_ping_ack = disable_ping_ack;
}
