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

#include <algorithm>
#include <cmath>
#include <ostream>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/memory_quota.h"

grpc_core::TraceFlag grpc_flowctl_trace(false, "flowctl");

namespace grpc_core {
namespace chttp2 {

TestOnlyTransportTargetWindowEstimatesMocker*
    g_test_only_transport_target_window_estimates_mocker;

bool g_test_only_transport_flow_control_window_check;

namespace {

constexpr const int64_t kMaxWindowUpdateSize = (1u << 31) - 1;

}  // namespace

const char* FlowControlAction::UrgencyString(Urgency u) {
  switch (u) {
    case Urgency::NO_ACTION_NEEDED:
      return "no-action";
    case Urgency::UPDATE_IMMEDIATELY:
      return "now";
    case Urgency::QUEUE_UPDATE:
      return "queue";
    default:
      GPR_UNREACHABLE_CODE(return "unknown");
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

std::ostream& operator<<(std::ostream& out, FlowControlAction::Urgency u) {
  return out << FlowControlAction::UrgencyString(u);
}

std::string FlowControlAction::DebugString() const {
  std::vector<std::string> segments;
  if (send_transport_update_ != Urgency::NO_ACTION_NEEDED) {
    segments.push_back(
        absl::StrCat("t:", UrgencyString(send_transport_update_)));
  }
  if (send_stream_update_ != Urgency::NO_ACTION_NEEDED) {
    segments.push_back(absl::StrCat("s:", UrgencyString(send_stream_update_)));
  }
  if (send_initial_window_update_ != Urgency::NO_ACTION_NEEDED) {
    segments.push_back(
        absl::StrCat("iw=", initial_window_size_, ":",
                     UrgencyString(send_initial_window_update_)));
  }
  if (send_max_frame_size_update_ != Urgency::NO_ACTION_NEEDED) {
    segments.push_back(
        absl::StrCat("mf=", max_frame_size_, ":",
                     UrgencyString(send_max_frame_size_update_)));
  }
  if (segments.empty()) return "no action";
  return absl::StrJoin(segments, ",");
}

std::ostream& operator<<(std::ostream& out, const FlowControlAction& action) {
  return out << action.DebugString();
}

TransportFlowControl::TransportFlowControl(const char* name,
                                           bool enable_bdp_probe,
                                           MemoryOwner* memory_owner)
    : memory_owner_(memory_owner),
      enable_bdp_probe_(enable_bdp_probe),
      bdp_estimator_(name),
      pid_controller_(PidController::Args()
                          .set_gain_p(4)
                          .set_gain_i(8)
                          .set_gain_d(0)
                          .set_initial_control_value(TargetLogBdp())
                          .set_min_control_value(-1)
                          .set_max_control_value(25)
                          .set_integral_range(10)),
      last_pid_update_(ExecCtx::Get()->Now()) {}

uint32_t TransportFlowControl::MaybeSendUpdate(bool writing_anyway) {
  const uint32_t target_announced_window =
      static_cast<uint32_t>(target_window());
  if ((writing_anyway || announced_window_ <= target_announced_window / 2) &&
      announced_window_ != target_announced_window) {
    const uint32_t announce =
        static_cast<uint32_t>(Clamp(target_announced_window - announced_window_,
                                    int64_t(0), kMaxWindowUpdateSize));
    announced_window_ += announce;
    return announce;
  }
  return 0;
}

absl::Status TransportFlowControl::ValidateRecvData(
    int64_t incoming_frame_size) {
  if (incoming_frame_size > announced_window_) {
    return absl::InternalError(absl::StrFormat(
        "frame of size %" PRId64 " overflows local window of %" PRId64,
        incoming_frame_size, announced_window_));
  }
  return absl::OkStatus();
}

void TransportFlowControl::CommitRecvData(int64_t incoming_frame_size) {
  announced_window_ -= incoming_frame_size;
}

StreamFlowControl::StreamFlowControl(TransportFlowControl* tfc) : tfc_(tfc) {}

absl::Status StreamFlowControl::RecvData(int64_t incoming_frame_size) {
  absl::Status error = tfc_->ValidateRecvData(incoming_frame_size);
  if (!error.ok()) return error;

  int64_t acked_stream_window =
      announced_window_delta_ + tfc_->acked_init_window();
  if (incoming_frame_size > acked_stream_window) {
    return absl::InternalError(absl::StrFormat(
        "frame of size %" PRId64 " overflows local window of %" PRId64,
        incoming_frame_size, acked_stream_window));
  }

  UpdateAnnouncedWindowDelta(tfc_, -incoming_frame_size);
  local_window_delta_ -= incoming_frame_size;
  min_progress_size_ -=
      std::min(static_cast<int64_t>(min_progress_size_), incoming_frame_size);
  tfc_->CommitRecvData(incoming_frame_size);
  return absl::OkStatus();
}

uint32_t StreamFlowControl::MaybeSendUpdate() {
  // If a recently sent settings frame caused the stream's flow control window
  // to go in the negative (or < min_progress_size_), update the delta.
  // In this case, we want to make sure that bytes are still flowing.
  UpdateProgress(min_progress_size_);
  if (local_window_delta_ > announced_window_delta_) {
    uint32_t announce = static_cast<uint32_t>(
        Clamp(local_window_delta_ - announced_window_delta_, int64_t(0),
              kMaxWindowUpdateSize));
    UpdateAnnouncedWindowDelta(tfc_, announce);
    return announce;
  }
  return 0;
}

void StreamFlowControl::UpdateProgress(uint32_t min_progress_size) {
  uint32_t max_recv_bytes;

  min_progress_size_ = min_progress_size;

  /* clamp max recv hint to an allowable size */
  if (min_progress_size >= kMaxWindowDelta) {
    max_recv_bytes = kMaxWindowDelta;
  } else {
    max_recv_bytes = static_cast<uint32_t>(min_progress_size);
  }

  /* add some small lookahead to keep pipelines flowing */
  GPR_DEBUG_ASSERT(max_recv_bytes <=
                   kMaxWindowUpdateSize - tfc_->sent_init_window());
  if (local_window_delta_ < max_recv_bytes) {
    uint32_t add_max_recv_bytes =
        static_cast<uint32_t>(max_recv_bytes - local_window_delta_);
    local_window_delta_ += add_max_recv_bytes;
  }
}

absl::Status TransportFlowControl::RecvData(int64_t incoming_frame_size) {
  absl::Status error = ValidateRecvData(incoming_frame_size);
  if (error.ok()) return error;
  CommitRecvData(incoming_frame_size);
  return absl::OkStatus();
}

void TransportFlowControl::RecvUpdate(uint32_t size) { remote_window_ += size; }

int64_t TransportFlowControl::target_window() const {
  // See comment above announced_stream_total_over_incoming_window_ for the
  // logic behind this decision.
  return static_cast<uint32_t>(
      std::min(static_cast<int64_t>((1u << 31) - 1),
               announced_stream_total_over_incoming_window_ +
                   target_initial_window_size_));
}

FlowControlAction TransportFlowControl::UpdateAction(FlowControlAction action) {
  if (announced_window_ < target_window() / 2) {
    action.set_send_transport_update(
        FlowControlAction::Urgency::UPDATE_IMMEDIATELY);
  }
  return action;
}

// Take in a target and modifies it based on the memory pressure of the system
static double AdjustForMemoryPressure(double memory_pressure, double target) {
  // do not increase window under heavy memory pressure.
  static const double kLowMemPressure = 0.1;
  static const double kZeroTarget = 22;
  static const double kHighMemPressure = 0.8;
  static const double kMaxMemPressure = 0.9;
  if (memory_pressure < kLowMemPressure && target < kZeroTarget) {
    target = (target - kZeroTarget) * memory_pressure / kLowMemPressure +
             kZeroTarget;
  } else if (memory_pressure > kHighMemPressure) {
    target *= 1 - std::min(1.0, (memory_pressure - kHighMemPressure) /
                                    (kMaxMemPressure - kHighMemPressure));
  }
  return target;
}

double TransportFlowControl::TargetLogBdp() {
  return AdjustForMemoryPressure(
      memory_owner_->is_valid() ? memory_owner_->InstantaneousPressure() : 0.0,
      1 + log2(bdp_estimator_.EstimateBdp()));
}

double TransportFlowControl::SmoothLogBdp(double value) {
  Timestamp now = ExecCtx::Get()->Now();
  double bdp_error = value - pid_controller_.last_control_value();
  const double dt = (now - last_pid_update_).seconds();
  last_pid_update_ = now;
  // Limit dt to 100ms
  const double kMaxDt = 0.1;
  return pid_controller_.Update(bdp_error, dt > kMaxDt ? kMaxDt : dt);
}

void TransportFlowControl::UpdateSetting(
    int64_t* desired_value, int64_t new_desired_value,
    FlowControlAction* action,
    FlowControlAction& (FlowControlAction::*set)(FlowControlAction::Urgency,
                                                 uint32_t)) {
  int64_t delta = new_desired_value - *desired_value;
  // TODO(ncteisen): tune this
  if (delta != 0 &&
      (delta <= -*desired_value / 5 || delta >= *desired_value / 5)) {
    *desired_value = new_desired_value;
    (action->*set)(FlowControlAction::Urgency::QUEUE_UPDATE, *desired_value);
  }
}

FlowControlAction TransportFlowControl::PeriodicUpdate() {
  FlowControlAction action;
  if (enable_bdp_probe_) {
    // get bdp estimate and update initial_window accordingly.
    // target might change based on how much memory pressure we are under
    // TODO(ncteisen): experiment with setting target to be huge under low
    // memory pressure.
    double target = pow(2, SmoothLogBdp(TargetLogBdp()));
    if (g_test_only_transport_target_window_estimates_mocker != nullptr) {
      // Hook for simulating unusual flow control situations in tests.
      target = g_test_only_transport_target_window_estimates_mocker
                   ->ComputeNextTargetInitialWindowSizeFromPeriodicUpdate(
                       target_initial_window_size_ /* current target */);
    }
    // Though initial window 'could' drop to 0, we keep the floor at
    // kMinInitialWindowSize
    UpdateSetting(
        &target_initial_window_size_,
        static_cast<int32_t>(Clamp(target, double(kMinInitialWindowSize),
                                   double(kMaxInitialWindowSize))),
        &action, &FlowControlAction::set_send_initial_window_update);

    // get bandwidth estimate and update max_frame accordingly.
    double bw_dbl = bdp_estimator_.EstimateBandwidth();
    // we target the max of BDP or bandwidth in microseconds.
    UpdateSetting(
        &target_frame_size_,
        static_cast<int32_t>(Clamp(
            std::max(static_cast<int32_t>(Clamp(bw_dbl, 0.0, double(INT_MAX))) /
                         1000,
                     static_cast<int32_t>(target_initial_window_size_)),
            16384, 16777215)),
        &action, &FlowControlAction::set_send_max_frame_size_update);
  }
  return UpdateAction(action);
}

FlowControlAction StreamFlowControl::UpdateAction(FlowControlAction action) {
  const uint32_t sent_init_window = tfc_->sent_init_window();
  if (local_window_delta_ > announced_window_delta_ &&
      announced_window_delta_ + sent_init_window <= sent_init_window / 2) {
    action.set_send_stream_update(
        FlowControlAction::Urgency::UPDATE_IMMEDIATELY);
  } else if (local_window_delta_ > announced_window_delta_) {
    action.set_send_stream_update(FlowControlAction::Urgency::QUEUE_UPDATE);
  }

  return action;
}

void StreamFlowControl::UpdateAnnouncedWindowDelta(TransportFlowControl* tfc,
                                                   int64_t change) {
  tfc->PreUpdateAnnouncedWindowOverIncomingWindow(announced_window_delta_);
  announced_window_delta_ += change;
  tfc->PostUpdateAnnouncedWindowOverIncomingWindow(announced_window_delta_);
}

void StreamFlowControl::SentData(int64_t outgoing_frame_size) {
  tfc_->StreamSentData(outgoing_frame_size);
  remote_window_delta_ -= outgoing_frame_size;
}

}  // namespace chttp2
}  // namespace grpc_core
