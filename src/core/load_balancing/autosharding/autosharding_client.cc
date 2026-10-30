//
// Copyright 2026 gRPC authors.
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

#include "src/core/load_balancing/autosharding/autosharding_client.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "proto/autosharding/v1/autosharding.upb.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/upb_utils.h"
#include "upb/mem/arena.hpp"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {

// Same values as the connection backoff in gRPC.
constexpr Duration kInitialBackoff = Duration::Seconds(1);
constexpr double kBackoffMultiplier = 1.6;
constexpr double kBackoffJitter = 0.2;
constexpr Duration kMaxBackoff = Duration::Seconds(120);

// Maximum length of AssignmentAck.error_message, in Unicode code points.
// The builder creates only ASCII messages, so bytes and code points are the
// same.
constexpr size_t kMaxAckErrorMessageLength = 512;

std::string SerializeRequest(
    const google_cloud_autosharding_v1_WatchShardingAssignmentRequest* request,
    upb_Arena* arena) {
  size_t size;
  char* buf =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_serialize(
          request, arena, &size);
  return std::string(buf, size);
}

std::string CreateInitialClientConfigRequest(absl::string_view target,
                                             absl::string_view client_uuid,
                                             int64_t latest_generation) {
  upb::Arena arena;
  auto* request =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_new(
          arena.ptr());
  auto* config =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_mutable_initial_client_config(
          request, arena.ptr());
  google_cloud_autosharding_v1_InitialClientConfig_set_target(
      config, StdStringToUpbString(target));
  google_cloud_autosharding_v1_InitialClientConfig_set_client_uuid(
      config, StdStringToUpbString(client_uuid));
  google_cloud_autosharding_v1_InitialClientConfig_set_latest_generation(
      config, latest_generation);
  return SerializeRequest(request, arena.ptr());
}

std::string CreateAssignmentAckRequest(int64_t generation, bool accepted,
                                       absl::string_view error_message) {
  upb::Arena arena;
  auto* request =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_new(
          arena.ptr());
  auto* ack =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_mutable_assignment_ack(
          request, arena.ptr());
  google_cloud_autosharding_v1_AssignmentAck_set_generation(ack, generation);
  google_cloud_autosharding_v1_AssignmentAck_set_accepted(ack, accepted);
  google_cloud_autosharding_v1_AssignmentAck_set_error_message(
      ack,
      StdStringToUpbString(error_message.substr(0, kMaxAckErrorMessageLength)));
  return SerializeRequest(request, arena.ptr());
}

}  // namespace

//
// AutoshardingClient::StreamCall
//

// One WatchShardingAssignment stream.
//
// To keep at most one message in flight, and to apply flow control, the
// next read starts only after the previous send completes.  The server
// sends nothing before it receives InitialClientConfig, so the first read
// starts after that message is sent.
class AutoshardingClient::StreamCall final
    : public InternallyRefCounted<StreamCall> {
 public:
  explicit StreamCall(RefCountedPtr<AutoshardingClient> client);

  void Orphan() override;

 private:
  class EventHandler;

  bool IsCurrentLocked() const { return client_->stream_.get() == this; }

  void OnRequestSentLocked(bool ok);
  void OnRecvMessageLocked(absl::string_view payload);
  void OnStatusReceivedLocked(absl::Status status);

  // Builds the assignment and sends the AssignmentAck.
  void HandleMetadataLocked(int64_t generation);

  const RefCountedPtr<AutoshardingClient> client_;
  OrphanablePtr<XdsTransport::StreamingCall> streaming_call_;
  // Chunks of the assignment in progress on this stream.  A new stream
  // starts with a new builder, so a partial assignment is never used.
  AutoshardingAssignmentBuilder builder_;
  bool seen_response_ = false;
};

// Moves each stream event into the WorkSerializer.
class AutoshardingClient::StreamCall::EventHandler final
    : public XdsTransport::StreamingCall::EventHandler {
 public:
  explicit EventHandler(RefCountedPtr<StreamCall> call)
      : call_(std::move(call)) {}

  void OnRequestSent(bool ok) override {
    call_->client_->work_serializer_->Run(
        [call = call_, ok]() { call->OnRequestSentLocked(ok); },
        DEBUG_LOCATION);
  }

  void OnRecvMessage(absl::string_view payload) override {
    call_->client_->work_serializer_->Run(
        [call = call_, payload = std::string(payload)]() {
          call->OnRecvMessageLocked(payload);
        },
        DEBUG_LOCATION);
  }

  void OnStatusReceived(absl::Status status) override {
    call_->client_->work_serializer_->Run(
        [call = call_, status = std::move(status)]() mutable {
          call->OnStatusReceivedLocked(std::move(status));
        },
        DEBUG_LOCATION);
  }

 private:
  RefCountedPtr<StreamCall> call_;
};

AutoshardingClient::StreamCall::StreamCall(
    RefCountedPtr<AutoshardingClient> client)
    : client_(std::move(client)) {
  streaming_call_ = client_->transport_->CreateStreamingCall(
      kMethod,
      std::make_unique<EventHandler>(
          // Passing the initial ref here.  This ref goes away when the
          // EventHandler is destroyed.
          RefCountedPtr<StreamCall>(this)),
      XdsTransport::CallOptions().set_wait_for_ready(true));
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding_client " << client_.get() << "] starting stream "
      << this << " for target \"" << client_->target_ << "\"";
  streaming_call_->SendMessage(
      CreateInitialClientConfigRequest(client_->target_, client_->client_uuid_,
                                       client_->last_accepted_generation_));
}

void AutoshardingClient::StreamCall::Orphan() {
  // The initial ref is held by the EventHandler, which is destroyed when
  // streaming_call_ is destroyed.  That may happen later than here, because
  // the transport may hold other refs to streaming_call_.
  streaming_call_.reset();
}

void AutoshardingClient::StreamCall::OnRequestSentLocked(bool ok) {
  if (!IsCurrentLocked()) return;
  // If the send failed, the stream is ending, and OnStatusReceived() will
  // be called.
  if (ok) streaming_call_->StartRecvMessage();
}

void AutoshardingClient::StreamCall::OnRecvMessageLocked(
    absl::string_view payload) {
  if (!IsCurrentLocked()) return;
  upb::Arena arena;
  const auto* response =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_parse(
          payload.data(), payload.size(), arena.ptr());
  // A lost message can be a chunk, so the next assignment would be
  // incomplete.  Fail the stream, so that the server resends everything.
  if (response == nullptr) {
    client_->OnStreamFinishedLocked(
        seen_response_,
        absl::InternalError(
            "cannot parse WatchShardingAssignmentResponse from autosharding "
            "server"));
    return;
  }
  seen_response_ = true;
  if (const auto* metadata =
          google_cloud_autosharding_v1_WatchShardingAssignmentResponse_metadata(
              response);
      metadata != nullptr) {
    // The next read starts when the AssignmentAck is sent.
    HandleMetadataLocked(
        google_cloud_autosharding_v1_AssignmentMetadata_generation(metadata));
    return;
  }
  // LoadReportingConfig is ignored for now (gRFC A119).
  if (const auto* chunk =
          google_cloud_autosharding_v1_WatchShardingAssignmentResponse_chunk(
              response);
      chunk != nullptr) {
    builder_.AddChunk(chunk);
  }
  streaming_call_->StartRecvMessage();
}

void AutoshardingClient::StreamCall::HandleMetadataLocked(int64_t generation) {
  AutoshardingAssignmentBuilder::Result result =
      builder_.Build(generation, client_->last_accepted_generation_);
  const bool accepted =
      result.outcome == AutoshardingAssignmentBuilder::Outcome::kAccepted;
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding_client " << client_.get() << "] assignment generation "
      << generation << (accepted ? " accepted" : " rejected") << ": \""
      << result.error_message << "\"";
  streaming_call_->SendMessage(
      CreateAssignmentAckRequest(generation, accepted, result.error_message));
  switch (result.outcome) {
    case AutoshardingAssignmentBuilder::Outcome::kStale:
      break;
    case AutoshardingAssignmentBuilder::Outcome::kRejected:
      client_->ReportErrorLocked(absl::UnavailableError(
          absl::StrCat("autosharding server sent an invalid assignment: ",
                       result.error_message)));
      break;
    case AutoshardingAssignmentBuilder::Outcome::kAccepted:
      client_->ReportAssignmentLocked(generation,
                                      std::move(*result.assignment));
      break;
  }
}

void AutoshardingClient::StreamCall::OnStatusReceivedLocked(
    absl::Status status) {
  if (!IsCurrentLocked()) return;
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding_client " << client_.get() << "] stream " << this
      << " ended: " << status;
  client_->OnStreamFinishedLocked(seen_response_, std::move(status));
}

//
// AutoshardingClient
//

AutoshardingClient::AutoshardingClient(Args args)
    : transport_(std::move(args.transport)),
      target_(std::move(args.target)),
      client_uuid_(std::move(args.client_uuid)),
      work_serializer_(std::move(args.work_serializer)),
      event_engine_(std::move(args.event_engine)),
      on_result_(std::move(args.on_result)),
      backoff_(BackOff::Options()
                   .set_initial_backoff(kInitialBackoff)
                   .set_multiplier(kBackoffMultiplier)
                   .set_jitter(kBackoffJitter)
                   .set_max_backoff(kMaxBackoff)) {
  initial_assignment_timer_handle_ = event_engine_->RunAfter(
      args.initial_assignment_timeout,
      [self = Ref(DEBUG_LOCATION, "InitialAssignmentTimer")]() mutable {
        ExecCtx exec_ctx;
        auto* self_ptr = self.get();
        self_ptr->work_serializer_->Run(
            [self = std::move(self)]() {
              self->OnInitialAssignmentTimerLocked();
            },
            DEBUG_LOCATION);
      });
  StartStreamLocked();
}

void AutoshardingClient::Orphan() {
  // Stopping the stream and the timers is enough to stop the callback: stream
  // events are ignored unless they come from stream_, and timer events are
  // ignored unless their handle is set.  The callback is kept until the
  // destructor, so it is never destroyed while it runs.
  stream_.reset();
  if (retry_timer_handle_.has_value()) {
    event_engine_->Cancel(*retry_timer_handle_);
    retry_timer_handle_.reset();
  }
  CancelInitialAssignmentTimerLocked();
  Unref(DEBUG_LOCATION, "Orphan");
}

void AutoshardingClient::ResetBackoffLocked() {
  backoff_.Reset();
  transport_->ResetBackoff();
}

void AutoshardingClient::StartStreamLocked() {
  GRPC_CHECK(stream_ == nullptr);
  stream_ = MakeOrphanable<StreamCall>(Ref(DEBUG_LOCATION, "StreamCall"));
}

void AutoshardingClient::OnStreamFinishedLocked(bool seen_response,
                                                absl::Status status) {
  // The chunks of a partial assignment are dropped with the stream.  The
  // accepted generation is kept.
  stream_.reset();
  // Same as XdsClient: reset the backoff if the stream received any response.
  if (seen_response) backoff_.Reset();
  // Start the retry timer before reporting the error, so that the timer is
  // cancelled if the result callback orphans this client.
  StartRetryTimerLocked();
  ReportErrorLocked(absl::UnavailableError(absl::StrCat(
      "stream to autosharding server failed: ", status.ToString())));
}

void AutoshardingClient::StartRetryTimerLocked() {
  const Duration delay = backoff_.NextAttemptDelay();
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding_client " << this << "] restarting stream in "
      << delay.millis() << "ms";
  retry_timer_handle_ = event_engine_->RunAfter(
      delay, [self = Ref(DEBUG_LOCATION, "RetryTimer")]() mutable {
        ExecCtx exec_ctx;
        auto* self_ptr = self.get();
        self_ptr->work_serializer_->Run(
            [self = std::move(self)]() { self->OnRetryTimerLocked(); },
            DEBUG_LOCATION);
      });
}

void AutoshardingClient::OnRetryTimerLocked() {
  if (!retry_timer_handle_.has_value()) return;
  retry_timer_handle_.reset();
  StartStreamLocked();
}

void AutoshardingClient::OnInitialAssignmentTimerLocked() {
  if (!initial_assignment_timer_handle_.has_value()) return;
  initial_assignment_timer_handle_.reset();
  ReportErrorLocked(absl::UnavailableError(
      "timed out waiting for initial assignment from autosharding server"));
}

void AutoshardingClient::CancelInitialAssignmentTimerLocked() {
  if (!initial_assignment_timer_handle_.has_value()) return;
  event_engine_->Cancel(*initial_assignment_timer_handle_);
  initial_assignment_timer_handle_.reset();
}

void AutoshardingClient::ReportAssignmentLocked(
    int64_t generation, AutoshardingAssignment assignment) {
  // Update the state before the callback, because the callback can orphan
  // this client.
  last_accepted_generation_ = generation;
  CancelInitialAssignmentTimerLocked();
  on_result_(std::move(assignment));
}

void AutoshardingClient::ReportErrorLocked(absl::Status status) {
  if (HasReportedAssignmentLocked()) return;
  // The first result, whether an assignment or an error, ends the wait for
  // the initial assignment (gRFC A119).
  CancelInitialAssignmentTimerLocked();
  on_result_(std::move(status));
}

}  // namespace grpc_core
