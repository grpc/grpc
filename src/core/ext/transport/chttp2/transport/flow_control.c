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

#include <math.h>
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
  uint32_t local_init_window;
  uint32_t local_max_frame;
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

#define TRACE_PADDING 30

static char* fmt_int64_diff_str(int64_t old, int64_t new) {
  char* str;
  if (old != new) {
    gpr_asprintf(&str, "%" PRId64 " -> %" PRId64 "", old, new);
  } else {
    gpr_asprintf(&str, "%" PRId64 "", old);
  }
  char* str_lp = gpr_leftpad(str, ' ', TRACE_PADDING);
  gpr_free(str);
  return str_lp;
}

static char* fmt_uint32_diff_str(uint32_t old, uint32_t new) {
  char* str;
  if (new > 0 && old != new) {
    gpr_asprintf(&str, "%" PRIu32 " -> %" PRIu32 "", old, new);
  } else {
    gpr_asprintf(&str, "%" PRIu32 "", old);
  }
  char* str_lp = gpr_leftpad(str, ' ', TRACE_PADDING);
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
  char* trw_str =
      fmt_int64_diff_str(shadow_fc->remote_window, tfc->remote_window);
  char* tlw_str = fmt_int64_diff_str(shadow_fc->target_window,
                                     grpc_chttp2_target_announced_window(tfc));
  char* taw_str =
      fmt_int64_diff_str(shadow_fc->announced_window, tfc->announced_window);
  char* srw_str;
  char* slw_str;
  char* saw_str;
  if (sfc != NULL) {
    srw_str = fmt_int64_diff_str(shadow_fc->remote_window_delta + remote_window,
                                 sfc->remote_window_delta + remote_window);
    slw_str =
        fmt_int64_diff_str(shadow_fc->local_window_delta + acked_local_window,
                           sfc->local_window_delta + acked_local_window);
    saw_str = fmt_int64_diff_str(
        shadow_fc->announced_window_delta + acked_local_window,
        sfc->announced_window_delta + acked_local_window);
  } else {
    srw_str = gpr_leftpad("", ' ', TRACE_PADDING);
    slw_str = gpr_leftpad("", ' ', TRACE_PADDING);
    saw_str = gpr_leftpad("", ' ', TRACE_PADDING);
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

static void trace_action(grpc_chttp2_transport_flowctl* tfc,
                         grpc_chttp2_flowctl_action action) {
  char* iw_str = fmt_uint32_diff_str(
      tfc->t->settings[GRPC_SENT_SETTINGS]
                      [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
      action.initial_window_size);
  char* mf_str = fmt_uint32_diff_str(
      tfc->t->settings[GRPC_SENT_SETTINGS][GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE],
      action.max_frame_size);
  gpr_log(GPR_DEBUG, "t[%s],  s[%s], settings[%s] iw:%s mf:%s",
          urgency_to_string(action.send_transport_update),
          urgency_to_string(action.send_stream_update),
          urgency_to_string(action.send_setting_update), iw_str, mf_str);
  gpr_free(iw_str);
  gpr_free(mf_str);
}

#define PRETRACE(tfc, sfc)       \
  shadow_flow_control shadow_fc; \
  GRPC_FLOW_CONTROL_IF_TRACING(pretrace(&shadow_fc, tfc, sfc))
#define POSTTRACE(tfc, sfc, reason) \
  GRPC_FLOW_CONTROL_IF_TRACING(posttrace(&shadow_fc, tfc, sfc, reason))
#define TRACEACTION(tfc, action) \
  GRPC_FLOW_CONTROL_IF_TRACING(trace_action(tfc, action))
#else
#define PRETRACE(tfc, sfc)
#define POSTTRACE(tfc, sfc, reason)
#define TRACEACTION(tfc, action)
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

// Returns an urgency with which to make an update
static grpc_chttp2_flowctl_urgency delta_is_significant(
    const grpc_chttp2_transport_flowctl* tfc, int32_t value,
    grpc_chttp2_setting_id setting_id) {
  int64_t delta = (int64_t)value -
                  (int64_t)tfc->t->settings[GRPC_LOCAL_SETTINGS][setting_id];
  // TODO(ncteisen): tune this
  if (delta != 0 && (delta <= -value / 5 || delta >= value / 5)) {
    return GRPC_CHTTP2_FLOWCTL_QUEUE_UPDATE;
  } else {
    return GRPC_CHTTP2_FLOWCTL_NO_ACTION_NEEDED;
  }
}

// Takes in a target and uses the pid controller to return a stabilized
// guess at the new bdp.
static double get_pid_controller_guess(grpc_chttp2_transport_flowctl* tfc,
                                       double target) {
  double bdp_error = target - grpc_pid_controller_last(&tfc->pid_controller);
  gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
  gpr_timespec dt_timespec = gpr_time_sub(now, tfc->last_pid_update);
  double dt = (double)dt_timespec.tv_sec + dt_timespec.tv_nsec * 1e-9;
  if (dt > 0.1) {
    dt = 0.1;
  }
  double log2_bdp_guess =
      grpc_pid_controller_update(&tfc->pid_controller, bdp_error, dt);
  tfc->last_pid_update = now;
  return pow(2, log2_bdp_guess);
}

// Take in a target and modifies it based on the memory pressure of the system
static double get_target_under_memory_pressure(
    grpc_chttp2_transport_flowctl* tfc, double target) {
  // do not increase window under heavy memory pressure.
  double memory_pressure = grpc_resource_quota_get_memory_pressure(
      grpc_resource_user_quota(grpc_endpoint_get_resource_user(tfc->t->ep)));
  if (memory_pressure > 0.8) {
    target *= 1 - GPR_MIN(1, (memory_pressure - 0.8) / 0.1);
  }
  return target;
}

grpc_chttp2_flowctl_action grpc_chttp2_flowctl_get_action(
    grpc_chttp2_transport_flowctl* tfc, grpc_chttp2_stream_flowctl* sfc) {
  grpc_chttp2_flowctl_action action;
  memset(&action, 0, sizeof(action));
  uint32_t target_announced_window = grpc_chttp2_target_announced_window(tfc);
  if (tfc->announced_window < target_announced_window / 2) {
    action.send_transport_update = GRPC_CHTTP2_FLOWCTL_UPDATE_IMMEDIATELY;
  }
  // TODO(ncteisen): tune this
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
  TRACEACTION(tfc, action);
  return action;
}

grpc_chttp2_flowctl_action grpc_chttp2_flowctl_get_bdp_action(
    grpc_chttp2_transport_flowctl* tfc) {
  grpc_chttp2_flowctl_action action;
  memset(&action, 0, sizeof(action));
  if (tfc->enable_bdp_probe) {
    action.need_ping = grpc_bdp_estimator_need_ping(&tfc->bdp_estimator);

    // get bdp estimate and update initial_window accordingly.
    int64_t estimate = -1;
    int32_t bdp = -1;
    if (grpc_bdp_estimator_get_estimate(&tfc->bdp_estimator, &estimate)) {
      double target = 1 + log2((double)estimate);

      // target might change based on how much memory pressure we are under
      // TODO(ncteisen): experiment with setting target to be huge under low
      // memory pressure.
      target = get_target_under_memory_pressure(tfc, target);

      // run our target through the pid controller to stabilize change.
      // TODO(ncteisen): experiment with other controllers here.
      double bdp_guess = get_pid_controller_guess(tfc, target);

      // Though initial window 'could' drop to 0, we keep the floor at 128
      bdp = GPR_MAX((int32_t)bdp_guess, 128);

      grpc_chttp2_flowctl_urgency init_window_update_urgency =
          delta_is_significant(tfc, bdp,
                               GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
      if (init_window_update_urgency != GRPC_CHTTP2_FLOWCTL_NO_ACTION_NEEDED) {
        action.send_setting_update = init_window_update_urgency;
        action.initial_window_size = (uint32_t)bdp;
      }
    }

    // get bandwidth estimate and update max_frame accordingly.
    double bw_dbl = -1;
    if (grpc_bdp_estimator_get_bw(&tfc->bdp_estimator, &bw_dbl)) {
      // we target the max of BDP or bandwidth in microseconds.
      int32_t frame_size =
          GPR_CLAMP(GPR_MAX((int32_t)bw_dbl / 1000, bdp), 16384, 16777215);
      grpc_chttp2_flowctl_urgency frame_size_urgency = delta_is_significant(
          tfc, frame_size, GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE);
      if (frame_size_urgency != GRPC_CHTTP2_FLOWCTL_NO_ACTION_NEEDED) {
        if (frame_size_urgency > action.send_setting_update) {
          action.send_setting_update = frame_size_urgency;
        }
        action.max_frame_size = (uint32_t)frame_size;
      }
    }
  }

  TRACEACTION(tfc, action);
  return action;
}
