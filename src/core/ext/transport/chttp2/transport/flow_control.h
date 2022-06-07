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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <iosfwd>
#include <string>

#include "absl/status/status.h"

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/transport/bdp_estimator.h"
#include "src/core/lib/transport/pid_controller.h"

extern grpc_core::TraceFlag grpc_flowctl_trace;

namespace grpc {
namespace testing {
class TrickledCHTTP2;  // to make this a friend
}  // namespace testing
}  // namespace grpc

namespace grpc_core {
namespace chttp2 {

static constexpr uint32_t kDefaultWindow = 65535;
static constexpr uint32_t kDefaultFrameSize = 16384;
static constexpr int64_t kMaxWindow = static_cast<int64_t>((1u << 31) - 1);
// TODO(ncteisen): Tune this
static constexpr uint32_t kFrameSize = 1024 * 1024;
static constexpr const uint32_t kMinInitialWindowSize = 128;
static constexpr const uint32_t kMaxInitialWindowSize = (1u << 30);
// The maximum per-stream flow control window delta to advertise.
static constexpr const uint32_t kMaxWindowDelta = (1u << 20);

class TransportFlowControl;
class StreamFlowControl;

extern bool g_test_only_transport_flow_control_window_check;

// Encapsulates a collections of actions the transport needs to take with
// regard to flow control. Each action comes with urgencies that tell the
// transport how quickly the action must take place.
class FlowControlAction {
 public:
  enum class Urgency : uint8_t {
    // Nothing to be done.
    NO_ACTION_NEEDED = 0,
    // Initiate a write to update the initial window immediately.
    UPDATE_IMMEDIATELY,
    // Push the flow control update into a send buffer, to be sent
    // out the next time a write is initiated.
    QUEUE_UPDATE,
  };

  Urgency send_stream_update() const { return send_stream_update_; }
  Urgency send_transport_update() const { return send_transport_update_; }
  Urgency send_initial_window_update() const {
    return send_initial_window_update_;
  }
  Urgency send_max_frame_size_update() const {
    return send_max_frame_size_update_;
  }
  uint32_t initial_window_size() const { return initial_window_size_; }
  uint32_t max_frame_size() const { return max_frame_size_; }

  FlowControlAction& set_send_stream_update(Urgency u) {
    send_stream_update_ = u;
    return *this;
  }
  FlowControlAction& set_send_transport_update(Urgency u) {
    send_transport_update_ = u;
    return *this;
  }
  FlowControlAction& set_send_initial_window_update(Urgency u,
                                                    uint32_t update) {
    send_initial_window_update_ = u;
    initial_window_size_ = update;
    return *this;
  }
  FlowControlAction& set_send_max_frame_size_update(Urgency u,
                                                    uint32_t update) {
    send_max_frame_size_update_ = u;
    max_frame_size_ = update;
    return *this;
  }

  static const char* UrgencyString(Urgency u);
  std::string DebugString() const;

  bool operator==(const FlowControlAction& other) const {
    return send_stream_update_ == other.send_stream_update_ &&
           send_transport_update_ == other.send_transport_update_ &&
           send_initial_window_update_ == other.send_initial_window_update_ &&
           send_max_frame_size_update_ == other.send_max_frame_size_update_ &&
           initial_window_size_ == other.initial_window_size_ &&
           max_frame_size_ == other.max_frame_size_;
  }

 private:
  Urgency send_stream_update_ = Urgency::NO_ACTION_NEEDED;
  Urgency send_transport_update_ = Urgency::NO_ACTION_NEEDED;
  Urgency send_initial_window_update_ = Urgency::NO_ACTION_NEEDED;
  Urgency send_max_frame_size_update_ = Urgency::NO_ACTION_NEEDED;
  uint32_t initial_window_size_ = 0;
  uint32_t max_frame_size_ = 0;
};

std::ostream& operator<<(std::ostream& out, FlowControlAction::Urgency urgency);
std::ostream& operator<<(std::ostream& out, const FlowControlAction& action);

// Implementation of flow control that abides to HTTP/2 spec and attempts
// to be as performant as possible.
class TransportFlowControl final {
 public:
  explicit TransportFlowControl(const char* name, bool enable_bdp_probe,
                                MemoryOwner* memory_owner);
  ~TransportFlowControl() {}

  bool bdp_probe() const { return enable_bdp_probe_; }

  // returns an announce if we should send a transport update to our peer,
  // else returns zero; writing_anyway indicates if a write would happen
  // regardless of the send - if it is false and this function returns non-zero,
  // this announce will cause a write to occur
  uint32_t MaybeSendUpdate(bool writing_anyway);

  // Reads the flow control data and returns and actionable struct that will
  // tell chttp2 exactly what it needs to do
  FlowControlAction MakeAction() { return UpdateAction(FlowControlAction()); }

  // Call periodically (at a low-ish rate, 100ms - 10s makes sense)
  // to perform more complex flow control calculations and return an action
  // to let chttp2 change its parameters
  FlowControlAction PeriodicUpdate();

  void StreamSentData(int64_t size) { remote_window_ -= size; }

  absl::Status ValidateRecvData(int64_t incoming_frame_size);
  void CommitRecvData(int64_t incoming_frame_size);

  absl::Status RecvData(int64_t incoming_frame_size);

  // we have received a WINDOW_UPDATE frame for a transport
  void RecvUpdate(uint32_t size);

  int64_t target_window() const;

  int64_t target_frame_size() const { return target_frame_size_; }

  void PreUpdateAnnouncedWindowOverIncomingWindow(int64_t delta) {
    if (delta > 0) {
      announced_stream_total_over_incoming_window_ -= delta;
    }
  }

  void PostUpdateAnnouncedWindowOverIncomingWindow(int64_t delta) {
    if (delta > 0) {
      announced_stream_total_over_incoming_window_ += delta;
    }
  }

  BdpEstimator* bdp_estimator() { return &bdp_estimator_; }

  void TestOnlyForceHugeWindow() {
    announced_window_ = 1024 * 1024 * 1024;
    remote_window_ = 1024 * 1024 * 1024;
  }

  uint32_t acked_init_window() const { return acked_init_window_; }
  uint32_t sent_init_window() const { return sent_init_window_; }

  void SetSentInitialWindow(uint32_t value) { sent_init_window_ = value; }
  void SetAckedInitialWindow(uint32_t value) { acked_init_window_ = value; }

  // Getters
  int64_t remote_window() const { return remote_window_; }
  int64_t announced_window() const { return announced_window_; }

 private:
  double TargetLogBdp();
  double SmoothLogBdp(double value);
  static void UpdateSetting(int64_t* desired_value, int64_t new_desired_value,
                            FlowControlAction* action,
                            FlowControlAction& (FlowControlAction::*set)(
                                FlowControlAction::Urgency, uint32_t));

  FlowControlAction UpdateAction(FlowControlAction action);

  MemoryOwner* const memory_owner_;

  /** calculating what we should give for local window:
      we track the total amount of flow control over initial window size
      across all streams: this is data that we want to receive right now (it
      has an outstanding read)
      and the total amount of flow control under initial window size across all
      streams: this is data we've read early
      we want to adjust incoming_window such that:
      incoming_window = total_over - max(bdp - total_under, 0) */
  int64_t announced_stream_total_over_incoming_window_ = 0;

  /** should we probe bdp? */
  const bool enable_bdp_probe_;

  /* bdp estimation */
  BdpEstimator bdp_estimator_;

  /* pid controller */
  PidController pid_controller_;
  Timestamp last_pid_update_;

  int64_t remote_window_ = kDefaultWindow;
  int64_t target_initial_window_size_ = kDefaultWindow;
  int64_t target_frame_size_ = kDefaultFrameSize;
  int64_t announced_window_ = kDefaultWindow;
  uint32_t sent_init_window_ = kDefaultWindow;
  uint32_t acked_init_window_ = kDefaultWindow;
};

// Implementation of flow control that abides to HTTP/2 spec and attempts
// to be as performant as possible.
class StreamFlowControl final {
 public:
  explicit StreamFlowControl(TransportFlowControl* tfc);
  ~StreamFlowControl() {
    tfc_->PreUpdateAnnouncedWindowOverIncomingWindow(announced_window_delta_);
  }

  FlowControlAction UpdateAction(FlowControlAction action);
  FlowControlAction MakeAction() { return UpdateAction(tfc_->MakeAction()); }

  // we have sent data on the wire, we must track this in our bookkeeping for
  // the remote peer's flow control.
  void SentData(int64_t outgoing_frame_size);

  // we have received data from the wire
  absl::Status RecvData(int64_t incoming_frame_size);

  // returns an announce if we should send a stream update to our peer, else
  // returns zero
  uint32_t MaybeSendUpdate();

  // we have received a WINDOW_UPDATE frame for a stream
  void RecvUpdate(uint32_t size) { remote_window_delta_ += size; }

  // the application is asking for a certain amount of bytes
  void UpdateProgress(uint32_t min_progress_size);

  int64_t remote_window_delta() const { return remote_window_delta_; }
  int64_t local_window_delta() const { return local_window_delta_; }
  int64_t announced_window_delta() const { return announced_window_delta_; }
  uint32_t min_progress_size() const { return min_progress_size_; }

  void TestOnlyForceHugeWindow() {
    announced_window_delta_ = 1024 * 1024 * 1024;
    local_window_delta_ = 1024 * 1024 * 1024;
    remote_window_delta_ = 1024 * 1024 * 1024;
  }

 private:
  TransportFlowControl* const tfc_;
  uint32_t min_progress_size_ = 0;
  int64_t remote_window_delta_ = 0;
  int64_t local_window_delta_ = 0;
  int64_t announced_window_delta_ = 0;

  void UpdateAnnouncedWindowDelta(TransportFlowControl* tfc, int64_t change);
};

class TestOnlyTransportTargetWindowEstimatesMocker {
 public:
  virtual ~TestOnlyTransportTargetWindowEstimatesMocker() {}
  virtual double ComputeNextTargetInitialWindowSizeFromPeriodicUpdate(
      double current_target) = 0;
};

extern TestOnlyTransportTargetWindowEstimatesMocker*
    g_test_only_transport_target_window_estimates_mocker;

}  // namespace chttp2
}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H
