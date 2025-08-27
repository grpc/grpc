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

#include <cstdint>
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/ext/transport/chttp2/transport/keepalive.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/ext/transport/chttp2/transport/ping_promise.h"
#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"
#include "src/core/ext/transport/chttp2/transport/writable_streams.h"
#include "src/core/lib/promise/inter_activity_mutex.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"

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

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P3] : Update the experimental status of the code before
// http2 rollout begins.
class Http2ClientTransport final : public ClientTransport {
  // TODO(tjagtap) : [PH2][P3] Move the definitions to the header for better
  // inlining. For now definitions are in the cc file to
  // reduce cognitive load in the header.
 public:
  Http2ClientTransport(
      PromiseEndpoint endpoint, GRPC_UNUSED const ChannelArgs& channel_args,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine,
      grpc_closure* on_receive_settings);

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

  void Orphan() override;
  void AbortWithError();

  RefCountedPtr<channelz::SocketNode> GetSocketNode() const override {
    return nullptr;
  }

  auto TestOnlyEnqueueOutgoingFrame(Http2Frame frame) {
    // TODO(tjagtap) : [PH2][P3] : See if making a sender in the constructor
    // and using that always would be more efficient.
    const size_t tokens = GetFrameMemoryUsage(frame);
    return AssertResultType<absl::Status>(Map(
        outgoing_frames_.MakeSender().Send(std::move(frame), tokens),
        [](StatusFlag status) {
          GRPC_HTTP2_CLIENT_DLOG
              << "Http2ClientTransport::TestOnlyEnqueueOutgoingFrame status="
              << status;
          return (status.ok()) ? absl::OkStatus()
                               : absl::InternalError("Failed to enqueue frame");
        }));
  }

  auto TestOnlySendPing(absl::AnyInvocable<void()> on_initiate,
                        bool important = false) {
    return ping_manager_.RequestPing(std::move(on_initiate), important);
  }

  template <typename Factory>
  auto TestOnlySpawnPromise(absl::string_view name, Factory factory) {
    return general_party_->Spawn(name, std::move(factory), [](auto) {});
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
  Http2Status ProcessMetadata(uint32_t stream_id, HeaderAssembler& assembler,
                              CallHandler& call,
                              bool& did_push_initial_metadata,
                              bool& did_push_trailing_metadata);

  // Reading from the endpoint.

  // Returns a promise to keep reading in a Loop till a fail/close is
  // received.
  auto ReadLoop();

  // Returns a promise that will read and process one HTTP2 frame.
  auto ReadAndProcessOneFrame();

  // Returns a promise that will process one HTTP2 frame.
  auto ProcessOneFrame(Http2Frame frame);

  // Returns a promise that will do the cleanup after the ReadLoop ends.
  auto OnReadLoopEnded();

  // Writing to the endpoint.

  // Write the frames from MPSC queue to the endpoint. Frames sent from here
  // will be DATA, HEADER, CONTINUATION and SETTINGS_ACK.  It is essential to
  // preserve the order of these frames at the time of write.
  auto WriteFromQueue(std::vector<Http2Frame>&& frames);

  // Write time sensitive control frames to the endpoint. Frames sent from here
  // will be:
  // 1. SETTINGS - This is first because for a new connection, SETTINGS MUST be
  //               the first frame to be written onto a connection as per
  //               RFC9113.
  // 2. GOAWAY - This is second because if this is the final GoAway, then we may
  //             not need to send anything else to the peer.
  // 3. PING and PING acks.
  // 4. WINDOW_UPDATE
  // 5. Custom gRPC security frame
  // These frames are written to the endpoint in a single endpoint write. If any
  // module needs to take action after the write (for cases like spawning
  // timeout promises), they MUST plug the call in the
  // NotifyControlFramesWriteDone.
  auto WriteControlFrames();

  // Notify the control frames modules that the endpoint write is done.
  void NotifyControlFramesWriteDone();

  // Returns a promise to keep writing in a Loop till a fail/close is
  // received.
  auto WriteLoop();

  // Returns a promise that will do the cleanup after the WriteLoop ends.
  auto OnWriteLoopEnded();

  // Returns a promise to keep draining data and control frames from all the
  // active streams. This includes all stream specific frames like data, header,
  // continuation and reset stream frames.
  auto StreamMultiplexerLoop();

  // Returns a promise that will do the cleanup after the StreamMultiplexerLoop
  // ends.
  auto OnStreamMultiplexerLoopEnded();

  // Returns a promise to fetch data from the callhandler and pass it further
  // down towards the endpoint.
  auto CallOutboundLoop(CallHandler call_handler, uint32_t stream_id,
                        InterActivityMutex<uint32_t>::Lock lock,
                        ClientMetadataHandle metadata);

  // Returns a promise to enqueue a frame to MPSC
  auto EnqueueOutgoingFrame(Http2Frame frame) {
    // TODO(tjagtap) : [PH2][P3] : See if making a sender in the constructor
    // and using that always would be more efficient.
    const size_t tokens = GetFrameMemoryUsage(frame);
    return AssertResultType<absl::Status>(Map(
        outgoing_frames_.MakeSender().Send(std::move(frame), tokens),
        [self = RefAsSubclass<Http2ClientTransport>()](StatusFlag status) {
          GRPC_HTTP2_CLIENT_DLOG
              << "Http2ClientTransport::EnqueueOutgoingFrame status=" << status;
          return (status.ok())
                     ? absl::OkStatus()
                     : self->HandleError(Http2Status::AbslConnectionError(
                           absl::StatusCode::kInternal,
                           "Failed to enqueue frame"));
        }));
  }

  // Force triggers a transport write cycle
  auto TriggerWriteCycle() { return EnqueueOutgoingFrame(Http2EmptyFrame{}); }

  RefCountedPtr<Party> general_party_;

  PromiseEndpoint endpoint_;
  Http2SettingsManager settings_;
  SettingsTimeoutManager transport_settings_;

  Http2FrameHeader current_frame_header_;

  // Managing the streams
  struct Stream : public RefCounted<Stream> {
    explicit Stream(CallHandler call, const uint32_t stream_id1,
                    bool allow_true_binary_metadata_peer,
                    bool allow_true_binary_metadata_acked)
        : call(std::move(call)),
          stream_state(HttpStreamState::kIdle),
          stream_id(stream_id1),
          header_assembler(stream_id1, allow_true_binary_metadata_acked),
          did_push_initial_metadata(false),
          did_push_trailing_metadata(false),
          data_queue(MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
              /*is_client*/ true, /*stream_id*/ stream_id1,
              /*queue_size*/ kStreamQueueSize,
              allow_true_binary_metadata_peer)) {}

    ////////////////////////////////////////////////////////////////////////////
    // Data Queue Helpers

    auto EnqueueInitialMetadata(ClientMetadataHandle&& metadata) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::Stream::EnqueueInitialMetadata stream_id="
          << stream_id;
      return data_queue->EnqueueInitialMetadata(std::move(metadata));
    }

    auto EnqueueTrailingMetadata(ClientMetadataHandle&& metadata) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::Stream::EnqueueTrailingMetadata stream_id="
          << stream_id;
      return data_queue->EnqueueTrailingMetadata(std::move(metadata));
    }

    auto EnqueueMessage(MessageHandle&& message) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::Stream::EnqueueMessage stream_id="
          << stream_id
          << " with payload size = " << message->payload()->Length();
      return data_queue->EnqueueMessage(std::move(message));
    }

    auto EnqueueHalfClosed() {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::Stream::EnqueueHalfClosed stream_id="
          << stream_id;
      return data_queue->EnqueueHalfClosed();
    }

    auto EnqueueResetStream(const uint32_t error_code) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::Stream::EnqueueResetStream stream_id="
          << stream_id << " with error_code = " << error_code;
      return data_queue->EnqueueResetStream(error_code);
    }

    auto DequeueFrames(const uint32_t transport_tokens,
                       const uint32_t max_frame_length,
                       HPackCompressor& encoder) {
      return data_queue->DequeueFrames(transport_tokens, max_frame_length,
                                       encoder);
    }

    ////////////////////////////////////////////////////////////////////////////
    // Stream State Management

    // Modify the stream state
    // The possible stream transitions are as follows:
    // kIdle -> kOpen
    // kOpen -> kClosed/kHalfClosedLocal/kHalfClosedRemote
    // kHalfClosedLocal/kHalfClosedRemote -> kClosed
    // kClosed -> kClosed
    void SentInitialMetadata() {
      DCHECK(stream_state == HttpStreamState::kIdle);
      stream_state = HttpStreamState::kOpen;
    }

    void MarkHalfClosedLocal() {
      switch (stream_state) {
        case HttpStreamState::kIdle:
          DCHECK(false) << "MarkHalfClosedLocal called for an idle stream";
          break;
        case HttpStreamState::kOpen:
          stream_state = HttpStreamState::kHalfClosedLocal;
          break;
        case HttpStreamState::kHalfClosedRemote:
          stream_state = HttpStreamState::kClosed;
          break;
        case HttpStreamState::kHalfClosedLocal:
          break;
        case HttpStreamState::kClosed:
          DCHECK(false) << "MarkHalfClosedLocal called for a closed stream";
          break;
      }
    }

    void MarkHalfClosedRemote() {
      switch (stream_state) {
        case HttpStreamState::kIdle:
          DCHECK(false) << "MarkHalfClosedRemote called for an idle stream";
          break;
        case HttpStreamState::kOpen:
          stream_state = HttpStreamState::kHalfClosedRemote;
          break;
        case HttpStreamState::kHalfClosedLocal:
          stream_state = HttpStreamState::kClosed;
          break;
        case HttpStreamState::kHalfClosedRemote:
          break;
        case HttpStreamState::kClosed:
          DCHECK(false) << "MarkHalfClosedRemote called for a closed stream";
          break;
      }
    }

    HttpStreamState GetStreamState() const { return stream_state; }

    inline bool IsClosed() const {
      return stream_state == HttpStreamState::kClosed;
    }

    CallHandler call;
    // TODO(akshitpatel) : [PH2][P3] : Investigate if this needs to be atomic.
    HttpStreamState stream_state;
    const uint32_t stream_id;
    GrpcMessageAssembler assembler;
    HeaderAssembler header_assembler;
    // TODO(akshitpatel) : [PH2][P2] : StreamQ should maintain a flag that
    // tracks if the half close has been sent for this stream. This flag is used
    // to notify the mixer that this stream is closed for
    // writes(HalfClosedLocal). When the mixer dequeues the last message for
    // the streamQ, it will mark the stream as closed for writes and send a
    // frame with end_stream or set the end_stream flag in the last data
    // frame being sent out. This is done as the stream state should not
    // transition to HalfClosedLocal till the end_stream frame is sent.
    bool did_push_initial_metadata;
    bool did_push_trailing_metadata;
    RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> data_queue;
  };

  uint32_t NextStreamId(
      InterActivityMutex<uint32_t>::Lock& next_stream_id_lock) {
    const uint32_t stream_id = *next_stream_id_lock;
    if (stream_id > RFC9113::kMaxStreamId31Bit) {
      // TODO(tjagtap) : [PH2][P3] : Handle case if transport runs out of stream
      // ids
      // RFC9113 : Stream identifiers cannot be reused. Long-lived connections
      // can result in an endpoint exhausting the available range of stream
      // identifiers. A client that is unable to establish a new stream
      // identifier can establish a new connection for new streams. A server
      // that is unable to establish a new stream identifier can send a GOAWAY
      // frame so that the client is forced to open a new connection for new
      // streams.
    }
    // RFC9113 : Streams initiated by a client MUST use odd-numbered stream
    // identifiers.
    (*next_stream_id_lock) += 2;
    return stream_id;
  }

  MpscReceiver<Http2Frame> outgoing_frames_;

  Mutex transport_mutex_;
  // TODO(tjagtap) : [PH2][P2] : Add to map in StartCall and clean this
  // mapping up in the on_done of the CallInitiator or CallHandler
  absl::flat_hash_map<uint32_t, RefCountedPtr<Stream>> stream_list_
      ABSL_GUARDED_BY(transport_mutex_);

  // Mutex to preserve the order of headers being sent out for new streams.
  // This also tracks the stream_id for creating new streams.
  InterActivityMutex<uint32_t> stream_id_mutex_;
  HPackCompressor encoder_;
  HPackParser parser_;
  bool is_transport_closed_ ABSL_GUARDED_BY(transport_mutex_) = false;

  ConnectivityStateTracker state_tracker_ ABSL_GUARDED_BY(transport_mutex_){
      "http2_client", GRPC_CHANNEL_READY};

  bool MakeStream(CallHandler call_handler, uint32_t stream_id);

  struct CloseStreamArgs {
    bool close_reads;
    bool close_writes;
    bool send_rst_stream;
    bool push_trailing_metadata;
  };

  // This function MUST be idempotent.
  void CloseStream(uint32_t stream_id, absl::Status status,
                   CloseStreamArgs args, DebugLocation whence = {});

  RefCountedPtr<Http2ClientTransport::Stream> LookupStream(uint32_t stream_id);

  auto EndpointReadSlice(const size_t num_bytes) {
    return Map(endpoint_.ReadSlice(num_bytes),
               [self = RefAsSubclass<Http2ClientTransport>()](
                   absl::StatusOr<Slice> status) {
                 // We are ignoring the case where the read fails and call
                 // GotData() regardless. Reasoning:
                 // 1. It is expected that if the read fails, the transport will
                 //    close and the keepalive loop will be stopped.
                 // 2. It does not seem worth to have an extra condition for the
                 //    success cases which would be way more common.
                 self->keepalive_manager_.GotData();
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
        GRPC_UNUSED absl::Status result =
            self->HandleError(Http2Status::Http2ConnectionError(
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
               [self = RefAsSubclass<Http2ClientTransport>()](
                   absl::StatusOr<SliceBuffer> status) {
                 // We are ignoring the case where the read fails and call
                 // GotData() regardless. Reasoning:
                 // 1. It is expected that if the read fails, the transport will
                 //    close and the keepalive loop will be stopped.
                 // 2. It does not seem worth to have an extra condition for the
                 //    success cases which would be way more common.
                 self->keepalive_manager_.GotData();
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
  absl::Status HandleError(Http2Status status, DebugLocation whence = {}) {
    auto error_type = status.GetType();
    DCHECK(error_type != Http2Status::Http2ErrorType::kOk);

    if (error_type == Http2Status::Http2ErrorType::kStreamError) {
      LOG(ERROR) << "Stream Error: " << status.DebugString();
      CloseStream(current_frame_header_.stream_id, status.GetAbslStreamError(),
                  CloseStreamArgs{
                      /*close_reads=*/true,
                      /*close_writes=*/true,
                      /*send_rst_stream=*/true,
                      /*push_trailing_metadata=*/true,
                  },
                  whence);
      return absl::OkStatus();
    } else if (error_type == Http2Status::Http2ErrorType::kConnectionError) {
      LOG(ERROR) << "Connection Error: " << status.DebugString();
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
  grpc_closure* on_receive_settings_;

  uint32_t max_header_list_size_soft_limit_;

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
    }
  }

  // Ping Helper functions
  Duration NextAllowedPingInterval() {
    MutexLock lock(&transport_mutex_);
    return (!keepalive_permit_without_calls_ && stream_list_.empty())
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
      // TODO(akshitpatel) : [PH2][P2] : Trigger goaway here.
      // Returns a promise that resolves once goaway is sent.
      LOG(INFO) << "Ping timeout at time: " << Timestamp::Now();

      // TODO(akshitpatel) : [PH2][P2] : The error code here has been chosen
      // based on CHTTP2's usage of GRPC_STATUS_UNAVAILABLE (which corresponds
      // to kRefusedStream). However looking at RFC9113, definition of
      // kRefusedStream doesn't seem to fit this case. We should revisit this
      // and update the error code.
      return Immediate(
          transport_->HandleError(Http2Status::Http2ConnectionError(
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
      // TODO(akshitpatel) : [PH2][P2] : Trigger goaway here.
      LOG(INFO) << "Keepalive timeout triggered";

      // TODO(akshitpatel) : [PH2][P2] : The error code here has been chosen
      // based on CHTTP2's usage of GRPC_STATUS_UNAVAILABLE (which corresponds
      // to kRefusedStream). However looking at RFC9113, definition of
      // kRefusedStream doesn't seem to fit this case. We should revisit this
      // and update the error code.
      return Immediate(
          transport_->HandleError(Http2Status::Http2ConnectionError(
              Http2ErrorCode::kRefusedStream, "Keepalive timeout")));
    }

    bool NeedToSendKeepAlivePing() override {
      bool need_to_send_ping = false;
      {
        MutexLock lock(&transport_->transport_mutex_);
        need_to_send_ping = (transport_->keepalive_permit_without_calls_ ||
                             !transport_->stream_list_.empty());
      }
      return need_to_send_ping;
    }

    // TODO(akshitpatel) : [PH2][P2] : Eventually there should be a separate ref
    // counted struct/class passed to all the transport promises/members. This
    // will help removing back references from the transport members to
    // transport and greatly simpilfy the cleanup path.
    Http2ClientTransport* transport_;
  };

  WritableStreams writable_stream_list_;

  absl::Status MaybeAddStreamToWritableStreamList(
      const uint32_t stream_id,
      const StreamDataQueue<ClientMetadataHandle>::EnqueueResult result) {
    if (result.became_writable) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport MaybeAddStreamToWritableStreamList "
             " Stream id: "
          << stream_id << " became writable";
      absl::Status status =
          writable_stream_list_.Enqueue(stream_id, result.priority);
      if (!status.ok()) {
        return HandleError(Http2Status::Http2ConnectionError(
            Http2ErrorCode::kRefusedStream,
            "Failed to enqueue stream to writable stream list"));
      }
    }
    return absl::OkStatus();
  }
  /// Based on channel args, preferred_rx_crypto_frame_sizes are advertised to
  /// the peer
  // TODO(tjagtap) : [PH2][P1] : Plumb this with the necessary frame size flow
  // control workflow corresponding to grpc_chttp2_act_on_flowctl_action
  GRPC_UNUSED bool enable_preferred_rx_crypto_frame_advertisement_;
};

// Since the corresponding class in CHTTP2 is about 3.9KB, our goal is to
// remain within that range. When this check fails, please update it to size
// (current size + 32) to make sure that it does not fail each time we add a
// small variable to the class.
GRPC_CHECK_CLASS_SIZE(Http2ClientTransport, 600);

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_CLIENT_TRANSPORT_H
