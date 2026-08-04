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
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/observable.h"
#include "src/core/lib/promise/prioritized_race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/streaming_call_promise_wrapper.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
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

template <typename T>
absl::Status ApplyHeaderMutations(
    const ExtProcResponse& response,
    const std::optional<HeaderMutationRules>& rules,
    grpc_metadata_batch& metadata) {
  if (const auto* response_part = std::get_if<T>(&response.response)) {
    const auto* rules_ptr = rules.has_value() ? &rules.value() : nullptr;
    const auto& mutations = response_part->mutation;
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
//  LOOP 1: Read-From-Client Pipeline Loop [SpawnReadFromClientLoop()]
//  +-----------------------------------------------------------------------+
//  | TrySeq(                                                               |
//  |     ProcessClientInitialMetadataFromClient(),                         |
//  |     ProcessClientMessagesFromClient())                                |
//  +-----------------------------------------------------------------------+
//                                     ||
//                     Joined via TryJoin() in Run()
//                                     ||
//  LOOP 2: Side-Stream Pull Pipeline Loop [SpawnReadFromSideStreamLoop()]
//  +-----------------------------------------------------------------------+
//  | SpawnReadFromSideStreamLoop()                                         |
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
  absl::AnyInvocable<Poll<StatusFlag>()> Run();

 private:
  // Spawns the read-from-client loop on handler_.
  // Called when the ExtProcCall is created.
  //
  // Handles client initial metadata, client messages, and half-close.
  // Sends each event to the ext_proc side-stream and/or to the server,
  // based on the configuration.
  absl::AnyInvocable<Poll<StatusFlag>()> SpawnReadFromClientLoop();

  // Spawns the read-from-ext_proc-side-stream loop on handler_.
  // Called when the ExtProcCall is created.
  //
  // Handles all events on the call, forwarding data to either handler_
  // (client) or initiator_ (server).
  absl::AnyInvocable<Poll<StatusFlag>()> SpawnReadFromSideStreamLoop();

  // Spawns the read-from-server loop on initiator_.
  // Called from StartChildCall().
  //
  // Handles server initial metadata, server messages, and server
  // trailing metadata.  Sends each event to the ext_proc side-stream
  // and/or to the client, based on the configuration.
  absl::AnyInvocable<Poll<StatusFlag>()> SpawnReadFromServerLoop();

  // Processes client initial metadata on the client-to-extproc path.
  absl::AnyInvocable<Poll<StatusFlag>()>
  ProcessClientInitialMetadataFromClient();
  // Processes the ext_proc server response for client initial metadata and
  // forwards the mutated metadata to the backend server.
  absl::AnyInvocable<Poll<StatusFlag>()> ProcessClientInitialMetadataResponse(
      absl::StatusOr<ExtProcResponse> response);
  // Prepares the ProcessingRequest protobuf message for client initial
  // metadata.
  absl::AnyInvocable<Poll<StatusFlag>()> SendClientInitialMetadataRequest(
      const ClientMetadataHandle& metadata,
      absl::string_view default_authority);

  // Initializes and starts the child call to the backend server, and spawns
  // the background task for the server-to-client response path.
  void StartChildCall(ClientMetadataHandle metadata,
                      ::google_protobuf_Struct* attributes = nullptr,
                      Timestamp start_time = Timestamp::InfPast());
  // Forwards client initial metadata to the backend server without sending to
  // ext_proc when request header processing is disabled. Prepares request
  // attributes if body processing is enabled.
  void ClientInitialMetadataNonProcessingMode(ClientMetadataHandle metadata);
  // Handles client initial metadata in observability mode.
  void ClientInitialMetadataObservabilityMode(ClientMetadataHandle metadata,
                                              Timestamp start_time);
  // Intercepts, sends to ext_proc, and applies mutations to client initial
  // metadata.
  StatusFlag ClientInitialMetadataNormalMode(
      ClientMetadataHandle metadata, Timestamp start_time,
      absl::StatusOr<ExtProcResponse> response);

  // Prepares the ProcessingRequest protobuf message for server initial
  // metadata and sends it over the ext_proc stream.
  absl::AnyInvocable<Poll<StatusFlag>()> SendServerInitialMetadataRequest(
      const ServerMetadataHandle& metadata, bool end_of_stream = false);
  // Processes server initial metadata on the server-to-extproc path.
  absl::AnyInvocable<Poll<StatusFlag>()>
  ProcessServerInitialMetadataFromServer();
  // Processes the ext_proc server response for server initial metadata and
  // forwards the mutated metadata to the client.
  absl::AnyInvocable<Poll<StatusFlag>()> ProcessServerInitialMetadataResponse(
      absl::StatusOr<ExtProcResponse> response);
  // Non-processing mode: pushes server initial metadata directly to client.
  absl::AnyInvocable<Poll<StatusFlag>()> ServerInitialMetadataNonProcessingMode(
      ServerMetadataHandle metadata);
  // Observability mode: records duration and pushes server initial metadata
  // to client.
  absl::AnyInvocable<Poll<StatusFlag>()> ServerInitialMetadataObservabilityMode(
      ServerMetadataHandle metadata, Timestamp start_time);
  // Normal mode: gets response from ext_proc server, applies mutations,
  // handles errors, and pushes to client.
  absl::AnyInvocable<Poll<StatusFlag>()> ServerInitialMetadataNormalMode(
      ServerMetadataHandle metadata, Timestamp start_time,
      absl::StatusOr<ExtProcResponse> response);
  // Handles server initial metadata when the external processor has requested
  // a drain.
  absl::AnyInvocable<Poll<StatusFlag>()> ServerInitialMetadataDrainMode(
      ServerMetadataHandle metadata);

  // Processes server trailing metadata on the server-to-extproc path.
  absl::AnyInvocable<Poll<StatusFlag>()>
  ProcessServerTrailingMetadataFromServer();
  // Processes the ext_proc server response for server trailing metadata and
  // forwards the mutated metadata to the client.
  absl::AnyInvocable<Poll<StatusFlag>()> ProcessServerTrailingMetadataResponse(
      absl::StatusOr<ExtProcResponse> response);
  // Prepares the ProcessingRequest protobuf message for server trailing
  // metadata (trailers) and sends it over the ext_proc stream.
  absl::AnyInvocable<Poll<StatusFlag>()> SendServerTrailingMetadataRequest(
      const ServerMetadataHandle& metadata);
  // Helper to process server trailing metadata for both trailers-only and
  // normal trailers.
  absl::AnyInvocable<Poll<StatusFlag>()> ProcessTrailingMetadata(
      bool is_trailers_only);
  // Handles server trailing metadata when drain operation was requested.
  absl::AnyInvocable<Poll<StatusFlag>()> ServerTrailingMetadataDrainMode(
      ServerMetadataHandle metadata);
  // Non-processing mode: closes body pipe sender if needed and forwards
  // server trailers to client.
  absl::AnyInvocable<Poll<StatusFlag>()>
  ServerTrailingMetadataNonProcessingMode(ServerMetadataHandle metadata);
  // Handles trailers-only RPC trailing metadata in observability mode.
  absl::AnyInvocable<Poll<StatusFlag>()> ServerTrailersOnlyObservabilityMode(
      ServerMetadataHandle metadata, Timestamp start_time);
  // Handles normal trailing metadata in observability mode.
  absl::AnyInvocable<Poll<StatusFlag>()>
  ServerTrailingMetadataObservabilityMode(ServerMetadataHandle metadata,
                                          Timestamp start_time);
  // Handles trailers-only RPC trailing metadata in normal processing mode.
  absl::AnyInvocable<Poll<StatusFlag>()> ServerTrailersOnlyNormalMode(
      ServerMetadataHandle metadata, Timestamp start_time,
      absl::StatusOr<ExtProcResponse> response);
  absl::AnyInvocable<Poll<StatusFlag>()> ServerTrailingMetadataNormalMode(
      ServerMetadataHandle metadata, Timestamp start_time,
      absl::StatusOr<ExtProcResponse> response);

  // Processes server-to-client messages on the backend-to-extproc path.
  absl::AnyInvocable<Poll<StatusFlag>()> ProcessServerMessagesFromServer();
  // Processes the ext_proc server response for server-to-client body
  // messages.
  absl::AnyInvocable<Poll<StatusFlag>()> ProcessServerMessageResponse(
      absl::StatusOr<ExtProcResponse> response);
  // Prepares the ProcessingRequest protobuf message for server response body
  // and sends it over the ext_proc stream.
  absl::AnyInvocable<Poll<StatusFlag>()> SendServerMessageRequest(
      const MessageHandle& message);
  // Forwards server message to client without ext_proc processing.
  absl::AnyInvocable<Poll<StatusFlag>()> ServerMessageNonProcessingMode(
      MessageHandle message);
  // Handles server-to-client message in observability mode.
  absl::AnyInvocable<Poll<StatusFlag>()> ServerMessageObservabilityMode(
      MessageHandle message);
  // Intercepts server-to-client message in normal mode.
  absl::AnyInvocable<Poll<StatusFlag>()> ServerMessageNormalMode(
      MessageHandle message);
  // Passes through server message directly to client if stream failure is
  // non-fatal, or returns error status if fatal.
  StatusFlag PassThroughServerMessage(MessageHandle message);
  // Processes a server message by sending a request to ext_proc if stream is
  // open, and passing through the message to the client.
  absl::AnyInvocable<Poll<StatusFlag>()> ProcessServerMessage(
      MessageHandle message, bool observability_mode);

  // Processes client-to-server messages on the client-to-extproc path.
  absl::AnyInvocable<Poll<StatusFlag>()> ProcessClientMessagesFromClient();
  // Processes the ext_proc server response for client-to-server body messages
  // and forwards mutated messages/finish sends to the backend.
  absl::AnyInvocable<Poll<StatusFlag>()> ProcessClientMessageResponse(
      absl::StatusOr<ExtProcResponse> result);
  // Prepares the ProcessingRequest protobuf message for client body messages.
  absl::AnyInvocable<Poll<StatusFlag>()> SendClientMessageRequest(
      const MessageHandle& message, bool end_of_stream,
      bool end_of_stream_without_message);
  // Intercepts client-to-server messages in non processing mode.
  absl::AnyInvocable<Poll<StatusFlag>()> ClientMessageNonProcessingMode();
  // Handles client-to-server message in observability mode.
  absl::AnyInvocable<Poll<StatusFlag>()> ClientMessageObservabilityMode(
      MessageHandle message);
  // Intercepts client-to-server message in normal mode.
  absl::AnyInvocable<Poll<StatusFlag>()> ClientMessageNormalModeSendOnly(
      MessageHandle message);
  // Processes a client message by sending a request to ext_proc if stream is
  // open, and forwarding to backend server based on mode.
  absl::AnyInvocable<Poll<StatusFlag>()> ProcessClientMessage(
      MessageHandle message, bool observability_mode);
  // Sends client half-close request to ext_proc or finishes sends based on
  // mode.
  absl::AnyInvocable<Poll<StatusFlag>()> SendClientHalfClose(
      bool observability_mode);

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

  // Sends a message to the external processor side-stream.
  // Coordinates client-side and server-side message sources so that only one
  // send is in-flight on streaming_call_ at a time, using a single Waker
  // without any queue or vector allocations.
  absl::AnyInvocable<Poll<StatusFlag>()> SendMessageToSideStream(
      std::string payload);

  // Parses and processes an incoming response message payload from the
  // side-stream.
  absl::AnyInvocable<Poll<StatusFlag>()> ProcessSideStreamResponse(
      absl::string_view payload);

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

  auto NextStreamStatus() const {
    return stream_status_.NextWhen(
        [](const std::optional<absl::Status>& status) {
          return status.has_value();
        });
  }

  Poll<std::optional<absl::Status>> PollStreamStatus() const {
    return NextStreamStatus()();
  }

  bool IsStreamClosed() const { return PollStreamStatus().ready(); }

  void SetStreamStatus(absl::Status status) {
    if (!IsStreamClosed()) {
      stream_status_.Set(status);
    }
  }

  // Returns true if the stream closed with an error and fail-open mode is not
  // permitted for this call (i.e. the stream error must fail the RPC).
  bool IsStreamFailureFatal() const {
    if (IsFailOpenAllowed()) return false;
    auto poll = PollStreamStatus();
    return poll.ready() && !poll.value()->ok();
  }

  // Evaluates the status to return when the external processor stream is
  // closed or when a send fails. Respects IsFailOpenAllowed() (which handles
  // both failure_mode_allow and observability_mode) by returning OkStatus()
  // when fail-open is permitted.
  absl::Status GetStreamClosedStatus(
      absl::Status default_error = absl::CancelledError("Stream closed")) {
    auto poll = PollStreamStatus();
    if (poll.ready()) {
      return poll.value().value_or(absl::OkStatus());
    }
    if (IsFailOpenAllowed()) {
      return absl::OkStatus();
    }
    return default_error;
  }

  auto WaitForStreamStatus() {
    return Map(NextStreamStatus(), [](std::optional<absl::Status> status) {
      return status.value_or(absl::OkStatus());
    });
  }

  void SetStreamError(absl::Status status) {
    if (!status.ok()) {
      auto error_md = CancelledServerMetadataFromStatus(status);
      handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
      if (initiator_.is_set()) {
        initiator_.SpawnCancel();
      }
    }
    SetStreamStatus(status);
    CompleteOutstandingProcessors(status);
  }

  void CloseStream() {
    if (!IsStreamClosed()) {
      stream_status_.Set(absl::OkStatus());
    }
    auto streaming_call = std::move(streaming_call_);
    ext_proc_send_waker_.Wakeup();
    streaming_call.reset();
  }

  void Orphan() override {
    CloseStream();
    Unref();
  }

  void CompleteOutstandingProcessors(absl::StatusOr<ExtProcResponse> response);

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
  mutable Observable<std::optional<absl::Status>> stream_status_{std::nullopt};

  // Atomic send state for lock-free coordination between client-side and
  // server-side message senders.
  SendState ext_proc_send_state_ = SendState::kIdle;
  // Waker for queuing send promises when a send operation is already in flight.
  Waker ext_proc_send_waker_;

  RefCountedPtr<XdsTransportFactory::XdsTransport> transport_;
  CallHandler handler_;
  CallInitiator initiator_;
  RefCountedPtr<XdsStreamingCallPromiseWrapper> streaming_call_;
  RefCountedPtr<ExtProcFilter> ext_proc_filter_;
};

ExtProcFilter::ExtProcCall::ExtProcCall(
    RefCountedPtr<ExtProcFilter> ext_proc_filter,
    RefCountedPtr<XdsTransportFactory::XdsTransport> transport,
    CallHandler handler)
    : transport_(std::move(transport)),
      handler_(handler),
      ext_proc_filter_(std::move(ext_proc_filter)) {
  const char* method = "/envoy.service.ext_proc.v3.ExternalProcessor/Process";
  streaming_call_ = MakeRefCounted<XdsStreamingCallPromiseWrapper>(
      *transport_, method, /*wait_for_ready=*/false);
}

ExtProcFilter::ExtProcCall::~ExtProcCall() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " destroyed";
  if (config().deferred_close_timeout != Duration::Zero() &&
      config().observability_mode) {
    ext_proc_filter_->event_engine_->RunAfter(
        config().deferred_close_timeout,
        [call = std::move(streaming_call_),
         transport = std::move(transport_)]() mutable {
          call.reset();
          transport.reset();
        });
  } else {
    streaming_call_.reset();
  }
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessSideStreamResponse(
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
  auto create_error =
      [this](
          absl::string_view message) -> absl::AnyInvocable<Poll<StatusFlag>()> {
    auto error = absl::InternalError(message);
    SetStreamError(error);
    return Immediate(StatusFlag(Failure{}));
  };
  return Match(
      (*parsed_response).response,
      [&](const ExtProcResponse::ImmediateResponse&)
          -> absl::AnyInvocable<Poll<StatusFlag>()> {
        if (config().disable_immediate_response || !server_trailers_sent_) {
          return create_error(
              config().disable_immediate_response
                  ? "unhandled immediate response due to config disabled it"
                  : "Immediate response received but trailers not sent to "
                    "ext_proc");
        }
        if (processing_mode().send_response_trailers) {
          response_trailers_received_ = true;
        }
        return ProcessServerTrailingMetadataResponse(
            std::move(*parsed_response));
      },
      [&](const ExtProcResponse::RequestHeaders&)
          -> absl::AnyInvocable<Poll<StatusFlag>()> {
        if (!processing_mode().send_request_headers) {
          return create_error(
              "Received request headers response but request headers are "
              "disabled");
        }
        if (processing_mode().send_request_headers) {
          request_headers_received_ = true;
        }
        return ProcessClientInitialMetadataResponse(
            std::move(*parsed_response));
      },
      [&](const ExtProcResponse::ResponseHeaders&)
          -> absl::AnyInvocable<Poll<StatusFlag>()> {
        if (!processing_mode().send_response_headers) {
          return create_error(
              "Received response headers response but response headers are "
              "disabled");
        }
        if (processing_mode().send_response_headers) {
          response_headers_received_ = true;
        }
        if (is_trailers_only_) {
          return ProcessServerTrailingMetadataResponse(
              std::move(*parsed_response));
        }
        return ProcessServerInitialMetadataResponse(
            std::move(*parsed_response));
      },
      [&](const ExtProcResponse::ResponseTrailers&)
          -> absl::AnyInvocable<Poll<StatusFlag>()> {
        if (!processing_mode().send_response_trailers) {
          return create_error(
              "Received response trailers response but response trailers are "
              "disabled");
        }
        if (is_trailers_only_) {
          return create_error(
              "Received response trailers response in a Trailers-Only call");
        }
        if (processing_mode().send_response_headers &&
            !response_headers_received_) {
          return create_error(
              "Received response trailers response before response headers "
              "response");
        }
        const bool s2c_body_outstanding =
            processing_mode().send_response_body &&
            outstanding_s2c_messages_ > 0;
        if (s2c_body_outstanding) {
          return create_error(
              "Received response trailers response before all outstanding "
              "response body responses were received");
        }
        if (processing_mode().send_response_trailers) {
          response_trailers_received_ = true;
        }
        return ProcessServerTrailingMetadataResponse(
            std::move(*parsed_response));
      },
      [&](const ExtProcResponse::RequestBody& request_body)
          -> absl::AnyInvocable<Poll<StatusFlag>()> {
        if (!processing_mode().send_request_body) {
          return create_error(
              "Received request body response but request body is disabled");
        }
        if (processing_mode().send_request_headers &&
            !request_headers_received_) {
          return create_error(
              "Received request body response before request headers "
              "response");
        }
        if (!DecrementOutstandingClientToServerMessages()) {
          return create_error(
              "Received unexpected request body response from external "
              "processor");
        }
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Parsed request body response, eos: "
            << request_body.mutation.end_of_stream << ", eos_without_msg: "
            << request_body.mutation.end_of_stream_without_message;
        if (request_body.mutation.end_of_stream_without_message) {
          if (!c2s_writes_done_) {
            return create_error("Client sends closed by external processor");
          }
          ext_proc_set_eos_ = true;
        } else if (request_body.mutation.end_of_stream) {
          ext_proc_set_eos_ = true;
        }
        return ProcessClientMessageResponse(std::move(*parsed_response));
      },
      [&](const ExtProcResponse::ResponseBody&)
          -> absl::AnyInvocable<Poll<StatusFlag>()> {
        if (!processing_mode().send_response_body) {
          return create_error(
              "Received response body response but response body is disabled");
        }
        if (is_trailers_only_) {
          return create_error(
              "Received response body response in a Trailers-Only call");
        }
        if (processing_mode().send_response_headers &&
            !response_headers_received_) {
          return create_error(
              "Received response body response before response headers "
              "response");
        }
        if (processing_mode().send_response_trailers &&
            response_trailers_received_) {
          return create_error(
              "Received response body response after response trailers "
              "response");
        }
        if (outstanding_s2c_messages_ == 0) {
          return create_error(
              "Received unexpected response body response from external "
              "processor");
        }
        bool should_close = false;
        DecrementOutstandingServerToClientMessages(&should_close);
        return ProcessServerMessageResponse(std::move(*parsed_response));
      },
      [](std::monostate) -> absl::AnyInvocable<Poll<StatusFlag>()> {
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
    stream_status_.Set(should_propagate_error ? status : absl::OkStatus());
    if (should_propagate_error) {
      // On fatal error, push error trailing metadata and cancel child call.
      auto error_md = CancelledServerMetadataFromStatus(status);
      handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
      if (initiator_.is_set()) {
        initiator_.SpawnCancel();
      }
      CompleteOutstandingProcessors(status);
    } else {
      // On clean close or fail-open, complete pending processors normally.
      CompleteOutstandingProcessors(ExtProcResponse{});
    }
    CloseStream();
  }
}

void ExtProcFilter::ExtProcCall::CompleteOutstandingProcessors(
    absl::StatusOr<ExtProcResponse> response) {
  if (processing_mode().send_request_headers && !request_headers_received_ &&
      client_initial_metadata_ != nullptr) {
    auto promise = ProcessClientInitialMetadataResponse(response);
    (void)promise();
  }
  if (processing_mode().send_response_headers && !response_headers_received_ &&
      server_initial_metadata_latch_.IsSet()) {
    auto promise = ProcessServerInitialMetadataResponse(response);
    (void)promise();
  }
  if (processing_mode().send_response_trailers &&
      !response_trailers_received_ && server_trailing_metadata_latch_.IsSet()) {
    auto promise = ProcessServerTrailingMetadataResponse(response);
    (void)promise();
  }
}

// This function role is to:
// - send the message to the ext proc server and wait for the send to get
// complete and then propagate the status
// - if a message is already in progress then wait for the in flight message to
// get complete and then send the previous one if the stream is not closed
// - Handle the failure mode allow
absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::SendMessageToSideStream(std::string payload) {
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
absl::AnyInvocable<Poll<StatusFlag>()> ExtProcFilter::ExtProcCall::Run() {
  return Map(TryJoin<absl::StatusOr>(SpawnReadFromClientLoop(),
                                     SpawnReadFromSideStreamLoop()),
             [self = Ref()](
                 absl::StatusOr<std::tuple<Empty, Empty>> res) -> StatusFlag {
               GRPC_TRACE_LOG(ext_proc_filter, INFO)
                   << "ExtProcCall " << self.get()
                   << " Run() finished with status: " << res.ok();
               return StatusFlag(res.ok());
             });
}

// Spawns the read-from-client loop on handler_.
// Called when the ExtProcCall is created.
//
// Handles client initial metadata, client messages, and half-close.
// Sends each event to the ext_proc side-stream and/or to the server,
// based on the configuration.
absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::SpawnReadFromClientLoop() {
  return Map(TrySeq(ProcessClientInitialMetadataFromClient(),
                    ProcessClientMessagesFromClient()),
             [self = Ref()](StatusFlag status) -> StatusFlag {
               GRPC_TRACE_LOG(ext_proc_filter, INFO)
                   << "ExtProcCall " << self.get()
                   << " read-from-client loop finished with status: "
                   << status.ok();
               return status;
             });
}

// Continuously pulls response messages from the external processor side-stream
// and dispatches them until the stream closes or an error occurs.
absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::SpawnReadFromSideStreamLoop() {
  return Seq(
      // Loop reading response messages from the side-stream until end-of-stream
      // or error.
      Loop([self = Ref()]() -> Promise<LoopCtl<StatusFlag>> {
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
      [self = Ref()](StatusFlag status) -> Promise<absl::Status> {
        if (!status.ok()) {
          return Immediate(absl::InternalError("Side stream failed"));
        }
        if (self->streaming_call_ == nullptr) {
          return Immediate(absl::OkStatus());
        }
        return self->streaming_call_->PullServerTrailingMetadata();
      },
      // Handle stream closure and resolve final status.
      [self = Ref()](absl::Status status) -> StatusFlag {
        self->HandleSideStreamStatus(status);
        status = self->GetStreamClosedStatus(status);
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProcCall " << self.get()
            << " SpawnReadFromSideStreamLoop finished with status: " << status;
        return StatusFlag(status.ok());
      });
}

// Handles the response path (Server to Client).
// This function sets up a pipeline to process server initial metadata,
// response messages, and server trailing metadata, potentially intercepting
// and mutating them via the ext_proc server.
//
// It also watches for ext_proc stream errors and aborts the call if a failure
// occurs and fail-open is not allowed.
absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::SpawnReadFromServerLoop() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " SpawnReadFromServerLoop started";
  auto response_pipeline = TrySeq(ProcessServerInitialMetadataFromServer(),
                                  ProcessServerMessagesFromServer(),
                                  ProcessServerTrailingMetadataFromServer());
  // Monitor the ext_proc stream for errors.
  // If the ext_proc stream fails and fail-open is NOT allowed, we abort the
  // call.
  auto watch_error = Seq(
      WaitForStreamStatus(),
      [self = Ref()](
          absl::Status status) -> absl::AnyInvocable<Poll<StatusFlag>()> {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "watch_error stream_status: " << status
            << ", failure_mode_allow: "
            << (self->config().failure_mode_allow.has_value()
                    ? (*self->config().failure_mode_allow ? "true" : "false")
                    : "unset");
        if (!status.ok()) {
          return Immediate(StatusFlag(Failure{}));
        }
        return []() -> Poll<StatusFlag> {
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
          promise = std::move(run_pipeline)]() mutable -> Poll<StatusFlag> {
    auto p = promise();
    if (auto* status = p.value_if_ready()) {
      GRPC_TRACE_LOG(ext_proc_filter, INFO)
          << "ExtProcCall " << self.get()
          << " SpawnReadFromServerLoop finished. status=" << status->ok();
      return *status;
    }
    return Pending{};
  };
}

//
// ExtProcFilter::ExtProcCall Client Initial Metadata Processing
//

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessClientInitialMetadataFromClient() {
  return TrySeq(
      handler_.PullClientInitialMetadata(),
      [self = Ref()](ClientMetadataHandle metadata) mutable
          -> absl::AnyInvocable<Poll<StatusFlag>()> {
        if (!self->config().processing_mode->send_request_headers) {
          // If request header processing is disabled, forward metadata directly
          // without calling ext_proc.
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Skipping client initial metadata (processing mode "
                 "disabled)";
          self->ClientInitialMetadataNonProcessingMode(std::move(metadata));
          return Immediate(StatusFlag(Success{}));
        } else if (self->config().observability_mode) {
          // In observability mode, send request headers to ext_proc
          // asynchronously without awaiting a response.
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Sending client initial metadata (observability "
                 "mode)";
          return Seq(
              self->SendClientInitialMetadataRequest(
                  metadata,
                  self->ext_proc_filter_->default_authority_.as_string_view()),
              [self, metadata = std::move(metadata)](StatusFlag) mutable
                  -> absl::AnyInvocable<Poll<StatusFlag>()> {
                self->ClientInitialMetadataObservabilityMode(
                    std::move(metadata), Timestamp::Now());
                return Immediate(StatusFlag(Success{}));
              });
        } else {
          // In normal mode, send request headers to ext_proc and store metadata
          // until the response arrives.
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Sending client initial metadata (normal mode)";
          auto send_promise = self->SendClientInitialMetadataRequest(
              metadata,
              self->ext_proc_filter_->default_authority_.as_string_view());
          self->client_initial_metadata_ = std::move(metadata);
          return send_promise;
        }
      });
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessClientInitialMetadataResponse(
    absl::StatusOr<ExtProcResponse> response) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Processing external processor response for client initial "
         "metadata";
  // Apply external processor response mutations to metadata and start child
  // call.
  return Immediate(
      ClientInitialMetadataNormalMode(std::move(client_initial_metadata_),
                                      Timestamp::Now(), std::move(response)));
}

absl::AnyInvocable<Poll<StatusFlag>()>
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

void ExtProcFilter::ExtProcCall::StartChildCall(
    ClientMetadataHandle metadata, ::google_protobuf_Struct* attributes,
    Timestamp start_time) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Starting downstream child call";
  // Record header processing duration metric if start time was provided.
  if (start_time != Timestamp::InfPast()) {
    ext_proc_filter_->RecordClientHeadersDuration(
        (Timestamp::Now() - start_time).seconds());
  }
  // Initialize downstream child call with modified client initial metadata.
  initiator_ = ext_proc_filter_->MakeChildCall(std::move(metadata),
                                               handler_.arena()->Ref());
  handler_.AddChildCall(initiator_);
  // Spawn background task to handle server-to-client path.
  initiator_.SpawnInfallible("read_from_server", [self = Ref()]() mutable {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: read_from_server task started";
    return self->initiator_.CancelIfFails(self->SpawnReadFromServerLoop());
  });
}

void ExtProcFilter::ExtProcCall::ClientInitialMetadataNonProcessingMode(
    ClientMetadataHandle metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Client initial metadata received (non-processing):\n"
      << metadata->DebugString();
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
  // Directly start downstream child call with unmodified client metadata.
  StartChildCall(std::move(metadata), attributes);
}

void ExtProcFilter::ExtProcCall::ClientInitialMetadataObservabilityMode(
    ClientMetadataHandle metadata, Timestamp start_time) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Client initial metadata received (observability):\n"
      << metadata->DebugString();
  StartChildCall(std::move(metadata),
                 /*attributes=*/nullptr, start_time);
}

StatusFlag ExtProcFilter::ExtProcCall::ClientInitialMetadataNormalMode(
    ClientMetadataHandle metadata, Timestamp start_time,
    absl::StatusOr<ExtProcResponse> response) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Client initial metadata received:\n"
      << metadata->DebugString();
  // Check if external processor returned an error status.
  if (!response.ok()) {
    GRPC_TRACE_LOG(ext_proc_filter, ERROR)
        << "ExtProc: External processor returned error status for client "
           "headers: "
        << response.status();
    return Failure{};
  }
  // Apply header mutations from ext_proc response to client initial metadata.
  if (auto status = ApplyHeaderMutations<ExtProcResponse::RequestHeaders>(
          *response, config().mutation_rules, *metadata);
      !status.ok()) {
    SetStreamError(status);
    return Failure{};
  }
  // Start downstream child call with mutated metadata.
  StartChildCall(std::move(metadata),
                 /*attributes=*/nullptr, start_time);
  return Success{};
}

//
// //
// ExtProcFilter::ExtProcCall Server Initial Metadata Processing
//

absl::AnyInvocable<Poll<StatusFlag>()>
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

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessServerInitialMetadataFromServer() {
  return Seq(
      initiator_.PullServerInitialMetadata(),
      [self = Ref()](std::optional<ServerMetadataHandle> metadata) mutable
          -> absl::AnyInvocable<Poll<StatusFlag>()> {
        if (!metadata.has_value()) {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: No server initial metadata (trailers-only response)";
          self->is_trailers_only_ = true;
          return Immediate(StatusFlag(Success{}));
        }
        if (!self->processing_mode().send_response_headers ||
            self->IsStreamClosed()) {
          // Response headers processing disabled or stream closed; pass through
          // metadata.
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Skipping server initial metadata (processing "
                 "disabled "
                 "or stream closed)";
          return self->ServerInitialMetadataNonProcessingMode(
              std::move(*metadata));
        } else if (self->config().observability_mode) {
          // Observability mode: send response headers asynchronously without
          // waiting.
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Sending server initial metadata (observability "
                 "mode)";
          return Seq(self->SendServerInitialMetadataRequest(*metadata),
                     [self, metadata = std::move(*metadata)](StatusFlag) mutable
                         -> absl::AnyInvocable<Poll<StatusFlag>()> {
                       return self->ServerInitialMetadataObservabilityMode(
                           std::move(metadata), Timestamp::Now());
                     });
        } else if (self->drain_requested_) {
          // Drain requested by ext_proc server; wait for stream termination
          // before forwarding.
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Handling server initial metadata in drain mode";
          return self->ServerInitialMetadataDrainMode(std::move(*metadata));
        } else {
          // Normal mode: send to ext_proc and latch metadata until external
          // processor responds.
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Sending server initial metadata (normal mode)";
          auto send_promise = self->SendServerInitialMetadataRequest(*metadata);
          self->server_initial_metadata_latch_.Set(std::move(*metadata));
          return send_promise;
        }
      });
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessServerInitialMetadataResponse(
    absl::StatusOr<ExtProcResponse> response) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Processing external processor response for server initial "
         "metadata";
  return Seq(server_initial_metadata_latch_.Wait(),
             [self = Ref(), response = std::move(response)](
                 ServerMetadataHandle metadata) mutable
                 -> absl::AnyInvocable<Poll<StatusFlag>()> {
               return self->ServerInitialMetadataNormalMode(
                   std::move(metadata), Timestamp::Now(), std::move(response));
             });
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerInitialMetadataNonProcessingMode(
    ServerMetadataHandle metadata) {
  if (IsStreamFailureFatal()) {
    return Immediate(StatusFlag(Failure{}));
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataNonProcessingMode metadata: "
      << metadata->DebugString();
  handler_.SpawnPushServerInitialMetadata(std::move(metadata));
  return Immediate(StatusFlag(Success{}));
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerInitialMetadataObservabilityMode(
    ServerMetadataHandle metadata, Timestamp start_time) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataObservabilityMode metadata: "
      << metadata->DebugString();
  ext_proc_filter_->RecordServerHeadersDuration(
      (Timestamp::Now() - start_time).seconds());
  handler_.SpawnPushServerInitialMetadata(std::move(metadata));
  return Immediate(StatusFlag(Success{}));
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerInitialMetadataNormalMode(
    ServerMetadataHandle metadata, Timestamp start_time,
    absl::StatusOr<ExtProcResponse> response) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataNormalMode metadata: "
      << metadata->DebugString();
  if (!response.ok()) {
    GRPC_TRACE_LOG(ext_proc_filter, ERROR)
        << "ExtProc: External processor returned error status for server "
           "headers: "
        << response.status();
    return Immediate(StatusFlag(Failure{}));
  }
  // Apply header mutations returned by the external processor.
  if (auto status = ApplyHeaderMutations<ExtProcResponse::ResponseHeaders>(
          *response, config().mutation_rules, *metadata);
      !status.ok()) {
    SetStreamError(status);
    return Immediate(StatusFlag(Failure{}));
  }
  if (!IsFailOpenAllowed() && IsStreamClosed()) {
    return Immediate(StatusFlag(Failure{}));
  }
  ext_proc_filter_->RecordServerHeadersDuration(
      (Timestamp::Now() - start_time).seconds());
  handler_.SpawnPushServerInitialMetadata(std::move(metadata));
  return Immediate(StatusFlag(Success{}));
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerInitialMetadataDrainMode(
    ServerMetadataHandle metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataDrainMode metadata: "
      << metadata->DebugString();
  return Map(
      WaitForStreamStatus(),
      [self = Ref(), metadata = std::move(metadata)](
          absl::Status status) mutable -> StatusFlag {
        if (self->IsStreamFailureFatal()) {
          return Failure{};
        }
        self->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
        return Success{};
      });
}

//
// ExtProcFilter::ExtProcCall Server Trailing Metadata Processing
//

absl::AnyInvocable<Poll<StatusFlag>()>
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

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessServerTrailingMetadataFromServer() {
  if (IsStreamFailureFatal()) {
    return Immediate(StatusFlag(Failure{}));
  }
  return ProcessTrailingMetadata(is_trailers_only_);
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessServerTrailingMetadataResponse(
    absl::StatusOr<ExtProcResponse> response) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Processing external processor response for server trailing "
         "metadata";
  return Seq(
      server_trailing_metadata_latch_.Wait(),
      [self = Ref(),
       response = std::move(response)](ServerMetadataHandle metadata) mutable
          -> absl::AnyInvocable<Poll<StatusFlag>()> {
        if (self->is_trailers_only_) {
          return self->ServerTrailersOnlyNormalMode(
              std::move(metadata), Timestamp::Now(), std::move(response));
        }
        return self->ServerTrailingMetadataNormalMode(
            std::move(metadata), Timestamp::Now(), std::move(response));
      });
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessTrailingMetadata(bool is_trailers_only) {
  return Seq(
      initiator_.PullServerTrailingMetadata(),
      [self = Ref(), is_trailers_only](ServerMetadataHandle metadata) mutable
          -> absl::AnyInvocable<Poll<StatusFlag>()> {
        // If trailing status is not OK (e.g. error from downstream), pass
        // trailers through directly.
        if (!IsStatusOk(*metadata)) {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Passing through non-OK server trailing metadata";
          self->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
          return Immediate(StatusFlag(Success{}));
        }
        const bool send_metadata =
            is_trailers_only ? self->processing_mode().send_response_headers
                             : self->processing_mode().send_response_trailers;
        if (!send_metadata || self->IsStreamClosed()) {
          // Processing disabled or stream closed; pass through metadata without
          // calling ext_proc.
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Skipping server trailing metadata (processing "
                 "disabled or stream closed)";
          if (is_trailers_only) {
            self->server_trailing_metadata_latch_.Set(nullptr);
          }
          return self->ServerTrailingMetadataNonProcessingMode(
              std::move(metadata));
        }
        auto send_request = [self, &metadata, is_trailers_only]() {
          if (is_trailers_only) {
            return self->SendServerInitialMetadataRequest(
                metadata, /*end_of_stream=*/true);
          }
          return self->SendServerTrailingMetadataRequest(metadata);
        };
        if (self->config().observability_mode) {
          // Observability mode: send trailers asynchronously without awaiting
          // external processor response.
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Sending server trailing metadata (observability "
                 "mode)";
          Timestamp start_time = Timestamp::Now();
          return Seq(send_request(),
                     [self, metadata = std::move(metadata), start_time,
                      is_trailers_only](StatusFlag status) mutable
                         -> absl::AnyInvocable<Poll<StatusFlag>()> {
                       if (!status.ok() && !self->IsFailOpenAllowed()) {
                         return Immediate(StatusFlag(Failure{}));
                       }
                       if (is_trailers_only) {
                         return self->ServerTrailersOnlyObservabilityMode(
                             std::move(metadata), start_time);
                       }
                       return self->ServerTrailingMetadataObservabilityMode(
                           std::move(metadata), start_time);
                     });
        }
        if (self->drain_requested_) {
          // Drain requested by external processor; wait for stream termination.
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Handling server trailing metadata in drain mode";
          return self->ServerTrailingMetadataDrainMode(std::move(metadata));
        }
        // Normal mode: send to ext_proc and latch metadata until response
        // arrives.
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Sending server trailing metadata (normal mode)";
        auto send_promise = send_request();
        self->server_trailing_metadata_latch_.Set(std::move(metadata));
        return send_promise;
      });
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerTrailingMetadataDrainMode(
    ServerMetadataHandle metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataDrainMode metadata: "
      << metadata->DebugString();
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

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerTrailingMetadataNonProcessingMode(
    ServerMetadataHandle metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataNonProcessingMode metadata: "
      << metadata->DebugString();
  handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
  return Immediate(StatusFlag(Success{}));
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerTrailersOnlyObservabilityMode(
    ServerMetadataHandle metadata, Timestamp start_time) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailersOnlyObservabilityMode metadata: "
      << metadata->DebugString();
  ext_proc_filter_->RecordServerHeadersDuration(
      (Timestamp::Now() - start_time).seconds());
  handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
  return Immediate(StatusFlag(Success{}));
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerTrailingMetadataObservabilityMode(
    ServerMetadataHandle metadata, Timestamp start_time) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataObservabilityMode metadata: "
      << metadata->DebugString();
  ext_proc_filter_->RecordServerTrailersDuration(
      (Timestamp::Now() - start_time).seconds());
  handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
  return Immediate(StatusFlag(Success{}));
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerTrailersOnlyNormalMode(
    ServerMetadataHandle metadata, Timestamp start_time,
    absl::StatusOr<ExtProcResponse> response) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailersOnlyNormalMode metadata: "
      << metadata->DebugString();
  if (!response.ok()) {
    GRPC_TRACE_LOG(ext_proc_filter, ERROR)
        << "ExtProc: External processor returned error status for "
           "trailers-only headers: "
        << response.status();
    return Immediate(StatusFlag(Failure{}));
  }
  const auto& cfg = config();
  if (const auto* immediate = std::get_if<ExtProcResponse::ImmediateResponse>(
          &response->response)) {
    auto error_md = CancelledServerMetadataFromStatus(
        static_cast<grpc_status_code>(immediate->status), immediate->details);
    (void)ApplyHeaderMutations<ExtProcResponse::ImmediateResponse>(
        *response, cfg.mutation_rules, *error_md);
    handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
    return Immediate(StatusFlag(Success{}));
  }
  if (auto status = ApplyHeaderMutations<ExtProcResponse::ResponseHeaders>(
          *response, cfg.mutation_rules, *metadata);
      !status.ok()) {
    SetStreamError(status);
    return Immediate(StatusFlag(Failure{}));
  }
  ext_proc_filter_->RecordServerHeadersDuration(
      (Timestamp::Now() - start_time).seconds());
  handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
  return Immediate(StatusFlag(Success{}));
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerTrailingMetadataNormalMode(
    ServerMetadataHandle metadata, Timestamp start_time,
    absl::StatusOr<ExtProcResponse> response) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerTrailingMetadataNormalMode metadata: "
      << metadata->DebugString();
  if (!response.ok()) {
    GRPC_TRACE_LOG(ext_proc_filter, ERROR)
        << "ExtProc: External processor returned error status for server "
           "trailers: "
        << response.status();
    return Immediate(StatusFlag(Failure{}));
  }
  if (const auto* immediate = std::get_if<ExtProcResponse::ImmediateResponse>(
          &response->response)) {
    auto error_md = CancelledServerMetadataFromStatus(
        static_cast<grpc_status_code>(immediate->status), immediate->details);
    (void)ApplyHeaderMutations<ExtProcResponse::ImmediateResponse>(
        *response, config().mutation_rules, *error_md);
    handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
    return Immediate(StatusFlag(Success{}));
  }
  if (auto status = ApplyHeaderMutations<ExtProcResponse::ResponseTrailers>(
          *response, config().mutation_rules, *metadata);
      !status.ok()) {
    SetStreamError(status);
    return Immediate(StatusFlag(Failure{}));
  }
  ext_proc_filter_->RecordServerTrailersDuration(
      (Timestamp::Now() - start_time).seconds());
  handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
  return Immediate(StatusFlag(Success{}));
}

//
// ExtProcFilter::ExtProcCall Server Message Processing
//

StatusFlag ExtProcFilter::ExtProcCall::PassThroughServerMessage(
    MessageHandle message) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Passing through server message directly";
  if (IsStreamFailureFatal()) {
    return Failure{};
  }
  handler_.SpawnPushMessage(std::move(message));
  return Success{};
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessServerMessage(MessageHandle message,
                                                 bool observability_mode) {
  if (!IsStreamClosed() && !ext_proc_stream_half_closed_) {
    return Seq(SendServerMessageRequest(message),
               [self = Ref(), message = std::move(message),
                observability_mode](StatusFlag status) mutable -> StatusFlag {
                 if (status.ok() && !self->IsStreamClosed()) {
                   if (observability_mode) {
                     // In observability mode, forward original message
                     // downstream immediately.
                     self->handler_.SpawnPushMessage(std::move(message));
                   }
                   return Success{};
                 }
                 // If send failed or stream closed, fall back to pass-through.
                 return self->PassThroughServerMessage(std::move(message));
               });
  }
  return Immediate(PassThroughServerMessage(std::move(message)));
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::SendServerMessageRequest(
    const MessageHandle& message) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Sending server body message request to side-stream";
  if (!config().observability_mode) {
    outstanding_s2c_messages_++;
  }
  std::string message_bytes;
  if (message != nullptr) {
    message_bytes = message->payload()->JoinIntoString();
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
  return Seq(SendMessageToSideStream(std::move(*payload)),
             [self = Ref()](StatusFlag status) {
               if (status.ok()) {
                 self->first_body_message_sent_ = true;
               }
               return status;
             });
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessServerMessagesFromServer() {
  if (is_trailers_only_) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Skipping server message processing (trailers-only "
           "response)";
    return Immediate(StatusFlag(Success{}));
  }
  return Seq(
      ForEach(
          MessagesFrom(initiator_),
          [self = Ref()](MessageHandle message) mutable
              -> absl::AnyInvocable<Poll<StatusFlag>()> {
            const bool send_body = self->processing_mode().send_response_body &&
                                   !self->IsStreamClosed();
            if (!send_body) {
              // Response body processing disabled or stream closed.
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << "ExtProc: Server message non-processing mode";
              return self->ServerMessageNonProcessingMode(std::move(message));
            } else if (self->config().observability_mode) {
              // Observability mode.
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << "ExtProc: Server message observability mode";
              return self->ServerMessageObservabilityMode(std::move(message));
            } else {
              // Normal mode.
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << "ExtProc: Server message normal mode";
              return self->ServerMessageNormalMode(std::move(message));
            }
          }),
      [self = Ref()]() {
        self->s2c_writes_done_ = true;
        return StatusFlag(Success{});
      });
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessServerMessageResponse(
    absl::StatusOr<ExtProcResponse> response) {
  if (is_trailers_only_) {
    return Immediate(StatusFlag(Success{}));
  }
  if (!response.ok()) {
    GRPC_TRACE_LOG(ext_proc_filter, ERROR)
        << "ExtProc: External processor returned error status for server body: "
        << response.status();
    return Immediate(StatusFlag(Failure{}));
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Processing external processor response for server body";
  const auto& response_body =
      std::get<ExtProcResponse::ResponseBody>((*response).response);
  auto slice = Slice::FromCopiedString(response_body.mutation.body);
  auto new_msg = handler_.arena()->MakePooled<Message>(
      SliceBuffer(std::move(slice)), /*flags=*/0);
  handler_.SpawnPushMessage(std::move(new_msg));
  return Immediate(StatusFlag(Success{}));
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerMessageNonProcessingMode(
    MessageHandle message) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerMessageNonProcessingMode";
  handler_.SpawnPushMessage(std::move(message));
  return Immediate(StatusFlag(Success{}));
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerMessageObservabilityMode(
    MessageHandle message) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerMessageObservabilityMode";
  return ProcessServerMessage(std::move(message),
                              /*observability_mode=*/true);
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ServerMessageNormalMode(MessageHandle message) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO) << "ExtProc: ServerMessageNormalMode";
  if (drain_requested_) {
    // If drain was requested, wait for stream status before pushing message.
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
  return ProcessServerMessage(std::move(message),
                              /*observability_mode=*/false);
}

//
// ExtProcFilter::ExtProcCall Client Message Processing
//

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::SendClientMessageRequest(
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

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessClientMessagesFromClient() {
  const bool send_request_body =
      config().processing_mode->send_request_body && !IsStreamClosed();
  if (!send_request_body) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: Client message non-processing mode (processing disabled "
           "or closed)";
    return ClientMessageNonProcessingMode();
  }
  return TrySeq(
      ForEach(
          MessagesFrom(handler_),
          [self = Ref()](MessageHandle message) mutable
              -> absl::AnyInvocable<Poll<StatusFlag>()> {
            if (self->config().observability_mode) {
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << "ExtProc: Client message observability mode";
              return self->ClientMessageObservabilityMode(std::move(message));
            } else {
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << "ExtProc: Client message normal mode";
              return self->ClientMessageNormalModeSendOnly(std::move(message));
            }
          }),
      [self = Ref()]() mutable -> absl::AnyInvocable<Poll<StatusFlag>()> {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Sending client half-close to ext_proc";
        return self->SendClientHalfClose(
            /*observability_mode=*/self->config().observability_mode);
      });
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessClientMessageResponse(
    absl::StatusOr<ExtProcResponse> result) {
  const bool send_request_body =
      processing_mode().send_request_body && !IsStreamClosed();
  if (!send_request_body || config().observability_mode) {
    return Immediate(StatusFlag(Success{}));
  }
  if (!result.ok()) {
    GRPC_TRACE_LOG(ext_proc_filter, ERROR)
        << "ExtProc: External processor returned error status for client body: "
        << result.status();
    absl::Status closed_status = GetStreamClosedStatus(result.status());
    return Immediate(StatusFlag(closed_status.ok()));
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Processing external processor response for client body";
  if (const auto* request_body =
          std::get_if<ExtProcResponse::RequestBody>(&result->response)) {
    if (!request_body->mutation.end_of_stream_without_message) {
      auto slice = Slice::FromCopiedString(request_body->mutation.body);
      auto new_msg = initiator_.arena()->MakePooled<Message>(
          SliceBuffer(std::move(slice)), /*flags=*/0);
      initiator_.SpawnPushMessage(std::move(new_msg));
    }
    if (request_body->mutation.end_of_stream ||
        request_body->mutation.end_of_stream_without_message) {
      Timestamp start_time = Timestamp::Now();
      if (c2s_writes_done_ || !IsStreamClosed()) {
        ext_proc_filter_->RecordClientHalfCloseDuration(
            (Timestamp::Now() - start_time).seconds());
        initiator_.SpawnFinishSends();
      }
    }
  }
  return Immediate(StatusFlag(Success{}));
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ClientMessageNonProcessingMode() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ClientToServerMessagesNonProcessingMode started";
  return Seq(
      ForEach(MessagesFrom(handler_),
              [self = Ref()](MessageHandle message) mutable -> StatusFlag {
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << "ExtProc: ClientToServerMessagesNonProcessingMode got "
                       "message";
                if (self->ext_proc_set_eos_) {
                  self->SetStreamError(absl::InternalError(
                      "Client sends closed by external processor"));
                  return Failure{};
                }
                self->initiator_.SpawnPushMessage(std::move(message));
                return Success{};
              }),
      [self = Ref()]() mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ClientToServerMessagesNonProcessingMode finished "
               "sends";
        self->initiator_.SpawnFinishSends();
        return StatusFlag(Success{});
      });
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ProcessClientMessage(MessageHandle message,
                                                 bool observability_mode) {
  // TODO(rishesh): removed this check once PH2 work is done
  if (ext_proc_set_eos_) {
    SetStreamError(
        absl::InternalError("Client sends closed by external processor"));
    return Immediate(StatusFlag(Failure{}));
  }
  if (!IsStreamClosed() && !ext_proc_stream_half_closed_) {
    return Seq(
        SendClientMessageRequest(message,
                                 /*end_of_stream=*/false,
                                 /*end_of_stream_without_message=*/false),
        [self = Ref(), message = std::move(message),
         observability_mode](StatusFlag status) mutable -> StatusFlag {
          if (!status.ok() || self->IsStreamClosed()) {
            if (self->IsStreamFailureFatal()) {
              return Failure{};
            }
            self->initiator_.SpawnPushMessage(std::move(message));
          } else if (observability_mode) {
            // In observability mode, forward original message immediately after
            // sending to ext_proc.
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

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::SendClientHalfClose(bool observability_mode) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: SendClientHalfClose invoked";
  Timestamp start_time = Timestamp::Now();
  c2s_writes_done_ = true;
  if (ext_proc_set_eos_) {
    return Immediate(StatusFlag(Success{}));
  }
  if (!observability_mode && drain_requested_) {
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
        [self = Ref(), start_time,
         observability_mode](StatusFlag status) mutable -> StatusFlag {
          if (!status.ok() && self->IsStreamFailureFatal()) {
            return Failure{};
          }
          self->ext_proc_filter_->RecordClientHalfCloseDuration(
              (Timestamp::Now() - start_time).seconds());
          if (!status.ok() || self->IsStreamClosed() || observability_mode) {
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

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ClientMessageObservabilityMode(
    MessageHandle message) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ClientMessageObservabilityMode";
  return ProcessClientMessage(std::move(message),
                              /*observability_mode=*/true);
}

absl::AnyInvocable<Poll<StatusFlag>()>
ExtProcFilter::ExtProcCall::ClientMessageNormalModeSendOnly(
    MessageHandle message) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ClientMessageNormalModeSendOnly";
  if (drain_requested_) {
    // If drain was requested, wait for stream status before pushing message.
    return Seq(WaitForStreamStatus(),
               [self = Ref(), message = std::move(message)](
                   absl::Status status) mutable -> StatusFlag {
                 if (!status.ok() && self->IsStreamFailureFatal()) {
                   return Failure{};
                 }
                 if (message != nullptr) {
                   self->initiator_.SpawnPushMessage(std::move(message));
                 }
                 return Success{};
               });
  }
  return ProcessClientMessage(std::move(message),
                              /*observability_mode=*/false);
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
  handler.SpawnInfallible(
      "ext_proc_call",
      [handler, ext_proc_filter = RefAsSubclass<ExtProcFilter>()]() mutable
          -> absl::AnyInvocable<Poll<Empty>()> {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: InterceptCall promise chain start";
        auto transport = ext_proc_filter->channel()->transport();
        // This shouldn't ever happen; added as a defensive check.
        if (transport == nullptr) {
          handler.PushServerInitialMetadata(ServerMetadataFromStatus(
              absl::InternalError("External processor transport unavailable")));
          return []() -> Poll<Empty> { return Empty{}; };
        }
        auto ext_proc_call = MakeRefCounted<ExtProcCall>(
            ext_proc_filter, std::move(transport), handler);
        return Map(ext_proc_call->Run(), [](StatusFlag) { return Empty{}; });
      });
}

}  // namespace grpc_core
