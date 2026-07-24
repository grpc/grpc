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

#include "src/core/filter/ext_proc/ext_proc_filter.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>

#include <memory>
#include <string>
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/filter/ext_proc/ext_proc_messages.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/debug/trace_impl.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/inter_activity_pipe.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/prioritized_race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/streaming_call_promise_wrapper.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {

namespace {

using OnSuccessCallback =
    absl::AnyInvocable<absl::AnyInvocable<Poll<absl::Status>()>(
        RefCountedPtr<ExtProcFilter::ExtProcCall>, ServerMetadataHandle,
        Timestamp)>;

const auto kMetricClientExtProcClientHeadersDuration =
    GlobalInstrumentsRegistry::RegisterDoubleHistogram(
        "grpc.client_ext_proc.client_headers_duration",
        "Time between when the ext_proc filter sees the client's headers and "
        "when it allows those headers to continue on to the next filter.",
        "s", false)
        .Labels(kMetricLabelTarget)
        .Build();

const auto kMetricClientExtProcClientHalfCloseDuration =
    GlobalInstrumentsRegistry::RegisterDoubleHistogram(
        "grpc.client_ext_proc.client_half_close_duration",
        "Time between when the ext_proc filter sees the client's half-close "
        "and when it allows that half-close to continue on to the next "
        "filter.",
        "s", false)
        .Labels(kMetricLabelTarget)
        .Build();

const auto kMetricClientExtProcServerHeadersDuration =
    GlobalInstrumentsRegistry::RegisterDoubleHistogram(
        "grpc.client_ext_proc.server_headers_duration",
        "Time between when the ext_proc filter sees the server's headers and "
        "when it allows those headers to continue on to the next filter.",
        "s", false)
        .Labels(kMetricLabelTarget)
        .Build();

const auto kMetricClientExtProcServerTrailersDuration =
    GlobalInstrumentsRegistry::RegisterDoubleHistogram(
        "grpc.client_ext_proc.server_trailers_duration",
        "Time between when the ext_proc filter sees the server's trailers and "
        "when it allows those trailers to continue on to the next filter.",
        "s", false)
        .Labels(kMetricLabelTarget)
        .Build();

bool IsProcessingEnabled(
    const std::optional<ExtProcFilter::ProcessingMode>& processing_mode) {
  if (!processing_mode.has_value()) return false;
  return processing_mode->send_request_headers ||
         processing_mode->send_response_headers ||
         processing_mode->send_response_trailers ||
         processing_mode->send_request_body ||
         processing_mode->send_response_body;
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
// ExtProcFilter::Config
//

std::string ExtProcFilter::Config::ToString() const {
  std::string result = "{";
  bool is_first = true;
  Match(
      channel_info,
      [&](const GrpcXdsServerTarget& target) {
        StrAppend(result, "grpc_service=");
        StrAppend(result, target.Key());
        is_first = false;
      },
      [&](const RefCountedPtr<ExtProcChannel>& channel) {
        if (channel != nullptr) {
          StrAppend(result, "ext_proc_channel=");
          StrAppend(result, channel->server().Key());
          is_first = false;
        }
      });
  if (failure_mode_allow.value_or(false)) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "failure_mode_allow=true");
    is_first = false;
  }
  if (processing_mode.has_value()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "processing_mode=");
    StrAppend(result, processing_mode->ToString());
    is_first = false;
  }
  if (!request_attributes.empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "request_attributes=[");
    StrAppend(result, absl::StrJoin(request_attributes, ", "));
    StrAppend(result, "]");
    is_first = false;
  }
  if (!response_attributes.empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "response_attributes=[");
    StrAppend(result, absl::StrJoin(response_attributes, ", "));
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

bool ExtProcFilter::Config::Equals(const FilterConfig& other) const {
  const auto& o = DownCast<const Config&>(other);
  return channel_info == o.channel_info &&
         failure_mode_allow == o.failure_mode_allow &&
         processing_mode == o.processing_mode &&
         request_attributes == o.request_attributes &&
         response_attributes == o.response_attributes &&
         mutation_rules == o.mutation_rules &&
         forwarding_allowed_headers == o.forwarding_allowed_headers &&
         forwarding_disallowed_headers == o.forwarding_disallowed_headers &&
         disable_immediate_response == o.disable_immediate_response &&
         observability_mode == o.observability_mode &&
         deferred_close_timeout == o.deferred_close_timeout;
}

//
// ExtProcFilter::ExtProcChannel
//

ExtProcFilter::ExtProcChannel::ExtProcChannel(
    std::shared_ptr<const XdsBootstrap::XdsServerTarget> server,
    RefCountedPtr<XdsTransportFactory::XdsTransport> transport)
    : server_(std::move(server)), transport_(std::move(transport)) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "creating channel " << this << " for server " << server_->server_uri();
}

ExtProcFilter::ExtProcChannel::~ExtProcChannel() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "destroying ext_proc channel " << this << " for server "
      << server_->server_uri();
}

//
// ExtProcFilter::ExtProcCall
//

class ExtProcFilter::ExtProcCall final : public DualRefCounted<ExtProcCall> {
 public:
  friend class ClientInitialMetadataProcessor;
  friend class ClientToServerMessageProcessor;
  friend class ServerInitialMetadataProcessor;
  friend class ServerToExtProcInitialMetadataProcessor;
  friend class ExtProcToClientInitialMetadataProcessor;
  friend class ServerToClientMessageProcessor;
  friend class ServerToExtProcMessageProcessor;
  friend class ExtProcToClientMessageProcessor;
  friend class ServerTrailingMetadataProcessor;
  friend class ServerToExtProcTrailingMetadataProcessor;
  friend class ExtProcToClientTrailingMetadataProcessor;

  ExtProcCall(RefCountedPtr<ExtProcFilter> ext_proc_filter,
              RefCountedPtr<XdsTransportFactory::XdsTransport> transport,
              CallHandler handler);

  ~ExtProcCall() override;

  absl::AnyInvocable<Poll<absl::Status>()> Call();

 private:
  class StreamEventHandler final
      : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
   public:
    explicit StreamEventHandler(WeakRefCountedPtr<ExtProcCall> call)
        : call_(std::move(call)) {}

    void OnRequestSent(bool ok) override { call_->OnRequestSent(ok); }

    void OnRecvMessage(absl::string_view payload) override {
      call_->OnRecvMessage(payload);
    }

    void OnStatusReceived(absl::Status status) override {
      call_->OnStatusReceived(std::move(status));
    }

   private:
    WeakRefCountedPtr<ExtProcCall> call_;
  };

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

  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 1>& request_body_pipe() {
    return request_body_pipe_;
  }

  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 1>& response_body_pipe() {
    return response_body_pipe_;
  }

  RefCountedPtr<const Config> config() const {
    return ext_proc_filter_->config_;
  }

  bool IsFirstMessageOnStream() {
    bool is_first = is_first_message_on_ext_proc_stream_;
    is_first_message_on_ext_proc_stream_ = false;
    return is_first;
  }

  bool IsFailOpenAllowed() const {
    const bool allow =
        ext_proc_filter_->config_->failure_mode_allow.value_or(false);
    if (ext_proc_filter_->config_->observability_mode) return allow;
    return allow && !first_body_message_sent_;
  }

  void MarkClientHalfCloseInitiated() { c2s_half_close_initiated_ = true; }

  bool IsStreamClosed() const {
    MutexLock lock(&mu_);
    return stream_status_value_.has_value();
  }

  bool ext_proc_stream_half_closed() const {
    return ext_proc_stream_half_closed_;
  }

  bool drain_requested() const { return drain_requested_; }

  void SetDrainRequested() { drain_requested_ = true; }

  bool IsStreamClosedCleanly() const {
    MutexLock lock(&mu_);
    return stream_status_value_.has_value() && stream_status_value_->ok();
  }

  void IncrementOutstandingServerToClientMessages() {
    outstanding_s2c_messages_++;
  }

  bool HasOutstandingServerToClientMessages() const {
    return outstanding_s2c_messages_ > 0;
  }

  bool DecrementOutstandingServerToClientMessages(bool* should_close) {
    if (outstanding_s2c_messages_ == 0) {
      return false;
    }
    outstanding_s2c_messages_--;
    if (s2c_writes_done_ && outstanding_s2c_messages_ == 0) {
      *should_close = true;
    }
    return true;
  }

  void IncrementOutstandingClientToServerMessages() {
    outstanding_c2s_messages_++;
  }

  bool DecrementOutstandingClientToServerMessages() {
    if (outstanding_c2s_messages_ == 0) {
      return false;
    }
    outstanding_c2s_messages_--;
    return true;
  }

  void SetServerToClientWritesDone() {
    s2c_writes_done_ = true;
    if (outstanding_s2c_messages_ == 0) {
      response_body_pipe_.sender.MarkClosed();
    }
  }

  void SetIsTrailersOnly() { is_trailers_only_ = true; }

  bool is_trailers_only() const { return is_trailers_only_; }

  void SetServerTrailersSent() { server_trailers_sent_ = true; }

  bool server_trailers_sent() const { return server_trailers_sent_; }

  void SetExtProcSetEos() { ext_proc_set_eos_ = true; }

  bool ext_proc_set_eos() const { return ext_proc_set_eos_; }

  void SetClientSendsDone() { c2s_writes_done_ = true; }

  bool c2s_write_done() const { return c2s_writes_done_; }

  absl::Status GetStreamStatus() const {
    MutexLock lock(&mu_);
    return stream_status_value_.value_or(absl::OkStatus());
  }

  auto WaitForStreamStatus() {
    return [this]() -> Poll<absl::Status> {
      {
        MutexLock lock(&mu_);
        if (stream_status_value_.has_value()) {
          return *stream_status_value_;
        }
      }
      auto poll = stream_status_.Wait()();
      if (poll.ready()) {
        MutexLock lock(&mu_);
        return stream_status_value_.value_or(absl::OkStatus());
      }
      return Pending{};
    };
  }

  void SetFirstBodyMessageSent() { first_body_message_sent_ = true; }

  void SetStreamError(absl::Status status) {
    SetStreamStatus(status);
    CompleteAllLatchesAndPipes(status);
    CloseStream();
  }

  void CloseStream() {
    RefCountedPtr<StreamingCallPromiseWrapper> streaming_call;
    {
      MutexLock lock(&mu_);
      if (!stream_status_value_.has_value()) {
        stream_status_value_ = absl::OkStatus();
        stream_status_.Set();
      }
      streaming_call = std::move(streaming_call_);
    }
    ext_proc_send_in_flight_ = false;
    ext_proc_send_waker_.Wakeup();
    streaming_call.reset();
    request_body_pipe_.sender.MarkClosed();
    response_body_pipe_.sender.MarkClosed();
  }

  void Orphaned() override { CloseStream(); }

  // Member functions
  void OnRequestSent(bool /*ok*/) {}
  void OnRecvMessage(absl::string_view payload);
  void OnStatusReceived(absl::Status status);
  void CompleteAllLatchesAndPipes(absl::StatusOr<ExtProcResponse> response);

  void SetStreamStatus(absl::Status status) {
    MutexLock lock(&mu_);
    if (!stream_status_value_.has_value()) {
      stream_status_value_ = status;
      stream_status_.Set();
    }
  }

  // Sends a message to the external processor side-stream.
  // Coordinates client-side and server-side message sources so that only one
  // send is in-flight on streaming_call_ at a time, using a single Waker
  // without any queue or vector allocations.
  absl::AnyInvocable<Poll<absl::Status>()> SendMessage(
      absl::AnyInvocable<absl::StatusOr<std::string>()> payload_generator);

  friend class ClientInitialMetadataProcessor;
  friend class ClientToServerMessageProcessor;
  friend class ServerInitialMetadataProcessor;
  friend class ServerToClientMessageProcessor;
  friend class ServerTrailingMetadataProcessor;

  // Intercepts and processes client-to-server messages.
  absl::AnyInvocable<Poll<absl::Status>()> ClientToServerMessages(
      ::google_protobuf_Struct* attributes);

  // Intercepts and processes server initial metadata.
  absl::AnyInvocable<Poll<absl::Status>()> ServerInitialMetadata();

  // Intercepts and processes server-to-client messages.
  absl::AnyInvocable<Poll<absl::Status>()> ServerToClientMessages();

  // Intercepts and processes server trailing metadata.
  absl::AnyInvocable<Poll<absl::Status>()> ServerTrailingMetadata();

  // Handles the response path (Server to Client).
  // This function sets up a pipeline to process server initial metadata,
  // response messages, and server trailing metadata, potentially intercepting
  // and mutating them via the ext_proc server.
  //
  // It also watches for ext_proc stream errors and aborts the call if a failure
  // occurs and fail-open is not allowed.
  absl::AnyInvocable<Poll<absl::Status>()> ServerToClientCall();

  // Data members
  InterActivityLatch<absl::StatusOr<ExtProcResponse>> request_headers_latch_;
  InterActivityLatch<absl::StatusOr<ExtProcResponse>> response_headers_latch_;
  InterActivityLatch<absl::StatusOr<ExtProcResponse>> response_trailers_latch_;
  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 1> request_body_pipe_;
  InterActivityPipe<absl::StatusOr<ExtProcResponse>, 1> response_body_pipe_;
  RefCountedPtr<ExtProcFilter> ext_proc_filter_;
  // Indicates whether a stream drain operation has been requested by the
  // filter.
  bool drain_requested_ = false;
  // True if no messages have been sent on the external processor stream yet.
  // Used to include overall processing_mode in the initial stream header
  // request.
  bool is_first_message_on_ext_proc_stream_ = true;
  // Tracks whether the first body message has been sent on the stream,
  // used for fail-open determination.
  bool first_body_message_sent_ = false;
  // TODO(rishesh): Need to remove this once PH2 work is done.
  // Number of messages sent to ext_proc that are awaiting response processing
  // in S2C and C2S directions respectively.
  size_t outstanding_s2c_messages_ = 0;
  size_t outstanding_c2s_messages_ = 0;
  // Stream state flags tracking directional write completion, half-close,
  // trailers-only RPC mode, and server trailers transmission.
  bool c2s_writes_done_ = false;
  bool s2c_writes_done_ = false;
  bool c2s_half_close_initiated_ = false;
  bool is_trailers_only_ = false;
  bool server_trailers_sent_ = false;
  // Set by external processor server when it requests end of stream (EOS).
  bool ext_proc_set_eos_ = false;
  // Indicates that the external processor stream has been half closed.
  bool ext_proc_stream_half_closed_ = false;
  InterActivityLatch<void> stream_status_;
  std::optional<absl::Status> stream_status_value_ ABSL_GUARDED_BY(mu_);
  bool ext_proc_send_in_flight_ = false;
  Waker ext_proc_send_waker_;

  mutable Mutex mu_;

  RefCountedPtr<XdsTransportFactory::XdsTransport> transport_;
  CallHandler handler_;
  CallInitiator initiator_;
  RefCountedPtr<StreamingCallPromiseWrapper> streaming_call_;
};

ExtProcFilter::ExtProcCall::ExtProcCall(
    RefCountedPtr<ExtProcFilter> ext_proc_filter,
    RefCountedPtr<XdsTransportFactory::XdsTransport> transport,
    CallHandler handler)
    : ext_proc_filter_(std::move(ext_proc_filter)),
      transport_(std::move(transport)),
      handler_(handler) {
  const char* method = "/envoy.service.ext_proc.v3.ExternalProcessor/Process";
  streaming_call_ = MakeRefCounted<StreamingCallPromiseWrapper>(
      *transport_, method, std::make_unique<StreamEventHandler>(WeakRef()),
      /*wait_for_ready=*/false);
  streaming_call_->StartRecvMessage();
}

ExtProcFilter::ExtProcCall::~ExtProcCall() {
  if (ext_proc_filter_->config_->deferred_close_timeout != Duration::Zero() &&
      ext_proc_filter_->config_->observability_mode) {
    if (ext_proc_filter_->event_engine_ != nullptr) {
      ext_proc_filter_->event_engine_->RunAfter(
          ext_proc_filter_->config_->deferred_close_timeout,
          [call = std::move(streaming_call_),
           transport = std::move(transport_)]() mutable {
            call.reset();
            transport.reset();
          });
    }
  } else {
    streaming_call_.reset();
  }
}

void ExtProcFilter::ExtProcCall::CompleteAllLatchesAndPipes(
    absl::StatusOr<ExtProcResponse> response) {
  const auto& processing_mode = *ext_proc_filter_->config_->processing_mode;
  if (processing_mode.send_request_headers && !request_headers_latch_.IsSet()) {
    request_headers_latch_.Set(response);
  }
  if (processing_mode.send_response_headers &&
      !response_headers_latch_.IsSet()) {
    response_headers_latch_.Set(response);
  }
  if (processing_mode.send_response_trailers &&
      !response_trailers_latch_.IsSet()) {
    response_trailers_latch_.Set(response);
  }
  if (processing_mode.send_request_body) {
    if (!response.ok()) {
      request_body_pipe_.sender.Push(response.status())();
    }
    request_body_pipe_.sender.MarkClosed();
  }
  if (processing_mode.send_response_body) {
    if (!response.ok()) {
      response_body_pipe_.sender.Push(response.status())();
    }
    response_body_pipe_.sender.MarkClosed();
  }
}

void ExtProcFilter::ExtProcCall::OnRecvMessage(absl::string_view payload) {
  // In observability mode, we only log the message and ignore it.
  // We must continue reading the stream to keep it alive.
  if (ext_proc_filter_->config_->observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this
        << " message received in observability mode (ignored), size="
        << payload.size();
    MutexLock lock(&mu_);
    if (streaming_call_ != nullptr) {
      streaming_call_->StartRecvMessage();
    }
    return;
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " message received, size=" << payload.size();
  // Parse the response from the external processor.
  auto parsed_response = ExtProcResponse::Parse(payload);
  if (!parsed_response.ok()) {
    // If parsing fails, we either fail the stream or close it cleanly
    // (fail-open) depending on configuration.
    if (!IsFailOpenAllowed()) {
      SetStreamError(parsed_response.status());
    } else {
      CompleteAllLatchesAndPipes(ExtProcResponse{});
      CloseStream();
    }
    return;
  }
  // If the server requests a drain, we half-close the stream to signal
  // we are done sending requests.
  if ((*parsed_response).request_drain) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this << " received request_drain=true";
    SetDrainRequested();
    ext_proc_stream_half_closed_ = true;
    {
      MutexLock lock(&mu_);
      if (streaming_call_ != nullptr) {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProcCall " << this << " sending half-close";
        streaming_call_->SendHalfClose();
      }
    }
  }
  // Dispatch the parsed response to the appropriate latch based on the
  // response type.
  const auto& processing_mode = *ext_proc_filter_->config_->processing_mode;
  Match((*parsed_response).response,
        [&](const ExtProcResponse::ImmediateResponse&) {
          if (ext_proc_filter_->config_->disable_immediate_response ||
              !server_trailers_sent()) {
            auto error = absl::InternalError(
                ext_proc_filter_->config_->disable_immediate_response
                    ? "unhandled immediate response due to config disabled it"
                    : "Immediate response received but trailers not sent to "
                      "ext_proc");
            SetStreamError(error);
            return;
          }
          if (processing_mode.send_response_trailers &&
              !response_trailers_latch_.IsSet()) {
            response_trailers_latch_.Set(std::move(*parsed_response));
          }
        },
        [&](const ExtProcResponse::RequestHeaders&) {
          if (!processing_mode.send_request_headers) {
            SetStreamError(
                absl::InternalError("Received request headers response but "
                                    "request headers are disabled"));
            return;
          }
          if (processing_mode.send_request_headers &&
              !request_headers_latch_.IsSet()) {
            request_headers_latch_.Set(std::move(*parsed_response));
          }
        },
        [&](const ExtProcResponse::ResponseHeaders&) {
          if (!processing_mode.send_response_headers) {
            SetStreamError(
                absl::InternalError("Received response headers response but "
                                    "response headers are disabled"));
            return;
          }
          if (processing_mode.send_response_headers &&
              !response_headers_latch_.IsSet()) {
            response_headers_latch_.Set(std::move(*parsed_response));
          }
        },
        [&](const ExtProcResponse::ResponseTrailers&) {
          if (!processing_mode.send_response_trailers) {
            SetStreamError(
                absl::InternalError("Received response trailers response but "
                                    "response trailers are disabled"));
            return;
          }
          if (is_trailers_only()) {
            SetStreamError(absl::InternalError(
                "Received response trailers response in a Trailers-Only call"));
            return;
          }
          if (processing_mode.send_response_headers &&
              !response_headers_latch_.IsSet()) {
            SetStreamError(absl::InternalError(
                "Received response trailers response before "
                "response headers response"));
            return;
          }
          const bool s2c_body_outstanding =
              processing_mode.send_response_body &&
              outstanding_s2c_messages_ > 0;
          if (s2c_body_outstanding) {
            SetStreamError(absl::InternalError(
                "Received response trailers response before all "
                "outstanding response body responses were received"));
            return;
          }
          if (processing_mode.send_response_trailers &&
              !response_trailers_latch_.IsSet()) {
            response_trailers_latch_.Set(std::move(*parsed_response));
          }
        },
        [&](const ExtProcResponse::RequestBody& request_body) {
          if (!processing_mode.send_request_body) {
            SetStreamError(absl::InternalError(
                "Received request body response but request body is disabled"));
            return;
          }
          if (processing_mode.send_request_headers &&
              !request_headers_latch_.IsSet()) {
            SetStreamError(
                absl::InternalError("Received request body response before "
                                    "request headers response"));
            return;
          }
          if (!DecrementOutstandingClientToServerMessages()) {
            SetStreamError(absl::InternalError(
                "Received unexpected request body response from "
                "external processor"));
            return;
          }
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Parsed request body response, eos: "
              << request_body.mutation.end_of_stream << ", eos_without_msg: "
              << request_body.mutation.end_of_stream_without_message;
          if (request_body.mutation.end_of_stream_without_message) {
            if (!c2s_write_done()) {
              SetStreamError(absl::InternalError(
                  "Client sends closed by external processor"));
              return;
            }
            SetExtProcSetEos();
            request_body_pipe_.sender.MarkClosed();
            return;
          }
          bool end_of_stream = request_body.mutation.end_of_stream;
          request_body_pipe_.sender.Push(std::move(*parsed_response))();
          if (end_of_stream) {
            SetExtProcSetEos();
            request_body_pipe_.sender.MarkClosed();
          }
        },
        [&](const ExtProcResponse::ResponseBody&) {
          if (!processing_mode.send_response_body) {
            SetStreamError(
                absl::InternalError("Received response body response but "
                                    "response body is disabled"));
            return;
          }
          if (is_trailers_only()) {
            SetStreamError(absl::InternalError(
                "Received response body response in a Trailers-Only call"));
            return;
          }
          if (processing_mode.send_response_headers &&
              !response_headers_latch_.IsSet()) {
            SetStreamError(
                absl::InternalError("Received response body response before "
                                    "response headers response"));
            return;
          }
          if (processing_mode.send_response_trailers &&
              response_trailers_latch_.IsSet()) {
            SetStreamError(absl::InternalError(
                "Received response body response after response "
                "trailers response"));
            return;
          }
          if (!HasOutstandingServerToClientMessages()) {
            SetStreamError(absl::InternalError(
                "Received unexpected response body response from "
                "external processor"));
            return;
          }
          // Push the message to the pipe BEFORE decrementing the outstanding
          // count. This prevents a race where SetServerToClientWritesDone()
          // runs concurrently, sees the outstanding count is 0, and closes the
          // pipe before we can push this last message.
          response_body_pipe_.sender.Push(std::move(*parsed_response))();
          bool should_close = false;
          DecrementOutstandingServerToClientMessages(&should_close);
          if (should_close) {
            // If writes are done and this was the last outstanding message, we
            // can close the pipe early to signal completion to the read loop.
            response_body_pipe_.sender.MarkClosed();
          }
        },
        [](std::monostate) {});
  MutexLock lock(&mu_);
  if (streaming_call_ != nullptr) {
    streaming_call_->StartRecvMessage();
  }
}

void ExtProcFilter::ExtProcCall::OnStatusReceived(absl::Status status) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " status received: " << status;
  const bool has_outstanding_messages =
      outstanding_c2s_messages_ > 0 || outstanding_s2c_messages_ > 0;
  const bool must_drain =
      !ext_proc_filter_->config_->observability_mode &&
      (ext_proc_filter_->config_->processing_mode->send_request_body ||
       ext_proc_filter_->config_->processing_mode->send_response_body);
  const bool drain_requested = drain_requested_;
  if (status.ok()) {
    if (must_drain && !drain_requested) {
      status = absl::InternalError("Stream closed cleanly without drain");
    } else if (has_outstanding_messages &&
               !ext_proc_filter_->config_->observability_mode) {
      status = absl::InternalError(
          "Stream closed cleanly with outstanding messages");
    }
  }
  const bool should_propagate_error = !status.ok() && !IsFailOpenAllowed();
  bool already_closed = false;
  {
    MutexLock lock(&mu_);
    already_closed = stream_status_value_.has_value();
    if (!already_closed) {
      stream_status_value_ = status;
      stream_status_.Set();
    }
  }
  // Always complete latches on status received to avoid hangs.
  if (should_propagate_error) {
    CompleteAllLatchesAndPipes(status);
  } else {
    CompleteAllLatchesAndPipes(ExtProcResponse{});
  }
  if (!already_closed) {
    CloseStream();
  }
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::SendMessage(
    absl::AnyInvocable<absl::StatusOr<std::string>()> payload_generator) {
  return [this, ext_proc_call = Ref(),
          payload_generator = std::move(payload_generator),
          send_promise = absl::AnyInvocable<Poll<absl::Status>()>()]() mutable
             -> Poll<absl::Status> {
    {
      MutexLock lock(&mu_);
      if (stream_status_value_.has_value() || streaming_call_ == nullptr) {
        return absl::CancelledError("Stream closed");
      }
    }
    // send_promise is nullptr on the initial poll. On subsequent polls while
    // an inner send is in-flight, send_promise is already initialized.
    if (send_promise == nullptr) {
      // Attempt to claim the in-flight send slot. Because ext_proc has at most
      // two message sources (client pipeline and server pipeline), at most one
      // sender can be in-flight and at most one can be waiting.
      RefCountedPtr<StreamingCallPromiseWrapper> streaming_call;
      {
        MutexLock lock(&mu_);
        if (ext_proc_send_in_flight_) {
          // Another send is currently in-flight. Save this caller's waker.
          ext_proc_send_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
          return Pending{};
        }
        ext_proc_send_in_flight_ = true;
        streaming_call = streaming_call_;
      }
      auto payload = payload_generator();
      if (!payload.ok()) {
        {
          MutexLock lock(&mu_);
          ext_proc_send_in_flight_ = false;
          ext_proc_send_waker_.Wakeup();
        }
        return payload.status();
      }
      if (streaming_call == nullptr) {
        {
          MutexLock lock(&mu_);
          ext_proc_send_in_flight_ = false;
          ext_proc_send_waker_.Wakeup();
        }
        return absl::CancelledError("Stream closed");
      }
      // Start the send on the underlying transport wrapper.
      send_promise = streaming_call->Send(std::move(*payload));
    }
    // Poll the in-flight send promise.
    auto poll = send_promise();
    if (poll.pending()) {
      return Pending{};
    }
    // Send completed. Release the in-flight slot and wake any waiting sender.
    {
      MutexLock lock(&mu_);
      ext_proc_send_in_flight_ = false;
      ext_proc_send_waker_.Wakeup();
    }
    return poll;
  };
}

// Handles the response path (Server to Client).
// Sets up a pipeline to process server initial metadata, response messages,
// and server trailing metadata.
// Also watches for ext_proc stream errors and aborts the call if a failure
// occurs and fail-open is not allowed.
absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerToClientCall() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " ServerToClientCall started";
  auto response_pipeline =
      TrySeq(ServerInitialMetadata(), ServerToClientMessages(),
             ServerTrailingMetadata());
  // Monitor the ext_proc stream for errors.
  // If the ext_proc stream fails and fail-open is NOT allowed, we abort the
  // call.
  auto watch_error = Seq(
      WaitForStreamStatus(),
      [self = Ref()](
          absl::Status status) -> absl::AnyInvocable<Poll<absl::Status>()> {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "watch_error stream_status: " << status
            << ", failure_mode_allow: "
            << (self->config()->failure_mode_allow.has_value()
                    ? (*self->config()->failure_mode_allow ? "true" : "false")
                    : "unset");
        if (!status.ok() &&
            !self->config()->failure_mode_allow.value_or(false)) {
          return [status]() -> Poll<absl::Status> { return status; };
        }
        return []() -> Poll<absl::Status> {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "watch_error returning Pending";
          return Pending{};
        };
      });
  // Race the response pipeline against the error watcher.
  // If watch_error returns an error, it will win the race and abort the
  // pipeline.
  auto run_pipeline =
      PrioritizedRace(std::move(watch_error), std::move(response_pipeline));
  return [self = Ref(),
          promise = std::move(run_pipeline)]() mutable -> Poll<absl::Status> {
    auto p = promise();
    if (auto* status = p.value_if_ready()) {
      GRPC_TRACE_LOG(ext_proc_filter, INFO)
          << "ExtProcCall " << self.get()
          << " ServerToClientCall finished. status=" << *status;
      // Handle failures in the pipeline (either from the response path or the
      // error watcher).
      if (!status->ok()) {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProcCall " << self.get()
            << " ServerToClientCall failed: " << *status;
        // Push error trailers to the parent call (client).
        auto error_md = CancelledServerMetadataFromStatus(*status);
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProcCall " << self.get()
            << ": Pushing server trailing metadata downstream (error)";
        self->handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
        // Cancel the child call to the backend server.
        self->initiator_.Cancel();
        // Close the ext_proc stream.
        self->CloseStream();
      }
      return *status;
    }
    return Pending{};
  };
}

// Intercepts and processes client initial metadata.
class ClientInitialMetadataProcessor {
 public:
  explicit ClientInitialMetadataProcessor(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call)
      : call_(std::move(call)) {}

  absl::AnyInvocable<Poll<absl::Status>()> Process() {
    // Check if request headers processing is enabled.
    if (!call_->ext_proc_filter_->config_->processing_mode
             ->send_request_headers) {
      return NonProcessingMode();
    } else if (call_->ext_proc_filter_->config_->observability_mode) {
      return ObservabilityMode();
    } else {
      return NormalMode();
    }
  }

 private:
  // Prepares the ProcessingRequest protobuf message for client initial metadata
  // (request headers), including filtering allowed/disallowed headers, creating
  // attributes struct, and sends it to the ext_proc server.
  static auto SendClientInitialMetadataRequest(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call,
      const ClientMetadataHandle& metadata,
      absl::string_view default_authority) {
    // Check if this is the first message on the stream to include processing
    // mode.
    const bool is_first_message = call->IsFirstMessageOnStream();
    upb::Arena arena;
    // Construct request attributes if configured.
    auto* header_attributes = CreateExtProcAttributesProtoStruct(
        arena.ptr(), call->ext_proc_filter_->config_->request_attributes,
        *metadata, default_authority);
    // Include processing_mode in the request only for the first stream message.
    std::optional<ExtProcProcessingMode> processing_mode;
    if (is_first_message) {
      processing_mode = call->ext_proc_filter_->config_->processing_mode;
    }
    // Serialize the client headers ProcessingRequest protobuf payload.
    auto payload = CreateExtProcClientHeadersRequest(
        arena.ptr(), metadata.get(),
        call->ext_proc_filter_->config_->forwarding_allowed_headers,
        call->ext_proc_filter_->config_->forwarding_disallowed_headers,
        header_attributes, call->ext_proc_filter_->config_->observability_mode,
        processing_mode);
    // Dispatch the payload over the external processor side-stream.
    return call->SendMessage(
        [payload = std::move(payload)]() { return payload; });
  }

  // Sends the client initial metadata request to the ext_proc server and
  // handles the result. If the send fails and fail-open is allowed, ignores the
  // error; otherwise, returns the failure status.
  static auto SendAndHandleClientInitialMetadata(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call,
      const ClientMetadataHandle& metadata) {
    return Map(
        // Send request headers to the ext_proc server.
        SendClientInitialMetadataRequest(
            call, metadata,
            call->ext_proc_filter_->default_authority_.as_string_view()),
        // Handle send completion status and determine fail-open behavior.
        [call](absl::Status status) mutable -> absl::Status {
          if (!status.ok()) {
            // If fail-open is allowed, ignore the send failure and proceed.
            if (call->IsFailOpenAllowed()) {
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << "ExtProc: Client initial metadata send failed, but "
                     "fail-open is allowed. Error: "
                  << status;
              return absl::OkStatus();
            }
            // If stream is closed with an error and fail-closed is configured,
            // propagate stream error.
            return (call->IsStreamClosed() && !call->GetStreamStatus().ok())
                       ? call->GetStreamStatus()
                       : status;
          }
          return absl::OkStatus();
        });
  }

  // Initializes and starts the child call to the backend server, and spawns the
  // background task for the server-to-client response path.
  static absl::AnyInvocable<Poll<absl::Status>()> StartChildCall(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call,
      ClientMetadataHandle metadata,
      ::google_protobuf_Struct* attributes = nullptr,
      Timestamp start_time = Timestamp::InfPast()) {
    if (start_time != Timestamp::InfPast()) {
      call->ext_proc_filter_->RecordClientHeadersDuration(
          (Timestamp::Now() - start_time).seconds());
    }
    call->initiator_ = call->ext_proc_filter_->MakeChildCall(
        std::move(metadata), call->handler_.arena()->Ref());
    call->handler_.AddChildCall(call->initiator_);
    // Spawn background task to handle server-to-client path.
    call->initiator_.SpawnInfallible("server_to_client", [call]() mutable {
      GRPC_TRACE_LOG(ext_proc_filter, INFO)
          << "ExtProc: server_to_client task started";
      return call->initiator_.CancelIfFails(call->ServerToClientCall());
    });
    // Continue with forwarding client messages (request body).
    return call->ClientToServerMessages(attributes);
  }

  // Forwards client initial metadata to the backend server without sending to
  // ext_proc when request header processing is disabled. Prepares request
  // attributes if body processing is enabled.
  absl::AnyInvocable<Poll<absl::Status>()> NonProcessingMode() {
    return TrySeq(
        // Pull initial metadata from the client.
        call_->handler_.PullClientInitialMetadata(),
        [call = call_](ClientMetadataHandle metadata) mutable
            -> absl::AnyInvocable<Poll<absl::Status>()> {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Client initial metadata received (non-processing):\n"
              << metadata->DebugString();
          const auto& processing_mode =
              *call->ext_proc_filter_->config_->processing_mode;
          ::google_protobuf_Struct* attributes = nullptr;
          // Check if request attributes need to be constructed for subsequent
          // request body processing.
          if (processing_mode.send_request_body &&
              !call->ext_proc_filter_->config_->request_attributes.empty()) {
            auto* arena = call->handler_.arena()->New<upb::Arena>();
            attributes = CreateExtProcAttributesProtoStruct(
                arena->ptr(),
                call->ext_proc_filter_->config_->request_attributes, *metadata,
                call->ext_proc_filter_->default_authority_.as_string_view());
          }
          // Bypass ext_proc header processing and start the backend child call
          // directly.
          return StartChildCall(call, std::move(metadata), attributes);
        });
  }

  // Handles client initial metadata in observability mode.
  absl::AnyInvocable<Poll<absl::Status>()> ObservabilityMode() {
    Timestamp start_time = Timestamp::Now();
    return TrySeq(
        call_->handler_.PullClientInitialMetadata(),
        [call = call_, start_time](ClientMetadataHandle metadata) mutable {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Client initial metadata received (observability):\n"
              << metadata->DebugString();
          // Handle write failure. In observability mode, if write fails and
          // failure_mode_allow is true, we fail-open and continue the call.
          auto send_promise =
              SendAndHandleClientInitialMetadata(call, metadata);
          // After the write attempt (successful or failed-open), we immediately
          // start the child call to the backend server without waiting for
          // responses from ext_proc.
          return TrySeq(
              std::move(send_promise),
              [call, metadata = std::move(metadata), start_time]() mutable {
                return StartChildCall(call, std::move(metadata),
                                      /*attributes=*/nullptr, start_time);
              });
        });
  }

  // Intercepts, sends to ext_proc, and applies mutations to client initial
  // metadata.
  absl::AnyInvocable<Poll<absl::Status>()> NormalMode() {
    Timestamp start_time = Timestamp::Now();
    return TrySeq(
        call_->handler_.PullClientInitialMetadata(),
        [call = call_, start_time](ClientMetadataHandle metadata) mutable {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Client initial metadata received:\n"
              << metadata->DebugString();
          auto send_promise =
              SendAndHandleClientInitialMetadata(call, metadata);
          return TrySeq(
              std::move(send_promise), call->request_headers_latch().Wait(),
              // Process the response from ext_proc and apply header mutations.
              [call,
               metadata = std::move(metadata)](ExtProcResponse response) mutable
                  -> absl::StatusOr<ClientMetadataHandle> {
                if (const auto* headers =
                        std::get_if<ExtProcResponse::RequestHeaders>(
                            &response.response);
                    headers != nullptr) {
                  const auto* rules = call->ext_proc_filter_->config_
                                              ->mutation_rules.has_value()
                                          ? &call->ext_proc_filter_->config_
                                                 ->mutation_rules.value()
                                          : nullptr;
                  auto status =
                      ApplyHeaderMutations(headers->mutation, rules, *metadata);
                  if (!status.ok()) return status;
                }
                return std::move(metadata);
              },
              [call, start_time](ClientMetadataHandle metadata) mutable {
                return StartChildCall(call, std::move(metadata),
                                      /*attributes=*/nullptr, start_time);
              });
        });
  }

  RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
};

// Intercepts and processes client-to-server messages.
class ClientToServerMessageProcessor {
 public:
  ClientToServerMessageProcessor(RefCountedPtr<ExtProcFilter::ExtProcCall> call,
                                 ::google_protobuf_Struct* attributes)
      : call_(std::move(call)), attributes_(attributes) {}

  absl::AnyInvocable<Poll<absl::Status>()> Process() {
    // Check if request body processing is enabled and stream is open.
    const bool send_request_body =
        call_->config()->processing_mode->send_request_body &&
        !call_->IsStreamClosed();
    if (!send_request_body) {
      return NonProcessingMode();
    } else if (call_->config()->observability_mode) {
      return ObservabilityMode();
    } else {
      return NormalMode();
    }
  }

 private:
  static auto SendClientMessageRequest(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call,
      const MessageHandle& message, bool end_of_stream,
      bool end_of_stream_without_message,
      ::google_protobuf_Struct* attributes) {
    // Extract payload bytes from message handle if present.
    std::string message_bytes;
    if (message != nullptr) {
      message_bytes = message->payload()->JoinIntoString();
    }
    // Increment outstanding client-to-server message count in non-observability
    // mode.
    if (!call->ext_proc_filter_->config_->observability_mode) {
      call->IncrementOutstandingClientToServerMessages();
    }
    // Mark half-close initiated on the client-to-server direction if
    // end_of_stream_without_message is set.
    if (end_of_stream_without_message) {
      call->MarkClientHalfCloseInitiated();
    }
    // Check if this is the first message on the stream to include processing
    // mode.
    const bool is_first_message = call->IsFirstMessageOnStream();
    return Map(call->SendMessage([ext_proc_call = call,
                                  message_bytes = std::move(message_bytes),
                                  end_of_stream, end_of_stream_without_message,
                                  attributes, is_first_message]() mutable {
      GRPC_TRACE_LOG(ext_proc_filter, INFO)
          << "ExtProc: ClientToServerMessages body message intercepted "
             "(observability mode)";
      std::optional<ExtProcProcessingMode> processing_mode;
      if (is_first_message) {
        processing_mode = ext_proc_call->config()->processing_mode;
      }
      upb::Arena arena;
      // Create the client body ProcessingRequest protobuf payload.
      return CreateExtProcClientBodyRequest(
          arena.ptr(), message_bytes, attributes,
          ext_proc_call->config()->observability_mode, processing_mode,
          end_of_stream, end_of_stream_without_message);
    }),
               // Mark first body message sent on success.
               [ext_proc_call = call](absl::Status status) {
                 if (status.ok()) {
                   ext_proc_call->SetFirstBodyMessageSent();
                 }
                 return status;
               });
  }

  // Forwards client messages to backend without processing when request body
  // processing is disabled or ext_proc stream is closed.
  absl::AnyInvocable<Poll<absl::Status>()> NonProcessingMode() {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: ClientToServerMessagesNonProcessingMode started";
    return Seq(
        // Iterate over all client messages and push them directly to backend.
        ForEach(MessagesFrom(call_->handler_),
                [call = call_](MessageHandle message) mutable -> absl::Status {
                  GRPC_TRACE_LOG(ext_proc_filter, INFO)
                      << "ExtProc: ClientToServerMessagesNonProcessingMode got "
                         "message";
                  // TODO(rishesh): Once PH2 work is done, we should make
                  // this pass (discard or handle cleanly). Currently we
                  // fail the RPC to avoid crashes on half-closed
                  // transport.
                  if (call->ext_proc_set_eos()) {
                    return absl::InternalError(
                        "Client sends closed by external processor");
                  }
                  call->initiator_.SpawnPushMessage(std::move(message));
                  return absl::OkStatus();
                }),
        // Finish sends on backend call once all client messages are forwarded.
        [call = call_]() mutable {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: ClientToServerMessagesNonProcessingMode finished "
                 "sends";
          call->initiator_.SpawnFinishSends();
          return absl::OkStatus();
        });
  }

  // Handles client-to-server messages in observability mode.
  absl::AnyInvocable<Poll<absl::Status>()> ObservabilityMode() {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: ClientToServerMessagesObservabilityMode started";
    return TrySeq(
        ForEach(
            MessagesFrom(call_->handler_),
            [call = call_,
             attributes = attributes_](MessageHandle message) mutable
                -> absl::AnyInvocable<Poll<absl::Status>()> {
              // If the external processor has already signaled end-of-stream
              // (EOS) for the client-to-server direction, we must fail the call
              // because the client is attempting to send more data.
              // TODO(rishesh): Once PH2 work is done, we should make
              // this pass (discard or handle cleanly). Currently we
              // fail the RPC to avoid crashes on half-closed
              // transport.
              if (call->ext_proc_set_eos()) {
                return Immediate(absl::InternalError(
                    "Client sends closed by external processor"));
              }
              // Send copy of message to ext_proc asynchronously while
              // forwarding to backend.
              if (!call->IsStreamClosed()) {
                return Seq(
                    SendClientMessageRequest(
                        call, message,
                        /*end_of_stream=*/false,
                        /*end_of_stream_without_message=*/false, attributes),
                    [call, message = std::move(message)](
                        absl::Status status) mutable -> absl::Status {
                      if (!status.ok() && !call->config()->failure_mode_allow) {
                        // If the send failed and fail-closed is configured, we
                        // fail the call. However, if the stream was closed
                        // cleanly by the server (e.g. trailers received), we
                        // ignore the failure to allow the call to complete.
                        if (call->IsStreamClosedCleanly()) {
                          GRPC_TRACE_LOG(ext_proc_filter, INFO)
                              << "ExtProc: Ignored client message send "
                                 "failure in observability mode due to "
                                 "clean close: "
                              << status;
                        } else {
                          return status;
                        }
                      }
                      // Forward the message to the backend.
                      call->initiator_.SpawnPushMessage(std::move(message));
                      return absl::OkStatus();
                    });
              }
              call->initiator_.SpawnPushMessage(std::move(message));
              return Immediate(absl::OkStatus());
            }),
        // After client finishes sending all messages (WritesDone).
        [call = call_, attributes = attributes_]() mutable
            -> absl::AnyInvocable<Poll<absl::Status>()> {
          Timestamp start_time = Timestamp::Now();
          // Send client half-close to ext_proc in observability mode.
          if (!call->IsStreamClosed()) {
            MessageHandle null_msg = nullptr;
            return Seq(
                SendClientMessageRequest(call, null_msg,
                                         /*end_of_stream=*/false,
                                         /*end_of_stream_without_message=*/true,
                                         attributes),
                [call,
                 start_time](absl::Status status) mutable -> absl::Status {
                  if (!status.ok()) {
                    GRPC_TRACE_LOG(ext_proc_filter, INFO)
                        << "ExtProc: Failed to send client half-close in "
                           "observability mode: "
                        << status;
                  }
                  call->ext_proc_filter_->RecordClientHalfCloseDuration(
                      (Timestamp::Now() - start_time).seconds());
                  call->initiator_.SpawnFinishSends();
                  call->SetClientSendsDone();
                  return absl::OkStatus();
                });
          }
          call->ext_proc_filter_->RecordClientHalfCloseDuration(
              (Timestamp::Now() - start_time).seconds());
          call->initiator_.SpawnFinishSends();
          call->SetClientSendsDone();
          return Immediate(absl::OkStatus());
        });
  }

  // Intercepts client-to-server body messages, sends them to ext_proc, and
  // forwards mutated responses to the backend.
  absl::AnyInvocable<Poll<absl::Status>()> NormalMode() {
    auto client_to_sidestream = TrySeq(
        ForEach(
            MessagesFrom(call_->handler_),
            [call = call_,
             attributes = attributes_](MessageHandle message) mutable
                -> absl::AnyInvocable<Poll<absl::Status>()> {
              // Wait for drain completion if drain was requested by ext_proc.
              if (call->drain_requested()) {
                return Seq(
                    call->WaitForStreamStatus(),
                    [call, message = std::move(message)](
                        absl::Status /*status*/) mutable -> absl::Status {
                      // If the stream did not close cleanly and fail-open is
                      // not allowed, return the stream error status
                      // immediately. Otherwise, bypass ext_proc and push the
                      // message.
                      if (!call->IsStreamClosedCleanly() &&
                          !call->IsFailOpenAllowed()) {
                        return call->GetStreamStatus();
                      }
                      if (message != nullptr) {
                        call->initiator_.SpawnPushMessage(std::move(message));
                      }
                      return absl::OkStatus();
                    });
              }
              // TODO(rishesh): Once PH2 work is done, we should make
              // this pass (discard or handle cleanly). Currently we
              // fail the RPC to avoid crashes on half-closed
              // transport.
              if (call->ext_proc_set_eos()) {
                return Immediate(absl::InternalError(
                    "Client sends closed by external processor"));
              }
              const bool send_message = !call->IsStreamClosed() &&
                                        !call->ext_proc_stream_half_closed();
              // Intercept message and send to ext_proc for processing.
              if (send_message) {
                return Seq(
                    SendClientMessageRequest(
                        call, message,
                        /*end_of_stream=*/false,
                        /*end_of_stream_without_message=*/false, attributes),
                    [call, message = std::move(message)](
                        absl::Status status) mutable -> absl::Status {
                      if (!status.ok() || call->IsStreamClosed()) {
                        // If not cleanly closed and fail-open is not allowed,
                        // return the error status immediately.
                        if (!call->IsStreamClosedCleanly() &&
                            !call->IsFailOpenAllowed()) {
                          return call->IsStreamClosed()
                                     ? call->GetStreamStatus()
                                     : status;
                        }
                        if (message != nullptr) {
                          call->initiator_.SpawnPushMessage(std::move(message));
                        }
                      }
                      return absl::OkStatus();
                    });
              }
              // Stream closed before we could send. If not cleanly closed and
              // fail-open is not allowed, return the error status immediately.
              // Otherwise, bypass ext_proc and forward the message.
              if (!call->IsStreamClosedCleanly() &&
                  !call->IsFailOpenAllowed()) {
                return Immediate(call->GetStreamStatus());
              }
              if (message != nullptr) {
                call->initiator_.SpawnPushMessage(std::move(message));
              }
              return Immediate(absl::OkStatus());
            }),
        // This promise runs when the client has finished sending all messages
        // (WritesDone). We must transition the client-to-server direction to
        // half-closed.
        [call = call_, attributes = attributes_]() mutable
            -> absl::AnyInvocable<Poll<absl::Status>()> {
          // If the external processor already closed the stream (EOS),
          // we don't need to notify it. Just mark client sends as done.
          if (call->ext_proc_set_eos()) {
            call->SetClientSendsDone();
            return Immediate(absl::OkStatus());
          }
          const bool send_message =
              !call->IsStreamClosed() && !call->ext_proc_stream_half_closed();
          if (send_message) {
            MessageHandle null_msg = nullptr;
            // Prepare an empty message with
            // end_of_stream_without_message=true to signal half-close to the
            // external processor.
            return Seq(SendClientMessageRequest(
                           call, null_msg,
                           /*end_of_stream=*/false,
                           /*end_of_stream_without_message=*/true, attributes),
                       [call](absl::Status status) mutable -> absl::Status {
                         if (!status.ok() || call->IsStreamClosed()) {
                           if (call->IsStreamClosedCleanly() ||
                               call->IsFailOpenAllowed()) {
                             call->initiator_.SpawnFinishSends();
                             call->SetClientSendsDone();
                             return absl::OkStatus();
                           }
                           return call->IsStreamClosed()
                                      ? call->GetStreamStatus()
                                      : status;
                         }
                         call->SetClientSendsDone();
                         return absl::OkStatus();
                       });
          }
          if (call->drain_requested() || call->IsStreamClosedCleanly() ||
              call->IsFailOpenAllowed()) {
            call->initiator_.SpawnFinishSends();
            call->SetClientSendsDone();
            return Immediate(absl::OkStatus());
          }
          return Immediate(call->GetStreamStatus());
        });
    // Handles processing responses from the external processing server
    // (sidestream) and forwarding the (possibly mutated) request body to the
    // backend server.
    auto sidestream_to_server = Seq(
        ForEach(std::move(call_->request_body_pipe().receiver),
                [call = call_](absl::StatusOr<ExtProcResponse> result) mutable {
                  // If the stream failed but fail-open is allowed, we ignore
                  // the error and proceed. Otherwise, we propagate the error.
                  if (!result.ok()) {
                    if (call->IsFailOpenAllowed()) {
                      return absl::OkStatus();
                    }
                    return result.status();
                  }
                  if (const auto* request_body =
                          std::get_if<ExtProcResponse::RequestBody>(
                              &result->response)) {
                    if (!request_body->mutation.end_of_stream_without_message) {
                      auto slice =
                          Slice::FromCopiedString(request_body->mutation.body);
                      auto new_msg =
                          call->initiator_.arena()->MakePooled<Message>(
                              SliceBuffer(std::move(slice)), /*flags=*/0);
                      call->initiator_.SpawnPushMessage(std::move(new_msg));
                    }
                  }
                  return absl::OkStatus();
                }),
        // If we have finished sending all client messages to the ext_proc
        // server, or if the ext_proc stream was closed (e.g. due to immediate
        // response), we signal the backend server that we are done sending the
        // request body.
        [call = call_](absl::Status status) mutable {
          Timestamp start_time = Timestamp::Now();
          if (call->c2s_write_done() || !call->IsStreamClosed()) {
            call->ext_proc_filter_->RecordClientHalfCloseDuration(
                (Timestamp::Now() - start_time).seconds());
            call->initiator_.SpawnFinishSends();
          }
          return status;
        });
    return Map(TryJoin<absl::StatusOr>(std::move(client_to_sidestream),
                                       std::move(sidestream_to_server)),
               [call = call_](auto result) -> absl::Status {
                 if (!result.ok()) {
                   return result.status();
                 }
                 if (call->IsFailOpenAllowed()) {
                   return absl::OkStatus();
                 }
                 return call->GetStreamStatus();
               });
  }

  RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
  ::google_protobuf_Struct* attributes_;
};

// Fetches server initial metadata from the backend, sends the ProcessingRequest
// to the external processor, and handles send errors across all modes.
class ServerToExtProcInitialMetadataProcessor {
 public:
  explicit ServerToExtProcInitialMetadataProcessor(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call)
      : call_(std::move(call)) {}

  // Prepares the ProcessingRequest protobuf message for server initial metadata
  // (response headers) and sends it over the ext_proc stream.
  static auto SendServerInitialMetadataRequest(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call,
      const ServerMetadataHandle& metadata, bool end_of_stream = false) {
    const bool is_first_message = call->IsFirstMessageOnStream();
    std::optional<ExtProcProcessingMode> processing_mode;
    if (is_first_message) {
      processing_mode = call->ext_proc_filter_->config_->processing_mode;
    }
    upb::Arena arena;
    absl::StatusOr<std::string> payload = CreateExtProcServerHeadersRequest(
        arena.ptr(), metadata.get(),
        call->ext_proc_filter_->config_->forwarding_allowed_headers,
        call->ext_proc_filter_->config_->forwarding_disallowed_headers,
        /*attributes=*/nullptr,
        call->ext_proc_filter_->config_->observability_mode, processing_mode,
        end_of_stream);
    return call->SendMessage(
        [payload = std::move(payload)]() { return payload; });
  }

  // Fetches server initial metadata from the backend and dispatches to
  // ext_proc.
  template <typename NextHandler>
  absl::AnyInvocable<Poll<absl::Status>()> FetchAndSend(
      NextHandler next_handler) {
    return Seq(
        // Pull server initial metadata from backend.
        call_->initiator_.PullServerInitialMetadata(),
        [self = *this, next_handler = std::move(next_handler)](
            std::optional<ServerMetadataHandle> md) mutable
            -> absl::AnyInvocable<Poll<absl::Status>()> {
          // If metadata is empty, this is a trailers-only RPC.
          if (!md.has_value()) {
            self.call_->SetIsTrailersOnly();
            return Immediate(absl::OkStatus());
          }
          ServerMetadataHandle metadata = std::move(*md);
          // Check if response headers processing is enabled.
          const bool send_headers =
              self.call_->config()->processing_mode->send_response_headers &&
              !self.call_->IsStreamClosed() &&
              !self.call_->ext_proc_stream_half_closed();
          if (!send_headers) {
            return next_handler.NonProcessingMode(std::move(metadata));
          } else if (self.call_->config()->observability_mode) {
            return self.ObservabilityMode(
                std::move(metadata),
                [next_handler](
                    RefCountedPtr<ExtProcFilter::ExtProcCall> /*call*/,
                    ServerMetadataHandle metadata,
                    Timestamp start_time) mutable {
                  return next_handler.ObservabilityMode(std::move(metadata),
                                                        start_time);
                });
          } else if (self.call_->drain_requested()) {
            return self.DrainMode(std::move(metadata));
          } else {
            return self.NormalMode(
                std::move(metadata),
                [next_handler](
                    RefCountedPtr<ExtProcFilter::ExtProcCall> /*call*/,
                    ServerMetadataHandle metadata,
                    Timestamp start_time) mutable {
                  return next_handler.NormalMode(std::move(metadata),
                                                 start_time);
                });
          }
        });
  }

  // Normal mode: sends headers to ext_proc and invokes callback upon send
  // completion.
  absl::AnyInvocable<Poll<absl::Status>()> NormalMode(
      ServerMetadataHandle metadata, OnSuccessCallback on_success) {
    return InitialMetadataHelper(std::move(metadata), /*end_of_stream=*/false,
                                 std::move(on_success));
  }

  // Observability mode: sends headers to ext_proc and invokes callback upon
  // send completion.
  absl::AnyInvocable<Poll<absl::Status>()> ObservabilityMode(
      ServerMetadataHandle metadata, OnSuccessCallback on_success) {
    return InitialMetadataHelper(std::move(metadata),
                                 /*end_of_stream=*/call_->is_trailers_only(),
                                 std::move(on_success));
  }

  // Handles server initial metadata when the external processor has requested a
  // drain.
  absl::AnyInvocable<Poll<absl::Status>()> DrainMode(
      ServerMetadataHandle metadata) {
    return Map(
        call_->WaitForStreamStatus(),
        [call = call_, metadata = std::move(metadata)](
            absl::Status status) mutable -> absl::Status {
          if (!call->IsStreamClosedCleanly() && !call->IsFailOpenAllowed()) {
            return status;
          }
          call->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
          return absl::OkStatus();
        });
  }

 private:
  // Helper for sending server initial metadata to ext_proc and handling send
  // errors across all modes.
  absl::AnyInvocable<Poll<absl::Status>()> InitialMetadataHelper(
      ServerMetadataHandle metadata, bool end_of_stream,
      OnSuccessCallback on_success) {
    Timestamp start_time = Timestamp::Now();
    return Seq(
        SendServerInitialMetadataRequest(call_, metadata, end_of_stream),
        [call = call_, metadata = std::move(metadata), start_time,
         on_success = std::move(on_success)](absl::Status status) mutable
            -> absl::AnyInvocable<Poll<absl::Status>()> {
          if (!status.ok()) {
            if (call->IsFailOpenAllowed() || call->IsStreamClosedCleanly()) {
              call->ext_proc_filter_->RecordServerHeadersDuration(
                  (Timestamp::Now() - start_time).seconds());
              call->handler_.SpawnPushServerInitialMetadata(
                  std::move(metadata));
              return Immediate(absl::OkStatus());
            }
            if (call->IsStreamClosed()) {
              absl::Status err = call->GetStreamStatus();
              return Immediate(!err.ok() ? err : status);
            }
            return Map(call->WaitForStreamStatus(),
                       [status](absl::Status stream_status) {
                         return !stream_status.ok() ? stream_status : status;
                       });
          }
          return on_success(call, std::move(metadata), start_time);
        });
  }

  RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
};

// Receives response message from ext_proc server, applies mutations, handles
// errors, and pushes final metadata to client in all modes.
class ExtProcToClientInitialMetadataProcessor {
 public:
  explicit ExtProcToClientInitialMetadataProcessor(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call)
      : call_(std::move(call)) {}

  // Non-processing mode: pushes server initial metadata directly to client.
  absl::AnyInvocable<Poll<absl::Status>()> NonProcessingMode(
      ServerMetadataHandle metadata) {
    return [call = call_, metadata = std::move(metadata)]() mutable {
      if (call != nullptr && call->IsStreamClosed() &&
          !call->IsStreamClosedCleanly() && !call->IsFailOpenAllowed()) {
        return call->GetStreamStatus();
      }
      GRPC_TRACE_LOG(ext_proc_filter, INFO)
          << "ExtProc: ServerInitialMetadataNonProcessingMode metadata: "
          << metadata->DebugString();
      call->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
      return absl::OkStatus();
    };
  }

  // Observability mode: records duration and pushes server initial metadata to
  // client.
  absl::AnyInvocable<Poll<absl::Status>()> ObservabilityMode(
      ServerMetadataHandle metadata, Timestamp start_time) {
    call_->ext_proc_filter_->RecordServerHeadersDuration(
        (Timestamp::Now() - start_time).seconds());
    call_->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
    return Immediate(absl::OkStatus());
  }

  // Normal mode: gets response from ext_proc server, applies mutations, handles
  // errors, and pushes to client.
  absl::AnyInvocable<Poll<absl::Status>()> NormalMode(
      ServerMetadataHandle metadata, Timestamp start_time) {
    return Map(
        // Wait for response headers from the external processing server.
        call_->response_headers_latch().Wait(),
        [metadata = std::move(metadata), call = call_, start_time](
            absl::StatusOr<ExtProcResponse> response) mutable -> absl::Status {
          // Propagate error if ext_proc response processing failed.
          if (!response.ok()) {
            return response.status();
          }
          // Apply header mutations if response contains ResponseHeaders.
          if (const auto* headers =
                  std::get_if<ExtProcResponse::ResponseHeaders>(
                      &response->response);
              headers != nullptr) {
            const auto* rules = call->config()->mutation_rules.has_value()
                                    ? &call->config()->mutation_rules.value()
                                    : nullptr;
            if (auto status =
                    ApplyHeaderMutations(headers->mutation, rules, *metadata);
                !status.ok()) {
              return status;
            }
          }
          // Check if the stream closed with an error and fail-open is not
          // allowed.
          if (!call->IsFailOpenAllowed() && call->IsStreamClosed()) {
            return call->GetStreamStatus();
          }
          // Record server headers processing duration.
          call->ext_proc_filter_->RecordServerHeadersDuration(
              (Timestamp::Now() - start_time).seconds());
          // Forward the (possibly mutated) server initial metadata to the
          // client.
          call->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
          return absl::OkStatus();
        });
  }

  RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
};

// Orchestrates server initial metadata processing by coordinating
// ServerToExtProcInitialMetadataProcessor and
// ExtProcToClientInitialMetadataProcessor.
class ServerInitialMetadataProcessor {
 public:
  explicit ServerInitialMetadataProcessor(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call)
      : server_to_extproc_(call), extproc_to_client_(std::move(call)) {}

  static auto SendServerInitialMetadataRequest(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call,
      const ServerMetadataHandle& metadata, bool end_of_stream = false) {
    return ServerToExtProcInitialMetadataProcessor::
        SendServerInitialMetadataRequest(std::move(call), metadata,
                                         end_of_stream);
  }

  absl::AnyInvocable<Poll<absl::Status>()> Process() {
    return server_to_extproc_.FetchAndSend(extproc_to_client_);
  }

 private:
  ServerToExtProcInitialMetadataProcessor server_to_extproc_;
  ExtProcToClientInitialMetadataProcessor extproc_to_client_;
};

// Intercepts server-to-client messages from backend, prepares and dispatches
// ProcessingRequest messages to the external processing server.
class ServerToExtProcMessageProcessor {
 public:
  explicit ServerToExtProcMessageProcessor(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call)
      : call_(std::move(call)) {}

  // Prepares the ProcessingRequest protobuf message for server response body
  // and sends it over the ext_proc stream.
  static auto SendServerMessageRequest(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call,
      const MessageHandle& message) {
    // Increment outstanding server-to-client message count in non-observability
    // mode.
    if (!call->ext_proc_filter_->config_->observability_mode) {
      call->IncrementOutstandingServerToClientMessages();
    }
    // Extract payload bytes from server message handle.
    std::string message_bytes;
    if (message != nullptr) {
      message_bytes = message->payload()->JoinIntoString();
    }
    // Check if this is the first message on the stream to include processing
    // mode.
    bool is_first_message = call->IsFirstMessageOnStream();
    return Map(
        call->SendMessage([ext_proc_call = call, is_first_message,
                           message_bytes = std::move(message_bytes)]() mutable {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: ServerToClientMessages body message intercepted";
          std::optional<ExtProcProcessingMode> processing_mode;
          if (is_first_message) {
            processing_mode = ext_proc_call->config()->processing_mode;
          }
          upb::Arena arena;
          // Create the server body ProcessingRequest protobuf payload.
          return CreateExtProcServerBodyRequest(
              arena.ptr(), message_bytes, /*attributes=*/nullptr,
              ext_proc_call->config()->observability_mode, processing_mode);
        }),
        // Mark first body message sent on success.
        [ext_proc_call = call](absl::Status status) {
          if (status.ok()) {
            ext_proc_call->SetFirstBodyMessageSent();
          }
          return status;
        });
  }

  // Handles server-to-client messages in observability mode.
  absl::AnyInvocable<Poll<absl::Status>()> ObservabilityMode() {
    return ForEach(
        MessagesFrom(call_->initiator_),
        [call = call_](MessageHandle message) mutable
            -> absl::AnyInvocable<Poll<absl::Status>()> {
          // Intercept message and send to ext_proc asynchronously without
          // blocking backend forward.
          if (!call->IsStreamClosed()) {
            return Seq(SendServerMessageRequest(call, message),
                       [call, message = std::move(message)](
                           absl::Status status) mutable -> absl::Status {
                         if (!status.ok() && !call->IsFailOpenAllowed()) {
                           if (!call->IsStreamClosedCleanly() &&
                               !call->IsFailOpenAllowed()) {
                             return status;
                           }
                         }
                         call->handler_.SpawnPushMessage(std::move(message));
                         return absl::OkStatus();
                       });
          }
          call->handler_.SpawnPushMessage(std::move(message));
          return Immediate(absl::OkStatus());
        });
  }

  // Intercepts server-to-client messages, sends to ext_proc, and coordinates
  // with next_handler (ExtProcToClientMessageProcessor) for response delivery.
  template <typename NextHandler>
  absl::AnyInvocable<Poll<absl::Status>()> NormalMode(
      NextHandler next_handler) {
    // send_loop: intercepts server messages and dispatches them to ext_proc.
    auto send_loop = Seq(
        ForEach(MessagesFrom(call_->initiator_),
                [call = call_](MessageHandle message) mutable
                    -> absl::AnyInvocable<Poll<absl::Status>()> {
                  // Wait for drain completion if drain was requested by
                  // ext_proc.
                  if (call->drain_requested()) {
                    return Seq(
                        call->WaitForStreamStatus(),
                        [call, message = std::move(message)](
                            absl::Status status) mutable -> absl::Status {
                          if (!call->IsStreamClosedCleanly() &&
                              !call->IsFailOpenAllowed()) {
                            return status;
                          }
                          call->handler_.SpawnPushMessage(std::move(message));
                          return absl::OkStatus();
                        });
                  }
                  const bool send_message =
                      !call->IsStreamClosed() &&
                      !call->ext_proc_stream_half_closed();
                  // Intercept and send server message to ext_proc if stream is
                  // open.
                  if (send_message) {
                    return Seq(
                        SendServerMessageRequest(call, message),
                        [call, message = std::move(message)](
                            absl::Status status) mutable -> absl::Status {
                          if (status.ok() && !call->IsStreamClosed()) {
                            return absl::OkStatus();
                          }
                          // If send failed or stream closed with error,
                          // propagate error if fail-open is not allowed and
                          // stream did not close cleanly.
                          if (!call->IsFailOpenAllowed() &&
                              !call->IsStreamClosedCleanly()) {
                            return call->IsStreamClosed()
                                       ? call->GetStreamStatus()
                                       : status;
                          }
                          // Otherwise, forward message directly to client.
                          call->handler_.SpawnPushMessage(std::move(message));
                          return absl::OkStatus();
                        });
                  }
                  // Ext_proc stream closed before message send. If not cleanly
                  // closed and fail-open is not allowed, return error status.
                  if (!call->IsStreamClosedCleanly() &&
                      !call->IsFailOpenAllowed()) {
                    return Immediate(call->GetStreamStatus());
                  }
                  // Bypass ext_proc and forward server message directly to
                  // client.
                  call->handler_.SpawnPushMessage(std::move(message));
                  return Immediate(absl::OkStatus());
                }),
        // Mark server-to-client writes done on the backend stream.
        [call = call_]() {
          call->SetServerToClientWritesDone();
          return absl::OkStatus();
        });
    // read_loop: receives mutated response body chunks from ext_proc and
    // forwards to client.
    auto read_loop = next_handler.NormalMode();
    // Join send_loop and read_loop concurrently.
    return Map(
        TryJoin<absl::StatusOr>(std::move(send_loop), std::move(read_loop)),
        [](auto result) -> absl::Status { return result.status(); });
  }

 private:
  RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
};

// Receives response message chunks from ext_proc server, constructs mutated
// messages, and pushes them to the client handler.
class ExtProcToClientMessageProcessor {
 public:
  explicit ExtProcToClientMessageProcessor(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call)
      : call_(std::move(call)) {}

  // Forwards server messages to client without ext_proc processing.
  absl::AnyInvocable<Poll<absl::Status>()> NonProcessingMode() {
    return ForEach(MessagesFrom(call_->initiator_),
                   // Forward unmutated message directly to client handler.
                   [call = call_](MessageHandle message) mutable {
                     call->handler_.SpawnPushMessage(std::move(message));
                     return absl::OkStatus();
                   });
  }

  // Receives mutated response body chunks from ext_proc and forwards to client.
  absl::AnyInvocable<Poll<absl::Status>()> NormalMode() {
    return ForEach(
        std::move(call_->response_body_pipe().receiver),
        [call = call_](absl::StatusOr<ExtProcResponse> response) mutable {
          if (!response.ok()) {
            return response.status();
          }
          const auto& response_body =
              std::get<ExtProcResponse::ResponseBody>((*response).response);
          auto slice = Slice::FromCopiedString(response_body.mutation.body);
          auto new_msg = call->handler_.arena()->MakePooled<Message>(
              SliceBuffer(std::move(slice)), /*flags=*/0);
          call->handler_.SpawnPushMessage(std::move(new_msg));
          return absl::OkStatus();
        });
  }

 private:
  RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
};

// Main dispatcher and orchestrator for server-to-client messages. Coordinates
// ServerToExtProcMessageProcessor and ExtProcToClientMessageProcessor.
class ServerToClientMessageProcessor {
 public:
  explicit ServerToClientMessageProcessor(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call)
      : call_(call),
        server_to_extproc_(call),
        extproc_to_client_(std::move(call)) {}

  absl::AnyInvocable<Poll<absl::Status>()> Process() {
    // For trailers-only RPCs, no response body processing is needed.
    if (call_->is_trailers_only()) {
      return Immediate(absl::OkStatus());
    }
    // Check if response body processing is enabled and stream is open.
    const bool send_body =
        call_->config()->processing_mode->send_response_body &&
        !call_->IsStreamClosed();
    if (!send_body) {
      return extproc_to_client_.NonProcessingMode();
    } else if (call_->config()->observability_mode) {
      return server_to_extproc_.ObservabilityMode();
    } else {
      return server_to_extproc_.NormalMode(extproc_to_client_);
    }
  }

 private:
  RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
  ServerToExtProcMessageProcessor server_to_extproc_;
  ExtProcToClientMessageProcessor extproc_to_client_;
};

// Sends server trailing metadata (or trailers-only initial metadata) to the
// external processor and handles send errors across all modes.
class ServerToExtProcTrailingMetadataProcessor {
 public:
  explicit ServerToExtProcTrailingMetadataProcessor(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call)
      : call_(std::move(call)) {}

  // Prepares the ProcessingRequest protobuf message for server trailing
  // metadata (trailers) and sends it over the ext_proc stream.
  static absl::AnyInvocable<Poll<absl::Status>()>
  SendServerTrailingMetadataRequest(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call,
      const ServerMetadataHandle& metadata) {
    // Return early if the ext_proc stream is closed or already half-closed.
    if (call->IsStreamClosed() || call->ext_proc_stream_half_closed()) {
      return Immediate(absl::OkStatus());
    }
    // Check if this is the first message on the stream to attach processing
    // mode.
    const bool is_first_message = call->IsFirstMessageOnStream();
    std::optional<ExtProcProcessingMode> processing_mode;
    if (is_first_message) {
      processing_mode = call->config()->processing_mode;
    }
    upb::Arena arena;
    // Create the server trailers ProcessingRequest protobuf payload.
    absl::StatusOr<std::string> payload = CreateExtProcServerTrailersRequest(
        arena.ptr(), metadata.get(), call->config()->forwarding_allowed_headers,
        call->config()->forwarding_disallowed_headers,
        /*attributes=*/nullptr, call->config()->observability_mode,
        processing_mode);
    return Map(
        call->SendMessage([payload = std::move(payload)]() { return payload; }),
        [ext_proc_call = call](absl::Status status) {
          // Mark server trailers sent on the ext_proc stream on success.
          if (status.ok()) {
            ext_proc_call->SetServerTrailersSent();
          }
          return status;
        });
  }

  // Orchestrates trailers-only flow for server-to-extproc side.
  template <typename NextHandler>
  absl::AnyInvocable<Poll<absl::Status>()> TrailersOnly(
      NextHandler next_handler) {
    return Seq(
        call_->initiator_.PullServerTrailingMetadata(),
        [self = *this, next_handler = std::move(next_handler)](
            ServerMetadataHandle metadata) mutable
            -> absl::AnyInvocable<Poll<absl::Status>()> {
          const bool send_headers =
              self.call_->config()->processing_mode->send_response_headers &&
              !self.call_->IsStreamClosed() &&
              !self.call_->ext_proc_stream_half_closed();
          if (!send_headers) {
            return next_handler.NonProcessingMode(std::move(metadata));
          } else if (self.call_->config()->observability_mode) {
            return self.TrailersOnlyHelper(
                std::move(metadata),
                [next_handler](
                    RefCountedPtr<ExtProcFilter::ExtProcCall> /*call*/,
                    ServerMetadataHandle metadata,
                    Timestamp start_time) mutable {
                  return next_handler.TrailersOnlyObservabilityMode(
                      std::move(metadata), start_time);
                });
          } else if (self.call_->drain_requested()) {
            return self.DrainMode(std::move(metadata));
          } else {
            return self.TrailersOnlyHelper(
                std::move(metadata),
                [next_handler](
                    RefCountedPtr<ExtProcFilter::ExtProcCall> /*call*/,
                    ServerMetadataHandle metadata,
                    Timestamp start_time) mutable {
                  return next_handler.TrailersOnlyNormalMode(
                      std::move(metadata), start_time);
                });
          }
        });
  }

  // Orchestrates normal trailers flow for server-to-extproc side.
  template <typename NextHandler>
  absl::AnyInvocable<Poll<absl::Status>()> NormalTrailers(
      NextHandler next_handler) {
    return Seq(
        // Pull server trailing metadata from backend.
        call_->initiator_.PullServerTrailingMetadata(),
        [self = *this, next_handler = std::move(next_handler)](
            ServerMetadataHandle metadata) mutable
            -> absl::AnyInvocable<Poll<absl::Status>()> {
          // If status is non-OK, bypass ext_proc and forward error trailers to
          // client immediately.
          if (!IsStatusOk(*metadata)) {
            self.call_->handler_.SpawnPushServerTrailingMetadata(
                std::move(metadata));
            return Immediate(absl::OkStatus());
          }
          const bool send_trailers =
              self.call_->config()->processing_mode->send_response_trailers &&
              !self.call_->IsStreamClosed() &&
              !self.call_->ext_proc_stream_half_closed();
          if (!send_trailers) {
            return next_handler.NonProcessingMode(std::move(metadata));
          } else if (self.call_->config()->observability_mode) {
            return self.NormalTrailersHelper(
                std::move(metadata),
                [next_handler](
                    RefCountedPtr<ExtProcFilter::ExtProcCall> /*call*/,
                    ServerMetadataHandle metadata,
                    Timestamp start_time) mutable {
                  return next_handler.ObservabilityMode(std::move(metadata),
                                                        start_time);
                });
          } else if (self.call_->drain_requested()) {
            return self.DrainMode(std::move(metadata));
          } else {
            return self.NormalTrailersHelper(
                std::move(metadata),
                [next_handler](
                    RefCountedPtr<ExtProcFilter::ExtProcCall> /*call*/,
                    ServerMetadataHandle metadata,
                    Timestamp start_time) mutable {
                  return next_handler.NormalMode(std::move(metadata),
                                                 start_time);
                });
          }
        });
  }

  // Handles server trailing metadata when the external processor has requested
  // a drain.
  absl::AnyInvocable<Poll<absl::Status>()> DrainMode(
      ServerMetadataHandle metadata) {
    return Map(
        call_->WaitForStreamStatus(),
        [call = call_, metadata = std::move(metadata)](
            absl::Status status) mutable -> absl::Status {
          if (!call->IsStreamClosedCleanly() && !call->IsFailOpenAllowed()) {
            return status;
          }
          call->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
          return absl::OkStatus();
        });
  }

 private:
  // Helper for sending normal server trailing metadata to ext_proc.
  absl::AnyInvocable<Poll<absl::Status>()> NormalTrailersHelper(
      ServerMetadataHandle metadata, OnSuccessCallback on_success) {
    Timestamp start_time = Timestamp::Now();
    return Seq(
        // Send server trailers request to ext_proc.
        SendServerTrailingMetadataRequest(call_, metadata),
        [call = call_, metadata = std::move(metadata), start_time,
         on_success = std::move(on_success)](absl::Status status) mutable
            -> absl::AnyInvocable<Poll<absl::Status>()> {
          if (!status.ok()) {
            // If fail-open is allowed or stream closed cleanly, record
            // duration and forward metadata.
            if (call->IsFailOpenAllowed() || call->IsStreamClosedCleanly()) {
              call->ext_proc_filter_->RecordServerTrailersDuration(
                  (Timestamp::Now() - start_time).seconds());
              call->handler_.SpawnPushServerTrailingMetadata(
                  std::move(metadata));
              return Immediate(absl::OkStatus());
            }
            // If stream is closed with error, return the stream status
            // error.
            if (call->IsStreamClosed()) {
              absl::Status err = call->GetStreamStatus();
              return Immediate(!err.ok() ? err : status);
            }
            // Otherwise, wait for stream closure status.
            return Map(call->WaitForStreamStatus(),
                       [status](absl::Status stream_status) {
                         return !stream_status.ok() ? stream_status : status;
                       });
          }
          // Invoke success callback with the response from ext_proc.
          return on_success(call, std::move(metadata), start_time);
        });
  }

  // Helper for sending trailers-only metadata to ext_proc as initial metadata
  // with EOS.
  absl::AnyInvocable<Poll<absl::Status>()> TrailersOnlyHelper(
      ServerMetadataHandle metadata, OnSuccessCallback on_success) {
    Timestamp start_time = Timestamp::Now();
    return Seq(
        // In trailers-only mode, send trailers as initial metadata with
        // end_of_stream=true.
        ServerInitialMetadataProcessor::SendServerInitialMetadataRequest(
            call_, metadata, /*end_of_stream=*/true),
        [call = call_, metadata = std::move(metadata), start_time,
         on_success = std::move(on_success)](absl::Status status) mutable
            -> absl::AnyInvocable<Poll<absl::Status>()> {
          if (!status.ok()) {
            // If fail-open is allowed or stream closed cleanly, record
            // duration and forward metadata.
            if (call->IsFailOpenAllowed() || call->IsStreamClosedCleanly()) {
              call->ext_proc_filter_->RecordServerHeadersDuration(
                  (Timestamp::Now() - start_time).seconds());
              call->handler_.SpawnPushServerTrailingMetadata(
                  std::move(metadata));
              return Immediate(absl::OkStatus());
            }
            // If stream is closed with error, return the stream status
            // error.
            if (call->IsStreamClosed()) {
              absl::Status err = call->GetStreamStatus();
              return Immediate(!err.ok() ? err : status);
            }
            // Otherwise, wait for stream closure status.
            return Map(call->WaitForStreamStatus(),
                       [status](absl::Status stream_status) {
                         return !stream_status.ok() ? stream_status : status;
                       });
          }
          // Invoke success callback with the response from ext_proc.
          return on_success(call, std::move(metadata), start_time);
        });
  }

  RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
};

// Receives response message from ext_proc server, applies mutations, handles
// errors, and pushes trailing metadata to client in all modes.
class ExtProcToClientTrailingMetadataProcessor {
 public:
  explicit ExtProcToClientTrailingMetadataProcessor(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call)
      : call_(std::move(call)) {}

  // Non-processing mode: closes body pipe sender if needed and forwards server
  // trailers to client.
  absl::AnyInvocable<Poll<absl::Status>()> NonProcessingMode(
      ServerMetadataHandle metadata) {
    if (call_->config()->processing_mode->send_response_body &&
        !call_->config()->observability_mode) {
      call_->response_body_pipe().sender.MarkClosed();
    }
    return [call = call_, metadata = std::move(metadata)]() mutable {
      if (call != nullptr && call->IsStreamClosed() &&
          !call->IsStreamClosedCleanly() && !call->IsFailOpenAllowed()) {
        return call->GetStreamStatus();
      }
      call->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
      return absl::OkStatus();
    };
  }

  // Handles trailers-only RPC trailing metadata in observability mode.
  absl::AnyInvocable<Poll<absl::Status>()> TrailersOnlyObservabilityMode(
      ServerMetadataHandle metadata, Timestamp start_time) {
    return
        [call = call_, metadata = std::move(metadata), start_time]() mutable {
          call->ext_proc_filter_->RecordServerHeadersDuration(
              (Timestamp::Now() - start_time).seconds());
          call->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
          return absl::OkStatus();
        };
  }

  // Handles normal trailing metadata in observability mode.
  absl::AnyInvocable<Poll<absl::Status>()> ObservabilityMode(
      ServerMetadataHandle metadata, Timestamp start_time) {
    return
        [call = call_, metadata = std::move(metadata), start_time]() mutable {
          call->ext_proc_filter_->RecordServerTrailersDuration(
              (Timestamp::Now() - start_time).seconds());
          call->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
          return absl::OkStatus();
        };
  }

  // Handles trailers-only RPC trailing metadata in normal processing mode.
  absl::AnyInvocable<Poll<absl::Status>()> TrailersOnlyNormalMode(
      ServerMetadataHandle metadata, Timestamp start_time) {
    return Map(
        // Wait for response headers from ext_proc in trailers-only mode.
        call_->response_headers_latch().Wait(),
        [metadata = std::move(metadata), call = call_, start_time](
            absl::StatusOr<ExtProcResponse> response) mutable -> absl::Status {
          absl::Status status = absl::OkStatus();
          if (!response.ok()) {
            status = response.status();
          } else if (const auto* headers =
                         std::get_if<ExtProcResponse::ResponseHeaders>(
                             &response->response);
                     headers != nullptr) {
            const auto* rules = call->config()->mutation_rules.has_value()
                                    ? &call->config()->mutation_rules.value()
                                    : nullptr;
            // Apply header mutations if returned.
            status = ApplyHeaderMutations(headers->mutation, rules, *metadata);
          }
          // If mutation failed and fail-open is not allowed, propagate stream
          // error.
          if (!status.ok() && !call->IsFailOpenAllowed()) {
            return call->IsStreamClosed() ? call->GetStreamStatus() : status;
          }
          call->ext_proc_filter_->RecordServerHeadersDuration(
              (Timestamp::Now() - start_time).seconds());
          // Forward mutated trailing metadata to client.
          call->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
          return absl::OkStatus();
        });
  }

  // Handles normal server trailing metadata in normal processing mode.
  absl::AnyInvocable<Poll<absl::Status>()> NormalMode(
      ServerMetadataHandle metadata, Timestamp start_time) {
    return Map(
        // Wait for response trailers from ext_proc.
        call_->response_trailers_latch().Wait(),
        [metadata = std::move(metadata), call = call_, start_time](
            absl::StatusOr<ExtProcResponse> response) mutable -> absl::Status {
          auto config = call->config();
          if (!response.ok()) {
            return response.status();
          }
          // Handle immediate response if returned by ext_proc.
          if (const auto* immediate =
                  std::get_if<ExtProcResponse::ImmediateResponse>(
                      &response->response);
              immediate != nullptr && (!config->disable_immediate_response ||
                                       call->server_trailers_sent())) {
            auto error_md = CancelledServerMetadataFromStatus(
                static_cast<grpc_status_code>(immediate->status),
                immediate->details);
            const auto* rules = config->mutation_rules.has_value()
                                    ? &config->mutation_rules.value()
                                    : nullptr;
            (void)ApplyHeaderMutations(immediate->header_mutation, rules,
                                       *error_md);
            call->handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
            return absl::OkStatus();
          }
          // Close response and request body pipe senders.
          call->response_body_pipe().sender.MarkClosed();
          call->request_body_pipe().sender.MarkClosed();
          // Apply trailer mutations if response contains
          // ResponseTrailers.
          if (const auto* response_trailers =
                  std::get_if<ExtProcResponse::ResponseTrailers>(
                      &response->response);
              response_trailers != nullptr) {
            const auto* rules = config->mutation_rules.has_value()
                                    ? &config->mutation_rules.value()
                                    : nullptr;
            if (auto status = ApplyHeaderMutations(response_trailers->mutation,
                                                   rules, *metadata);
                !status.ok()) {
              return status;
            }
          }
          if (!call->IsFailOpenAllowed() && call->IsStreamClosed()) {
            return call->GetStreamStatus();
          }
          call->ext_proc_filter_->RecordServerTrailersDuration(
              (Timestamp::Now() - start_time).seconds());
          // Forward final server trailing metadata to client.
          call->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
          return absl::OkStatus();
        });
  }

  RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
};

// Main dispatcher and orchestrator for server trailing metadata. Distinguishes
// between "trailers-only" RPCs and normal RPCs and coordinates
// ServerToExtProcTrailingMetadataProcessor and
// ExtProcToClientTrailingMetadataProcessor.
class ServerTrailingMetadataProcessor {
 public:
  explicit ServerTrailingMetadataProcessor(
      RefCountedPtr<ExtProcFilter::ExtProcCall> call)
      : call_(call),
        server_to_extproc_(call),
        extproc_to_client_(std::move(call)) {}

  absl::AnyInvocable<Poll<absl::Status>()> Process() {
    if (call_->IsStreamClosed() || call_->ext_proc_stream_half_closed()) {
      absl::Status error = call_->GetStreamStatus();
      if (!error.ok() && !call_->IsFailOpenAllowed()) {
        return Immediate(error);
      }
    }
    if (call_->is_trailers_only()) {
      return server_to_extproc_.TrailersOnly(extproc_to_client_);
    }
    return server_to_extproc_.NormalTrailers(extproc_to_client_);
  }

 private:
  RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
  ServerToExtProcTrailingMetadataProcessor server_to_extproc_;
  ExtProcToClientTrailingMetadataProcessor extproc_to_client_;
};

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientToServerMessages(
    ::google_protobuf_Struct* attributes) {
  return ClientToServerMessageProcessor(Ref(), attributes).Process();
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerInitialMetadata() {
  return ServerInitialMetadataProcessor(Ref()).Process();
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerToClientMessages() {
  return ServerToClientMessageProcessor(Ref()).Process();
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerTrailingMetadata() {
  return ServerTrailingMetadataProcessor(Ref()).Process();
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::Call() {
  return ClientInitialMetadataProcessor(Ref()).Process();
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
  if (filter_args.config() == nullptr) {
    return absl::InvalidArgumentError("ext_proc filter config is missing");
  }
  if (filter_args.config()->type() != Config::Type()) {
    return absl::InternalError("ext_proc filter config has wrong type");
  }
  auto config = filter_args.config().TakeAsSubclass<const Config>();
  return MakeRefCounted<ExtProcFilter>(args, std::move(config));
}

ExtProcFilter::ExtProcFilter(const ChannelArgs& args,
                             RefCountedPtr<const Config> config)
    : config_(std::move(config)),
      event_engine_(
          args.GetObjectRef<grpc_event_engine::experimental::EventEngine>()),
      default_authority_(Slice::FromCopiedString(
          args.GetString(GRPC_ARG_DEFAULT_AUTHORITY)
              .value_or(
                  CoreConfiguration::Get()
                      .resolver_registry()
                      .GetDefaultAuthority(
                          args.GetString(GRPC_ARG_SERVER_URI).value_or(""))))),
      target_(args.GetString(GRPC_ARG_SERVER_URI).value_or("")),
      is_client_(true),
      stats_plugin_group_(
          args.GetObjectRef<GlobalStatsPluginRegistry::StatsPluginGroup>()) {}

void ExtProcFilter::RecordClientHeadersDuration(double duration_seconds) const {
  if (stats_plugin_group_ != nullptr && is_client_) {
    stats_plugin_group_->RecordHistogram(
        kMetricClientExtProcClientHeadersDuration, duration_seconds, {target_},
        {});
  }
}

void ExtProcFilter::RecordClientHalfCloseDuration(
    double duration_seconds) const {
  if (stats_plugin_group_ != nullptr && is_client_) {
    stats_plugin_group_->RecordHistogram(
        kMetricClientExtProcClientHalfCloseDuration, duration_seconds,
        {target_}, {});
  }
}

void ExtProcFilter::RecordServerHeadersDuration(double duration_seconds) const {
  if (stats_plugin_group_ != nullptr && is_client_) {
    stats_plugin_group_->RecordHistogram(
        kMetricClientExtProcServerHeadersDuration, duration_seconds, {target_},
        {});
  }
}

void ExtProcFilter::RecordServerTrailersDuration(
    double duration_seconds) const {
  if (stats_plugin_group_ != nullptr && is_client_) {
    stats_plugin_group_->RecordHistogram(
        kMetricClientExtProcServerTrailersDuration, duration_seconds, {target_},
        {});
  }
}

void ExtProcFilter::InterceptCall(UnstartedCallHandler unstarted_call_handler) {
  if (!IsProcessingEnabled(config_->processing_mode)) {
    PassThrough(std::move(unstarted_call_handler));
    return;
  }
  CallHandler handler = Consume(std::move(unstarted_call_handler));
  handler.SpawnGuarded(
      "ext_proc_call",
      [handler, ext_proc_filter = RefAsSubclass<ExtProcFilter>()]() mutable
          -> ArenaPromise<absl::Status> {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: InterceptCall promise chain start";
        auto transport = ext_proc_filter->channel()->transport();
        if (transport == nullptr) {
          return ArenaPromise<absl::Status>([]() -> Poll<absl::Status> {
            return absl::InternalError("ExtProc channel transport is null");
          });
        }
        auto ext_proc_call = MakeRefCounted<ExtProcCall>(
            ext_proc_filter, std::move(transport), handler);
        return ArenaPromise<absl::Status>(ext_proc_call->Call());
      });
}

}  // namespace grpc_core
