//
//
// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SERVER_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SERVER_TRANSPORT_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "src/core/call/call_destination.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/goaway.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_promises.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/ext/transport/chttp2/transport/http2_ztrace_collector.h"
#include "src/core/ext/transport/chttp2/transport/incoming_metadata_tracker.h"
#include "src/core/ext/transport/chttp2/transport/keepalive.h"
#include "src/core/ext/transport/chttp2/transport/ping_promise.h"
#include "src/core/ext/transport/chttp2/transport/security_frame.h"
#include "src/core/ext/transport/chttp2/transport/stream.h"
#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"
#include "src/core/ext/transport/chttp2/transport/writable_streams.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace http2 {

// Experimental : The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P5] : Update the experimental status of the code when
// http2 rollout is completed.
class Http2ServerTransport final : public ServerTransport {
  // TODO(tjagtap) : [PH2][P3] Move the definitions to the header for better
  // inlining. For now definitions are in the cc file to
  // reduce cognitive load in the header.
 public:
  Http2ServerTransport(
      PromiseEndpoint endpoint, GRPC_UNUSED const ChannelArgs& channel_args,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine);

  Http2ServerTransport(const Http2ServerTransport&) = delete;
  Http2ServerTransport& operator=(const Http2ServerTransport&) = delete;
  Http2ServerTransport(Http2ServerTransport&&) = delete;
  Http2ServerTransport& operator=(Http2ServerTransport&&) = delete;
  ~Http2ServerTransport() override;

  FilterStackTransport* filter_stack_transport() override { return nullptr; }
  ClientTransport* client_transport() override { return nullptr; }
  ServerTransport* server_transport() override { return this; }
  absl::string_view GetTransportName() const override { return "http2"; }

  // TODO(tjagtap) : [PH2][EXT] : These can be removed when event engine rollout
  // is complete.
  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}

  void StartWatch(RefCountedPtr<StateWatcher>) override {
    // TODO(roth): Implement as part of migrating server side to new
    // watcher API.
  }
  void StopWatch(RefCountedPtr<StateWatcher>) override {
    // TODO(roth): Implement as part of migrating server side to new
    // watcher API.
  }

  void SetCallDestination(
      RefCountedPtr<UnstartedCallDestination> call_destination) override;

  void PerformOp(grpc_transport_op*) override;

  void Orphan() override;
  void AbortWithError();

  RefCountedPtr<channelz::SocketNode> GetSocketNode() const override {
    return nullptr;
  }

 private:
  //////////////////////////////////////////////////////////////////////////////
  // Spawn Helpers and Promise Helpers

  //////////////////////////////////////////////////////////////////////////////
  // Endpoint Helpers

  //////////////////////////////////////////////////////////////////////////////
  // Transport Read Path

  // Returns a promise to keep reading in a Loop till a fail/close is received.
  auto ReadLoop();

  // Returns a promise that will read and process one HTTP2 frame.
  auto ReadAndProcessOneFrame();

  // Returns a promise that will process one HTTP2 frame.
  auto ProcessOneFrame(Http2Frame frame);

  // Returns a promise that will do the cleanup after the ReadLoop ends.
  auto OnReadLoopEnded();

  //////////////////////////////////////////////////////////////////////////////
  // Transport Write Path

  // Read from the MPSC queue and write it.
  auto WriteFromQueue();

  // Returns a promise to keep writing in a Loop till a fail/close is received.
  auto WriteLoop();

  // TODO(akshitpatel) : [PH2][P1] : Delete this when write path is implemented.
  auto OnWriteLoopEnded();

  absl::Status TriggerWriteCycle(DebugLocation whence = {}) {
    LOG(INFO) << "Http2ServerTransport::TriggerWriteCycle location="
              << whence.file() << ":" << whence.line();
    return absl::OkStatus();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Settings

  //////////////////////////////////////////////////////////////////////////////
  // Flow Control

  //////////////////////////////////////////////////////////////////////////////
  // Stream Creation and Stream Handling

  //////////////////////////////////////////////////////////////////////////////
  // Ping Keepalive and Goaway

  //////////////////////////////////////////////////////////////////////////////
  // Error Path and Close Path

  // This function MUST be idempotent.
  void CloseStream(uint32_t stream_id, absl::Status status,
                   DebugLocation whence = {}) {
    LOG(INFO) << "Http2ServerTransport::CloseStream for stream id=" << stream_id
              << " status=" << status << " location=" << whence.file() << ":"
              << whence.line();
    // TODO(akshitpatel) : [PH2][P2] : Implement this.
  }

  // This function is supposed to be idempotent.
  void CloseTransport(const Http2Status& status, DebugLocation whence = {}) {
    LOG(INFO) << "Http2ServerTransport::CloseTransport status=" << status
              << " location=" << whence.file() << ":" << whence.line();
    // TODO(akshitpatel) : [PH2][P2] : Implement this.
  }

  // Handles the error status and returns the corresponding absl status. Absl
  // Status is returned so that the error can be gracefully handled
  // by promise primitives.
  // If the error is a stream error, it closes the stream and returns an ok
  // status. Ok status is returned because the calling transport promise loops
  // should not be cancelled in case of stream errors.
  // If the error is a connection error, it closes the transport and returns the
  // corresponding (failed) absl status.
  absl::Status HandleError(Http2Status status, DebugLocation whence = {}) {
    auto error_type = status.GetType();
    GRPC_DCHECK(error_type != Http2Status::Http2ErrorType::kOk);

    if (error_type == Http2Status::Http2ErrorType::kStreamError) {
      CloseStream(current_frame_header_.stream_id, status.GetAbslStreamError(),
                  whence);
      return absl::OkStatus();
    } else if (error_type == Http2Status::Http2ErrorType::kConnectionError) {
      CloseTransport(status, whence);
      return status.GetAbslConnectionError();
    }

    GPR_UNREACHABLE_CODE(return absl::InternalError("Invalid error type"));
  }

  //////////////////////////////////////////////////////////////////////////////
  // Misc

  //////////////////////////////////////////////////////////////////////////////
  // Inner Classes and Structs

  class PingSystemInterfaceImpl : public PingInterface {
   public:
    static std::unique_ptr<PingInterface> Make(Http2ServerTransport* transport);
    absl::Status TriggerWrite() override;
    Promise<absl::Status> PingTimeout() override;

   private:
    // Holding a raw pointer to transport works because all the promises
    // invoking the methods of this class are invoked while holding a ref to the
    // transport.
    Http2ServerTransport* transport_;
    explicit PingSystemInterfaceImpl(Http2ServerTransport* transport)
        : transport_(transport) {}
  };

  class KeepAliveInterfaceImpl : public KeepAliveInterface {
   public:
    static std::unique_ptr<KeepAliveInterface> Make(
        Http2ServerTransport* transport);

   private:
    explicit KeepAliveInterfaceImpl(Http2ServerTransport* transport)
        : transport_(transport) {
      // TODO(akshitpatel) [PH2][P2] Implement this
    }
    Promise<absl::Status> SendPingAndWaitForAck() override;
    Promise<absl::Status> OnKeepAliveTimeout() override;
    bool NeedToSendKeepAlivePing() override;
    // Holding a raw pointer to transport works because all the promises
    // invoking the methods of this class are invoked while holding a ref to the
    // transport.
    Http2ServerTransport* transport_;
  };

  class GoawayInterfaceImpl : public GoawayInterface {
   public:
    static std::unique_ptr<GoawayInterface> Make(
        GRPC_UNUSED Http2ServerTransport* transport) {
      // TODO(akshitpatel) : [PH2][P1] : Fix
      return nullptr;
    }

    Promise<absl::Status> SendPingAndWaitForAck() override {
      return transport_->ping_manager_->RequestPing(/*on_initiate=*/[] {},
                                                    /*important=*/true);
    }

    absl::Status TriggerWriteCycle() override {
      return transport_->TriggerWriteCycle();
    }
    uint32_t GetLastAcceptedStreamId() override;

   private:
    explicit GoawayInterfaceImpl(Http2ServerTransport* transport)
        : transport_(transport) {
      // TODO(akshitpatel) [PH2][P2] Implement this
    }
    // Holding a raw pointer to transport works because all the promises
    // invoking the methods of this class are invoked while holding a ref to the
    // transport.
    Http2ServerTransport* transport_;
  };

  //////////////////////////////////////////////////////////////////////////////
  // All Data Members

  RefCountedPtr<UnstartedCallDestination> call_destination_;

  // TODO(akshitpatel) : [PH2][P0] : Remove this when write path is ready.
  MpscReceiver<Http2Frame> outgoing_frames_;

  // TODO(tjagtap) : [PH2][P0] : These are copied as is from the client
  // transport. Take a look if modifications are needed.

  RefCountedPtr<Party> general_party_;  // Refer Gemini.md for party slot usage
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;

  PromiseEndpoint endpoint_;
  RefCountedPtr<SettingsPromiseManager> settings_;

  Http2FrameHeader current_frame_header_;

  Mutex transport_mutex_;

  absl::flat_hash_map<uint32_t, RefCountedPtr<Stream>> stream_list_
      ABSL_GUARDED_BY(transport_mutex_);

  uint32_t next_stream_id_;
  HPackCompressor encoder_;
  HPackParser parser_;
  bool is_transport_closed_ ABSL_GUARDED_BY(transport_mutex_) = false;
  Latch<void> transport_closed_latch_;

  ConnectivityStateTracker state_tracker_ ABSL_GUARDED_BY(transport_mutex_){
      "http2_server", GRPC_CHANNEL_READY};

  RefCountedPtr<StateWatcher> watcher_ ABSL_GUARDED_BY(transport_mutex_);

  bool should_reset_ping_clock_;
  bool is_first_write_;
  IncomingMetadataTracker incoming_headers_;
  WriteContext write_context_;

  // Tracks the max allowed stream id. Currently this is only set on receiving a
  // graceful GOAWAY frame.
  uint32_t max_allowed_stream_id_ = RFC9113::kMaxStreamId31Bit;

  // Duration between two consecutive keepalive pings.
  Duration keepalive_time_;
  bool test_only_ack_pings_;
  std::optional<PingManager> ping_manager_;
  std::optional<KeepaliveManager> keepalive_manager_;

  // Flags
  bool keepalive_permit_without_calls_;

  GoawayManager goaway_manager_;

  WritableStreams<RefCountedPtr<Stream>> writable_stream_list_;

  /// Based on channel args, preferred_rx_crypto_frame_sizes are advertised to
  /// the peer
  bool enable_preferred_rx_crypto_frame_advertisement_;
  RefCountedPtr<SecurityFrameHandler> security_frame_handler_;
  MemoryOwner memory_owner_;
  chttp2::TransportFlowControl flow_control_;
  std::shared_ptr<PromiseHttp2ZTraceCollector> ztrace_collector_;

  // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
  Waker periodic_updates_waker_;

  Http2ReadContext reader_state_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SERVER_TRANSPORT_H
