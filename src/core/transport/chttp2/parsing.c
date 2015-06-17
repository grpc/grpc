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

#include "src/core/transport/chttp2/http2_errors.h"
#include "src/core/transport/chttp2/timeout_encoding.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

static int init_frame_parser(grpc_chttp2_transport_parsing *transport_parsing);
static int init_header_frame_parser(
    grpc_chttp2_transport_parsing *transport_parsing, int is_continuation);
static int init_data_frame_parser(
    grpc_chttp2_transport_parsing *transport_parsing);
static int init_rst_stream_parser(
    grpc_chttp2_transport_parsing *transport_parsing);
static int init_settings_frame_parser(
    grpc_chttp2_transport_parsing *transport_parsing);
static int init_window_update_frame_parser(
    grpc_chttp2_transport_parsing *transport_parsing);
static int init_ping_parser(grpc_chttp2_transport_parsing *transport_parsing);
static int init_goaway_parser(grpc_chttp2_transport_parsing *transport_parsing);
static int init_skip_frame_parser(
    grpc_chttp2_transport_parsing *transport_parsing, int is_header);

static int parse_frame_slice(grpc_chttp2_transport_parsing *transport_parsing,
                             gpr_slice slice, int is_last);

void grpc_chttp2_prepare_to_read(grpc_chttp2_transport_global *transport_global,
                                 grpc_chttp2_transport_parsing *transport_parsing) {
  grpc_chttp2_stream_global *stream_global;
  grpc_chttp2_stream_parsing *stream_parsing;

  /* update the parsing view of incoming window */
  transport_parsing->incoming_window = transport_global->incoming_window;
  while (grpc_chttp2_list_pop_incoming_window_updated(
      transport_global, transport_parsing, &stream_global, &stream_parsing)) {
    stream_parsing->incoming_window = transport_parsing->incoming_window;
  }
}

void grpc_chttp2_publish_reads(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_parsing *transport_parsing) {
  grpc_chttp2_stream_global *stream_global;
  grpc_chttp2_stream_parsing *stream_parsing;

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

  /* TODO(ctiller): re-implement */
  GPR_ASSERT(transport_parsing->initial_window_update == 0);

#if 0
  while ((s = stream_list_remove_head(t, FINISHED_READ_OP)) != NULL) {
    int publish = 0;
    GPR_ASSERT(s->incoming_sopb);
    *s->publish_state =
        compute_state(s->write_state == WRITE_STATE_SENT_CLOSE, s->read_closed);
    if (*s->publish_state != s->published_state) {
      s->published_state = *s->publish_state;
      publish = 1;
      if (s->published_state == GRPC_STREAM_CLOSED) {
        remove_from_stream_map(t, s);
      }
    }
    if (s->parser.incoming_sopb.nops > 0) {
      grpc_sopb_swap(s->incoming_sopb, &s->parser.incoming_sopb);
      publish = 1;
    }
    if (publish) {
      if (s->incoming_metadata_count > 0) {
        patch_metadata_ops(s);
      }
      s->incoming_sopb = NULL;
      schedule_cb(t, s->global.recv_done_closure, 1);
    }
  }
#endif

  /* copy parsing qbuf to global qbuf */
  gpr_slice_buffer_move_into(&transport_parsing->qbuf, &transport_global->qbuf);

  /* update global settings */
  if (transport_parsing->settings_updated) {
    memcpy(transport_global->settings[PEER_SETTINGS],
           transport_parsing->settings, sizeof(transport_parsing->settings));
    transport_parsing->settings_updated = 0;
  }

  /* update settings based on ack if received */
  if (transport_parsing->settings_ack_received) {
    memcpy(transport_global->settings[ACKED_SETTINGS],
           transport_global->settings[SENT_SETTINGS],
           GRPC_CHTTP2_NUM_SETTINGS * sizeof(gpr_uint32));
    transport_parsing->settings_ack_received = 0;
  }

  /* move goaway to the global state if we received one (it will be
     published later */
  if (transport_parsing->goaway_received) {
    grpc_chttp2_add_incoming_goaway(transport_global,
                                    transport_parsing->goaway_error,
                                    transport_parsing->goaway_text);
    transport_parsing->goaway_received = 0;
  }

  /* for each stream that saw an update, fixup global state */
  while (grpc_chttp2_list_pop_parsing_seen_stream(
      transport_global, transport_parsing, &stream_global, &stream_parsing)) {
    /* update incoming flow control window */
    if (stream_parsing->incoming_window_delta) {
      stream_global->incoming_window -= stream_parsing->incoming_window_delta;
      stream_parsing->incoming_window_delta = 0;
      grpc_chttp2_list_add_writable_window_update_stream(transport_global,
                                                         stream_global);
    }

    /* update outgoing flow control window */
    if (stream_parsing->outgoing_window_update) {
      int was_zero = stream_global->outgoing_window <= 0;
      int is_zero;
      stream_global->outgoing_window += stream_parsing->outgoing_window_update;
      stream_parsing->outgoing_window_update = 0;
      is_zero = stream_global->outgoing_window <= 0;
      if (was_zero && !is_zero) {
        grpc_chttp2_list_add_writable_stream(transport_global, stream_global);
      }
    }

    /* updating closed status */
    if (stream_parsing->received_close) {
      stream_global->read_closed = 1;
      grpc_chttp2_list_add_read_write_state_changed(transport_global,
                                                    stream_global);
    }
    if (stream_parsing->saw_rst_stream) {
      stream_global->cancelled = 1;
      grpc_chttp2_list_add_read_write_state_changed(transport_global,
                                                    stream_global);
    }

    /* publish incoming stream ops */
    if (stream_parsing->data_parser.incoming_sopb.nops > 0) {
      grpc_incoming_metadata_buffer_move_to_referencing_sopb(&stream_parsing->incoming_metadata, &stream_global->incoming_metadata, &stream_parsing->data_parser.incoming_sopb);
      grpc_sopb_move_to(&stream_parsing->data_parser.incoming_sopb, &stream_global->incoming_sopb);
      grpc_chttp2_list_add_read_write_state_changed(transport_global,
                                                    stream_global);
    }
  }
}

int grpc_chttp2_perform_read(grpc_chttp2_transport_parsing *transport_parsing,
                             gpr_slice slice) {
  gpr_uint8 *beg = GPR_SLICE_START_PTR(slice);
  gpr_uint8 *end = GPR_SLICE_END_PTR(slice);
  gpr_uint8 *cur = beg;

  if (cur == end) return 1;

  switch (transport_parsing->deframe_state) {
    case DTS_CLIENT_PREFIX_0:
    case DTS_CLIENT_PREFIX_1:
    case DTS_CLIENT_PREFIX_2:
    case DTS_CLIENT_PREFIX_3:
    case DTS_CLIENT_PREFIX_4:
    case DTS_CLIENT_PREFIX_5:
    case DTS_CLIENT_PREFIX_6:
    case DTS_CLIENT_PREFIX_7:
    case DTS_CLIENT_PREFIX_8:
    case DTS_CLIENT_PREFIX_9:
    case DTS_CLIENT_PREFIX_10:
    case DTS_CLIENT_PREFIX_11:
    case DTS_CLIENT_PREFIX_12:
    case DTS_CLIENT_PREFIX_13:
    case DTS_CLIENT_PREFIX_14:
    case DTS_CLIENT_PREFIX_15:
    case DTS_CLIENT_PREFIX_16:
    case DTS_CLIENT_PREFIX_17:
    case DTS_CLIENT_PREFIX_18:
    case DTS_CLIENT_PREFIX_19:
    case DTS_CLIENT_PREFIX_20:
    case DTS_CLIENT_PREFIX_21:
    case DTS_CLIENT_PREFIX_22:
    case DTS_CLIENT_PREFIX_23:
      while (cur != end && transport_parsing->deframe_state != DTS_FH_0) {
        if (*cur != GRPC_CHTTP2_CLIENT_CONNECT_STRING[transport_parsing
                                                          ->deframe_state]) {
          gpr_log(GPR_ERROR,
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
    case DTS_FH_0:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_frame_size = ((gpr_uint32)*cur) << 16;
      if (++cur == end) {
        transport_parsing->deframe_state = DTS_FH_1;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_1:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_frame_size |= ((gpr_uint32)*cur) << 8;
      if (++cur == end) {
        transport_parsing->deframe_state = DTS_FH_2;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_2:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_frame_size |= *cur;
      if (++cur == end) {
        transport_parsing->deframe_state = DTS_FH_3;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_3:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_frame_type = *cur;
      if (++cur == end) {
        transport_parsing->deframe_state = DTS_FH_4;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_4:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_frame_flags = *cur;
      if (++cur == end) {
        transport_parsing->deframe_state = DTS_FH_5;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_5:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_stream_id = (((gpr_uint32)*cur) & 0x7f) << 24;
      if (++cur == end) {
        transport_parsing->deframe_state = DTS_FH_6;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_6:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_stream_id |= ((gpr_uint32)*cur) << 16;
      if (++cur == end) {
        transport_parsing->deframe_state = DTS_FH_7;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_7:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_stream_id |= ((gpr_uint32)*cur) << 8;
      if (++cur == end) {
        transport_parsing->deframe_state = DTS_FH_8;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_8:
      GPR_ASSERT(cur < end);
      transport_parsing->incoming_stream_id |= ((gpr_uint32)*cur);
      transport_parsing->deframe_state = DTS_FRAME;
      if (!init_frame_parser(transport_parsing)) {
        return 0;
      }
      if (transport_parsing->incoming_stream_id) {
        transport_parsing->last_incoming_stream_id =
            transport_parsing->incoming_stream_id;
      }
      if (transport_parsing->incoming_frame_size == 0) {
        if (!parse_frame_slice(transport_parsing, gpr_empty_slice(), 1)) {
          return 0;
        }
        if (++cur == end) {
          transport_parsing->deframe_state = DTS_FH_0;
          return 1;
        }
        goto dts_fh_0; /* loop */
      }
      if (++cur == end) {
        return 1;
      }
    /* fallthrough */
    case DTS_FRAME:
      GPR_ASSERT(cur < end);
      if ((gpr_uint32)(end - cur) == transport_parsing->incoming_frame_size) {
        if (!parse_frame_slice(
                transport_parsing,
                gpr_slice_sub_no_ref(slice, cur - beg, end - beg), 1)) {
          return 0;
        }
        transport_parsing->deframe_state = DTS_FH_0;
        return 1;
      } else if ((gpr_uint32)(end - cur) >
                 transport_parsing->incoming_frame_size) {
        if (!parse_frame_slice(
                transport_parsing,
                gpr_slice_sub_no_ref(
                    slice, cur - beg,
                    cur + transport_parsing->incoming_frame_size - beg),
                1)) {
          return 0;
        }
        cur += transport_parsing->incoming_frame_size;
        goto dts_fh_0; /* loop */
      } else {
        if (!parse_frame_slice(
                transport_parsing,
                gpr_slice_sub_no_ref(slice, cur - beg, end - beg), 0)) {
          return 0;
        }
        transport_parsing->incoming_frame_size -= (end - cur);
        return 1;
      }
      gpr_log(GPR_ERROR, "should never reach here");
      abort();
  }

  gpr_log(GPR_ERROR, "should never reach here");
  abort();

  return 0;
}

static int init_frame_parser(grpc_chttp2_transport_parsing *transport_parsing) {
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
    return init_header_frame_parser(transport_parsing, 1);
  }
  switch (transport_parsing->incoming_frame_type) {
    case GRPC_CHTTP2_FRAME_DATA:
      return init_data_frame_parser(transport_parsing);
    case GRPC_CHTTP2_FRAME_HEADER:
      return init_header_frame_parser(transport_parsing, 0);
    case GRPC_CHTTP2_FRAME_CONTINUATION:
      gpr_log(GPR_ERROR, "Unexpected CONTINUATION frame");
      return 0;
    case GRPC_CHTTP2_FRAME_RST_STREAM:
      return init_rst_stream_parser(transport_parsing);
    case GRPC_CHTTP2_FRAME_SETTINGS:
      return init_settings_frame_parser(transport_parsing);
    case GRPC_CHTTP2_FRAME_WINDOW_UPDATE:
      return init_window_update_frame_parser(transport_parsing);
    case GRPC_CHTTP2_FRAME_PING:
      return init_ping_parser(transport_parsing);
    case GRPC_CHTTP2_FRAME_GOAWAY:
      return init_goaway_parser(transport_parsing);
    default:
      gpr_log(GPR_ERROR, "Unknown frame type %02x",
              transport_parsing->incoming_frame_type);
      return init_skip_frame_parser(transport_parsing, 0);
  }
}

static grpc_chttp2_parse_error skip_parser(
    void *parser, grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last) {
  return GRPC_CHTTP2_PARSE_OK;
}

static void skip_header(void *tp, grpc_mdelem *md) { grpc_mdelem_unref(md); }

static int init_skip_frame_parser(
    grpc_chttp2_transport_parsing *transport_parsing, int is_header) {
  if (is_header) {
    int is_eoh = transport_parsing->expect_continuation_stream_id != 0;
    transport_parsing->parser = grpc_chttp2_header_parser_parse;
    transport_parsing->parser_data = &transport_parsing->hpack_parser;
    transport_parsing->hpack_parser.on_header = skip_header;
    transport_parsing->hpack_parser.on_header_user_data = NULL;
    transport_parsing->hpack_parser.is_boundary = is_eoh;
    transport_parsing->hpack_parser.is_eof =
        is_eoh ? transport_parsing->header_eof : 0;
  } else {
    transport_parsing->parser = skip_parser;
  }
  return 1;
}

static void become_skip_parser(
    grpc_chttp2_transport_parsing *transport_parsing) {
  init_skip_frame_parser(
      transport_parsing,
      transport_parsing->parser == grpc_chttp2_header_parser_parse);
}

static grpc_chttp2_parse_error update_incoming_window(
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing) {
  if (transport_parsing->incoming_frame_size >
      transport_parsing->incoming_window) {
    gpr_log(GPR_ERROR, "frame of size %d overflows incoming window of %d",
            transport_parsing->incoming_frame_size,
            transport_parsing->incoming_window);
    return GRPC_CHTTP2_CONNECTION_ERROR;
  }

  if (transport_parsing->incoming_frame_size >
      stream_parsing->incoming_window) {
    gpr_log(GPR_ERROR, "frame of size %d overflows incoming window of %d",
            transport_parsing->incoming_frame_size,
            stream_parsing->incoming_window);
    return GRPC_CHTTP2_CONNECTION_ERROR;
  }

  transport_parsing->incoming_window -= transport_parsing->incoming_frame_size;
  stream_parsing->incoming_window -= transport_parsing->incoming_frame_size;
  stream_parsing->incoming_window_delta +=
      transport_parsing->incoming_frame_size;
  grpc_chttp2_list_add_parsing_seen_stream(transport_parsing, stream_parsing);

  return GRPC_CHTTP2_PARSE_OK;
}

static int init_data_frame_parser(
    grpc_chttp2_transport_parsing *transport_parsing) {
  grpc_chttp2_stream_parsing *stream_parsing =
      grpc_chttp2_parsing_lookup_stream(transport_parsing,
                                        transport_parsing->incoming_stream_id);
  grpc_chttp2_parse_error err = GRPC_CHTTP2_PARSE_OK;
  if (!stream_parsing || stream_parsing->received_close)
    return init_skip_frame_parser(transport_parsing, 0);
  if (err == GRPC_CHTTP2_PARSE_OK) {
    err = update_incoming_window(transport_parsing, stream_parsing);
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
      return init_skip_frame_parser(transport_parsing, 0);
    case GRPC_CHTTP2_CONNECTION_ERROR:
      return 0;
  }
  gpr_log(GPR_ERROR, "should never reach here");
  abort();
  return 0;
}

static void free_timeout(void *p) { gpr_free(p); }

static void on_header(void *tp, grpc_mdelem *md) {
  grpc_chttp2_transport_parsing *transport_parsing = tp;
  grpc_chttp2_stream_parsing *stream_parsing =
      transport_parsing->incoming_stream;

  GPR_ASSERT(stream_parsing);

  IF_TRACING(gpr_log(GPR_INFO, "HTTP:%d:HDR: %s: %s", stream_parsing->id,
                     transport_parsing->is_client ? "CLI" : "SVR",
                     grpc_mdstr_as_c_string(md->key),
                     grpc_mdstr_as_c_string(md->value)));

  if (md->key == transport_parsing->str_grpc_timeout) {
    gpr_timespec *cached_timeout = grpc_mdelem_get_user_data(md, free_timeout);
    if (!cached_timeout) {
      /* not already parsed: parse it now, and store the result away */
      cached_timeout = gpr_malloc(sizeof(gpr_timespec));
      if (!grpc_chttp2_decode_timeout(grpc_mdstr_as_c_string(md->value),
                                      cached_timeout)) {
        gpr_log(GPR_ERROR, "Ignoring bad timeout value '%s'",
                grpc_mdstr_as_c_string(md->value));
        *cached_timeout = gpr_inf_future;
      }
      grpc_mdelem_set_user_data(md, free_timeout, cached_timeout);
    }
    grpc_chttp2_incoming_metadata_buffer_set_deadline(
        &stream_parsing->incoming_metadata,
        gpr_time_add(gpr_now(), *cached_timeout));
    grpc_mdelem_unref(md);
  } else {
    grpc_chttp2_incoming_metadata_buffer_add(&stream_parsing->incoming_metadata,
                                             md);
  }

  grpc_chttp2_list_add_parsing_seen_stream(transport_parsing, stream_parsing);
}

static int init_header_frame_parser(
    grpc_chttp2_transport_parsing *transport_parsing, int is_continuation) {
  int is_eoh = (transport_parsing->incoming_frame_flags &
                GRPC_CHTTP2_DATA_FLAG_END_HEADERS) != 0;
  grpc_chttp2_stream_parsing *stream_parsing;

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
  if (!stream_parsing) {
    if (is_continuation) {
      gpr_log(GPR_ERROR,
              "grpc_chttp2_stream disbanded before CONTINUATION received");
      return init_skip_frame_parser(transport_parsing, 1);
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
      return init_skip_frame_parser(transport_parsing, 1);
    } else if (transport_parsing->last_incoming_stream_id >
               transport_parsing->incoming_stream_id) {
      gpr_log(GPR_ERROR,
              "ignoring out of order new grpc_chttp2_stream request on server; "
              "last grpc_chttp2_stream "
              "id=%d, new grpc_chttp2_stream id=%d",
              transport_parsing->last_incoming_stream_id,
              transport_parsing->incoming_stream_id);
      return init_skip_frame_parser(transport_parsing, 1);
    } else if ((transport_parsing->incoming_stream_id & 1) == 0) {
      gpr_log(GPR_ERROR,
              "ignoring grpc_chttp2_stream with non-client generated index %d",
              transport_parsing->incoming_stream_id);
      return init_skip_frame_parser(transport_parsing, 1);
    }
    stream_parsing = transport_parsing->incoming_stream =
        grpc_chttp2_parsing_accept_stream(
            transport_parsing, transport_parsing->incoming_stream_id);
    if (!stream_parsing) {
      gpr_log(GPR_ERROR, "grpc_chttp2_stream not accepted");
      return init_skip_frame_parser(transport_parsing, 1);
    }
  } else {
    transport_parsing->incoming_stream = stream_parsing;
  }
  if (stream_parsing->received_close) {
    gpr_log(GPR_ERROR, "skipping already closed grpc_chttp2_stream header");
    transport_parsing->incoming_stream = NULL;
    return init_skip_frame_parser(transport_parsing, 1);
  }
  transport_parsing->parser = grpc_chttp2_header_parser_parse;
  transport_parsing->parser_data = &transport_parsing->hpack_parser;
  transport_parsing->hpack_parser.on_header = on_header;
  transport_parsing->hpack_parser.on_header_user_data = transport_parsing;
  transport_parsing->hpack_parser.is_boundary = is_eoh;
  transport_parsing->hpack_parser.is_eof =
      is_eoh ? transport_parsing->header_eof : 0;
  if (!is_continuation && (transport_parsing->incoming_frame_flags &
                           GRPC_CHTTP2_FLAG_HAS_PRIORITY)) {
    grpc_chttp2_hpack_parser_set_has_priority(&transport_parsing->hpack_parser);
  }
  return 1;
}

static int init_window_update_frame_parser(
    grpc_chttp2_transport_parsing *transport_parsing) {
  int ok = GRPC_CHTTP2_PARSE_OK == grpc_chttp2_window_update_parser_begin_frame(
                                       &transport_parsing->simple.window_update,
                                       transport_parsing->incoming_frame_size,
                                       transport_parsing->incoming_frame_flags);
  transport_parsing->parser = grpc_chttp2_window_update_parser_parse;
  transport_parsing->parser_data = &transport_parsing->simple.window_update;
  return ok;
}

static int init_ping_parser(grpc_chttp2_transport_parsing *transport_parsing) {
  int ok = GRPC_CHTTP2_PARSE_OK == grpc_chttp2_ping_parser_begin_frame(
                                       &transport_parsing->simple.ping,
                                       transport_parsing->incoming_frame_size,
                                       transport_parsing->incoming_frame_flags);
  transport_parsing->parser = grpc_chttp2_ping_parser_parse;
  transport_parsing->parser_data = &transport_parsing->simple.ping;
  return ok;
}

static int init_rst_stream_parser(
    grpc_chttp2_transport_parsing *transport_parsing) {
  int ok = GRPC_CHTTP2_PARSE_OK == grpc_chttp2_rst_stream_parser_begin_frame(
                                       &transport_parsing->simple.rst_stream,
                                       transport_parsing->incoming_frame_size,
                                       transport_parsing->incoming_frame_flags);
  transport_parsing->parser = grpc_chttp2_rst_stream_parser_parse;
  transport_parsing->parser_data = &transport_parsing->simple.rst_stream;
  return ok;
}

static int init_goaway_parser(
    grpc_chttp2_transport_parsing *transport_parsing) {
  int ok = GRPC_CHTTP2_PARSE_OK == grpc_chttp2_goaway_parser_begin_frame(
                                       &transport_parsing->goaway_parser,
                                       transport_parsing->incoming_frame_size,
                                       transport_parsing->incoming_frame_flags);
  transport_parsing->parser = grpc_chttp2_goaway_parser_parse;
  transport_parsing->parser_data = &transport_parsing->goaway_parser;
  return ok;
}

static int init_settings_frame_parser(
    grpc_chttp2_transport_parsing *transport_parsing) {
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
  } else {
    transport_parsing->settings_updated = 1;
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

static int parse_frame_slice(grpc_chttp2_transport_parsing *transport_parsing,
                             gpr_slice slice, int is_last) {
  grpc_chttp2_stream_parsing *stream_parsing =
      transport_parsing->incoming_stream;
  switch (transport_parsing->parser(transport_parsing->parser_data,
                                    transport_parsing, stream_parsing, slice,
                                    is_last)) {
    case GRPC_CHTTP2_PARSE_OK:
      if (stream_parsing) {
        grpc_chttp2_list_add_parsing_seen_stream(transport_parsing,
                                                 stream_parsing);
      }
      return 1;
    case GRPC_CHTTP2_STREAM_ERROR:
      become_skip_parser(transport_parsing);
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
  gpr_log(GPR_ERROR, "should never reach here");
  abort();
  return 0;
}

#if 0
      if (st.end_of_stream) {
        transport_parsing->incoming_stream->read_closed = 1;
        maybe_finish_read(t, transport_parsing->incoming_stream, 1);
      }
      if (st.need_flush_reads) {
        maybe_finish_read(t, transport_parsing->incoming_stream, 1);
      }
      if (st.metadata_boundary) {
        add_metadata_batch(t, transport_parsing->incoming_stream);
        maybe_finish_read(t, transport_parsing->incoming_stream, 1);
      }
      if (st.ack_settings) {
      }
      if (st.send_ping_ack) {
      }
      if (st.goaway) {
        add_goaway(t, st.goaway_error, st.goaway_text);
      }
      if (st.rst_stream) {
        cancel_stream_id(
            t, transport_parsing->incoming_stream_id,
            grpc_chttp2_http2_error_to_grpc_status(st.rst_stream_reason),
            st.rst_stream_reason, 0);
      }
      if (st.process_ping_reply) {
        for (i = 0; i < transport_parsing->ping_count; i++) {
          if (0 ==
              memcmp(transport_parsing->pings[i].id, transport_parsing->simple.ping.opaque_8bytes, 8)) {
            transport_parsing->pings[i].cb(transport_parsing->pings[i].user_data);
            memmove(&transport_parsing->pings[i], &transport_parsing->pings[i + 1],
                    (transport_parsing->ping_count - i - 1) * sizeof(grpc_chttp2_outstanding_ping));
            transport_parsing->ping_count--;
            break;
          }
        }
      }
      if (st.initial_window_update) {
        for (i = 0; i < transport_parsing->stream_map.count; i++) {
          grpc_chttp2_stream *s = (grpc_chttp2_stream *)(transport_parsing->stream_map.values[i]);
          s->global.outgoing_window_update += st.initial_window_update;
          stream_list_join(t, s, NEW_OUTGOING_WINDOW);
        }
      }
      if (st.window_update) {
        if (transport_parsing->incoming_stream_id) {
          /* if there was a grpc_chttp2_stream id, this is for some grpc_chttp2_stream */
          grpc_chttp2_stream *s = lookup_stream(t, transport_parsing->incoming_stream_id);
          if (s) {
            s->global.outgoing_window_update += st.window_update;
            stream_list_join(t, s, NEW_OUTGOING_WINDOW);
          }
        } else {
          /* grpc_chttp2_transport level window update */
            transport_parsing->global.outgoing_window_update += st.window_update;
        }
      }
#endif
