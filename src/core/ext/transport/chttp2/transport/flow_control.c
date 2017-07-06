/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/support/string.h"

#ifndef NDEBUG

typedef struct {
  int64_t remote_window;
  int64_t local_window;
  int64_t announced_window;
  int64_t remote_window_delta;
  int64_t local_window_delta;
  int64_t announced_window_delta;
} shadow_flow_control;

static void pretrace(shadow_flow_control* sfc, grpc_chttp2_transport* t,
                     grpc_chttp2_stream* s) {
  sfc->remote_window = t->remote_window;
  sfc->local_window = t->local_window;
  sfc->announced_window = t->announced_window;
  if (s != NULL) {
    sfc->remote_window_delta = s->remote_window_delta;
    sfc->local_window_delta = s->local_window_delta;
    sfc->announced_window_delta = s->announced_window_delta;
  }
}

static char* fmt_str(int64_t old, int64_t new) {
  char* str;
  if (old != new) {
    gpr_asprintf(&str, "%" PRId64 " -> %" PRId64 "", old, new);
  } else {
    gpr_asprintf(&str, "%" PRId64 "", old);
  }
  char* str_lp = gpr_leftpad(str, ' ', 30);
  gpr_free(str);
  return str_lp;
}

static void posttrace(shadow_flow_control* sfc, grpc_chttp2_transport* t,
                      grpc_chttp2_stream* s, char* reason) {
  uint32_t acked_local_window =
      t->settings[GRPC_SENT_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
  uint32_t remote_window =
      t->settings[GRPC_PEER_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
  char* trw_str = fmt_str(sfc->remote_window, t->remote_window);
  char* tlw_str = fmt_str(sfc->local_window, t->local_window);
  char* taw_str = fmt_str(sfc->announced_window, t->announced_window);
  char* srw_str;
  char* slw_str;
  char* saw_str;
  if (s != NULL) {
    srw_str = fmt_str(sfc->remote_window_delta + remote_window,
                      s->remote_window_delta + remote_window);
    slw_str = fmt_str(sfc->local_window_delta + acked_local_window,
                      s->local_window_delta + acked_local_window);
    saw_str = fmt_str(sfc->announced_window_delta + acked_local_window,
                      s->announced_window_delta + acked_local_window);
  } else {
    srw_str = gpr_leftpad("", ' ', 30);
    slw_str = gpr_leftpad("", ' ', 30);
    saw_str = gpr_leftpad("", ' ', 30);
  }
  gpr_log(GPR_DEBUG,
          "%p[%u][%s] | %s | trw:%s, tlw:%s, taw:%s, srw:%s, slw:%s, saw:%s", t,
          s != NULL ? s->id : 0, t->is_client ? "cli" : "svr", reason, trw_str,
          tlw_str, taw_str, srw_str, slw_str, saw_str);
  gpr_free(trw_str);
  gpr_free(tlw_str);
  gpr_free(taw_str);
  gpr_free(srw_str);
  gpr_free(slw_str);
  gpr_free(saw_str);
}

#define PRETRACE(t, s)     \
  shadow_flow_control sfc; \
  GRPC_FLOW_CONTROL_IF_TRACING(pretrace(&sfc, t, s))
#define POSTTRACE(t, s, reason) \
  GRPC_FLOW_CONTROL_IF_TRACING(posttrace(&sfc, t, s, reason))
#else
#define PRETRACE(t, s)
#define POSTTRACE(t, s, reason) ;
#endif

/* How many bytes of incoming flow control would we like to advertise */
static uint32_t grpc_chttp2_target_announced_window(grpc_chttp2_transport* t) {
  return (uint32_t)GPR_MIN(
      (int64_t)((1u << 31) - 1),
      t->announced_stream_total_over_incoming_window +
          t->settings[GRPC_ACKED_SETTINGS]
                     [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE]);
}

// we have sent data on the wire, we must track this in our bookkeeping for the
// remote peer's flow control.
void grpc_chttp2_flowctl_sent_data(grpc_chttp2_transport* t,
                                   grpc_chttp2_stream* s, int64_t size) {
  PRETRACE(t, s);
  t->remote_window -= size;
  s->remote_window_delta -= size;
  POSTTRACE(t, s, "  data sent");
}

static void announced_window_delta_preupdate(grpc_chttp2_transport* t,
                                             grpc_chttp2_stream* s) {
  if (s->announced_window_delta > 0) {
    t->announced_stream_total_over_incoming_window -= s->announced_window_delta;
  } else {
    t->announced_stream_total_under_incoming_window +=
        -s->announced_window_delta;
  }
}

static void announced_window_delta_postupdate(grpc_chttp2_transport* t,
                                              grpc_chttp2_stream* s) {
  if (s->announced_window_delta > 0) {
    t->announced_stream_total_over_incoming_window += s->announced_window_delta;
  } else {
    t->announced_stream_total_under_incoming_window -=
        -s->announced_window_delta;
  }
}

// We have received data from the wire. We must track this in our own flow
// control bookkeeping.
// Returns an error if the incoming frame violates our flow control.
grpc_error* grpc_chttp2_flowctl_recv_data(grpc_exec_ctx* exec_ctx,
                                          grpc_chttp2_transport* t,
                                          grpc_chttp2_stream* s,
                                          int64_t incoming_frame_size) {
  PRETRACE(t, s);
  if (incoming_frame_size > t->local_window) {
    char* msg;
    gpr_asprintf(&msg, "frame of size %d overflows local window of %" PRId64,
                 t->incoming_frame_size, t->local_window);
    grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return err;
  }

  // TODO(ncteisen): can this ever be null? ANSWER: only when incoming frame
  // size is zero?
  if (s != NULL) {
    int64_t acked_stream_window =
        s->announced_window_delta +
        t->settings[GRPC_ACKED_SETTINGS]
                   [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    int64_t sent_stream_window =
        s->announced_window_delta +
        t->settings[GRPC_SENT_SETTINGS]
                   [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    if (incoming_frame_size > acked_stream_window) {
      if (incoming_frame_size <= sent_stream_window) {
        gpr_log(
            GPR_ERROR,
            "Incoming frame of size %d exceeds local window size of %" PRId64
            ".\n"
            "The (un-acked, future) window size would be %" PRId64
            " which is not exceeded.\n"
            "This would usually cause a disconnection, but allowing it due to"
            "broken HTTP2 implementations in the wild.\n"
            "See (for example) https://github.com/netty/netty/issues/6520.",
            t->incoming_frame_size, acked_stream_window, sent_stream_window);
      } else {
        char* msg;
        gpr_asprintf(&msg,
                     "frame of size %d overflows local window of %" PRId64,
                     t->incoming_frame_size, acked_stream_window);
        grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(msg);
        return err;
      }
    }

    announced_window_delta_preupdate(t, s);
    s->announced_window_delta -= incoming_frame_size;
    announced_window_delta_postupdate(t, s);
    s->local_window_delta -= incoming_frame_size;
    s->received_bytes += incoming_frame_size;

    if (s->announced_window_delta > 0) {
      t->announced_stream_total_over_incoming_window +=
          s->announced_window_delta;
    } else {
      t->announced_stream_total_under_incoming_window -=
          -s->announced_window_delta;
    }

    // TODO(control bit)
    if ((int64_t)s->local_window_delta > (int64_t)s->announced_window_delta && (int64_t)s->announced_window_delta <=
        (int64_t)t->settings[GRPC_SENT_SETTINGS]
                            [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE] /
            2) {
      grpc_chttp2_become_writable(exec_ctx, t, s,
                                  GRPC_CHTTP2_STREAM_WRITE_INITIATE_UNCOVERED,
                                  "window-update-required");
    }
  }

  t->announced_window -= incoming_frame_size;
  t->local_window -= incoming_frame_size;

  // TODO(control bit)
  uint32_t target_announced_window = grpc_chttp2_target_announced_window(t);
  if (t->announced_window <= target_announced_window / 2) {
    grpc_chttp2_initiate_write(exec_ctx, t, "flow_control");
  }

  POSTTRACE(t, s, "  data recv");
  return GRPC_ERROR_NONE;
}

// Returns a non zero announce integer if we should send a transport window
// update
uint32_t grpc_chttp2_flowctl_maybe_send_transport_update(
    grpc_chttp2_transport* t) {
  PRETRACE(t, NULL);
  uint32_t target_announced_window = grpc_chttp2_target_announced_window(t);
  uint32_t threshold_to_send_transport_window_update =
      t->outbuf.count > 0 ? 3 * target_announced_window / 4
                          : target_announced_window / 2;
  if (t->announced_window < t->local_window &&
      t->announced_window <= threshold_to_send_transport_window_update &&
      t->announced_window != target_announced_window) {
    uint32_t announce = (uint32_t)GPR_CLAMP(
        target_announced_window - t->announced_window, 0, UINT32_MAX);
    t->announced_window += announce;
    t->local_window =
        t->announced_window;  // announced should never be higher than local.
    POSTTRACE(t, NULL, "t updt sent");
    return announce;
  }

  // uint32_t announce = 0;
  // if (t->local_window > t->announced_window) {
  //   announce = (uint32_t)GPR_CLAMP(
  //       t->local_window - t->announced_window, 0, UINT32_MAX);
  //   t->announced_window += announce;
  //   POSTTRACE(t, NULL, "t updt sent");
  // }
  GRPC_FLOW_CONTROL_IF_TRACING(
      gpr_log(GPR_DEBUG, "%p[0][%s] will not to send transport update", t,
              t->is_client ? "cli" : "svr"));
  return 0;
}

// Returns a non zero announce integer if we should send a stream window update
uint32_t grpc_chttp2_flowctl_maybe_send_stream_update(grpc_chttp2_stream* s) {
  PRETRACE(s->t, s);
  if (s->local_window_delta > s->announced_window_delta) {
    uint32_t announce = (uint32_t)GPR_CLAMP(
        s->local_window_delta - s->announced_window_delta, 0, UINT32_MAX);
    announced_window_delta_preupdate(s->t, s);
    s->announced_window_delta += announce;
    announced_window_delta_postupdate(s->t, s);
    POSTTRACE(s->t, s, "s updt sent");
    return announce;
  }
  GRPC_FLOW_CONTROL_IF_TRACING(
      gpr_log(GPR_DEBUG, "%p[%u][%s] will not to send stream update", s->t,
              s->id, s->t->is_client ? "cli" : "svr"));
  return 0;
}

// we have received a WINDOW_UPDATE frame for a transport
void grpc_chttp2_flowctl_recv_transport_update(grpc_chttp2_transport* t,
                                               uint32_t size) {
  PRETRACE(t, NULL);
  t->remote_window += size;
  POSTTRACE(t, NULL, "t updt recv");
}

// we have received a WINDOW_UPDATE frame for a stream
void grpc_chttp2_flowctl_recv_stream_update(grpc_chttp2_stream* s,
                                            uint32_t size) {
  PRETRACE(s->t, s);
  s->remote_window_delta += size;
  POSTTRACE(s->t, s, "s updt recv");
}

void grpc_chttp2_flowctl_incoming_bs_update(grpc_exec_ctx *exec_ctx,
                                                     grpc_chttp2_transport *t,
                                                     grpc_chttp2_stream *s,
                                                     size_t max_size_hint,
                                                     size_t have_already) {
  PRETRACE(t, s);
  uint32_t max_recv_bytes;
  uint32_t initial_window_size =
      t->settings[GRPC_SENT_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];

  /* clamp max recv hint to an allowable size */
  if (max_size_hint >= UINT32_MAX - initial_window_size) {
    max_recv_bytes = UINT32_MAX - initial_window_size;
  } else {
    max_recv_bytes = (uint32_t)max_size_hint;
  }

  /* account for bytes already received but unknown to higher layers */
  if (max_recv_bytes >= have_already) {
    max_recv_bytes -= (uint32_t)have_already;
  } else {
    max_recv_bytes = 0;
  }

  /* add some small lookahead to keep pipelines flowing */
  GPR_ASSERT(max_recv_bytes <= UINT32_MAX - initial_window_size);
  if (s->local_window_delta < max_recv_bytes && !s->read_closed) {
    uint32_t add_max_recv_bytes =
        (uint32_t)(max_recv_bytes - s->local_window_delta);
    grpc_chttp2_stream_write_type write_type =
        GRPC_CHTTP2_STREAM_WRITE_INITIATE_UNCOVERED;
    s->local_window_delta += add_max_recv_bytes;
    s->t->local_window += add_max_recv_bytes;
    // TODO(control bits)
    if ((int64_t)initial_window_size + (int64_t)s->announced_window_delta >
            (int64_t)initial_window_size / 2 &&
        t->announced_window > (int64_t)initial_window_size / 2) {
      write_type = GRPC_CHTTP2_STREAM_WRITE_PIGGYBACK;  // TODO(contol bits)
    }
    GRPC_FLOW_CONTROL_IF_TRACING(gpr_log(
        GPR_DEBUG, "%p[%u][%s] becoming writable, %sinitiating read", t, s->id,
        t->is_client ? "cli" : "svr",
        write_type == GRPC_CHTTP2_STREAM_WRITE_PIGGYBACK ? "not " : ""));
    grpc_chttp2_become_writable(exec_ctx, t, s, write_type,
                                "read_incoming_stream");
  }
  POSTTRACE(t, s, "app st recv");
}

void grpc_chttp2_flowctl_destroy_stream(grpc_chttp2_stream* s) {
  if (s->announced_window_delta > 0) {
    s->t->announced_stream_total_over_incoming_window -=
        s->announced_window_delta;
  } else {
    s->t->announced_stream_total_under_incoming_window +=
        -s->announced_window_delta;
  }
}
