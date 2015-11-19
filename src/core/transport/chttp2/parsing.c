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

#include "src/core/transport/chttp2/internal.h"

#include <string.h>

#include "src/core/profiling/timers.h"
#include "src/core/transport/chttp2/http2_errors.h"
#include "src/core/transport/chttp2/status_conversion.h"
#include "src/core/transport/chttp2/timeout_encoding.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

static int init_frame_parser(grpc_exec_ctx *exec_ctx,
                             grpc_chttp2_transport_parsing *transport_parsing);
static int init_header_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing,
    int is_continuation);
static int init_data_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing);
static int init_rst_stream_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing);
static int init_settings_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing);
static int init_window_update_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing);
static int init_ping_parser(grpc_exec_ctx *exec_ctx,
                            grpc_chttp2_transport_parsing *transport_parsing);
static int init_goaway_parser(grpc_exec_ctx *exec_ctx,
                              grpc_chttp2_transport_parsing *transport_parsing);
static int init_skip_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing,
    int is_header);

static int parse_frame_slice(grpc_exec_ctx *exec_ctx,
                             grpc_chttp2_transport_parsing *transport_parsing,
                             gpr_slice slice, int is_last);

void grpc_chttp2_prepare_to_read(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_parsing *transport_parsing) {
  grpc_chttp2_stream_global *stream_global;
  grpc_chttp2_stream_parsing *stream_parsing;

  GPR_TIMER_BEGIN("grpc_chttp2_prepare_to_read", 0);

  transport_parsing->next_stream_id = transport_global->next_stream_id;
  transport_parsing->last_sent_max_table_size =
      transport_global->settings[GRPC_SENT_SETTINGS]
                                [GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE];

  /* update the parsing view of incoming window */
  while (grpc_chttp2_list_pop_unannounced_incoming_window_available(
      transport_global, transport_parsing, &stream_global, &stream_parsing)) {
    GRPC_CHTTP2_FLOW_MOVE_STREAM("parse", transport_parsing, stream_parsing,
                                 incoming_window, stream_global,
                                 unannounced_incoming_window_for_parse);
  }

  GPR_TIMER_END("grpc_chttp2_prepare_to_read", 0);
}

void grpc_chttp2_publish_reads(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_parsing *transport_parsing) {
  grpc_chttp2_stream_global *stream_global;
  grpc_chttp2_stream_parsing *stream_parsing;
  int was_zero;
  int is_zero;

  /* transport_parsing->last_incoming_stream_id is used as
     last-grpc_chttp2_stream-id when
     sending GOAWAY frame.
     https://tools.ietf.org/html/draft-ietf-httpbis-http2-17#section-6.8
     says that last-grpc_chttp2_stream-id is peer-initiated grpc_chttp2_stream
     ID.  So,
     since we don't have server pushed streams, client should send
     GOAWAY last-grpc_chttp2_stream-id=0 in this case. */
  if (!transport_parsing->is_client) {
    transport_global->last_incoming_stream_id =
        transport_parsing->incoming_stream_id;
  }

  /* copy parsing qbuf to global qbuf */
  gpr_slice_buffer_move_into(&transport_parsing->qbuf, &transport_global->qbuf);

  /* update global settings */
  if (transport_parsing->settings_updated) {
    memcpy(transport_global->settings[GRPC_PEER_SETTINGS],
           transport_parsing->settings, sizeof(transport_parsing->settings));
    transport_parsing->settings_updated = 0;
  }

  /* update settings based on ack if received */
  if (transport_parsing->settings_ack_received) {
    memcpy(transport_global->settings[GRPC_ACKED_SETTINGS],
           transport_global->settings[GRPC_SENT_SETTINGS],
           GRPC_CHTTP2_NUM_SETTINGS * sizeof(gpr_uint32));
    transport_parsing->settings_ack_received = 0;
    transport_global->sent_local_settings = 0;
  }

  /* move goaway to the global state if we received one (it will be
     published later */
  if (transport_parsing->goaway_received) {
    grpc_chttp2_add_incoming_goaway(exec_ctx, transport_global,
                                    (gpr_uint32)transport_parsing->goaway_error,
                                    transport_parsing->goaway_text);
    transport_parsing->goaway_text = gpr_empty_slice();
    transport_parsing->goaway_received = 0;
  }

  /* propagate flow control tokens to global state */
  was_zero = transport_global->outgoing_window <= 0;
  GRPC_CHTTP2_FLOW_MOVE_TRANSPORT("parsed", transport_global, outgoing_window,
                                  transport_parsing, outgoing_window);
  is_zero = transport_global->outgoing_window <= 0;
  if (was_zero && !is_zero) {
    while (grpc_chttp2_list_pop_stalled_by_transport(transport_global,
                                                     &stream_global)) {
      grpc_chttp2_list_add_writable_stream(transport_global, stream_global);
    }
  }

  if (transport_parsing->incoming_window <
      transport_global->connection_window_target * 3 / 4) {
    gpr_int64 announce_bytes = transport_global->connection_window_target -
                               transport_parsing->incoming_window;
    GRPC_CHTTP2_FLOW_CREDIT_TRANSPORT("parsed", transport_global,
                                      announce_incoming_window, announce_bytes);
    GRPC_CHTTP2_FLOW_CREDIT_TRANSPORT("parsed", transport_parsing,
                                      incoming_window, announce_bytes);
  }

  /* for each stream that saw an update, fixup global state */
  while (grpc_chttp2_list_pop_parsing_seen_stream(
      transport_global, transport_parsing, &stream_global, &stream_parsing)) {
    if (stream_parsing->seen_error) {
      stream_global->seen_error = 1;
      grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
    }

    /* update outgoing flow control window */
    was_zero = stream_global->outgoing_window <= 0;
    GRPC_CHTTP2_FLOW_MOVE_STREAM("parsed", transport_global, stream_global,
                                 outgoing_window, stream_parsing,
                                 outgoing_window);
    is_zero = stream_global->outgoing_window <= 0;
    if (was_zero && !is_zero) {
      grpc_chttp2_list_add_writable_stream(transport_global, stream_global);
    }

    stream_global->max_recv_bytes -= (gpr_uint32)GPR_MIN(
        stream_global->max_recv_bytes, stream_parsing->received_bytes);
    stream_parsing->received_bytes = 0;

    /* publish incoming stream ops */
    if (stream_global->incoming_frames.tail != NULL) {
      stream_global->incoming_frames.tail->is_tail = 0;
    }
    if (stream_parsing->data_parser.incoming_frames.head != NULL) {
      grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
    }
    grpc_chttp2_incoming_frame_queue_merge(
        &stream_global->incoming_frames,
        &stream_parsing->data_parser.incoming_frames);
    if (stream_global->incoming_frames.tail != NULL) {
      stream_global->incoming_frames.tail->is_tail = 1;
    }

    if (!stream_global->published_initial_metadata &&
        stream_parsing->got_metadata_on_parse[0]) {
      stream_parsing->got_metadata_on_parse[0] = 0;
      stream_global->published_initial_metadata = 1;
      GPR_SWAP(grpc_chttp2_incoming_metadata_buffer,
               stream_parsing->metadata_buffer[0],
               stream_global->received_initial_metadata);
      grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
    }
    if (!stream_global->published_trailing_metadata &&
        stream_parsing->got_metadata_on_parse[1]) {
      stream_parsing->got_metadata_on_parse[1] = 0;
      stream_global->published_trailing_metadata = 1;
      GPR_SWAP(grpc_chttp2_incoming_metadata_buffer,
               stream_parsing->metadata_buffer[1],
               stream_global->received_trailing_metadata);
      grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
    }

    if (stream_parsing->saw_rst_stream) {
      if (stream_parsing->rst_stream_reason != GRPC_CHTTP2_NO_ERROR) {
        grpc_status_code status_code = grpc_chttp2_http2_error_to_grpc_status(
            (grpc_chttp2_error_code)stream_parsing->rst_stream_reason);
        char *status_details;
        gpr_slice slice_details;
        gpr_asprintf(&status_details, "Received RST_STREAM err=%d",
                     stream_parsing->rst_stream_reason);
        slice_details = gpr_slice_from_copied_string(status_details);
        gpr_free(status_details);
        grpc_chttp2_fake_status(exec_ctx, transport_global, stream_global,
                                status_code, &slice_details);
      }
      grpc_chttp2_mark_stream_closed(exec_ctx, transport_global, stream_global,
                                     1, 1);
    }

    if (stream_parsing->received_close) {
      grpc_chttp2_mark_stream_closed(exec_ctx, transport_global, stream_global,
                                     1, 0);
    }
  }
}

int grpc_chttp2_perform_read(grpc_exec_ctx *exec_ctx,
                             grpc_chttp2_transport_parsing *transport_parsing,
                             gpr_slice slice) {
  gpr_uint8 *beg = GPR_SLICE_START_PTR(slice);
  gpr_uint8 *end = GPR_SLICE_END_PTR(slice);
  gpr_uint8 *cur = beg;

  if (cur == end) return 1;

  switch (transport_parsing->deframe_state) {
    case GRPC_DTS_CLIENT_PREFIX_0:
    case GRPC_DTS_CLIENT_PREFIX_1:
    case GRPC_DTS_CLIENT_PREFIX_2:
    case GRPC_DTS_CLIENT_PREFIX_3:
    case GRPC_DTS_CLIENT_PREFIX_4:
    case GRPC_DTS_CLIENT_PREFIX_5:
    case GRPC_DTS_CLIENT_PREFIX_6:
    case GRPC_DTS_CLIENT_PREFIX_7:
    case GRPC_DTS_CLIENT_PREFIX_8:
    case GRPC_DTS_CLIENT_PREFIX_9:
    case GRPC_DTS_CLIENT_PREFIX_10:
    case GRPC_DTS_CLIENT_PREFIX_11:
    case GRPC_DTS_CLIENT_PREFIX_12:
    case GRPC_DTS_CLIENT_PREFIX_13:
    case GRPC_DTS_CLIENT_PREFIX_14:
    case GRPC_DTS_CLIENT_PREFIX_15:
    case GRPC_DTS_CLIENT_PREFIX_16:
    case GRPC_DTS_CLIENT_PREFIX_17:
    case GRPC_DTS_CLIENT_PREFIX_18:
    case GRPC_DTS_CLIENT_PREFIX_19:
    case GRPC_DTS_CLIENT_PREFIX_20:
    case GRPC_DTS_CLIENT_PREFIX_21:
    case GRPC_DTS_CLIENT_PREFIX_22:
    case GRPC_DTS_CLIENT_PREFIX_23:
      while (cur != end && transport_parsing->deframe_state != GRPC_DTS_FH_0) {
        if (*cur != GRPC_CHTTP2_CLIENT_CONNECT_STRING[transport_parsing
                                                          ->deframe_state]) {
          gpr_log(GPR_INFO,
                  "Connect string mismatch: expected '%c' (%d) got '%c' (%d) "
                  "at byte %d",
                  GRPC_CHTTP2_CLIENT_CONNECT_STRING[transport_parsing
                                                        ->deframe_state],
                  (int)(gpr_uint8)GRPC_CHTTP2_CLIENT_CONNECT_STRING
                      [transport_parsing->deframe_state],
                  *cur, (int)*cur, transport_parsing->deframe_state);
          return 0;
        }
        ++cur;
        ++transport_parsing->deframe_state;
      }
      if (cur == end) {
        return 1;
      }
    /* fallthrough */
    dts_fh_0:
    case GRPC_DTS_FH_0:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_frame_size = ((gpr_uint32)*cur) << 16;
      if (++cur == end) {
        transport_parsing->deframe_state = GRPC_DTS_FH_1;
        return 1;
      }
    /* fallthrough */
    case GRPC_DTS_FH_1:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_frame_size |= ((gpr_uint32)*cur) << 8;
      if (++cur == end) {
        transport_parsing->deframe_state = GRPC_DTS_FH_2;
        return 1;
      }
    /* fallthrough */
    case GRPC_DTS_FH_2:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_frame_size |= *cur;
      if (++cur == end) {
        transport_parsing->deframe_state = GRPC_DTS_FH_3;
        return 1;
      }
    /* fallthrough */
    case GRPC_DTS_FH_3:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_frame_type = *cur;
      if (++cur == end) {
        transport_parsing->deframe_state = GRPC_DTS_FH_4;
        return 1;
      }
    /* fallthrough */
    case GRPC_DTS_FH_4:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_frame_flags = *cur;
      if (++cur == end) {
        transport_parsing->deframe_state = GRPC_DTS_FH_5;
        return 1;
      }
    /* fallthrough */
    case GRPC_DTS_FH_5:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_stream_id = (((gpr_uint32)*cur) & 0x7f) << 24;
      if (++cur == end) {
        transport_parsing->deframe_state = GRPC_DTS_FH_6;
        return 1;
      }
    /* fallthrough */
    case GRPC_DTS_FH_6:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_stream_id |= ((gpr_uint32)*cur) << 16;
      if (++cur == end) {
        transport_parsing->deframe_state = GRPC_DTS_FH_7;
        return 1;
      }
    /* fallthrough */
    case GRPC_DTS_FH_7:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_stream_id |= ((gpr_uint32)*cur) << 8;
      if (++cur == end) {
        transport_parsing->deframe_state = GRPC_DTS_FH_8;
        return 1;
      }
    /* fallthrough */
    case GRPC_DTS_FH_8:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_stream_id |= ((gpr_uint32)*cur);
      transport_parsing->deframe_state = GRPC_DTS_FRAME;
      if (!init_frame_parser(exec_ctx, transport_parsing)) {
        return 0;
      }
      if (transport_parsing->incoming_stream_id) {
        transport_parsing->last_incoming_stream_id =
            transport_parsing->incoming_stream_id;
      }
      if (transport_parsing->incoming_frame_size == 0) {
        if (!parse_frame_slice(exec_ctx, transport_parsing, gpr_empty_slice(),
                               1)) {
          return 0;
        }
        transport_parsing->incoming_stream = NULL;
        if (++cur == end) {
          transport_parsing->deframe_state = GRPC_DTS_FH_0;
          return 1;
        }
        goto dts_fh_0; /* loop */
      }
      if (++cur == end) {
        return 1;
      }
    /* fallthrough */
    case GRPC_DTS_FRAME:
      GPR_ASSERT(cur < end);
      if ((gpr_uint32)(end - cur) == transport_parsing->incoming_frame_size) {
        if (!parse_frame_slice(exec_ctx, transport_parsing,
                               gpr_slice_sub_no_ref(slice, (size_t)(cur - beg),
                                                    (size_t)(end - beg)),
                               1)) {
          return 0;
        }
        transport_parsing->deframe_state = GRPC_DTS_FH_0;
        transport_parsing->incoming_stream = NULL;
        return 1;
      } else if ((gpr_uint32)(end - cur) >
                 transport_parsing->incoming_frame_size) {
        size_t cur_offset = (size_t)(cur - beg);
        if (!parse_frame_slice(
                exec_ctx, transport_parsing,
                gpr_slice_sub_no_ref(
                    slice, cur_offset,
                    cur_offset + transport_parsing->incoming_frame_size),
                1)) {
          return 0;
        }
        cur += transport_parsing->incoming_frame_size;
        transport_parsing->incoming_stream = NULL;
        goto dts_fh_0; /* loop */
      } else {
        if (!parse_frame_slice(exec_ctx, transport_parsing,
                               gpr_slice_sub_no_ref(slice, (size_t)(cur - beg),
                                                    (size_t)(end - beg)),
                               0)) {
          return 0;
        }
        transport_parsing->incoming_frame_size -= (gpr_uint32)(end - cur);
        return 1;
      }
      GPR_UNREACHABLE_CODE(return 0);
  }

  GPR_UNREACHABLE_CODE(return 0);
}

static int init_frame_parser(grpc_exec_ctx *exec_ctx,
                             grpc_chttp2_transport_parsing *transport_parsing) {
  if (transport_parsing->expect_continuation_stream_id != 0) {
    if (transport_parsing->incoming_frame_type !=
        GRPC_CHTTP2_FRAME_CONTINUATION) {
      gpr_log(GPR_ERROR, "Expected CONTINUATION frame, got frame type %02x",
              transport_parsing->incoming_frame_type);
      return 0;
    }
    if (transport_parsing->expect_continuation_stream_id !=
        transport_parsing->incoming_stream_id) {
      gpr_log(GPR_ERROR,
              "Expected CONTINUATION frame for grpc_chttp2_stream %08x, got "
              "grpc_chttp2_stream %08x",
              transport_parsing->expect_continuation_stream_id,
              transport_parsing->incoming_stream_id);
      return 0;
    }
    return init_header_frame_parser(exec_ctx, transport_parsing, 1);
  }
  switch (transport_parsing->incoming_frame_type) {
    case GRPC_CHTTP2_FRAME_DATA:
      return init_data_frame_parser(exec_ctx, transport_parsing);
    case GRPC_CHTTP2_FRAME_HEADER:
      return init_header_frame_parser(exec_ctx, transport_parsing, 0);
    case GRPC_CHTTP2_FRAME_CONTINUATION:
      gpr_log(GPR_ERROR, "Unexpected CONTINUATION frame");
      return 0;
    case GRPC_CHTTP2_FRAME_RST_STREAM:
      return init_rst_stream_parser(exec_ctx, transport_parsing);
    case GRPC_CHTTP2_FRAME_SETTINGS:
      return init_settings_frame_parser(exec_ctx, transport_parsing);
    case GRPC_CHTTP2_FRAME_WINDOW_UPDATE:
      return init_window_update_frame_parser(exec_ctx, transport_parsing);
    case GRPC_CHTTP2_FRAME_PING:
      return init_ping_parser(exec_ctx, transport_parsing);
    case GRPC_CHTTP2_FRAME_GOAWAY:
      return init_goaway_parser(exec_ctx, transport_parsing);
    default:
      gpr_log(GPR_ERROR, "Unknown frame type %02x",
              transport_parsing->incoming_frame_type);
      return init_skip_frame_parser(exec_ctx, transport_parsing, 0);
  }
}

static grpc_chttp2_parse_error skip_parser(
    grpc_exec_ctx *exec_ctx, void *parser,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last) {
  return GRPC_CHTTP2_PARSE_OK;
}

static void skip_header(void *tp, grpc_mdelem *md) { GRPC_MDELEM_UNREF(md); }

static int init_skip_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing,
    int is_header) {
  if (is_header) {
    gpr_uint8 is_eoh = transport_parsing->expect_continuation_stream_id != 0;
    transport_parsing->parser = grpc_chttp2_header_parser_parse;
    transport_parsing->parser_data = &transport_parsing->hpack_parser;
    transport_parsing->hpack_parser.on_header = skip_header;
    transport_parsing->hpack_parser.on_header_user_data = NULL;
    transport_parsing->hpack_parser.is_boundary = is_eoh;
    transport_parsing->hpack_parser.is_eof =
        (gpr_uint8)(is_eoh ? transport_parsing->header_eof : 0);
  } else {
    transport_parsing->parser = skip_parser;
  }
  return 1;
}

void grpc_chttp2_parsing_become_skip_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing) {
  init_skip_frame_parser(
      exec_ctx, transport_parsing,
      transport_parsing->parser == grpc_chttp2_header_parser_parse);
}

static grpc_chttp2_parse_error update_incoming_window(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing) {
  gpr_uint32 incoming_frame_size = transport_parsing->incoming_frame_size;
  if (incoming_frame_size > transport_parsing->incoming_window) {
    gpr_log(GPR_ERROR, "frame of size %d overflows incoming window of %d",
            transport_parsing->incoming_frame_size,
            transport_parsing->incoming_window);
    return GRPC_CHTTP2_CONNECTION_ERROR;
  }

  if (incoming_frame_size > stream_parsing->incoming_window) {
    gpr_log(GPR_ERROR, "frame of size %d overflows incoming window of %d",
            transport_parsing->incoming_frame_size,
            stream_parsing->incoming_window);
    return GRPC_CHTTP2_CONNECTION_ERROR;
  }

  GRPC_CHTTP2_FLOW_DEBIT_TRANSPORT("parse", transport_parsing, incoming_window,
                                   incoming_frame_size);
  GRPC_CHTTP2_FLOW_DEBIT_STREAM("parse", transport_parsing, stream_parsing,
                                incoming_window, incoming_frame_size);
  stream_parsing->received_bytes += incoming_frame_size;

  grpc_chttp2_list_add_parsing_seen_stream(transport_parsing, stream_parsing);

  return GRPC_CHTTP2_PARSE_OK;
}

static int init_data_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing) {
  grpc_chttp2_stream_parsing *stream_parsing =
      grpc_chttp2_parsing_lookup_stream(transport_parsing,
                                        transport_parsing->incoming_stream_id);
  grpc_chttp2_parse_error err = GRPC_CHTTP2_PARSE_OK;
  if (!stream_parsing || stream_parsing->received_close)
    return init_skip_frame_parser(exec_ctx, transport_parsing, 0);
  if (err == GRPC_CHTTP2_PARSE_OK) {
    err = update_incoming_window(exec_ctx, transport_parsing, stream_parsing);
  }
  if (err == GRPC_CHTTP2_PARSE_OK) {
    err = grpc_chttp2_data_parser_begin_frame(
        &stream_parsing->data_parser, transport_parsing->incoming_frame_flags);
  }
  switch (err) {
    case GRPC_CHTTP2_PARSE_OK:
      transport_parsing->incoming_stream = stream_parsing;
      transport_parsing->parser = grpc_chttp2_data_parser_parse;
      transport_parsing->parser_data = &stream_parsing->data_parser;
      return 1;
    case GRPC_CHTTP2_STREAM_ERROR:
      stream_parsing->received_close = 1;
      stream_parsing->saw_rst_stream = 1;
      stream_parsing->rst_stream_reason = GRPC_CHTTP2_PROTOCOL_ERROR;
      gpr_slice_buffer_add(
          &transport_parsing->qbuf,
          grpc_chttp2_rst_stream_create(transport_parsing->incoming_stream_id,
                                        GRPC_CHTTP2_PROTOCOL_ERROR));
      return init_skip_frame_parser(exec_ctx, transport_parsing, 0);
    case GRPC_CHTTP2_CONNECTION_ERROR:
      return 0;
  }
  GPR_UNREACHABLE_CODE(return 0);
}

static void free_timeout(void *p) { gpr_free(p); }

static void on_initial_header(void *tp, grpc_mdelem *md) {
  grpc_chttp2_transport_parsing *transport_parsing = tp;
  grpc_chttp2_stream_parsing *stream_parsing =
      transport_parsing->incoming_stream;

  GPR_TIMER_BEGIN("on_initial_header", 0);

  GPR_ASSERT(stream_parsing);

  GRPC_CHTTP2_IF_TRACING(gpr_log(
      GPR_INFO, "HTTP:%d:HDR:%s: %s: %s", stream_parsing->id,
      transport_parsing->is_client ? "CLI" : "SVR",
      grpc_mdstr_as_c_string(md->key), grpc_mdstr_as_c_string(md->value)));

  if (md->key == transport_parsing->elem_grpc_status_ok->key &&
      md != transport_parsing->elem_grpc_status_ok) {
    /* TODO(ctiller): check for a status like " 0" */
    stream_parsing->seen_error = 1;
  }

  if (md->key == transport_parsing->str_grpc_timeout) {
    gpr_timespec *cached_timeout = grpc_mdelem_get_user_data(md, free_timeout);
    if (!cached_timeout) {
      /* not already parsed: parse it now, and store the result away */
      cached_timeout = gpr_malloc(sizeof(gpr_timespec));
      if (!grpc_chttp2_decode_timeout(grpc_mdstr_as_c_string(md->value),
                                      cached_timeout)) {
        gpr_log(GPR_ERROR, "Ignoring bad timeout value '%s'",
                grpc_mdstr_as_c_string(md->value));
        *cached_timeout = gpr_inf_future(GPR_CLOCK_REALTIME);
      }
      grpc_mdelem_set_user_data(md, free_timeout, cached_timeout);
    }
    grpc_chttp2_incoming_metadata_buffer_set_deadline(
        &stream_parsing->metadata_buffer[0],
        gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), *cached_timeout));
    GRPC_MDELEM_UNREF(md);
  } else {
    grpc_chttp2_incoming_metadata_buffer_add(
        &stream_parsing->metadata_buffer[0], md);
  }

  grpc_chttp2_list_add_parsing_seen_stream(transport_parsing, stream_parsing);

  GPR_TIMER_END("on_initial_header", 0);
}

static void on_trailing_header(void *tp, grpc_mdelem *md) {
  grpc_chttp2_transport_parsing *transport_parsing = tp;
  grpc_chttp2_stream_parsing *stream_parsing =
      transport_parsing->incoming_stream;

  GPR_TIMER_BEGIN("on_trailing_header", 0);

  GPR_ASSERT(stream_parsing);

  GRPC_CHTTP2_IF_TRACING(gpr_log(
      GPR_INFO, "HTTP:%d:TRL:%s: %s: %s", stream_parsing->id,
      transport_parsing->is_client ? "CLI" : "SVR",
      grpc_mdstr_as_c_string(md->key), grpc_mdstr_as_c_string(md->value)));

  if (md->key == transport_parsing->elem_grpc_status_ok->key &&
      md != transport_parsing->elem_grpc_status_ok) {
    /* TODO(ctiller): check for a status like " 0" */
    stream_parsing->seen_error = 1;
  }

  grpc_chttp2_incoming_metadata_buffer_add(&stream_parsing->metadata_buffer[1],
                                           md);

  grpc_chttp2_list_add_parsing_seen_stream(transport_parsing, stream_parsing);

  GPR_TIMER_END("on_trailing_header", 0);
}

static int init_header_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing,
    int is_continuation) {
  gpr_uint8 is_eoh = (transport_parsing->incoming_frame_flags &
                      GRPC_CHTTP2_DATA_FLAG_END_HEADERS) != 0;
  int via_accept = 0;
  grpc_chttp2_stream_parsing *stream_parsing;

  /* TODO(ctiller): when to increment header_frames_received? */

  if (is_eoh) {
    transport_parsing->expect_continuation_stream_id = 0;
  } else {
    transport_parsing->expect_continuation_stream_id =
        transport_parsing->incoming_stream_id;
  }

  if (!is_continuation) {
    transport_parsing->header_eof = (transport_parsing->incoming_frame_flags &
                                     GRPC_CHTTP2_DATA_FLAG_END_STREAM) != 0;
  }

  /* could be a new grpc_chttp2_stream or an existing grpc_chttp2_stream */
  stream_parsing = grpc_chttp2_parsing_lookup_stream(
      transport_parsing, transport_parsing->incoming_stream_id);
  if (stream_parsing == NULL) {
    if (is_continuation) {
      gpr_log(GPR_ERROR,
              "grpc_chttp2_stream disbanded before CONTINUATION received");
      return init_skip_frame_parser(exec_ctx, transport_parsing, 1);
    }
    if (transport_parsing->is_client) {
      if ((transport_parsing->incoming_stream_id & 1) &&
          transport_parsing->incoming_stream_id <
              transport_parsing->next_stream_id) {
        /* this is an old (probably cancelled) grpc_chttp2_stream */
      } else {
        gpr_log(GPR_ERROR,
                "ignoring new grpc_chttp2_stream creation on client");
      }
      return init_skip_frame_parser(exec_ctx, transport_parsing, 1);
    } else if (transport_parsing->last_incoming_stream_id >
               transport_parsing->incoming_stream_id) {
      gpr_log(GPR_ERROR,
              "ignoring out of order new grpc_chttp2_stream request on server; "
              "last grpc_chttp2_stream "
              "id=%d, new grpc_chttp2_stream id=%d",
              transport_parsing->last_incoming_stream_id,
              transport_parsing->incoming_stream_id);
      return init_skip_frame_parser(exec_ctx, transport_parsing, 1);
    } else if ((transport_parsing->incoming_stream_id & 1) == 0) {
      gpr_log(GPR_ERROR,
              "ignoring grpc_chttp2_stream with non-client generated index %d",
              transport_parsing->incoming_stream_id);
      return init_skip_frame_parser(exec_ctx, transport_parsing, 1);
    }
    stream_parsing = transport_parsing->incoming_stream =
        grpc_chttp2_parsing_accept_stream(
            exec_ctx, transport_parsing, transport_parsing->incoming_stream_id);
    if (stream_parsing == NULL) {
      gpr_log(GPR_ERROR, "grpc_chttp2_stream not accepted");
      return init_skip_frame_parser(exec_ctx, transport_parsing, 1);
    }
    via_accept = 1;
  } else {
    transport_parsing->incoming_stream = stream_parsing;
  }
  GPR_ASSERT(stream_parsing != NULL && (via_accept == 0 || via_accept == 1));
  if (stream_parsing->received_close) {
    gpr_log(GPR_ERROR, "skipping already closed grpc_chttp2_stream header");
    transport_parsing->incoming_stream = NULL;
    return init_skip_frame_parser(exec_ctx, transport_parsing, 1);
  }
  transport_parsing->parser = grpc_chttp2_header_parser_parse;
  transport_parsing->parser_data = &transport_parsing->hpack_parser;
  switch (stream_parsing->header_frames_received) {
    case 0:
      transport_parsing->hpack_parser.on_header = on_initial_header;
      break;
    case 1:
      transport_parsing->hpack_parser.on_header = on_trailing_header;
      break;
    case 2:
      gpr_log(GPR_ERROR, "too many header frames received");
      return init_skip_frame_parser(exec_ctx, transport_parsing, 1);
  }
  transport_parsing->hpack_parser.on_header_user_data = transport_parsing;
  transport_parsing->hpack_parser.is_boundary = is_eoh;
  transport_parsing->hpack_parser.is_eof =
      (gpr_uint8)(is_eoh ? transport_parsing->header_eof : 0);
  if (!is_continuation && (transport_parsing->incoming_frame_flags &
                           GRPC_CHTTP2_FLAG_HAS_PRIORITY)) {
    grpc_chttp2_hpack_parser_set_has_priority(&transport_parsing->hpack_parser);
  }
  return 1;
}

static int init_window_update_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing) {
  int ok = GRPC_CHTTP2_PARSE_OK == grpc_chttp2_window_update_parser_begin_frame(
                                       &transport_parsing->simple.window_update,
                                       transport_parsing->incoming_frame_size,
                                       transport_parsing->incoming_frame_flags);
  if (transport_parsing->incoming_stream_id) {
    transport_parsing->incoming_stream = grpc_chttp2_parsing_lookup_stream(
        transport_parsing, transport_parsing->incoming_stream_id);
  }
  transport_parsing->parser = grpc_chttp2_window_update_parser_parse;
  transport_parsing->parser_data = &transport_parsing->simple.window_update;
  return ok;
}

static int init_ping_parser(grpc_exec_ctx *exec_ctx,
                            grpc_chttp2_transport_parsing *transport_parsing) {
  int ok = GRPC_CHTTP2_PARSE_OK == grpc_chttp2_ping_parser_begin_frame(
                                       &transport_parsing->simple.ping,
                                       transport_parsing->incoming_frame_size,
                                       transport_parsing->incoming_frame_flags);
  transport_parsing->parser = grpc_chttp2_ping_parser_parse;
  transport_parsing->parser_data = &transport_parsing->simple.ping;
  return ok;
}

static int init_rst_stream_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing) {
  int ok = GRPC_CHTTP2_PARSE_OK == grpc_chttp2_rst_stream_parser_begin_frame(
                                       &transport_parsing->simple.rst_stream,
                                       transport_parsing->incoming_frame_size,
                                       transport_parsing->incoming_frame_flags);
  transport_parsing->incoming_stream = grpc_chttp2_parsing_lookup_stream(
      transport_parsing, transport_parsing->incoming_stream_id);
  if (!transport_parsing->incoming_stream) {
    return init_skip_frame_parser(exec_ctx, transport_parsing, 0);
  }
  transport_parsing->parser = grpc_chttp2_rst_stream_parser_parse;
  transport_parsing->parser_data = &transport_parsing->simple.rst_stream;
  return ok;
}

static int init_goaway_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing) {
  int ok = GRPC_CHTTP2_PARSE_OK == grpc_chttp2_goaway_parser_begin_frame(
                                       &transport_parsing->goaway_parser,
                                       transport_parsing->incoming_frame_size,
                                       transport_parsing->incoming_frame_flags);
  transport_parsing->parser = grpc_chttp2_goaway_parser_parse;
  transport_parsing->parser_data = &transport_parsing->goaway_parser;
  return ok;
}

static int init_settings_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing) {
  int ok;

  if (transport_parsing->incoming_stream_id != 0) {
    gpr_log(GPR_ERROR, "settings frame received for grpc_chttp2_stream %d",
            transport_parsing->incoming_stream_id);
    return 0;
  }

  ok = GRPC_CHTTP2_PARSE_OK == grpc_chttp2_settings_parser_begin_frame(
                                   &transport_parsing->simple.settings,
                                   transport_parsing->incoming_frame_size,
                                   transport_parsing->incoming_frame_flags,
                                   transport_parsing->settings);
  if (!ok) {
    return 0;
  }
  if (transport_parsing->incoming_frame_flags & GRPC_CHTTP2_FLAG_ACK) {
    transport_parsing->settings_ack_received = 1;
    grpc_chttp2_hptbl_set_max_bytes(
        &transport_parsing->hpack_parser.table,
        transport_parsing->last_sent_max_table_size);
  }
  transport_parsing->parser = grpc_chttp2_settings_parser_parse;
  transport_parsing->parser_data = &transport_parsing->simple.settings;
  return ok;
}

/*
static int is_window_update_legal(gpr_int64 window_update, gpr_int64 window) {
  return window + window_update < MAX_WINDOW;
}
*/

static int parse_frame_slice(grpc_exec_ctx *exec_ctx,
                             grpc_chttp2_transport_parsing *transport_parsing,
                             gpr_slice slice, int is_last) {
  grpc_chttp2_stream_parsing *stream_parsing =
      transport_parsing->incoming_stream;
  switch (transport_parsing->parser(exec_ctx, transport_parsing->parser_data,
                                    transport_parsing, stream_parsing, slice,
                                    is_last)) {
    case GRPC_CHTTP2_PARSE_OK:
      if (stream_parsing) {
        grpc_chttp2_list_add_parsing_seen_stream(transport_parsing,
                                                 stream_parsing);
      }
      return 1;
    case GRPC_CHTTP2_STREAM_ERROR:
      grpc_chttp2_parsing_become_skip_parser(exec_ctx, transport_parsing);
      if (stream_parsing) {
        stream_parsing->saw_rst_stream = 1;
        stream_parsing->rst_stream_reason = GRPC_CHTTP2_PROTOCOL_ERROR;
        gpr_slice_buffer_add(
            &transport_parsing->qbuf,
            grpc_chttp2_rst_stream_create(transport_parsing->incoming_stream_id,
                                          GRPC_CHTTP2_PROTOCOL_ERROR));
      }
      return 1;
    case GRPC_CHTTP2_CONNECTION_ERROR:
      return 0;
  }
  GPR_UNREACHABLE_CODE(return 0);
}
