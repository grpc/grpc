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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H

#include <grpc/support/port_platform.h>
#include <limits.h>
#include <stdint.h>

#include <algorithm>
#include <iosfwd>
#include <optional>
#include <string>
#include <utility>

#include "src/core/channelz/property_list.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/transport/bdp_estimator.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/time.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace chttp2 {

static constexpr uint32_t kDefaultWindow = 65535;
static constexpr uint32_t kDefaultFrameSize = 16384;
static constexpr int64_t kMaxWindow = static_cast<int64_t>((1u << 31) - 1);
// If smaller than this, advertise zero window.
static constexpr uint32_t kMinPositiveInitialWindowSize = 1024;
static constexpr const uint32_t kMaxInitialWindowSize = (1u << 30);
// The maximum per-stream flow control window delta to advertise.
static constexpr const int64_t kMaxWindowDelta = (1u << 20);
static constexpr const int kDefaultPreferredRxCryptoFrameSize = INT_MAX;

// TODO(tjagtap) [PH2][P2][BDP] Remove this static sleep when the BDP code is
// done. This needs to be dynamic.
constexpr Duration kFlowControlPeriodicUpdateTimer = Duration::Seconds(8);

class TransportFlowControl;
class StreamFlowControl;

enum class StallEdge { kNoChange, kStalled, kUnstalled };

#define GRPC_HTTP2_FLOW_CONTROL_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

// Encapsulates a collections of actions the transport needs to take with
// regard to flow control. Each action comes with urgencies that tell the
// transport how quickly the action must take place.
class GRPC_MUST_USE_RESULT FlowControlAction {
 public:
  enum class Urgency : uint8_t {
    // Nothing to be done.
    NO_ACTION_NEEDED = 0,
    // Initiate a write to send updates immediately.
    UPDATE_IMMEDIATELY,
    // Queue the flow control update, to be sent out the next time a write is
    // initiated.
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
  Urgency preferred_rx_crypto_frame_size_update() const {
    return preferred_rx_crypto_frame_size_update_;
  }

  // Returns true if any action has UPDATE_IMMEDIATELY urgency.
  bool AnyUpdateImmediately() const {
    return send_stream_update_ == Urgency::UPDATE_IMMEDIATELY ||
           send_transport_update_ == Urgency::UPDATE_IMMEDIATELY ||
           send_initial_window_update_ == Urgency::UPDATE_IMMEDIATELY ||
           send_max_frame_size_update_ == Urgency::UPDATE_IMMEDIATELY ||
           preferred_rx_crypto_frame_size_update_ ==
               Urgency::UPDATE_IMMEDIATELY;
  }

  std::string ImmediateUpdateReasons() const;

  // Returns the value of SETTINGS_INITIAL_WINDOW_SIZE that we will send to the
  // peer.
  uint32_t initial_window_size() const { return initial_window_size_; }
  // Returns the value of SETTINGS_MAX_FRAME_SIZE that we will send to the peer.
  uint32_t max_frame_size() const { return max_frame_size_; }
  // Returns the value of GRPC_PREFERRED_RECEIVE_CRYPTO_FRAME_SIZE that we will
  // send to the peer.
  uint32_t preferred_rx_crypto_frame_size() const {
    return preferred_rx_crypto_frame_size_;
  }

  FlowControlAction& test_only_set_send_initial_window_update(Urgency u,
                                                              uint32_t update) {
    return set_send_initial_window_update(u, update);
  }
  FlowControlAction& test_only_set_send_max_frame_size_update(Urgency u,
                                                              uint32_t update) {
    return set_send_max_frame_size_update(u, update);
  }
  FlowControlAction& test_only_set_preferred_rx_crypto_frame_size_update(
      Urgency u, uint32_t update) {
    return set_preferred_rx_crypto_frame_size_update(u, update);
  }

  static const char* UrgencyString(Urgency u);
  std::string DebugString() const;

  void AssertEmpty() { GRPC_CHECK(*this == FlowControlAction()); }

  bool operator==(const FlowControlAction& other) const {
    return send_stream_update_ == other.send_stream_update_ &&
           send_transport_update_ == other.send_transport_update_ &&
           send_initial_window_update_ == other.send_initial_window_update_ &&
           send_max_frame_size_update_ == other.send_max_frame_size_update_ &&
           (send_initial_window_update_ == Urgency::NO_ACTION_NEEDED ||
            initial_window_size_ == other.initial_window_size_) &&
           (send_max_frame_size_update_ == Urgency::NO_ACTION_NEEDED ||
            max_frame_size_ == other.max_frame_size_) &&
           (preferred_rx_crypto_frame_size_update_ ==
                Urgency::NO_ACTION_NEEDED ||
            preferred_rx_crypto_frame_size_ ==
                other.preferred_rx_crypto_frame_size_);
  }

 private:
  friend class StreamFlowControl;
  friend class TransportFlowControl;

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
  FlowControlAction& set_preferred_rx_crypto_frame_size_update(
      Urgency u, uint32_t update) {
    preferred_rx_crypto_frame_size_update_ = u;
    preferred_rx_crypto_frame_size_ = update;
    return *this;
  }

  Urgency send_stream_update_ = Urgency::NO_ACTION_NEEDED;
  Urgency send_transport_update_ = Urgency::NO_ACTION_NEEDED;
  Urgency send_initial_window_update_ = Urgency::NO_ACTION_NEEDED;
  Urgency send_max_frame_size_update_ = Urgency::NO_ACTION_NEEDED;
  Urgency preferred_rx_crypto_frame_size_update_ = Urgency::NO_ACTION_NEEDED;
  uint32_t initial_window_size_ = 0;
  uint32_t max_frame_size_ = 0;
  uint32_t preferred_rx_crypto_frame_size_ = 0;
};

std::ostream& operator<<(std::ostream& out, FlowControlAction::Urgency urgency);
std::ostream& operator<<(std::ostream& out, const FlowControlAction& action);

// Implementation of flow control that abides to HTTP/2 spec and attempts
// to be as performant as possible.
// This class manages the flow control at a connection level.
class TransportFlowControl final {
 public:
  explicit TransportFlowControl(absl::string_view name, bool enable_bdp_probe,
                                MemoryOwner* memory_owner);
  ~TransportFlowControl() {}

  bool bdp_probe() const { return enable_bdp_probe_; }

  // Returns a non-zero announce if we should send a transport update to our
  // peer, else returns zero; writing_anyway indicates if a write would happen
  // regardless of the send - if it is false and this function returns non-zero,
  // the caller can send a flow control update.
  uint32_t DesiredAnnounceSize(bool writing_anyway) const;

  // Call to update transport flow control state after sending a transport
  // WINDOW_UPDATE with `announce` size. `announce` should be value returned
  // by `DesiredAnnounceSize`.
  void SentUpdate(uint32_t announce);

  // Convenience method that combines `DesiredAnnounceSize` and `SentUpdate`.
  // Call to get window increment and update state in one go.
  uint32_t MaybeSendUpdate(bool writing_anyway) {
    uint32_t n = DesiredAnnounceSize(writing_anyway);
    SentUpdate(n);
    return n;
  }

  // Track an update to the incoming flow control counters - that is how many
  // tokens we report to our peer for the data that we are willing to accept.
  // Instantiators *must* call MakeAction before destruction of this object.
  class IncomingUpdateContext {
   public:
    explicit IncomingUpdateContext(TransportFlowControl* tfc) : tfc_(tfc) {}
    ~IncomingUpdateContext() { GRPC_CHECK_EQ(tfc_, nullptr); }

    IncomingUpdateContext(const IncomingUpdateContext&) = delete;
    IncomingUpdateContext& operator=(const IncomingUpdateContext&) = delete;
    IncomingUpdateContext(IncomingUpdateContext&&) = delete;
    IncomingUpdateContext& operator=(IncomingUpdateContext&&) = delete;

    // Reads the flow control data and returns an actionable struct that will
    // tell the transport exactly what it needs to do.
    FlowControlAction MakeAction() {
      return std::exchange(tfc_, nullptr)->UpdateAction(FlowControlAction());
    }

    // We have received data from the wire.
    // We check if this data is within our flow control limits or not.
    // If it exceeds the limit, we send an error.
    // RFC9113 : A receiver MAY respond with a stream error or connection
    // error of type FLOW_CONTROL_ERROR if it is unable to accept a frame.
    //
    // This function updates transport window for received data.
    // Call this ONLY IF stream is not available (e.g. already closed) AND
    // transport window must be updated to remain in sync with peer.
    // If stream IS available, call RecvData() on
    // StreamFlowControl::IncomingUpdateContext instead.
    absl::Status RecvData(
        int64_t incoming_frame_size, absl::FunctionRef<absl::Status()> stream =
                                         []() { return absl::OkStatus(); });

   private:
    friend class StreamFlowControl;
    // Update a stream announce window delta, keeping track of how much total
    // positive delta is present on the transport.
    void UpdateAnnouncedWindowDelta(int64_t* delta, int64_t change) {
      if (change == 0) return;
      if (*delta > 0) {
        tfc_->announced_stream_total_over_incoming_window_ -= *delta;
      }
      *delta += change;
      if (*delta > 0) {
        tfc_->announced_stream_total_over_incoming_window_ += *delta;
      }
    }

    TransportFlowControl* tfc_;
  };

  // Track an update to the outgoing flow control counters - that is how many
  // tokens our peer has said we can send.
  class OutgoingUpdateContext {
   public:
    explicit OutgoingUpdateContext(TransportFlowControl* tfc) : tfc_(tfc) {}

    OutgoingUpdateContext(const OutgoingUpdateContext&) = delete;
    OutgoingUpdateContext& operator=(const OutgoingUpdateContext&) = delete;
    OutgoingUpdateContext(OutgoingUpdateContext&&) = delete;
    OutgoingUpdateContext& operator=(OutgoingUpdateContext&&) = delete;

    // Call this function when a transport-level WINDOW_UPDATE frame is received
    // from peer to increase remote window.
    void RecvUpdate(uint32_t size) { tfc_->remote_window_ += size; }

    // Finish the update and check whether we became stalled or unstalled.
    StallEdge Finish() {
      bool is_stalled = tfc_->remote_window_ <= 0;
      if (is_stalled != was_stalled_) {
        return is_stalled ? StallEdge::kStalled : StallEdge::kUnstalled;
      } else {
        return StallEdge::kNoChange;
      }
    }

   private:
    friend class StreamFlowControl;
    void StreamSentData(int64_t size) { tfc_->remote_window_ -= size; }

    TransportFlowControl* tfc_;
    const bool was_stalled_ = tfc_->remote_window_ <= 0;
  };

  // Call periodically (at a low-ish rate, 100ms - 10s makes sense)
  // to perform more complex flow control calculations and return an action
  // to let the transport change its parameters.
  // TODO(tjagtap) [PH2][P2] Plumb with PH2 flow control.
  FlowControlAction PeriodicUpdate();

  int64_t test_only_target_window() const { return target_window(); }
  int64_t test_only_target_frame_size() const { return target_frame_size(); }
  int64_t test_only_target_preferred_rx_crypto_frame_size() const {
    return target_preferred_rx_crypto_frame_size();
  }

  BdpEstimator* bdp_estimator() { return &bdp_estimator_; }

  uint32_t test_only_acked_init_window() const { return acked_init_window(); }
  uint32_t test_only_sent_init_window() const { return sent_init_window(); }

  // Call after you prepare and queue a settings frame to send to the peer.
  void FlushedSettings() { sent_init_window_ = queued_init_window(); }

  // Updates the initial window size that we have acknowledged from the peer.
  // This affects stream-level flow control for data received from the peer.
  // If the new value differs from target_initial_window_size_, we return an
  // action to send an update to the peer with our target.
  FlowControlAction SetAckedInitialWindow(uint32_t value);

  void set_target_initial_window_size(uint32_t value) {
    target_initial_window_size_ =
        std::min(value, Http2Settings::max_initial_window_size());
  }

  // Getters
  int64_t remote_window() const { return remote_window_; }
  int64_t test_only_announced_window() const { return announced_window(); }

  int64_t test_only_announced_stream_total_over_incoming_window() const {
    return announced_stream_total_over_incoming_window();
  }

  // A snapshot of the flow control stats to export.
  struct Stats {
    int64_t target_window;
    int64_t target_frame_size;
    int64_t target_preferred_rx_crypto_frame_size;
    uint32_t acked_init_window;
    uint32_t queued_init_window;
    uint32_t sent_init_window;
    int64_t remote_window;
    int64_t announced_window;
    int64_t announced_stream_total_over_incoming_window;
    // BDP estimator stats.
    int64_t bdp_accumulator;
    int64_t bdp_estimate;
    double bdp_bw_est;

    std::string ToString() const;
    channelz::PropertyList ChannelzProperties() const {
      return channelz::PropertyList()
          .Set("target_window", target_window)
          .Set("target_frame_size", target_frame_size)
          .Set("target_preferred_rx_crypto_frame_size",
               target_preferred_rx_crypto_frame_size)
          .Set("acked_init_window", acked_init_window)
          .Set("queued_init_window", queued_init_window)
          .Set("sent_init_window", sent_init_window)
          .Set("remote_window", remote_window)
          .Set("announced_window", announced_window)
          .Set("announced_stream_total_over_incoming_window",
               announced_stream_total_over_incoming_window)
          .Set("bdp_accumulator", bdp_accumulator)
          .Set("bdp_estimate", bdp_estimate)
          .Set("bdp_bw_est", bdp_bw_est);
    }
  };

  Stats stats() const {
    Stats stats;
    stats.target_window = target_window();
    stats.target_frame_size = target_frame_size();
    stats.target_preferred_rx_crypto_frame_size =
        target_preferred_rx_crypto_frame_size();
    stats.acked_init_window = acked_init_window();
    stats.queued_init_window = queued_init_window();
    stats.sent_init_window = sent_init_window();
    stats.remote_window = remote_window();
    stats.announced_window = announced_window();
    stats.announced_stream_total_over_incoming_window =
        announced_stream_total_over_incoming_window();
    stats.bdp_accumulator = bdp_estimator_.accumulator();
    stats.bdp_estimate = bdp_estimator_.EstimateBdp();
    stats.bdp_bw_est = bdp_estimator_.EstimateBandwidth();
    return stats;
  }

  void AddStreamToWindowUpdateList(const uint32_t stream_id) {
    window_update_list_.insert(stream_id);
  }
  absl::flat_hash_set<uint32_t> DrainWindowUpdateList() {
    return std::exchange(window_update_list_, {});
  }
  size_t window_update_list_size() const { return window_update_list_.size(); }

 private:
  friend class StreamFlowControl;

  void RemoveAnnouncedWindowDelta(int64_t delta) {
    if (delta > 0) {
      announced_stream_total_over_incoming_window_ -= delta;
    }
  }

  double TargetInitialWindowSizeBasedOnMemoryPressureAndBdp() const;
  int64_t target_window() const;
  int64_t target_frame_size() const { return target_frame_size_; }
  int64_t target_preferred_rx_crypto_frame_size() const {
    return target_preferred_rx_crypto_frame_size_;
  }
  uint32_t acked_init_window() const { return acked_init_window_; }
  uint32_t queued_init_window() const { return target_initial_window_size_; }
  uint32_t sent_init_window() const { return sent_init_window_; }
  int64_t announced_window() const { return announced_window_; }
  int64_t announced_stream_total_over_incoming_window() const {
    return announced_stream_total_over_incoming_window_;
  }

  static void UpdateSetting(absl::string_view name, int64_t* desired_value,
                            uint32_t new_desired_value,
                            FlowControlAction* action,
                            FlowControlAction& (FlowControlAction::*set)(
                                FlowControlAction::Urgency, uint32_t));

  FlowControlAction UpdateAction(FlowControlAction action);

  MemoryOwner* const memory_owner_;

  /// calculating what we should give for local window:
  /// we track the total amount of flow control over initial window size
  /// across all streams: this is data that we want to receive right now (it
  /// has an outstanding read)
  /// and the total amount of flow control under initial window size across all
  /// streams: this is data we've read early
  /// we want to adjust incoming_window such that:
  /// incoming_window = total_over - max(bdp - total_under, 0)
  int64_t announced_stream_total_over_incoming_window_ = 0;

  /// should we probe bdp?
  const bool enable_bdp_probe_;

  // bdp estimation
  BdpEstimator bdp_estimator_;

  int64_t remote_window_ = kDefaultWindow;
  int64_t target_initial_window_size_ = kDefaultWindow;
  int64_t target_frame_size_ = kDefaultFrameSize;
  int64_t target_preferred_rx_crypto_frame_size_ =
      kDefaultPreferredRxCryptoFrameSize;
  int64_t announced_window_ = kDefaultWindow;
  uint32_t acked_init_window_ = kDefaultWindow;
  uint32_t sent_init_window_ = kDefaultWindow;
  absl::flat_hash_set<uint32_t> window_update_list_;
};

// Implementation of flow control that abides to HTTP/2 spec and attempts
// to be as performant as possible.
class StreamFlowControl final {
 public:
  explicit StreamFlowControl(TransportFlowControl* tfc);
  ~StreamFlowControl() {
    tfc_->RemoveAnnouncedWindowDelta(announced_window_delta_);
  }

  // Track an update to the incoming flow control counters - that is how many
  // tokens we report to our peer that we're willing to accept.
  // Instantiators *must* call MakeAction before destruction of this value.
  class IncomingUpdateContext {
   public:
    explicit IncomingUpdateContext(StreamFlowControl* sfc)
        : tfc_upd_(sfc->tfc_), sfc_(sfc) {}

    IncomingUpdateContext(const IncomingUpdateContext&) = delete;
    IncomingUpdateContext& operator=(const IncomingUpdateContext&) = delete;
    IncomingUpdateContext(IncomingUpdateContext&&) = delete;
    IncomingUpdateContext& operator=(IncomingUpdateContext&&) = delete;

    FlowControlAction MakeAction() {
      return sfc_->UpdateAction(tfc_upd_.MakeAction());
    }

    // We have received data from the wire.
    // We check if this data is within our flow control limits or not.
    // If it exceeds the limit, we send an error.
    // RFC9113 : A receiver MAY respond with a stream error or connection
    // error of type FLOW_CONTROL_ERROR if it is unable to accept a frame.
    //
    // Updates stream and transport window for received data on an active
    // stream. Calling this updates BOTH windows; do not call
    // TransportFlowControl::IncomingUpdateContext::RecvData() separately.
    absl::Status RecvData(int64_t incoming_frame_size);

    // Informs flow control that the application needs at least
    // `min_progress_size` bytes to make progress on reading the current stream.
    // An example usage of this would be, say we receive the first 1000 bytes of
    // a 2000 byte gRPC message, we can call SetMinProgressSize(1000)
    // TODO(tjagtap) [PH2][P2] Plumb with PH2 flow control.
    void SetMinProgressSize(int64_t min_progress_size) {
      sfc_->min_progress_size_ = min_progress_size;
    }

    // Informs flow control that `pending_size` bytes are buffered and waiting
    // for application to read. Call this when a complete message is assembled
    // but not yet pulled by the application. This helps flow control decide
    // whether to send a WINDOW_UPDATE to the peer.
    // TODO(tjagtap) [PH2][P1] Plumb with PH2 flow control.
    void SetPendingSize(int64_t pending_size);

    // This is a hack in place till SetPendingSize is fully plumbed. This hack
    // function just pretends that the application needs more bytes. Since we
    // dont actually know how many bytes the application needs, we just want to
    // refill the used up tokens. The only way to refill used up tokens is to
    // call this function for each DATA frame.
    // TODO(tjagtap) [PH2][P1] Remove hack after SetPendingSize is plumbed.
    void HackIncrementPendingSize(int64_t pending_size);

   private:
    TransportFlowControl::IncomingUpdateContext tfc_upd_;
    StreamFlowControl* const sfc_;
  };

  // Track an update to the outgoing flow control counters - that is how many
  // tokens our peer has said we can send.
  class OutgoingUpdateContext {
   public:
    explicit OutgoingUpdateContext(StreamFlowControl* sfc)
        : tfc_upd_(sfc->tfc_), sfc_(sfc) {}

    OutgoingUpdateContext(const OutgoingUpdateContext&) = delete;
    OutgoingUpdateContext& operator=(const OutgoingUpdateContext&) = delete;
    OutgoingUpdateContext(OutgoingUpdateContext&&) = delete;
    OutgoingUpdateContext& operator=(OutgoingUpdateContext&&) = delete;

    // Call this when a WINDOW_UPDATE frame is received from peer for this
    // stream, to increase send window.
    void RecvUpdate(uint32_t size) { sfc_->remote_window_delta_ += size; }

    // Call this after sending a DATA frame for this stream, to decrease send
    // window based on `outgoing_frame_size`.
    void SentData(int64_t outgoing_frame_size) {
      tfc_upd_.StreamSentData(outgoing_frame_size);
      sfc_->remote_window_delta_ -= outgoing_frame_size;
    }

   private:
    TransportFlowControl::OutgoingUpdateContext tfc_upd_;
    StreamFlowControl* const sfc_;
  };

  // Returns a non-zero announce if we should send a stream update to our
  // peer, else returns zero;
  uint32_t DesiredAnnounceSize() const;

  // Call after sending stream-level WINDOW_UPDATE to peer to update internal
  // state. Argument should be value previously returned by
  // `DesiredAnnounceSize`.
  void SentUpdate(uint32_t announce);

  // Convenience method that combines `DesiredAnnounceSize` and `SentUpdate`.
  // Call to get window increment and update state in one go.
  uint32_t MaybeSendUpdate() {
    uint32_t n = DesiredAnnounceSize();
    SentUpdate(n);
    return n;
  }

  int64_t remote_window_delta() const { return remote_window_delta_; }
  int64_t test_only_announced_window_delta() const {
    return announced_window_delta_;
  }
  int64_t test_only_min_progress_size() const { return min_progress_size_; }

  // A snapshot of the flow control stats to export.
  struct Stats {
    int64_t min_progress_size;
    int64_t remote_window_delta;
    int64_t announced_window_delta;
    std::optional<int64_t> pending_size;

    std::string ToString() const;
  };

  Stats stats() const {
    Stats stats;
    stats.min_progress_size = min_progress_size_;
    stats.remote_window_delta = remote_window_delta();
    stats.announced_window_delta = announced_window_delta_;
    stats.pending_size = pending_size_;
    return stats;
  }

  void ReportIfStalled(bool is_client, uint32_t stream_id,
                       const Http2Settings& peer_settings) const {
    if (remote_window_delta() + peer_settings.initial_window_size() <= 0 ||
        tfc_->remote_window_ == 0) {
      GRPC_HTTP2_FLOW_CONTROL_DLOG
          << "PH2 " << (is_client ? "CLIENT" : "SERVER")
          << " Flow Control Stalled :"
          << " Settings { peer initial window size="
          << peer_settings.initial_window_size()
          << "}, Transport {remote_window=" << tfc_->remote_window()
          << ", transport announced_window=" << tfc_->announced_window()
          << "}, Stream {stream_id=" << stream_id
          << ", remote_window_delta=" << remote_window_delta()
          << ", remote_window_delta() + peer_settings.initial_window_size() ="
          << (remote_window_delta() + peer_settings.initial_window_size())
          << " }";
    }
  }

 private:
  TransportFlowControl* const tfc_;
  int64_t min_progress_size_ = 0;
  int64_t remote_window_delta_ = 0;
  int64_t announced_window_delta_ = 0;
  std::optional<int64_t> pending_size_;

  FlowControlAction UpdateAction(FlowControlAction action);
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

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H
