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

#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/http2_errors.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/lib/transport/timeout_encoding.h"

static grpc_error *init_frame_parser(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t);
static grpc_error *init_header_frame_parser(grpc_exec_ctx *exec_ctx,
                                            grpc_chttp2_transport *t,
                                            int is_continuation);
static grpc_error *init_data_frame_parser(grpc_exec_ctx *exec_ctx,
                                          grpc_chttp2_transport *t);
static grpc_error *init_rst_stream_parser(grpc_exec_ctx *exec_ctx,
                                          grpc_chttp2_transport *t);
static grpc_error *init_settings_frame_parser(grpc_exec_ctx *exec_ctx,
                                              grpc_chttp2_transport *t);
static grpc_error *init_window_update_frame_parser(grpc_exec_ctx *exec_ctx,
                                                   grpc_chttp2_transport *t);
static grpc_error *init_ping_parser(grpc_exec_ctx *exec_ctx,
                                    grpc_chttp2_transport *t);
static grpc_error *init_goaway_parser(grpc_exec_ctx *exec_ctx,
                                      grpc_chttp2_transport *t);
static grpc_error *init_skip_frame_parser(grpc_exec_ctx *exec_ctx,
                                          grpc_chttp2_transport *t,
                                          int is_header);

static grpc_error *parse_frame_slice(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t, grpc_slice slice,
                                     int is_last);

grpc_error *grpc_chttp2_perform_read(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t,
                                     grpc_slice slice) {
  uint8_t *beg = GRPC_SLICE_START_PTR(slice);
  uint8_t *end = GRPC_SLICE_END_PTR(slice);
  uint8_t *cur = beg;
  grpc_error *err;

  if (cur == end) return GRPC_ERROR_NONE;

  switch (t->deframe_state) {
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
      while (cur != end && t->deframe_state != GRPC_DTS_FH_0) {
        if (*cur != GRPC_CHTTP2_CLIENT_CONNECT_STRING[t->deframe_state]) {
          char *msg;
          gpr_asprintf(
              &msg,
              "Connect string mismatch: expected '%c' (%d) got '%c' (%d) "
              "at byte %d",
              GRPC_CHTTP2_CLIENT_CONNECT_STRING[t->deframe_state],
              (int)(uint8_t)GRPC_CHTTP2_CLIENT_CONNECT_STRING[t->deframe_state],
              *cur, (int)*cur, t->deframe_state);
          err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
          gpr_free(msg);
          return err;
        }
        ++cur;
        ++t->deframe_state;
      }
      if (cur == end) {
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    dts_fh_0:
    case GRPC_DTS_FH_0:
      GPR_ASSERT(cur < end);
      t->incoming_frame_size = ((uint32_t)*cur) << 16;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_1;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_1:
      GPR_ASSERT(cur < end);
      t->incoming_frame_size |= ((uint32_t)*cur) << 8;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_2;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_2:
      GPR_ASSERT(cur < end);
      t->incoming_frame_size |= *cur;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_3;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_3:
      GPR_ASSERT(cur < end);
      t->incoming_frame_type = *cur;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_4;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_4:
      GPR_ASSERT(cur < end);
      t->incoming_frame_flags = *cur;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_5;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_5:
      GPR_ASSERT(cur < end);
      t->incoming_stream_id = (((uint32_t)*cur) & 0x7f) << 24;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_6;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_6:
      GPR_ASSERT(cur < end);
      t->incoming_stream_id |= ((uint32_t)*cur) << 16;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_7;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_7:
      GPR_ASSERT(cur < end);
      t->incoming_stream_id |= ((uint32_t)*cur) << 8;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_8;
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FH_8:
      GPR_ASSERT(cur < end);
      t->incoming_stream_id |= ((uint32_t)*cur);
      t->deframe_state = GRPC_DTS_FRAME;
      err = init_frame_parser(exec_ctx, t);
      if (err != GRPC_ERROR_NONE) {
        return err;
      }
      if (t->incoming_frame_size == 0) {
        err = parse_frame_slice(exec_ctx, t, grpc_empty_slice(), 1);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        t->incoming_stream = NULL;
        if (++cur == end) {
          t->deframe_state = GRPC_DTS_FH_0;
          return GRPC_ERROR_NONE;
        }
        goto dts_fh_0; /* loop */
      } else if (t->incoming_frame_size >
                 t->settings[GRPC_ACKED_SETTINGS]
                            [GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE]) {
        char *msg;
        gpr_asprintf(&msg, "Frame size %d is larger than max frame size %d",
                     t->incoming_frame_size,
                     t->settings[GRPC_ACKED_SETTINGS]
                                [GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE]);
        err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(msg);
        return err;
      }
      if (++cur == end) {
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_DTS_FRAME:
      GPR_ASSERT(cur < end);
      if ((uint32_t)(end - cur) == t->incoming_frame_size) {
        err = parse_frame_slice(
            exec_ctx, t, grpc_slice_sub_no_ref(slice, (size_t)(cur - beg),
                                               (size_t)(end - beg)),
            1);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        t->deframe_state = GRPC_DTS_FH_0;
        t->incoming_stream = NULL;
        return GRPC_ERROR_NONE;
      } else if ((uint32_t)(end - cur) > t->incoming_frame_size) {
        size_t cur_offset = (size_t)(cur - beg);
        err = parse_frame_slice(
            exec_ctx, t,
            grpc_slice_sub_no_ref(slice, cur_offset,
                                  cur_offset + t->incoming_frame_size),
            1);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        cur += t->incoming_frame_size;
        t->incoming_stream = NULL;
        goto dts_fh_0; /* loop */
      } else {
        err = parse_frame_slice(
            exec_ctx, t, grpc_slice_sub_no_ref(slice, (size_t)(cur - beg),
                                               (size_t)(end - beg)),
            0);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        t->incoming_frame_size -= (uint32_t)(end - cur);
        return GRPC_ERROR_NONE;
      }
      GPR_UNREACHABLE_CODE(return 0);
  }

  GPR_UNREACHABLE_CODE(return 0);
}

static grpc_error *init_frame_parser(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t) {
  if (t->is_first_frame &&
      t->incoming_frame_type != GRPC_CHTTP2_FRAME_SETTINGS) {
    char *msg;
    gpr_asprintf(
        &msg, "Expected SETTINGS frame as the first frame, got frame type %d",
        t->incoming_frame_type);
    grpc_error *err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return err;
  }
  t->is_first_frame = false;
  if (t->expect_continuation_stream_id != 0) {
    if (t->incoming_frame_type != GRPC_CHTTP2_FRAME_CONTINUATION) {
      char *msg;
      gpr_asprintf(&msg, "Expected CONTINUATION frame, got frame type %02x",
                   t->incoming_frame_type);
      grpc_error *err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
      gpr_free(msg);
      return err;
    }
    if (t->expect_continuation_stream_id != t->incoming_stream_id) {
      char *msg;
      gpr_asprintf(
          &msg,
          "Expected CONTINUATION frame for grpc_chttp2_stream %08x, got "
          "grpc_chttp2_stream %08x",
          t->expect_continuation_stream_id, t->incoming_stream_id);
      grpc_error *err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
      gpr_free(msg);
      return err;
    }
    return init_header_frame_parser(exec_ctx, t, 1);
  }
  switch (t->incoming_frame_type) {
    case GRPC_CHTTP2_FRAME_DATA:
      return init_data_frame_parser(exec_ctx, t);
    case GRPC_CHTTP2_FRAME_HEADER:
      return init_header_frame_parser(exec_ctx, t, 0);
    case GRPC_CHTTP2_FRAME_CONTINUATION:
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Unexpected CONTINUATION frame");
    case GRPC_CHTTP2_FRAME_RST_STREAM:
      return init_rst_stream_parser(exec_ctx, t);
    case GRPC_CHTTP2_FRAME_SETTINGS:
      return init_settings_frame_parser(exec_ctx, t);
    case GRPC_CHTTP2_FRAME_WINDOW_UPDATE:
      return init_window_update_frame_parser(exec_ctx, t);
    case GRPC_CHTTP2_FRAME_PING:
      return init_ping_parser(exec_ctx, t);
    case GRPC_CHTTP2_FRAME_GOAWAY:
      return init_goaway_parser(exec_ctx, t);
    default:
      if (grpc_http_trace) {
        gpr_log(GPR_ERROR, "Unknown frame type %02x", t->incoming_frame_type);
      }
      return init_skip_frame_parser(exec_ctx, t, 0);
  }
}

static grpc_error *skip_parser(grpc_exec_ctx *exec_ctx, void *parser,
                               grpc_chttp2_transport *t, grpc_chttp2_stream *s,
                               grpc_slice slice, int is_last) {
  return GRPC_ERROR_NONE;
}

static void skip_header(grpc_exec_ctx *exec_ctx, void *tp, grpc_mdelem md) {
  GRPC_MDELEM_UNREF(exec_ctx, md);
}

static grpc_error *init_skip_frame_parser(grpc_exec_ctx *exec_ctx,
                                          grpc_chttp2_transport *t,
                                          int is_header) {
  if (is_header) {
    uint8_t is_eoh = t->expect_continuation_stream_id != 0;
    t->parser = grpc_chttp2_header_parser_parse;
    t->parser_data = &t->hpack_parser;
    t->hpack_parser.on_header = skip_header;
    t->hpack_parser.on_header_user_data = NULL;
    t->hpack_parser.is_boundary = is_eoh;
    t->hpack_parser.is_eof = (uint8_t)(is_eoh ? t->header_eof : 0);
  } else {
    t->parser = skip_parser;
  }
  return GRPC_ERROR_NONE;
}

void grpc_chttp2_parsing_become_skip_parser(grpc_exec_ctx *exec_ctx,
                                            grpc_chttp2_transport *t) {
  init_skip_frame_parser(exec_ctx, t,
                         t->parser == grpc_chttp2_header_parser_parse);
}

static grpc_error *update_incoming_window(grpc_exec_ctx *exec_ctx,
                                          grpc_chttp2_transport *t,
                                          grpc_chttp2_stream *s) {
  uint32_t incoming_frame_size = t->incoming_frame_size;
  if (incoming_frame_size > t->incoming_window) {
    char *msg;
    gpr_asprintf(&msg, "frame of size %d overflows incoming window of %" PRId64,
                 t->incoming_frame_size, t->incoming_window);
    grpc_error *err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return err;
  }

  if (s != NULL) {
    if (incoming_frame_size >
        s->incoming_window_delta +
            t->settings[GRPC_ACKED_SETTINGS]
                       [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE]) {
      if (incoming_frame_size <=
          s->incoming_window_delta +
              t->settings[GRPC_SENT_SETTINGS]
                         [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE]) {
        gpr_log(
            GPR_ERROR,
            "Incoming frame of size %d exceeds incoming window size of %" PRId64
            ".\n"
            "The (un-acked, future) window size would be %" PRId64
            " which is not exceeded.\n"
            "This would usually cause a disconnection, but allowing it due to "
            "broken HTTP2 implementations in the wild.\n"
            "See (for example) https://github.com/netty/netty/issues/6520.",
            t->incoming_frame_size,
            s->incoming_window_delta +
                t->settings[GRPC_ACKED_SETTINGS]
                           [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
            s->incoming_window_delta +
                t->settings[GRPC_SENT_SETTINGS]
                           [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE]);
      } else {
        char *msg;
        gpr_asprintf(&msg,
                     "frame of size %d overflows incoming window of %" PRId64,
                     t->incoming_frame_size,
                     s->incoming_window_delta +
                         t->settings[GRPC_ACKED_SETTINGS]
                                    [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE]);
        grpc_error *err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(msg);
        return err;
      }
    }

    GRPC_CHTTP2_FLOW_DEBIT_STREAM_INCOMING_WINDOW_DELTA("parse", t, s,
                                                        incoming_frame_size);
    if ((int64_t)t->settings[GRPC_SENT_SETTINGS]
                            [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE] +
            (int64_t)s->incoming_window_delta - (int64_t)s->announce_window <=
        (int64_t)t->settings[GRPC_SENT_SETTINGS]
                            [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE] /
            2) {
      grpc_chttp2_become_writable(exec_ctx, t, s,
                                  GRPC_CHTTP2_STREAM_WRITE_INITIATE_UNCOVERED,
                                  "window-update-required");
    }
    s->received_bytes += incoming_frame_size;
  }

  uint32_t target_incoming_window = grpc_chttp2_target_incoming_window(t);
  GRPC_CHTTP2_FLOW_DEBIT_TRANSPORT("parse", t, incoming_window,
                                   incoming_frame_size);
  if (t->incoming_window <= target_incoming_window / 2) {
    grpc_chttp2_initiate_write(exec_ctx, t, false, "flow_control");
  }

  return GRPC_ERROR_NONE;
}

static grpc_error *init_data_frame_parser(grpc_exec_ctx *exec_ctx,
                                          grpc_chttp2_transport *t) {
  grpc_chttp2_stream *s =
      grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
  grpc_error *err = GRPC_ERROR_NONE;
  err = update_incoming_window(exec_ctx, t, s);
  if (err != GRPC_ERROR_NONE) {
    goto error_handler;
  }
  if (s == NULL) {
    return init_skip_frame_parser(exec_ctx, t, 0);
  }
  s->stats.incoming.framing_bytes += 9;
  if (err == GRPC_ERROR_NONE && s->read_closed) {
    return init_skip_frame_parser(exec_ctx, t, 0);
  }
  if (err == GRPC_ERROR_NONE) {
    err = grpc_chttp2_data_parser_begin_frame(&s->data_parser,
                                              t->incoming_frame_flags, s->id);
  }
error_handler:
  if (err == GRPC_ERROR_NONE) {
    t->incoming_stream = s;
    t->parser = grpc_chttp2_data_parser_parse;
    t->parser_data = &s->data_parser;
    return GRPC_ERROR_NONE;
  } else if (grpc_error_get_int(err, GRPC_ERROR_INT_STREAM_ID, NULL)) {
    /* handle stream errors by closing the stream */
    if (s != NULL) {
      grpc_chttp2_mark_stream_closed(exec_ctx, t, s, true, false, err);
    }
    grpc_slice_buffer_add(
        &t->qbuf, grpc_chttp2_rst_stream_create(t->incoming_stream_id,
                                                GRPC_HTTP2_PROTOCOL_ERROR,
                                                &s->stats.outgoing));
    return init_skip_frame_parser(exec_ctx, t, 0);
  } else {
    return err;
  }
}

static void free_timeout(void *p) { gpr_free(p); }

static void on_initial_header(grpc_exec_ctx *exec_ctx, void *tp,
                              grpc_mdelem md) {
  grpc_chttp2_transport *t = tp;
  grpc_chttp2_stream *s = t->incoming_stream;

  GPR_TIMER_BEGIN("on_initial_header", 0);

  GPR_ASSERT(s != NULL);

  if (grpc_http_trace) {
    char *key = grpc_slice_to_c_string(GRPC_MDKEY(md));
    char *value =
        grpc_dump_slice(GRPC_MDVALUE(md), GPR_DUMP_HEX | GPR_DUMP_ASCII);
    gpr_log(GPR_INFO, "HTTP:%d:HDR:%s: %s: %s", s->id,
            t->is_client ? "CLI" : "SVR", key, value);
    gpr_free(key);
    gpr_free(value);
  }

  if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_GRPC_STATUS) &&
      !grpc_mdelem_eq(md, GRPC_MDELEM_GRPC_STATUS_0)) {
    /* TODO(ctiller): check for a status like " 0" */
    s->seen_error = true;
  }

  if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_GRPC_TIMEOUT)) {
    gpr_timespec *cached_timeout = grpc_mdelem_get_user_data(md, free_timeout);
    gpr_timespec timeout;
    if (cached_timeout == NULL) {
      /* not already parsed: parse it now, and store the result away */
      cached_timeout = gpr_malloc(sizeof(gpr_timespec));
      if (!grpc_http2_decode_timeout(GRPC_MDVALUE(md), cached_timeout)) {
        char *val = grpc_slice_to_c_string(GRPC_MDVALUE(md));
        gpr_log(GPR_ERROR, "Ignoring bad timeout value '%s'", val);
        gpr_free(val);
        *cached_timeout = gpr_inf_future(GPR_TIMESPAN);
      }
      timeout = *cached_timeout;
      grpc_mdelem_set_user_data(md, free_timeout, cached_timeout);
    } else {
      timeout = *cached_timeout;
    }
    grpc_chttp2_incoming_metadata_buffer_set_deadline(
        &s->metadata_buffer[0],
        gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), timeout));
    GRPC_MDELEM_UNREF(exec_ctx, md);
  } else {
    const size_t new_size = s->metadata_buffer[0].size + GRPC_MDELEM_LENGTH(md);
    const size_t metadata_size_limit =
        t->settings[GRPC_ACKED_SETTINGS]
                   [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE];
    if (new_size > metadata_size_limit) {
      gpr_log(GPR_DEBUG,
              "received initial metadata size exceeds limit (%" PRIuPTR
              " vs. %" PRIuPTR ")",
              new_size, metadata_size_limit);
      grpc_chttp2_cancel_stream(
          exec_ctx, t, s,
          grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                  "received initial metadata size exceeds limit"),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED));
      grpc_chttp2_parsing_become_skip_parser(exec_ctx, t);
      s->seen_error = true;
      GRPC_MDELEM_UNREF(exec_ctx, md);
    } else {
      grpc_error *error = grpc_chttp2_incoming_metadata_buffer_add(
          exec_ctx, &s->metadata_buffer[0], md);
      if (error != GRPC_ERROR_NONE) {
        grpc_chttp2_cancel_stream(exec_ctx, t, s, error);
        grpc_chttp2_parsing_become_skip_parser(exec_ctx, t);
        s->seen_error = true;
        GRPC_MDELEM_UNREF(exec_ctx, md);
      }
    }
  }

  GPR_TIMER_END("on_initial_header", 0);
}

static void on_trailing_header(grpc_exec_ctx *exec_ctx, void *tp,
                               grpc_mdelem md) {
  grpc_chttp2_transport *t = tp;
  grpc_chttp2_stream *s = t->incoming_stream;

  GPR_TIMER_BEGIN("on_trailing_header", 0);

  GPR_ASSERT(s != NULL);

  if (grpc_http_trace) {
    char *key = grpc_slice_to_c_string(GRPC_MDKEY(md));
    char *value =
        grpc_dump_slice(GRPC_MDVALUE(md), GPR_DUMP_HEX | GPR_DUMP_ASCII);
    gpr_log(GPR_INFO, "HTTP:%d:TRL:%s: %s: %s", s->id,
            t->is_client ? "CLI" : "SVR", key, value);
    gpr_free(key);
    gpr_free(value);
  }

  if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_GRPC_STATUS) &&
      !grpc_mdelem_eq(md, GRPC_MDELEM_GRPC_STATUS_0)) {
    /* TODO(ctiller): check for a status like " 0" */
    s->seen_error = true;
  }

  const size_t new_size = s->metadata_buffer[1].size + GRPC_MDELEM_LENGTH(md);
  const size_t metadata_size_limit =
      t->settings[GRPC_ACKED_SETTINGS]
                 [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE];
  if (new_size > metadata_size_limit) {
    gpr_log(GPR_DEBUG,
            "received trailing metadata size exceeds limit (%" PRIuPTR
            " vs. %" PRIuPTR ")",
            new_size, metadata_size_limit);
    grpc_chttp2_cancel_stream(
        exec_ctx, t, s,
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                               "received trailing metadata size exceeds limit"),
                           GRPC_ERROR_INT_GRPC_STATUS,
                           GRPC_STATUS_RESOURCE_EXHAUSTED));
    grpc_chttp2_parsing_become_skip_parser(exec_ctx, t);
    s->seen_error = true;
    GRPC_MDELEM_UNREF(exec_ctx, md);
  } else {
    grpc_error *error = grpc_chttp2_incoming_metadata_buffer_add(
        exec_ctx, &s->metadata_buffer[1], md);
    if (error != GRPC_ERROR_NONE) {
      grpc_chttp2_cancel_stream(exec_ctx, t, s, error);
      grpc_chttp2_parsing_become_skip_parser(exec_ctx, t);
      s->seen_error = true;
      GRPC_MDELEM_UNREF(exec_ctx, md);
    }
  }

  GPR_TIMER_END("on_trailing_header", 0);
}

static grpc_error *init_header_frame_parser(grpc_exec_ctx *exec_ctx,
                                            grpc_chttp2_transport *t,
                                            int is_continuation) {
  uint8_t is_eoh =
      (t->incoming_frame_flags & GRPC_CHTTP2_DATA_FLAG_END_HEADERS) != 0;
  grpc_chttp2_stream *s;

  /* TODO(ctiller): when to increment header_frames_received? */

  if (is_eoh) {
    t->expect_continuation_stream_id = 0;
  } else {
    t->expect_continuation_stream_id = t->incoming_stream_id;
  }

  if (!is_continuation) {
    t->header_eof =
        (t->incoming_frame_flags & GRPC_CHTTP2_DATA_FLAG_END_STREAM) != 0;
  }

  /* could be a new grpc_chttp2_stream or an existing grpc_chttp2_stream */
  s = grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
  if (s == NULL) {
    if (is_continuation) {
      GRPC_CHTTP2_IF_TRACING(
          gpr_log(GPR_ERROR,
                  "grpc_chttp2_stream disbanded before CONTINUATION received"));
      return init_skip_frame_parser(exec_ctx, t, 1);
    }
    if (t->is_client) {
      if ((t->incoming_stream_id & 1) &&
          t->incoming_stream_id < t->next_stream_id) {
        /* this is an old (probably cancelled) grpc_chttp2_stream */
      } else {
        GRPC_CHTTP2_IF_TRACING(gpr_log(
            GPR_ERROR, "ignoring new grpc_chttp2_stream creation on client"));
      }
      return init_skip_frame_parser(exec_ctx, t, 1);
    } else if (t->last_new_stream_id >= t->incoming_stream_id) {
      GRPC_CHTTP2_IF_TRACING(gpr_log(
          GPR_ERROR,
          "ignoring out of order new grpc_chttp2_stream request on server; "
          "last grpc_chttp2_stream "
          "id=%d, new grpc_chttp2_stream id=%d",
          t->last_new_stream_id, t->incoming_stream_id));
      return init_skip_frame_parser(exec_ctx, t, 1);
    } else if ((t->incoming_stream_id & 1) == 0) {
      GRPC_CHTTP2_IF_TRACING(gpr_log(
          GPR_ERROR,
          "ignoring grpc_chttp2_stream with non-client generated index %d",
          t->incoming_stream_id));
      return init_skip_frame_parser(exec_ctx, t, 1);
    }
    t->last_new_stream_id = t->incoming_stream_id;
    s = t->incoming_stream =
        grpc_chttp2_parsing_accept_stream(exec_ctx, t, t->incoming_stream_id);
    if (s == NULL) {
      GRPC_CHTTP2_IF_TRACING(
          gpr_log(GPR_ERROR, "grpc_chttp2_stream not accepted"));
      return init_skip_frame_parser(exec_ctx, t, 1);
    }
  } else {
    t->incoming_stream = s;
  }
  GPR_ASSERT(s != NULL);
  s->stats.incoming.framing_bytes += 9;
  if (s->read_closed) {
    GRPC_CHTTP2_IF_TRACING(gpr_log(
        GPR_ERROR, "skipping already closed grpc_chttp2_stream header"));
    t->incoming_stream = NULL;
    return init_skip_frame_parser(exec_ctx, t, 1);
  }
  t->parser = grpc_chttp2_header_parser_parse;
  t->parser_data = &t->hpack_parser;
  switch (s->header_frames_received) {
    case 0:
      t->hpack_parser.on_header = on_initial_header;
      break;
    case 1:
      t->hpack_parser.on_header = on_trailing_header;
      break;
    case 2:
      gpr_log(GPR_ERROR, "too many header frames received");
      return init_skip_frame_parser(exec_ctx, t, 1);
  }
  t->hpack_parser.on_header_user_data = t;
  t->hpack_parser.is_boundary = is_eoh;
  t->hpack_parser.is_eof = (uint8_t)(is_eoh ? t->header_eof : 0);
  if (!is_continuation &&
      (t->incoming_frame_flags & GRPC_CHTTP2_FLAG_HAS_PRIORITY)) {
    grpc_chttp2_hpack_parser_set_has_priority(&t->hpack_parser);
  }
  return GRPC_ERROR_NONE;
}

static grpc_error *init_window_update_frame_parser(grpc_exec_ctx *exec_ctx,
                                                   grpc_chttp2_transport *t) {
  grpc_error *err = grpc_chttp2_window_update_parser_begin_frame(
      &t->simple.window_update, t->incoming_frame_size,
      t->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  if (t->incoming_stream_id != 0) {
    grpc_chttp2_stream *s = t->incoming_stream =
        grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
    if (s == NULL) {
      return init_skip_frame_parser(exec_ctx, t, 0);
    }
    s->stats.incoming.framing_bytes += 9;
  }
  t->parser = grpc_chttp2_window_update_parser_parse;
  t->parser_data = &t->simple.window_update;
  return GRPC_ERROR_NONE;
}

static grpc_error *init_ping_parser(grpc_exec_ctx *exec_ctx,
                                    grpc_chttp2_transport *t) {
  grpc_error *err = grpc_chttp2_ping_parser_begin_frame(
      &t->simple.ping, t->incoming_frame_size, t->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  t->parser = grpc_chttp2_ping_parser_parse;
  t->parser_data = &t->simple.ping;
  return GRPC_ERROR_NONE;
}

static grpc_error *init_rst_stream_parser(grpc_exec_ctx *exec_ctx,
                                          grpc_chttp2_transport *t) {
  grpc_error *err = grpc_chttp2_rst_stream_parser_begin_frame(
      &t->simple.rst_stream, t->incoming_frame_size, t->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  grpc_chttp2_stream *s = t->incoming_stream =
      grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
  if (!t->incoming_stream) {
    return init_skip_frame_parser(exec_ctx, t, 0);
  }
  s->stats.incoming.framing_bytes += 9;
  t->parser = grpc_chttp2_rst_stream_parser_parse;
  t->parser_data = &t->simple.rst_stream;
  return GRPC_ERROR_NONE;
}

static grpc_error *init_goaway_parser(grpc_exec_ctx *exec_ctx,
                                      grpc_chttp2_transport *t) {
  grpc_error *err = grpc_chttp2_goaway_parser_begin_frame(
      &t->goaway_parser, t->incoming_frame_size, t->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  t->parser = grpc_chttp2_goaway_parser_parse;
  t->parser_data = &t->goaway_parser;
  return GRPC_ERROR_NONE;
}

static grpc_error *init_settings_frame_parser(grpc_exec_ctx *exec_ctx,
                                              grpc_chttp2_transport *t) {
  if (t->incoming_stream_id != 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Settings frame received for grpc_chttp2_stream");
  }

  grpc_error *err = grpc_chttp2_settings_parser_begin_frame(
      &t->simple.settings, t->incoming_frame_size, t->incoming_frame_flags,
      t->settings[GRPC_PEER_SETTINGS]);
  if (err != GRPC_ERROR_NONE) {
    return err;
  }
  if (t->incoming_frame_flags & GRPC_CHTTP2_FLAG_ACK) {
    memcpy(t->settings[GRPC_ACKED_SETTINGS], t->settings[GRPC_SENT_SETTINGS],
           GRPC_CHTTP2_NUM_SETTINGS * sizeof(uint32_t));
    grpc_chttp2_hptbl_set_max_bytes(
        exec_ctx, &t->hpack_parser.table,
        t->settings[GRPC_ACKED_SETTINGS]
                   [GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE]);
    t->sent_local_settings = 0;
  }
  t->parser = grpc_chttp2_settings_parser_parse;
  t->parser_data = &t->simple.settings;
  return GRPC_ERROR_NONE;
}

static grpc_error *parse_frame_slice(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t, grpc_slice slice,
                                     int is_last) {
  grpc_chttp2_stream *s = t->incoming_stream;
  grpc_error *err = t->parser(exec_ctx, t->parser_data, t, s, slice, is_last);
  if (err == GRPC_ERROR_NONE) {
    return err;
  } else if (grpc_error_get_int(err, GRPC_ERROR_INT_STREAM_ID, NULL)) {
    if (grpc_http_trace) {
      const char *msg = grpc_error_string(err);
      gpr_log(GPR_ERROR, "%s", msg);
    }
    grpc_chttp2_parsing_become_skip_parser(exec_ctx, t);
    if (s) {
      s->forced_close_error = err;
      grpc_slice_buffer_add(
          &t->qbuf, grpc_chttp2_rst_stream_create(t->incoming_stream_id,
                                                  GRPC_HTTP2_PROTOCOL_ERROR,
                                                  &s->stats.outgoing));
    } else {
      GRPC_ERROR_UNREF(err);
    }
  }
  return err;
}
