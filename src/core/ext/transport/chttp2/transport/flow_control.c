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

static uint32_t grpc_chttp2_target_announced_window(
    const grpc_chttp2_transport_flowctl* tfc);

#ifndef NDEBUG

typedef struct {
  int64_t remote_window;
  int64_t target_window;
  int64_t announced_window;
  int64_t remote_window_delta;
  int64_t local_window_delta;
  int64_t announced_window_delta;
} shadow_flow_control;

static void pretrace(shadow_flow_control* shadow_fc,
                     grpc_chttp2_transport_flowctl* tfc,
                     grpc_chttp2_stream_flowctl* sfc) {
  shadow_fc->remote_window = tfc->remote_window;
  shadow_fc->target_window = grpc_chttp2_target_announced_window(tfc);
  shadow_fc->announced_window = tfc->announced_window;
  if (sfc != NULL) {
    shadow_fc->remote_window_delta = sfc->remote_window_delta;
    shadow_fc->local_window_delta = sfc->local_window_delta;
    shadow_fc->announced_window_delta = sfc->announced_window_delta;
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

static void posttrace(shadow_flow_control* shadow_fc,
                      grpc_chttp2_transport_flowctl* tfc,
                      grpc_chttp2_stream_flowctl* sfc, char* reason) {
  uint32_t acked_local_window =
      tfc->t->settings[GRPC_SENT_SETTINGS]
                      [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
  uint32_t remote_window =
      tfc->t->settings[GRPC_PEER_SETTINGS]
                      [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
  char* trw_str = fmt_str(shadow_fc->remote_window, tfc->remote_window);
  char* tlw_str = fmt_str(shadow_fc->target_window,
                          grpc_chttp2_target_announced_window(tfc));
  char* taw_str = fmt_str(shadow_fc->announced_window, tfc->announced_window);
  char* srw_str;
  char* slw_str;
  char* saw_str;
  if (sfc != NULL) {
    srw_str = fmt_str(shadow_fc->remote_window_delta + remote_window,
                      sfc->remote_window_delta + remote_window);
    slw_str = fmt_str(shadow_fc->local_window_delta + acked_local_window,
                      sfc->local_window_delta + acked_local_window);
    saw_str = fmt_str(shadow_fc->announced_window_delta + acked_local_window,
                      sfc->announced_window_delta + acked_local_window);
  } else {
    srw_str = gpr_leftpad("", ' ', 30);
    slw_str = gpr_leftpad("", ' ', 30);
    saw_str = gpr_leftpad("", ' ', 30);
  }
  gpr_log(GPR_DEBUG,
          "%p[%u][%s] | %s | trw:%s, ttw:%s, taw:%s, srw:%s, slw:%s, saw:%s",
          tfc, sfc != NULL ? sfc->s->id : 0, tfc->t->is_client ? "cli" : "svr",
          reason, trw_str, tlw_str, taw_str, srw_str, slw_str, saw_str);
  gpr_free(trw_str);
  gpr_free(tlw_str);
  gpr_free(taw_str);
  gpr_free(srw_str);
  gpr_free(slw_str);
  gpr_free(saw_str);
}

static char* urgency_to_string(grpc_chttp2_flowctl_urgency urgency) {
  switch (urgency) {
    case GRPC_CHTTP2_FLOWCTL_NO_ACTION_NEEDED:
      return "no action";
    case GRPC_CHTTP2_FLOWCTL_UPDATE_IMMEDIATELY:
      return "update immediately";
    case GRPC_CHTTP2_FLOWCTL_QUEUE_UPDATE:
      return "queue update";
    default:
      GPR_UNREACHABLE_CODE(return "unknown");
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

static void trace_action(grpc_chttp2_flowctl_action action) {
  gpr_log(GPR_DEBUG, "transport: %s,  stream: %s",
          urgency_to_string(action.send_transport_update),
          urgency_to_string(action.send_stream_update));
}

#define PRETRACE(tfc, sfc)       \
  shadow_flow_control shadow_fc; \
  GRPC_FLOW_CONTROL_IF_TRACING(pretrace(&shadow_fc, tfc, sfc))
#define POSTTRACE(tfc, sfc, reason) \
  GRPC_FLOW_CONTROL_IF_TRACING(posttrace(&shadow_fc, tfc, sfc, reason))
#define TRACEACTION(action) GRPC_FLOW_CONTROL_IF_TRACING(trace_action(action))
#else
#define PRETRACE(tfc, sfc)
#define POSTTRACE(tfc, sfc, reason)
#define TRACEACTION(action)
#endif

/* How many bytes of incoming flow control would we like to advertise */
static uint32_t grpc_chttp2_target_announced_window(
    const grpc_chttp2_transport_flowctl* tfc) {
  return (uint32_t)GPR_MIN(
      (int64_t)((1u << 31) - 1),
      tfc->announced_stream_total_over_incoming_window +
          tfc->t->settings[GRPC_SENT_SETTINGS]
                          [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE]);
}

// we have sent data on the wire, we must track this in our bookkeeping for the
// remote peer's flow control.
void grpc_chttp2_flowctl_sent_data(grpc_chttp2_transport_flowctl* tfc,
                                   grpc_chttp2_stream_flowctl* sfc,
                                   int64_t size) {
  PRETRACE(tfc, sfc);
  tfc->remote_window -= size;
  sfc->remote_window_delta -= size;
  POSTTRACE(tfc, sfc, "  data sent");
}

static void announced_window_delta_preupdate(grpc_chttp2_transport_flowctl* tfc,
                                             grpc_chttp2_stream_flowctl* sfc) {
  if (sfc->announced_window_delta > 0) {
    tfc->announced_stream_total_over_incoming_window -=
        sfc->announced_window_delta;
  } else {
    tfc->announced_stream_total_under_incoming_window +=
        -sfc->announced_window_delta;
  }
}

static void announced_window_delta_postupdate(
    grpc_chttp2_transport_flowctl* tfc, grpc_chttp2_stream_flowctl* sfc) {
  if (sfc->announced_window_delta > 0) {
    tfc->announced_stream_total_over_incoming_window +=
        sfc->announced_window_delta;
  } else {
    tfc->announced_stream_total_under_incoming_window -=
        -sfc->announced_window_delta;
  }
}

// We have received data from the wire. We must track this in our own flow
// control bookkeeping.
// Returns an error if the incoming frame violates our flow control.
grpc_error* grpc_chttp2_flowctl_recv_data(grpc_chttp2_transport_flowctl* tfc,
                                          grpc_chttp2_stream_flowctl* sfc,
                                          int64_t incoming_frame_size) {
  uint32_t sent_init_window =
      tfc->t->settings[GRPC_SENT_SETTINGS]
                      [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
  uint32_t acked_init_window =
      tfc->t->settings[GRPC_ACKED_SETTINGS]
                      [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
  PRETRACE(tfc, sfc);
  if (incoming_frame_size > tfc->announced_window) {
    char* msg;
    gpr_asprintf(&msg,
                 "frame of size %" PRId64 " overflows local window of %" PRId64,
                 incoming_frame_size, tfc->announced_window);
    grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return err;
  }

  if (sfc != NULL) {
    int64_t acked_stream_window =
        sfc->announced_window_delta + acked_init_window;
    int64_t sent_stream_window = sfc->announced_window_delta + sent_init_window;
    if (incoming_frame_size > acked_stream_window) {
      if (incoming_frame_size <= sent_stream_window) {
        gpr_log(
            GPR_ERROR,
            "Incoming frame of size %" PRId64
            " exceeds local window size of %" PRId64
            ".\n"
            "The (un-acked, future) window size would be %" PRId64
            " which is not exceeded.\n"
            "This would usually cause a disconnection, but allowing it due to"
            "broken HTTP2 implementations in the wild.\n"
            "See (for example) https://github.com/netty/netty/issues/6520.",
            incoming_frame_size, acked_stream_window, sent_stream_window);
      } else {
        char* msg;
        gpr_asprintf(&msg, "frame of size %" PRId64
                           " overflows local window of %" PRId64,
                     incoming_frame_size, acked_stream_window);
        grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(msg);
        return err;
      }
    }

    announced_window_delta_preupdate(tfc, sfc);
    sfc->announced_window_delta -= incoming_frame_size;
    announced_window_delta_postupdate(tfc, sfc);
    sfc->local_window_delta -= incoming_frame_size;
  }

  tfc->announced_window -= incoming_frame_size;

  POSTTRACE(tfc, sfc, "  data recv");
  return GRPC_ERROR_NONE;
}

// Returns a non zero announce integer if we should send a transport window
// update
uint32_t grpc_chttp2_flowctl_maybe_send_transport_update(
    grpc_chttp2_transport_flowctl* tfc) {
  PRETRACE(tfc, NULL);
  uint32_t target_announced_window = grpc_chttp2_target_announced_window(tfc);
  uint32_t threshold_to_send_transport_window_update =
      tfc->t->outbuf.count > 0 ? 3 * target_announced_window / 4
                               : target_announced_window / 2;
  if (tfc->announced_window <= threshold_to_send_transport_window_update &&
      tfc->announced_window != target_announced_window) {
    uint32_t announce = (uint32_t)GPR_CLAMP(
        target_announced_window - tfc->announced_window, 0, UINT32_MAX);
    tfc->announced_window += announce;
    POSTTRACE(tfc, NULL, "t updt sent");
    return announce;
  }
  GRPC_FLOW_CONTROL_IF_TRACING(
      gpr_log(GPR_DEBUG, "%p[0][%s] will not send transport update", tfc,
              tfc->t->is_client ? "cli" : "svr"));
  return 0;
}

// Returns a non zero announce integer if we should send a stream window update
uint32_t grpc_chttp2_flowctl_maybe_send_stream_update(
    grpc_chttp2_transport_flowctl* tfc, grpc_chttp2_stream_flowctl* sfc) {
  PRETRACE(tfc, sfc);
  if (sfc->local_window_delta > sfc->announced_window_delta) {
    uint32_t announce = (uint32_t)GPR_CLAMP(
        sfc->local_window_delta - sfc->announced_window_delta, 0, UINT32_MAX);
    announced_window_delta_preupdate(tfc, sfc);
    sfc->announced_window_delta += announce;
    announced_window_delta_postupdate(tfc, sfc);
    POSTTRACE(tfc, sfc, "s updt sent");
    return announce;
  }
  GRPC_FLOW_CONTROL_IF_TRACING(
      gpr_log(GPR_DEBUG, "%p[%u][%s] will not send stream update", tfc,
              sfc->s->id, tfc->t->is_client ? "cli" : "svr"));
  return 0;
}

// we have received a WINDOW_UPDATE frame for a transport
void grpc_chttp2_flowctl_recv_transport_update(
    grpc_chttp2_transport_flowctl* tfc, uint32_t size) {
  PRETRACE(tfc, NULL);
  tfc->remote_window += size;
  POSTTRACE(tfc, NULL, "t updt recv");
}

// we have received a WINDOW_UPDATE frame for a stream
void grpc_chttp2_flowctl_recv_stream_update(grpc_chttp2_transport_flowctl* tfc,
                                            grpc_chttp2_stream_flowctl* sfc,
                                            uint32_t size) {
  PRETRACE(tfc, sfc);
  sfc->remote_window_delta += size;
  POSTTRACE(tfc, sfc, "s updt recv");
}

void grpc_chttp2_flowctl_incoming_bs_update(grpc_chttp2_transport_flowctl* tfc,
                                            grpc_chttp2_stream_flowctl* sfc,
                                            size_t max_size_hint,
                                            size_t have_already) {
  PRETRACE(tfc, sfc);
  uint32_t max_recv_bytes;
  uint32_t sent_init_window =
      tfc->t->settings[GRPC_SENT_SETTINGS]
                      [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];

  /* clamp max recv hint to an allowable size */
  if (max_size_hint >= UINT32_MAX - sent_init_window) {
    max_recv_bytes = UINT32_MAX - sent_init_window;
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
  GPR_ASSERT(max_recv_bytes <= UINT32_MAX - sent_init_window);
  if (sfc->local_window_delta < max_recv_bytes) {
    uint32_t add_max_recv_bytes =
        (uint32_t)(max_recv_bytes - sfc->local_window_delta);
    sfc->local_window_delta += add_max_recv_bytes;
  }
  POSTTRACE(tfc, sfc, "app st recv");
}

void grpc_chttp2_flowctl_destroy_stream(grpc_chttp2_transport_flowctl* tfc,
                                        grpc_chttp2_stream_flowctl* sfc) {
  announced_window_delta_preupdate(tfc, sfc);
}

grpc_chttp2_flowctl_action grpc_chttp2_flowctl_get_action(
    const grpc_chttp2_transport_flowctl* tfc,
    const grpc_chttp2_stream_flowctl* sfc) {
  grpc_chttp2_flowctl_action action;
  memset(&action, 0, sizeof(action));
  uint32_t target_announced_window = grpc_chttp2_target_announced_window(tfc);
  if (tfc->announced_window < target_announced_window / 2) {
    action.send_transport_update = GRPC_CHTTP2_FLOWCTL_UPDATE_IMMEDIATELY;
  }
  if (sfc != NULL && !sfc->s->read_closed) {
    uint32_t sent_init_window =
        tfc->t->settings[GRPC_SENT_SETTINGS]
                        [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    if ((int64_t)sfc->local_window_delta >
            (int64_t)sfc->announced_window_delta &&
        (int64_t)sfc->announced_window_delta + sent_init_window <=
            sent_init_window / 2) {
      action.send_stream_update = GRPC_CHTTP2_FLOWCTL_UPDATE_IMMEDIATELY;
    } else if (sfc->local_window_delta > sfc->announced_window_delta) {
      action.send_stream_update = GRPC_CHTTP2_FLOWCTL_QUEUE_UPDATE;
    }
  }
  TRACEACTION(action);
  return action;
}
