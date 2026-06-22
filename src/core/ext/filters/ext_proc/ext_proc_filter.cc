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

#include "src/core/ext/filters/ext_proc/ext_proc_filter.h"

#include <string>

#include "envoy/service/ext_proc/v3/external_processor.upb.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/debug/trace_flags.h"
#include "src/core/lib/promise/all_ok.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/string.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "absl/log/log.h"

namespace grpc_core {

namespace {

bool IsProcessingEnabled(const ExtProcFilter::ProcessingMode& processing_mode) {
  return processing_mode.send_request_headers ||
         processing_mode.send_response_headers ||
         processing_mode.send_response_trailers ||
         processing_mode.send_request_body ||
         processing_mode.send_response_body;
}

absl::Status ApplyHeaderMutations(
    const ExtProcResponse::HeaderMutation& mutations,
    const HeaderMutationRules* rules, grpc_metadata_batch& metadata) {
  for (const auto& remove : mutations.remove_headers) {
    auto status = ApplyXdsHeaderMutationsRemoval(remove, rules, metadata);
    if (!status.ok()) {
      return status;
    }
  }
  for (const auto& add : mutations.set_headers) {
    auto status = ApplyXdsHeaderMutationsAddition(add, rules, metadata);
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

}  // namespace

//
// ExtProcFilter::ProcessingMode
//

std::string ExtProcFilter::ProcessingMode::ToString() const {
  std::string result = "{";
  StrAppend(result, "send_request_headers=");
  StrAppend(result, send_request_headers ? "true" : "false");
  StrAppend(result, ", send_response_headers=");
  StrAppend(result, send_response_headers ? "true" : "false");
  StrAppend(result, ", send_response_trailers=");
  StrAppend(result, send_response_trailers ? "true" : "false");
  StrAppend(result, ", send_request_body=");
  StrAppend(result, send_request_body ? "true" : "false");
  StrAppend(result, ", send_response_body=");
  StrAppend(result, send_response_body ? "true" : "false");
  StrAppend(result, "}");
  return result;
}

//
// ExtProcFilter::Config
//

std::string ExtProcFilter::Config::ToString() const {
  std::string result = "{";
  bool is_first = true;
  if (grpc_service != nullptr) {
    StrAppend(result, "grpc_service=");
    StrAppend(result, grpc_service->ToString());
    is_first = false;
  }
  if (failure_mode_allow) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "failure_mode_allow=true");
    is_first = false;
  }
  if (!is_first) StrAppend(result, ", ");
  StrAppend(result, "processing_mode=");
  StrAppend(result, processing_mode.ToString());
  is_first = false;
  if (!request_attributes.empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "request_attributes=[");
    bool first_attr = true;
    for (const auto& attr : request_attributes) {
      if (!first_attr) StrAppend(result, ", ");
      StrAppend(result, attr);
      first_attr = false;
    }
    StrAppend(result, "]");
    is_first = false;
  }
  if (!response_attributes.empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "response_attributes=[");
    bool first_attr = true;
    for (const auto& attr : response_attributes) {
      if (!first_attr) StrAppend(result, ", ");
      StrAppend(result, attr);
      first_attr = false;
    }
    StrAppend(result, "]");
    is_first = false;
  }
  if (mutation_rules.has_value()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "mutation_rules=");
    StrAppend(result, mutation_rules->ToString());
    is_first = false;
  }
  if (!forwarding_allowed_headers.empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "forwarding_allowed_headers=[");
    bool first_matcher = true;
    for (const auto& matcher : forwarding_allowed_headers) {
      if (!first_matcher) StrAppend(result, ", ");
      StrAppend(result, matcher.ToString());
      first_matcher = false;
    }
    StrAppend(result, "]");
    is_first = false;
  }
  if (!forwarding_disallowed_headers.empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "forwarding_disallowed_headers=[");
    bool first_matcher = true;
    for (const auto& matcher : forwarding_disallowed_headers) {
      if (!first_matcher) StrAppend(result, ", ");
      StrAppend(result, matcher.ToString());
      first_matcher = false;
    }
    StrAppend(result, "]");
    is_first = false;
  }
  if (disable_immediate_response) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "disable_immediate_response=true");
    is_first = false;
  }
  if (observability_mode) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "observability_mode=true");
    is_first = false;
  }
  if (deferred_close_timeout != Duration::Zero()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "deferred_close_timeout=");
    StrAppend(result, deferred_close_timeout.ToString());
  }
  StrAppend(result, "}");
  return result;
}

//
// ExtProcFilter::ExtProcChannel
//

ExtProcFilter::ExtProcChannel::ExtProcChannel(
    std::shared_ptr<const XdsBootstrap::XdsServerTarget> server,
    RefCountedPtr<XdsTransportFactory> transport_factory)
    : server_(std::move(server)) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "creating channel " << this << " for server " << server_->server_uri();
  absl::Status status;
  transport_ = transport_factory->GetTransport(*server_, &status);
  GRPC_CHECK(transport_ != nullptr);
  if (!status.ok()) {
    LOG(ERROR) << "Error creating ext_proc channel to " << server_->server_uri()
               << ": " << status;
  }
}

ExtProcFilter::ExtProcChannel::~ExtProcChannel() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "destroying ext_proc channel " << this << " for server "
      << server_->server_uri();
}

class ExtProcFilter::ExtProcCall : public DualRefCounted<ExtProcCall> {
 public:
  explicit ExtProcCall(RefCountedPtr<ExtProcChannel> channel,
                       ProcessingMode processing_mode = {},
                       bool observability_mode = false,
                       bool failure_mode_allow = false,
                       Duration deferred_close_timeout = Duration::Seconds(5));
  ~ExtProcCall() override;

  ExtProcChannel* channel() const { return channel_.get(); }

  bool IsFirstMessageOnStream() {
    MutexLock lock(&mu_);
    return is_first_message_on_stream_;
  }

  void MarkFirstBodyMessageSent() {
    MutexLock lock(&mu_);
    is_first_body_message_sent_on_stream_ = true;
  }

  bool IsStreamClosed() {
    MutexLock lock(&mu_);
    return stream_closed_;
  }

  bool IsClientSendsDone() {
    MutexLock lock(&mu_);
    return client_sends_done_;
  }

  bool IsProcessorSentHalfClose() {
    MutexLock lock(&mu_);
    return processor_sent_half_close_;
  }

  void SetProcessorSentHalfClose() {
    MutexLock lock(&mu_);
    processor_sent_half_close_ = true;
  }

  void SetIsTrailersOnly() {
    MutexLock lock(&mu_);
    is_trailers_only_ = true;
  }

  bool IsTrailersOnly() {
    MutexLock lock(&mu_);
    return is_trailers_only_;
  }

  void MarkClientSendsDone();
  void SetStreamErrorStatus(absl::Status status);
  absl::Status GetStreamErrorStatus();
  void IncrementOutstandingClientToServerMessages();
  bool DecrementOutstandingClientToServerMessages();
  void IncrementOutstandingServerToClientMessages();
  bool DecrementOutstandingServerToClientMessages();
  void MarkServerToClientWritesDone();

  InterActivityLatch<absl::StatusOr<ExtProcResponse>> request_headers_latch_;
  InterActivityLatch<absl::StatusOr<ExtProcResponse>> response_headers_latch_;
  InterActivityLatch<absl::StatusOr<ExtProcResponse>> response_trailers_latch_;
  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 16> request_body_pipe_;
  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 16> response_body_pipe_;
  InterActivityLatch<void> stream_error_status_latch_;
  InterActivityLatch<void> all_server_to_client_responses_received_latch_;

  void OnRecvMessage(absl::string_view payload);
  void OnRequestSent(bool ok);
  void OnStatusReceived(absl::Status status);
  void OnConnectivityFailure(absl::Status status);

  auto SendMessageLocked(bool condition,
                         absl::AnyInvocable<std::string()> payload_generator);

 private:
  class StreamEventHandler final
      : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
   public:
    explicit StreamEventHandler(WeakRefCountedPtr<ExtProcCall> ext_proc_call)
        : ext_proc_call_(std::move(ext_proc_call)) {}

    void OnRequestSent(bool ok) override {
      if (auto call = ext_proc_call_->RefIfNonZero(); call != nullptr) {
        call->OnRequestSent(ok);
      }
    }
    void OnRecvMessage(absl::string_view payload) override {
      if (auto call = ext_proc_call_->RefIfNonZero(); call != nullptr) {
        call->OnRecvMessage(payload);
      }
    }
    void OnStatusReceived(absl::Status status) override {
      if (auto call = ext_proc_call_->RefIfNonZero(); call != nullptr) {
        call->OnStatusReceived(std::move(status));
      }
    }

   private:
    WeakRefCountedPtr<ExtProcCall> ext_proc_call_;
  };

  class ConnectivityWatcher final
      : public XdsTransportFactory::XdsTransport::ConnectivityFailureWatcher {
   public:
    explicit ConnectivityWatcher(WeakRefCountedPtr<ExtProcCall> ext_proc_call)
        : ext_proc_call_(std::move(ext_proc_call)) {}
    void OnConnectivityFailure(absl::Status status) override {
      if (auto call = ext_proc_call_->RefIfNonZero(); call != nullptr) {
        call->OnConnectivityFailure(status);
      }
    }

   private:
    WeakRefCountedPtr<ExtProcCall> ext_proc_call_;
  };

  void Orphaned() override;
  void ClearWriteQueueAndUnblockLocked(
      std::vector<std::shared_ptr<InterActivityLatch<void>>>*
          latches_to_unblock) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);

  RefCountedPtr<ExtProcChannel> channel_;

  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall>
      streaming_call_;
  RefCountedPtr<ConnectivityWatcher> connectivity_watcher_;

  bool observability_mode_;
  bool failure_mode_allow_;
  ProcessingMode processing_mode_;
  Duration deferred_close_timeout_;

  Mutex mu_;
  // True if this call has been orphaned (filter destroyed).
  bool orphaned_ ABSL_GUARDED_BY(&mu_) = false;
  // True if the external processor stream is closed (successfully or with
  // error).
  bool stream_closed_ ABSL_GUARDED_BY(&mu_) = false;
  // Stores the error status if the external processor stream fails.
  absl::Status stream_error_status_ ABSL_GUARDED_BY(&mu_);
  // True if no messages have been sent on the ext_proc stream yet. Used to
  // determine if attributes should be included in the request.
  bool is_first_message_on_stream_ ABSL_GUARDED_BY(&mu_) = true;
  // True if the first body message (request or response) has been sent to the
  // processor. Used for failure_mode_allow bypass logic.
  bool is_first_body_message_sent_on_stream_ ABSL_GUARDED_BY(&mu_) = false;
  // True if the client has half-closed (finished sending request messages).
  bool client_sends_done_ ABSL_GUARDED_BY(&mu_) = false;
  // True if the processor half-closed its sending stream (sent EOS).
  bool processor_sent_half_close_ ABSL_GUARDED_BY(&mu_) = false;
  // Number of client request body messages sent to the processor that are
  // awaiting a response. Used to detect unexpected/unsolicited responses.
  int outstanding_client_to_server_messages_ ABSL_GUARDED_BY(mu_) = 0;
  // Number of server response body messages sent to the processor that are
  // awaiting a response. Used to detect unexpected responses and trigger
  // clean close.
  int outstanding_server_to_client_messages_ ABSL_GUARDED_BY(mu_) = 0;
  bool server_to_client_writes_done_ ABSL_GUARDED_BY(&mu_) = false;
  bool is_trailers_only_ ABSL_GUARDED_BY(&mu_) = false;
  std::queue<std::shared_ptr<InterActivityLatch<void>>> write_queue_
      ABSL_GUARDED_BY(&mu_);
  bool write_active_ ABSL_GUARDED_BY(&mu_) = false;
  std::shared_ptr<InterActivityLatch<void>> write_completed_latch_
      ABSL_GUARDED_BY(&mu_);
};

//
// ExtProcFilter::ExtProcCall
//

ExtProcFilter::ExtProcCall::ExtProcCall(
    RefCountedPtr<ExtProcFilter::ExtProcChannel> ext_proc_channel,
    ProcessingMode processing_mode, bool observability_mode,
    bool failure_mode_allow, Duration deferred_close_timeout)
    : DualRefCounted<ExtProcFilter::ExtProcCall>(
          GRPC_TRACE_FLAG_ENABLED(ext_proc_filter) ? "ExtProcCall" : nullptr),
      channel_(std::move(ext_proc_channel)),
      observability_mode_(observability_mode),
      failure_mode_allow_(failure_mode_allow),
      processing_mode_(processing_mode),
      deferred_close_timeout_(deferred_close_timeout) {
  GRPC_CHECK_NE(channel(), nullptr);
  const char* method = "/envoy.service.ext_proc.v3.ExternalProcessor/Process";
  streaming_call_ = channel()->transport()->CreateStreamingCall(
      method, std::make_unique<StreamEventHandler>(WeakRef()));
  GRPC_CHECK(streaming_call_ != nullptr);
  connectivity_watcher_ = MakeRefCounted<ConnectivityWatcher>(WeakRef());
  channel()->transport()->StartConnectivityFailureWatch(connectivity_watcher_);
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ext_proc server " << channel()->server()->server_uri()
      << ": starting ext_proc call (ext_proc_call=" << this
      << ", streaming_call=" << streaming_call_.get() << ")";
  streaming_call_->StartRecvMessage();
}

ExtProcFilter::ExtProcCall::~ExtProcCall() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO) << "ExtProc: ExtProcCall destroyed";
  if (connectivity_watcher_ != nullptr) {
    channel()->transport()->StopConnectivityFailureWatch(connectivity_watcher_);
  }
  streaming_call_.reset();
}

auto ExtProcFilter::ExtProcCall::SendMessageLocked(
    bool condition, absl::AnyInvocable<std::string()> payload_generator) {
  return If(
      condition,
      [self = Ref(),
       payload_generator = std::move(payload_generator)]() mutable {
        std::shared_ptr<InterActivityLatch<void>> start_latch;
        bool my_turn = false;
        {
          MutexLock lock(&self->mu_);
          if (!self->write_active_) {
            self->write_active_ = true;
            my_turn = true;
          } else {
            start_latch = std::make_shared<InterActivityLatch<void>>();
            self->write_queue_.push(start_latch);
          }
        }
        return TrySeq(
            If(
                !my_turn,
                [start_latch]() {
                  return Map(start_latch->Wait(),
                             [start_latch](Empty) -> absl::Status {
                               return absl::OkStatus();
                             });
                },
                []() -> absl::Status { return absl::OkStatus(); }),
            [self, payload_generator = std::move(payload_generator)]() mutable {
              std::string payload = payload_generator();
              bool closed = false;
              std::shared_ptr<InterActivityLatch<void>> completed_latch;
              {
                MutexLock lock(&self->mu_);
                if (self->streaming_call_ == nullptr || self->stream_closed_) {
                  closed = true;
                } else {
                  completed_latch =
                      std::make_shared<InterActivityLatch<void>>();
                  self->write_completed_latch_ = completed_latch;
                  if (!self->orphaned_) {
                    self->streaming_call_->SendMessage(std::move(payload));
                  }
                }
              }
              return If(
                  closed,
                  [self]() -> absl::Status {
                    return self->GetStreamErrorStatus();
                  },
                  [completed_latch]() {
                    return Map(completed_latch->Wait(),
                               [completed_latch](Empty) -> absl::Status {
                                 return absl::OkStatus();
                               });
                  });
            },
            [self]() mutable -> absl::Status {
              std::shared_ptr<InterActivityLatch<void>> next_start_latch;
              {
                MutexLock lock(&self->mu_);
                self->write_completed_latch_ = nullptr;
                if (!self->write_queue_.empty()) {
                  next_start_latch = std::move(self->write_queue_.front());
                  self->write_queue_.pop();
                } else {
                  self->write_active_ = false;
                }
              }
              if (next_start_latch != nullptr) {
                next_start_latch->Set();
              }
              return absl::OkStatus();
            });
      },
      []() -> absl::Status { return absl::OkStatus(); });
}

void ExtProcFilter::ExtProcCall::OnRequestSent(bool ok) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " request sent ok=" << ok;
  std::shared_ptr<InterActivityLatch<void>> latch;
  {
    MutexLock lock(&mu_);
    if (orphaned_) return;
    if (ok && is_first_message_on_stream_) {
      is_first_message_on_stream_ = false;
    }
    latch = std::move(write_completed_latch_);
  }
  if (latch != nullptr) {
    latch->Set();
  }
}

void ExtProcFilter::ExtProcCall::MarkClientSendsDone() {
  MutexLock lock(&mu_);
  if (!client_sends_done_) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this << " client sends done (boolean set)";
    client_sends_done_ = true;
  }
}

void ExtProcFilter::ExtProcCall::SetStreamErrorStatus(absl::Status status) {
  MutexLock lock(&mu_);
  if (stream_error_status_.ok()) {
    stream_error_status_ = std::move(status);
    stream_error_status_latch_.Set();
  }
}

absl::Status ExtProcFilter::ExtProcCall::GetStreamErrorStatus() {
  MutexLock lock(&mu_);
  return stream_error_status_;
}

void ExtProcFilter::ExtProcCall::IncrementOutstandingClientToServerMessages() {
  MutexLock lock(&mu_);
  outstanding_client_to_server_messages_++;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this
      << " IncrementOutstandingClientToServerMessages: "
      << outstanding_client_to_server_messages_;
}

bool ExtProcFilter::ExtProcCall::DecrementOutstandingClientToServerMessages() {
  MutexLock lock(&mu_);
  if (outstanding_client_to_server_messages_ == 0) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this
        << " DecrementOutstandingClientToServerMessages: already 0, failing";
    return false;
  }
  outstanding_client_to_server_messages_--;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this
      << " DecrementOutstandingClientToServerMessages: "
      << outstanding_client_to_server_messages_;
  return true;
}

void ExtProcFilter::ExtProcCall::IncrementOutstandingServerToClientMessages() {
  MutexLock lock(&mu_);
  outstanding_server_to_client_messages_++;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this
      << " IncrementOutstandingServerToClientMessages: "
      << outstanding_server_to_client_messages_;
}

bool ExtProcFilter::ExtProcCall::DecrementOutstandingServerToClientMessages() {
  MutexLock lock(&mu_);
  if (outstanding_server_to_client_messages_ == 0) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this
        << " DecrementOutstandingServerToClientMessages: already 0, failing";
    return false;
  }
  outstanding_server_to_client_messages_--;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this
      << " DecrementOutstandingServerToClientMessages: "
      << outstanding_server_to_client_messages_;
  if (server_to_client_writes_done_ &&
      outstanding_server_to_client_messages_ == 0) {
    all_server_to_client_responses_received_latch_.Set();
  }
  return true;
}

void ExtProcFilter::ExtProcCall::MarkServerToClientWritesDone() {
  MutexLock lock(&mu_);
  server_to_client_writes_done_ = true;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " MarkServerToClientWritesDone: outstanding="
      << outstanding_server_to_client_messages_;
  if (outstanding_server_to_client_messages_ == 0) {
    all_server_to_client_responses_received_latch_.Set();
  }
}

void ExtProcFilter::ExtProcCall::OnStatusReceived(absl::Status status) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " status received: " << status;
  bool is_first_body_message_sent = false;
  bool is_first_message_on_stream = false;
  {
    MutexLock lock(&mu_);
    stream_closed_ = true;
    is_first_body_message_sent = is_first_body_message_sent_on_stream_;
    is_first_message_on_stream = is_first_message_on_stream_;
  }
  if (!status.ok() && !is_first_message_on_stream &&
      (!failure_mode_allow_ || is_first_body_message_sent)) {
    SetStreamErrorStatus(status);
    if (processing_mode_.send_request_headers &&
        !request_headers_latch_.IsSet()) {
      request_headers_latch_.Set(status);
    }
    if (processing_mode_.send_response_headers &&
        !response_headers_latch_.IsSet()) {
      response_headers_latch_.Set(status);
    }
    if (processing_mode_.send_response_trailers &&
        !response_trailers_latch_.IsSet()) {
      response_trailers_latch_.Set(status);
    }
    if (processing_mode_.send_request_body &&
        !request_body_pipe_.sender.IsClosed()) {
      request_body_pipe_.sender.MarkClosed();
    }
    if (processing_mode_.send_response_body &&
        !response_body_pipe_.sender.IsClosed()) {
      response_body_pipe_.sender.MarkClosed();
    }
  } else {
    if (processing_mode_.send_request_headers &&
        !request_headers_latch_.IsSet()) {
      request_headers_latch_.Set(ExtProcResponse{});
    }
    if (processing_mode_.send_response_headers &&
        !response_headers_latch_.IsSet()) {
      response_headers_latch_.Set(ExtProcResponse{});
    }
    if (processing_mode_.send_response_trailers &&
        !response_trailers_latch_.IsSet()) {
      response_trailers_latch_.Set(ExtProcResponse{});
    }
    if (processing_mode_.send_request_body &&
        !request_body_pipe_.sender.IsClosed()) {
      request_body_pipe_.sender.MarkClosed();
    }
    if (processing_mode_.send_response_body &&
        !response_body_pipe_.sender.IsClosed()) {
      response_body_pipe_.sender.MarkClosed();
    }
  }
  std::vector<std::shared_ptr<InterActivityLatch<void>>> latches_to_unblock;
  RefCountedPtr<ConnectivityWatcher> watcher_to_stop;
  {
    MutexLock lock(&mu_);
    ClearWriteQueueAndUnblockLocked(&latches_to_unblock);
    if (write_completed_latch_ != nullptr) {
      latches_to_unblock.push_back(std::move(write_completed_latch_));
    }
    streaming_call_.reset();
    watcher_to_stop = std::move(connectivity_watcher_);
  }
  if (watcher_to_stop != nullptr) {
    channel()->transport()->StopConnectivityFailureWatch(watcher_to_stop);
  }
  for (auto& latch : latches_to_unblock) {
    latch->Set();
  }
}

void ExtProcFilter::ExtProcCall::OnConnectivityFailure(absl::Status status) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " connectivity failure: " << status;
  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall> call_to_reset;
  {
    MutexLock lock(&mu_);
    if (stream_closed_) return;
    call_to_reset = std::move(streaming_call_);
  }
  call_to_reset.reset();
}

void ExtProcFilter::ExtProcCall::OnRecvMessage(absl::string_view payload) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " message received, size=" << payload.size();
  upb::Arena arena;
  auto* response = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      payload.data(), payload.size(), arena.ptr());
  if (response == nullptr) {
    LOG(ERROR) << "Failed to parse ProcessingResponse";
    auto error = absl::InternalError("Failed to parse ProcessingResponse");
    if (!failure_mode_allow_) {
      if (processing_mode_.send_request_headers &&
          !request_headers_latch_.IsSet()) {
        request_headers_latch_.Set(error);
      }
      if (processing_mode_.send_response_headers &&
          !response_headers_latch_.IsSet()) {
        response_headers_latch_.Set(error);
      }
      if (processing_mode_.send_response_trailers &&
          !response_trailers_latch_.IsSet()) {
        response_trailers_latch_.Set(error);
      }
    } else {
      if (processing_mode_.send_request_headers &&
          !request_headers_latch_.IsSet()) {
        request_headers_latch_.Set(ExtProcResponse{});
      }
      if (processing_mode_.send_response_headers &&
          !response_headers_latch_.IsSet()) {
        response_headers_latch_.Set(ExtProcResponse{});
      }
      if (processing_mode_.send_response_trailers &&
          !response_trailers_latch_.IsSet()) {
        response_trailers_latch_.Set(ExtProcResponse{});
      }
    }
    if (processing_mode_.send_request_body &&
        !request_body_pipe_.sender.IsClosed()) {
      request_body_pipe_.sender.MarkClosed();
    }
    if (processing_mode_.send_response_body &&
        !response_body_pipe_.sender.IsClosed()) {
      response_body_pipe_.sender.MarkClosed();
    }
  } else {
    auto parsed_response_or =
        ParseExtProcResponse(response, observability_mode_);
    if (!parsed_response_or.ok()) {
      LOG(ERROR) << "Failed to validate ProcessingResponse: "
                 << parsed_response_or.status();
      if (!failure_mode_allow_) {
        if (processing_mode_.send_request_headers &&
            !request_headers_latch_.IsSet()) {
          request_headers_latch_.Set(parsed_response_or.status());
        }
        if (processing_mode_.send_response_headers &&
            !response_headers_latch_.IsSet()) {
          response_headers_latch_.Set(parsed_response_or.status());
        }
        if (processing_mode_.send_response_trailers &&
            !response_trailers_latch_.IsSet()) {
          response_trailers_latch_.Set(parsed_response_or.status());
        }
      } else {
        if (processing_mode_.send_request_headers &&
            !request_headers_latch_.IsSet()) {
          request_headers_latch_.Set(ExtProcResponse{});
        }
        if (processing_mode_.send_response_headers &&
            !response_headers_latch_.IsSet()) {
          response_headers_latch_.Set(ExtProcResponse{});
        }
        if (processing_mode_.send_response_trailers &&
            !response_trailers_latch_.IsSet()) {
          response_trailers_latch_.Set(ExtProcResponse{});
        }
      }
      if (processing_mode_.send_request_body &&
          !request_body_pipe_.sender.IsClosed()) {
        request_body_pipe_.sender.MarkClosed();
      }
      if (processing_mode_.send_response_body &&
          !response_body_pipe_.sender.IsClosed()) {
        response_body_pipe_.sender.MarkClosed();
      }
    } else {
      auto parsed_response = std::move(*parsed_response_or);
      if (parsed_response.immediate_response.has_value()) {
        if (processing_mode_.send_request_headers &&
            !request_headers_latch_.IsSet()) {
          request_headers_latch_.Set(std::move(parsed_response));
        } else if (processing_mode_.send_response_headers &&
                   !response_headers_latch_.IsSet()) {
          response_headers_latch_.Set(std::move(parsed_response));
        } else if (processing_mode_.send_response_trailers &&
                   !response_trailers_latch_.IsSet()) {
          response_trailers_latch_.Set(std::move(parsed_response));
        }
      } else if (parsed_response.request_headers.has_value()) {
        if (!processing_mode_.send_request_headers) {
          LOG(ERROR) << "Received request headers response but request headers "
                        "are disabled";
          SetStreamErrorStatus(
              absl::InternalError("Received request headers response but "
                                  "request headers are disabled"));
          return;
        }
        if (processing_mode_.send_request_headers &&
            !request_headers_latch_.IsSet()) {
          request_headers_latch_.Set(std::move(parsed_response));
        }
      } else if (parsed_response.response_headers.has_value()) {
        if (!processing_mode_.send_response_headers) {
          LOG(ERROR) << "Received response headers response but response "
                        "headers are disabled";
          SetStreamErrorStatus(
              absl::InternalError("Received response headers response but "
                                  "response headers are disabled"));
          return;
        }
        if (processing_mode_.send_response_headers &&
            !response_headers_latch_.IsSet()) {
          response_headers_latch_.Set(std::move(parsed_response));
        }
      } else if (parsed_response.response_trailers.has_value()) {
        if (!processing_mode_.send_response_trailers) {
          LOG(ERROR) << "Received response trailers response but response "
                        "trailers are disabled";
          SetStreamErrorStatus(
              absl::InternalError("Received response trailers response but "
                                  "response trailers are disabled"));
          if (!response_trailers_latch_.IsSet()) {
            response_trailers_latch_.Set(
                absl::InternalError("Received response trailers response but "
                                    "response trailers are disabled"));
          }
          return;
        }
        if (IsTrailersOnly()) {
          LOG(ERROR)
              << "Received response trailers response in a Trailers-Only call";
          SetStreamErrorStatus(absl::InternalError(
              "Received response trailers response in a Trailers-Only call"));
          if (!response_trailers_latch_.IsSet()) {
            response_trailers_latch_.Set(absl::InternalError(
                "Received response trailers response in a Trailers-Only call"));
          }
          return;
        }
        if (processing_mode_.send_response_headers &&
            !response_headers_latch_.IsSet()) {
          LOG(ERROR) << "Received response trailers response before response "
                        "headers response";
          SetStreamErrorStatus(
              absl::InternalError("Received response trailers response before "
                                  "response headers response"));
          if (!response_trailers_latch_.IsSet()) {
            response_trailers_latch_.Set(
                absl::InternalError("Received response trailers response "
                                    "before response headers response"));
          }
          return;
        }
        if (processing_mode_.send_response_trailers &&
            !response_trailers_latch_.IsSet()) {
          response_trailers_latch_.Set(std::move(parsed_response));
        }
      } else if (parsed_response.request_body.has_value()) {
        if (!processing_mode_.send_request_body) {
          LOG(ERROR)
              << "Received request body response but request body is disabled";
          SetStreamErrorStatus(absl::InternalError(
              "Received request body response but request body is disabled"));
          if (!request_body_pipe_.sender.IsClosed()) {
            request_body_pipe_.sender.MarkClosed();
          }
          return;
        }
        if (processing_mode_.send_request_headers &&
            !request_headers_latch_.IsSet()) {
          LOG(ERROR) << "Received request body response before request headers "
                        "response";
          SetStreamErrorStatus(
              absl::InternalError("Received request body response before "
                                  "request headers response"));
          if (!request_body_pipe_.sender.IsClosed()) {
            request_body_pipe_.sender.MarkClosed();
          }
          return;
        }
        auto& request_body = *parsed_response.request_body;
        bool eos = false;
        if (request_body.ok()) {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Parsed request body response, eos: "
              << request_body->end_of_stream << ", eos_without_msg: "
              << request_body->end_of_stream_without_message;
          if (request_body->end_of_stream_without_message) {
            // TODO(rishesh): Once PH2 work is done, we should make this
            // pass (half-close without sending message). Currently we fail the
            // RPC to avoid hangs on legacy transport adapters.
            SetStreamErrorStatus(absl::InternalError(
                "end_of_stream_without_message not supported"));
            if (!request_body_pipe_.sender.IsClosed()) {
              request_body_pipe_.sender.MarkClosed();
            }
            return;
          }
          eos = request_body->end_of_stream;
        }
        request_body_pipe_.sender.Push(std::move(parsed_response))();
        if (eos) {
          SetProcessorSentHalfClose();
          if (!request_body_pipe_.sender.IsClosed()) {
            request_body_pipe_.sender.MarkClosed();
          }
        }
      } else if (parsed_response.response_body.has_value()) {
        if (!processing_mode_.send_response_body) {
          LOG(ERROR) << "Received response body response but response body is "
                        "disabled";
          SetStreamErrorStatus(absl::InternalError(
              "Received response body response but response body is disabled"));
          if (!response_body_pipe_.sender.IsClosed()) {
            response_body_pipe_.sender.MarkClosed();
          }
          return;
        }
        if (IsTrailersOnly()) {
          LOG(ERROR)
              << "Received response body response in a Trailers-Only call";
          SetStreamErrorStatus(absl::InternalError(
              "Received response body response in a Trailers-Only call"));
          if (!response_body_pipe_.sender.IsClosed()) {
            response_body_pipe_.sender.MarkClosed();
          }
          return;
        }
        if (processing_mode_.send_response_headers &&
            !response_headers_latch_.IsSet()) {
          LOG(ERROR) << "Received response body response before response "
                        "headers response";
          SetStreamErrorStatus(
              absl::InternalError("Received response body response before "
                                  "response headers response"));
          if (!response_body_pipe_.sender.IsClosed()) {
            response_body_pipe_.sender.MarkClosed();
          }
          return;
        }
        auto& response_body = *parsed_response.response_body;
        bool eos = false;
        if (response_body.ok()) {
          eos = response_body->end_of_stream ||
                response_body->end_of_stream_without_message;
        }
        response_body_pipe_.sender.Push(std::move(parsed_response))();
        if (eos) {
          SetProcessorSentHalfClose();
          if (!response_body_pipe_.sender.IsClosed()) {
            response_body_pipe_.sender.MarkClosed();
          }
        }
      }
    }
  }
  MutexLock lock(&mu_);
  if (!orphaned_ && streaming_call_ != nullptr) {
    streaming_call_->StartRecvMessage();
  }
}

void ExtProcFilter::ExtProcCall::ClearWriteQueueAndUnblockLocked(
    std::vector<std::shared_ptr<InterActivityLatch<void>>>*
        latches_to_unblock) {
  while (!write_queue_.empty()) {
    latches_to_unblock->push_back(std::move(write_queue_.front()));
    write_queue_.pop();
  }
  if (write_completed_latch_ != nullptr) {
    latches_to_unblock->push_back(std::move(write_completed_latch_));
  }
  write_active_ = false;
}

void ExtProcFilter::ExtProcCall::Orphaned() {
  std::vector<std::shared_ptr<InterActivityLatch<void>>> latches_to_unblock;
  RefCountedPtr<ConnectivityWatcher> watcher_to_stop;
  {
    MutexLock lock(&mu_);
    orphaned_ = true;
    ClearWriteQueueAndUnblockLocked(&latches_to_unblock);
    streaming_call_.reset();
    watcher_to_stop = std::move(connectivity_watcher_);
  }
  if (watcher_to_stop != nullptr) {
    channel()->transport()->StopConnectivityFailureWatch(watcher_to_stop);
  }
  for (auto& latch : latches_to_unblock) {
    latch->Set();
  }
}

//
// ExtProcFilter
//

const grpc_channel_filter ExtProcFilter::kFilterVtable = MakePromiseBasedFilter<
    ExtProcFilter, FilterEndpoint::kClient,
    kFilterExaminesServerInitialMetadata | kFilterExaminesOutboundMessages |
        kFilterExaminesInboundMessages | kFilterExaminesCallContext>();

absl::StatusOr<RefCountedPtr<ExtProcFilter>> ExtProcFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args filter_args) {
  if (filter_args.config()->type() != Config::Type()) {
    return absl::InternalError("ext_proc filter config has wrong type");
  }
  auto config = filter_args.config().TakeAsSubclass<const Config>();
  return MakeRefCounted<ExtProcFilter>(args, std::move(config),
                                       std::move(filter_args));
}

ExtProcFilter::ExtProcFilter(const ChannelArgs& args,
                             RefCountedPtr<const Config> config,
                             ChannelFilter::Args filter_args)
    : config_(std::move(config)),
      channel_(config_->channel),
      default_authority_(Slice::FromCopiedString(
          args.GetString(GRPC_ARG_DEFAULT_AUTHORITY)
              .value_or(
                  CoreConfiguration::Get()
                      .resolver_registry()
                      .GetDefaultAuthority(
                          args.GetString(GRPC_ARG_SERVER_URI).value_or(""))))) {
}

auto ExtProcFilter::SendServerInitialMetadataRequest(
    RefCountedPtr<ExtProcCall> ext_proc_call,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  const bool is_trailers_only =
      (*metadata)->get(GrpcTrailersOnly()).value_or(false);
  return ext_proc_call->SendMessageLocked(
      /*condition=*/true, [self = RefAsSubclass<ExtProcFilter>(), ext_proc_call,
                           metadata, is_trailers_only]() {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Sending server initial metadata, is_trailers_only="
            << is_trailers_only;
        upb::Arena serialization_arena;
        return CreateExtProcRequest(
            serialization_arena.ptr(), ExtProcRequestType::kServerHeaders,
            metadata->get(), self->config()->forwarding_allowed_headers,
            self->config()->forwarding_disallowed_headers,
            /*attributes=*/nullptr,
            /*observability_mode=*/false,
            ext_proc_call->IsFirstMessageOnStream(),
            self->config()->processing_mode.send_request_body,
            self->config()->processing_mode.send_response_body,
            /*end_of_stream=*/is_trailers_only);
      });
}

auto ExtProcFilter::ReadServerInitialMetadataResponse(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  return Seq(
      TrySeq(ext_proc_call->response_headers_latch_.Wait(),
             [self = RefAsSubclass<ExtProcFilter>(), metadata,
              ext_proc_call](ExtProcResponse response) mutable -> absl::Status {
               GRPC_TRACE_LOG(ext_proc_filter, INFO)
                   << "ExtProc: ServerInitialMetadata response received. "
                      "has_headers: "
                   << response.response_headers.has_value();
               if (response.response_headers.has_value()) {
                 const auto& response_headers = *response.response_headers;
                 if (!response_headers.ok()) {
                   return response_headers.status();
                 }
                 const auto* rules =
                     self->config()->mutation_rules.has_value()
                         ? &self->config()->mutation_rules.value()
                         : nullptr;
                 auto status =
                     ApplyHeaderMutations(*response_headers, rules, **metadata);
                 if (!status.ok()) {
                   return status;
                 }
               }
               return absl::OkStatus();
             }),
      [handler, metadata](absl::Status result) mutable {
        handler.SpawnPushServerInitialMetadata(std::move(*metadata));
        return result;
      });
}

auto ExtProcFilter::ServerInitialMetadataNormalMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataNormalMode pulled. metadata: "
      << (*metadata)->DebugString();
  return TrySeq(SendServerInitialMetadataRequest(ext_proc_call, metadata),
                [self = RefAsSubclass<ExtProcFilter>(), handler, initiator,
                 ext_proc_call, metadata]() {
                  return self->ReadServerInitialMetadataResponse(
                      handler, initiator, ext_proc_call, metadata);
                });
}

auto ExtProcFilter::ServerInitialMetadataMaybeObservabilityMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_ext_proc_stream,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataMaybeObservabilityMode pulled. "
         "send_to_ext_proc_stream: "
      << send_to_ext_proc_stream
      << ", metadata: " << (*metadata)->DebugString();

  const bool is_trailers_only =
      (*metadata)->get(GrpcTrailersOnly()).value_or(false);

  std::shared_ptr<upb::Arena> serialization_arena;
  envoy_config_core_v3_HeaderMap* upb_headers = nullptr;

  if (send_to_ext_proc_stream) {
    serialization_arena = std::make_shared<upb::Arena>();
    upb_headers =
        envoy_config_core_v3_HeaderMap_new(serialization_arena->ptr());
    PopulateMetadataBatchToHeaderMap(**metadata,
                                     config_->forwarding_allowed_headers,
                                     config_->forwarding_disallowed_headers,
                                     serialization_arena->ptr(), upb_headers);
  } else {
    // If we are not sending headers, we must unblock the concurrent message
    // loop which might be waiting for this latch.
    if (!ext_proc_call->response_headers_latch_.IsSet()) {
      ext_proc_call->response_headers_latch_.Set(ExtProcResponse{});
    }
  }

  // Push metadata to client immediately
  handler.SpawnPushServerInitialMetadata(std::move(*metadata));

  return ext_proc_call->SendMessageLocked(
      send_to_ext_proc_stream,
      [self = RefAsSubclass<ExtProcFilter>(), ext_proc_call,
       serialization_arena, upb_headers, is_trailers_only]() mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Sending server initial metadata (observability mode)";
        upb::Arena arena;
        return CreateExtProcRequest(
            arena.ptr(), ExtProcRequestType::kServerHeaders, upb_headers,
            self->config()->forwarding_allowed_headers,
            self->config()->forwarding_disallowed_headers,
            /*attributes=*/nullptr,
            /*observability_mode=*/true,
            ext_proc_call->IsFirstMessageOnStream(),
            self->config()->processing_mode.send_request_body,
            self->config()->processing_mode.send_response_body,
            /*end_of_stream=*/is_trailers_only);
      });
}

auto ExtProcFilter::SendServerMessageRequest(const MessageHandle& message,
                                             ExtProcCall* ext_proc_call,
                                             bool send_to_ext_proc_stream) {
  Message* msg_ptr = message.get();
  return ext_proc_call->SendMessageLocked(
      send_to_ext_proc_stream, [this, msg_ptr, ext_proc_call]() {
        ext_proc_call->MarkFirstBodyMessageSent();
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerToClientMessages body message intercepted:\n"
            << (msg_ptr != nullptr ? msg_ptr->DebugString() : "nullptr");
        upb::Arena arena;
        std::string message_bytes;
        if (msg_ptr != nullptr) {
          message_bytes = msg_ptr->payload()->JoinIntoString();
        }
        return CreateExtProcRequest(
            arena.ptr(), ExtProcRequestType::kServerMessage,
            upb_StringView_FromDataAndSize(message_bytes.data(),
                                           message_bytes.size()),
            {}, {},   // no headers
            nullptr,  // no attributes
            config_->observability_mode,
            ext_proc_call->IsFirstMessageOnStream(),
            config_->processing_mode.send_request_body,
            config_->processing_mode.send_response_body,
            /*end_of_stream=*/false,
            /*end_of_stream_without_message=*/false);
      });
}

auto ExtProcFilter::SendServerMessageRequest(std::string message_bytes,
                                             ExtProcCall* ext_proc_call,
                                             bool send_to_ext_proc_stream) {
  return ext_proc_call->SendMessageLocked(
      send_to_ext_proc_stream,
      [this, ext_proc_call, message_bytes = std::move(message_bytes)]() {
        ext_proc_call->MarkFirstBodyMessageSent();
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerToClientMessages body message intercepted "
               "(observability mode)";
        upb::Arena arena;
        return CreateExtProcRequest(
            arena.ptr(), ExtProcRequestType::kServerMessage,
            upb_StringView_FromDataAndSize(message_bytes.data(),
                                           message_bytes.size()),
            {}, {},   // no headers
            nullptr,  // no attributes
            config_->observability_mode,
            ext_proc_call->IsFirstMessageOnStream(),
            config_->processing_mode.send_request_body,
            config_->processing_mode.send_response_body,
            /*end_of_stream=*/false,
            /*end_of_stream_without_message=*/false);
      });
}

auto ExtProcFilter::ServerToClientMessagesMaybeObservabilityMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_ext_proc_stream) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerToClientMessagesMaybeObservabilityMode started, "
         "send_to_ext_proc_stream="
      << send_to_ext_proc_stream
      << ", stream_closed=" << ext_proc_call->IsStreamClosed();
  return ForEach(
      MessagesFrom(initiator),
      [self = RefAsSubclass<ExtProcFilter>(), handler, ext_proc_call,
       send_to_ext_proc_stream](MessageHandle message) mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerToClientMessagesMaybeObservabilityMode "
               "processing message, send_to_ext_proc_stream="
            << send_to_ext_proc_stream
            << ", stream_closed=" << ext_proc_call->IsStreamClosed();
        std::string message_bytes;
        const bool send =
            send_to_ext_proc_stream && !ext_proc_call->IsStreamClosed();
        if (send && message != nullptr) {
          message_bytes = message->payload()->JoinIntoString();
        }
        handler.SpawnPushMessage(std::move(message));
        return self->SendServerMessageRequest(std::move(message_bytes),
                                              ext_proc_call.get(), send);
      });
}

auto ExtProcFilter::SendServerToClientMessagesRequest(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: SendServerToClientMessagesRequest started";
  return Seq(
      ForEach(MessagesFrom(initiator),
              [self = RefAsSubclass<ExtProcFilter>(), handler, initiator,
               ext_proc_call](MessageHandle message) mutable {
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << "ExtProc: ServerToClient S2C Write Loop pulled message, "
                       "waiting for headers";
                auto shared_message =
                    std::make_shared<MessageHandle>(std::move(message));
                const bool send_response_headers =
                    self->config()->processing_mode.send_response_headers;
                return TrySeq(
                    If(
                        send_response_headers,
                        [ext_proc_call]() {
                          return Map(
                              ext_proc_call->response_headers_latch_.Wait(),
                              [](auto&&...) -> absl::Status {
                                return absl::OkStatus();
                              });
                        },
                        []() -> absl::Status { return absl::OkStatus(); }),
                    [self, handler, ext_proc_call, shared_message]() mutable {
                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                          << "ExtProc: ServerToClient S2C Write Loop headers "
                             "finished, processing message";
                      const bool send_to_ext_proc_stream =
                          self->config()->processing_mode.send_response_body &&
                          !ext_proc_call->IsStreamClosed();
                      return If(
                          send_to_ext_proc_stream,
                          [self, ext_proc_call, shared_message]() mutable {
                            ext_proc_call
                                ->IncrementOutstandingServerToClientMessages();
                            MessageHandle message = std::move(*shared_message);
                            return Map(self->SendServerMessageRequest(
                                           message, ext_proc_call.get(),
                                           /*send_to_ext_proc_stream=*/true),
                                       [message = std::move(message)](
                                           absl::Status status) mutable {
                                         return status;
                                       });
                          },
                          [handler, shared_message]() mutable {
                            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                                << "ExtProc: ServerToClient S2C Write Loop "
                                   "bypassing ext_proc";
                            MessageHandle message = std::move(*shared_message);
                            handler.SpawnPushMessage(std::move(message));
                            return absl::OkStatus();
                          });
                    });
              }),
      [ext_proc_call]() {
        ext_proc_call->MarkServerToClientWritesDone();
        return absl::OkStatus();
      });
}

auto ExtProcFilter::ReadServerToClientMessagesResponse(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ReadServerToClientMessagesResponse started";

  // 1. Read Loop: Read from response_body_pipe_, construct message, push to
  // handler.
  auto read_loop = ForEach(
      std::move(ext_proc_call->response_body_pipe_.receiver),
      [self = RefAsSubclass<ExtProcFilter>(), handler, initiator,
       ext_proc_call](absl::StatusOr<ExtProcResponse> response) mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerToClient S2C Read Loop got response";
        if (!response.ok()) {
          return response.status();
        }
        auto& ext_proc_response = *response;
        if (!ext_proc_response.response_body.has_value()) {
          return absl::InternalError("Missing response_body in response");
        }
        const auto& response_body = *ext_proc_response.response_body;
        if (!response_body.ok()) {
          auto error_md =
              CancelledServerMetadataFromStatus(response_body.status());
          handler.SpawnPushServerTrailingMetadata(std::move(error_md));
          initiator.SpawnCancel();
          return response_body.status();
        }
        const auto& body_mutation = *response_body;
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerToClient S2C Read Loop playing body mutation: "
            << body_mutation.body.size() << "b";
        auto slice = Slice::FromCopiedString(body_mutation.body);
        auto new_msg =
            handler.arena()->MakePooled<Message>(SliceBuffer(std::move(slice)),
                                                 /*flags=*/0);
        handler.SpawnPushMessage(std::move(new_msg));
        if (!ext_proc_call->DecrementOutstandingServerToClientMessages()) {
          return absl::InternalError(
              "Received unexpected response body response from "
              "external processor");
        }
        return absl::OkStatus();
      });

  // 2. Coordinator: Wait for all messages to be processed (writes done AND
  // outstanding is 0).
  auto close_pipe_promise =
      Map(ext_proc_call->all_server_to_client_responses_received_latch_.Wait(),
          [ext_proc_call](Empty) {
            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                << "ExtProc: ServerToClient S2C Coordinator: all responses "
                   "received, closing pipe";
            if (!ext_proc_call->response_body_pipe_.sender.IsClosed()) {
              ext_proc_call->response_body_pipe_.sender.MarkClosed();
            }
            return absl::OkStatus();
          });

  // Combine them concurrently
  return Map(TryJoin<absl::StatusOr>(std::move(close_pipe_promise),
                                     std::move(read_loop)),
             [](auto result) -> absl::Status { return result.status(); });
}

auto ExtProcFilter::ServerToClientMessagesNormalMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerToClientMessagesNormalMode started";
  return Map(TryJoin<absl::StatusOr>(SendServerToClientMessagesRequest(
                                         handler, initiator, ext_proc_call),
                                     ReadServerToClientMessagesResponse(
                                         handler, initiator, ext_proc_call)),
             [](auto result) -> absl::Status { return result.status(); });
}

auto ExtProcFilter::SendServerTrailingMetadataRequest(
    RefCountedPtr<ExtProcCall> ext_proc_call,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  return ext_proc_call->SendMessageLocked(
      !ext_proc_call->IsStreamClosed(),
      [self = RefAsSubclass<ExtProcFilter>(), ext_proc_call, metadata]() {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Sending server trailing metadata";
        upb::Arena serialization_arena;
        return CreateExtProcRequest(
            serialization_arena.ptr(), ExtProcRequestType::kServerTrailers,
            metadata->get(), self->config()->forwarding_allowed_headers,
            self->config()->forwarding_disallowed_headers,
            /*attributes=*/nullptr,
            /*observability_mode=*/false,
            ext_proc_call->IsFirstMessageOnStream(),
            self->config()->processing_mode.send_request_body,
            self->config()->processing_mode.send_response_body);
      });
}

auto ExtProcFilter::ReadServerTrailingMetadataResponse(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  return Seq(
      TrySeq(ext_proc_call->response_trailers_latch_.Wait(),
             [self = RefAsSubclass<ExtProcFilter>(), metadata,
              ext_proc_call](ExtProcResponse response) mutable -> absl::Status {
               GRPC_TRACE_LOG(ext_proc_filter, INFO)
                   << "ExtProc: ServerTrailingMetadata response received. "
                      "OK: true, has_trailers: "
                   << response.response_trailers.has_value();
               // Rule 3: Processing and non-observability mode
               if (self->config()->processing_mode.send_response_body &&
                   !self->config()->observability_mode &&
                   !ext_proc_call->response_body_pipe_.sender.IsClosed()) {
                 ext_proc_call->response_body_pipe_.sender.MarkClosed();
               }
               if (response.response_trailers.has_value()) {
                 const auto& response_trailers = *response.response_trailers;
                 if (!response_trailers.ok()) {
                   return response_trailers.status();
                 }
                 const auto* rules =
                     self->config()->mutation_rules.has_value()
                         ? &self->config()->mutation_rules.value()
                         : nullptr;
                 auto status = ApplyHeaderMutations(*response_trailers, rules,
                                                    **metadata);
                 GRPC_TRACE_LOG(ext_proc_filter, INFO)
                     << "ExtProc: ServerTrailingMetadata mutations applied, "
                        "status: "
                     << status.ToString()
                     << ", mutated metadata: " << (*metadata)->DebugString();
                 if (!status.ok()) {
                   return status;
                 }
               }
               return absl::OkStatus();
             }),
      [self = RefAsSubclass<ExtProcFilter>(), handler, metadata,
       ext_proc_call](absl::Status result) mutable {
        if (!result.ok()) {
          *metadata = CancelledServerMetadataFromStatus(result);
        }
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerTrailingMetadata pushing metadata immediately";
        handler.SpawnPushServerTrailingMetadata(std::move(*metadata));
        return absl::OkStatus();
      });
}

auto ExtProcFilter::ServerTrailingMetadataNormalMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataNormalMode pulled. Status OK: "
      << IsStatusOk(*metadata) << ", metadata: " << (*metadata)->DebugString();
  return TrySeq(SendServerTrailingMetadataRequest(ext_proc_call, metadata),
                [self = RefAsSubclass<ExtProcFilter>(), handler, initiator,
                 ext_proc_call, metadata]() {
                  return self->ReadServerTrailingMetadataResponse(
                      handler, initiator, ext_proc_call, metadata);
                });
}

auto ExtProcFilter::ServerTrailingMetadataMaybeObservabilityMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_ext_proc_stream,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataMaybeObservabilityMode pulled. "
         "Status OK: "
      << IsStatusOk(*metadata)
      << ", send_to_ext_proc_stream: " << send_to_ext_proc_stream
      << ", metadata: " << (*metadata)->DebugString();
  return Seq(
      ext_proc_call->SendMessageLocked(
          send_to_ext_proc_stream,
          [self = RefAsSubclass<ExtProcFilter>(), ext_proc_call, metadata]() {
            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                << "ExtProc: Sending server trailing metadata";
            upb::Arena serialization_arena;
            return CreateExtProcRequest(
                serialization_arena.ptr(), ExtProcRequestType::kServerTrailers,
                metadata->get(), self->config()->forwarding_allowed_headers,
                self->config()->forwarding_disallowed_headers,
                /*attributes=*/nullptr,
                /*observability_mode=*/true,
                ext_proc_call->IsFirstMessageOnStream(),
                self->config()->processing_mode.send_request_body,
                self->config()->processing_mode.send_response_body);
          }),
      [self = RefAsSubclass<ExtProcFilter>(), handler, metadata,
       ext_proc_call](absl::Status result) mutable {
        // Rule 1: Non Processing mode (send_to_ext_proc_stream is false)
        // Rule 2: Processing and observability mode (send_to_ext_proc_stream is
        // true) Both guarded by the global condition:
        if (self->config()->processing_mode.send_response_body &&
            !self->config()->observability_mode &&
            !ext_proc_call->response_body_pipe_.sender.IsClosed()) {
          ext_proc_call->response_body_pipe_.sender.MarkClosed();
        }
        if (!result.ok()) {
          *metadata = CancelledServerMetadataFromStatus(result);
        }
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerTrailingMetadata pushing metadata immediately";
        handler.SpawnPushServerTrailingMetadata(std::move(*metadata));
        return absl::OkStatus();
      });
}

auto ExtProcFilter::ServerTrailingMetadata(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  return Seq(initiator.PullServerTrailingMetadata(), [self = RefAsSubclass<
                                                          ExtProcFilter>(),
                                                      handler, initiator,
                                                      ext_proc_call](
                                                         ServerMetadataHandle
                                                             md) mutable {
    auto shared_metadata =
        std::make_shared<ServerMetadataHandle>(std::move(md));
    const bool is_trailers_only =
        (*shared_metadata)->get(GrpcTrailersOnly()).value_or(false);
    if (is_trailers_only) {
      ext_proc_call->SetIsTrailersOnly();
    }

    return If(
        is_trailers_only,
        [self, handler, initiator, ext_proc_call, shared_metadata]() mutable {
          const bool send_headers =
              self->config()->processing_mode.send_response_headers &&
              !ext_proc_call->IsStreamClosed();
          return If(
              self->config()->observability_mode,
              [self, handler, ext_proc_call, shared_metadata,
               send_headers]() mutable {
                std::shared_ptr<upb::Arena> serialization_arena;
                envoy_config_core_v3_HeaderMap* upb_headers = nullptr;
                if (send_headers) {
                  serialization_arena = std::make_shared<upb::Arena>();
                  upb_headers = envoy_config_core_v3_HeaderMap_new(
                      serialization_arena->ptr());
                  PopulateMetadataBatchToHeaderMap(
                      **shared_metadata,
                      self->config()->forwarding_allowed_headers,
                      self->config()->forwarding_disallowed_headers,
                      serialization_arena->ptr(), upb_headers);
                }
                return Seq(
                    ext_proc_call->SendMessageLocked(
                        send_headers,
                        [self, ext_proc_call, serialization_arena,
                         upb_headers]() mutable {
                          GRPC_TRACE_LOG(ext_proc_filter, INFO)
                              << "ExtProc: Sending server trailers-only as "
                                 "headers (observability mode)";
                          upb::Arena arena;
                          return CreateExtProcRequest(
                              arena.ptr(), ExtProcRequestType::kServerHeaders,
                              upb_headers,
                              self->config()->forwarding_allowed_headers,
                              self->config()->forwarding_disallowed_headers,
                              /*attributes=*/nullptr,
                              /*observability_mode=*/true,
                              ext_proc_call->IsFirstMessageOnStream(),
                              self->config()->processing_mode.send_request_body,
                              self->config()
                                  ->processing_mode.send_response_body,
                              /*end_of_stream=*/true);
                        }),
                    [handler, ext_proc_call,
                     shared_metadata](absl::Status result) mutable {
                      handler.SpawnPushServerTrailingMetadata(
                          std::move(*shared_metadata));
                      return result;
                    });
              },
              [self, handler, initiator, ext_proc_call, send_headers,
               shared_metadata]() mutable {
                return If(
                    send_headers,
                    [self, handler, initiator, ext_proc_call,
                     shared_metadata]() mutable {
                      return TrySeq(
                          self->SendServerInitialMetadataRequest(
                              ext_proc_call, shared_metadata),
                          [self, handler, initiator, ext_proc_call,
                           shared_metadata]() mutable {
                            return Seq(
                                TrySeq(
                                    Race(
                                        ext_proc_call->response_headers_latch_
                                            .Wait(),
                                        Map(ext_proc_call
                                                ->stream_error_status_latch_
                                                .Wait(),
                                            [ext_proc_call](Empty) {
                                              return absl::StatusOr<
                                                  ExtProcResponse>(
                                                  ext_proc_call
                                                      ->GetStreamErrorStatus());
                                            })),
                                    [self, ext_proc_call, shared_metadata](
                                        ExtProcResponse response) mutable
                                        -> absl::Status {
                                      if (response.response_headers
                                              .has_value()) {
                                        const auto& response_headers =
                                            *response.response_headers;
                                        if (!response_headers.ok()) {
                                          return response_headers.status();
                                        }
                                        const auto* rules =
                                            self->config()
                                                    ->mutation_rules.has_value()
                                                ? &self->config()
                                                       ->mutation_rules.value()
                                                : nullptr;
                                        return ApplyHeaderMutations(
                                            *response_headers, rules,
                                            **shared_metadata);
                                      }
                                      return absl::OkStatus();
                                    }),
                                [handler,
                                 shared_metadata](absl::Status result) mutable {
                                  if (!result.ok()) {
                                    *shared_metadata =
                                        CancelledServerMetadataFromStatus(
                                            result);
                                  }
                                  handler.SpawnPushServerTrailingMetadata(
                                      std::move(*shared_metadata));
                                  return result;
                                });
                          });
                    },
                    [handler, shared_metadata]() mutable -> absl::Status {
                      handler.SpawnPushServerTrailingMetadata(
                          std::move(*shared_metadata));
                      return absl::OkStatus();
                    });
              });
        },
        [self, handler, initiator, ext_proc_call, shared_metadata]() mutable {
          const bool send_trailers_to_ext_proc_stream =
              self->config()->processing_mode.send_response_trailers &&
              IsStatusOk(*shared_metadata) && !ext_proc_call->IsStreamClosed();
          return If(
              send_trailers_to_ext_proc_stream,
              [self, handler, initiator, ext_proc_call,
               shared_metadata]() mutable {
                return If(
                    self->config()->observability_mode,
                    [self, handler, initiator, ext_proc_call,
                     shared_metadata]() mutable {
                      return self->ServerTrailingMetadataMaybeObservabilityMode(
                          handler, initiator, std::move(ext_proc_call),
                          /*send_to_ext_proc_stream=*/true, shared_metadata);
                    },
                    [self, handler, initiator, ext_proc_call,
                     shared_metadata]() mutable {
                      return self->ServerTrailingMetadataNormalMode(
                          handler, initiator, std::move(ext_proc_call),
                          shared_metadata);
                    });
              },
              [self, handler, initiator, ext_proc_call,
               shared_metadata]() mutable {
                return self->ServerTrailingMetadataMaybeObservabilityMode(
                    handler, initiator, std::move(ext_proc_call),
                    /*send_to_ext_proc_stream=*/false, shared_metadata);
              });
        });
  });
}

auto ExtProcFilter::ProcessServerToClient(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  auto process_flow = TrySeq(
      initiator.PullServerInitialMetadata(),
      [self = RefAsSubclass<ExtProcFilter>(), handler, initiator,
       ext_proc_call](std::optional<ServerMetadataHandle> metadata) mutable {
        const bool has_md = metadata.has_value();
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerInitialMetadata received, present: " << has_md;
        return If(
            has_md,
            [self, handler, initiator, ext_proc_call,
             metadata = std::move(metadata)]() mutable {
              auto shared_metadata =
                  std::make_shared<ServerMetadataHandle>(std::move(*metadata));
              const bool is_trailers_only =
                  (*shared_metadata)->get(GrpcTrailersOnly()).value_or(false);
              const bool send_headers =
                  self->config()->processing_mode.send_response_headers &&
                  !ext_proc_call->IsStreamClosed();

              return If(
                  self->config()->observability_mode,
                  [self, handler, initiator, ext_proc_call, send_headers,
                   shared_metadata]() mutable {
                    auto server_initial_metadata_promise =
                        self->ServerInitialMetadataMaybeObservabilityMode(
                            handler, initiator, ext_proc_call, send_headers,
                            shared_metadata);
                    return Race(
                        TrySeq(
                            TryJoin<absl::StatusOr>(
                                std::move(server_initial_metadata_promise),
                                self->ServerToClientMessagesMaybeObservabilityMode(
                                    handler, initiator, ext_proc_call,
                                    /*send_to_ext_proc_stream=*/true)),
                            [self, handler, initiator,
                             ext_proc_call]() mutable {
                              return self->ServerTrailingMetadata(
                                  handler, initiator, std::move(ext_proc_call));
                            }),
                        Map(ext_proc_call->stream_error_status_latch_.Wait(),
                            [ext_proc_call](Empty) {
                              return ext_proc_call->GetStreamErrorStatus();
                            }));
                  },
                  [self, handler, initiator, ext_proc_call, send_headers,
                   shared_metadata, is_trailers_only]() mutable {
                    // Normal Mode: SIM (sequential) -> Messages (concurrent
                    // Write/Read) -> Trailers (sequential)

                    // 1. SIM Stage (sequential)
                    auto sim_stage = If(
                        send_headers,
                        [self, ext_proc_call, shared_metadata, handler,
                         initiator]() {
                          return TrySeq(
                              self->SendServerInitialMetadataRequest(
                                  ext_proc_call, shared_metadata),
                              [self, handler, initiator, ext_proc_call,
                               shared_metadata]() {
                                return self->ReadServerInitialMetadataResponse(
                                    handler, initiator, ext_proc_call,
                                    shared_metadata);
                              });
                        },
                        [handler, shared_metadata]() mutable -> absl::Status {
                          handler.SpawnPushServerInitialMetadata(
                              std::move(*shared_metadata));
                          return absl::OkStatus();
                        });

                    // 2. Messages Stage (concurrent)
                    auto messages_stage = [self, handler, initiator,
                                           ext_proc_call, is_trailers_only]() {
                      return If(
                          is_trailers_only,
                          []() -> absl::Status { return absl::OkStatus(); },
                          [self, handler, initiator, ext_proc_call]() {
                            const bool response_body_enabled =
                                self->config()
                                    ->processing_mode.send_response_body;

                            auto messages_write =
                                self->SendServerToClientMessagesRequest(
                                    handler, initiator, ext_proc_call);

                            auto messages_read = If(
                                response_body_enabled,
                                [self, handler, initiator, ext_proc_call]() {
                                  return self
                                      ->ReadServerToClientMessagesResponse(
                                          handler, initiator, ext_proc_call);
                                },
                                []() -> absl::Status {
                                  return absl::OkStatus();
                                });

                            return AllOk<absl::Status>(
                                std::move(messages_write),
                                std::move(messages_read));
                          });
                    };

                    // 3. Trailers Stage (sequential)
                    auto trailers_stage = [self, handler, initiator,
                                           ext_proc_call,
                                           is_trailers_only]() mutable {
                      return If(
                          is_trailers_only,
                          []() -> absl::Status { return absl::OkStatus(); },
                          [self, handler, initiator, ext_proc_call]() mutable {
                            return Seq(
                                initiator.PullServerTrailingMetadata(),
                                [self, handler, initiator,
                                 ext_proc_call](ServerMetadataHandle md) {
                                  auto shared_md =
                                      std::make_shared<ServerMetadataHandle>(
                                          std::move(md));
                                  const bool send_trailers =
                                      self->config()
                                          ->processing_mode
                                          .send_response_trailers &&
                                      IsStatusOk(*shared_md) &&
                                      !ext_proc_call->IsStreamClosed();
                                  return If(
                                      send_trailers,
                                      [self, handler, initiator, ext_proc_call,
                                       shared_md]() {
                                        return TrySeq(
                                            self->SendServerTrailingMetadataRequest(
                                                ext_proc_call, shared_md),
                                            [self, handler, initiator,
                                             ext_proc_call, shared_md]() {
                                              return self
                                                  ->ReadServerTrailingMetadataResponse(
                                                      handler, initiator,
                                                      ext_proc_call, shared_md);
                                            });
                                      },
                                      [handler,
                                       shared_md]() mutable -> absl::Status {
                                        handler.SpawnPushServerTrailingMetadata(
                                            std::move(*shared_md));
                                        return absl::OkStatus();
                                      });
                                });
                          });
                    };

                    // Run the stages sequentially
                    auto normal_flow =
                        TrySeq(std::move(sim_stage), std::move(messages_stage),
                               std::move(trailers_stage));

                    // Race the entire normal flow with the stream error status
                    // latch
                    return Race(
                        std::move(normal_flow),
                        Map(ext_proc_call->stream_error_status_latch_.Wait(),
                            [ext_proc_call](Empty) {
                              return ext_proc_call->GetStreamErrorStatus();
                            }));
                  });
            },
            [self, handler, initiator, ext_proc_call]() mutable {
              return self->ServerTrailingMetadata(handler, initiator,
                                                  std::move(ext_proc_call));
            });
      });
  return Map(
      std::move(process_flow),
      [handler, initiator](absl::Status result) mutable -> StatusFlag {
        if (!result.ok() && result.code() != absl::StatusCode::kCancelled) {
          handler.SpawnPushServerTrailingMetadata(
              CancelledServerMetadataFromStatus(result));
          initiator.SpawnCancel(result);
          return Failure{};
        }
        return Success{};
      });
}

auto ExtProcFilter::SendClientMessageRequest(
    const MessageHandle& message, ExtProcCall* ext_proc_call,
    bool end_of_stream, bool end_of_stream_without_message,
    bool send_to_ext_proc_stream, ::google_protobuf_Struct* attributes) {
  Message* msg_ptr = message.get();
  return ext_proc_call->SendMessageLocked(
      send_to_ext_proc_stream, [this, ext_proc_call, msg_ptr, end_of_stream,
                                end_of_stream_without_message, attributes]() {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ClientToServerMessages body message intercepted:\n"
            << (msg_ptr != nullptr ? msg_ptr->DebugString() : "<nullptr>");
        upb::Arena arena;
        std::string message_bytes;
        if (msg_ptr != nullptr) {
          message_bytes = msg_ptr->payload()->JoinIntoString();
        }
        ext_proc_call->MarkFirstBodyMessageSent();
        return CreateExtProcRequest(
            arena.ptr(), ExtProcRequestType::kClientMessage,
            upb_StringView_FromDataAndSize(message_bytes.data(),
                                           message_bytes.size()),
            /*allowed_headers=*/{}, /*disallowed_headers=*/{}, attributes,
            config_->observability_mode,
            ext_proc_call->IsFirstMessageOnStream(),
            config_->processing_mode.send_request_body,
            config_->processing_mode.send_response_body, end_of_stream,
            end_of_stream_without_message);
      });
}

auto ExtProcFilter::SendClientMessageRequest(
    std::string message_bytes, ExtProcCall* ext_proc_call, bool end_of_stream,
    bool end_of_stream_without_message, bool send_to_ext_proc_stream,
    ::google_protobuf_Struct* attributes) {
  return ext_proc_call->SendMessageLocked(
      send_to_ext_proc_stream,
      [this, ext_proc_call, message_bytes = std::move(message_bytes),
       end_of_stream, end_of_stream_without_message, attributes]() {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ClientToServerMessages body message intercepted "
               "(observability mode)";
        upb::Arena arena;
        ext_proc_call->MarkFirstBodyMessageSent();
        return CreateExtProcRequest(
            arena.ptr(), ExtProcRequestType::kClientMessage,
            upb_StringView_FromDataAndSize(message_bytes.data(),
                                           message_bytes.size()),
            /*allowed_headers=*/{}, /*disallowed_headers=*/{}, attributes,
            config_->observability_mode,
            ext_proc_call->IsFirstMessageOnStream(),
            config_->processing_mode.send_request_body,
            config_->processing_mode.send_response_body, end_of_stream,
            end_of_stream_without_message);
      });
}

auto ExtProcFilter::ClientToServerMessagesMaybeObservabilityMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_ext_proc_stream,
    ::google_protobuf_Struct* attributes) {
  return TrySeq(
      ForEach(
          MessagesFrom(handler),
          [self = RefAsSubclass<ExtProcFilter>(), initiator, ext_proc_call,
           send_to_ext_proc_stream, attributes](MessageHandle message) mutable {
            return If(
                ext_proc_call->IsProcessorSentHalfClose(),
                []() -> absl::Status {
                  return absl::InternalError(
                      "Client sends closed by external processor");
                },
                [self, initiator, ext_proc_call, send_to_ext_proc_stream,
                 attributes, message = std::move(message)]() mutable {
                  std::string message_bytes;
                  const bool send = send_to_ext_proc_stream &&
                                    !ext_proc_call->IsStreamClosed();
                  if (send && message != nullptr) {
                    message_bytes = message->payload()->JoinIntoString();
                  }
                  initiator.SpawnPushMessage(std::move(message));
                  return self->SendClientMessageRequest(
                      std::move(message_bytes), ext_proc_call.get(),
                      /*end_of_stream=*/false,
                      /*end_of_stream_without_message=*/false, send,
                      attributes);
                });
          }),
      [self = RefAsSubclass<ExtProcFilter>(), initiator, ext_proc_call,
       send_to_ext_proc_stream, attributes]() mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ClientToServerMessages finished sends";
        std::string message_bytes;
        const bool send =
            send_to_ext_proc_stream && !ext_proc_call->IsStreamClosed();
        return Map(
            self->SendClientMessageRequest(
                std::move(message_bytes), ext_proc_call.get(),
                /*end_of_stream=*/false,
                /*end_of_stream_without_message=*/true, send, attributes),
            [initiator](absl::Status status) mutable {
              initiator.SpawnFinishSends();
              return status;
            });
      });
}

auto ExtProcFilter::ClientToServerMessagesNormalMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call,
    ::google_protobuf_Struct* attributes) {
  auto client_to_sidestream = TrySeq(
      ForEach(
          MessagesFrom(handler),
          [self = RefAsSubclass<ExtProcFilter>(), initiator, ext_proc_call,
           attributes](MessageHandle message) mutable {
            return If(
                ext_proc_call->IsProcessorSentHalfClose(),
                []() {
                  // TODO(rishesh): Once PH2 work is done, we should make this
                  // pass (discard or handle cleanly). Currently we fail the
                  // RPC to avoid crashes on half-closed transport.
                  return absl::InternalError(
                      "Client sends closed by external processor");
                },
                [self, initiator, ext_proc_call, attributes,
                 message = std::move(message)]() mutable {
                  const bool send_to_ext_proc_stream =
                      !ext_proc_call->IsStreamClosed();
                  if (send_to_ext_proc_stream) {
                    ext_proc_call->IncrementOutstandingClientToServerMessages();
                  }
                  return Map(
                      self->SendClientMessageRequest(
                          message, ext_proc_call.get(),
                          /*end_of_stream=*/false,
                          /*end_of_stream_without_message=*/false,
                          send_to_ext_proc_stream, attributes),
                      [ext_proc_call, initiator, message = std::move(message)](
                          absl::Status status) mutable {
                        if (ext_proc_call->IsStreamClosed()) {
                          initiator.SpawnPushMessage(std::move(message));
                        }
                        return status;
                      });
                });
          }),
      [self = RefAsSubclass<ExtProcFilter>(), initiator, ext_proc_call,
       attributes]() mutable {
        return If(
            ext_proc_call->IsProcessorSentHalfClose(),
            [ext_proc_call]() {
              ext_proc_call->MarkClientSendsDone();
              return absl::OkStatus();
            },
            [self, initiator, ext_proc_call, attributes]() mutable {
              MessageHandle null_msg = nullptr;
              const bool send_to_ext_proc_stream =
                  !ext_proc_call->IsStreamClosed();
              if (send_to_ext_proc_stream) {
                ext_proc_call->IncrementOutstandingClientToServerMessages();
              }
              return Map(
                  self->SendClientMessageRequest(
                      null_msg, ext_proc_call.get(),
                      /*end_of_stream=*/false,
                      /*end_of_stream_without_message=*/true,
                      send_to_ext_proc_stream, attributes),
                  [ext_proc_call, initiator](absl::Status status) mutable {
                    if (ext_proc_call->IsStreamClosed()) {
                      initiator.SpawnFinishSends();
                    }
                    ext_proc_call->MarkClientSendsDone();
                    return status;
                  });
            });
      });
  auto sidestream_to_server = Seq(
      ForEach(std::move(ext_proc_call->request_body_pipe_.receiver),
              [initiator,
               ext_proc_call](absl::StatusOr<ExtProcResponse> result) mutable {
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << "ExtProc: sidestream_to_server ForEach got item, ok: "
                    << result.ok();
                if (result.ok()) {
                  auto& ext_proc_response = *result;
                  if (ext_proc_response.request_body.has_value()) {
                    if (!ext_proc_call
                             ->DecrementOutstandingClientToServerMessages()) {
                      return absl::InternalError(
                          "Received unexpected request body response from "
                          "external processor");
                    }
                    const auto& request_body = *ext_proc_response.request_body;
                    if (!request_body.ok()) {
                      return request_body.status();
                    }
                    const auto& body_mutation = *request_body;
                    if (!body_mutation.end_of_stream_without_message) {
                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                          << "ExtProc: ClientToServerMessages playing body "
                             "mutation: "
                          << body_mutation.body.size() << "b";
                      auto slice = Slice::FromCopiedString(body_mutation.body);
                      auto new_msg = initiator.arena()->MakePooled<Message>(
                          SliceBuffer(std::move(slice)),
                          /*flags=*/0);
                      initiator.SpawnPushMessage(std::move(new_msg));
                    }
                  }
                }
                return absl::OkStatus();
              }),
      [initiator, ext_proc_call](absl::Status status) mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ClientToServerMessages finished sends, status: "
            << status.ToString()
            << ", IsClientSendsDone: " << ext_proc_call->IsClientSendsDone()
            << ", IsStreamClosed: " << ext_proc_call->IsStreamClosed();
        if (ext_proc_call->IsClientSendsDone() ||
            !ext_proc_call->IsStreamClosed()) {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Calling SpawnFinishSends";
          initiator.SpawnFinishSends();
        }
        return status;
      });
  return Map(TryJoin<absl::StatusOr>(std::move(client_to_sidestream),
                                     std::move(sidestream_to_server)),
             [ext_proc_call](auto result) -> absl::Status {
               if (!result.ok()) return result.status();
               return ext_proc_call->GetStreamErrorStatus();
             });
}

auto ExtProcFilter::ClientToServerMessages(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call,
    ::google_protobuf_Struct* attributes) {
  return Map(
      Race(If(
               config_->processing_mode.send_request_body,
               [this, handler, initiator, ext_proc_call, attributes]() mutable {
                 return If(
                     config_->observability_mode,
                     [this, handler, initiator, ext_proc_call,
                      attributes]() mutable {
                       return ClientToServerMessagesMaybeObservabilityMode(
                           handler, initiator, std::move(ext_proc_call),
                           /*send_to_ext_proc_stream=*/true, attributes);
                     },
                     [this, handler, initiator, ext_proc_call,
                      attributes]() mutable {
                       return ClientToServerMessagesNormalMode(
                           handler, initiator, std::move(ext_proc_call),
                           attributes);
                     });
               },
               [this, handler, initiator, ext_proc_call, attributes]() mutable {
                 return ClientToServerMessagesMaybeObservabilityMode(
                     handler, initiator, std::move(ext_proc_call),
                     /*send_to_ext_proc_stream=*/false, attributes);
               }),
           Map(ext_proc_call->stream_error_status_latch_.Wait(),
               [ext_proc_call](Empty) {
                 return ext_proc_call->GetStreamErrorStatus();
               })),
      [initiator, ext_proc_call](absl::Status status) mutable {
        if (!status.ok()) {
          initiator.SpawnCancel(status);
        }
        return status;
      });
}

void ExtProcFilter::InterceptCall(UnstartedCallHandler unstarted_call_handler) {
  CallHandler handler = Consume(std::move(unstarted_call_handler));
  if (!IsProcessingEnabled(config_->processing_mode)) {
    handler.SpawnGuarded(
        "ext_proc_bypass",
        [self = RefAsSubclass<ExtProcFilter>(), handler]() mutable {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: No processing mode enabled, bypassing filter";
          return TrySeq(handler.PullClientInitialMetadata(),
                        [self, handler](ClientMetadataHandle metadata) mutable {
                          CallInitiator initiator = self->MakeChildCall(
                              std::move(metadata), handler.arena()->Ref());
                          handler.AddChildCall(initiator);
                          ForwardCall(handler, initiator);
                          return absl::OkStatus();
                        });
        });
    return;
  }
  handler.SpawnGuarded("ext_proc_call", [self = RefAsSubclass<ExtProcFilter>(),
                                         handler]() mutable {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: InterceptCall promise chain start";
    auto ext_proc_call = MakeRefCounted<ExtProcCall>(
        self->channel(), self->config()->processing_mode,
        self->config()->observability_mode, self->config()->failure_mode_allow,
        self->config()->deferred_close_timeout);
    return If(
        self->config()->observability_mode,
        [self, handler, ext_proc_call]() mutable {
          // Observability Mode: Concurrent Flow
          return TrySeq(
              handler.PullClientInitialMetadata(),
              [self, handler,
               ext_proc_call](ClientMetadataHandle metadata) mutable {
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << "ExtProc: Client initial metadata received "
                       "(observability):\n"
                    << metadata->DebugString();
                const bool send_headers =
                    self->config()->processing_mode.send_request_headers;
                std::shared_ptr<upb::Arena> serialization_arena;
                envoy_config_core_v3_HeaderMap* upb_headers = nullptr;
                ::google_protobuf_Struct* header_attributes = nullptr;
                ::google_protobuf_Struct* attributes = nullptr;
                upb::Arena* attributes_arena = nullptr;

                if (send_headers) {
                  serialization_arena = std::make_shared<upb::Arena>();
                  upb_headers = envoy_config_core_v3_HeaderMap_new(
                      serialization_arena->ptr());
                  PopulateMetadataBatchToHeaderMap(
                      *metadata, self->config()->forwarding_allowed_headers,
                      self->config()->forwarding_disallowed_headers,
                      serialization_arena->ptr(), upb_headers);
                  header_attributes = ParseAttributes(
                      serialization_arena->ptr(),
                      self->config()->request_attributes, *metadata,
                      self->default_authority_.as_string_view());
                } else if (self->config()->processing_mode.send_request_body &&
                           !self->config()->request_attributes.empty()) {
                  attributes_arena = handler.arena()->New<upb::Arena>();
                  attributes = ParseAttributes(
                      attributes_arena->ptr(),
                      self->config()->request_attributes, *metadata,
                      self->default_authority_.as_string_view());
                }

                auto client_headers_write_promise =
                    ext_proc_call->SendMessageLocked(
                        send_headers,
                        [self, ext_proc_call, serialization_arena, upb_headers,
                         header_attributes]() mutable {
                          GRPC_TRACE_LOG(ext_proc_filter, INFO)
                              << "ExtProc: Sending client initial metadata "
                                 "(observability mode)";
                          upb::Arena arena;
                          return CreateExtProcRequest(
                              arena.ptr(), ExtProcRequestType::kClientHeaders,
                              upb_headers,
                              self->config()->forwarding_allowed_headers,
                              self->config()->forwarding_disallowed_headers,
                              header_attributes,
                              self->config()->observability_mode,
                              ext_proc_call->IsFirstMessageOnStream(),
                              self->config()->processing_mode.send_request_body,
                              self->config()
                                  ->processing_mode.send_response_body);
                        });

                // Push headers upstream immediately to start the child call
                CallInitiator initiator = self->MakeChildCall(
                    std::move(metadata), handler.arena()->Ref());
                handler.AddChildCall(initiator);

                // Spawn server_to_client task immediately
                initiator.SpawnGuarded(
                    "server_to_client",
                    [self, handler, initiator, ext_proc_call]() mutable {
                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                          << "ExtProc: server_to_client task started";
                      return self->ProcessServerToClient(handler, initiator,
                                                         ext_proc_call);
                    });

                // Run client headers write and client-to-server messages
                // concurrently
                return Map(
                    TryJoin<absl::StatusOr>(
                        std::move(client_headers_write_promise),
                        self->ClientToServerMessages(
                            handler, initiator, ext_proc_call, attributes)),
                    [initiator](auto result) mutable -> absl::Status {
                      if (!result.ok()) {
                        initiator.SpawnCancel(result.status());
                      }
                      return result.status();
                    });
              });
        },
        [self, handler, ext_proc_call]() mutable {
          // Normal Mode: Sequential Flow
          return TrySeq(
              handler.PullClientInitialMetadata(),
              [self, handler,
               ext_proc_call](ClientMetadataHandle metadata) mutable {
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << "ExtProc: Client initial metadata received:\n"
                    << metadata->DebugString();
                auto shared_metadata =
                    std::make_shared<ClientMetadataHandle>(std::move(metadata));
                const bool send_headers =
                    self->config()->processing_mode.send_request_headers;
                return Seq(
                    ext_proc_call->SendMessageLocked(
                        send_headers,
                        [self, ext_proc_call, shared_metadata]() {
                          GRPC_TRACE_LOG(ext_proc_filter, INFO)
                              << "ExtProc: Sending client initial metadata";
                          bool is_first_message_on_stream =
                              ext_proc_call->IsFirstMessageOnStream();
                          upb::Arena serialization_arena;
                          return CreateExtProcRequest(
                              serialization_arena.ptr(),
                              ExtProcRequestType::kClientHeaders,
                              shared_metadata->get(),
                              self->config()->forwarding_allowed_headers,
                              self->config()->forwarding_disallowed_headers,
                              ParseAttributes(
                                  serialization_arena.ptr(),
                                  self->config()->request_attributes,
                                  **shared_metadata,
                                  self->default_authority_.as_string_view()),
                              self->config()->observability_mode,
                              is_first_message_on_stream,
                              self->config()->processing_mode.send_request_body,
                              self->config()
                                  ->processing_mode.send_response_body);
                        }),
                    [shared_metadata](absl::Status) mutable
                        -> absl::StatusOr<ClientMetadataHandle> {
                      return std::move(*shared_metadata);
                    });
              },
              [self, handler,
               ext_proc_call](ClientMetadataHandle metadata) mutable {
                return TrySeq(
                    If(
                        self->config()->processing_mode.send_request_headers &&
                            !self->config()->observability_mode,
                        [ext_proc_call]() {
                          return Race(
                              ext_proc_call->request_headers_latch_.Wait(),
                              Map(ext_proc_call->stream_error_status_latch_
                                      .Wait(),
                                  [ext_proc_call](Empty) {
                                    return absl::StatusOr<ExtProcResponse>(
                                        ext_proc_call->GetStreamErrorStatus());
                                  }));
                        },
                        []() -> absl::StatusOr<ExtProcResponse> {
                          return ExtProcResponse{};
                        }),
                    [self, metadata = std::move(metadata)](
                        ExtProcResponse response) mutable
                        -> absl::StatusOr<ClientMetadataHandle> {
                      if (response.request_headers.has_value()) {
                        const auto& request_headers = *response.request_headers;
                        if (!request_headers.ok()) {
                          return request_headers.status();
                        }
                        const auto* rules =
                            self->config()->mutation_rules.has_value()
                                ? &self->config()->mutation_rules.value()
                                : nullptr;
                        auto status = ApplyHeaderMutations(*request_headers,
                                                           rules, *metadata);
                        if (!status.ok()) return status;
                      }
                      return std::move(metadata);
                    },
                    [self, handler,
                     ext_proc_call](ClientMetadataHandle metadata) mutable {
                      ::google_protobuf_Struct* attributes = nullptr;
                      if (!self->config()
                               ->processing_mode.send_request_headers &&
                          self->config()->processing_mode.send_request_body &&
                          !self->config()->request_attributes.empty()) {
                        auto* attributes_arena =
                            handler.arena()->New<upb::Arena>();
                        attributes = ParseAttributes(
                            attributes_arena->ptr(),
                            self->config()->request_attributes, *metadata,
                            self->default_authority_.as_string_view());
                      }
                      CallInitiator initiator = self->MakeChildCall(
                          std::move(metadata), handler.arena()->Ref());
                      handler.AddChildCall(initiator);
                      initiator.SpawnGuarded(
                          "server_to_client",
                          [self, handler, initiator, ext_proc_call]() mutable {
                            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                                << "ExtProc: server_to_client task started";
                            return self->ProcessServerToClient(
                                handler, initiator, ext_proc_call);
                          });
                      return self->ClientToServerMessages(
                          handler, initiator, std::move(ext_proc_call),
                          !self->config()->processing_mode.send_request_headers
                              ? attributes
                              : nullptr);
                    });
              });
        });
  });
}

}  // namespace grpc_core