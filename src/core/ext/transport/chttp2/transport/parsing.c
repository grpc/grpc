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

#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/http2_errors.h"
#include "src/core/ext/transport/chttp2/transport/status_conversion.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/timeout_encoding.h"

static grpc_error *init_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global);
static grpc_error *init_header_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    int is_continuation);
static grpc_error *init_data_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global);
static grpc_error *init_rst_stream_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global);
static grpc_error *init_settings_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global);
static grpc_error *init_window_update_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global);
static grpc_error *init_ping_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global);
static grpc_error *init_goaway_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global);
static grpc_error *init_skip_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    int is_header);

static grpc_error *parse_frame_slice(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    gpr_slice slice, int is_last);

grpc_error *grpc_chttp2_perform_read(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    gpr_slice slice) {
  uint8_t *beg = GPR_SLICE_START_PTR(slice);
  uint8_t *end = GPR_SLICE_END_PTR(slice);
  uint8_t *cur = beg;
  grpc_error *err;

  if (cur == end) return GRPC_ERROR_NONE;

  switch (transport_global->deframe_state) {
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
      while (cur != end && transport_global->deframe_state != GRPC_DTS_FH_0) {
        if (*cur != GRPC_CHTTP2_CLIENT_CONNECT_STRING[transport_global
                                                          ->deframe_state]) {
          char *msg;
          gpr_asprintf(
              &msg,
              "Connect string mismatch: expected '%c' (%d) got '%c' (%d) "
              "at byte %d",
              GRPC_CHTTP2_CLIENT_CONNECT_STRING[transport_global
                                                    ->deframe_state],
              (int)(uint8_t)GRPC_CHTTP2_CLIENT_CONNECT_STRING
                  [transport_global->deframe_state],
              *cur, (int)*cur, transport_global->deframe_state);
          err = GRPC_ERROR_CREATE(msg);
          gpr_free(msg);
          return err;
        }
        ++cur;
        ++transport_global->deframe_state;
      }
      if (cur == end) {
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    dts_fh_0:
    case GRPC_DTS_FH_0:
      GPR_ASSERT(cur < end);
      transport_global->incoming_frame_size = ((uint32_t)*cur) << 16;
      if (++cur == end) {
        transport_global->deframe_state = GRPC_DTS_FH_1;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_1:
      GPR_ASSERT(cur < end);
      transport_global->incoming_frame_size |= ((uint32_t)*cur) << 8;
      if (++cur == end) {
        transport_global->deframe_state = GRPC_DTS_FH_2;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_2:
      GPR_ASSERT(cur < end);
      transport_global->incoming_frame_size |= *cur;
      if (++cur == end) {
        transport_global->deframe_state = GRPC_DTS_FH_3;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_3:
      GPR_ASSERT(cur < end);
      transport_global->incoming_frame_type = *cur;
      if (++cur == end) {
        transport_global->deframe_state = GRPC_DTS_FH_4;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_4:
      GPR_ASSERT(cur < end);
      transport_global->incoming_frame_flags = *cur;
      if (++cur == end) {
        transport_global->deframe_state = GRPC_DTS_FH_5;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_5:
      GPR_ASSERT(cur < end);
      transport_global->incoming_stream_id = (((uint32_t)*cur) & 0x7f) << 24;
      if (++cur == end) {
        transport_global->deframe_state = GRPC_DTS_FH_6;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_6:
      GPR_ASSERT(cur < end);
      transport_global->incoming_stream_id |= ((uint32_t)*cur) << 16;
      if (++cur == end) {
        transport_global->deframe_state = GRPC_DTS_FH_7;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_7:
      GPR_ASSERT(cur < end);
      transport_global->incoming_stream_id |= ((uint32_t)*cur) << 8;
      if (++cur == end) {
        transport_global->deframe_state = GRPC_DTS_FH_8;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_8:
      GPR_ASSERT(cur < end);
      transport_global->incoming_stream_id |= ((uint32_t)*cur);
      transport_global->deframe_state = GRPC_DTS_FRAME;
      err = init_frame_parser(exec_ctx, transport_global);
      if (err != GRPC_ERROR_NONE) {
        return err;
      }
      if (transport_global->incoming_stream_id != 0 &&
          transport_global->incoming_stream_id >
              transport_global->last_incoming_stream_id) {
        transport_global->last_incoming_stream_id =
            transport_global->incoming_stream_id;
      }
      if (transport_global->incoming_frame_size == 0) {
        err =
            parse_frame_slice(exec_ctx, transport_global, gpr_empty_slice(), 1);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        transport_global->incoming_stream = NULL;
        if (++cur == end) {
          transport_global->deframe_state = GRPC_DTS_FH_0;
          return GRPC_ERROR_NONE;
        }
        goto dts_fh_0; /* loop */
      } else if (transport_global->incoming_frame_size >
                 transport_global
                     ->settings[GRPC_ACKED_SETTINGS]
                               [GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE]) {
        char *msg;
        gpr_asprintf(
            &msg, "Frame size %d is larger than max frame size %d",
            transport_global->incoming_frame_size,
            transport_global->settings[GRPC_ACKED_SETTINGS]
                                      [GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE]);
        err = GRPC_ERROR_CREATE(msg);
        gpr_free(msg);
        return err;
      }
      if (++cur == end) {
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FRAME:
      GPR_ASSERT(cur < end);
      if ((uint32_t)(end - cur) == transport_global->incoming_frame_size) {
        err = parse_frame_slice(exec_ctx, transport_global,
                                gpr_slice_sub_no_ref(slice, (size_t)(cur - beg),
                                                     (size_t)(end - beg)),
                                1);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        transport_global->deframe_state = GRPC_DTS_FH_0;
        transport_global->incoming_stream = NULL;
        return GRPC_ERROR_NONE;
      } else if ((uint32_t)(end - cur) >
                 transport_global->incoming_frame_size) {
        size_t cur_offset = (size_t)(cur - beg);
        err = parse_frame_slice(
            exec_ctx, transport_global,
            gpr_slice_sub_no_ref(
                slice, cur_offset,
                cur_offset + transport_global->incoming_frame_size),
            1);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        cur += transport_global->incoming_frame_size;
        transport_global->incoming_stream = NULL;
        goto dts_fh_0; /* loop */
      } else {
        err = parse_frame_slice(exec_ctx, transport_global,
                                gpr_slice_sub_no_ref(slice, (size_t)(cur - beg),
                                                     (size_t)(end - beg)),
                                0);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        transport_global->incoming_frame_size -= (uint32_t)(end - cur);
        return GRPC_ERROR_NONE;
      }
      GPR_UNREACHABLE_CODE(return 0);
  }

  GPR_UNREACHABLE_CODE(return 0);
}

static grpc_error *init_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global) {
  if (transport_global->is_first_frame &&
      transport_global->incoming_frame_type != GRPC_CHTTP2_FRAME_SETTINGS) {
    char *msg;
    gpr_asprintf(
        &msg, "Expected SETTINGS frame as the first frame, got frame type %d",
        transport_global->incoming_frame_type);
    grpc_error *err = GRPC_ERROR_CREATE(msg);
    gpr_free(msg);
    return err;
  }
  transport_global->is_first_frame = false;
  if (transport_global->expect_continuation_stream_id != 0) {
    if (transport_global->incoming_frame_type !=
        GRPC_CHTTP2_FRAME_CONTINUATION) {
      char *msg;
      gpr_asprintf(&msg, "Expected CONTINUATION frame, got frame type %02x",
                   transport_global->incoming_frame_type);
      grpc_error *err = GRPC_ERROR_CREATE(msg);
      gpr_free(msg);
      return err;
    }
    if (transport_global->expect_continuation_stream_id !=
        transport_global->incoming_stream_id) {
      char *msg;
      gpr_asprintf(
          &msg,
          "Expected CONTINUATION frame for grpc_chttp2_stream %08x, got "
          "grpc_chttp2_stream %08x",
          transport_global->expect_continuation_stream_id,
          transport_global->incoming_stream_id);
      grpc_error *err = GRPC_ERROR_CREATE(msg);
      gpr_free(msg);
      return err;
    }
    return init_header_frame_parser(exec_ctx, transport_global, 1);
  }
  switch (transport_global->incoming_frame_type) {
    case GRPC_CHTTP2_FRAME_DATA:
      return init_data_frame_parser(exec_ctx, transport_global);
    case GRPC_CHTTP2_FRAME_HEADER:
      return init_header_frame_parser(exec_ctx, transport_global, 0);
    case GRPC_CHTTP2_FRAME_CONTINUATION:
      return GRPC_ERROR_CREATE("Unexpected CONTINUATION frame");
    case GRPC_CHTTP2_FRAME_RST_STREAM:
      return init_rst_stream_parser(exec_ctx, transport_global);
    case GRPC_CHTTP2_FRAME_SETTINGS:
      return init_settings_frame_parser(exec_ctx, transport_global);
    case GRPC_CHTTP2_FRAME_WINDOW_UPDATE:
      return init_window_update_frame_parser(exec_ctx, transport_global);
    case GRPC_CHTTP2_FRAME_PING:
      return init_ping_parser(exec_ctx, transport_global);
    case GRPC_CHTTP2_FRAME_GOAWAY:
      return init_goaway_parser(exec_ctx, transport_global);
    default:
      if (grpc_http_trace) {
        gpr_log(GPR_ERROR, "Unknown frame type %02x",
                transport_global->incoming_frame_type);
      }
      return init_skip_frame_parser(exec_ctx, transport_global, 0);
  }
}

static grpc_error *skip_parser(grpc_exec_ctx *exec_ctx, void *parser,
                               grpc_chttp2_transport_global *transport_global,
                               grpc_chttp2_stream_global *stream_global,
                               gpr_slice slice, int is_last) {
  return GRPC_ERROR_NONE;
}

static void skip_header(void *tp, grpc_mdelem *md) { GRPC_MDELEM_UNREF(md); }

static grpc_error *init_skip_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    int is_header) {
  if (is_header) {
    uint8_t is_eoh = transport_global->expect_continuation_stream_id != 0;
    transport_global->parser = grpc_chttp2_header_parser_parse;
    transport_global->parser_data = &transport_global->hpack_parser;
    transport_global->hpack_parser.on_header = skip_header;
    transport_global->hpack_parser.on_header_user_data = NULL;
    transport_global->hpack_parser.is_boundary = is_eoh;
    transport_global->hpack_parser.is_eof =
        (uint8_t)(is_eoh ? transport_global->header_eof : 0);
  } else {
    transport_global->parser = skip_parser;
  }
  return GRPC_ERROR_NONE;
}

void grpc_chttp2_parsing_become_skip_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global) {
  init_skip_frame_parser(
      exec_ctx, transport_global,
      transport_global->parser == grpc_chttp2_header_parser_parse);
}

static grpc_error *update_incoming_window(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  uint32_t incoming_frame_size = transport_global->incoming_frame_size;
  if (incoming_frame_size > transport_global->incoming_window) {
    char *msg;
    gpr_asprintf(&msg, "frame of size %d overflows incoming window of %" PRId64,
                 transport_global->incoming_frame_size,
                 transport_global->incoming_window);
    grpc_error *err = GRPC_ERROR_CREATE(msg);
    gpr_free(msg);
    return err;
  }

  if (incoming_frame_size > stream_global->incoming_window) {
    char *msg;
    gpr_asprintf(&msg, "frame of size %d overflows incoming window of %" PRId64,
                 transport_global->incoming_frame_size,
                 stream_global->incoming_window);
    grpc_error *err = GRPC_ERROR_CREATE(msg);
    gpr_free(msg);
    return err;
  }

  GRPC_CHTTP2_FLOW_DEBIT_TRANSPORT("parse", transport_global, incoming_window,
                                   incoming_frame_size);
  GRPC_CHTTP2_FLOW_DEBIT_STREAM("parse", transport_global, stream_global,
                                incoming_window, incoming_frame_size);
  stream_global->received_bytes += incoming_frame_size;
  stream_global->max_recv_bytes -=
      (uint32_t)GPR_MIN(stream_global->max_recv_bytes, incoming_frame_size);

  return GRPC_ERROR_NONE;
}

static grpc_error *init_data_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global) {
  grpc_chttp2_stream_global *stream_global = grpc_chttp2_parsing_lookup_stream(
      transport_global, transport_global->incoming_stream_id);
  grpc_error *err = GRPC_ERROR_NONE;
  if (stream_global == NULL) {
    return init_skip_frame_parser(exec_ctx, transport_global, 0);
  }
  stream_global->stats.incoming.framing_bytes += 9;
  if (stream_global->read_closed) {
    return init_skip_frame_parser(exec_ctx, transport_global, 0);
  }
  if (err == GRPC_ERROR_NONE) {
    err = update_incoming_window(exec_ctx, transport_global, stream_global);
  }
  if (err == GRPC_ERROR_NONE) {
    err = grpc_chttp2_data_parser_begin_frame(
        &stream_global->data_parser, transport_global->incoming_frame_flags,
        stream_global->id);
  }
  if (err == GRPC_ERROR_NONE) {
    transport_global->incoming_stream = stream_global;
    transport_global->parser = grpc_chttp2_data_parser_parse;
    transport_global->parser_data = &stream_global->data_parser;
    return GRPC_ERROR_NONE;
  } else if (grpc_error_get_int(err, GRPC_ERROR_INT_STREAM_ID, NULL)) {
    /* handle stream errors by closing the stream */
    grpc_chttp2_mark_stream_closed(exec_ctx, transport_global, stream_global,
                                   true, false, GRPC_ERROR_REF(err));
    gpr_slice_buffer_add(
        &transport_global->qbuf,
        grpc_chttp2_rst_stream_create(transport_global->incoming_stream_id,
                                      GRPC_CHTTP2_PROTOCOL_ERROR,
                                      &stream_global->stats.outgoing));
    return init_skip_frame_parser(exec_ctx, transport_global, 0);
  } else {
    return err;
  }
}

static void free_timeout(void *p) { gpr_free(p); }

static void on_initial_header(void *tp, grpc_mdelem *md) {
  grpc_chttp2_transport_global *transport_global = tp;
  grpc_chttp2_stream_global *stream_global = transport_global->incoming_stream;

  GPR_TIMER_BEGIN("on_initial_header", 0);

  GPR_ASSERT(stream_global);

  GRPC_CHTTP2_IF_TRACING(gpr_log(
      GPR_INFO, "HTTP:%d:HDR:%s: %s: %s", stream_global->id,
      transport_global->is_client ? "CLI" : "SVR",
      grpc_mdstr_as_c_string(md->key), grpc_mdstr_as_c_string(md->value)));

  if (md->key == GRPC_MDSTR_GRPC_STATUS && md != GRPC_MDELEM_GRPC_STATUS_0) {
    /* TODO(ctiller): check for a status like " 0" */
    stream_global->seen_error = true;
  }

  if (md->key == GRPC_MDSTR_GRPC_TIMEOUT) {
    gpr_timespec *cached_timeout = grpc_mdelem_get_user_data(md, free_timeout);
    if (!cached_timeout) {
      /* not already parsed: parse it now, and store the result away */
      cached_timeout = gpr_malloc(sizeof(gpr_timespec));
      if (!grpc_http2_decode_timeout(grpc_mdstr_as_c_string(md->value),
                                     cached_timeout)) {
        gpr_log(GPR_ERROR, "Ignoring bad timeout value '%s'",
                grpc_mdstr_as_c_string(md->value));
        *cached_timeout = gpr_inf_future(GPR_TIMESPAN);
      }
      grpc_mdelem_set_user_data(md, free_timeout, cached_timeout);
    }
    grpc_chttp2_incoming_metadata_buffer_set_deadline(
        &stream_global->metadata_buffer[0],
        gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), *cached_timeout));
    GRPC_MDELEM_UNREF(md);
  } else {
    const size_t new_size =
        stream_global->metadata_buffer[0].size + GRPC_MDELEM_LENGTH(md);
    const size_t metadata_size_limit =
        transport_global->settings[GRPC_ACKED_SETTINGS]
                                  [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE];
    if (new_size > metadata_size_limit) {
      if (!stream_global->exceeded_metadata_size) {
        gpr_log(GPR_DEBUG,
                "received initial metadata size exceeds limit (%" PRIuPTR
                " vs. %" PRIuPTR ")",
                new_size, metadata_size_limit);
        stream_global->seen_error = true;
        stream_global->exceeded_metadata_size = true;
      }
      GRPC_MDELEM_UNREF(md);
    } else {
      grpc_chttp2_incoming_metadata_buffer_add(
          &stream_global->metadata_buffer[0], md);
    }
  }

  GPR_TIMER_END("on_initial_header", 0);
}

static void on_trailing_header(void *tp, grpc_mdelem *md) {
  grpc_chttp2_transport_global *transport_global = tp;
  grpc_chttp2_stream_global *stream_global = transport_global->incoming_stream;

  GPR_TIMER_BEGIN("on_trailing_header", 0);

  GPR_ASSERT(stream_global);

  GRPC_CHTTP2_IF_TRACING(gpr_log(
      GPR_INFO, "HTTP:%d:TRL:%s: %s: %s", stream_global->id,
      transport_global->is_client ? "CLI" : "SVR",
      grpc_mdstr_as_c_string(md->key), grpc_mdstr_as_c_string(md->value)));

  if (md->key == GRPC_MDSTR_GRPC_STATUS && md != GRPC_MDELEM_GRPC_STATUS_0) {
    /* TODO(ctiller): check for a status like " 0" */
    stream_global->seen_error = true;
  }

  const size_t new_size =
      stream_global->metadata_buffer[1].size + GRPC_MDELEM_LENGTH(md);
  const size_t metadata_size_limit =
      transport_global->settings[GRPC_ACKED_SETTINGS]
                                [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE];
  if (new_size > metadata_size_limit) {
    if (!stream_global->exceeded_metadata_size) {
      gpr_log(GPR_DEBUG,
              "received trailing metadata size exceeds limit (%" PRIuPTR
              " vs. %" PRIuPTR ")",
              new_size, metadata_size_limit);
      stream_global->seen_error = true;
      stream_global->exceeded_metadata_size = true;
    }
    GRPC_MDELEM_UNREF(md);
  } else {
    grpc_chttp2_incoming_metadata_buffer_add(&stream_global->metadata_buffer[1],
                                             md);
  }

  GPR_TIMER_END("on_trailing_header", 0);
}

static grpc_error *init_header_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    int is_continuation) {
  uint8_t is_eoh = (transport_global->incoming_frame_flags &
                    GRPC_CHTTP2_DATA_FLAG_END_HEADERS) != 0;
  int via_accept = 0;
  grpc_chttp2_stream_global *stream_global;

  /* TODO(ctiller): when to increment header_frames_received? */

  if (is_eoh) {
    transport_global->expect_continuation_stream_id = 0;
  } else {
    transport_global->expect_continuation_stream_id =
        transport_global->incoming_stream_id;
  }

  if (!is_continuation) {
    transport_global->header_eof = (transport_global->incoming_frame_flags &
                                    GRPC_CHTTP2_DATA_FLAG_END_STREAM) != 0;
  }

  /* could be a new grpc_chttp2_stream or an existing grpc_chttp2_stream */
  stream_global = grpc_chttp2_parsing_lookup_stream(
      transport_global, transport_global->incoming_stream_id);
  if (stream_global == NULL) {
    if (is_continuation) {
      gpr_log(GPR_ERROR,
              "grpc_chttp2_stream disbanded before CONTINUATION received");
      return init_skip_frame_parser(exec_ctx, transport_global, 1);
    }
    if (transport_global->is_client) {
      if ((transport_global->incoming_stream_id & 1) &&
          transport_global->incoming_stream_id <
              transport_global->next_stream_id) {
        /* this is an old (probably cancelled) grpc_chttp2_stream */
      } else {
        gpr_log(GPR_ERROR,
                "ignoring new grpc_chttp2_stream creation on client");
      }
      return init_skip_frame_parser(exec_ctx, transport_global, 1);
    } else if (transport_global->last_incoming_stream_id >
               transport_global->incoming_stream_id) {
      gpr_log(GPR_ERROR,
              "ignoring out of order new grpc_chttp2_stream request on server; "
              "last grpc_chttp2_stream "
              "id=%d, new grpc_chttp2_stream id=%d",
              transport_global->last_incoming_stream_id,
              transport_global->incoming_stream_id);
      return init_skip_frame_parser(exec_ctx, transport_global, 1);
    } else if ((transport_global->incoming_stream_id & 1) == 0) {
      gpr_log(GPR_ERROR,
              "ignoring grpc_chttp2_stream with non-client generated index %d",
              transport_global->incoming_stream_id);
      return init_skip_frame_parser(exec_ctx, transport_global, 1);
    }
    stream_global = transport_global->incoming_stream =
        grpc_chttp2_parsing_accept_stream(exec_ctx, transport_global,
                                          transport_global->incoming_stream_id);
    if (stream_global == NULL) {
      gpr_log(GPR_ERROR, "grpc_chttp2_stream not accepted");
      return init_skip_frame_parser(exec_ctx, transport_global, 1);
    }
    via_accept = 1;
  } else {
    transport_global->incoming_stream = stream_global;
  }
  GPR_ASSERT(stream_global != NULL && (via_accept == 0 || via_accept == 1));
  stream_global->stats.incoming.framing_bytes += 9;
  if (stream_global->read_closed) {
    gpr_log(GPR_ERROR, "skipping already closed grpc_chttp2_stream header");
    transport_global->incoming_stream = NULL;
    return init_skip_frame_parser(exec_ctx, transport_global, 1);
  }
  transport_global->parser = grpc_chttp2_header_parser_parse;
  transport_global->parser_data = &transport_global->hpack_parser;
  switch (stream_global->header_frames_received) {
    case 0:
      transport_global->hpack_parser.on_header = on_initial_header;
      break;
    case 1:
      transport_global->hpack_parser.on_header = on_trailing_header;
      break;
    case 2:
      gpr_log(GPR_ERROR, "too many header frames received");
      return init_skip_frame_parser(exec_ctx, transport_global, 1);
  }
  transport_global->hpack_parser.on_header_user_data = transport_global;
  transport_global->hpack_parser.is_boundary = is_eoh;
  transport_global->hpack_parser.is_eof =
      (uint8_t)(is_eoh ? transport_global->header_eof : 0);
  if (!is_continuation && (transport_global->incoming_frame_flags &
                           GRPC_CHTTP2_FLAG_HAS_PRIORITY)) {
    grpc_chttp2_hpack_parser_set_has_priority(&transport_global->hpack_parser);
  }
  return GRPC_ERROR_NONE;
}

static grpc_error *init_window_update_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global) {
  grpc_error *err = grpc_chttp2_window_update_parser_begin_frame(
      &transport_global->simple.window_update,
      transport_global->incoming_frame_size,
      transport_global->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  if (transport_global->incoming_stream_id != 0) {
    grpc_chttp2_stream_global *stream_global =
        transport_global->incoming_stream = grpc_chttp2_parsing_lookup_stream(
            transport_global, transport_global->incoming_stream_id);
    if (stream_global == NULL) {
      return init_skip_frame_parser(exec_ctx, transport_global, 0);
    }
    stream_global->stats.incoming.framing_bytes += 9;
  }
  transport_global->parser = grpc_chttp2_window_update_parser_parse;
  transport_global->parser_data = &transport_global->simple.window_update;
  return GRPC_ERROR_NONE;
}

static grpc_error *init_ping_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global) {
  grpc_error *err = grpc_chttp2_ping_parser_begin_frame(
      &transport_global->simple.ping, transport_global->incoming_frame_size,
      transport_global->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  transport_global->parser = grpc_chttp2_ping_parser_parse;
  transport_global->parser_data = &transport_global->simple.ping;
  return GRPC_ERROR_NONE;
}

static grpc_error *init_rst_stream_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global) {
  grpc_error *err = grpc_chttp2_rst_stream_parser_begin_frame(
      &transport_global->simple.rst_stream,
      transport_global->incoming_frame_size,
      transport_global->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  grpc_chttp2_stream_global *stream_global = transport_global->incoming_stream =
      grpc_chttp2_parsing_lookup_stream(transport_global,
                                        transport_global->incoming_stream_id);
  if (!transport_global->incoming_stream) {
    return init_skip_frame_parser(exec_ctx, transport_global, 0);
  }
  stream_global->stats.incoming.framing_bytes += 9;
  transport_global->parser = grpc_chttp2_rst_stream_parser_parse;
  transport_global->parser_data = &transport_global->simple.rst_stream;
  return GRPC_ERROR_NONE;
}

static grpc_error *init_goaway_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global) {
  grpc_error *err = grpc_chttp2_goaway_parser_begin_frame(
      &transport_global->goaway_parser, transport_global->incoming_frame_size,
      transport_global->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  transport_global->parser = grpc_chttp2_goaway_parser_parse;
  transport_global->parser_data = &transport_global->goaway_parser;
  return GRPC_ERROR_NONE;
}

static grpc_error *init_settings_frame_parser(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global) {
  if (transport_global->incoming_stream_id != 0) {
    return GRPC_ERROR_CREATE("Settings frame received for grpc_chttp2_stream");
  }

  grpc_error *err = grpc_chttp2_settings_parser_begin_frame(
      &transport_global->simple.settings, transport_global->incoming_frame_size,
      transport_global->incoming_frame_flags,
      transport_global->settings[GRPC_PEER_SETTINGS]);
  if (err != GRPC_ERROR_NONE) {
    return err;
  }
  if (transport_global->incoming_frame_flags & GRPC_CHTTP2_FLAG_ACK) {
    memcpy(transport_global->settings[GRPC_ACKED_SETTINGS],
           transport_global->settings[GRPC_SENT_SETTINGS],
           GRPC_CHTTP2_NUM_SETTINGS * sizeof(uint32_t));
    grpc_chttp2_hptbl_set_max_bytes(
        &transport_global->hpack_parser.table,
        transport_global->settings[GRPC_ACKED_SETTINGS]
                                  [GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE]);
    transport_global->sent_local_settings = 0;
  }
  transport_global->parser = grpc_chttp2_settings_parser_parse;
  transport_global->parser_data = &transport_global->simple.settings;
  return GRPC_ERROR_NONE;
}

/*
static int is_window_update_legal(int64_t window_update, int64_t window) {
  return window + window_update < MAX_WINDOW;
}
*/

static grpc_error *parse_frame_slice(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    gpr_slice slice, int is_last) {
  grpc_chttp2_stream_global *stream_global = transport_global->incoming_stream;
  grpc_error *err =
      transport_global->parser(exec_ctx, transport_global->parser_data,
                               transport_global, stream_global, slice, is_last);
  if (err == GRPC_ERROR_NONE) {
    if (stream_global != NULL) {
      grpc_chttp2_list_add_check_read_ops(exec_ctx, transport_global,
                                          stream_global);
    }
    return err;
  } else if (grpc_error_get_int(err, GRPC_ERROR_INT_STREAM_ID, NULL)) {
    if (grpc_http_trace) {
      const char *msg = grpc_error_string(err);
      gpr_log(GPR_ERROR, "%s", msg);
      grpc_error_free_string(msg);
    }
    grpc_chttp2_parsing_become_skip_parser(exec_ctx, transport_global);
    if (stream_global) {
      stream_global->forced_close_error = err;
      gpr_slice_buffer_add(
          &transport_global->qbuf,
          grpc_chttp2_rst_stream_create(transport_global->incoming_stream_id,
                                        GRPC_CHTTP2_PROTOCOL_ERROR,
                                        &stream_global->stats.outgoing));
    } else {
      GRPC_ERROR_UNREF(err);
    }
  }
  return err;
}
