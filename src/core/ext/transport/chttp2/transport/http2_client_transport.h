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
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/ext/transport/chttp2/transport/ping_promise.h"
#include "src/core/lib/promise/inter_activity_mutex.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
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

// Max Party Slots (Always): 2

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
          event_engine);

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

  void Orphan() override;
  void AbortWithError();

  RefCountedPtr<channelz::SocketNode> GetSocketNode() const override {
    return nullptr;
  }

  auto TestOnlyEnqueueOutgoingFrame(Http2Frame frame) {
    return AssertResultType<absl::Status>(Map(
        outgoing_frames_.MakeSender().Send(std::move(frame)),
        [](StatusFlag status) {
          HTTP2_CLIENT_DLOG
              << "Http2ClientTransport::TestOnlyEnqueueOutgoingFrame status="
              << status;
          return (status.ok()) ? absl::OkStatus()
                               : absl::InternalError("Failed to enqueue frame");
        }));
  }
  auto TestOnlySendPing(absl::AnyInvocable<void()> on_initiate) {
    return ping_manager_.RequestPing(std::move(on_initiate));
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

  // Read from the MPSC queue and write it.
  auto WriteFromQueue();

  // Returns a promise to keep writing in a Loop till a fail/close is
  // received.
  auto WriteLoop();

  // Returns a promise that will do the cleanup after the WriteLoop ends.
  auto OnWriteLoopEnded();

  // Returns a promise to fetch data from the callhandler and pass it further
  // down towards the endpoint.
  auto CallOutboundLoop(CallHandler call_handler, uint32_t stream_id,
                        InterActivityMutex<uint32_t>::Lock lock,
                        ClientMetadataHandle metadata);

  // Returns a promise to enqueue a frame to MPSC
  auto EnqueueOutgoingFrame(Http2Frame frame) {
    return AssertResultType<absl::Status>(Map(
        outgoing_frames_.MakeSender().Send(std::move(frame)),
        [self = RefAsSubclass<Http2ClientTransport>()](StatusFlag status) {
          HTTP2_CLIENT_DLOG
              << "Http2ClientTransport::EnqueueOutgoingFrame status=" << status;
          return (status.ok())
                     ? absl::OkStatus()
                     : self->HandleError(Http2Status::AbslConnectionError(
                           absl::StatusCode::kInternal,
                           "Failed to enqueue frame"));
        }));
  }

  RefCountedPtr<Party> general_party_;

  PromiseEndpoint endpoint_;
  Http2SettingsManager settings_;

  // TODO(tjagtap) : [PH2][P3] : This is not nice. Fix by using Stapler.
  Http2FrameHeader current_frame_header_;

  // Managing the streams
  struct Stream : public RefCounted<Stream> {
    explicit Stream(CallHandler call, const uint32_t stream_id1)
        : call(std::move(call)),
          stream_state(HttpStreamState::kIdle),
          stream_id(stream_id1) {}

    CallHandler call;
    HttpStreamState stream_state;
    const uint32_t stream_id;
    TransportSendQeueue send_queue;
    GrpcMessageAssembler assembler;
    HeaderAssembler header_assembler;
    // TODO(tjagtap) : [PH2][P2] : Add more members as necessary
  };

  uint32_t NextStreamId(
      InterActivityMutex<uint32_t>::Lock& next_stream_id_lock) {
    const uint32_t stream_id = *next_stream_id_lock;
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

  bool MakeStream(CallHandler call_handler, uint32_t stream_id);
  // This function MUST be idempotent.
  void CloseStream(uint32_t stream_id, absl::Status status,
                   DebugLocation whence = {}) {
    HTTP2_CLIENT_DLOG << "Http2ClientTransport::CloseStream for stream id: "
                      << stream_id << " status=" << status
                      << " location=" << whence.file() << ":" << whence.line();
    // TODO(akshitpatel) : [PH2][P1] : Implement this.
  }
  RefCountedPtr<Http2ClientTransport::Stream> LookupStream(uint32_t stream_id);

  // This function MUST be idempotent.
  void CloseTransport(const Http2Status& status, DebugLocation whence = {}) {
    HTTP2_CLIENT_DLOG << "Http2ClientTransport::CloseTransport status="
                      << status << " location=" << whence.file() << ":"
                      << whence.line();
    // TODO(akshitpatel) : [PH2][P1] : Implement this.
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
    DCHECK(error_type != Http2Status::Http2ErrorType::kOk);

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
  bool bytes_sent_in_last_write_;

  // Ping related members
  Duration keepalive_time_;
  Duration ping_timeout_;
  PingManager ping_manager_;
  std::vector<uint64_t> pending_ping_acks_;

  // Flags
  bool keepalive_permit_without_calls_;

  auto SendPing(absl::AnyInvocable<void()> on_initiate) {
    return ping_manager_.RequestPing(std::move(on_initiate));
  }

  // Ping Helper functions
  // Returns a promise that resolves once a ping frame is written to the
  // endpoint.
  auto CreateAndWritePing(bool ack, uint64_t opaque_data) {
    Http2Frame frame = Http2PingFrame{ack, opaque_data};
    SliceBuffer output_buf;
    Serialize(absl::Span<Http2Frame>(&frame, 1), output_buf);
    return endpoint_.Write(std::move(output_buf), {});
  }

  Duration NextAllowedPingInterval() {
    MutexLock lock(&transport_mutex_);
    return (!keepalive_permit_without_calls_ && stream_list_.empty())
               ? Duration::Hours(2)
               : Duration::Seconds(1);
  }

  auto MaybeSendPing() {
    return ping_manager_.MaybeSendPing(NextAllowedPingInterval(),
                                       ping_timeout_);
  }

  auto MaybeSendPingAcks() {
    return AssertResultType<absl::Status>(If(
        pending_ping_acks_.empty(), [] { return absl::OkStatus(); },
        [this] {
          std::vector<Http2Frame> frames;
          frames.reserve(pending_ping_acks_.size());
          for (auto& opaque_data : pending_ping_acks_) {
            frames.emplace_back(Http2PingFrame{true, opaque_data});
          }
          pending_ping_acks_.clear();
          SliceBuffer output_buf;
          Serialize(absl::Span<Http2Frame>(frames), output_buf);
          return endpoint_.Write(std::move(output_buf), {});
        }));
  }

  class PingSystemInterfaceImpl : public PingInterface {
   public:
    static std::unique_ptr<PingInterface> Make(
        Http2ClientTransport* transport) {
      return std::make_unique<PingSystemInterfaceImpl>(
          PingSystemInterfaceImpl(transport));
    }

    // Returns a promise that resolves once a ping frame is written to the
    // endpoint.
    Promise<absl::Status> SendPing(SendPingArgs args) override {
      return transport_->CreateAndWritePing(args.ack, args.opaque_data);
    }

    Promise<absl::Status> TriggerWrite() override {
      return transport_->EnqueueOutgoingFrame(Http2EmptyFrame{});
    }

    Promise<absl::Status> PingTimeout() override {
      // TODO(akshitpatel) : [PH2][P1] : Trigger goaway here.
      // Returns a promise that resolves once goaway is sent.
      LOG(INFO) << "Ping timeout at time: " << Timestamp::Now();

      return Immediate(
          transport_->HandleError(Http2Status::Http2ConnectionError(
              Http2ErrorCode::kRefusedStream, "Ping timeout")));
    }

   private:
    // TODO(akshitpatel) : [PH2][P1] : Eventually there should be a separate ref
    // counted struct/class passed to all the transport promises/members. This
    // will help removing back references from the transport members to
    // transport and greatly simpilfy the cleanup path.
    Http2ClientTransport* transport_;
    explicit PingSystemInterfaceImpl(Http2ClientTransport* transport)
        : transport_(transport) {}
  };
};

// Since the corresponding class in CHTTP2 is about 3.9KB, our goal is to
// remain within that range. When this check fails, please update it to size
// (current size + 32) to make sure that it does not fail each time we add a
// small variable to the class.
GRPC_CHECK_CLASS_SIZE(Http2ClientTransport, 600);

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_CLIENT_TRANSPORT_H
