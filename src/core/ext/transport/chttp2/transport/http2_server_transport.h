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
#include "src/core/lib/promise/map.h"
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
#include "absl/meta/type_traits.h"
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
class Http2ServerTransport final : public ServerTransport,
                                   public channelz::DataSource {
  // TODO(tjagtap) : [PH2][P3] Move the definitions to the header for better
  // inlining. For now definitions are in the cc file to
  // reduce cognitive load in the header.
 public:
  //////////////////////////////////////////////////////////////////////////////
  // Constructor, Destructor etc.
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

  //////////////////////////////////////////////////////////////////////////////
  // Deprecated Stuff

  // TODO(tjagtap) : [PH2][EXT] : Remove after event engine rollout
  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}

  //////////////////////////////////////////////////////////////////////////////
  // Transport Functions

  void SetCallDestination(
      RefCountedPtr<UnstartedCallDestination> call_destination) override;

  void PerformOp(grpc_transport_op*) override;

  void Orphan() override;

  bool AreTransportFlowControlTokensAvailable() {
    return flow_control_.remote_window() > 0;
  }

  void SpawnTransportLoops();

  //////////////////////////////////////////////////////////////////////////////
  // Channelz and ZTrace

  std::unique_ptr<channelz::ZTrace> GetZTrace(absl::string_view name) override {
    if (name == "transport_frames") return ztrace_collector_->MakeZTrace();
    return nullptr;
  }

  RefCountedPtr<channelz::SocketNode> GetSocketNode() const override;

  void AddData(channelz::DataSink sink) override;
  void SpawnAddChannelzData(RefCountedPtr<Party> party,
                            channelz::DataSink sink);

  //////////////////////////////////////////////////////////////////////////////
  // Watchers

  void StartWatch(RefCountedPtr<StateWatcher> watcher) override;
  void StopWatch(RefCountedPtr<StateWatcher> watcher) override;

  //////////////////////////////////////////////////////////////////////////////
  // Test Only Functions

  template <typename Factory>
  void TestOnlySpawnPromise(absl::string_view name, Factory&& factory) {
    SpawnInfallible(general_party_, name, std::forward<Factory>(factory));
  }

  absl::Status TestOnlyTriggerWriteCycle() { return TriggerWriteCycle(); }

  auto TestOnlySendPing(absl::AnyInvocable<void()> on_initiate,
                        bool important = false) {
    return ping_manager_->RequestPing(std::move(on_initiate), important);
  }

  int64_t TestOnlyTransportFlowControlWindow();
  int64_t TestOnlyGetStreamFlowControlWindow(const uint32_t stream_id);

 private:
  //////////////////////////////////////////////////////////////////////////////
  // Watchers

  void StartConnectivityWatch(
      grpc_connectivity_state state,
      OrphanablePtr<ConnectivityStateWatcherInterface> watcher);

  void StopConnectivityWatch(ConnectivityStateWatcherInterface* watcher);

  void NotifyStateWatcherOnDisconnectLocked(
      absl::Status status, StateWatcher::DisconnectInfo disconnect_info)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&transport_mutex_);

  //////////////////////////////////////////////////////////////////////////////
  // Transport Read Path

  // Synchronous functions for processing each type of frame
  Http2Status ProcessIncomingFrame(Http2DataFrame&& frame);
  Http2Status ProcessIncomingFrame(Http2HeaderFrame&& frame);
  Http2Status ProcessIncomingFrame(Http2RstStreamFrame&& frame);
  Http2Status ProcessIncomingFrame(Http2SettingsFrame&& frame);
  Http2Status ProcessIncomingFrame(Http2PingFrame&& frame);
  Http2Status ProcessIncomingFrame(Http2GoawayFrame&& frame);
  Http2Status ProcessIncomingFrame(Http2WindowUpdateFrame&& frame);
  Http2Status ProcessIncomingFrame(Http2ContinuationFrame&& frame);
  Http2Status ProcessIncomingFrame(Http2SecurityFrame&& frame);
  Http2Status ProcessIncomingFrame(Http2UnknownFrame&& frame);
  Http2Status ProcessIncomingFrame(Http2EmptyFrame&& frame);

  Http2Status ProcessMetadata(RefCountedPtr<Stream> stream);

  Http2Status ParseAndDiscardHeaders(SliceBuffer&& buffer, bool is_end_headers,
                                     Stream* stream,
                                     Http2Status&& original_status,
                                     DebugLocation whence = {});

  // Returns a promise that will process one HTTP2 frame.
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Http2Status
  ProcessOneIncomingFrame(Http2Frame&& frame) {
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::ProcessOneIncomingFrame Factory";
    return std::visit(
        [this](auto&& frame) {
          return ProcessIncomingFrame(std::forward<decltype(frame)>(frame));
        },
        std::forward<Http2Frame>(frame));
  }

  auto ReadAndProcessOneFrame();

  // Returns a promise to keep reading in a Loop till a fail/close is received.
  auto ReadLoop();

  //////////////////////////////////////////////////////////////////////////////
  // Transport Write Path

  // Triggers a write cycle. If successful, returns true.
  // If failed, calls HandleError and returns false.
  bool TriggerWriteCycleOrHandleError(DebugLocation whence = {}) {
    absl::Status status = TriggerWriteCycle(whence);
    if (GPR_LIKELY(status.ok())) return true;
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::TriggerWriteCycleOrHandleError failed with "
           "status: "
        << status << " at " << whence.file() << ":" << whence.line();
    GRPC_UNUSED absl::Status unused_status =
        HandleError(std::nullopt, ToHttpOkOrConnError(status), whence);
    return false;
  }

  // Write time sensitive control frames to the endpoint. Frames sent from here
  // will be GOAWAY, SETTINGS, PING and PING acks, WINDOW_UPDATE and
  // Custom gRPC security frame.
  // These frames are written to the endpoint in a single endpoint write. If any
  // module needs to take action after the write (for cases like spawning
  // timeout promises), they MUST plug the call in the
  // NotifyControlFramesWriteDone.

  // Prepares all the HTTP2 control frames that are to be sent out in this write
  // cycle. The modules can choose to either trigger an endpoint write for the
  // frames or defer the write with the stream specific frames. In most cases,
  // the frames are deferred and a single write is triggered for all the
  // frames.
  // absl::Status PrepareControlFrames();

  // If there are any urgent frames this would trigger an additional endpoint
  // write. CAUTION: This will add significant overhead if used for non-urgent
  // frames.
  // auto MaybeWriteUrgentFrames();

  // Notify the modules that an endpoint write is done. This corresponds to the
  // generic endpoint write that happens in the MultiplexerLoop.
  // void NotifyFramesWriteDone();

  // Notify the modules that an urgent endpoint write is done. If some module
  // add frames to this buffer in PrepareControlFrames, they can use this to
  // do post processing after the write is done.
  // void NotifyUrgentFramesWriteDone();

  // Returns a promise to keep draining control frames and data frames from all
  // the writable streams and write to the endpoint.
  // auto MultiplexerLoop();

  // Returns a promise to fetch data from the callhandler and pass it further
  // down towards the endpoint.
  // auto CallOutboundLoop(RefCountedPtr<Stream> stream);

  // TODO(akshitpatel) : [PH2][P0] : Delete when implementing write loop.
  auto WriteFromQueue();

  // TODO(akshitpatel) : [PH2][P0] : Delete when implementing write loop.
  auto WriteLoop();

  // Force triggers a transport write cycle
  absl::Status TriggerWriteCycle(DebugLocation whence = {}) {
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::TriggerWriteCycle invoked from "
        << whence.file() << ":" << whence.line();
    return writable_stream_list_.ForceReadyForWrite();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Spawn Helpers and Promise Helpers

  template <typename Promise,
            std::enable_if_t<std::is_same_v<decltype(std::declval<Promise>()()),
                                            Poll<absl::Status>>,
                             bool> = true>
  auto UntilTransportClosed(Promise&& promise) {
    return Race(Map(transport_closed_latch_.Wait(),
                    [self = RefAsSubclass<Http2ServerTransport>()](Empty) {
                      GRPC_HTTP2_SERVER_DLOG << "Transport closed";
                      return absl::CancelledError("Transport closed");
                    }),
                std::forward<Promise>(promise));
  }

  template <typename Promise,
            std::enable_if_t<std::is_same_v<decltype(std::declval<Promise>()()),
                                            Poll<Empty>>,
                             bool> = true>
  auto UntilTransportClosed(Promise&& promise) {
    return Race(Map(transport_closed_latch_.Wait(),
                    [self = RefAsSubclass<Http2ServerTransport>()](Empty) {
                      GRPC_HTTP2_SERVER_DLOG << "Transport closed";
                      return Empty{};
                    }),
                std::forward<Promise>(promise));
  }

  // Spawns an infallible promise on the given party.
  template <typename Factory>
  void SpawnInfallible(RefCountedPtr<Party> party, absl::string_view name,
                       Factory&& factory) {
    party->Spawn(name, std::forward<Factory>(factory), [](Empty) {});
  }

  // Spawns an infallible promise on the transport party.
  template <typename Factory>
  void SpawnInfallibleTransportParty(absl::string_view name,
                                     Factory&& factory) {
    SpawnInfallible(general_party_, name, std::forward<Factory>(factory));
  }

  // Spawns a promise on the transport party. If the promise returns a non-ok
  // status, it is handled by closing the transport with the corresponding
  // status.
  template <typename Factory>
  void SpawnGuardedTransportParty(absl::string_view name, Factory&& factory) {
    general_party_->Spawn(
        name, std::forward<Factory>(factory),
        [self = RefAsSubclass<Http2ServerTransport>()](absl::Status status) {
          if (!status.ok()) {
            GRPC_UNUSED absl::Status error = self->HandleError(
                /*stream_id=*/std::nullopt, ToHttpOkOrConnError(status));
          }
        });
  }

  template <typename Factory, typename OnDone>
  void SpawnWithOnDoneTransportParty(absl::string_view name, Factory&& factory,
                                     OnDone&& on_done) {
    general_party_->Spawn(name, std::forward<Factory>(factory),
                          std::forward<OnDone>(on_done));
  }

  //////////////////////////////////////////////////////////////////////////////
  // Endpoint Helpers

  // Callers MUST ensure that the transport is not destroyed till the promise is
  // resolved or cancelled.
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto EndpointReadSlice(
      const size_t num_bytes) {
    return Map(endpoint_.ReadSlice(num_bytes),
               [this, num_bytes](absl::StatusOr<Slice> status) {
                 OnEndpointRead(status.ok(), num_bytes);
                 return status;
               });
  }

  // Callers MUST ensure that the transport is not destroyed till the promise is
  // resolved or cancelled.
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto EndpointRead(
      const size_t num_bytes) {
    return Map(endpoint_.Read(num_bytes),
               [this, num_bytes](absl::StatusOr<SliceBuffer> status) {
                 OnEndpointRead(status.ok(), num_bytes);
                 return status;
               });
  }

  void OnEndpointRead(const bool is_ok, const size_t num_bytes) {
    if (is_ok) {
      keepalive_manager_->GotData();
      ztrace_collector_->Append(PromiseEndpointReadTrace{num_bytes});
    }
  }

  auto EndpointWrite(SliceBuffer&& output_buf);

  // Serialize and write the frames in the write cycle to the endpoint.
  auto SerializeAndWrite();

  //////////////////////////////////////////////////////////////////////////////
  // Settings

  // auto WaitForSettingsTimeoutOnDone();
  // void MaybeSpawnWaitForSettingsTimeout();
  // void EnforceLatestIncomingSettings();

  //////////////////////////////////////////////////////////////////////////////
  // Flow Control and BDP

  // Processes the flow control action and take necessary steps.
  void ActOnFlowControlAction(const chttp2::FlowControlAction& action,
                              Stream* stream);

  // void MaybeGetWindowUpdateFrames(FrameSender& frame_sender);

  auto FlowControlPeriodicUpdateLoop();

  // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
  void AddPeriodicUpdatePromiseWaker() {
    periodic_updates_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
  }

  // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
  void WakeupPeriodicUpdatePromise() { periodic_updates_waker_.Wakeup(); }

  //////////////////////////////////////////////////////////////////////////////
  // Stream List Operations

  RefCountedPtr<Stream> LookupStream(uint32_t stream_id);

  // void AddToStreamList(RefCountedPtr<Stream> stream);

  // absl::Status MaybeAddStreamToWritableStreamList(
  //     const RefCountedPtr<Stream> stream,
  //     const StreamDataQueue<ClientMetadataHandle>::StreamWritabilityUpdate
  //         result);

  // Returns the next stream id. If the next stream id is not available, it
  // returns std::nullopt. MUST be called from the transport party.
  // absl::StatusOr<uint32_t> NextStreamId();

  // Returns the next stream id without incrementing it. MUST be called from the
  // transport party.
  // uint32_t PeekNextStreamId() const { return next_stream_id_; }

  // Returns the last stream id sent by the transport. If no streams were sent,
  // returns 0. MUST be called from the transport party.
  // uint32_t GetLastStreamId() const {
  //   const uint32_t next_stream_id = PeekNextStreamId();
  //   return (next_stream_id > 1) ? (next_stream_id - 2) : 0;
  // }

  inline uint32_t GetActiveStreamCountLocked() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(transport_mutex_) {
    // TODO(tjagtap) : [PH2][P1] : Check if impl needs to change for server.
    return stream_list_.size();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Stream Operations

  // Returns a promise to fetch data from the callhandler and pass it further
  // down towards the endpoint.
  // auto CallOutboundLoop(CallHandler call_handler, RefCountedPtr<Stream>
  // stream,
  //                       ClientMetadataHandle metadata);

  // absl::Status InitializeStream(Stream& stream);

  // absl::Status DequeueStreamFrames(RefCountedPtr<Stream> stream,
  //                                  WriteCycle& write_cycle);

  // Runs on the call party.
  // std::optional<RefCountedPtr<Stream>> MakeStream(CallHandler call_handler);

  // void BeginCloseStream(RefCountedPtr<Stream> stream,
  //                       std::optional<uint32_t> reset_stream_error_code,
  //                       ServerMetadataHandle&& metadata,
  //                       DebugLocation whence = {});

  // This function MUST be idempotent.
  void CloseStream(uint32_t stream_id, absl::Status status,
                   DebugLocation whence = {}) {
    LOG(INFO) << "Http2ServerTransport::CloseStream for stream id=" << stream_id
              << " status=" << status << " location=" << whence.file() << ":"
              << whence.line();
    // TODO(akshitpatel) : [PH2][P2] : Implement this.
  }

  // This function MUST be idempotent.
  // void CloseStream(Stream& stream, CloseStreamArgs args,
  //                  DebugLocation whence = {});

  //////////////////////////////////////////////////////////////////////////////
  // Ping Keepalive and Goaway

  // void MaybeSpawnPingTimeout(std::optional<uint64_t> opaque_data);
  // void MaybeSpawnDelayedPing(std::optional<Duration> delayed_ping_wait);

  auto SendPing(absl::AnyInvocable<void()> on_initiate, bool important) {
    return ping_manager_->RequestPing(std::move(on_initiate), important);
  }

  auto WaitForPingAck() { return ping_manager_->WaitForPingAck(); }

  // Duration NextAllowedPingInterval() {
  //   MutexLock lock(&transport_mutex_);
  //   return (!keepalive_permit_without_calls_ &&
  //           GetActiveStreamCountLocked() == 0)
  //              ? Duration::Hours(2)
  //              : Duration::Seconds(1);
  // }

  // absl::Status AckPing(uint64_t opaque_data);

  // void MaybeSpawnKeepaliveLoop();

  // uint32_t GetMaxAllowedStreamId() const;
  // void SetMaxAllowedStreamId(uint32_t max_allowed_stream_id);

  //////////////////////////////////////////////////////////////////////////////
  // Error Path and Close Path

  // void MaybeSpawnCloseTransport(Http2Status http2_status,
  //                               DebugLocation whence = {});

  // bool CanCloseTransportLocked() const
  //     ABSL_EXCLUSIVE_LOCKS_REQUIRED(transport_mutex_);

  // This function MUST run on the transport party.
  void CloseTransport() {}

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
    // TODO(akshitpatel) : [PH2][P0] : Implement this. And remove the log.
    GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::HandleError for stream id="
                           << (stream_id.has_value() ? absl::StrCat(*stream_id)
                                                     : "nullopt")
                           << " status=" << status.DebugString()
                           << " location=" << whence.file() << ":"
                           << whence.line();
    return absl::OkStatus();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Misc Transport Stuff

  // void ReportDisconnection(const absl::Status& status,
  //                          StateWatcher::DisconnectInfo disconnect_info,
  //                          const char* reason);

  // void ReportDisconnectionLocked(const absl::Status& status,
  //                                StateWatcher::DisconnectInfo
  //                                disconnect_info, const char* reason)
  //     ABSL_EXCLUSIVE_LOCKS_REQUIRED(&transport_mutex_);

  // bool SetOnDone(CallHandler call_handler, RefCountedPtr<Stream> stream);

  void ReadChannelArgs(const ChannelArgs& channel_args,
                       TransportChannelArgs& args);

  auto SecurityFrameLoop();

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
        : transport_(transport) {}
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
        Http2ServerTransport* transport);

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
        : transport_(transport) {}
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

  GRPC_UNUSED uint32_t next_stream_id_;
  HPackCompressor encoder_;
  HPackParser parser_;
  GRPC_UNUSED bool is_transport_closed_ ABSL_GUARDED_BY(transport_mutex_) =
      false;
  Latch<void> transport_closed_latch_;

  ConnectivityStateTracker state_tracker_ ABSL_GUARDED_BY(transport_mutex_){
      "http2_server", GRPC_CHANNEL_READY};

  RefCountedPtr<StateWatcher> watcher_ ABSL_GUARDED_BY(transport_mutex_);

  GRPC_UNUSED bool should_reset_ping_clock_;
  GRPC_UNUSED bool is_first_write_;
  IncomingMetadataTracker incoming_headers_;

  // Transport wide write context. This is used to track the state of the
  // transport during write cycles.
  TransportWriteContext transport_write_context_;

  // Tracks the max allowed stream id. Currently this is only set on receiving a
  // graceful GOAWAY frame.
  GRPC_UNUSED uint32_t max_allowed_stream_id_ = RFC9113::kMaxStreamId31Bit;

  // Duration between two consecutive keepalive pings.
  Duration keepalive_time_;
  GRPC_UNUSED bool test_only_ack_pings_;
  std::optional<PingManager> ping_manager_;
  std::optional<KeepaliveManager> keepalive_manager_;

  // Flags
  bool keepalive_permit_without_calls_;

  GoawayManager goaway_manager_;

  WritableStreams<RefCountedPtr<Stream>> writable_stream_list_;

  /// Based on channel args, preferred_rx_crypto_frame_sizes are advertised to
  /// the peer
  GRPC_UNUSED bool enable_preferred_rx_crypto_frame_advertisement_;
  GRPC_UNUSED RefCountedPtr<SecurityFrameHandler> security_frame_handler_;
  MemoryOwner memory_owner_;
  chttp2::TransportFlowControl flow_control_;
  std::shared_ptr<PromiseHttp2ZTraceCollector> ztrace_collector_;

  // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
  Waker periodic_updates_waker_;

  Http2ReadContext reader_state_;
};

// TODO(tjagtap) : [PH2][P1] : Handle the case where a Server receives two
// header frames. Which means that the client sent trailing metadata. While we
// dont expect a gRPC C++ peer to send trailing metadata, not handling it might
// break interop tests and genuine interop cases.

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SERVER_TRANSPORT_H
