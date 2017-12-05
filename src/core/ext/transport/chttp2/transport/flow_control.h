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

#include <grpc/support/useful.h>
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/lib/support/abstract.h"
#include "src/core/lib/support/manual_constructor.h"
#include "src/core/lib/transport/bdp_estimator.h"
#include "src/core/lib/transport/pid_controller.h"

struct grpc_chttp2_transport;
struct grpc_chttp2_stream;

extern grpc_core::TraceFlag grpc_flowctl_trace;

namespace grpc {
namespace testing {
class TrickledCHTTP2;  // to make this a friend
}  // namespace testing
}  // namespace grpc

namespace grpc_core {
namespace chttp2 {

static constexpr uint32_t kDefaultWindow = 65535;

class TransportFlowControl;
class StreamFlowControl;

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
  void Trace(grpc_chttp2_transport* t) const;

 private:
  Urgency send_stream_update_ = Urgency::NO_ACTION_NEEDED;
  Urgency send_transport_update_ = Urgency::NO_ACTION_NEEDED;
  Urgency send_initial_window_update_ = Urgency::NO_ACTION_NEEDED;
  Urgency send_max_frame_size_update_ = Urgency::NO_ACTION_NEEDED;
  uint32_t initial_window_size_ = 0;
  uint32_t max_frame_size_ = 0;
};

class FlowControlTrace {
 public:
  FlowControlTrace(const char* reason, TransportFlowControl* tfc,
                   StreamFlowControl* sfc) {
    if (enabled_) Init(reason, tfc, sfc);
  }

  ~FlowControlTrace() {
    if (enabled_) Finish();
  }

 private:
  void Init(const char* reason, TransportFlowControl* tfc,
            StreamFlowControl* sfc);
  void Finish();

  const bool enabled_ = grpc_flowctl_trace.enabled();

  TransportFlowControl* tfc_;
  StreamFlowControl* sfc_;
  const char* reason_;
  int64_t remote_window_;
  int64_t target_window_;
  int64_t announced_window_;
  int64_t remote_window_delta_;
  int64_t local_window_delta_;
  int64_t announced_window_delta_;
};

class TransportFlowControlBase {
 public:
  TransportFlowControlBase() {}
  virtual ~TransportFlowControlBase() {}
  virtual uint32_t MaybeSendUpdate(bool writing_anyway) { abort(); }
  virtual FlowControlAction MakeAction() { abort(); }
  virtual FlowControlAction PeriodicUpdate(grpc_exec_ctx* exec_ctx) { abort(); }
  virtual void StreamSentData(int64_t size) { abort(); }
  virtual grpc_error* RecvData(int64_t incoming_frame_size) { abort(); }
  virtual void RecvUpdate(uint32_t size) { abort(); }
  // TODO(ncteisen): maybe completely encapsulate this inside FlowControl
  virtual BdpEstimator* bdp_estimator() { return nullptr; }
  int64_t remote_window() const { return remote_window_; }
  virtual int64_t target_window() const { return target_initial_window_size_; }
  int64_t announced_window() const { return announced_window_; }
  virtual void TestOnlyForceHugeWindow() {}

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  friend class ::grpc::testing::TrickledCHTTP2;
  int64_t remote_window_ = kDefaultWindow;
  int64_t target_initial_window_size_ = kDefaultWindow;
  int64_t announced_window_ = kDefaultWindow;
};

const int64_t kMaxWindow = (int64_t)((1u << 31) - 1);

class TransportFlowControlDisabled final : public TransportFlowControlBase {
 public:
  TransportFlowControlDisabled() {
    remote_window_ = kMaxWindow;
    target_initial_window_size_ = kMaxWindow;
    announced_window_ = kMaxWindow;
  }
  uint32_t MaybeSendUpdate(bool writing_anyway) override { return 0; }
  FlowControlAction MakeAction() override { return FlowControlAction(); }
  FlowControlAction PeriodicUpdate(grpc_exec_ctx* exec_ctx) override {
    return FlowControlAction();
  }
  void StreamSentData(int64_t size) override {}
  grpc_error* RecvData(int64_t incoming_frame_size) override {
    return GRPC_ERROR_NONE;
  }
  void RecvUpdate(uint32_t size) override {}
  int64_t target_window() const override { return kMaxWindow; }
};

class TransportFlowControl final : public TransportFlowControlBase {
 public:
  TransportFlowControl(grpc_exec_ctx* exec_ctx, const grpc_chttp2_transport* t,
                       bool enable_bdp_probe);
  ~TransportFlowControl() {}

  bool bdp_probe() const { return enable_bdp_probe_; }

  // returns an announce if we should send a transport update to our peer,
  // else returns zero; writing_anyway indicates if a write would happen
  // regardless of the send - if it is false and this function returns non-zero,
  // this announce will cause a write to occur
  uint32_t MaybeSendUpdate(bool writing_anyway) override;

  // Reads the flow control data and returns and actionable struct that will
  // tell chttp2 exactly what it needs to do
  FlowControlAction MakeAction() override {
    return UpdateAction(FlowControlAction());
  }

  // Call periodically (at a low-ish rate, 100ms - 10s makes sense)
  // to perform more complex flow control calculations and return an action
  // to let chttp2 change its parameters
  FlowControlAction PeriodicUpdate(grpc_exec_ctx* exec_ctx) override;

  void StreamSentData(int64_t size) override { remote_window_ -= size; }

  grpc_error* ValidateRecvData(int64_t incoming_frame_size);
  void CommitRecvData(int64_t incoming_frame_size) {
    announced_window_ -= incoming_frame_size;
  }

  grpc_error* RecvData(int64_t incoming_frame_size) override {
    FlowControlTrace trace("  data recv", this, nullptr);
    grpc_error* error = ValidateRecvData(incoming_frame_size);
    if (error != GRPC_ERROR_NONE) return error;
    CommitRecvData(incoming_frame_size);
    return GRPC_ERROR_NONE;
  }

  // we have received a WINDOW_UPDATE frame for a transport
  void RecvUpdate(uint32_t size) override {
    FlowControlTrace trace("t updt recv", this, nullptr);
    remote_window_ += size;
  }

  int64_t target_window() const override {
    return (uint32_t)GPR_MIN((int64_t)((1u << 31) - 1),
                             announced_stream_total_over_incoming_window_ +
                                 target_initial_window_size_);
  }

  const grpc_chttp2_transport* transport() const { return t_; }

  void PreUpdateAnnouncedWindowOverIncomingWindow(int64_t delta) {
    if (delta > 0) {
      announced_stream_total_over_incoming_window_ -= delta;
    } else {
      announced_stream_total_under_incoming_window_ += -delta;
    }
  }

  void PostUpdateAnnouncedWindowOverIncomingWindow(int64_t delta) {
    if (delta > 0) {
      announced_stream_total_over_incoming_window_ += delta;
    } else {
      announced_stream_total_under_incoming_window_ -= -delta;
    }
  }

  BdpEstimator* bdp_estimator() override { return &bdp_estimator_; }

  void TestOnlyForceHugeWindow() override {
    announced_window_ = 1024 * 1024 * 1024;
    remote_window_ = 1024 * 1024 * 1024;
  }

 private:
  double TargetLogBdp();
  double SmoothLogBdp(grpc_exec_ctx* exec_ctx, double value);
  FlowControlAction::Urgency DeltaUrgency(int32_t value,
                                          grpc_chttp2_setting_id setting_id);

  FlowControlAction UpdateAction(FlowControlAction action) {
    if (announced_window_ < target_window() / 2) {
      action.set_send_transport_update(
          FlowControlAction::Urgency::UPDATE_IMMEDIATELY);
    }
    return action;
  }

  const grpc_chttp2_transport* const t_;

  /** calculating what we should give for local window:
      we track the total amount of flow control over initial window size
      across all streams: this is data that we want to receive right now (it
      has an outstanding read)
      and the total amount of flow control under initial window size across all
      streams: this is data we've read early
      we want to adjust incoming_window such that:
      incoming_window = total_over - max(bdp - total_under, 0) */
  int64_t announced_stream_total_over_incoming_window_ = 0;
  int64_t announced_stream_total_under_incoming_window_ = 0;

  /** should we probe bdp? */
  const bool enable_bdp_probe_;

  /* bdp estimation */
  grpc_core::BdpEstimator bdp_estimator_;

  /* pid controller */
  grpc_core::PidController pid_controller_;
  grpc_millis last_pid_update_ = 0;
};

class StreamFlowControlBase {
 public:
  StreamFlowControlBase() {}
  virtual ~StreamFlowControlBase() {}
  virtual FlowControlAction UpdateAction(FlowControlAction action) { abort(); }
  virtual FlowControlAction MakeAction() { abort(); }
  virtual void SentData(int64_t outgoing_frame_size) { abort(); }
  virtual grpc_error* RecvData(int64_t incoming_frame_size) { abort(); }
  virtual uint32_t MaybeSendUpdate() { abort(); }
  virtual void RecvUpdate(uint32_t size) { abort(); }
  virtual void IncomingByteStreamUpdate(size_t max_size_hint,
                                        size_t have_already) {
    abort();
  }
  virtual void TestOnlyForceHugeWindow() {}
  int64_t remote_window_delta() { return remote_window_delta_; }
  int64_t local_window_delta() { return local_window_delta_; }
  int64_t announced_window_delta() { return announced_window_delta_; }

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  friend class ::grpc::testing::TrickledCHTTP2;
  int64_t remote_window_delta_ = 0;
  int64_t local_window_delta_ = 0;
  int64_t announced_window_delta_ = 0;
};

class StreamFlowControlDisabled : public StreamFlowControlBase {
 public:
  FlowControlAction UpdateAction(FlowControlAction action) override {
    return action;
  }
  FlowControlAction MakeAction() override { return FlowControlAction(); }
  void SentData(int64_t outgoing_frame_size) override {}
  grpc_error* RecvData(int64_t incoming_frame_size) override {
    return GRPC_ERROR_NONE;
  }
  uint32_t MaybeSendUpdate() override { return 0; }
  void RecvUpdate(uint32_t size) override {}
  void IncomingByteStreamUpdate(size_t max_size_hint,
                                size_t have_already) override {}
};

class StreamFlowControl final : public StreamFlowControlBase {
 public:
  StreamFlowControl(TransportFlowControl* tfc, const grpc_chttp2_stream* s);
  ~StreamFlowControl() {
    tfc_->PreUpdateAnnouncedWindowOverIncomingWindow(announced_window_delta_);
  }

  FlowControlAction UpdateAction(FlowControlAction action) override;
  FlowControlAction MakeAction() override {
    return UpdateAction(tfc_->MakeAction());
  }

  // we have sent data on the wire, we must track this in our bookkeeping for
  // the remote peer's flow control.
  void SentData(int64_t outgoing_frame_size) override {
    FlowControlTrace tracer("  data sent", tfc_, this);
    tfc_->StreamSentData(outgoing_frame_size);
    remote_window_delta_ -= outgoing_frame_size;
  }

  // we have received data from the wire
  grpc_error* RecvData(int64_t incoming_frame_size) override;

  // returns an announce if we should send a stream update to our peer, else
  // returns zero
  uint32_t MaybeSendUpdate() override;

  // we have received a WINDOW_UPDATE frame for a stream
  void RecvUpdate(uint32_t size) override {
    FlowControlTrace trace("s updt recv", tfc_, this);
    remote_window_delta_ += size;
  }

  // the application is asking for a certain amount of bytes
  void IncomingByteStreamUpdate(size_t max_size_hint,
                                size_t have_already) override;

  int64_t remote_window_delta() const { return remote_window_delta_; }
  int64_t local_window_delta() const { return local_window_delta_; }
  int64_t announced_window_delta() const { return announced_window_delta_; }

  const grpc_chttp2_stream* stream() const { return s_; }

  void TestOnlyForceHugeWindow() override {
    announced_window_delta_ = 1024 * 1024 * 1024;
    local_window_delta_ = 1024 * 1024 * 1024;
    remote_window_delta_ = 1024 * 1024 * 1024;
  }

 private:
  TransportFlowControl* const tfc_;
  const grpc_chttp2_stream* const s_;

  void UpdateAnnouncedWindowDelta(TransportFlowControl* tfc, int64_t change) {
    tfc->PreUpdateAnnouncedWindowOverIncomingWindow(announced_window_delta_);
    announced_window_delta_ += change;
    tfc->PostUpdateAnnouncedWindowOverIncomingWindow(announced_window_delta_);
  }
};

}  // namespace chttp2
}  // namespace grpc_core

#endif
