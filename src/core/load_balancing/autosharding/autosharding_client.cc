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
#include <optional>
#include <string>
#include <utility>

#include "proto/autosharding/v1/autosharding.upb.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/sync.h"
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

// Reads and writes are independent.  The next read starts as soon as a
// response is processed.  At most one send is in flight; while a send is in
// flight, only the latest AssignmentAck is kept for sending next.
class AutoshardingClient::StreamCall final
    : public InternallyRefCounted<StreamCall> {
 public:
  // Starts the stream: sends InitialClientConfig and starts reads.
  // Must be called while holding client->mu_.  A lock annotation cannot be
  // used on a constructor called through MakeOrphanable(), so the
  // constructor asserts that the lock is held.
  explicit StreamCall(RefCountedPtr<AutoshardingClient> client);

  // Must be called while holding client_->mu_.
  void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS;

 private:
  class EventHandler;

  bool IsCurrentLocked() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&AutoshardingClient::mu_) {
    return client_->stream_.get() == this;
  }

  void OnRequestSent(bool ok);
  void OnRecvMessage(absl::string_view payload);
  void OnStatusReceived(absl::Status status);

  void HandleResultLocked(AutoshardingAssignmentBuilder::Result result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&AutoshardingClient::mu_);
  void SendMessageLocked(std::string payload)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&AutoshardingClient::mu_);

  const RefCountedPtr<AutoshardingClient> client_;
  OrphanablePtr<XdsTransport::StreamingCall> streaming_call_
      ABSL_GUARDED_BY(&AutoshardingClient::mu_);
  bool seen_valid_assignment_ ABSL_GUARDED_BY(&AutoshardingClient::mu_) = false;
  bool send_in_flight_ ABSL_GUARDED_BY(&AutoshardingClient::mu_) = false;
  // The latest AssignmentAck to send when the in-flight send completes.
  std::optional<std::string> pending_ack_
      ABSL_GUARDED_BY(&AutoshardingClient::mu_);
};

class AutoshardingClient::StreamCall::EventHandler final
    : public XdsTransport::StreamingCall::EventHandler {
 public:
  explicit EventHandler(RefCountedPtr<StreamCall> call)
      : call_(std::move(call)) {}

  void OnRequestSent(bool ok) override { call_->OnRequestSent(ok); }
  void OnRecvMessage(absl::string_view payload) override {
    call_->OnRecvMessage(payload);
  }
  void OnStatusReceived(absl::Status status) override {
    call_->OnStatusReceived(std::move(status));
  }

 private:
  RefCountedPtr<StreamCall> call_;
};

AutoshardingClient::StreamCall::StreamCall(
    RefCountedPtr<AutoshardingClient> client)
    : client_(std::move(client)) {
  client_->mu_.AssertHeld();
  streaming_call_ = client_->transport_->CreateStreamingCall(
      kMethod,
      std::make_unique<EventHandler>(Ref(DEBUG_LOCATION, "EventHandler")),
      XdsTransport::CallOptions().set_wait_for_ready(true));
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding_client " << client_.get() << "] starting stream "
      << this << " for target \"" << client_->target_ << "\"";
  SendMessageLocked(CreateInitialClientConfigRequest(
      client_->target_, client_->client_uuid_,
      client_->builder_.last_accepted_generation()));
  streaming_call_->StartRecvMessage();
}

void AutoshardingClient::StreamCall::Orphan() {
  // EventHandler holds its own ref, so this object stays alive until the
  // transport destroys the EventHandler.
  streaming_call_.reset();
  Unref(DEBUG_LOCATION, "Orphan");
}

void AutoshardingClient::StreamCall::SendMessageLocked(std::string payload) {
  if (send_in_flight_) {
    // Older AssignmentAcks are superseded, so keep only the latest one.
    pending_ack_ = std::move(payload);
    return;
  }
  send_in_flight_ = true;
  streaming_call_->SendMessage(std::move(payload));
}

void AutoshardingClient::StreamCall::OnRequestSent(bool ok) {
  MutexLock lock(client_->mu_);
  if (!IsCurrentLocked()) return;
  send_in_flight_ = false;
  // If the send failed, the stream is ending, and OnStatusReceived() will
  // be called.
  if (!ok || !pending_ack_.has_value()) return;
  SendMessageLocked(*std::exchange(pending_ack_, std::nullopt));
}

void AutoshardingClient::StreamCall::OnRecvMessage(absl::string_view payload) {
  upb::Arena arena;
  const auto* response =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_parse(
          payload.data(), payload.size(), arena.ptr());
  MutexLock lock(client_->mu_);
  if (!IsCurrentLocked()) return;
  // A lost message can be a chunk, so the next assignment would be
  // incomplete.  Fail the stream, so that the server resends everything.
  if (response == nullptr) {
    client_->OnStreamFinishedLocked(
        seen_valid_assignment_,
        absl::InternalError(
            "cannot parse WatchShardingAssignmentResponse from autosharding "
            "server"));
    return;
  }
  std::optional<AutoshardingAssignmentBuilder::Result> result =
      client_->builder_.ProcessResponseMessage(response);
  if (result.has_value()) HandleResultLocked(*std::move(result));
  streaming_call_->StartRecvMessage();
}

void AutoshardingClient::StreamCall::HandleResultLocked(
    AutoshardingAssignmentBuilder::Result result) {
  const bool accepted = result.assignment.ok();
  absl::string_view error_message = accepted
                                        ? absl::string_view(result.issues)
                                        : result.assignment.status().message();
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding_client " << client_.get() << "] assignment generation "
      << result.generation << (accepted ? " accepted" : " rejected") << ": \""
      << error_message << "\"";
  SendMessageLocked(
      CreateAssignmentAckRequest(result.generation, accepted, error_message));
  if (accepted) {
    seen_valid_assignment_ = true;
    client_->ReportAssignmentLocked(*std::move(result.assignment));
  } else {
    client_->ReportErrorLocked(absl::UnavailableError(absl::StrCat(
        "autosharding server sent an invalid assignment: ", error_message)));
  }
}

void AutoshardingClient::StreamCall::OnStatusReceived(absl::Status status) {
  MutexLock lock(client_->mu_);
  if (!IsCurrentLocked()) return;
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding_client " << client_.get() << "] stream " << this
      << " ended: " << status;
  client_->OnStreamFinishedLocked(seen_valid_assignment_, std::move(status));
}

//
// AutoshardingClient
//

AutoshardingClient::AutoshardingClient(
    RefCountedPtr<XdsTransport> transport, std::string target,
    std::string client_uuid, Duration initial_assignment_timeout,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    ResultCallback on_result)
    : transport_(std::move(transport)),
      target_(std::move(target)),
      client_uuid_(std::move(client_uuid)),
      event_engine_(std::move(event_engine)),
      on_result_(std::move(on_result)),
      backoff_(BackOff::Options()
                   .set_initial_backoff(kInitialBackoff)
                   .set_multiplier(kBackoffMultiplier)
                   .set_jitter(kBackoffJitter)
                   .set_max_backoff(kMaxBackoff)) {
  MutexLock lock(mu_);
  initial_assignment_timer_handle_ = event_engine_->RunAfter(
      initial_assignment_timeout,
      [self = Ref(DEBUG_LOCATION, "InitialAssignmentTimer")]() mutable {
        ExecCtx exec_ctx;
        self->OnInitialAssignmentTimer();
        self.reset();
      });
  StartStreamLocked();
}

// Defined here, where StreamCall is a complete type.
AutoshardingClient::~AutoshardingClient() = default;

void AutoshardingClient::Orphan() {
  {
    MutexLock lock(mu_);
    orphaned_ = true;
    stream_.reset();
    if (retry_timer_handle_.has_value()) {
      event_engine_->Cancel(*retry_timer_handle_);
      retry_timer_handle_.reset();
    }
    CancelInitialAssignmentTimerLocked();
  }
  Unref(DEBUG_LOCATION, "Orphan");
}

void AutoshardingClient::ResetBackoff() {
  {
    MutexLock lock(mu_);
    backoff_.Reset();
    if (retry_timer_handle_.has_value()) {
      // If the timer callback is already running, it sees that the handle is
      // reset and does nothing.
      event_engine_->Cancel(*retry_timer_handle_);
      retry_timer_handle_.reset();
      StartStreamLocked();
    }
  }
  transport_->ResetBackoff();
}

void AutoshardingClient::StartStreamLocked() {
  GRPC_CHECK(stream_ == nullptr);
  stream_ = MakeOrphanable<StreamCall>(Ref(DEBUG_LOCATION, "StreamCall"));
}

void AutoshardingClient::OnStreamFinishedLocked(bool seen_valid_assignment,
                                                absl::Status status) {
  stream_.reset();
  builder_.ResetPartialAssignment();
  if (seen_valid_assignment) {
    backoff_.Reset();
    StartStreamLocked();
    return;
  }
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
        self->OnRetryTimer();
        self.reset();
      });
}

void AutoshardingClient::OnRetryTimer() {
  MutexLock lock(mu_);
  if (!retry_timer_handle_.has_value()) return;
  retry_timer_handle_.reset();
  StartStreamLocked();
}

void AutoshardingClient::OnInitialAssignmentTimer() {
  MutexLock lock(mu_);
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
    RefCountedPtr<const AutoshardingAssignment> assignment) {
  CancelInitialAssignmentTimerLocked();
  if (!orphaned_) on_result_(Ref(), std::move(assignment));
}

void AutoshardingClient::ReportErrorLocked(absl::Status status) {
  if (HasReportedAssignmentLocked()) return;
  // The first result, whether an assignment or an error, ends the wait for
  // the initial assignment (gRFC A119).
  CancelInitialAssignmentTimerLocked();
  if (!orphaned_) on_result_(Ref(), std::move(status));
}

}  // namespace grpc_core
