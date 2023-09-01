//
//
// Copyright 2017 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/flow_control.h"

#include <inttypes.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <ostream>
#include <string>
#include <tuple>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/resource_quota/memory_quota.h"

grpc_core::TraceFlag grpc_flowctl_trace(false, "flowctl");

namespace grpc_core {
namespace chttp2 {

TestOnlyTransportTargetWindowEstimatesMocker*
    g_test_only_transport_target_window_estimates_mocker;

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

TransportFlowControl::TransportFlowControl(absl::string_view name,
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
      last_pid_update_(Timestamp::Now()) {}

uint32_t TransportFlowControl::MaybeSendUpdate(bool writing_anyway) {
  const uint32_t target_announced_window =
      static_cast<uint32_t>(target_window());
  if ((writing_anyway || announced_window_ <= target_announced_window / 2) &&
      announced_window_ != target_announced_window) {
    const uint32_t announce =
        static_cast<uint32_t>(Clamp(target_announced_window - announced_window_,
                                    int64_t{0}, kMaxWindowUpdateSize));
    announced_window_ += announce;
    return announce;
  }
  return 0;
}

StreamFlowControl::StreamFlowControl(TransportFlowControl* tfc) : tfc_(tfc) {}

absl::Status StreamFlowControl::IncomingUpdateContext::RecvData(
    int64_t incoming_frame_size) {
  return tfc_upd_.RecvData(incoming_frame_size, [this, incoming_frame_size]() {
    int64_t acked_stream_window =
        sfc_->announced_window_delta_ + sfc_->tfc_->acked_init_window();
    if (incoming_frame_size > acked_stream_window) {
      return absl::InternalError(absl::StrFormat(
          "frame of size %" PRId64 " overflows local window of %" PRId64,
          incoming_frame_size, acked_stream_window));
    }

    tfc_upd_.UpdateAnnouncedWindowDelta(&sfc_->announced_window_delta_,
                                        -incoming_frame_size);
    sfc_->min_progress_size_ -=
        std::min(sfc_->min_progress_size_, incoming_frame_size);
    return absl::OkStatus();
  });
}

absl::Status TransportFlowControl::IncomingUpdateContext::RecvData(
    int64_t incoming_frame_size, absl::FunctionRef<absl::Status()> stream) {
  if (incoming_frame_size > tfc_->announced_window_) {
    return absl::InternalError(absl::StrFormat(
        "frame of size %" PRId64 " overflows local window of %" PRId64,
        incoming_frame_size, tfc_->announced_window_));
  }
  absl::Status error = stream();
  if (!error.ok()) return error;
  tfc_->announced_window_ -= incoming_frame_size;
  return absl::OkStatus();
}

int64_t TransportFlowControl::target_window() const {
  // See comment above announced_stream_total_over_incoming_window_ for the
  // logic behind this decision.
  return static_cast<uint32_t>(
      std::min(static_cast<int64_t>((1u << 31) - 1),
               announced_stream_total_over_incoming_window_ +
                   target_initial_window_size_));
}

FlowControlAction TransportFlowControl::UpdateAction(FlowControlAction action) {
  const int64_t target = target_window();
  // round up so that one byte targets are sent.
  const int64_t send_threshold = (target + 1) / 2;
  if (announced_window_ < send_threshold) {
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
      memory_owner_->is_valid()
          ? memory_owner_->GetPressureInfo().pressure_control_value
          : 0.0,
      1 + log2(bdp_estimator_.EstimateBdp()));
}

double TransportFlowControl::SmoothLogBdp(double value) {
  Timestamp now = Timestamp::Now();
  double bdp_error = value - pid_controller_.last_control_value();
  const double dt = (now - last_pid_update_).seconds();
  last_pid_update_ = now;
  // Limit dt to 100ms
  const double kMaxDt = 0.1;
  return pid_controller_.Update(bdp_error, dt > kMaxDt ? kMaxDt : dt);
}

double
TransportFlowControl::TargetInitialWindowSizeBasedOnMemoryPressureAndBdp()
    const {
  const double bdp = bdp_estimator_.EstimateBdp() * 2.0;
  const double memory_pressure =
      memory_owner_->GetPressureInfo().pressure_control_value;
  // Linear interpolation between two values.
  // Given a line segment between the two points (t_min, a), and (t_max, b),
  // and a value t such that t_min <= t <= t_max, return the value on the line
  // segment at t.
  auto lerp = [](double t, double t_min, double t_max, double a, double b) {
    return a + (b - a) * (t - t_min) / (t_max - t_min);
  };
  // We split memory pressure into three broad regions:
  // 1. Low memory pressure, the "anything goes" case - we assume no memory
  //    pressure concerns and advertise a huge window to keep things flowing.
  // 2. Moderate memory pressure, the "adjust to BDP" case - we linearly ramp
  //    down window size to 2*BDP - which should still allow bytes to flow, but
  //    is arguably more considered.
  // 3. High memory pressure - past 50% we linearly ramp down window size from
  //    BDP to 0 - at which point senders effectively must request to send bytes
  //    to us.
  //
  //          ▲
  //          │
  //  4mb ────┤---------x----
  //          │              -----
  //  BDP ────┤                   ----x---
  //          │                           ----
  //          │                               -----
  //          │                                    ----
  //          │                                        -----
  //          │                                             ---x
  //          ├─────────┬─────────────┬────────────────────────┬─────►
  //          │Anything │Adjust to    │Drop to zero            │
  //          │Goes     │BDP          │                        │
  //          0%        20%           50%                      100% memory
  //                                                                pressure
  const double kAnythingGoesPressure = 0.2;
  const double kAdjustedToBdpPressure = 0.5;
  const double kOneMegabyte = 1024.0 * 1024.0;
  const double kAnythingGoesWindow = std::max(4.0 * kOneMegabyte, bdp);
  if (memory_pressure < kAnythingGoesPressure) {
    return kAnythingGoesWindow;
  } else if (memory_pressure < kAdjustedToBdpPressure) {
    return lerp(memory_pressure, kAnythingGoesPressure, kAdjustedToBdpPressure,
                kAnythingGoesWindow, bdp);
  } else if (memory_pressure < 1.0) {
    return lerp(memory_pressure, kAdjustedToBdpPressure, 1.0, bdp, 0);
  } else {
    return 0;
  }
}

void TransportFlowControl::UpdateSetting(
    grpc_chttp2_setting_id id, int64_t* desired_value,
    uint32_t new_desired_value, FlowControlAction* action,
    FlowControlAction& (FlowControlAction::*set)(FlowControlAction::Urgency,
                                                 uint32_t)) {
  new_desired_value =
      Clamp(new_desired_value, grpc_chttp2_settings_parameters[id].min_value,
            grpc_chttp2_settings_parameters[id].max_value);
  if (new_desired_value != *desired_value) {
    if (grpc_flowctl_trace.enabled()) {
      gpr_log(GPR_INFO, "[flowctl] UPDATE SETTING %s from %" PRId64 " to %d",
              grpc_chttp2_settings_parameters[id].name, *desired_value,
              new_desired_value);
    }
    // Reaching zero can only happen for initial window size, and if it occurs
    // we really want to wake up writes and ensure all the queued stream
    // window updates are flushed, since stream flow control operates
    // differently at zero window size.
    FlowControlAction::Urgency urgency =
        FlowControlAction::Urgency::QUEUE_UPDATE;
    if (*desired_value == 0 || new_desired_value == 0) {
      urgency = FlowControlAction::Urgency::UPDATE_IMMEDIATELY;
    }
    *desired_value = new_desired_value;
    (action->*set)(urgency, *desired_value);
  }
}

FlowControlAction TransportFlowControl::SetAckedInitialWindow(uint32_t value) {
  acked_init_window_ = value;
  FlowControlAction action;
  if (acked_init_window_ != target_initial_window_size_) {
    FlowControlAction::Urgency urgency =
        FlowControlAction::Urgency::QUEUE_UPDATE;
    if (acked_init_window_ == 0 || target_initial_window_size_ == 0) {
      urgency = FlowControlAction::Urgency::UPDATE_IMMEDIATELY;
    }
    action.set_send_initial_window_update(urgency, target_initial_window_size_);
  }
  return action;
}

FlowControlAction TransportFlowControl::PeriodicUpdate() {
  FlowControlAction action;
  if (enable_bdp_probe_) {
    // get bdp estimate and update initial_window accordingly.
    // target might change based on how much memory pressure we are under
    // TODO(ncteisen): experiment with setting target to be huge under low
    // memory pressure.
    uint32_t target = static_cast<uint32_t>(RoundUpToPowerOf2(
        Clamp(IsMemoryPressureControllerEnabled()
                  ? TargetInitialWindowSizeBasedOnMemoryPressureAndBdp()
                  : pow(2, SmoothLogBdp(TargetLogBdp())),
              0.0, static_cast<double>(kMaxInitialWindowSize))));
    if (target < kMinPositiveInitialWindowSize) target = 0;
    if (g_test_only_transport_target_window_estimates_mocker != nullptr) {
      // Hook for simulating unusual flow control situations in tests.
      target = g_test_only_transport_target_window_estimates_mocker
                   ->ComputeNextTargetInitialWindowSizeFromPeriodicUpdate(
                       target_initial_window_size_ /* current target */);
    }
    // Though initial window 'could' drop to 0, we keep the floor at
    // kMinInitialWindowSize
    UpdateSetting(GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
                  &target_initial_window_size_, target, &action,
                  &FlowControlAction::set_send_initial_window_update);
    // we target the max of BDP or bandwidth in microseconds.
    UpdateSetting(GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE, &target_frame_size_,
                  target, &action,
                  &FlowControlAction::set_send_max_frame_size_update);

    if (IsTcpFrameSizeTuningEnabled()) {
      // Advertise PREFERRED_RECEIVE_CRYPTO_FRAME_SIZE to peer. By advertising
      // PREFERRED_RECEIVE_CRYPTO_FRAME_SIZE to the peer, we are informing the
      // peer that we have tcp frame size tuning enabled and we inform it of our
      // prefered rx frame sizes. The prefered rx frame size is determined as:
      // Clamp(target_frame_size_ * 2, 16384, 0x7fffffff). In the future, this
      // maybe updated to a different function of the memory pressure.
      UpdateSetting(
          GRPC_CHTTP2_SETTINGS_GRPC_PREFERRED_RECEIVE_CRYPTO_FRAME_SIZE,
          &target_preferred_rx_crypto_frame_size_,
          Clamp(static_cast<unsigned int>(target_frame_size_ * 2), 16384u,
                0x7ffffffu),
          &action,
          &FlowControlAction::set_preferred_rx_crypto_frame_size_update);
    }
  }
  return UpdateAction(action);
}

uint32_t StreamFlowControl::MaybeSendUpdate() {
  TransportFlowControl::IncomingUpdateContext tfc_upd(tfc_);
  const int64_t announce = DesiredAnnounceSize();
  pending_size_ = absl::nullopt;
  tfc_upd.UpdateAnnouncedWindowDelta(&announced_window_delta_, announce);
  GPR_ASSERT(DesiredAnnounceSize() == 0);
  std::ignore = tfc_upd.MakeAction();
  return static_cast<uint32_t>(announce);
}

int64_t StreamFlowControl::DesiredAnnounceSize() const {
  int64_t desired_window_delta = [this]() {
    if (min_progress_size_ == 0) {
      if (pending_size_.has_value() &&
          announced_window_delta_ < -*pending_size_) {
        return -*pending_size_;
      } else {
        return announced_window_delta_;
      }
    } else {
      return std::min(min_progress_size_, kMaxWindowDelta);
    }
  }();
  return Clamp(desired_window_delta - announced_window_delta_, int64_t{0},
               kMaxWindowUpdateSize);
}

FlowControlAction StreamFlowControl::UpdateAction(FlowControlAction action) {
  const int64_t desired_announce_size = DesiredAnnounceSize();
  if (desired_announce_size > 0) {
    FlowControlAction::Urgency urgency =
        FlowControlAction::Urgency::QUEUE_UPDATE;
    // Size at which we probably want to wake up and write regardless of whether
    // we *have* to.
    // Currently set at half the initial window size or 8kb (whichever is
    // greater). 8kb means we don't send rapidly unnecessarily when the initial
    // window size is small.
    const int64_t hurry_up_size = std::max(
        static_cast<int64_t>(tfc_->sent_init_window()) / 2, int64_t{8192});
    if (desired_announce_size > hurry_up_size) {
      urgency = FlowControlAction::Urgency::UPDATE_IMMEDIATELY;
    }
    // min_progress_size_ > 0 means we have a reader ready to read.
    if (min_progress_size_ > 0) {
      // If we're into initial window to receive that data we should wake up and
      // send an update.
      if (announced_window_delta_ < 0) {
        urgency = FlowControlAction::Urgency::UPDATE_IMMEDIATELY;
      } else if (announced_window_delta_ == 0 &&
                 tfc_->sent_init_window() == 0) {
        // Special case when initial window size is zero, meaning that
        // announced_window_delta cannot become negative (it may already be so
        // however).
        urgency = FlowControlAction::Urgency::UPDATE_IMMEDIATELY;
      }
    }
    action.set_send_stream_update(urgency);
  }
  return action;
}

void StreamFlowControl::IncomingUpdateContext::SetPendingSize(
    int64_t pending_size) {
  GPR_ASSERT(pending_size >= 0);
  sfc_->pending_size_ = pending_size;
}

}  // namespace chttp2
}  // namespace grpc_core
