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

  bool GetAndSetIsFirstMessageOnStream() {
    MutexLock lock(&mu_);
    bool first = is_first_message_on_stream_;
    is_first_message_on_stream_ = false;
    return first;
  }

  bool GetAndSetIsFirstBodyMessage() {
    MutexLock lock(&mu_);
    bool first = is_first_body_message_;
    is_first_body_message_ = false;
    return first;
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

  void MarkClientSendsDone();
  void MarkServerSendsDone();
  void SetStreamErrorStatus(absl::Status status);
  absl::Status GetStreamErrorStatus();
  void IncrementOutstandingClientToServerMessages();
  bool DecrementOutstandingClientToServerMessages();
  void IncrementOutstandingServerToClientMessages();
  bool DecrementOutstandingServerToClientMessages();

  InterActivityLatch<absl::StatusOr<ExtProcResponse>> request_headers_latch_;
  InterActivityLatch<absl::StatusOr<ExtProcResponse>> response_headers_latch_;
  InterActivityLatch<absl::StatusOr<ExtProcResponse>> response_trailers_latch_;
  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 1> request_body_pipe_;
  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 1> response_body_pipe_;
  InterActivityLatch<void> dispatch_trailers_latch_;
  InterActivityLatch<void> stream_error_status_latch_;

  void OnRecvMessage(absl::string_view payload);
  void OnRequestSent();
  void OnStatusReceived(absl::Status status);

  auto SendMessageLocked(bool condition,
                         absl::AnyInvocable<std::string()> payload_generator);

 private:
  class StreamEventHandler final
      : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
   public:
    explicit StreamEventHandler(WeakRefCountedPtr<ExtProcCall> ext_proc_call)
        : ext_proc_call_(std::move(ext_proc_call)) {}

    void OnRequestSent(bool /*ok*/) override {
      if (auto call = ext_proc_call_->RefIfNonZero(); call != nullptr) {
        call->OnRequestSent();
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

  void Orphaned() override;
  void ClearWriteQueueAndUnblockLocked(
      std::vector<std::shared_ptr<InterActivityLatch<void>>>*
          latches_to_unblock) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);

  RefCountedPtr<ExtProcChannel> channel_;

  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall>
      streaming_call_;

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
  // True if no body messages (request or response) have been sent to the
  // processor yet. Used for failure_mode_allow bypass logic.
  bool is_first_body_message_ ABSL_GUARDED_BY(&mu_) = true;
  // True if the client has half-closed (finished sending request messages).
  bool client_sends_done_ ABSL_GUARDED_BY(&mu_) = false;
  // True if the server has finished sending response messages.
  bool server_sends_done_ ABSL_GUARDED_BY(&mu_) = false;
  // True if the processor half-closed its sending stream (sent EOS).
  bool processor_sent_half_close_ ABSL_GUARDED_BY(&mu_) = false;
  // Number of client request body messages sent to the processor that are
  // awaiting a response. Used to detect unexpected/unsolicited responses.
  int outstanding_client_to_server_messages_ ABSL_GUARDED_BY(mu_) = 0;
  // Number of server response body messages sent to the processor that are
  // awaiting a response. Used to detect unexpected responses and trigger
  // clean close.
  int outstanding_server_to_client_messages_ ABSL_GUARDED_BY(mu_) = 0;
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
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ext_proc server " << channel()->server()->server_uri()
      << ": starting ext_proc call (ext_proc_call=" << this
      << ", streaming_call=" << streaming_call_.get() << ")";
  streaming_call_->StartRecvMessage();
}

ExtProcFilter::ExtProcCall::~ExtProcCall() { streaming_call_.reset(); }

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
                if (self->streaming_call_ == nullptr) {
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
                  []() -> absl::Status {
                    return absl::AbortedError("Stream closed");
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

void ExtProcFilter::ExtProcCall::OnRequestSent() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " request sent";
  std::shared_ptr<InterActivityLatch<void>> latch;
  {
    MutexLock lock(&mu_);
    if (orphaned_) return;
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

void ExtProcFilter::ExtProcCall::MarkServerSendsDone() {
  bool close_pipe = false;
  {
    MutexLock lock(&mu_);
    if (!server_sends_done_) {
      GRPC_TRACE_LOG(ext_proc_filter, INFO)
          << "ExtProcCall " << this << " server sends done (boolean set)";
      server_sends_done_ = true;
      if (outstanding_server_to_client_messages_ == 0) {
        close_pipe = true;
      }
    }
  }
  if (close_pipe) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this
        << " MarkServerSendsDone: closing response body pipe";
    if (!response_body_pipe_.sender.IsClosed()) {
      response_body_pipe_.sender.MarkClosed();
    }
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
  bool close_pipe = false;
  {
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
    if (server_sends_done_ && outstanding_server_to_client_messages_ == 0) {
      close_pipe = true;
    }
  }
  if (close_pipe) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this
        << " DecrementOutstandingServerToClientMessages: closing response body "
           "pipe";
    if (!response_body_pipe_.sender.IsClosed()) {
      response_body_pipe_.sender.MarkClosed();
    }
  }
  return true;
}

void ExtProcFilter::ExtProcCall::OnStatusReceived(absl::Status status) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " status received: " << status;
  bool is_first_body_message = true;
  {
    MutexLock lock(&mu_);
    stream_closed_ = true;
    is_first_body_message = is_first_body_message_;
  }
  if (!status.ok() && (!failure_mode_allow_ || !is_first_body_message)) {
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
  {
    MutexLock lock(&mu_);
    ClearWriteQueueAndUnblockLocked(&latches_to_unblock);
    if (write_completed_latch_ != nullptr) {
      latches_to_unblock.push_back(std::move(write_completed_latch_));
    }
    streaming_call_.reset();
  }
  for (auto& latch : latches_to_unblock) {
    latch->Set();
  }
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
        if (processing_mode_.send_request_headers &&
            !request_headers_latch_.IsSet()) {
          request_headers_latch_.Set(std::move(parsed_response));
        }
      } else if (parsed_response.response_headers.has_value()) {
        if (processing_mode_.send_response_headers &&
            !response_headers_latch_.IsSet()) {
          response_headers_latch_.Set(std::move(parsed_response));
        }
      } else if (parsed_response.response_trailers.has_value()) {
        if (processing_mode_.send_response_trailers &&
            !response_trailers_latch_.IsSet()) {
          response_trailers_latch_.Set(std::move(parsed_response));
        }
      } else if (parsed_response.request_body.has_value()) {
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
  {
    MutexLock lock(&mu_);
    orphaned_ = true;
    ClearWriteQueueAndUnblockLocked(&latches_to_unblock);
    streaming_call_.reset();
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

auto ExtProcFilter::ServerInitialMetadataNormalMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataNormalMode pulled. metadata: "
      << (*metadata)->DebugString();
  return Seq(
      TrySeq(ext_proc_call->SendMessageLocked(
                 /*condition=*/true,
                 [self = RefAsSubclass<ExtProcFilter>(), ext_proc_call,
                  metadata]() {
                   GRPC_TRACE_LOG(ext_proc_filter, INFO)
                       << "ExtProc: Sending server initial metadata";
                   upb::Arena serialization_arena;
                   return CreateExtProcRequest(
                       serialization_arena.ptr(),
                       ExtProcRequestType::kServerHeaders, metadata->get(),
                       self->config()->forwarding_allowed_headers,
                       self->config()->forwarding_disallowed_headers,
                       /*attributes=*/nullptr,
                       /*observability_mode=*/false,
                       /*is_first_message=*/false,
                       self->config()->processing_mode.send_request_body,
                       self->config()->processing_mode.send_response_body);
                 }),
             ext_proc_call->response_headers_latch_.Wait(),
             [self = RefAsSubclass<ExtProcFilter>(),
              metadata](ExtProcResponse response) mutable -> absl::Status {
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
      [handler, initiator, metadata](absl::Status result) mutable {
        handler.SpawnPushServerInitialMetadata(std::move(*metadata));
        return result;
      });
}

auto ExtProcFilter::ServerInitialMetadataMaybeObservabilityMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_processor,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataMaybeObservabilityMode pulled. "
         "send_to_processor: "
      << send_to_processor << ", metadata: " << (*metadata)->DebugString();
  return Seq(
      ext_proc_call->SendMessageLocked(
          send_to_processor,
          [self = RefAsSubclass<ExtProcFilter>(), ext_proc_call, metadata]() {
            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                << "ExtProc: Sending server initial metadata (observability)";
            upb::Arena serialization_arena;
            return CreateExtProcRequest(
                serialization_arena.ptr(), ExtProcRequestType::kServerHeaders,
                metadata->get(), self->config()->forwarding_allowed_headers,
                self->config()->forwarding_disallowed_headers,
                /*attributes=*/nullptr,
                /*observability_mode=*/true,
                /*is_first_message=*/false,
                self->config()->processing_mode.send_request_body,
                self->config()->processing_mode.send_response_body);
          }),
      [handler, initiator, metadata](absl::Status result) mutable {
        handler.SpawnPushServerInitialMetadata(std::move(*metadata));
        return result;
      });
}

auto ExtProcFilter::ServerInitialMetadata(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  return TrySeq(
      initiator.PullServerInitialMetadata(),
      [self = RefAsSubclass<ExtProcFilter>(), handler, initiator,
       ext_proc_call](std::optional<ServerMetadataHandle> metadata) mutable {
        const bool has_md = metadata.has_value();
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerInitialMetadata received, present: " << has_md;
        return Map(
            If(
                has_md,
                [self, handler, initiator, ext_proc_call,
                 metadata = std::move(metadata)]() mutable {
                  auto shared_metadata = std::make_shared<ServerMetadataHandle>(
                      std::move(*metadata));
                  const bool send_headers =
                      self->config()->processing_mode.send_response_headers &&
                      !ext_proc_call->IsStreamClosed();
                  return If(
                      send_headers,
                      [self, handler, initiator, ext_proc_call,
                       shared_metadata]() mutable {
                        return If(
                            self->config()->observability_mode,
                            [self, handler, initiator, ext_proc_call,
                             shared_metadata]() mutable {
                              return self
                                  ->ServerInitialMetadataMaybeObservabilityMode(
                                      handler, initiator,
                                      std::move(ext_proc_call),
                                      /*send_to_processor=*/true,
                                      shared_metadata);
                            },
                            [self, handler, initiator, ext_proc_call,
                             shared_metadata]() mutable {
                              return self->ServerInitialMetadataNormalMode(
                                  handler, initiator, std::move(ext_proc_call),
                                  shared_metadata);
                            });
                      },
                      [self, handler, initiator, ext_proc_call,
                       shared_metadata]() mutable {
                        return self
                            ->ServerInitialMetadataMaybeObservabilityMode(
                                handler, initiator, std::move(ext_proc_call),
                                /*send_to_processor=*/false, shared_metadata);
                      });
                },
                []() -> absl::Status { return absl::OkStatus(); }),
            [handler, initiator](absl::Status result) mutable -> StatusFlag {
              if (!result.ok()) {
                handler.SpawnPushServerTrailingMetadata(
                    CancelledServerMetadataFromStatus(result));
                initiator.SpawnCancel();
                return Failure{};
              }
              return Success{};
            });
      });
}

auto ExtProcFilter::SendServerMessageRequest(const MessageHandle& message,
                                             ExtProcCall* ext_proc_call,
                                             bool send_to_processor) {
  Message* msg_ptr = message.get();
  return ext_proc_call->SendMessageLocked(send_to_processor, [this, msg_ptr,
                                                              ext_proc_call]() {
    ext_proc_call->GetAndSetIsFirstBodyMessage();
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: ServerToClientMessages body message intercepted:\n"
        << (msg_ptr != nullptr ? msg_ptr->DebugString() : "nullptr");
    upb::Arena arena;
    std::string message_bytes;
    if (msg_ptr != nullptr) {
      message_bytes = msg_ptr->payload()->JoinIntoString();
    }
    return CreateExtProcRequest(arena.ptr(), ExtProcRequestType::kServerMessage,
                                upb_StringView_FromDataAndSize(
                                    message_bytes.data(), message_bytes.size()),
                                {}, {},   // no headers
                                nullptr,  // no attributes
                                config_->observability_mode,
                                /*is_first_message=*/false,
                                config_->processing_mode.send_request_body,
                                config_->processing_mode.send_response_body,
                                /*end_of_stream=*/false,
                                /*end_of_stream_without_message=*/false);
  });
}

auto ExtProcFilter::ServerToClientMessagesMaybeObservabilityMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_processor) {
  return ForEach(
      MessagesFrom(initiator),
      [self = RefAsSubclass<ExtProcFilter>(), handler, ext_proc_call,
       send_to_processor](MessageHandle message) mutable {
        return Map(self->SendServerMessageRequest(
                       message, ext_proc_call.get(),
                       send_to_processor && !ext_proc_call->IsStreamClosed()),
                   [handler,
                    message = std::move(message)](absl::Status status) mutable {
                     handler.SpawnPushMessage(std::move(message));
                     return status;
                   });
      });
}

auto ExtProcFilter::ServerToClientMessagesNormalMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  auto server_to_sidestream = Map(
      ForEach(MessagesFrom(initiator),
              [self = RefAsSubclass<ExtProcFilter>(), handler,
               ext_proc_call](MessageHandle message) mutable {
                const bool send_to_processor = !ext_proc_call->IsStreamClosed();
                if (send_to_processor) {
                  ext_proc_call->IncrementOutstandingServerToClientMessages();
                }
                return Map(
                    self->SendServerMessageRequest(message, ext_proc_call.get(),
                                                   send_to_processor),
                    [ext_proc_call, handler, message = std::move(message)](
                        absl::Status status) mutable {
                      if (ext_proc_call->IsStreamClosed()) {
                        handler.SpawnPushMessage(std::move(message));
                      }
                      return status;
                    });
              }),
      [ext_proc_call](absl::Status status) {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerToClientMessages finished sends, status: "
            << status.ToString();
        ext_proc_call->MarkServerSendsDone();
        return status;
      });

  auto sidestream_to_client = Seq(
      ForEach(
          std::move(ext_proc_call->response_body_pipe_.receiver),
          [handler, initiator,
           ext_proc_call](absl::StatusOr<ExtProcResponse> result) mutable {
            if (result.ok()) {
              auto& ext_proc_response = *result;
              if (ext_proc_response.response_body.has_value()) {
                if (!ext_proc_call
                         ->DecrementOutstandingServerToClientMessages()) {
                  return absl::InternalError(
                      "Received unexpected response body response from "
                      "external processor");
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
                    << "ExtProc: ServerToClientMessages playing body mutation: "
                    << body_mutation.body.size() << "b";
                auto slice = Slice::FromCopiedString(body_mutation.body);
                auto new_msg = handler.arena()->MakePooled<Message>(
                    SliceBuffer(std::move(slice)),
                    /*flags=*/0);
                handler.SpawnPushMessage(std::move(new_msg));
                if (body_mutation.end_of_stream) {
                  ext_proc_call->dispatch_trailers_latch_.Set();
                }
              }
            }
            return absl::OkStatus();
          }),
      [initiator, ext_proc_call](absl::Status status) mutable {
        if (!ext_proc_call->IsStreamClosed()) {
          if (ext_proc_call->IsProcessorSentHalfClose()) {
            initiator.SpawnCancel(absl::InternalError(
                "Client sends closed by external processor"));
          } else {
            initiator.SpawnCancel();
          }
        }
        return status;
      });

  return Map(TryJoin<absl::StatusOr>(std::move(server_to_sidestream),
                                     std::move(sidestream_to_client)),
             [ext_proc_call](auto result) -> absl::Status {
               if (!result.ok()) return result.status();
               return ext_proc_call->GetStreamErrorStatus();
             });
}

auto ExtProcFilter::ServerToClientMessages(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  return Race(If(
                  config_->processing_mode.send_response_body,
                  [this, handler, initiator, ext_proc_call]() mutable {
                    return If(
                        config_->observability_mode,
                        [this, handler, initiator, ext_proc_call]() mutable {
                          return ServerToClientMessagesMaybeObservabilityMode(
                              handler, initiator, ext_proc_call,
                              /*send_to_processor=*/true);
                        },
                        [this, handler, initiator, ext_proc_call]() mutable {
                          return ServerToClientMessagesNormalMode(
                              handler, initiator, std::move(ext_proc_call));
                        });
                  },
                  [this, handler, initiator, ext_proc_call]() mutable {
                    return ServerToClientMessagesMaybeObservabilityMode(
                        handler, initiator, std::move(ext_proc_call),
                        /*send_to_processor=*/false);
                  }),
              Map(ext_proc_call->stream_error_status_latch_.Wait(),
                  [ext_proc_call](Empty) {
                    return ext_proc_call->GetStreamErrorStatus();
                  }));
}

auto ExtProcFilter::ServerTrailingMetadataNormalMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataNormalMode pulled. Status OK: "
      << IsStatusOk(*metadata) << ", metadata: " << (*metadata)->DebugString();
  return Seq(
      TrySeq(ext_proc_call->SendMessageLocked(
                 !ext_proc_call->IsStreamClosed(),
                 [self = RefAsSubclass<ExtProcFilter>(), ext_proc_call,
                  metadata]() {
                   GRPC_TRACE_LOG(ext_proc_filter, INFO)
                       << "ExtProc: Sending server trailing metadata";
                   upb::Arena serialization_arena;
                   return CreateExtProcRequest(
                       serialization_arena.ptr(),
                       ExtProcRequestType::kServerTrailers, metadata->get(),
                       self->config()->forwarding_allowed_headers,
                       self->config()->forwarding_disallowed_headers,
                       /*attributes=*/nullptr,
                       /*observability_mode=*/false,
                       /*is_first_message=*/false,
                       self->config()->processing_mode.send_request_body,
                       self->config()->processing_mode.send_response_body);
                 }),
             ext_proc_call->response_trailers_latch_.Wait(),
             [self = RefAsSubclass<ExtProcFilter>(),
              metadata](ExtProcResponse response) mutable -> absl::Status {
               GRPC_TRACE_LOG(ext_proc_filter, INFO)
                   << "ExtProc: ServerTrailingMetadata response received. "
                      "OK: true, has_trailers: "
                   << response.response_trailers.has_value();
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

auto ExtProcFilter::ServerTrailingMetadataMaybeObservabilityMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_processor,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataMaybeObservabilityMode pulled. "
         "Status OK: "
      << IsStatusOk(*metadata) << ", send_to_processor: " << send_to_processor
      << ", metadata: " << (*metadata)->DebugString();
  return Seq(
      ext_proc_call->SendMessageLocked(
          send_to_processor,
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
                /*is_first_message=*/false,
                self->config()->processing_mode.send_request_body,
                self->config()->processing_mode.send_response_body);
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

auto ExtProcFilter::ServerTrailingMetadata(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  return Seq(
      initiator.PullServerTrailingMetadata(),
      [self = RefAsSubclass<ExtProcFilter>(), handler, initiator,
       ext_proc_call](ServerMetadataHandle md) mutable {
        auto shared_metadata =
            std::make_shared<ServerMetadataHandle>(std::move(md));
        const bool send_trailers =
            self->config()->processing_mode.send_response_trailers &&
            IsStatusOk(*shared_metadata) && !ext_proc_call->IsStreamClosed();
        return If(
            send_trailers,
            [self, handler, initiator, ext_proc_call,
             shared_metadata]() mutable {
              return If(
                  self->config()->observability_mode,
                  [self, handler, initiator, ext_proc_call,
                   shared_metadata]() mutable {
                    return self->ServerTrailingMetadataMaybeObservabilityMode(
                        handler, initiator, std::move(ext_proc_call),
                        /*send_to_processor=*/true, shared_metadata);
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
                  /*send_to_processor=*/false, shared_metadata);
            });
      });
}

auto ExtProcFilter::ProcessServerToClient(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  return TrySeq(
      ServerInitialMetadata(handler, initiator, ext_proc_call),
      [this, handler, initiator, ext_proc_call]() mutable {
        return Seq(
            ServerToClientMessages(handler, initiator, ext_proc_call),
            [this, handler, initiator,
             ext_proc_call](absl::Status status) mutable {
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << "ExtProc: ServerToClientMessages completed with status: "
                  << status.ToString();
              const bool is_error =
                  !status.ok() && status.code() != absl::StatusCode::kCancelled;
              return If(
                  is_error,
                  [handler, initiator, status]() mutable {
                    handler.SpawnPushServerTrailingMetadata(
                        CancelledServerMetadataFromStatus(status));
                    initiator.SpawnCancel(status);
                    return absl::OkStatus();
                  },
                  [this, handler, initiator, ext_proc_call]() mutable {
                    return ServerTrailingMetadata(handler, initiator,
                                                  std::move(ext_proc_call));
                  });
            });
      });
}

auto ExtProcFilter::SendClientMessageRequest(
    const MessageHandle& message, ExtProcCall* ext_proc_call,
    bool end_of_stream, bool end_of_stream_without_message,
    bool send_to_processor, ::google_protobuf_Struct* attributes) {
  if (config_->processing_mode.send_request_headers) {
    attributes = nullptr;
  }
  Message* msg_ptr = message.get();
  return ext_proc_call->SendMessageLocked(
      send_to_processor, [this, ext_proc_call, msg_ptr, end_of_stream,
                          end_of_stream_without_message, attributes]() {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ClientToServerMessages body message intercepted:\n"
            << (msg_ptr != nullptr ? msg_ptr->DebugString() : "nullptr");
        upb::Arena arena;
        std::string message_bytes;
        if (msg_ptr != nullptr) {
          message_bytes = msg_ptr->payload()->JoinIntoString();
        }
        const bool is_first_message_on_stream =
            ext_proc_call->GetAndSetIsFirstMessageOnStream();
        ext_proc_call->GetAndSetIsFirstBodyMessage();
        ::google_protobuf_Struct* attrs = nullptr;
        if (is_first_message_on_stream) {
          attrs = attributes;
        }
        return CreateExtProcRequest(
            arena.ptr(), ExtProcRequestType::kClientMessage,
            upb_StringView_FromDataAndSize(message_bytes.data(),
                                           message_bytes.size()),
            /*allowed_headers=*/{}, /*disallowed_headers=*/{}, attrs,
            config_->observability_mode, is_first_message_on_stream,
            config_->processing_mode.send_request_body,
            config_->processing_mode.send_response_body, end_of_stream,
            end_of_stream_without_message);
      });
}

auto ExtProcFilter::ClientToServerMessagesMaybeObservabilityMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_processor,
    ::google_protobuf_Struct* attributes) {
  return TrySeq(
      ForEach(
          MessagesFrom(handler),
          [self = RefAsSubclass<ExtProcFilter>(), initiator, ext_proc_call,
           send_to_processor, attributes](MessageHandle message) mutable {
            return If(
                ext_proc_call->IsProcessorSentHalfClose(),
                []() -> absl::Status {
                  // PH2 Work is required here
                  return absl::InternalError(
                      "Client sends closed by external processor");
                },
                [self, initiator, ext_proc_call, send_to_processor, attributes,
                 message = std::move(message)]() mutable {
                  if (!send_to_processor || ext_proc_call->IsStreamClosed()) {
                    GRPC_TRACE_LOG(ext_proc_filter, INFO)
                        << "ExtProc: ClientToServerMessages bypass message "
                           "pushed upstream:\n"
                        << (message != nullptr ? message->DebugString()
                                               : "nullptr");
                  }
                  return Map(
                      self->SendClientMessageRequest(
                          message, ext_proc_call.get(),
                          /*end_of_stream=*/false,
                          /*end_of_stream_without_message=*/false,
                          send_to_processor && !ext_proc_call->IsStreamClosed(),
                          attributes),
                      [initiator, message = std::move(message)](
                          absl::Status status) mutable {
                        initiator.SpawnPushMessage(std::move(message));
                        return status;
                      });
                });
          }),
      [self = RefAsSubclass<ExtProcFilter>(), initiator, ext_proc_call,
       send_to_processor, attributes]() mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ClientToServerMessages finished sends";
        MessageHandle null_msg = nullptr;
        return Map(self->SendClientMessageRequest(
                       null_msg, ext_proc_call.get(),
                       /*end_of_stream=*/false,
                       /*end_of_stream_without_message=*/true,
                       send_to_processor && !ext_proc_call->IsStreamClosed(),
                       attributes),
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
                  const bool send_to_processor =
                      !ext_proc_call->IsStreamClosed();
                  if (send_to_processor) {
                    ext_proc_call->IncrementOutstandingClientToServerMessages();
                  }
                  return Map(
                      self->SendClientMessageRequest(
                          message, ext_proc_call.get(),
                          /*end_of_stream=*/false,
                          /*end_of_stream_without_message=*/false,
                          send_to_processor, attributes),
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
              const bool send_to_processor = !ext_proc_call->IsStreamClosed();
              if (send_to_processor) {
                ext_proc_call->IncrementOutstandingClientToServerMessages();
              }
              return Map(
                  self->SendClientMessageRequest(
                      null_msg, ext_proc_call.get(),
                      /*end_of_stream=*/false,
                      /*end_of_stream_without_message=*/true, send_to_processor,
                      attributes),
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
                           /*send_to_processor=*/true, attributes);
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
                     /*send_to_processor=*/false, attributes);
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
    return TrySeq(
        handler.PullClientInitialMetadata(),
        [self, handler, ext_proc_call](ClientMetadataHandle metadata) mutable {
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
                        ext_proc_call->GetAndSetIsFirstMessageOnStream();
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
                        self->config()->processing_mode.send_response_body);
                  }),
              [shared_metadata](absl::Status) mutable
                  -> absl::StatusOr<ClientMetadataHandle> {
                return std::move(*shared_metadata);
              });
        },
        [self, handler, ext_proc_call](ClientMetadataHandle metadata) mutable {
          return TrySeq(
              If(
                  self->config()->processing_mode.send_request_headers &&
                      !self->config()->observability_mode,
                  [ext_proc_call]() {
                    return ext_proc_call->request_headers_latch_.Wait();
                  },
                  []() -> absl::StatusOr<ExtProcResponse> {
                    return ExtProcResponse{};
                  }),
              [self,
               metadata = std::move(metadata)](ExtProcResponse response) mutable
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
                  auto status =
                      ApplyHeaderMutations(*request_headers, rules, *metadata);
                  if (!status.ok()) return status;
                }
                return std::move(metadata);
              },
              [self, handler,
               ext_proc_call](ClientMetadataHandle metadata) mutable {
                ::google_protobuf_Struct* attributes = nullptr;
                if (!self->config()->processing_mode.send_request_headers &&
                    self->config()->processing_mode.send_request_body &&
                    !self->config()->request_attributes.empty()) {
                  auto* attributes_arena = handler.arena()->New<upb::Arena>();
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
                      return self->ProcessServerToClient(handler, initiator,
                                                         ext_proc_call);
                    });
                return self->ClientToServerMessages(
                    handler, initiator, std::move(ext_proc_call), attributes);
              });
        });
  });
}

}  // namespace grpc_core