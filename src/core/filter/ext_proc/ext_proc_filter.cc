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

#include <string>
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/filter/ext_proc/ext_proc_messages.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/debug/trace_impl.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/observable.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/streaming_call_promise_wrapper.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {

namespace {

class ExtProcTelemetryDomain final
    : public InstrumentDomain<ExtProcTelemetryDomain> {
 public:
  using Backend = HighContentionBackend;
  static constexpr absl::string_view kName = "client_ext_proc";
  GRPC_INSTRUMENT_DOMAIN_LABELS("target");

  static HistogramHandle<ExponentialHistogramShape> kClientHeadersDuration;
  static HistogramHandle<ExponentialHistogramShape> kClientHalfCloseDuration;
  static HistogramHandle<ExponentialHistogramShape> kServerHeadersDuration;
  static HistogramHandle<ExponentialHistogramShape> kServerTrailersDuration;
};

ExtProcTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcTelemetryDomain::kClientHeadersDuration =
        ExtProcTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.client_ext_proc.client_headers_duration",
            "Time between when the ext_proc filter sees the client's headers "
            "and when it allows those headers to continue on to the next "
            "filter.",
            "s", 60, 20);

ExtProcTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcTelemetryDomain::kClientHalfCloseDuration =
        ExtProcTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.client_ext_proc.client_half_close_duration",
            "Time between when the ext_proc filter sees the client's "
            "half-close and when it allows that half-close to continue on to "
            "the next filter.",
            "s", 60, 20);

ExtProcTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcTelemetryDomain::kServerHeadersDuration =
        ExtProcTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.client_ext_proc.server_headers_duration",
            "Time between when the ext_proc filter sees the server's headers "
            "and when it allows those headers to continue on to the next "
            "filter.",
            "s", 60, 20);

ExtProcTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcTelemetryDomain::kServerTrailersDuration =
        ExtProcTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.client_ext_proc.server_trailers_duration",
            "Time between when the ext_proc filter sees the server's "
            "trailers and when it allows those trailers to continue on to "
            "the next filter.",
            "s", 60, 20);

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
    const std::optional<HeaderMutationRules>& rules,
    grpc_metadata_batch& metadata) {
  const auto* rules_ptr = rules.has_value() ? &rules.value() : nullptr;
  for (const auto& remove : mutations.remove_headers) {
    auto status = ApplyXdsHeaderMutationsRemoval(remove, rules_ptr, metadata);
    if (!status.ok()) {
      return status;
    }
  }
  for (const auto& add : mutations.set_headers) {
    auto status = ApplyXdsHeaderMutationsAddition(add, rules_ptr, metadata);
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
    GrpcXdsServerTarget server,
    RefCountedPtr<XdsTransportFactory::XdsTransport> transport)
    : server_(std::move(server)), transport_(std::move(transport)) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "creating channel " << this << " for server " << server_.server_uri();
}

ExtProcFilter::ExtProcChannel::~ExtProcChannel() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "destroying ext_proc channel " << this << " for server "
      << server_.server_uri();
}

//
// ExtProcFilter::ExtProcCall
//

// High-Level Architecture of 3 Concurrent Pipeline Loops across 2 Activities:
//
//  [ Activity 1: handler_ ]
//  LOOP 1: Read-From-Client Pipeline Loop [HandleReadFromClientLoop()]
//  +-----------------------------------------------------------------------+
//  | TrySeq(                                                               |
//  |     ProcessClientInitialMetadataFromClient(),                         |
//  |     ProcessClientMessagesFromClient())                                |
//  +-----------------------------------------------------------------------+
//                                     ||
//                     Joined via TryJoin() in Run()
//                                     ||
//  LOOP 2: Side-Stream Pull Pipeline Loop [HandleReadFromSideStreamLoop()]
//  +-----------------------------------------------------------------------+
//  | HandleReadFromSideStreamLoop()                                         |
//  |   -> Loop: streaming_call_->PullMessage()                             |
//  |   -> ProcessSideStreamResponse() (dispatches responses/mutations)     |
//  |   -> HandleSideStreamStatus(status)                                   |
//  +-----------------------------------------------------------------------+
//
//  [ Activity 2: initiator_ ] (Spawned on child call startup)
//  LOOP 3: Read-From-Server Response Pipeline Loop [SpawnReadFromServerLoop()]
//  +-----------------------------------------------------------------------+
//  | PrioritizedRace(                                                      |
//  |     watch_error,                                                      |
//  |     TrySeq(ProcessServerInitialMetadataFromServer(),                  |
//  |            ProcessServerMessagesFromServer(),                         |
//  |            ProcessServerTrailingMetadataFromServer()))                |
//  +-----------------------------------------------------------------------+

class ExtProcFilter::ExtProcCall final
    : public InternallyRefCounted<ExtProcCall> {
 public:
  ExtProcCall(RefCountedPtr<ExtProcFilter> ext_proc_filter,
              RefCountedPtr<XdsTransportFactory::XdsTransport> transport,
              CallHandler handler);

  ~ExtProcCall() override;

  // Main entry point for an external processor call. Spawns and manages the
  // concurrent request (client-to-server) and response (server-to-client)
  // processing pipelines alongside side-stream message pulling.
  ArenaPromise<absl::Status> Run();

 private:
  // Tracks the state of outgoing message sends on the ext_proc side-stream.
  enum class SendState {
    // Initial state: no message send is currently in flight.
    kIdle,
    // A message send operation has been claimed and is currently in flight on
    // the underlying transport wrapper.
    kSendInFlight,
    // The stream has been closed or terminated due to an error, preventing
    // subsequent sends.
    kSendFailed,
  };
  // Spawns the read-from-client loop on handler_.
  // Called when the ExtProcCall is created.
  //
  // Handles client initial metadata, client messages, and half-close.
  // Sends each event to the ext_proc side-stream and/or to the server,
  // based on the configuration.
  ArenaPromise<StatusFlag> HandleReadFromClientLoop();

  // Handle the read-from-ext_proc-side-stream loop on handler_.
  // Called when the ExtProcCall is created.
  //
  // Handles all events on the call, forwarding data to either handler_
  // (client) or initiator_ (server).
  ArenaPromise<StatusFlag> HandleReadFromSideStreamLoop();

  // Spawns the read-from-server loop on initiator_.
  // Called from StartChildCall().
  //
  // Handles server initial metadata, server messages, and server
  // trailing metadata. Sends each event to the ext_proc side-stream
  // and/or to the client, based on the configuration.
  void SpawnReadFromServerLoop();

  // Read-from-client event handlers
  ArenaPromise<StatusFlag> HandleInitialMetadataFromClient(
      ClientMetadataHandle metadata);
  ArenaPromise<StatusFlag> HandleMessageFromClient(MessageHandle message);
  ArenaPromise<StatusFlag> HandleHalfCloseFromClient();

  // Read-from-server event handlers
  ArenaPromise<StatusFlag> HandleInitialMetadataFromServer(
      std::optional<ServerMetadataHandle> metadata);
  ArenaPromise<StatusFlag> HandleMessageFromServer(MessageHandle message);
  ArenaPromise<StatusFlag> HandleTrailingMetadataFromServer(
      ServerMetadataHandle metadata);

  // Read-from-sidestream event handlers
  StatusFlag HandleClientInitialMetadataFromSidestream(
      const ExtProcResponse::RequestHeaders& response);
  StatusFlag HandleClientMessageFromSidestream(
      const ExtProcResponse::RequestBody& response);
  ArenaPromise<StatusFlag> HandleServerInitialMetadataFromSidestream(
      const ExtProcResponse::ResponseHeaders& response);
  StatusFlag HandleServerMessageFromSidestream(
      const ExtProcResponse::ResponseBody& response);
  ArenaPromise<StatusFlag> HandleServerTrailingMetadataFromSidestream(
      const ExtProcResponse::ResponseTrailers& response);
  ArenaPromise<StatusFlag> HandleImmediateResponseFromSidestream(
      const ExtProcResponse::ImmediateResponse& response);

  // Initializes and starts the child call to the backend server, and spawns
  // the background task for the server-to-client response path.
  void StartChildCall(ClientMetadataHandle metadata,
                      ::google_protobuf_Struct* attributes = nullptr,
                      Timestamp start_time = Timestamp::InfPast());

  // Prepares the ProcessingRequest protobuf message for client initial
  // metadata and sends it over the ext_proc stream.
  ArenaPromise<StatusFlag> SendClientInitialMetadataRequest(
      const ClientMetadataHandle& metadata,
      absl::string_view default_authority);
  // Prepares the ProcessingRequest protobuf message for client body messages.
  ArenaPromise<StatusFlag> SendClientMessageRequest(
      const MessageHandle& message, bool end_of_stream,
      bool end_of_stream_without_message);
  // Prepares the ProcessingRequest protobuf message for server initial
  // metadata and sends it over the ext_proc stream.
  ArenaPromise<StatusFlag> SendServerInitialMetadataRequest(
      const ServerMetadataHandle& metadata, bool end_of_stream = false);
  // Prepares the ProcessingRequest protobuf message for server response body
  // and sends it over the ext_proc stream.
  ArenaPromise<StatusFlag> SendServerMessageRequest(
      const MessageHandle& message);
  // Prepares the ProcessingRequest protobuf message for server trailing
  // metadata (trailers) and sends it over the ext_proc stream.
  ArenaPromise<StatusFlag> SendServerTrailingMetadataRequest(
      const ServerMetadataHandle& metadata);

  // Sends a message to the external processor side-stream.
  // Coordinates client-side and server-side message sources so that only one
  // send is in-flight on streaming_call_ at a time, using a single Waker
  // without any queue or vector allocations.
  ArenaPromise<StatusFlag> SendMessageToSideStream(std::string payload);

  // Parses and processes an incoming response message payload from the
  // side-stream.
  ArenaPromise<StatusFlag> ProcessSideStreamResponse(absl::string_view payload);

  // Handles transport status updates/closure on the ext_proc side-stream.
  void HandleSideStreamStatus(absl::Status status);

  const Config& config() const { return *ext_proc_filter_->config_; }

  const ProcessingMode& processing_mode() const {
    return *config().processing_mode;
  }

  bool IsFirstMessageOnStream() {
    return std::exchange(is_first_message_on_ext_proc_stream_, false);
  }

  bool IsFailOpenAllowed() const {
    const bool allow = config().failure_mode_allow.value_or(false);
    if (config().observability_mode) return allow;
    return allow && !first_body_message_sent_;
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

  bool DecrementOutstandingClientToServerMessages() {
    if (outstanding_c2s_messages_ == 0) {
      return false;
    }
    outstanding_c2s_messages_--;
    return true;
  }

  bool IsStreamClosed() const { return stream_status_.has_value(); }

  // Returns true if the stream closed with an error and fail-open mode is not
  // permitted for this call (i.e. the stream error must fail the RPC).
  bool IsStreamFailureFatal() const {
    if (IsFailOpenAllowed()) return false;
    return stream_status_.has_value() && !stream_status_->ok();
  }

  // Evaluates the status to return when the external processor stream is
  // closed or when a send fails. Respects IsFailOpenAllowed() (which handles
  // both failure_mode_allow and observability_mode) by returning OkStatus()
  // when fail-open is permitted.
  absl::Status GetStreamClosedStatus(
      absl::Status default_error = absl::CancelledError("Stream closed")) {
    if (stream_status_.has_value()) {
      return *stream_status_;
    }
    if (IsFailOpenAllowed()) {
      return absl::OkStatus();
    }
    return default_error;
  }

  auto WaitForStreamStatus() {
    return Map(drain_closed_.NextWhen([](bool closed) { return closed; }),
               [self = Ref()](bool) { return self->GetStreamClosedStatus(); });
  }

  void SetStreamError(absl::Status status) {
    if (!status.ok()) {
      auto error_md = CancelledServerMetadataFromStatus(status);
      handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
      if (initiator_.is_set()) {
        initiator_.SpawnCancel();
      }
    }
    if (!IsStreamClosed()) {
      stream_status_ = status;
      drain_closed_.Set(true);
    }
  }

  void CloseStream() {
    if (!IsStreamClosed()) {
      stream_status_ = absl::OkStatus();
      drain_closed_.Set(true);
    }
    auto streaming_call = std::move(streaming_call_);
    ext_proc_send_waker_.Wakeup();
    streaming_call.reset();
  }

  void Orphan() override {
    CloseStream();
    Unref();
  }

  void CompleteOutstandingProcessors();

  // Flags tracking whether the respective ext_proc response messages have
  // been received from the external processor.
  bool request_headers_received_ = false;
  bool response_headers_received_ = false;
  bool response_trailers_received_ = false;

  // Client initial metadata stored during request header processing.
  ClientMetadataHandle client_initial_metadata_;
  InterActivityLatch<ServerMetadataHandle> server_initial_metadata_latch_;
  InterActivityLatch<ServerMetadataHandle> server_trailing_metadata_latch_;

  // Request attributes generated during request header processing to be
  // attached to subsequent request body processing requests.
  ::google_protobuf_Struct* request_attributes_ = nullptr;
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
  bool half_close_initiated_ = false;
  bool is_trailers_only_ = false;
  bool server_trailers_sent_ = false;
  // Set by external processor server when it requests end of stream (EOS).
  bool ext_proc_set_eos_ = false;
  // Indicates that the external processor stream has been half closed.
  bool ext_proc_stream_half_closed_ = false;
  std::optional<absl::Status> stream_status_;
  mutable Observable<bool> drain_closed_{false};

  // Atomic send state for lock-free coordination between client-side and
  // server-side message senders.
  SendState ext_proc_send_state_ = SendState::kIdle;
  // Waker for queuing send promises when a send operation is already in flight.
  Waker ext_proc_send_waker_;
  CallHandler handler_;
  CallInitiator initiator_;
  RefCountedPtr<XdsStreamingCallPromiseWrapper> streaming_call_;
  RefCountedPtr<ExtProcFilter> ext_proc_filter_;
};

ExtProcFilter::ExtProcCall::ExtProcCall(
    RefCountedPtr<ExtProcFilter> ext_proc_filter,
    RefCountedPtr<XdsTransportFactory::XdsTransport> transport,
    CallHandler handler)
    : handler_(handler), ext_proc_filter_(std::move(ext_proc_filter)) {
  const char* method = "/envoy.service.ext_proc.v3.ExternalProcessor/Process";
  streaming_call_ = MakeRefCounted<XdsStreamingCallPromiseWrapper>(
      *transport, method, /*wait_for_ready=*/false);
}

ExtProcFilter::ExtProcCall::~ExtProcCall() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " destroyed";
  if (config().deferred_close_timeout != Duration::Zero() &&
      config().observability_mode) {
    ext_proc_filter_->event_engine_->RunAfter(
        config().deferred_close_timeout,
        [call = std::move(streaming_call_)]() mutable { call.reset(); });
  } else {
    streaming_call_.reset();
  }
}

ArenaPromise<StatusFlag> ExtProcFilter::ExtProcCall::ProcessSideStreamResponse(
    absl::string_view payload) {
  // In observability mode, we only log the message and ignore it.
  // We must continue reading the stream to keep it alive.
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this
        << " message received in observability mode (ignored), size="
        << payload.size();
    return Immediate(StatusFlag(Success{}));
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " message received, size=" << payload.size();
  // Parse the response from the external processor.
  auto parsed_response = ExtProcResponse::Parse(payload);
  if (!parsed_response.ok()) {
    SetStreamError(parsed_response.status());
    return Immediate(StatusFlag(Failure{}));
  }
  // If the server requests a drain, we half-close the stream to signal
  // we are done sending requests.
  if ((*parsed_response).request_drain) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this << " received request_drain=true";
    drain_requested_ = true;
    ext_proc_stream_half_closed_ = true;
    if (streaming_call_ != nullptr) {
      GRPC_TRACE_LOG(ext_proc_filter, INFO)
          << "ExtProcCall " << this << " sending half-close";
      streaming_call_->SendHalfClose();
    }
  }
  // Dispatch the parsed response to the appropriate processor based on the
  // response type.
  return Match(
      (*parsed_response).response,
      [&](const ExtProcResponse::ImmediateResponse& response)
          -> ArenaPromise<StatusFlag> {
        return HandleImmediateResponseFromSidestream(response);
      },
      [&](const ExtProcResponse::RequestHeaders& response)
          -> ArenaPromise<StatusFlag> {
        return Immediate(HandleClientInitialMetadataFromSidestream(response));
      },
      [&](const ExtProcResponse::ResponseHeaders& response)
          -> ArenaPromise<StatusFlag> {
        return HandleServerInitialMetadataFromSidestream(response);
      },
      [&](const ExtProcResponse::ResponseTrailers& response)
          -> ArenaPromise<StatusFlag> {
        return HandleServerTrailingMetadataFromSidestream(response);
      },
      [&](const ExtProcResponse::RequestBody& response)
          -> ArenaPromise<StatusFlag> {
        return Immediate(HandleClientMessageFromSidestream(response));
      },
      [&](const ExtProcResponse::ResponseBody& response)
          -> ArenaPromise<StatusFlag> {
        return Immediate(HandleServerMessageFromSidestream(response));
      },
      [](std::monostate) -> ArenaPromise<StatusFlag> {
        return Immediate(StatusFlag(Success{}));
      });
}

void ExtProcFilter::ExtProcCall::HandleSideStreamStatus(absl::Status status) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " status received: " << status;
  const bool has_outstanding_messages =
      outstanding_c2s_messages_ > 0 || outstanding_s2c_messages_ > 0;
  const bool must_drain =
      !config().observability_mode && (processing_mode().send_request_body ||
                                       processing_mode().send_response_body);
  // Check if a clean stream closure violated draining or message-in-flight
  // requirements.
  if (status.ok()) {
    if (must_drain && !drain_requested_) {
      status = absl::InternalError("Stream closed cleanly without drain");
    }
    // TODO(rishesh): removed this check once PH2 work is done
    else if (has_outstanding_messages && !config().observability_mode) {
      status = absl::InternalError(
          "Stream closed cleanly with outstanding messages");
    }
  }
  const bool should_propagate_error = !status.ok() && !IsFailOpenAllowed();
  // Ensure stream status recording, error propagation, and teardown run
  // idempotently once.
  if (!IsStreamClosed()) {
    stream_status_ = should_propagate_error ? status : absl::OkStatus();
    drain_closed_.Set(true);
    if (should_propagate_error) {
      // On fatal error, push error trailing metadata and cancel child call.
      auto error_md = CancelledServerMetadataFromStatus(status);
      handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
      if (initiator_.is_set()) {
        initiator_.SpawnCancel();
      }
    } else {
      // On clean close or fail-open, complete pending processors normally.
      CompleteOutstandingProcessors();
    }
    CloseStream();
  }
}

void ExtProcFilter::ExtProcCall::CompleteOutstandingProcessors() {
  if (processing_mode().send_request_headers && !request_headers_received_ &&
      client_initial_metadata_ != nullptr) {
    (void)HandleClientInitialMetadataFromSidestream(
        ExtProcResponse::RequestHeaders{});
  }
  if (processing_mode().send_response_headers && !response_headers_received_ &&
      (server_initial_metadata_latch_.IsSet() ||
       (is_trailers_only_ && server_trailing_metadata_latch_.IsSet()))) {
    auto promise = HandleServerInitialMetadataFromSidestream(
        ExtProcResponse::ResponseHeaders{});
    (void)promise();
  }
  if (processing_mode().send_response_trailers && !is_trailers_only_ &&
      !response_trailers_received_ && server_trailing_metadata_latch_.IsSet()) {
    auto promise = HandleServerTrailingMetadataFromSidestream(
        ExtProcResponse::ResponseTrailers{});
    (void)promise();
  }
}

// This function role is to:
// - send the message to the ext proc server and wait for the send to get
// complete and then propagate the status
// - if a message is already in progress then wait for the in flight message to
// get complete and then send the previous one if the stream is not closed
// - Handle the failure mode allow
ArenaPromise<StatusFlag> ExtProcFilter::ExtProcCall::SendMessageToSideStream(
    std::string payload) {
  return Seq(
      // Wait until send state is kIdle, then mark kSendInFlight.
      [self = Ref()]() -> Poll<StatusFlag> {
        if (self->ext_proc_send_state_ == SendState::kSendFailed) {
          return Failure{};
        }
        if (self->ext_proc_send_state_ != SendState::kIdle) {
          self->ext_proc_send_waker_ =
              GetContext<Activity>()->MakeNonOwningWaker();
          return Pending{};
        }
        self->ext_proc_send_state_ = SendState::kSendInFlight;
        return Success{};
      },
      // Safely acquire streaming_call_ and push the payload.
      [self = Ref(), payload = std::move(payload)](
          StatusFlag status) mutable -> ArenaPromise<StatusFlag> {
        if (!status.ok()) {
          return Immediate(StatusFlag(Failure{}));
        }
        // CloseStream() moves out and resets streaming_call_, so it may be
        // null if the side-stream closed while this send was queued or
        // executing.
        if (self->streaming_call_ == nullptr) {
          return Immediate(StatusFlag(Failure{}));
        }
        return self->streaming_call_->PushMessage(std::move(payload));
      },
      // Reset send state and wake up any waiting senders.
      [self = Ref()](StatusFlag status) -> ArenaPromise<StatusFlag> {
        self->ext_proc_send_state_ =
            status.ok() ? SendState::kIdle : SendState::kSendFailed;
        self->ext_proc_send_waker_.Wakeup();
        return Immediate(StatusFlag(Success{}));
      });
}

// Handles the response path (Server to Client).
ArenaPromise<absl::Status> ExtProcFilter::ExtProcCall::Run() {
  return Map(TryJoin<absl::StatusOr>(HandleReadFromClientLoop(),
                                     HandleReadFromSideStreamLoop()),
             [self = Ref()](
                 absl::StatusOr<std::tuple<Empty, Empty>> res) -> absl::Status {
               GRPC_TRACE_LOG(ext_proc_filter, INFO)
                   << "ExtProcCall " << self.get()
                   << " Run() finished with status: " << res.ok();
               return self->GetStreamClosedStatus();
             });
}

// Handle the read-from-client loop on handler_.
// Called when the ExtProcCall is created.
//
// Handles client initial metadata, client messages, and half-close.
// Sends each event to the ext_proc side-stream and/or to the server,
// based on the configuration.
ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleReadFromClientLoop() {
  return Seq(
      TrySeq(
          handler_.PullClientInitialMetadata(),
          [self = Ref()](ClientMetadataHandle metadata) {
            return self->HandleInitialMetadataFromClient(std::move(metadata));
          },
          [self = Ref()]() {
            return ForEach(
                MessagesFrom(self->handler_), [self](MessageHandle message) {
                  return self->HandleMessageFromClient(std::move(message));
                });
          },
          [self = Ref()]() { return self->HandleHalfCloseFromClient(); }),
      [self = Ref()](StatusFlag status) -> StatusFlag {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProcCall " << self.get()
            << " read-from-client loop finished with status: " << status.ok();
        return status;
      });
}

// Continuously pulls response messages from the external processor side-stream
// and dispatches them until the stream closes or an error occurs.
ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleReadFromSideStreamLoop() {
  return Seq(
      // Loop reading response messages from the side-stream until end-of-stream
      // or error.
      Loop([self = Ref()]() -> Promise<LoopCtl<StatusFlag>> {
        // CloseStream() moves out and resets streaming_call_, so it may be null
        // if the side-stream was closed while this loop was running. If so,
        // terminate the read loop cleanly.
        if (self->streaming_call_ == nullptr) {
          return Immediate(LoopCtl<StatusFlag>(Success{}));
        }
        return Seq(
            // Pull the next response message from the streaming call.
            self->streaming_call_->PullMessage(),
            // Process the message; stop loop if end-of-stream (nullopt) or
            // error.
            [self](std::optional<std::string> msg)
                -> Promise<LoopCtl<StatusFlag>> {
              if (!msg.has_value()) {
                return Immediate(LoopCtl<StatusFlag>(Success{}));
              }
              return Seq(self->ProcessSideStreamResponse(std::move(*msg)),
                         [](StatusFlag status) -> LoopCtl<StatusFlag> {
                           if (!status.ok()) return Failure{};
                           return Continue();
                         });
            });
      }),
      // Once message loop ends, pull trailing metadata from the stream.
      [self = Ref()](StatusFlag) -> Promise<absl::Status> {
        if (self->streaming_call_ == nullptr) {
          return Immediate(absl::InternalError("Side stream unavailable"));
        }
        return self->streaming_call_->PullServerTrailingMetadata();
      },
      // Handle stream closure and resolve final status.
      [self = Ref()](absl::Status status) -> StatusFlag {
        self->HandleSideStreamStatus(status);
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProcCall " << self.get()
            << " HandleReadFromSideStreamLoop finished with status: " << status;
        return StatusFlag(status.ok() || self->IsFailOpenAllowed());
      });
}

// Handles the response path (Server to Client).
// This function sets up a pipeline to process server initial metadata,
// response messages, and server trailing metadata, potentially intercepting
// and mutating them via the ext_proc server.
//
// It also watches for ext_proc stream errors and aborts the call if a failure
// occurs and fail-open is not allowed.
void ExtProcFilter::ExtProcCall::SpawnReadFromServerLoop() {
  initiator_.SpawnGuarded("read_from_server", [self = Ref()]() {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: read_from_server task started";
    return TrySeq(
        self->initiator_.PullServerInitialMetadata(),
        [self](std::optional<ServerMetadataHandle> metadata) {
          return self->HandleInitialMetadataFromServer(std::move(metadata));
        },
        [self]() -> ArenaPromise<StatusFlag> {
          if (self->is_trailers_only_) {
            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                << "ExtProc: Skipping server message processing (trailers-only "
                   "response)";
            return Immediate(StatusFlag(Success{}));
          }
          return Seq(ForEach(MessagesFrom(self->initiator_),
                             [self](MessageHandle message) {
                               return self->HandleMessageFromServer(
                                   std::move(message));
                             }),
                     [self]() {
                       self->s2c_writes_done_ = true;
                       return StatusFlag(Success{});
                     });
        },
        [self]() {
          return Seq(self->initiator_.PullServerTrailingMetadata(),
                     [self](ServerMetadataHandle metadata) {
                       return self->HandleTrailingMetadataFromServer(
                           std::move(metadata));
                     });
        });
  });
}

//
// Read-from-client Event Handlers
//

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleInitialMetadataFromClient(
    ClientMetadataHandle metadata) {
  if (!processing_mode().send_request_headers) {
    // If request header processing is disabled, forward metadata directly
    // without calling ext_proc.
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Skipping client initial metadata (processing mode "
           "disabled)";
    ::google_protobuf_Struct* attributes = nullptr;
    // If request body will be sent later and request attributes are configured,
    // extract initial attributes from client metadata.
    if (processing_mode().send_request_body &&
        !config().request_attributes.empty()) {
      auto* arena = handler_.arena()->New<upb::Arena>();
      attributes = CreateExtProcAttributesProtoStruct(
          arena->ptr(), config().request_attributes, *metadata,
          ext_proc_filter_->default_authority_.as_string_view());
    }
    request_attributes_ = attributes;
    StartChildCall(std::move(metadata), attributes);
    return Immediate(StatusFlag(Success{}));
  }
  if (config().observability_mode) {
    // In observability mode, send request headers to ext_proc
    // asynchronously without awaiting a response.
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Sending client initial metadata (observability mode)";
    Timestamp start_time = Timestamp::Now();
    return Seq(
        SendClientInitialMetadataRequest(
            metadata, ext_proc_filter_->default_authority_.as_string_view()),
        [self = Ref(), metadata = std::move(metadata),
         start_time](StatusFlag) mutable -> ArenaPromise<StatusFlag> {
          self->StartChildCall(std::move(metadata), /*attributes=*/nullptr,
                               start_time);
          return Immediate(StatusFlag(Success{}));
        });
  }
  // In normal mode, send request headers to ext_proc and store metadata
  // until the response arrives.
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Sending client initial metadata (normal mode)";
  auto send_promise = SendClientInitialMetadataRequest(
      metadata, ext_proc_filter_->default_authority_.as_string_view());
  client_initial_metadata_ = std::move(metadata);
  return send_promise;
}

void ExtProcFilter::ExtProcCall::StartChildCall(
    ClientMetadataHandle metadata, ::google_protobuf_Struct* attributes,
    Timestamp start_time) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Starting downstream child call";
  if (start_time != Timestamp::InfPast()) {
    ext_proc_filter_->RecordClientHeadersDuration(
        (Timestamp::Now() - start_time).seconds());
  }
  initiator_ = ext_proc_filter_->MakeChildCall(std::move(metadata),
                                               handler_.arena()->Ref());
  handler_.AddChildCall(initiator_);
  SpawnReadFromServerLoop();
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::SendClientInitialMetadataRequest(
    const ClientMetadataHandle& metadata, absl::string_view default_authority) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Sending client initial metadata request to side-stream";
  // Include processing mode in the request if this is the first message on the
  // stream.
  std::optional<ExtProcProcessingMode> processing_mode;
  if (IsFirstMessageOnStream()) {
    processing_mode = config().processing_mode;
  }
  upb::Arena arena;
  auto* header_attributes = CreateExtProcAttributesProtoStruct(
      arena.ptr(), config().request_attributes, *metadata, default_authority);
  auto payload = CreateExtProcClientHeadersRequest(
      arena.ptr(), metadata.get(), config().forwarding_allowed_headers,
      config().forwarding_disallowed_headers, header_attributes,
      config().observability_mode, processing_mode);
  if (!payload.ok()) {
    return Immediate(StatusFlag(Failure{}));
  }
  // Send the serialized request payload over the side-stream.
  return SendMessageToSideStream(std::move(*payload));
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleInitialMetadataFromServer(
    std::optional<ServerMetadataHandle> metadata) {
  if (!metadata.has_value()) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: No server initial metadata (trailers-only response)";
    is_trailers_only_ = true;
    return Immediate(StatusFlag(Success{}));
  }
  if (!processing_mode().send_response_headers || IsStreamClosed()) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Skipping server initial metadata (processing disabled "
           "or stream closed)";
    if (IsStreamFailureFatal()) {
      return Immediate(StatusFlag(Failure{}));
    }
    handler_.SpawnPushServerInitialMetadata(std::move(*metadata));
    return Immediate(StatusFlag(Success{}));
  }
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Sending server initial metadata (observability mode)";
    Timestamp start_time = Timestamp::Now();
    return Seq(
        SendServerInitialMetadataRequest(*metadata),
        [self = Ref(), metadata = std::move(*metadata),
         start_time](StatusFlag) mutable -> ArenaPromise<StatusFlag> {
          self->ext_proc_filter_->RecordServerHeadersDuration(
              (Timestamp::Now() - start_time).seconds());
          self->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
          return Immediate(StatusFlag(Success{}));
        });
  }
  if (drain_requested_) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Handling server initial metadata in drain mode";
    return Map(
        WaitForStreamStatus(),
        [self = Ref(), metadata = std::move(*metadata)](
            absl::Status status) mutable -> StatusFlag {
          if (self->IsStreamFailureFatal()) {
            return Failure{};
          }
          self->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
          return Success{};
        });
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Sending server initial metadata (normal mode)";
  auto send_promise = SendServerInitialMetadataRequest(*metadata);
  server_initial_metadata_latch_.Set(std::move(*metadata));
  return send_promise;
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::SendServerInitialMetadataRequest(
    const ServerMetadataHandle& metadata, bool end_of_stream) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Sending server initial metadata request to side-stream";
  if (IsStreamClosed() || ext_proc_stream_half_closed_) {
    return Immediate(StatusFlag(Success{}));
  }
  // Include processing mode if this is the first message on the stream.
  std::optional<ExtProcProcessingMode> processing_mode;
  if (IsFirstMessageOnStream()) {
    processing_mode = config().processing_mode;
  }
  upb::Arena arena;
  auto payload = CreateExtProcServerHeadersRequest(
      arena.ptr(), metadata.get(), config().forwarding_allowed_headers,
      config().forwarding_disallowed_headers,
      /*attributes=*/nullptr, config().observability_mode, processing_mode,
      end_of_stream);
  if (!payload.ok()) {
    return Immediate(StatusFlag(Failure{}));
  }
  return SendMessageToSideStream(std::move(*payload));
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleTrailingMetadataFromServer(
    ServerMetadataHandle metadata) {
  if (IsStreamFailureFatal()) {
    return Immediate(StatusFlag(Failure{}));
  }
  // If trailing status is not OK (e.g. error from downstream), pass
  // trailers through directly.
  if (!IsStatusOk(*metadata)) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Passing through non-OK server trailing metadata";
    handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
    return Immediate(StatusFlag(Success{}));
  }
  const bool send_metadata = is_trailers_only_
                                 ? processing_mode().send_response_headers
                                 : processing_mode().send_response_trailers;
  if (!send_metadata || IsStreamClosed()) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Skipping server trailing metadata (processing "
           "disabled or stream closed)";
    if (is_trailers_only_) {
      server_trailing_metadata_latch_.Set(nullptr);
    }
    handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
    return Immediate(StatusFlag(Success{}));
  }
  auto send_request = [self = Ref(), &metadata]() {
    if (self->is_trailers_only_) {
      return self->SendServerInitialMetadataRequest(metadata,
                                                    /*end_of_stream=*/true);
    }
    return self->SendServerTrailingMetadataRequest(metadata);
  };
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Sending server trailing metadata (observability mode)";
    Timestamp start_time = Timestamp::Now();
    return Seq(
        send_request(),
        [self = Ref(), metadata = std::move(metadata),
         start_time](StatusFlag status) mutable -> ArenaPromise<StatusFlag> {
          if (!status.ok() && !self->IsFailOpenAllowed()) {
            return Immediate(StatusFlag(Failure{}));
          }
          if (self->is_trailers_only_) {
            self->ext_proc_filter_->RecordServerHeadersDuration(
                (Timestamp::Now() - start_time).seconds());
          } else {
            self->ext_proc_filter_->RecordServerTrailersDuration(
                (Timestamp::Now() - start_time).seconds());
          }
          self->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
          return Immediate(StatusFlag(Success{}));
        });
  }
  if (drain_requested_) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Handling server trailing metadata in drain mode";
    return Map(
        WaitForStreamStatus(),
        [self = Ref(), metadata = std::move(metadata)](
            absl::Status status) mutable -> StatusFlag {
          if (self->IsStreamFailureFatal()) {
            return Failure{};
          }
          self->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
          return Success{};
        });
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Sending server trailing metadata (normal mode)";
  auto send_promise = send_request();
  server_trailing_metadata_latch_.Set(std::move(metadata));
  return send_promise;
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::SendServerTrailingMetadataRequest(
    const ServerMetadataHandle& metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Sending server trailing metadata request to side-stream";
  if (IsStreamClosed() || ext_proc_stream_half_closed_) {
    return Immediate(StatusFlag(Success{}));
  }
  // Include processing mode if this is the first message on the stream.
  std::optional<ExtProcProcessingMode> processing_mode;
  if (IsFirstMessageOnStream()) {
    processing_mode = config().processing_mode;
  }
  upb::Arena arena;
  auto payload = CreateExtProcServerTrailersRequest(
      arena.ptr(), metadata.get(), config().forwarding_allowed_headers,
      config().forwarding_disallowed_headers,
      /*attributes=*/nullptr, config().observability_mode, processing_mode);
  if (!payload.ok()) {
    return Immediate(StatusFlag(Failure{}));
  }
  return Map(SendMessageToSideStream(std::move(*payload)),
             [self = Ref()](StatusFlag status) {
               if (status.ok()) {
                 self->server_trailers_sent_ = true;
               }
               return status;
             });
}

//
// ExtProcFilter::ExtProcCall Server Message Processing
//

ArenaPromise<StatusFlag> ExtProcFilter::ExtProcCall::HandleMessageFromServer(
    MessageHandle message) {
  const bool send_body =
      processing_mode().send_response_body && !IsStreamClosed();
  if (!send_body) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Server message non-processing mode";
    if (IsStreamFailureFatal()) {
      return Immediate(StatusFlag(Failure{}));
    }
    handler_.SpawnPushMessage(std::move(message));
    return Immediate(StatusFlag(Success{}));
  }
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Server message observability mode";
    if (!IsStreamClosed() && !ext_proc_stream_half_closed_) {
      return Seq(SendServerMessageRequest(message),
                 [self = Ref(), message = std::move(message)](
                     StatusFlag) mutable -> StatusFlag {
                   self->handler_.SpawnPushMessage(std::move(message));
                   return Success{};
                 });
    }
    if (IsStreamFailureFatal()) {
      return Immediate(StatusFlag(Failure{}));
    }
    handler_.SpawnPushMessage(std::move(message));
    return Immediate(StatusFlag(Success{}));
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Server message normal mode";
  if (drain_requested_) {
    return Seq(WaitForStreamStatus(),
               [self = Ref(), message = std::move(message)](
                   absl::Status status) mutable -> StatusFlag {
                 if (!status.ok() && self->IsStreamFailureFatal()) {
                   return Failure{};
                 }
                 self->handler_.SpawnPushMessage(std::move(message));
                 return Success{};
               });
  }
  if (!IsStreamClosed() && !ext_proc_stream_half_closed_) {
    return Seq(SendServerMessageRequest(message),
               [self = Ref(), message = std::move(message)](
                   StatusFlag status) mutable -> StatusFlag {
                 if (!status.ok() || self->IsStreamClosed()) {
                   if (self->IsStreamFailureFatal()) {
                     return Failure{};
                   }
                   self->handler_.SpawnPushMessage(std::move(message));
                 }
                 return Success{};
               });
  }
  if (IsStreamFailureFatal()) {
    return Immediate(StatusFlag(Failure{}));
  }
  handler_.SpawnPushMessage(std::move(message));
  return Immediate(StatusFlag(Success{}));
}

ArenaPromise<StatusFlag> ExtProcFilter::ExtProcCall::SendServerMessageRequest(
    const MessageHandle& message) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Sending server body message request to side-stream";
  if (IsStreamClosed() || ext_proc_stream_half_closed_) {
    return Immediate(StatusFlag(Success{}));
  }
  std::string message_bytes;
  if (message != nullptr) {
    message_bytes = message->payload()->JoinIntoString();
  }
  if (!config().observability_mode) {
    outstanding_s2c_messages_++;
  }
  std::optional<ExtProcProcessingMode> processing_mode;
  if (IsFirstMessageOnStream()) {
    processing_mode = config().processing_mode;
  }
  upb::Arena arena;
  auto payload = CreateExtProcServerBodyRequest(
      arena.ptr(), message_bytes, /*attributes=*/nullptr,
      config().observability_mode, processing_mode);
  if (!payload.ok()) {
    return Immediate(StatusFlag(Failure{}));
  }
  return Map(SendMessageToSideStream(std::move(*payload)),
             [self = Ref()](StatusFlag status) {
               if (status.ok()) {
                 self->first_body_message_sent_ = true;
               }
               return status;
             });
}

ArenaPromise<StatusFlag> ExtProcFilter::ExtProcCall::HandleMessageFromClient(
    MessageHandle message) {
  if (ext_proc_set_eos_) {
    SetStreamError(
        absl::InternalError("Client sends closed by external processor"));
    return Immediate(StatusFlag(Failure{}));
  }
  const bool send_request_body =
      processing_mode().send_request_body && !IsStreamClosed();
  if (!send_request_body) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Client message non-processing mode (processing disabled "
           "or closed)";
    initiator_.SpawnPushMessage(std::move(message));
    return Immediate(StatusFlag(Success{}));
  }
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Client message observability mode";
    if (!IsStreamClosed() && !ext_proc_stream_half_closed_) {
      return Seq(
          SendClientMessageRequest(message,
                                   /*end_of_stream=*/false,
                                   /*end_of_stream_without_message=*/false),
          [self = Ref(),
           message = std::move(message)](StatusFlag) mutable -> StatusFlag {
            self->initiator_.SpawnPushMessage(std::move(message));
            return Success{};
          });
    }
    if (IsStreamFailureFatal()) {
      return Immediate(StatusFlag(Failure{}));
    }
    initiator_.SpawnPushMessage(std::move(message));
    return Immediate(StatusFlag(Success{}));
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Client message normal mode";
  if (drain_requested_) {
    return Seq(WaitForStreamStatus(),
               [self = Ref(), message = std::move(message)](
                   absl::Status status) mutable -> StatusFlag {
                 if (!status.ok() && !self->IsFailOpenAllowed()) {
                   return Failure{};
                 }
                 if (message != nullptr) {
                   self->initiator_.SpawnPushMessage(std::move(message));
                 }
                 return Success{};
               });
  }
  if (!IsStreamClosed() && !ext_proc_stream_half_closed_) {
    return Seq(
        SendClientMessageRequest(message,
                                 /*end_of_stream=*/false,
                                 /*end_of_stream_without_message=*/false),
        [self = Ref(), message = std::move(message)](
            StatusFlag status) mutable -> StatusFlag {
          if (!status.ok() || self->IsStreamClosed()) {
            if (self->IsStreamFailureFatal()) {
              return Failure{};
            }
            self->initiator_.SpawnPushMessage(std::move(message));
          }
          return Success{};
        });
  }
  if (IsStreamFailureFatal()) {
    return Immediate(StatusFlag(Failure{}));
  }
  initiator_.SpawnPushMessage(std::move(message));
  return Immediate(StatusFlag(Success{}));
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleHalfCloseFromClient() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: HandleHalfCloseFromClient invoked";
  const bool send_request_body =
      processing_mode().send_request_body && !IsStreamClosed();
  if (!send_request_body) {
    initiator_.SpawnFinishSends();
    c2s_writes_done_ = true;
    return Immediate(StatusFlag(Success{}));
  }
  Timestamp start_time = Timestamp::Now();
  c2s_writes_done_ = true;
  if (ext_proc_set_eos_) {
    return Immediate(StatusFlag(Success{}));
  }
  if (!config().observability_mode && drain_requested_) {
    ext_proc_filter_->RecordClientHalfCloseDuration(
        (Timestamp::Now() - start_time).seconds());
    initiator_.SpawnFinishSends();
    c2s_writes_done_ = true;
    return Immediate(StatusFlag(Success{}));
  }
  if (!IsStreamClosed() && !ext_proc_stream_half_closed_) {
    MessageHandle null_msg = nullptr;
    return Seq(
        SendClientMessageRequest(null_msg,
                                 /*end_of_stream=*/false,
                                 /*end_of_stream_without_message=*/true),
        [self = Ref(), start_time](StatusFlag status) mutable -> StatusFlag {
          if (!status.ok() && self->IsStreamFailureFatal()) {
            return Failure{};
          }
          self->ext_proc_filter_->RecordClientHalfCloseDuration(
              (Timestamp::Now() - start_time).seconds());
          if (!status.ok() || self->IsStreamClosed() ||
              self->config().observability_mode) {
            self->initiator_.SpawnFinishSends();
          }
          return Success{};
        });
  }
  if (IsStreamFailureFatal()) {
    return Immediate(StatusFlag(Failure{}));
  }
  ext_proc_filter_->RecordClientHalfCloseDuration(
      (Timestamp::Now() - start_time).seconds());
  initiator_.SpawnFinishSends();
  c2s_writes_done_ = true;
  return Immediate(StatusFlag(Success{}));
}

ArenaPromise<StatusFlag> ExtProcFilter::ExtProcCall::SendClientMessageRequest(
    const MessageHandle& message, bool end_of_stream,
    bool end_of_stream_without_message) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Sending client body message request to side-stream";
  std::string message_bytes;
  if (message != nullptr) {
    message_bytes = message->payload()->JoinIntoString();
  }
  if (!config().observability_mode) {
    outstanding_c2s_messages_++;
  }
  if (end_of_stream_without_message) {
    half_close_initiated_ = true;
  }
  std::optional<ExtProcProcessingMode> processing_mode;
  if (IsFirstMessageOnStream()) {
    processing_mode = config().processing_mode;
  }
  upb::Arena arena;
  auto payload = CreateExtProcClientBodyRequest(
      arena.ptr(), message_bytes, request_attributes_,
      config().observability_mode, processing_mode, end_of_stream,
      end_of_stream_without_message);
  if (!payload.ok()) {
    return Immediate(StatusFlag(Failure{}));
  }
  return Map(SendMessageToSideStream(std::move(*payload)),
             [self = Ref()](StatusFlag status) {
               if (status.ok()) {
                 self->first_body_message_sent_ = true;
               }
               return status;
             });
}

//
// Read-from-sidestream Event Handlers
//

StatusFlag
ExtProcFilter::ExtProcCall::HandleClientInitialMetadataFromSidestream(
    const ExtProcResponse::RequestHeaders& response) {
  if (!processing_mode().send_request_headers) {
    SetStreamError(absl::InternalError(
        "Received request headers response but request headers are disabled"));
    return Failure{};
  }
  request_headers_received_ = true;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Processing external processor response for client initial "
         "metadata";
  if (auto status =
          ApplyHeaderMutations(response.mutation, config().mutation_rules,
                               *client_initial_metadata_);
      !status.ok()) {
    SetStreamError(status);
    return Failure{};
  }
  StartChildCall(std::move(client_initial_metadata_), /*attributes=*/nullptr,
                 Timestamp::Now());
  return Success{};
}

StatusFlag ExtProcFilter::ExtProcCall::HandleClientMessageFromSidestream(
    const ExtProcResponse::RequestBody& response) {
  if (!processing_mode().send_request_body) {
    SetStreamError(absl::InternalError(
        "Received request body response but request body is disabled"));
    return Failure{};
  }
  if (processing_mode().send_request_headers && !request_headers_received_) {
    SetStreamError(absl::InternalError(
        "Received request body response before request headers response"));
    return Failure{};
  }
  if (!DecrementOutstandingClientToServerMessages()) {
    SetStreamError(absl::InternalError(
        "Received unexpected request body response from external processor"));
    return Failure{};
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Parsed request body response, eos: "
      << response.mutation.end_of_stream << ", eos_without_msg: "
      << response.mutation.end_of_stream_without_message;
  if (response.mutation.end_of_stream_without_message) {
    if (!c2s_writes_done_) {
      SetStreamError(
          absl::InternalError("Client sends closed by external processor"));
      return Failure{};
    }
    ext_proc_set_eos_ = true;
  } else if (response.mutation.end_of_stream) {
    ext_proc_set_eos_ = true;
  }
  const bool send_request_body =
      processing_mode().send_request_body && !IsStreamClosed();
  if (!send_request_body || config().observability_mode) {
    return Success{};
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Processing external processor response for client body";
  if (!response.mutation.end_of_stream_without_message) {
    auto slice = Slice::FromCopiedString(response.mutation.body);
    auto new_msg = initiator_.arena()->MakePooled<Message>(
        SliceBuffer(std::move(slice)), /*flags=*/0);
    initiator_.SpawnPushMessage(std::move(new_msg));
  }
  if (response.mutation.end_of_stream ||
      response.mutation.end_of_stream_without_message) {
    Timestamp start_time = Timestamp::Now();
    if (c2s_writes_done_ || !IsStreamClosed()) {
      ext_proc_filter_->RecordClientHalfCloseDuration(
          (Timestamp::Now() - start_time).seconds());
      initiator_.SpawnFinishSends();
    }
  }
  return Success{};
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleServerInitialMetadataFromSidestream(
    const ExtProcResponse::ResponseHeaders& response) {
  if (!processing_mode().send_response_headers) {
    SetStreamError(absl::InternalError(
        "Received response headers response but response headers are "
        "disabled"));
    return Immediate(StatusFlag(Failure{}));
  }
  response_headers_received_ = true;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Processing external processor response for server initial "
         "metadata";
  auto latch_wait = is_trailers_only_ ? server_trailing_metadata_latch_.Wait()
                                      : server_initial_metadata_latch_.Wait();
  return Seq(
      std::move(latch_wait),
      [self = Ref(),
       response](ServerMetadataHandle metadata) mutable -> StatusFlag {
        if (auto status = ApplyHeaderMutations(
                response.mutation, self->config().mutation_rules, *metadata);
            !status.ok()) {
          self->SetStreamError(status);
          return Failure{};
        }
        if (!self->IsFailOpenAllowed() && self->IsStreamClosed()) {
          return Failure{};
        }
        self->ext_proc_filter_->RecordServerHeadersDuration(0);
        if (self->is_trailers_only_) {
          self->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
        } else {
          self->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
        }
        return Success{};
      });
}

StatusFlag ExtProcFilter::ExtProcCall::HandleServerMessageFromSidestream(
    const ExtProcResponse::ResponseBody& response) {
  if (!processing_mode().send_response_body) {
    SetStreamError(absl::InternalError(
        "Received response body response but response body is disabled"));
    return Failure{};
  }
  if (is_trailers_only_) {
    SetStreamError(absl::InternalError(
        "Received response body response in a Trailers-Only call"));
    return Failure{};
  }
  if (processing_mode().send_response_headers && !response_headers_received_) {
    SetStreamError(absl::InternalError(
        "Received response body response before response headers response"));
    return Failure{};
  }
  if (processing_mode().send_response_trailers && response_trailers_received_) {
    SetStreamError(absl::InternalError(
        "Received response body response after response trailers response"));
    return Failure{};
  }
  if (outstanding_s2c_messages_ == 0) {
    SetStreamError(absl::InternalError(
        "Received unexpected response body response from external processor"));
    return Failure{};
  }
  bool should_close = false;
  DecrementOutstandingServerToClientMessages(&should_close);
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Processing external processor response for server body";
  auto slice = Slice::FromCopiedString(response.mutation.body);
  auto new_msg = handler_.arena()->MakePooled<Message>(
      SliceBuffer(std::move(slice)), /*flags=*/0);
  handler_.SpawnPushMessage(std::move(new_msg));
  return Success{};
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleServerTrailingMetadataFromSidestream(
    const ExtProcResponse::ResponseTrailers& response) {
  if (!processing_mode().send_response_trailers) {
    SetStreamError(absl::InternalError(
        "Received response trailers response but response trailers are "
        "disabled"));
    return Immediate(StatusFlag(Failure{}));
  }
  if (is_trailers_only_) {
    SetStreamError(absl::InternalError(
        "Received response trailers response in a Trailers-Only call"));
    return Immediate(StatusFlag(Failure{}));
  }
  if (processing_mode().send_response_headers && !response_headers_received_) {
    SetStreamError(absl::InternalError(
        "Received response trailers response before response headers "
        "response"));
    return Immediate(StatusFlag(Failure{}));
  }
  const bool s2c_body_outstanding =
      processing_mode().send_response_body && outstanding_s2c_messages_ > 0;
  if (s2c_body_outstanding) {
    SetStreamError(absl::InternalError(
        "Received response trailers response before all outstanding "
        "response body responses were received"));
    return Immediate(StatusFlag(Failure{}));
  }
  response_trailers_received_ = true;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Processing external processor response for server trailing "
         "metadata";
  return Seq(
      server_trailing_metadata_latch_.Wait(),
      [self = Ref(),
       response](ServerMetadataHandle metadata) mutable -> StatusFlag {
        if (auto status = ApplyHeaderMutations(
                response.mutation, self->config().mutation_rules, *metadata);
            !status.ok()) {
          self->SetStreamError(status);
          return Failure{};
        }
        self->ext_proc_filter_->RecordServerTrailersDuration(0);
        self->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
        return Success{};
      });
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleImmediateResponseFromSidestream(
    const ExtProcResponse::ImmediateResponse& response) {
  if (config().disable_immediate_response || !server_trailers_sent_) {
    SetStreamError(absl::InternalError(
        config().disable_immediate_response
            ? "unhandled immediate response due to config disabled it"
            : "Immediate response received but trailers not sent to "
              "ext_proc"));
    return Immediate(StatusFlag(Failure{}));
  }
  if (processing_mode().send_response_trailers) {
    response_trailers_received_ = true;
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Processing external processor immediate response";
  return Seq(
      server_trailing_metadata_latch_.Wait(),
      [self = Ref(),
       response](ServerMetadataHandle /*metadata*/) mutable -> StatusFlag {
        auto error_md = CancelledServerMetadataFromStatus(
            static_cast<grpc_status_code>(response.status), response.details);
        (void)ApplyHeaderMutations(response.mutation,
                                   self->config().mutation_rules, *error_md);
        self->handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
        return Success{};
      });
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
    : V3InterceptorToV2Bridge<ExtProcFilter>(args),
      config_(std::move(config)),
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
      collection_scope_([&] {
        auto stats_plugin_group =
            args.GetObjectRef<GlobalStatsPluginRegistry::StatsPluginGroup>();
        return stats_plugin_group != nullptr
                   ? stats_plugin_group->GetCollectionScope()
                   : nullptr;
      }()) {}

ExtProcFilter::~ExtProcFilter() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcFilter " << this << " destroyed";
}

void ExtProcFilter::RecordClientHeadersDuration(double duration_seconds) const {
  if (collection_scope_ != nullptr) {
    auto storage =
        ExtProcTelemetryDomain::GetStorage(collection_scope_, target_);
    storage->Increment(ExtProcTelemetryDomain::kClientHeadersDuration,
                       static_cast<int64_t>(duration_seconds));
  }
}

void ExtProcFilter::RecordClientHalfCloseDuration(
    double duration_seconds) const {
  if (collection_scope_ != nullptr) {
    auto storage =
        ExtProcTelemetryDomain::GetStorage(collection_scope_, target_);
    storage->Increment(ExtProcTelemetryDomain::kClientHalfCloseDuration,
                       static_cast<int64_t>(duration_seconds));
  }
}

void ExtProcFilter::RecordServerHeadersDuration(double duration_seconds) const {
  if (collection_scope_ != nullptr) {
    auto storage =
        ExtProcTelemetryDomain::GetStorage(collection_scope_, target_);
    storage->Increment(ExtProcTelemetryDomain::kServerHeadersDuration,
                       static_cast<int64_t>(duration_seconds));
  }
}

void ExtProcFilter::RecordServerTrailersDuration(
    double duration_seconds) const {
  if (collection_scope_ != nullptr) {
    auto storage =
        ExtProcTelemetryDomain::GetStorage(collection_scope_, target_);
    storage->Increment(ExtProcTelemetryDomain::kServerTrailersDuration,
                       static_cast<int64_t>(duration_seconds));
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
        // This shouldn't ever happen; added as a defensive check.
        if (transport == nullptr) {
          return ArenaPromise<absl::Status>([]() -> Poll<absl::Status> {
            return absl::InternalError(
                "External processor transport unavailable");
          });
        }
        auto ext_proc_call = MakeRefCounted<ExtProcCall>(
            ext_proc_filter, std::move(transport), handler);
        return ext_proc_call->Run();
      });
}

}  // namespace grpc_core
