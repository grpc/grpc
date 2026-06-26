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

#include <atomic>
#include <string>

#include "envoy/service/ext_proc/v3/external_processor.upb.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/filters/ext_proc/ext_proc_messages.h"
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
#include "src/core/xds/xds_client/serialized_streaming_call.h"
#include "absl/log/log.h"

namespace grpc_core {

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
  InterActivityLatch<absl::StatusOr<ExtProcResponse>>& request_headers_latch() {
    return request_headers_latch_;
  }
  InterActivityLatch<absl::StatusOr<ExtProcResponse>>&
  response_headers_latch() {
    return response_headers_latch_;
  }
  InterActivityLatch<absl::StatusOr<ExtProcResponse>>&
  response_trailers_latch() {
    return response_trailers_latch_;
  }
  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 16>& request_body_pipe() {
    return request_body_pipe_;
  }
  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 16>& response_body_pipe() {
    return response_body_pipe_;
  }

  ExtProcCall(RefCountedPtr<ExtProcChannel> channel, bool observability_mode,
              bool failure_mode_allow, bool disable_immediate_response,
              const ProcessingMode& processing_mode,
              const Duration& deferred_close_timeout)
      : channel_(std::move(channel)),
        observability_mode_(observability_mode),
        failure_mode_allow_(failure_mode_allow),
        disable_immediate_response_(disable_immediate_response),
        processing_mode_(processing_mode),
        deferred_close_timeout_(deferred_close_timeout) {
    const char* method = "/envoy.service.ext_proc.v3.ExternalProcessor/Process";
    streaming_call_ = MakeOrphanable<SerializedStreamingCall>(
        channel_->transport(), method,
        std::make_unique<StreamEventHandler>(WeakRef()));
    streaming_call_->StartRecvMessage();
    // Start connectivity failure watch!
    RefCountedPtr<ConnectivityWatcher> watcher;
    {
      MutexLock lock(&mu_);
      connectivity_watcher_ = MakeRefCounted<ConnectivityWatcher>(WeakRef());
      watcher = connectivity_watcher_;
    }
    channel_->transport()->StartConnectivityFailureWatch(watcher);
    // Take a strong ref to keep ourselves alive until the stream is closed.
    Ref(DEBUG_LOCATION, "active_stream").release();
  }

  ~ExtProcCall() override { streaming_call_.reset(); }

  bool IsFirstMessageOnStream() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
    bool is_first = is_first_message_on_ext_proc_stream_;
    is_first_message_on_ext_proc_stream_ = false;
    return is_first;
  }

  void MarkFirstClientBodyMessageSentLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
    c2s_first_body_message_sent_ = true;
  }

  void MarkFirstServerBodyMessageSentLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
    s2c_first_body_message_sent_ = true;
  }

  bool IsClientFailOpenAllowed() {
    MutexLock lock(&mu_);
    return failure_mode_allow_ &&
           (!c2s_first_body_message_sent_ || observability_mode_);
  }

  bool IsServerFailOpenAllowed() {
    MutexLock lock(&mu_);
    return failure_mode_allow_ &&
           (!s2c_first_body_message_sent_ || observability_mode_);
  }

  bool IsStreamFailOpenAllowed() {
    MutexLock lock(&mu_);
    if (!failure_mode_allow_) return false;
    if (observability_mode_) return true;
    const bool c2s_committed =
        c2s_first_body_message_sent_ && !c2s_half_close_initiated_;
    const bool s2c_committed =
        s2c_first_body_message_sent_ && !s2c_writes_done_;
    return !(c2s_committed || s2c_committed);
  }

  void MarkClientHalfCloseInitiatedLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
    c2s_half_close_initiated_ = true;
  }

  bool IsStreamClosed() const {
    return stream_closed_.load(std::memory_order_acquire);
  }

  void IncrementOutstandingServerToClientMessages() {
    MutexLock lock(&mu_);
    outstanding_s2c_messages_++;
  }

  void MarkServerToClientWritesDone() {
    MutexLock lock(&mu_);
    s2c_writes_done_ = true;
    if (outstanding_s2c_messages_ == 0) {
      all_server_to_client_responses_received_latch_.Set();
    }
  }

  void SetIsTrailersOnly() {
    MutexLock lock(&mu_);
    is_trailers_only_ = true;
  }

  bool DecrementOutstandingServerToClientMessages() {
    MutexLock lock(&mu_);
    if (outstanding_s2c_messages_ == 0) {
      return false;
    }
    outstanding_s2c_messages_--;
    if (s2c_writes_done_ && outstanding_s2c_messages_ == 0) {
      all_server_to_client_responses_received_latch_.Set();
    }
    return true;
  }

  InterActivityLatch<void>& all_server_to_client_responses_received_latch() {
    return all_server_to_client_responses_received_latch_;
  }

  bool IsProcessorSentHalfClose() {
    MutexLock lock(&mu_);
    return processor_sent_half_close_;
  }

  void IncrementOutstandingClientToServerMessages() {
    MutexLock lock(&mu_);
    outstanding_c2s_messages_++;
  }

  bool DecrementOutstandingClientToServerMessages() {
    MutexLock lock(&mu_);
    if (outstanding_c2s_messages_ == 0) {
      return false;
    }
    outstanding_c2s_messages_--;
    return true;
  }

  void MarkClientSendsDone() {
    MutexLock lock(&mu_);
    c2s_writes_done_ = true;
  }

  bool IsClientSendsDone() {
    MutexLock lock(&mu_);
    return c2s_writes_done_;
  }

  absl::Status GetStreamErrorStatus() {
    MutexLock lock(&mu_);
    return stream_status_;
  }

  Mutex* mu() ABSL_LOCK_RETURNED(mu_) { return &mu_; }

  absl::AnyInvocable<Poll<absl::Status>()> SendMessageLocked(
      bool condition, absl::AnyInvocable<std::string()> payload_generator)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
    if (!condition) {
      return []() -> Poll<absl::Status> { return absl::OkStatus(); };
    }
    if (stream_closed_.load(std::memory_order_acquire) ||
        streaming_call_ == nullptr) {
      return []() -> Poll<absl::Status> {
        return absl::CancelledError("Stream closed");
      };
    }
    return streaming_call_->Send(payload_generator());
  }

  void CloseStream() {
    OrphanablePtr<SerializedStreamingCall> call_to_reset;
    RefCountedPtr<ConnectivityWatcher> watcher_to_stop;
    {
      MutexLock lock(&mu_);
      stream_closed_.store(true, std::memory_order_release);
      call_to_reset = std::move(streaming_call_);
      watcher_to_stop = std::move(connectivity_watcher_);
    }
    call_to_reset.reset();
    if (watcher_to_stop != nullptr) {
      channel_->transport()->StopConnectivityFailureWatch(watcher_to_stop);
    }
    if (!request_body_pipe_.sender.IsClosed()) {
      request_body_pipe_.sender.MarkClosed();
    }
    if (!response_body_pipe_.sender.IsClosed()) {
      response_body_pipe_.sender.MarkClosed();
    }
    if (!all_server_to_client_responses_received_latch_.IsSet()) {
      all_server_to_client_responses_received_latch_.Set();
    }
  }

 private:
  class StreamEventHandler final
      : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
   public:
    explicit StreamEventHandler(WeakRefCountedPtr<ExtProcCall> call)
        : call_(std::move(call)) {}

    void OnRequestSent(bool ok) override {
      if (auto call = call_->RefIfNonZero(); call != nullptr) {
        call->OnRequestSent(ok);
      }
    }

    void OnRecvMessage(absl::string_view payload) override {
      if (auto call = call_->RefIfNonZero(); call != nullptr) {
        call->OnRecvMessage(payload);
      }
    }

    void OnStatusReceived(absl::Status status) override {
      if (auto call = call_->RefIfNonZero(); call != nullptr) {
        call->OnStatusReceived(std::move(status));
      }
    }

   private:
    WeakRefCountedPtr<ExtProcCall> call_;
  };

  class ConnectivityWatcher final
      : public XdsTransportFactory::XdsTransport::ConnectivityFailureWatcher {
   public:
    explicit ConnectivityWatcher(WeakRefCountedPtr<ExtProcCall> call)
        : call_(std::move(call)) {}

    void OnConnectivityFailure(absl::Status status) override {
      if (call_ != nullptr) {
        auto call = call_->RefIfNonZero();
        if (call != nullptr) {
          call->OnStatusReceived(std::move(status));
        }
      }
    }

   private:
    WeakRefCountedPtr<ExtProcCall> call_;
  };

  void CompleteAllLatchesAndPipes(
      absl::StatusOr<ExtProcResponse> status_or_response) {
    if (processing_mode_.send_request_headers &&
        !request_headers_latch_.IsSet()) {
      request_headers_latch_.Set(status_or_response);
    }
    if (processing_mode_.send_response_headers &&
        !response_headers_latch_.IsSet()) {
      response_headers_latch_.Set(status_or_response);
    }
    if (processing_mode_.send_response_trailers &&
        !response_trailers_latch_.IsSet()) {
      response_trailers_latch_.Set(status_or_response);
    }
    if (processing_mode_.send_request_body &&
        !request_body_pipe_.sender.IsClosed()) {
      if (!status_or_response.ok()) {
        request_body_pipe_.sender.Push(status_or_response.status())();
      }
      request_body_pipe_.sender.MarkClosed();
    }
    if (processing_mode_.send_response_body &&
        !response_body_pipe_.sender.IsClosed()) {
      if (!status_or_response.ok()) {
        response_body_pipe_.sender.Push(status_or_response.status())();
      }
      response_body_pipe_.sender.MarkClosed();
    }
  }

  void SetStreamErrorStatus(absl::Status status) {
    MutexLock lock(&mu_);
    stream_status_ = status;
    stream_closed_.store(true, std::memory_order_release);
  }

  bool IsTrailersOnly() {
    MutexLock lock(&mu_);
    return is_trailers_only_;
  }

  void SetProcessorSentHalfClose() {
    MutexLock lock(&mu_);
    processor_sent_half_close_ = true;
  }

  void OnRequestSent(bool ok) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this << " request sent ok=" << ok;
  }

  void OnRecvMessage(absl::string_view payload) {
    const bool fail_open = IsStreamFailOpenAllowed();
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this
        << " message received, size=" << payload.size();
    upb::Arena arena;
    auto* response = envoy_service_ext_proc_v3_ProcessingResponse_parse(
        payload.data(), payload.size(), arena.ptr());
    if (response == nullptr) {
      LOG(ERROR) << "Failed to parse ProcessingResponse";
      auto error = absl::InternalError("Failed to parse ProcessingResponse");
      if (!fail_open) {
        CompleteAllLatchesAndPipes(error);
      } else {
        CompleteAllLatchesAndPipes(ExtProcResponse{});
      }
      CloseStream();
      return;
    } else {
      auto parsed_response_or =
          ParseExtProcResponse(response, observability_mode_);
      if (!parsed_response_or.ok()) {
        LOG(ERROR) << "Failed to validate ProcessingResponse: "
                   << parsed_response_or.status();
        if (!fail_open) {
          CompleteAllLatchesAndPipes(parsed_response_or.status());
        } else {
          CompleteAllLatchesAndPipes(ExtProcResponse{});
        }
        CloseStream();
        return;
      } else {
        auto parsed_response = std::move(*parsed_response_or);
        if (parsed_response.immediate_response.has_value() &&
            disable_immediate_response_) {
          auto error = absl::PermissionDeniedError(
              "unhandled immediate response due to config disabled it");
          if (!fail_open) {
            SetStreamErrorStatus(error);
          }
          if (!fail_open) {
            CompleteAllLatchesAndPipes(error);
          } else {
            CompleteAllLatchesAndPipes(ExtProcResponse{});
          }
          CloseStream();
          return;
        }
        if (parsed_response.immediate_response.has_value()) {
          if (processing_mode_.send_request_headers &&
              !request_headers_latch_.IsSet()) {
            request_headers_latch_.Set(std::move(parsed_response));
          } else if (processing_mode_.send_request_body &&
                     !request_body_pipe_.sender.IsClosed()) {
            request_body_pipe_.sender.Push(std::move(parsed_response))();
          } else if (processing_mode_.send_response_headers &&
                     !response_headers_latch_.IsSet()) {
            response_headers_latch_.Set(std::move(parsed_response));
          } else if (processing_mode_.send_response_body &&
                     !response_body_pipe_.sender.IsClosed()) {
            response_body_pipe_.sender.Push(std::move(parsed_response))();
          } else if (processing_mode_.send_response_trailers &&
                     !response_trailers_latch_.IsSet()) {
            response_trailers_latch_.Set(std::move(parsed_response));
          }
        } else if (parsed_response.request_headers.has_value()) {
          if (!processing_mode_.send_request_headers) {
            LOG(ERROR)
                << "Received request headers response but request headers "
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
            LOG(ERROR) << "Received response trailers response in a "
                          "Trailers-Only call";
            SetStreamErrorStatus(absl::InternalError(
                "Received response trailers response in a Trailers-Only call"));
            if (!response_trailers_latch_.IsSet()) {
              response_trailers_latch_.Set(
                  absl::InternalError("Received response trailers response in "
                                      "a Trailers-Only call"));
            }
            return;
          }
          if (processing_mode_.send_response_headers &&
              !response_headers_latch_.IsSet()) {
            LOG(ERROR) << "Received response trailers response before response "
                          "headers response";
            SetStreamErrorStatus(absl::InternalError(
                "Received response trailers response before "
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
            LOG(ERROR) << "Received request body response but request body is "
                          "disabled";
            SetStreamErrorStatus(absl::InternalError(
                "Received request body response but request body is disabled"));
            if (!request_body_pipe_.sender.IsClosed()) {
              request_body_pipe_.sender.MarkClosed();
            }
            return;
          }
          if (processing_mode_.send_request_headers &&
              !request_headers_latch_.IsSet()) {
            LOG(ERROR)
                << "Received request body response before request headers "
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
              // pass (half-close without sending message). Currently we fail
              // the RPC to avoid hangs on legacy transport adapters.
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
            LOG(ERROR)
                << "Received response body response but response body is "
                   "disabled";
            SetStreamErrorStatus(
                absl::InternalError("Received response body response but "
                                    "response body is disabled"));
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
    if (streaming_call_ != nullptr) {
      streaming_call_->StartRecvMessage();
    }
  }

  void OnStatusReceived(absl::Status status) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this << " status received: " << status;
    bool should_unref = false;
    bool already_closed = false;
    {
      MutexLock lock(&mu_);
      already_closed = stream_closed_.load(std::memory_order_acquire);
      stream_closed_.store(true, std::memory_order_release);
      // We manually acquired a strong reference ("active_stream") in the
      // constructor to keep the ExtProcCall alive during the fire-and-forget
      // phase of observability mode.
      // Because both the ConnectivityWatcher (detecting channel failure) and
      // the StreamEventHandler (detecting stream failure) can trigger this
      // callback concurrently or in any order, we must guarantee that we
      // release this manual reference exactly once. Double-unreffing would
      // drop the ref count to 0 prematurely, causing a crash when active
      // promises subsequently try to copy their RefCountedPtr.
      if (!unreffed_active_stream_) {
        unreffed_active_stream_ = true;
        should_unref = true;
      }
    }
    // ALWAYS complete latches on status received to avoid hangs,
    // even if CloseStream() was already called (e.g. due to immediate
    // response).
    // Determine if we should propagate the stream error and fail the call.
    // We fail the call if the stream failed, and EITHER:
    // 1. We are in fail-closed mode (failure_mode_allow_ is false).
    // 2. We are in fail-open mode, BUT we are not allowed to fail-open because
    //    there are outstanding body messages currently in-flight (which
    //    could result in partial mutations) and we are not in observability
    //    mode.
    const bool fail_open_allowed = IsStreamFailOpenAllowed();
    const bool should_propagate_error = !status.ok() && !fail_open_allowed;
    if (should_propagate_error) {
      {
        MutexLock lock(&mu_);
        stream_status_ = status;
      }
      CompleteAllLatchesAndPipes(status);
    } else {
      CompleteAllLatchesAndPipes(ExtProcResponse{});
    }
    if (!already_closed) {
      CloseStream();
    }
    if (should_unref) {
      // Release the active stream ref!
      Unref(DEBUG_LOCATION, "active_stream");
    }
  }

  void Orphaned() override { CloseStream(); }

  InterActivityLatch<absl::StatusOr<ExtProcResponse>> request_headers_latch_;
  InterActivityLatch<absl::StatusOr<ExtProcResponse>> response_headers_latch_;
  InterActivityLatch<absl::StatusOr<ExtProcResponse>> response_trailers_latch_;
  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 16> request_body_pipe_;
  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 16> response_body_pipe_;

  RefCountedPtr<ExtProcChannel> channel_;
  bool observability_mode_;
  bool failure_mode_allow_;
  bool disable_immediate_response_;
  ProcessingMode processing_mode_;
  Duration deferred_close_timeout_;
  OrphanablePtr<SerializedStreamingCall> streaming_call_;
  Mutex mu_;
  std::atomic<bool> stream_closed_{false};
  bool is_first_message_on_ext_proc_stream_ ABSL_GUARDED_BY(&mu_) = true;
  bool c2s_first_body_message_sent_ ABSL_GUARDED_BY(&mu_) = false;
  bool s2c_first_body_message_sent_ ABSL_GUARDED_BY(&mu_) = false;
  size_t outstanding_s2c_messages_ ABSL_GUARDED_BY(&mu_) = 0;
  bool s2c_writes_done_ ABSL_GUARDED_BY(&mu_) = false;
  bool is_trailers_only_ ABSL_GUARDED_BY(&mu_) = false;
  bool processor_sent_half_close_ ABSL_GUARDED_BY(&mu_) = false;
  size_t outstanding_c2s_messages_ ABSL_GUARDED_BY(&mu_) = 0;
  bool c2s_writes_done_ ABSL_GUARDED_BY(&mu_) = false;
  bool c2s_half_close_initiated_ ABSL_GUARDED_BY(&mu_) = false;
  InterActivityLatch<void> all_server_to_client_responses_received_latch_;
  RefCountedPtr<ConnectivityWatcher> connectivity_watcher_
      ABSL_GUARDED_BY(&mu_);
  absl::Status stream_status_ ABSL_GUARDED_BY(&mu_);
  bool unreffed_active_stream_ ABSL_GUARDED_BY(&mu_) = false;
};

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

auto SendServerInitialMetadataRequest(
    ExtProcFilter::ExtProcCall* ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata, bool condition,
    bool end_of_stream = false)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  const bool is_first_message = ext_proc_call->IsFirstMessageOnStream();
  return ext_proc_call->SendMessageLocked(
      /*condition=*/condition, [config = std::move(config), metadata,
                                is_first_message, end_of_stream]() {
        upb::Arena serialization_arena;
        return CreateExtProcRequest(
            serialization_arena.ptr(), ExtProcRequestType::kServerHeaders,
            metadata->get(), config->forwarding_allowed_headers,
            config->forwarding_disallowed_headers,
            /*attributes=*/nullptr,
            /*observability_mode=*/config->observability_mode, is_first_message,
            config->processing_mode.send_request_body,
            config->processing_mode.send_response_body,
            /*end_of_stream=*/end_of_stream);
      });
}

auto ReadServerInitialMetadataResponse(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  return Seq(
      TrySeq(
          ext_proc_call->response_headers_latch().Wait(),
          [config = std::move(config), metadata, handler, initiator,
           ext_proc_call](ExtProcResponse response) mutable -> absl::Status {
            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                << "ExtProc: ServerInitialMetadata response received. "
                   "has_headers: "
                << response.response_headers.has_value();
            if (response.immediate_response.has_value() &&
                !config->disable_immediate_response) {
              auto& immediate_response = *response.immediate_response;
              auto error_md = CancelledServerMetadataFromStatus(
                  static_cast<grpc_status_code>(immediate_response.status),
                  immediate_response.details);
              if (immediate_response.header_mutation.ok()) {
                const auto* rules = config->mutation_rules.has_value()
                                        ? &config->mutation_rules.value()
                                        : nullptr;
                auto status = ApplyHeaderMutations(
                    *immediate_response.header_mutation, rules, *error_md);
                if (!status.ok()) {
                  GRPC_TRACE_LOG(ext_proc_filter, ERROR)
                      << "Failed to apply immediate response header mutations: "
                      << status;
                }
              }
              handler.SpawnPushServerTrailingMetadata(std::move(error_md));
              ext_proc_call->CloseStream();
              return absl::OkStatus();
            }
            if (response.response_headers.has_value()) {
              const auto& response_headers = *response.response_headers;
              if (!response_headers.ok()) {
                return response_headers.status();
              }
              const auto* rules = config->mutation_rules.has_value()
                                      ? &config->mutation_rules.value()
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

auto ServerInitialMetadataNormalMode(
    CallHandler handler, CallInitiator initiator,
    ExtProcFilter::ExtProcCall* ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataNormalMode pulled. metadata: "
      << (*metadata)->DebugString();
  return TrySeq(
      SendServerInitialMetadataRequest(ext_proc_call, config, metadata,
                                       /*condition=*/true),
      [handler, initiator, ext_proc_call = ext_proc_call->Ref(),
       config = std::move(config), metadata]() mutable {
        return ReadServerInitialMetadataResponse(handler, initiator,
                                                 std::move(ext_proc_call),
                                                 std::move(config), metadata);
      });
}

auto ServerInitialMetadataObservabilityMode(
    CallHandler handler,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataObservabilityMode pulled. metadata: "
      << (*metadata)->DebugString();
  auto serialization_arena = std::make_shared<upb::Arena>();
  auto* upb_headers =
      envoy_config_core_v3_HeaderMap_new(serialization_arena->ptr());
  PopulateMetadataBatchToHeaderMap(**metadata,
                                   config->forwarding_allowed_headers,
                                   config->forwarding_disallowed_headers,
                                   serialization_arena->ptr(), upb_headers);

  absl::AnyInvocable<Poll<absl::Status>()> send_promise;
  {
    MutexLock lock(ext_proc_call->mu());
    const bool is_first_message = ext_proc_call->IsFirstMessageOnStream();
    send_promise = ext_proc_call->SendMessageLocked(
        /*condition=*/true, [config, upb_headers, is_first_message]() mutable {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Sending server initial metadata (observability "
                 "mode)";
          upb::Arena arena;
          return CreateExtProcRequest(
              arena.ptr(), ExtProcRequestType::kServerHeaders, upb_headers,
              config->forwarding_allowed_headers,
              config->forwarding_disallowed_headers,
              /*attributes=*/nullptr,
              /*observability_mode=*/true, is_first_message,
              config->processing_mode.send_request_body,
              config->processing_mode.send_response_body,
              /*end_of_stream=*/false);
        });
  }
  return Map(std::move(send_promise),
             [handler, metadata, ext_proc_call = std::move(ext_proc_call),
              config = std::move(config)](
                 absl::Status result) mutable -> absl::Status {
               const bool failure_mode_allow = config->failure_mode_allow;
               if (!result.ok() && !failure_mode_allow) {
                 return result;  // Fail-closed!
               }
               handler.SpawnPushServerInitialMetadata(std::move(*metadata));
               return absl::OkStatus();
             });
}

auto ServerInitialMetadataNonProcessingMode(
    CallHandler handler, ExtProcFilter::ExtProcCall* ext_proc_call,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataNonProcessingMode pulled. metadata: "
      << (*metadata)->DebugString();
  // If we are not sending headers, we must unblock the concurrent message
  // loop which might be waiting for this latch.
  if (!ext_proc_call->response_headers_latch().IsSet()) {
    ext_proc_call->response_headers_latch().Set(ExtProcResponse{});
  }
  // Push metadata to client immediately
  handler.SpawnPushServerInitialMetadata(std::move(*metadata));
  return absl::OkStatus();
}

absl::AnyInvocable<Poll<absl::Status>()> ServerInitialMetadata(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  const bool send_headers = config->processing_mode.send_response_headers &&
                            !ext_proc_call->IsStreamClosed();
  absl::AnyInvocable<Poll<absl::Status>()> promise;
  if (send_headers) {
    if (config->observability_mode) {
      auto p = ServerInitialMetadataObservabilityMode(
          handler, std::move(ext_proc_call), std::move(config),
          std::move(metadata));
      promise = [p = std::move(p)]() mutable { return p(); };
    } else {
      MutexLock lock(ext_proc_call->mu());
      auto p = ServerInitialMetadataNormalMode(
          handler, initiator, ext_proc_call.get(), std::move(config),
          std::move(metadata));
      promise = [p = std::move(p)]() mutable { return p(); };
    }
  } else {
    promise = [handler, ext_proc_call = std::move(ext_proc_call),
               metadata]() mutable {
      return ServerInitialMetadataNonProcessingMode(
          handler, ext_proc_call.get(), metadata);
    };
  }
  return promise;
}

auto SendServerMessageRequest(std::string message_bytes,
                              ExtProcFilter::ExtProcCall* ext_proc_call,
                              RefCountedPtr<const ExtProcFilter::Config> config,
                              bool send_to_ext_proc_stream)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  bool is_first_message = ext_proc_call->IsFirstMessageOnStream();
  return ext_proc_call->SendMessageLocked(
      send_to_ext_proc_stream,
      [ext_proc_call = ext_proc_call->Ref(), is_first_message,
       config = std::move(config),
       message_bytes = std::move(message_bytes)]() mutable {
        ext_proc_call->mu()->AssertHeld();
        ext_proc_call->MarkFirstServerBodyMessageSentLocked();
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerToClientMessages body message intercepted";
        upb::Arena arena;
        return CreateExtProcRequest(
            arena.ptr(), ExtProcRequestType::kServerMessage,
            upb_StringView_FromDataAndSize(message_bytes.data(),
                                           message_bytes.size()),
            {}, {},   // no headers
            nullptr,  // no attributes
            config->observability_mode, is_first_message,
            config->processing_mode.send_request_body,
            config->processing_mode.send_response_body,
            /*end_of_stream=*/false,
            /*end_of_stream_without_message=*/false);
      });
}

auto SendServerMessageHandleRequest(
    const MessageHandle& message, ExtProcFilter::ExtProcCall* ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    bool send_to_ext_proc_stream)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  std::string message_bytes;
  if (message != nullptr) {
    message_bytes = message->payload()->JoinIntoString();
  }
  return SendServerMessageRequest(std::move(message_bytes), ext_proc_call,
                                  std::move(config), send_to_ext_proc_stream);
}

auto ServerToClientMessagesObservabilityMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerToClientMessagesObservabilityMode started, "
      << "stream_closed=" << ext_proc_call->IsStreamClosed();
  return ForEach(MessagesFrom(initiator), [handler, ext_proc_call, config](
                                              MessageHandle message) mutable {
    const bool stream_closed = ext_proc_call->IsStreamClosed();
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: ServerToClientMessagesObservabilityMode "
           "processing message, stream_closed="
        << stream_closed;
    std::string message_bytes;
    const bool send = !stream_closed;
    if (send && message != nullptr) {
      message_bytes = message->payload()->JoinIntoString();
    }
    absl::AnyInvocable<Poll<absl::Status>()> send_promise;
    {
      MutexLock lock(ext_proc_call->mu());
      send_promise = SendServerMessageRequest(
          std::move(message_bytes), ext_proc_call.get(), config, send);
    }
    return Map(std::move(send_promise),
               [handler, message = std::move(message),
                failure_mode_allow = config->failure_mode_allow](
                   absl::Status status) mutable -> absl::Status {
                 if (!status.ok() && !failure_mode_allow) {
                   return status;
                 }
                 handler.SpawnPushMessage(std::move(message));
                 return absl::OkStatus();
               });
  });
}

auto ServerToClientMessagesNonProcessingMode(CallHandler handler,
                                             CallInitiator initiator) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerToClientMessagesNonProcessingMode started";
  return ForEach(MessagesFrom(initiator),
                 [handler](MessageHandle message) mutable {
                   GRPC_TRACE_LOG(ext_proc_filter, INFO)
                       << "ExtProc: ServerToClientMessagesNonProcessingMode "
                          "forwarding message";
                   handler.SpawnPushMessage(std::move(message));
                   return absl::OkStatus();
                 });
}

absl::AnyInvocable<Poll<absl::Status>()> SendServerToClientMessagesRequest(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: SendServerToClientMessagesRequest started";
  const bool send_response_headers =
      config->processing_mode.send_response_headers;
  auto promise = Seq(
      // Phase 1: Wait for server initial metadata to be pushed (latch set).
      If(
          send_response_headers,
          [ext_proc_call]() {
            return Map(
                ext_proc_call->response_headers_latch().Wait(),
                [](auto&&...) -> absl::Status { return absl::OkStatus(); });
          },
          []() -> absl::Status { return absl::OkStatus(); }),
      // Phase 2: Start polling messages from the backend.
      [handler, initiator, ext_proc_call, config]() mutable {
        return ForEach(
            MessagesFrom(initiator),
            [handler, ext_proc_call, config](MessageHandle message) mutable {
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << "ExtProc: ServerToClient S2C Write Loop pulled message, "
                     "processing";
              const bool send_to_ext_proc_stream =
                  config->processing_mode.send_response_body &&
                  !ext_proc_call->IsStreamClosed();
              return If(
                  send_to_ext_proc_stream,
                  [ext_proc_call, config,
                   message = std::move(message)]() mutable {
                    ext_proc_call->IncrementOutstandingServerToClientMessages();
                    absl::AnyInvocable<Poll<absl::Status>()> send_promise;
                    {
                      MutexLock lock(ext_proc_call->mu());
                      send_promise = SendServerMessageHandleRequest(
                          message, ext_proc_call.get(), config,
                          /*send_to_ext_proc_stream=*/true);
                    }
                    return Map(
                        std::move(send_promise),
                        [message = std::move(message)](
                            absl::Status status) mutable { return status; });
                  },
                  [handler, ext_proc_call, config,
                   message = std::move(message)]() mutable {
                    if (config->processing_mode.send_response_body &&
                        ext_proc_call->IsStreamClosed()) {
                      if (!ext_proc_call->IsServerFailOpenAllowed()) {
                        return ext_proc_call->GetStreamErrorStatus();
                      }
                    }
                    GRPC_TRACE_LOG(ext_proc_filter, INFO)
                        << "ExtProc: ServerToClient S2C Write Loop "
                           "bypassing ext_proc";
                    handler.SpawnPushMessage(std::move(message));
                    return absl::OkStatus();
                  });
            });
      },
      // Phase 3: Mark writes done when polling finishes.
      [ext_proc_call]() {
        ext_proc_call->MarkServerToClientWritesDone();
        return absl::OkStatus();
      });
  return [promise = std::move(promise)]() mutable { return promise(); };
}

absl::AnyInvocable<Poll<absl::Status>()> ReadServerToClientMessagesResponse(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ReadServerToClientMessagesResponse started";
  // Read from response_body_pipe_, construct message, push to
  // handler.
  auto read_loop = ForEach(
      std::move(ext_proc_call->response_body_pipe().receiver),
      [handler, initiator, ext_proc_call,
       config](absl::StatusOr<ExtProcResponse> response) mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerToClient S2C Read Loop got response";
        if (!response.ok()) {
          return response.status();
        }
        auto& ext_proc_response = *response;
        if (ext_proc_response.immediate_response.has_value() &&
            !config->disable_immediate_response) {
          auto& immediate_response = *ext_proc_response.immediate_response;
          auto error_md = CancelledServerMetadataFromStatus(
              static_cast<grpc_status_code>(immediate_response.status),
              immediate_response.details);
          if (immediate_response.header_mutation.ok()) {
            const auto* rules = config->mutation_rules.has_value()
                                    ? &config->mutation_rules.value()
                                    : nullptr;
            auto status = ApplyHeaderMutations(
                *immediate_response.header_mutation, rules, *error_md);
            if (!status.ok()) {
              GRPC_TRACE_LOG(ext_proc_filter, ERROR)
                  << "Failed to apply immediate response header mutations: "
                  << status;
            }
          }
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Immediate response error_md: "
              << error_md->DebugString();
          handler.SpawnPushServerTrailingMetadata(std::move(error_md));
          ext_proc_call->CloseStream();
          return absl::OkStatus();
        }
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
  // Wait for all messages to be processed (writes done AND
  // outstanding is 0).
  auto close_pipe_promise =
      Map(ext_proc_call->all_server_to_client_responses_received_latch().Wait(),
          [ext_proc_call](Empty) {
            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                << "ExtProc: ServerToClient S2C Coordinator: all responses "
                   "received, closing pipe";
            if (!ext_proc_call->response_body_pipe().sender.IsClosed()) {
              ext_proc_call->response_body_pipe().sender.MarkClosed();
            }
            return absl::OkStatus();
          });
  // Combine them concurrently
  auto promise =
      Map(TryJoin<absl::StatusOr>(std::move(close_pipe_promise),
                                  std::move(read_loop)),
          [](auto result) -> absl::Status { return result.status(); });
  return [promise = std::move(promise)]() mutable { return promise(); };
}

absl::AnyInvocable<Poll<absl::Status>()> ServerToClientMessagesNormalMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerToClientMessagesNormalMode started";
  auto promise = Map(
      TryJoin<absl::StatusOr>(
          SendServerToClientMessagesRequest(handler, initiator, ext_proc_call,
                                            config),
          ReadServerToClientMessagesResponse(
              handler, initiator, std::move(ext_proc_call), std::move(config))),
      [](auto result) -> absl::Status { return result.status(); });
  return [promise = std::move(promise)]() mutable { return promise(); };
}

absl::AnyInvocable<Poll<absl::Status>()> ServerToClientMessages(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config) {
  const bool send_body = config->processing_mode.send_response_body &&
                         !ext_proc_call->IsStreamClosed();
  absl::AnyInvocable<Poll<absl::Status>()> promise;
  if (send_body) {
    if (config->observability_mode) {
      auto p = ServerToClientMessagesObservabilityMode(
          handler, initiator, std::move(ext_proc_call), std::move(config));
      promise = [p = std::move(p)]() mutable { return p(); };
    } else {
      promise = ServerToClientMessagesNormalMode(
          handler, initiator, std::move(ext_proc_call), std::move(config));
    }
  } else {
    auto p = ServerToClientMessagesNonProcessingMode(handler, initiator);
    promise = [p = std::move(p)]() mutable { return p(); };
  }
  return promise;
}

auto SendServerTrailingMetadataRequest(
    ExtProcFilter::ExtProcCall* ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  const bool is_first_message = ext_proc_call->IsFirstMessageOnStream();
  const bool is_stream_closed = ext_proc_call->IsStreamClosed();
  return ext_proc_call->SendMessageLocked(
      !is_stream_closed,
      [ext_proc_call = ext_proc_call->Ref(), config = std::move(config),
       metadata, is_first_message]() {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Sending server trailing metadata";
        upb::Arena serialization_arena;
        return CreateExtProcRequest(
            serialization_arena.ptr(), ExtProcRequestType::kServerTrailers,
            metadata->get(), config->forwarding_allowed_headers,
            config->forwarding_disallowed_headers,
            /*attributes=*/nullptr,
            /*observability_mode=*/false, is_first_message,
            config->processing_mode.send_request_body,
            config->processing_mode.send_response_body,
            /*end_of_stream=*/false,
            /*end_of_stream_without_message=*/false);
      });
}

absl::AnyInvocable<Poll<absl::Status>()> ReadServerTrailingMetadataResponse(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ReadServerTrailingMetadataResponse started";
  auto promise = Seq(
      TrySeq(
          ext_proc_call->response_trailers_latch().Wait(),
          [metadata, ext_proc_call, config,
           initiator](ExtProcResponse response) mutable -> absl::Status {
            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                << "ExtProc: ServerTrailingMetadata response received. "
                   "OK: true, has_trailers: "
                << response.response_trailers.has_value();
            if (response.immediate_response.has_value() &&
                !config->disable_immediate_response) {
              auto& immediate_response = *response.immediate_response;
              auto error_md = CancelledServerMetadataFromStatus(
                  static_cast<grpc_status_code>(immediate_response.status),
                  immediate_response.details);
              if (immediate_response.header_mutation.ok()) {
                const auto* rules = config->mutation_rules.has_value()
                                        ? &config->mutation_rules.value()
                                        : nullptr;
                auto status = ApplyHeaderMutations(
                    *immediate_response.header_mutation, rules, *error_md);
                if (!status.ok()) {
                  GRPC_TRACE_LOG(ext_proc_filter, ERROR)
                      << "Failed to apply immediate response header mutations: "
                      << status;
                }
              }
              *metadata = std::move(error_md);
              initiator.SpawnCancel();
              ext_proc_call->CloseStream();
              return absl::OkStatus();
            }
            // Rule 3: Processing and non-observability mode
            if (config->processing_mode.send_response_body &&
                !config->observability_mode &&
                !ext_proc_call->response_body_pipe().sender.IsClosed()) {
              ext_proc_call->response_body_pipe().sender.MarkClosed();
            }
            if (response.response_trailers.has_value()) {
              const auto& response_trailers = *response.response_trailers;
              if (!response_trailers.ok()) {
                return response_trailers.status();
              }
              const auto* rules = config->mutation_rules.has_value()
                                      ? &config->mutation_rules.value()
                                      : nullptr;
              auto status =
                  ApplyHeaderMutations(*response_trailers, rules, **metadata);
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
      [handler, metadata, ext_proc_call](absl::Status result) mutable {
        if (!result.ok()) {
          *metadata = CancelledServerMetadataFromStatus(result);
        }
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerTrailingMetadata pushing metadata immediately";
        handler.SpawnPushServerTrailingMetadata(std::move(*metadata));
        return absl::OkStatus();
      });
  return [promise = std::move(promise)]() mutable { return promise(); };
}

absl::AnyInvocable<Poll<absl::Status>()> ServerTrailingMetadataNormalMode(
    CallHandler handler, CallInitiator initiator,
    ExtProcFilter::ExtProcCall* ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataNormalMode pulled. metadata: "
      << (*metadata)->DebugString();
  auto promise =
      TrySeq(SendServerTrailingMetadataRequest(ext_proc_call, config, metadata),
             [handler, initiator, ext_proc_call = ext_proc_call->Ref(),
              config = std::move(config), metadata]() mutable {
               return ReadServerTrailingMetadataResponse(
                   handler, initiator, std::move(ext_proc_call),
                   std::move(config), std::move(metadata));
             });
  return [promise = std::move(promise)]() mutable { return promise(); };
}

absl::AnyInvocable<Poll<absl::Status>()>
ServerTrailingMetadataObservabilityMode(
    CallHandler handler, ExtProcFilter::ExtProcCall* ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataObservabilityMode pulled. metadata: "
      << (*metadata)->DebugString();
  const bool is_first_message = ext_proc_call->IsFirstMessageOnStream();
  const bool is_stream_closed = ext_proc_call->IsStreamClosed();
  auto send_promise = ext_proc_call->SendMessageLocked(
      !is_stream_closed, [ext_proc_call = ext_proc_call->Ref(), config = config,
                          metadata, is_first_message]() {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Sending server trailing metadata (observability mode)";
        upb::Arena serialization_arena;
        return CreateExtProcRequest(
            serialization_arena.ptr(), ExtProcRequestType::kServerTrailers,
            metadata->get(), config->forwarding_allowed_headers,
            config->forwarding_disallowed_headers,
            /*attributes=*/nullptr,
            /*observability_mode=*/true, is_first_message,
            config->processing_mode.send_request_body,
            config->processing_mode.send_response_body,
            /*end_of_stream=*/false,
            /*end_of_stream_without_message=*/false);
      });
  // TODO(rishesh): Limitation: In observability mode, if the backend server
  // sends trailers immediately after the response body, the RPC may complete
  // and the filter may be destroyed (closing the ext_proc stream) before
  // the ext_proc server has finished processing the response body or before
  // any stream errors (like RESOURCE_EXHAUSTED) can travel back over the
  // network to OnStatusReceived. This means fail-closed
  // (failure_mode_allow=false) is best-effort for server-to-client body
  // messages and trailers.
  auto promise = Seq(
      std::move(send_promise),
      [handler, metadata, ext_proc_call = ext_proc_call->Ref(),
       config = std::move(config)](absl::Status result) mutable {
        if (config->processing_mode.send_response_body &&
            !config->observability_mode &&
            !ext_proc_call->response_body_pipe().sender.IsClosed()) {
          ext_proc_call->response_body_pipe().sender.MarkClosed();
        }
        if (!result.ok()) {
          *metadata = CancelledServerMetadataFromStatus(result);
        }
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerTrailingMetadata pushing metadata immediately";
        handler.SpawnPushServerTrailingMetadata(std::move(*metadata));
        return absl::OkStatus();
      });
  return [promise = std::move(promise)]() mutable { return promise(); };
}

absl::AnyInvocable<Poll<absl::Status>()>
ServerTrailingMetadataNonProcessingMode(
    CallHandler handler, ExtProcFilter::ExtProcCall* ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataNonProcessingMode pulled. metadata: "
      << (*metadata)->DebugString();
  if (config->processing_mode.send_response_body &&
      !config->observability_mode &&
      !ext_proc_call->response_body_pipe().sender.IsClosed()) {
    ext_proc_call->response_body_pipe().sender.MarkClosed();
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadata pushing metadata immediately";
  handler.SpawnPushServerTrailingMetadata(std::move(*metadata));
  return []() -> Poll<absl::Status> { return absl::OkStatus(); };
}

absl::AnyInvocable<Poll<absl::Status>()>
ServerTrailingMetadataTrailersOnlyObservabilityMode(
    CallHandler handler, ExtProcFilter::ExtProcCall* ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata, bool send_headers)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataTrailersOnlyObservabilityMode started";
  std::shared_ptr<upb::Arena> serialization_arena;
  envoy_config_core_v3_HeaderMap* upb_headers = nullptr;
  if (send_headers) {
    serialization_arena = std::make_shared<upb::Arena>();
    upb_headers =
        envoy_config_core_v3_HeaderMap_new(serialization_arena->ptr());
    PopulateMetadataBatchToHeaderMap(**metadata,
                                     config->forwarding_allowed_headers,
                                     config->forwarding_disallowed_headers,
                                     serialization_arena->ptr(), upb_headers);
  }
  const bool is_first_message = ext_proc_call->IsFirstMessageOnStream();
  auto send_promise = ext_proc_call->SendMessageLocked(
      send_headers,
      [ext_proc_call = ext_proc_call->Ref(), config, serialization_arena,
       upb_headers, is_first_message]() mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Sending server trailers-only as headers "
               "(observability mode)";
        upb::Arena arena;
        return CreateExtProcRequest(
            arena.ptr(), ExtProcRequestType::kServerHeaders, upb_headers,
            config->forwarding_allowed_headers,
            config->forwarding_disallowed_headers,
            /*attributes=*/nullptr,
            /*observability_mode=*/true, is_first_message,
            config->processing_mode.send_request_body,
            config->processing_mode.send_response_body,
            /*end_of_stream=*/true);
      });
  auto promise =
      Seq(std::move(send_promise),
          [handler, metadata](absl::Status result) mutable {
            handler.SpawnPushServerTrailingMetadata(std::move(*metadata));
            return result;
          });
  return [promise = std::move(promise)]() mutable { return promise(); };
}

absl::AnyInvocable<Poll<absl::Status>()>
ServerTrailingMetadataTrailersOnlyNormalMode(
    CallHandler handler, CallInitiator initiator,
    ExtProcFilter::ExtProcCall* ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataTrailersOnlyNormalMode started";
  auto promise = TrySeq(
      SendServerInitialMetadataRequest(ext_proc_call, config, metadata,
                                       /*condition=*/true,
                                       /*end_of_stream=*/true),
      [handler, initiator, ext_proc_call = ext_proc_call->Ref(),
       config = std::move(config), metadata]() mutable {
        const auto* rules = config->mutation_rules.has_value()
                                ? &config->mutation_rules.value()
                                : nullptr;
        return Seq(
            TrySeq(ext_proc_call->response_headers_latch().Wait(),
                   [rules, metadata, config, ext_proc_call, initiator](
                       ExtProcResponse response) mutable -> absl::Status {
                     if (response.immediate_response.has_value() &&
                         !config->disable_immediate_response) {
                       auto& immediate_response = *response.immediate_response;
                       auto error_md = CancelledServerMetadataFromStatus(
                           static_cast<grpc_status_code>(
                               immediate_response.status),
                           immediate_response.details);
                       if (immediate_response.header_mutation.ok()) {
                         auto status = ApplyHeaderMutations(
                             *immediate_response.header_mutation, rules,
                             *error_md);
                         if (!status.ok()) {
                           GRPC_TRACE_LOG(ext_proc_filter, ERROR)
                               << "Failed to apply immediate response header "
                                  "mutations: "
                               << status;
                         }
                       }
                       *metadata = std::move(error_md);
                       ext_proc_call->CloseStream();
                       return absl::OkStatus();
                     }
                     if (response.response_headers.has_value()) {
                       const auto& response_headers =
                           *response.response_headers;
                       if (!response_headers.ok()) {
                         return response_headers.status();
                       }
                       return ApplyHeaderMutations(*response_headers, rules,
                                                   **metadata);
                     }
                     return absl::OkStatus();
                   }),
            [handler, metadata](absl::Status result) mutable {
              if (!result.ok()) {
                *metadata = CancelledServerMetadataFromStatus(result);
              }
              handler.SpawnPushServerTrailingMetadata(std::move(*metadata));
              return result;
            });
      });
  return [promise = std::move(promise)]() mutable { return promise(); };
}

absl::AnyInvocable<Poll<absl::Status>()> ServerTrailingMetadataTrailersOnly(
    CallHandler handler, CallInitiator initiator,
    ExtProcFilter::ExtProcCall* ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  const bool send_headers = config->processing_mode.send_response_headers &&
                            !ext_proc_call->IsStreamClosed();
  if (config->observability_mode) {
    return ServerTrailingMetadataTrailersOnlyObservabilityMode(
        handler, ext_proc_call, std::move(config), std::move(metadata),
        send_headers);
  } else if (send_headers) {
    return ServerTrailingMetadataTrailersOnlyNormalMode(
        handler, initiator, ext_proc_call, std::move(config),
        std::move(metadata));
  } else {
    return ServerTrailingMetadataNonProcessingMode(
        handler, ext_proc_call, std::move(config), std::move(metadata));
  }
}

absl::AnyInvocable<Poll<absl::Status>()> ServerTrailingMetadataNormal(
    CallHandler handler, CallInitiator initiator,
    ExtProcFilter::ExtProcCall* ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  const bool send_trailers_to_ext_proc_stream =
      config->processing_mode.send_response_trailers && IsStatusOk(*metadata) &&
      !ext_proc_call->IsStreamClosed();
  if (send_trailers_to_ext_proc_stream) {
    if (config->observability_mode) {
      return ServerTrailingMetadataObservabilityMode(
          handler, ext_proc_call, std::move(config), std::move(metadata));
    } else {
      return ServerTrailingMetadataNormalMode(handler, initiator, ext_proc_call,
                                              std::move(config),
                                              std::move(metadata));
    }
  } else {
    return ServerTrailingMetadataNonProcessingMode(
        handler, ext_proc_call, std::move(config), std::move(metadata));
  }
}

absl::AnyInvocable<Poll<absl::Status>()> ServerTrailingMetadata(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    std::shared_ptr<ServerMetadataHandle> metadata) {
  const bool is_trailers_only =
      (*metadata)->get(GrpcTrailersOnly()).value_or(false);
  if (is_trailers_only) {
    ext_proc_call->SetIsTrailersOnly();
  }
  absl::AnyInvocable<Poll<absl::Status>()> promise;
  {
    MutexLock lock(ext_proc_call->mu());
    if (is_trailers_only) {
      promise = ServerTrailingMetadataTrailersOnly(
          handler, initiator, ext_proc_call.get(), std::move(config),
          std::move(metadata));
    } else {
      promise =
          ServerTrailingMetadataNormal(handler, initiator, ext_proc_call.get(),
                                       std::move(config), std::move(metadata));
    }
  }
  return promise;
}

auto SendClientMessageRequest(const MessageHandle& message,
                              ExtProcFilter::ExtProcCall* ext_proc_call,
                              RefCountedPtr<const ExtProcFilter::Config> config,
                              bool end_of_stream,
                              bool end_of_stream_without_message,
                              bool send_to_ext_proc_stream,
                              ::google_protobuf_Struct* attributes)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  if (end_of_stream_without_message && send_to_ext_proc_stream) {
    ext_proc_call->MarkClientHalfCloseInitiatedLocked();
  }
  Message* msg_ptr = message.get();
  const bool is_first_message = ext_proc_call->IsFirstMessageOnStream();
  return ext_proc_call->SendMessageLocked(
      send_to_ext_proc_stream,
      [ext_proc_call = ext_proc_call->Ref(), config, msg_ptr, end_of_stream,
       end_of_stream_without_message, attributes, is_first_message]() {
        ext_proc_call->mu()->AssertHeld();
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ClientToServerMessages body message intercepted:\n"
            << (msg_ptr != nullptr ? msg_ptr->DebugString() : "<nullptr>");
        upb::Arena arena;
        std::string message_bytes;
        if (msg_ptr != nullptr) {
          message_bytes = msg_ptr->payload()->JoinIntoString();
        }
        ext_proc_call->MarkFirstClientBodyMessageSentLocked();
        return CreateExtProcRequest(
            arena.ptr(), ExtProcRequestType::kClientMessage,
            upb_StringView_FromDataAndSize(message_bytes.data(),
                                           message_bytes.size()),
            /*allowed_headers=*/{}, /*disallowed_headers=*/{}, attributes,
            config->observability_mode, is_first_message,
            config->processing_mode.send_request_body,
            config->processing_mode.send_response_body, end_of_stream,
            end_of_stream_without_message);
      });
}

auto SendClientMessageRequest(std::string message_bytes,
                              ExtProcFilter::ExtProcCall* ext_proc_call,
                              RefCountedPtr<const ExtProcFilter::Config> config,
                              bool end_of_stream,
                              bool end_of_stream_without_message,
                              bool send_to_ext_proc_stream,
                              ::google_protobuf_Struct* attributes)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ext_proc_call->mu()) {
  const bool is_first_message = ext_proc_call->IsFirstMessageOnStream();
  return ext_proc_call->SendMessageLocked(
      send_to_ext_proc_stream,
      [ext_proc_call = ext_proc_call->Ref(), config,
       message_bytes = std::move(message_bytes), end_of_stream,
       end_of_stream_without_message, attributes, is_first_message]() mutable {
        ext_proc_call->mu()->AssertHeld();
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ClientToServerMessages body message intercepted "
               "(observability mode)";
        upb::Arena arena;
        ext_proc_call->MarkFirstClientBodyMessageSentLocked();
        return CreateExtProcRequest(
            arena.ptr(), ExtProcRequestType::kClientMessage,
            upb_StringView_FromDataAndSize(message_bytes.data(),
                                           message_bytes.size()),
            /*allowed_headers=*/{}, /*disallowed_headers=*/{}, attributes,
            config->observability_mode, is_first_message,
            config->processing_mode.send_request_body,
            config->processing_mode.send_response_body, end_of_stream,
            end_of_stream_without_message);
      });
}

absl::AnyInvocable<Poll<absl::Status>()>
ClientToServerMessagesMaybeObservabilityMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    bool send_to_ext_proc_stream, ::google_protobuf_Struct* attributes) {
  return TrySeq(
      ForEach(MessagesFrom(handler),
              [config, initiator, ext_proc_call, send_to_ext_proc_stream,
               attributes](MessageHandle message) mutable {
                return If(
                    ext_proc_call->IsProcessorSentHalfClose(),
                    []() -> absl::Status {
                      return absl::InternalError(
                          "Client sends closed by external processor");
                    },
                    [config, initiator, ext_proc_call, send_to_ext_proc_stream,
                     attributes, message = std::move(message)]() mutable {
                      std::string message_bytes;
                      const bool send = send_to_ext_proc_stream &&
                                        !ext_proc_call->IsStreamClosed();
                      if (send && message != nullptr) {
                        message_bytes = message->payload()->JoinIntoString();
                      }
                      absl::AnyInvocable<Poll<absl::Status>()> promise;
                      {
                        MutexLock lock(ext_proc_call->mu());
                        promise = SendClientMessageRequest(
                            std::move(message_bytes), ext_proc_call.get(),
                            config, /*end_of_stream=*/false,
                            /*end_of_stream_without_message=*/false, send,
                            attributes);
                      }
                      return Map(
                          std::move(promise),
                          [initiator, message = std::move(message),
                           failure_mode_allow = config->failure_mode_allow](
                              absl::Status status) mutable -> absl::Status {
                            if (!status.ok() && !failure_mode_allow) {
                              return status;
                            }
                            initiator.SpawnPushMessage(std::move(message));
                            return absl::OkStatus();
                          });
                    });
              }),
      [config, initiator, ext_proc_call, send_to_ext_proc_stream,
       attributes]() mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ClientToServerMessages finished sends";
        std::string message_bytes;
        const bool send =
            send_to_ext_proc_stream && !ext_proc_call->IsStreamClosed();
        absl::AnyInvocable<Poll<absl::Status>()> promise;
        {
          MutexLock lock(ext_proc_call->mu());
          promise = SendClientMessageRequest(
              std::move(message_bytes), ext_proc_call.get(), config,
              /*end_of_stream=*/false,
              /*end_of_stream_without_message=*/true, send, attributes);
        }
        return Map(std::move(promise),
                   [initiator, failure_mode_allow = config->failure_mode_allow](
                       absl::Status status) mutable {
                     if (!status.ok() && !failure_mode_allow) {
                       return status;
                     }
                     initiator.SpawnFinishSends();
                     return absl::OkStatus();
                   });
      });
}

absl::AnyInvocable<Poll<absl::Status>()> ClientToServerMessagesNormalMode(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    ::google_protobuf_Struct* attributes) {
  auto client_to_sidestream = TrySeq(
      ForEach(
          MessagesFrom(handler),
          [config, initiator, ext_proc_call,
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
                [config, initiator, ext_proc_call, attributes,
                 message = std::move(message)]() mutable {
                  const bool send_to_ext_proc_stream =
                      !ext_proc_call->IsStreamClosed();
                  if (send_to_ext_proc_stream) {
                    ext_proc_call->IncrementOutstandingClientToServerMessages();
                  }
                  absl::AnyInvocable<Poll<absl::Status>()> send_promise;
                  {
                    MutexLock lock(ext_proc_call->mu());
                    send_promise = SendClientMessageRequest(
                        message, ext_proc_call.get(), config,
                        /*end_of_stream=*/false,
                        /*end_of_stream_without_message=*/false,
                        send_to_ext_proc_stream, attributes);
                  }
                  return Map(
                      std::move(send_promise),
                      [ext_proc_call, initiator, config,
                       message = std::move(message)](
                          absl::Status status) mutable -> absl::Status {
                        if (!status.ok() || ext_proc_call->IsStreamClosed()) {
                          if (ext_proc_call->IsClientFailOpenAllowed()) {
                            initiator.SpawnPushMessage(std::move(message));
                            return absl::OkStatus();
                          } else {
                            return ext_proc_call->IsStreamClosed()
                                       ? ext_proc_call->GetStreamErrorStatus()
                                       : status;
                          }
                        }
                        return absl::OkStatus();
                      });
                });
          }),
      [config, initiator, ext_proc_call, attributes]() mutable {
        return If(
            ext_proc_call->IsProcessorSentHalfClose(),
            [ext_proc_call]() {
              ext_proc_call->MarkClientSendsDone();
              return absl::OkStatus();
            },
            [config, initiator, ext_proc_call, attributes]() mutable {
              MessageHandle null_msg = nullptr;
              const bool send_to_ext_proc_stream =
                  !ext_proc_call->IsStreamClosed();
              if (send_to_ext_proc_stream) {
                ext_proc_call->IncrementOutstandingClientToServerMessages();
              }
              absl::AnyInvocable<Poll<absl::Status>()> send_promise;
              {
                MutexLock lock(ext_proc_call->mu());
                send_promise = SendClientMessageRequest(
                    null_msg, ext_proc_call.get(), config,
                    /*end_of_stream=*/false,
                    /*end_of_stream_without_message=*/true,
                    send_to_ext_proc_stream, attributes);
              }
              return Map(
                  std::move(send_promise),
                  [ext_proc_call, initiator,
                   config](absl::Status status) mutable -> absl::Status {
                    if (!status.ok() || ext_proc_call->IsStreamClosed()) {
                      if (ext_proc_call->IsClientFailOpenAllowed()) {
                        initiator.SpawnFinishSends();
                        ext_proc_call->MarkClientSendsDone();
                        return absl::OkStatus();
                      } else {
                        return ext_proc_call->IsStreamClosed()
                                   ? ext_proc_call->GetStreamErrorStatus()
                                   : status;
                      }
                    }
                    ext_proc_call->MarkClientSendsDone();
                    return absl::OkStatus();
                  });
            });
      });
  auto sidestream_to_server = Seq(
      ForEach(
          std::move(ext_proc_call->request_body_pipe().receiver),
          [handler, initiator, ext_proc_call,
           config](absl::StatusOr<ExtProcResponse> result) mutable {
            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                << "ExtProc: sidestream_to_server ForEach got item, ok: "
                << result.ok();
            if (!result.ok()) {
              if (ext_proc_call->IsClientFailOpenAllowed()) {
                return absl::OkStatus();
              }
              return result.status();
            }
            auto& ext_proc_response = *result;
            if (ext_proc_response.immediate_response.has_value() &&
                !config->disable_immediate_response) {
              auto& immediate_response = *ext_proc_response.immediate_response;
              auto error_md = CancelledServerMetadataFromStatus(
                  static_cast<grpc_status_code>(immediate_response.status),
                  immediate_response.details);
              if (immediate_response.header_mutation.ok()) {
                const auto* rules = config->mutation_rules.has_value()
                                        ? &config->mutation_rules.value()
                                        : nullptr;
                auto status = ApplyHeaderMutations(
                    *immediate_response.header_mutation, rules, *error_md);
                if (!status.ok()) {
                  GRPC_TRACE_LOG(ext_proc_filter, ERROR)
                      << "Failed to apply immediate response header mutations: "
                      << status;
                }
              }
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << "ExtProc: Immediate response error_md: "
                  << error_md->DebugString();
              handler.SpawnPushServerTrailingMetadata(std::move(error_md));
              ext_proc_call->CloseStream();
              return absl::OkStatus();
            }
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

absl::AnyInvocable<Poll<absl::Status>()> ClientToServerMessages(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcFilter::ExtProcCall> ext_proc_call,
    RefCountedPtr<const ExtProcFilter::Config> config,
    ::google_protobuf_Struct* attributes) {
  return If(
      config->processing_mode.send_request_body,
      [handler, initiator, ext_proc_call, config, attributes]() mutable {
        return If(
            config->observability_mode,
            [handler, initiator, ext_proc_call, config, attributes]() mutable {
              return ClientToServerMessagesMaybeObservabilityMode(
                  handler, initiator, std::move(ext_proc_call),
                  std::move(config), /*send_to_ext_proc_stream=*/true,
                  attributes);
            },
            [handler, initiator, ext_proc_call, config, attributes]() mutable {
              return ClientToServerMessagesNormalMode(
                  handler, initiator, std::move(ext_proc_call),
                  std::move(config), attributes);
            });
      },
      [handler, initiator, ext_proc_call, config, attributes]() mutable {
        return ClientToServerMessagesMaybeObservabilityMode(
            handler, initiator, std::move(ext_proc_call), std::move(config),
            /*send_to_ext_proc_stream=*/false, attributes);
      });
}

}  // namespace

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

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ProcessCallObservabilityMode(
    CallHandler handler, RefCountedPtr<ExtProcCall> ext_proc_call) {
  auto promise = TrySeq(
      handler.PullClientInitialMetadata(),
      [self = RefAsSubclass<ExtProcFilter>(), handler,
       ext_proc_call](ClientMetadataHandle metadata) mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Client initial metadata received "
               "(observability):\n"
            << metadata->DebugString();
        const bool send_headers =
            self->config_->processing_mode.send_request_headers;
        std::shared_ptr<upb::Arena> serialization_arena;
        envoy_config_core_v3_HeaderMap* upb_headers = nullptr;
        ::google_protobuf_Struct* header_attributes = nullptr;
        ::google_protobuf_Struct* attributes = nullptr;
        upb::Arena* attributes_arena = nullptr;
        if (send_headers) {
          serialization_arena = std::make_shared<upb::Arena>();
          upb_headers =
              envoy_config_core_v3_HeaderMap_new(serialization_arena->ptr());
          PopulateMetadataBatchToHeaderMap(
              *metadata, self->config_->forwarding_allowed_headers,
              self->config_->forwarding_disallowed_headers,
              serialization_arena->ptr(), upb_headers);
          header_attributes = ParseAttributes(
              serialization_arena->ptr(), self->config_->request_attributes,
              *metadata, self->default_authority_.as_string_view());
        } else if (self->config_->processing_mode.send_request_body &&
                   !self->config_->request_attributes.empty()) {
          attributes_arena = handler.arena()->New<upb::Arena>();
          attributes = ParseAttributes(
              attributes_arena->ptr(), self->config_->request_attributes,
              *metadata, self->default_authority_.as_string_view());
        }
        absl::AnyInvocable<Poll<absl::Status>()> client_headers_write_promise;
        {
          MutexLock lock(ext_proc_call->mu());
          const bool is_first_message = ext_proc_call->IsFirstMessageOnStream();
          client_headers_write_promise = ext_proc_call->SendMessageLocked(
              send_headers,
              [self, ext_proc_call = ext_proc_call->Ref(), serialization_arena,
               upb_headers, header_attributes, is_first_message]() mutable {
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << "ExtProc: Sending client initial metadata "
                       "(observability mode)";
                upb::Arena arena;
                return CreateExtProcRequest(
                    arena.ptr(), ExtProcRequestType::kClientHeaders,
                    upb_headers, self->config()->forwarding_allowed_headers,
                    self->config()->forwarding_disallowed_headers,
                    header_attributes, self->config()->observability_mode,
                    is_first_message,
                    self->config()->processing_mode.send_request_body,
                    self->config()->processing_mode.send_response_body);
              });
        }
        auto write_promise =
            Map(std::move(client_headers_write_promise),
                [failure_mode_allow = self->config()->failure_mode_allow](
                    absl::Status status) -> absl::Status {
                  if (!status.ok() && failure_mode_allow) {
                    GRPC_TRACE_LOG(ext_proc_filter, INFO)
                        << "ExtProc: Initial metadata write failed, but "
                           "failure_mode_allow=true. Proceeding with fail-open "
                           "behavior. Error: "
                        << status;
                    return absl::OkStatus();
                  }
                  return status;
                });
        return TrySeq(
            std::move(write_promise),
            [self, handler, metadata = std::move(metadata),
             ext_proc_call = std::move(ext_proc_call), attributes]() mutable {
              CallInitiator initiator = self->MakeChildCall(
                  std::move(metadata), handler.arena()->Ref());
              handler.AddChildCall(initiator);
              // Spawn server_to_client task
              initiator.SpawnInfallible(
                  "server_to_client",
                  [self, handler, initiator,
                   ext_proc_call = ext_proc_call->Ref()]() mutable {
                    GRPC_TRACE_LOG(ext_proc_filter, INFO)
                        << "ExtProc: server_to_client task started";
                    return initiator.CancelIfFails(self->ProcessServerToClient(
                        handler, initiator, std::move(ext_proc_call)));
                  });
              return ClientToServerMessages(handler, initiator,
                                            std::move(ext_proc_call),
                                            self->config(), attributes);
            });
      });
  return [promise = std::move(promise)]() mutable { return promise(); };
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ProcessCallNormalMode(
    CallHandler handler, RefCountedPtr<ExtProcCall> ext_proc_call) {
  auto promise = TrySeq(
      handler.PullClientInitialMetadata(),
      [self = RefAsSubclass<ExtProcFilter>(), handler,
       ext_proc_call](ClientMetadataHandle metadata) mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Client initial metadata received:\n"
            << metadata->DebugString();
        auto shared_metadata =
            std::make_shared<ClientMetadataHandle>(std::move(metadata));
        const bool send_headers =
            self->config_->processing_mode.send_request_headers;
        absl::AnyInvocable<Poll<absl::Status>()> send_promise;
        {
          MutexLock lock(ext_proc_call->mu());
          const bool is_first_message = ext_proc_call->IsFirstMessageOnStream();
          send_promise = ext_proc_call->SendMessageLocked(
              send_headers, [self, ext_proc_call = ext_proc_call->Ref(),
                             shared_metadata, is_first_message]() {
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << "ExtProc: Sending client initial metadata";
                upb::Arena serialization_arena;
                return CreateExtProcRequest(
                    serialization_arena.ptr(),
                    ExtProcRequestType::kClientHeaders, shared_metadata->get(),
                    self->config()->forwarding_allowed_headers,
                    self->config()->forwarding_disallowed_headers,
                    ParseAttributes(serialization_arena.ptr(),
                                    self->config()->request_attributes,
                                    **shared_metadata,
                                    self->default_authority_.as_string_view()),
                    self->config()->observability_mode, is_first_message,
                    self->config()->processing_mode.send_request_body,
                    self->config()->processing_mode.send_response_body);
              });
        }
        return Seq(std::move(send_promise),
                   [shared_metadata](absl::Status) mutable
                       -> absl::StatusOr<ClientMetadataHandle> {
                     return std::move(*shared_metadata);
                   });
      },
      [self = RefAsSubclass<ExtProcFilter>(), handler,
       ext_proc_call](ClientMetadataHandle metadata) mutable {
        // Type-erase the conditional headers wait using standard C++ if-else
        // to completely prevent any complex template type mismatches.
        absl::AnyInvocable<Poll<absl::StatusOr<ExtProcResponse>>()>
            headers_promise;
        if (self->config()->processing_mode.send_request_headers &&
            !self->config()->observability_mode) {
          headers_promise = ext_proc_call->request_headers_latch().Wait();
        } else {
          headers_promise = []() -> Poll<absl::StatusOr<ExtProcResponse>> {
            return ExtProcResponse{};
          };
        }
        return TrySeq(
            std::move(headers_promise),
            [self,
             metadata = std::move(metadata)](ExtProcResponse response) mutable
                -> absl::StatusOr<std::variant<
                    ClientMetadataHandle, ExtProcResponse::ImmediateResponse>> {
              if (response.immediate_response.has_value() &&
                  !self->config()->disable_immediate_response) {
                return response.immediate_response.value();
              }
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
             ext_proc_call](std::variant<ClientMetadataHandle,
                                         ExtProcResponse::ImmediateResponse>
                                result) mutable
                -> absl::AnyInvocable<Poll<absl::Status>()> {
              if (std::holds_alternative<ExtProcResponse::ImmediateResponse>(
                      result)) {
                auto& immediate_response =
                    std::get<ExtProcResponse::ImmediateResponse>(result);
                auto error_md = CancelledServerMetadataFromStatus(
                    static_cast<grpc_status_code>(immediate_response.status),
                    immediate_response.details);
                if (immediate_response.header_mutation.ok()) {
                  const auto* rules =
                      self->config()->mutation_rules.has_value()
                          ? &self->config()->mutation_rules.value()
                          : nullptr;
                  auto status = ApplyHeaderMutations(
                      *immediate_response.header_mutation, rules, *error_md);
                  if (!status.ok()) {
                    GRPC_TRACE_LOG(ext_proc_filter, ERROR)
                        << "Failed to apply immediate response header "
                           "mutations: "
                        << status;
                  }
                }
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << "ExtProc: Immediate response error_md: "
                    << error_md->DebugString();
                handler.SpawnPushServerTrailingMetadata(std::move(error_md));
                ext_proc_call->CloseStream();
                return absl::AnyInvocable<Poll<absl::Status>()>(
                    []() -> Poll<absl::Status> { return absl::OkStatus(); });
              }
              auto metadata = std::move(std::get<ClientMetadataHandle>(result));
              ::google_protobuf_Struct* attributes = nullptr;
              if (!self->config()->processing_mode.send_request_headers &&
                  self->config()->processing_mode.send_request_body &&
                  !self->config()->request_attributes.empty()) {
                auto* attributes_arena = handler.arena()->New<upb::Arena>();
                attributes = ParseAttributes(
                    attributes_arena->ptr(), self->config()->request_attributes,
                    *metadata, self->default_authority_.as_string_view());
              }
              CallInitiator initiator = self->MakeChildCall(
                  std::move(metadata), handler.arena()->Ref());
              handler.AddChildCall(initiator);
              initiator.SpawnInfallible(
                  "server_to_client",
                  [self, handler, initiator, ext_proc_call]() mutable {
                    GRPC_TRACE_LOG(ext_proc_filter, INFO)
                        << "ExtProc: server_to_client task started";
                    return initiator.CancelIfFails(self->ProcessServerToClient(
                        handler, initiator, ext_proc_call));
                  });
              return ClientToServerMessages(
                  handler, initiator, std::move(ext_proc_call), self->config(),
                  !self->config()->processing_mode.send_request_headers
                      ? attributes
                      : nullptr);
            });
      });
  return [promise = std::move(promise)]() mutable { return promise(); };
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ProcessServerToClient(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  auto response_pipeline = Seq(
      // Phase 1: Headers and Messages (short-circuiting!)
      TrySeq(
          // Step A: Pull Server Initial Metadata.
          initiator.PullServerInitialMetadata(),
          [handler, initiator, ext_proc_call,
           config = config_](std::optional<ServerMetadataHandle> md) mutable {
            const bool has_md = md.has_value();
            return If(
                has_md,
                [handler, initiator, ext_proc_call, config,
                 md = std::move(md)]() mutable {
                  auto shared_md =
                      std::make_shared<ServerMetadataHandle>(std::move(*md));
                  return TrySeq(
                      // 1. Intercept and push Server Initial Metadata.
                      ServerInitialMetadata(handler, initiator, ext_proc_call,
                                            config, shared_md),
                      // 2. Intercept and push Server-to-Client Messages.
                      [handler, initiator, ext_proc_call, config]() mutable {
                        return ServerToClientMessages(handler, initiator,
                                                      ext_proc_call, config);
                      });
                },
                []() {
                  // Trailers-Only: Bypasses both headers and messages!
                  return absl::OkStatus();
                });
          }),
      // Handle Phase 1 result and conditionally run Phase 2
      [handler, initiator, ext_proc_call,
       config = config_](absl::Status phase1_result) mutable {
        if (!phase1_result.ok()) {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: ProcessServerToClient Phase 1 failed: "
              << phase1_result;
          // Push the Phase 1 error to the parent call as trailing metadata
          // first
          auto error_md = CancelledServerMetadataFromStatus(phase1_result);
          handler.SpawnPushServerTrailingMetadata(std::move(error_md));
          // Then cancel the child call to prevent leaks/hangs
          initiator.Cancel();
          // Close the ext_proc stream
          ext_proc_call->CloseStream();
          // Return the error to complete the pipeline
          return absl::AnyInvocable<Poll<absl::Status>()>(
              [phase1_result]() -> Poll<absl::Status> {
                return phase1_result;
              });
        }
        // Phase 1 succeeded! Proceed to Phase 2: Pull, intercept, and push
        // Server Trailing Metadata.
        return absl::AnyInvocable<Poll<absl::Status>()>(Seq(
            initiator.PullServerTrailingMetadata(),
            [handler, initiator, ext_proc_call,
             config](ServerMetadataHandle md) mutable
                -> absl::AnyInvocable<Poll<absl::Status>()> {
              if (ext_proc_call->IsStreamClosed() &&
                  !ext_proc_call->GetStreamErrorStatus().ok()) {
                auto error = ext_proc_call->GetStreamErrorStatus();
                auto error_md = CancelledServerMetadataFromStatus(error);
                handler.SpawnPushServerTrailingMetadata(std::move(error_md));
                return [error]() -> Poll<absl::Status> { return error; };
              }
              auto shared_md =
                  std::make_shared<ServerMetadataHandle>(std::move(md));
              return ServerTrailingMetadata(handler, initiator, ext_proc_call,
                                            config, shared_md);
            }));
      });
  return [response_pipeline = std::move(response_pipeline)]() mutable {
    return response_pipeline();
  };
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
        self->channel(), self->config()->observability_mode,
        self->config()->failure_mode_allow,
        self->config()->disable_immediate_response,
        self->config()->processing_mode,
        self->config()->deferred_close_timeout);
    return If(
        self->config()->observability_mode,
        [self, handler, ext_proc_call]() mutable {
          return self->ProcessCallObservabilityMode(handler,
                                                    std::move(ext_proc_call));
        },
        [self, handler, ext_proc_call]() mutable {
          return self->ProcessCallNormalMode(handler, std::move(ext_proc_call));
        });
  });
}

}  // namespace grpc_core