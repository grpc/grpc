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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/flow_control.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/gpr/string.h"

grpc_core::TraceFlag grpc_flowctl_trace(false, "flowctl");

namespace grpc_core {
namespace chttp2 {

namespace {

static constexpr const int kTracePadding = 30;
static constexpr const uint32_t kMaxWindowUpdateSize = (1u << 31) - 1;

static char* fmt_int64_diff_str(int64_t old_val, int64_t new_val) {
  char* str;
  if (old_val != new_val) {
    gpr_asprintf(&str, "%" PRId64 " -> %" PRId64 "", old_val, new_val);
  } else {
    gpr_asprintf(&str, "%" PRId64 "", old_val);
  }
  char* str_lp = gpr_leftpad(str, ' ', kTracePadding);
  gpr_free(str);
  return str_lp;
}

static char* fmt_uint32_diff_str(uint32_t old_val, uint32_t new_val) {
  char* str;
  if (old_val != new_val) {
    gpr_asprintf(&str, "%" PRIu32 " -> %" PRIu32 "", old_val, new_val);
  } else {
    gpr_asprintf(&str, "%" PRIu32 "", old_val);
  }
  char* str_lp = gpr_leftpad(str, ' ', kTracePadding);
  gpr_free(str);
  return str_lp;
}
}  // namespace

void FlowControlTrace::Init(const char* reason, TransportFlowControl* tfc,
                            StreamFlowControl* sfc) {
  tfc_ = tfc;
  sfc_ = sfc;
  reason_ = reason;
  remote_window_ = tfc->remote_window();
  target_window_ = tfc->target_window();
  announced_window_ = tfc->announced_window();
  if (sfc != nullptr) {
    remote_window_delta_ = sfc->remote_window_delta();
    local_window_delta_ = sfc->local_window_delta();
    announced_window_delta_ = sfc->announced_window_delta();
  }
}

void FlowControlTrace::Finish() {
  uint32_t acked_local_window =
      tfc_->transport()->settings[GRPC_SENT_SETTINGS]
                                 [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
  uint32_t remote_window =
      tfc_->transport()->settings[GRPC_PEER_SETTINGS]
                                 [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
  char* trw_str = fmt_int64_diff_str(remote_window_, tfc_->remote_window());
  char* tlw_str = fmt_int64_diff_str(target_window_, tfc_->target_window());
  char* taw_str =
      fmt_int64_diff_str(announced_window_, tfc_->announced_window());
  char* srw_str;
  char* slw_str;
  char* saw_str;
  if (sfc_ != nullptr) {
    srw_str = fmt_int64_diff_str(remote_window_delta_ + remote_window,
                                 sfc_->remote_window_delta() + remote_window);
    slw_str =
        fmt_int64_diff_str(local_window_delta_ + acked_local_window,
                           sfc_->local_window_delta() + acked_local_window);
    saw_str =
        fmt_int64_diff_str(announced_window_delta_ + acked_local_window,
                           sfc_->announced_window_delta() + acked_local_window);
  } else {
    srw_str = gpr_leftpad("", ' ', kTracePadding);
    slw_str = gpr_leftpad("", ' ', kTracePadding);
    saw_str = gpr_leftpad("", ' ', kTracePadding);
  }
  gpr_log(GPR_DEBUG,
          "%p[%u][%s] | %s | trw:%s, tlw:%s, taw:%s, srw:%s, slw:%s, saw:%s",
          tfc_, sfc_ != nullptr ? sfc_->stream()->id : 0,
          tfc_->transport()->is_client ? "cli" : "svr", reason_, trw_str,
          tlw_str, taw_str, srw_str, slw_str, saw_str);
  gpr_free(trw_str);
  gpr_free(tlw_str);
  gpr_free(taw_str);
  gpr_free(srw_str);
  gpr_free(slw_str);
  gpr_free(saw_str);
}

const char* FlowControlAction::UrgencyString(Urgency u) {
  switch (u) {
    case Urgency::NO_ACTION_NEEDED:
      return "no action";
    case Urgency::UPDATE_IMMEDIATELY:
      return "update immediately";
    case Urgency::QUEUE_UPDATE:
      return "queue update";
    default:
      GPR_UNREACHABLE_CODE(return "unknown");
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

void FlowControlAction::Trace(grpc_chttp2_transport* t) const {
  char* iw_str = fmt_uint32_diff_str(
      t->settings[GRPC_SENT_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
      initial_window_size_);
  char* mf_str = fmt_uint32_diff_str(
      t->settings[GRPC_SENT_SETTINGS][GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE],
      max_frame_size_);
  gpr_log(GPR_DEBUG, "t[%s],  s[%s], iw:%s:%s mf:%s:%s",
          UrgencyString(send_transport_update_),
          UrgencyString(send_stream_update_),
          UrgencyString(send_initial_window_update_), iw_str,
          UrgencyString(send_max_frame_size_update_), mf_str);
  gpr_free(iw_str);
  gpr_free(mf_str);
}

TransportFlowControlDisabled::TransportFlowControlDisabled(
    grpc_chttp2_transport* t) {
  remote_window_ = kMaxWindow;
  target_initial_window_size_ = kMaxWindow;
  announced_window_ = kMaxWindow;
  t->settings[GRPC_PEER_SETTINGS][GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE] =
      kFrameSize;
  t->settings[GRPC_SENT_SETTINGS][GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE] =
      kFrameSize;
  t->settings[GRPC_ACKED_SETTINGS][GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE] =
      kFrameSize;
  t->settings[GRPC_PEER_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE] =
      kMaxWindow;
  t->settings[GRPC_SENT_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE] =
      kMaxWindow;
  t->settings[GRPC_ACKED_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE] =
      kMaxWindow;
}

TransportFlowControl::TransportFlowControl(const grpc_chttp2_transport* t,
                                           bool enable_bdp_probe)
    : t_(t),
      enable_bdp_probe_(enable_bdp_probe),
      bdp_estimator_(t->peer_string),
      pid_controller_(grpc_core::PidController::Args()
                          .set_gain_p(4)
                          .set_gain_i(8)
                          .set_gain_d(0)
                          .set_initial_control_value(TargetLogBdp())
                          .set_min_control_value(-1)
                          .set_max_control_value(25)
                          .set_integral_range(10)),
      last_pid_update_(grpc_core::ExecCtx::Get()->Now()) {}

uint32_t TransportFlowControl::MaybeSendUpdate(bool writing_anyway) {
  FlowControlTrace trace("t updt sent", this, nullptr);
  const uint32_t target_announced_window =
      static_cast<uint32_t>(target_window());
  if ((writing_anyway || announced_window_ <= target_announced_window / 2) &&
      announced_window_ != target_announced_window) {
    const uint32_t announce = static_cast<uint32_t> GPR_CLAMP(
        target_announced_window - announced_window_, 0, kMaxWindowUpdateSize);
    announced_window_ += announce;
    return announce;
  }
  return 0;
}

grpc_error* TransportFlowControl::ValidateRecvData(
    int64_t incoming_frame_size) {
  if (incoming_frame_size > announced_window_) {
    char* msg;
    gpr_asprintf(&msg,
                 "frame of size %" PRId64 " overflows local window of %" PRId64,
                 incoming_frame_size, announced_window_);
    grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return err;
  }
  return GRPC_ERROR_NONE;
}

StreamFlowControl::StreamFlowControl(TransportFlowControl* tfc,
                                     const grpc_chttp2_stream* s)
    : tfc_(tfc), s_(s) {}

grpc_error* StreamFlowControl::RecvData(int64_t incoming_frame_size) {
  FlowControlTrace trace("  data recv", tfc_, this);

  grpc_error* error = GRPC_ERROR_NONE;
  error = tfc_->ValidateRecvData(incoming_frame_size);
  if (error != GRPC_ERROR_NONE) return error;

  uint32_t sent_init_window =
      tfc_->transport()->settings[GRPC_SENT_SETTINGS]
                                 [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
  uint32_t acked_init_window =
      tfc_->transport()->settings[GRPC_ACKED_SETTINGS]
                                 [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];

  int64_t acked_stream_window = announced_window_delta_ + acked_init_window;
  int64_t sent_stream_window = announced_window_delta_ + sent_init_window;
  if (incoming_frame_size > acked_stream_window) {
    if (incoming_frame_size <= sent_stream_window) {
      gpr_log(GPR_ERROR,
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
      gpr_asprintf(
          &msg, "frame of size %" PRId64 " overflows local window of %" PRId64,
          incoming_frame_size, acked_stream_window);
      grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
      gpr_free(msg);
      return err;
    }
  }

  UpdateAnnouncedWindowDelta(tfc_, -incoming_frame_size);
  local_window_delta_ -= incoming_frame_size;
  tfc_->CommitRecvData(incoming_frame_size);
  return GRPC_ERROR_NONE;
}

uint32_t StreamFlowControl::MaybeSendUpdate() {
  FlowControlTrace trace("s updt sent", tfc_, this);
  if (local_window_delta_ > announced_window_delta_) {
    uint32_t announce = static_cast<uint32_t> GPR_CLAMP(
        local_window_delta_ - announced_window_delta_, 0, kMaxWindowUpdateSize);
    UpdateAnnouncedWindowDelta(tfc_, announce);
    return announce;
  }
  return 0;
}

void StreamFlowControl::IncomingByteStreamUpdate(size_t max_size_hint,
                                                 size_t have_already) {
  FlowControlTrace trace("app st recv", tfc_, this);
  uint32_t max_recv_bytes;
  uint32_t sent_init_window =
      tfc_->transport()->settings[GRPC_SENT_SETTINGS]
                                 [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];

  /* clamp max recv hint to an allowable size */
  if (max_size_hint >= kMaxWindowUpdateSize - sent_init_window) {
    max_recv_bytes = kMaxWindowUpdateSize - sent_init_window;
  } else {
    max_recv_bytes = static_cast<uint32_t>(max_size_hint);
  }

  /* account for bytes already received but unknown to higher layers */
  if (max_recv_bytes >= have_already) {
    max_recv_bytes -= static_cast<uint32_t>(have_already);
  } else {
    max_recv_bytes = 0;
  }

  /* add some small lookahead to keep pipelines flowing */
  GPR_DEBUG_ASSERT(max_recv_bytes <= kMaxWindowUpdateSize - sent_init_window);
  if (local_window_delta_ < max_recv_bytes) {
    uint32_t add_max_recv_bytes =
        static_cast<uint32_t>(max_recv_bytes - local_window_delta_);
    local_window_delta_ += add_max_recv_bytes;
  }
}

// Take in a target and modifies it based on the memory pressure of the system
static double AdjustForMemoryPressure(grpc_resource_quota* quota,
                                      double target) {
  // do not increase window under heavy memory pressure.
  double memory_pressure = grpc_resource_quota_get_memory_pressure(quota);
  static const double kLowMemPressure = 0.1;
  static const double kZeroTarget = 22;
  static const double kHighMemPressure = 0.8;
  static const double kMaxMemPressure = 0.9;
  if (memory_pressure < kLowMemPressure && target < kZeroTarget) {
    target = (target - kZeroTarget) * memory_pressure / kLowMemPressure +
             kZeroTarget;
  } else if (memory_pressure > kHighMemPressure) {
    target *= 1 - GPR_MIN(1, (memory_pressure - kHighMemPressure) /
                                 (kMaxMemPressure - kHighMemPressure));
  }
  return target;
}

double TransportFlowControl::TargetLogBdp() {
  return AdjustForMemoryPressure(
      grpc_resource_user_quota(grpc_endpoint_get_resource_user(t_->ep)),
      1 + log2(bdp_estimator_.EstimateBdp()));
}

double TransportFlowControl::SmoothLogBdp(double value) {
  grpc_millis now = grpc_core::ExecCtx::Get()->Now();
  double bdp_error = value - pid_controller_.last_control_value();
  const double dt = static_cast<double>(now - last_pid_update_) * 1e-3;
  last_pid_update_ = now;
  // Limit dt to 100ms
  const double kMaxDt = 0.1;
  return pid_controller_.Update(bdp_error, dt > kMaxDt ? kMaxDt : dt);
}

FlowControlAction::Urgency TransportFlowControl::DeltaUrgency(
    int64_t value, grpc_chttp2_setting_id setting_id) {
  int64_t delta = value - static_cast<int64_t>(
                              t_->settings[GRPC_LOCAL_SETTINGS][setting_id]);
  // TODO(ncteisen): tune this
  if (delta != 0 && (delta <= -value / 5 || delta >= value / 5)) {
    return FlowControlAction::Urgency::QUEUE_UPDATE;
  } else {
    return FlowControlAction::Urgency::NO_ACTION_NEEDED;
  }
}

FlowControlAction TransportFlowControl::PeriodicUpdate() {
  FlowControlAction action;
  if (enable_bdp_probe_) {
    // get bdp estimate and update initial_window accordingly.
    // target might change based on how much memory pressure we are under
    // TODO(ncteisen): experiment with setting target to be huge under low
    // memory pressure.
    const double target = pow(2, SmoothLogBdp(TargetLogBdp()));

    // Though initial window 'could' drop to 0, we keep the floor at 128
    target_initial_window_size_ =
        static_cast<int32_t> GPR_CLAMP(target, 128, INT32_MAX);

    action.set_send_initial_window_update(
        DeltaUrgency(target_initial_window_size_,
                     GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE),
        static_cast<uint32_t>(target_initial_window_size_));

    // get bandwidth estimate and update max_frame accordingly.
    double bw_dbl = bdp_estimator_.EstimateBandwidth();
    // we target the max of BDP or bandwidth in microseconds.
    int32_t frame_size = static_cast<int32_t> GPR_CLAMP(
        GPR_MAX((int32_t)GPR_CLAMP(bw_dbl, 0, INT_MAX) / 1000,
                target_initial_window_size_),
        16384, 16777215);
    action.set_send_max_frame_size_update(
        DeltaUrgency(static_cast<int64_t>(frame_size),
                     GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE),
        frame_size);
  }
  return UpdateAction(action);
}

FlowControlAction StreamFlowControl::UpdateAction(FlowControlAction action) {
  // TODO(ncteisen): tune this
  if (!s_->read_closed) {
    uint32_t sent_init_window =
        tfc_->transport()->settings[GRPC_SENT_SETTINGS]
                                   [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    if (local_window_delta_ > announced_window_delta_ &&
        announced_window_delta_ + sent_init_window <= sent_init_window / 2) {
      action.set_send_stream_update(
          FlowControlAction::Urgency::UPDATE_IMMEDIATELY);
    } else if (local_window_delta_ > announced_window_delta_) {
      action.set_send_stream_update(FlowControlAction::Urgency::QUEUE_UPDATE);
    }
  }

  return action;
}

}  // namespace chttp2
}  // namespace grpc_core
