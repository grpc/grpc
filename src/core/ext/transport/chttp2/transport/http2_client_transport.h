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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_CLIENT_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_CLIENT_TRANSPORT_H

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/goaway.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_promises.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/ext/transport/chttp2/transport/http2_ztrace_collector.h"
#include "src/core/ext/transport/chttp2/transport/incoming_metadata_tracker.h"
#include "src/core/ext/transport/chttp2/transport/keepalive.h"
#include "src/core/ext/transport/chttp2/transport/ping_promise.h"
#include "src/core/ext/transport/chttp2/transport/stream.h"
#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"
#include "src/core/ext/transport/chttp2/transport/writable_streams.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/check_class_size.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {
namespace http2 {

// All Promise Based HTTP2 Transport TODOs have the tag
// [PH2][Pn] where n = 0 to 5.
// This helps to maintain the uniformity for quick lookup and fixing.
//
// [PH2][P0] MUST be fixed before the current PR is submitted.
// [PH2][P1] MUST be fixed before the current sub-project is considered
//           complete.
// [PH2][P2] MUST be fixed before the current Milestone is considered
//           complete.
// [PH2][P3] MUST be fixed before Milestone 3 is considered complete.
// [PH2][P4] Can be fixed after roll out begins. Evaluate these during
//           Milestone 4. Either do the TODOs or delete them.
// [PH2][P5] This MUST be a separate standalone project.
// [PH2][EXT] This is a TODO related to a project unrelated to PH2 but happening
//            in parallel.

// Http2 Client Transport Spawns Overview

// | Promise Spawn       | Max Duration | Promise Resolution    | Max Spawns |
// |                     | for Spawn    |                       |            |
// |---------------------|--------------|-----------------------|------------|
// | Endpoint Read Loop  | Infinite     | On transport close    | One        |
// | Endpoint Write Loop | Infinite     | On transport close    | One        |
// | Stream Multiplexer  | Infinite     | On transport close    | One        |
// | Close Transport     | CloseTimeout | On transport close    | One        |

// Max Party Slots (Always): 3
// Max Promise Slots (Worst Case): 4

// Experimental : The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P3] : Update the experimental status of the code before
// http2 rollout begins.
class Http2ClientTransport final : public ClientTransport,
                                   public channelz::DataSource {
  // TODO(tjagtap) : [PH2][P3] Move the definitions to the header for better
  // inlining. For now definitions are in the cc file to
  // reduce cognitive load in the header.
 public:
  Http2ClientTransport(
      PromiseEndpoint endpoint, GRPC_UNUSED const ChannelArgs& channel_args,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine,
      absl::AnyInvocable<void(absl::StatusOr<uint32_t>)> on_receive_settings);

  Http2ClientTransport(const Http2ClientTransport&) = delete;
  Http2ClientTransport& operator=(const Http2ClientTransport&) = delete;
  Http2ClientTransport(Http2ClientTransport&&) = delete;
  Http2ClientTransport& operator=(Http2ClientTransport&&) = delete;
  ~Http2ClientTransport() override;

  FilterStackTransport* filter_stack_transport() override { return nullptr; }
  ClientTransport* client_transport() override { return this; }
  ServerTransport* server_transport() override { return nullptr; }
  absl::string_view GetTransportName() const override { return "http2"; }

  // TODO(tjagtap) : [PH2][EXT] : These can be removed when event engine rollout
  // is complete.
  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}

  // Called at the start of a stream.
  void StartCall(CallHandler call_handler) override;

  void PerformOp(grpc_transport_op*) override;
  void StartConnectivityWatch(
      grpc_connectivity_state state,
      OrphanablePtr<ConnectivityStateWatcherInterface> watcher);
  void StopConnectivityWatch(ConnectivityStateWatcherInterface* watcher);

  void StartWatch(RefCountedPtr<StateWatcher> watcher) override;
  void StopWatch(RefCountedPtr<StateWatcher> watcher) override;

  void Orphan() override;

  RefCountedPtr<channelz::SocketNode> GetSocketNode() const override {
    return const_cast<channelz::BaseNode*>(
               channelz::DataSource::channelz_node())
        ->RefAsSubclass<channelz::SocketNode>();
  }

  std::unique_ptr<channelz::ZTrace> GetZTrace(absl::string_view name) override {
    if (name == "transport_frames") return ztrace_collector_->MakeZTrace();
    return nullptr;
  }

  void AddData(channelz::DataSink sink) override;
  void SpawnAddChannelzData(channelz::DataSink sink);

  auto TestOnlyTriggerWriteCycle() {
    return Immediate(writable_stream_list_.ForceReadyForWrite());
  }

  auto TestOnlySendPing(absl::AnyInvocable<void()> on_initiate,
                        bool important = false) {
    return ping_manager_.RequestPing(std::move(on_initiate), important);
  }

  template <typename Factory>
  void TestOnlySpawnPromise(absl::string_view name, Factory&& factory) {
    general_party_->Spawn(name, std::forward<Factory>(factory), [](Empty) {});
  }

  int64_t TestOnlyTransportFlowControlWindow() {
    return flow_control_.remote_window();
  }

  int64_t TestOnlyGetStreamFlowControlWindow(const uint32_t stream_id) {
    RefCountedPtr<Stream> stream = LookupStream(stream_id);
    if (stream == nullptr) {
      return -1;
    }
    return stream->flow_control.remote_window_delta();
  }

  bool AreTransportFlowControlTokensAvailable() {
    return flow_control_.remote_window() > 0;
  }

 private:
  // Promise factory for processing each type of frame
  Http2Status ProcessHttp2DataFrame(Http2DataFrame frame);
  Http2Status ProcessHttp2HeaderFrame(Http2HeaderFrame frame);
  Http2Status ProcessHttp2RstStreamFrame(Http2RstStreamFrame frame);
  Http2Status ProcessHttp2SettingsFrame(Http2SettingsFrame frame);
  auto ProcessHttp2PingFrame(Http2PingFrame frame);
  Http2Status ProcessHttp2GoawayFrame(Http2GoawayFrame frame);
  Http2Status ProcessHttp2WindowUpdateFrame(Http2WindowUpdateFrame frame);
  Http2Status ProcessHttp2ContinuationFrame(Http2ContinuationFrame frame);
  Http2Status ProcessHttp2SecurityFrame(Http2SecurityFrame frame);
  Http2Status ProcessMetadata(RefCountedPtr<Stream> stream);

  // Reading from the endpoint.

  // Returns a promise to keep reading in a Loop till a fail/close is
  // received.
  auto ReadLoop();

  // Returns a promise that will read and process one HTTP2 frame.
  auto ReadAndProcessOneFrame();

  // Returns a promise that will process one HTTP2 frame.
  auto ProcessOneFrame(Http2Frame frame);

  // Writing to the endpoint.

  // Write time sensitive control frames to the endpoint. Frames sent from here
  // will be GOAWAY, SETTINGS, PING and PING acks, WINDOW_UPDATE and
  // Custom gRPC security frame.
  // These frames are written to the endpoint in a single endpoint write. If any
  // module needs to take action after the write (for cases like spawning
  // timeout promises), they MUST plug the call in the
  // NotifyControlFramesWriteDone.
  auto WriteControlFrames();

  // Notify the control frames modules that the endpoint write is done.
  void NotifyControlFramesWriteDone();

  // Returns a promise to keep draining control frames and data frames from all
  // the writable streams and write to the endpoint.
  auto MultiplexerLoop();

  // Returns a promise to fetch data from the callhandler and pass it further
  // down towards the endpoint.
  auto CallOutboundLoop(CallHandler call_handler, RefCountedPtr<Stream> stream,
                        ClientMetadataHandle metadata);

  // TODO(akshitpatel) : [PH2][P3] : Make this a synchronous function.
  // Force triggers a transport write cycle
  auto TriggerWriteCycle() {
    return Immediate(writable_stream_list_.ForceReadyForWrite());
  }

  auto FlowControlPeriodicUpdateLoop();
  // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
  void AddPeriodicUpdatePromiseWaker() {
    periodic_updates_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
  }
  // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
  void WakeupPeriodicUpdatePromise() { periodic_updates_waker_.Wakeup(); }

  // Processes the flow control action and take necessary steps.
  void ActOnFlowControlAction(const chttp2::FlowControlAction& action,
                              RefCountedPtr<Stream> stream);

  void NotifyStateWatcherOnDisconnectLocked(
      absl::Status status, StateWatcher::DisconnectInfo disconnect_info)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&transport_mutex_);

  RefCountedPtr<Party> general_party_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;

  PromiseEndpoint endpoint_;
  Http2SettingsManager settings_;
  SettingsTimeoutManager transport_settings_;

  Http2FrameHeader current_frame_header_;
  // Returns the number of active streams. A stream is removed from the `active`
  // list once both client and server agree to close the stream. The count of
  // stream_list_(even though stream list represents streams open for reads)
  // works here because of the following cases where the stream is closed:
  // 1. Reading a RST stream frame: In this case, the stream is immediately
  //    closed for reads and writes and removed from the stream_list_
  //    (effectively tracking the number of active streams).
  // 2. Reading a Trailing Metadata frame: In this case, the stream MAY be
  //    closed for reads and writes immediately which follows the above case. In
  //    other cases, the transport either reads RST stream frame from the server
  //    (and follows case 1) or sends a half close frame and closes the stream
  //    for reads and writes (in the multiplexer loop).
  // 3. Hitting error condition in the transport: In this case, RST stream is
  //    is enqueued and the stream is closed for reads immediately. This means
  //    we effectively reduce the number of active streams inline (because we
  //    remove the stream from the stream_list_). This is fine because the
  //    priority logic in list of writable streams ensures that the RST stream
  //    frame is given priority over any new streams being created by the
  //    client.
  // 4. Application abort: In this case, multiplexer loop will write RST stream
  //    frame to the endpoint and close the stream from reads and writes. This
  //    then follows the same reasoning as case 1.
  inline uint32_t GetActiveStreamCount() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(transport_mutex_) {
    return stream_list_.size();
  }

  // Returns the next stream id. If the next stream id is not available, it
  // returns std::nullopt. MUST be called from the transport party.
  absl::StatusOr<uint32_t> NextStreamId() {
    if (next_stream_id_ > GetMaxAllowedStreamId()) {
      // TODO(tjagtap) : [PH2][P3] : Handle case if transport runs out of stream
      // ids
      // RFC9113 : Stream identifiers cannot be reused. Long-lived connections
      // can result in an endpoint exhausting the available range of stream
      // identifiers. A client that is unable to establish a new stream
      // identifier can establish a new connection for new streams. A server
      // that is unable to establish a new stream identifier can send a GOAWAY
      // frame so that the client is forced to open a new connection for new
      // streams.
      return absl::ResourceExhaustedError("No more stream ids available");
    }
    // TODO(akshitpatel) : [PH2][P3] : There is a channel arg to delay
    // starting new streams instead of failing them. This needs to be
    // implemented.
    {
      MutexLock lock(&transport_mutex_);
      if (GetActiveStreamCount() >= settings_.peer().max_concurrent_streams()) {
        return absl::ResourceExhaustedError("Reached max concurrent streams");
      }
    }

    // RFC9113 : Streams initiated by a client MUST use odd-numbered stream
    // identifiers.
    uint32_t new_stream_id =
        std::exchange(next_stream_id_, next_stream_id_ + 2);
    if (GPR_UNLIKELY(next_stream_id_ > GetMaxAllowedStreamId())) {
      ReportDisconnection(
          absl::ResourceExhaustedError("Transport Stream IDs exhausted"),
          {},  // TODO(tjagtap) : [PH2][P2] : Report better disconnect info.
          "no_more_stream_ids");
    }
    return new_stream_id;
  }

  // Returns the next stream id without incrementing it. MUST be called from the
  // transport party.
  uint32_t PeekNextStreamId() const { return next_stream_id_; }

  absl::Status AssignStreamId(RefCountedPtr<Stream> stream);

  void AddToStreamList(RefCountedPtr<Stream> stream);

  Mutex transport_mutex_;

  absl::flat_hash_map<uint32_t, RefCountedPtr<Stream>> stream_list_
      ABSL_GUARDED_BY(transport_mutex_);

  uint32_t next_stream_id_;
  HPackCompressor encoder_;
  HPackParser parser_;
  bool is_transport_closed_ ABSL_GUARDED_BY(transport_mutex_) = false;
  Latch<void> transport_closed_latch_;

  template <typename Promise>
  auto UntilTransportClosed(Promise promise) {
    return Race(Map(transport_closed_latch_.Wait(),
                    [](Empty) {
                      GRPC_HTTP2_CLIENT_DLOG << "Transport closed";
                      return absl::CancelledError("Transport closed");
                    }),
                std::move(promise));
  }

  // Spawns an infallible promise on the transport party.
  template <typename Factory>
  void SpawnInfallibleTransportParty(absl::string_view name, Factory&& factory);

  // Spawns a promise on the transport party. If the promise returns a non-ok
  // status, it is handled by closing the transport with the corresponding
  // status.
  template <typename Factory>
  void SpawnGuardedTransportParty(absl::string_view name, Factory&& factory);

  ConnectivityStateTracker state_tracker_ ABSL_GUARDED_BY(transport_mutex_){
      "http2_client", GRPC_CHANNEL_READY};

  RefCountedPtr<StateWatcher> watcher_ ABSL_GUARDED_BY(transport_mutex_);

  // Runs on the call party.
  std::optional<RefCountedPtr<Stream>> MakeStream(CallHandler call_handler);

  struct CloseStreamArgs {
    bool close_reads;
    bool close_writes;
  };

  // This function MUST be idempotent.
  void CloseStream(RefCountedPtr<Stream> stream, CloseStreamArgs args,
                   DebugLocation whence = {});

  void BeginCloseStream(RefCountedPtr<Stream> stream,
                        std::optional<uint32_t> reset_stream_error_code,
                        ServerMetadataHandle&& metadata,
                        DebugLocation whence = {});

  RefCountedPtr<Stream> LookupStream(uint32_t stream_id);

  auto EndpointReadSlice(const size_t num_bytes) {
    return Map(endpoint_.ReadSlice(num_bytes),
               [self = RefAsSubclass<Http2ClientTransport>(),
                num_bytes](absl::StatusOr<Slice> status) {
                 if (status.ok()) {
                   self->keepalive_manager_.GotData();
                   self->ztrace_collector_->Append(
                       PromiseEndpointReadTrace{num_bytes});
                 }
                 return status;
               });
  }

  // HTTP2 Settings
  void MarkPeerSettingsResolved() {
    settings_.SetPreviousSettingsPromiseResolved(true);
  }
  auto WaitForSettingsTimeoutDone() {
    return [self = RefAsSubclass<Http2ClientTransport>()](absl::Status status) {
      if (!status.ok()) {
        GRPC_UNUSED absl::Status result = self->HandleError(
            std::nullopt, Http2Status::Http2ConnectionError(
                              Http2ErrorCode::kProtocolError,
                              std::string(RFC9113::kSettingsTimeout)));
      } else {
        self->MarkPeerSettingsResolved();
      }
    };
  }
  // TODO(tjagtap) : [PH2][P1] : Plumbing. Call this after the SETTINGS frame
  // has been written to endpoint_.
  void SpawnWaitForSettingsTimeout() {
    settings_.SetPreviousSettingsPromiseResolved(false);
    general_party_->Spawn("WaitForSettingsTimeout",
                          transport_settings_.WaitForSettingsTimeout(),
                          WaitForSettingsTimeoutDone());
  }

  auto EndpointRead(const size_t num_bytes) {
    return Map(endpoint_.Read(num_bytes),
               [self = RefAsSubclass<Http2ClientTransport>(),
                num_bytes](absl::StatusOr<SliceBuffer> status) {
                 if (status.ok()) {
                   self->keepalive_manager_.GotData();
                   self->ztrace_collector_->Append(
                       PromiseEndpointReadTrace{num_bytes});
                 }
                 return status;
               });
  }

  // This function MUST run on the transport party.
  void CloseTransport();

  void MaybeSpawnCloseTransport(Http2Status http2_status,
                                DebugLocation whence = {});

  // Handles the error status and returns the corresponding absl status. Absl
  // Status is returned so that the error can be gracefully handled
  // by promise primitives.
  // If the error is a stream error, it closes the stream and returns an ok
  // status. Ok status is returned because the calling transport promise loops
  // should not be cancelled in case of stream errors.
  // If the error is a connection error, it closes the transport and returns the
  // corresponding (failed) absl status.
  absl::Status HandleError(const std::optional<uint32_t> stream_id,
                           Http2Status status, DebugLocation whence = {}) {
    auto error_type = status.GetType();
    GRPC_DCHECK(error_type != Http2Status::Http2ErrorType::kOk);

    if (error_type == Http2Status::Http2ErrorType::kStreamError) {
      GRPC_HTTP2_CLIENT_ERROR_DLOG << "Stream Error: " << status.DebugString();
      GRPC_DCHECK(stream_id.has_value());
      BeginCloseStream(
          LookupStream(stream_id.value()),
          Http2ErrorCodeToFrameErrorCode(status.GetStreamErrorCode()),
          ServerMetadataFromStatus(status.GetAbslStreamError()), whence);
      return absl::OkStatus();
    } else if (error_type == Http2Status::Http2ErrorType::kConnectionError) {
      GRPC_HTTP2_CLIENT_ERROR_DLOG << "Connection Error: "
                                   << status.DebugString();
      absl::Status absl_status = status.GetAbslConnectionError();
      MaybeSpawnCloseTransport(std::move(status), whence);
      return absl_status;
    }
    GPR_UNREACHABLE_CODE(return absl::InternalError("Invalid error type"));
  }

  bool should_reset_ping_clock_;
  bool incoming_header_in_progress_;
  bool incoming_header_end_stream_;
  bool is_first_write_;
  uint32_t incoming_header_stream_id_;
  absl::AnyInvocable<void(absl::StatusOr<uint32_t>)> on_receive_settings_;

  uint32_t max_header_list_size_soft_limit_;

  // The target number of bytes to write in a single write cycle. We may not
  // always honour this max_write_size. We MAY overshoot it at most once per
  // write cycle.
  size_t max_write_size_;
  // The number of bytes remaining to be written in the current write cycle.
  size_t write_bytes_remaining_;

  // The max_write_size will be decided dynamically based on the available
  // bandwidth on the wire. We aim to keep the time spent in the write loop to
  // about 100ms.
  void SetMaxWriteSize(const size_t max_write_size) {
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport SetMaxWriteSize "
                           << " max_write_size changed: " << max_write_size_
                           << " -> " << max_write_size;
    max_write_size_ = max_write_size;
  }

  size_t GetMaxWriteSize() const { return max_write_size_; }

  auto SerializeAndWrite(std::vector<Http2Frame>&& frames);
  // Tracks the max allowed stream id. Currently this is only set on receiving a
  // graceful GOAWAY frame.
  uint32_t max_allowed_stream_id_ = RFC9113::kMaxStreamId31Bit;

  uint32_t GetMaxAllowedStreamId() const;

  void SetMaxAllowedStreamId(uint32_t max_allowed_stream_id);

  bool CanCloseTransportLocked() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(transport_mutex_);

  // Ping related members
  // TODO(akshitpatel) : [PH2][P2] : Consider removing the timeout related
  // members.
  // Duration between two consecutive keepalive pings
  const Duration keepalive_time_;
  // Duration to wait for a keepalive ping ack before triggering timeout. This
  // only takes effect if the assigned value is less than the ping timeout.
  const Duration keepalive_timeout_;
  // Duration to wait for ping ack before triggering timeout
  const Duration ping_timeout_;
  PingManager ping_manager_;
  KeepaliveManager keepalive_manager_;

  // Flags
  bool keepalive_permit_without_calls_;

  auto SendPing(absl::AnyInvocable<void()> on_initiate, bool important) {
    return ping_manager_.RequestPing(std::move(on_initiate), important);
  }
  auto WaitForPingAck() { return ping_manager_.WaitForPingAck(); }

  void MaybeGetSettingsFrame(SliceBuffer& output_buf) {
    std::optional<Http2Frame> settings_frame = settings_.MaybeSendUpdate();
    if (settings_frame.has_value()) {
      Serialize(absl::Span<Http2Frame>(&settings_frame.value(), 1), output_buf);
      flow_control_.FlushedSettings();
    }
  }

  void MaybeGetWindowUpdateFrames(SliceBuffer& output_buf);

  void ReportDisconnection(const absl::Status& status,
                           StateWatcher::DisconnectInfo disconnect_info,
                           const char* reason);

  void ReportDisconnectionLocked(const absl::Status& status,
                                 StateWatcher::DisconnectInfo disconnect_info,
                                 const char* reason)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&transport_mutex_);

  // Ping Helper functions
  Duration NextAllowedPingInterval() {
    MutexLock lock(&transport_mutex_);
    return (!keepalive_permit_without_calls_ && GetActiveStreamCount() == 0)
               ? Duration::Hours(2)
               : Duration::Seconds(1);
  }

  auto AckPing(uint64_t opaque_data) {
    bool valid_ping_ack_received = true;

    if (!ping_manager_.AckPing(opaque_data)) {
      GRPC_HTTP2_CLIENT_DLOG << "Unknown ping response received for ping id="
                             << opaque_data;
      valid_ping_ack_received = false;
    }

    return If(
        // It is possible that the PingRatePolicy may decide to not send a ping
        // request (in cases like the number of inflight pings is too high).
        // When this happens, it becomes important to ensure that if a ping ack
        // is received and there is an "important" outstanding ping request, we
        // should retry to send it out now.
        valid_ping_ack_received && ping_manager_.ImportantPingRequested(),
        [self = RefAsSubclass<Http2ClientTransport>()] {
          return Map(self->TriggerWriteCycle(), [](const absl::Status status) {
            return (status.ok())
                       ? Http2Status::Ok()
                       : Http2Status::AbslConnectionError(
                             status.code(), std::string(status.message()));
          });
        },
        [] { return Immediate(Http2Status::Ok()); });
  }

  class PingSystemInterfaceImpl : public PingInterface {
   public:
    static std::unique_ptr<PingInterface> Make(
        Http2ClientTransport* transport) {
      return std::make_unique<PingSystemInterfaceImpl>(
          PingSystemInterfaceImpl(transport));
    }

    Promise<absl::Status> TriggerWrite() override {
      return transport_->TriggerWriteCycle();
    }

    Promise<absl::Status> PingTimeout() override {
      GRPC_HTTP2_CLIENT_DLOG << "Ping timeout at time: " << Timestamp::Now();

      // TODO(akshitpatel) : [PH2][P2] : The error code here has been chosen
      // based on CHTTP2's usage of GRPC_STATUS_UNAVAILABLE (which corresponds
      // to kRefusedStream). However looking at RFC9113, definition of
      // kRefusedStream doesn't seem to fit this case. We should revisit this
      // and update the error code.
      return Immediate(transport_->HandleError(
          std::nullopt, Http2Status::Http2ConnectionError(
                            Http2ErrorCode::kRefusedStream, "Ping timeout")));
    }

   private:
    // TODO(akshitpatel) : [PH2][P2] : Eventually there should be a separate ref
    // counted struct/class passed to all the transport promises/members. This
    // will help removing back references from the transport members to
    // transport and greatly simpilfy the cleanup path.
    Http2ClientTransport* transport_;
    explicit PingSystemInterfaceImpl(Http2ClientTransport* transport)
        : transport_(transport) {}
  };

  class KeepAliveInterfaceImpl : public KeepAliveInterface {
   public:
    static std::unique_ptr<KeepAliveInterface> Make(
        Http2ClientTransport* transport) {
      return std::make_unique<KeepAliveInterfaceImpl>(
          KeepAliveInterfaceImpl(transport));
    }

   private:
    explicit KeepAliveInterfaceImpl(Http2ClientTransport* transport)
        : transport_(transport) {}
    Promise<absl::Status> SendPingAndWaitForAck() override {
      return TrySeq(transport_->TriggerWriteCycle(), [transport = transport_] {
        return transport->WaitForPingAck();
      });
    }
    Promise<absl::Status> OnKeepAliveTimeout() override {
      GRPC_HTTP2_CLIENT_DLOG << "Keepalive timeout triggered";

      // TODO(akshitpatel) : [PH2][P2] : The error code here has been chosen
      // based on CHTTP2's usage of GRPC_STATUS_UNAVAILABLE (which corresponds
      // to kRefusedStream). However looking at RFC9113, definition of
      // kRefusedStream doesn't seem to fit this case. We should revisit this
      // and update the error code.
      return Immediate(transport_->HandleError(
          std::nullopt,
          Http2Status::Http2ConnectionError(Http2ErrorCode::kRefusedStream,
                                            "Keepalive timeout")));
    }

    bool NeedToSendKeepAlivePing() override {
      bool need_to_send_ping = false;
      {
        MutexLock lock(&transport_->transport_mutex_);
        need_to_send_ping = (transport_->keepalive_permit_without_calls_ ||
                             transport_->GetActiveStreamCount() > 0);
      }
      return need_to_send_ping;
    }

    // TODO(akshitpatel) : [PH2][P2] : Eventually there should be a separate ref
    // counted struct/class passed to all the transport promises/members. This
    // will help removing back references from the transport members to
    // transport and greatly simpilfy the cleanup path.
    Http2ClientTransport* transport_;
  };

  class GoawayInterfaceImpl : public GoawayInterface {
   public:
    static std::unique_ptr<GoawayInterface> Make(
        Http2ClientTransport* transport) {
      return std::make_unique<GoawayInterfaceImpl>(
          GoawayInterfaceImpl(transport));
    }

    Promise<absl::Status> SendPingAndWaitForAck() override {
      return transport_->ping_manager_.RequestPing(/*on_initiate=*/[] {},
                                                   /*important=*/true);
    }

    void TriggerWriteCycle() override { transport_->TriggerWriteCycle(); }

    uint32_t GetLastAcceptedStreamId() override {
      GRPC_DCHECK(false)
          << "GetLastAcceptedStreamId is not implemented for client transport.";
      LOG(ERROR) << "GetLastAcceptedStreamId is not implemented for client "
                    "transport.";
      return 0;
    }

   private:
    explicit GoawayInterfaceImpl(Http2ClientTransport* transport)
        : transport_(transport) {}

    Http2ClientTransport* transport_;
  };

  GoawayManager goaway_manager_;

  WritableStreams<RefCountedPtr<Stream>> writable_stream_list_;

  absl::Status MaybeAddStreamToWritableStreamList(
      const RefCountedPtr<Stream> stream,
      const StreamDataQueue<ClientMetadataHandle>::EnqueueResult result) {
    if (result.became_writable) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport MaybeAddStreamToWritableStreamList "
             " Stream id: "
          << stream->GetStreamId() << " became writable";
      absl::Status status =
          writable_stream_list_.Enqueue(stream, result.priority);
      if (!status.ok()) {
        return HandleError(
            std::nullopt,
            Http2Status::Http2ConnectionError(
                Http2ErrorCode::kRefusedStream,
                "Failed to enqueue stream to writable stream list"));
      }
    }
    return absl::OkStatus();
  }
  bool SetOnDone(CallHandler call_handler, RefCountedPtr<Stream> stream);
  absl::StatusOr<std::vector<Http2Frame>> DequeueStreamFrames(
      RefCountedPtr<Stream> stream);

  /// Based on channel args, preferred_rx_crypto_frame_sizes are advertised to
  /// the peer
  // TODO(tjagtap) : [PH2][P1] : Plumb this with the necessary frame size flow
  // control workflow corresponding to grpc_chttp2_act_on_flowctl_action
  GRPC_UNUSED bool enable_preferred_rx_crypto_frame_advertisement_;
  MemoryOwner memory_owner_;
  chttp2::TransportFlowControl flow_control_;
  std::shared_ptr<PromiseHttp2ZTraceCollector> ztrace_collector_;
  absl::flat_hash_set<uint32_t> window_update_list_;

  // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
  Waker periodic_updates_waker_;

  // TODO(tjagtap) [PH2][P2][Settings] Set this to true when we receive settings
  // that appear "Urgent". Example - initial window size 0 is urgent because it
  // indicates extreme memory pressure on the server.
  bool should_stall_read_loop_;
  Waker read_loop_waker_;
  Http2Status ParseAndDiscardHeaders(SliceBuffer&& buffer,
                                     bool is_initial_metadata,
                                     bool is_end_headers, uint32_t stream_id,
                                     RefCountedPtr<Stream> stream,
                                     Http2Status&& original_status,
                                     DebugLocation whence = {});
};

// Since the corresponding class in CHTTP2 is about 3.9KB, our goal is to
// remain within that range. When this check fails, please update it to size
// (current size + 32) to make sure that it does not fail each time we add a
// small variable to the class.
GRPC_CHECK_CLASS_SIZE(Http2ClientTransport, 600);

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_CLIENT_TRANSPORT_H
