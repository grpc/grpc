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
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
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

// High-Level Architecture of 3 Concurrent Pipeline Loops:
//
//  LOOP 1: Request Pipeline Loop [ClientToServerCall()]
//  +-----------------------------------------------------------------------+
//  | TrySeq(                                                               |
//  |     MakeRefCounted<ClientInitialMetadataProcessor>(Ref())             |
//  |         ->ProcessFromClientToExtProcServer(),                         |
//  |     MakeRefCounted<ClientMessageProcessor>(Ref(), request_attributes_)|
//  |         ->ProcessFromClientToExtProcServer())                         |
//  +-----------------------------------------------------------------------+
//                                     ||
//                          Joined via TryJoin()
//                                     ||
//  LOOP 2: Side-Stream Pull Loop [PullMessagesFromSideStream()]
//  +-----------------------------------------------------------------------+
//  | PullMessagesFromSideStream()                                          |
//  |   -> Loop: streaming_call_->PullMessage()                             |
//  |   -> ProcessSideStreamResponse() -> CompleteOutstandingProcessors()   |
//  |   -> HandleSideStreamStatus(status)                                   |
//  +-----------------------------------------------------------------------+
//                                     ||
//                       Spawned when child call starts
//                                     ||
//  LOOP 3: Response Pipeline Loop [ServerToClientCall()]
//  +-----------------------------------------------------------------------+
//  | TrySeq(                                                               |
//  |     MakeRefCounted<ServerInitialMetadataProcessor>(Ref())             |
//  |         ->ProcessFromServerToExtProcServer(),                         |
//  |     MakeRefCounted<ServerMessageProcessor>(Ref())                     |
//  |         ->ProcessFromServerToExtProcServer(),                         |
//  |     MakeRefCounted<ServerTrailingMetadataProcessor>(Ref())            |
//  |         ->ProcessFromServerToExtProcServer())                         |
//  +-----------------------------------------------------------------------+

class ExtProcFilter::ExtProcCall final : public DualRefCounted<ExtProcCall> {
 public:
  ExtProcCall(RefCountedPtr<ExtProcFilter> ext_proc_filter,
              RefCountedPtr<XdsTransportFactory::XdsTransport> transport,
              CallHandler handler);

  ~ExtProcCall() override;

  // Main entry point for an external processor call. Spawns and manages the
  // concurrent request (client-to-server) and response (server-to-client)
  // processing pipelines alongside side-stream message pulling.
  absl::AnyInvocable<Poll<absl::Status>()> Run();

 private:
  // Orchestrates the processing of client initial metadata (request headers)
  // across both directions: client -> ext_proc server, and ext_proc response ->
  // backend server.
  class ClientInitialMetadataProcessor
      : public RefCounted<ClientInitialMetadataProcessor> {
   public:
    explicit ClientInitialMetadataProcessor(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call)
        : call_(std::move(call)) {}

    // Processes client initial metadata on the client-to-extproc path.
    absl::AnyInvocable<Poll<absl::Status>()> ProcessFromClientToExtProcServer();
    // Processes the ext_proc server response for client initial metadata and
    // forwards the mutated metadata to the backend server.
    absl::AnyInvocable<Poll<absl::Status>()> ProcessFromExtProcServerToClient(
        absl::StatusOr<ExtProcResponse> response);

   private:
    // Prepares the ProcessingRequest protobuf message for client initial
    // metadata.
    absl::AnyInvocable<Poll<absl::Status>()> SendClientInitialMetadataRequest(
        const ClientMetadataHandle& metadata,
        absl::string_view default_authority);
    // Sends the client initial metadata request to the ext_proc server and
    // handles the result.
    absl::AnyInvocable<Poll<absl::Status>()> SendAndHandleClientInitialMetadata(
        const ClientMetadataHandle& metadata);
    // Initializes and starts the child call to the backend server, and spawns
    // the background task for the server-to-client response path.
    absl::AnyInvocable<Poll<absl::Status>()> StartChildCall(
        ClientMetadataHandle metadata,
        ::google_protobuf_Struct* attributes = nullptr,
        Timestamp start_time = Timestamp::InfPast());
    // Forwards client initial metadata to the backend server without sending to
    // ext_proc when request header processing is disabled. Prepares request
    // attributes if body processing is enabled.
    absl::AnyInvocable<Poll<absl::Status>()> NonProcessingMode(
        ClientMetadataHandle metadata);
    // Handles client initial metadata in observability mode.
    absl::AnyInvocable<Poll<absl::Status>()> ObservabilityMode(
        ClientMetadataHandle metadata, Timestamp start_time);
    // Intercepts, sends to ext_proc, and applies mutations to client initial
    // metadata.
    absl::AnyInvocable<Poll<absl::Status>()> NormalMode(
        ClientMetadataHandle metadata, Timestamp start_time,
        absl::StatusOr<ExtProcResponse> response);

    RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
  };

  // Orchestrates the processing of server initial metadata (response headers)
  // across both directions: server -> ext_proc server, and ext_proc response ->
  // client.
  class ServerInitialMetadataProcessor
      : public RefCounted<ServerInitialMetadataProcessor> {
   public:
    explicit ServerInitialMetadataProcessor(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call)
        : call_(std::move(call)) {}

    // Prepares the ProcessingRequest protobuf message for server initial
    // metadata and sends it over the ext_proc stream.
    static absl::AnyInvocable<Poll<absl::Status>()>
    SendServerInitialMetadataRequest(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call,
        const ServerMetadataHandle& metadata, bool end_of_stream = false);

    // Processes server initial metadata on the server-to-extproc path.
    absl::AnyInvocable<Poll<absl::Status>()> ProcessFromServerToExtProcServer();
    // Processes the ext_proc server response for server initial metadata and
    // forwards the mutated metadata to the client.
    absl::AnyInvocable<Poll<absl::Status>()> ProcessFromExtProcServerToClient(
        absl::StatusOr<ExtProcResponse> response);

   private:
    // Non-processing mode: pushes server initial metadata directly to client.
    absl::AnyInvocable<Poll<absl::Status>()> NonProcessingMode(
        ServerMetadataHandle metadata);
    // Observability mode: records duration and pushes server initial metadata
    // to client.
    absl::AnyInvocable<Poll<absl::Status>()> ObservabilityMode(
        ServerMetadataHandle metadata, Timestamp start_time);
    // Normal mode: gets response from ext_proc server, applies mutations,
    // handles errors, and pushes to client.
    absl::AnyInvocable<Poll<absl::Status>()> NormalMode(
        ServerMetadataHandle metadata, Timestamp start_time,
        absl::StatusOr<ExtProcResponse> response);
    // Handles server initial metadata when the external processor has requested
    // a drain.
    absl::AnyInvocable<Poll<absl::Status>()> DrainMode(
        ServerMetadataHandle metadata);

    RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
  };

  // Orchestrates the processing of server trailing metadata (response trailers)
  // across both directions: server -> ext_proc server, and ext_proc response ->
  // client.
  class ServerTrailingMetadataProcessor
      : public RefCounted<ServerTrailingMetadataProcessor> {
   public:
    explicit ServerTrailingMetadataProcessor(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call)
        : call_(std::move(call)) {}

    // Prepares the ProcessingRequest protobuf message for server trailing
    // metadata (trailers) and sends it over the ext_proc stream.
    static absl::AnyInvocable<Poll<absl::Status>()>
    SendServerTrailingMetadataRequest(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call,
        const ServerMetadataHandle& metadata);

    // Processes server trailing metadata on the server-to-extproc path.
    absl::AnyInvocable<Poll<absl::Status>()> ProcessFromServerToExtProcServer();
    // Processes the ext_proc server response for server trailing metadata and
    // forwards the mutated metadata to the client.
    absl::AnyInvocable<Poll<absl::Status>()> ProcessFromExtProcServerToClient(
        absl::StatusOr<ExtProcResponse> response);

   private:
    // Orchestrates trailers-only flow for server-to-extproc side.
    absl::AnyInvocable<Poll<absl::Status>()> TrailersOnly();
    // Orchestrates normal trailers flow for server-to-extproc side.
    absl::AnyInvocable<Poll<absl::Status>()> NormalTrailers();
    // Handles server trailing metadata when drain operation was requested.
    absl::AnyInvocable<Poll<absl::Status>()> DrainMode(
        ServerMetadataHandle metadata);
    absl::AnyInvocable<Poll<absl::Status>()> NormalTrailersHelper(
        ServerMetadataHandle metadata);
    // Non-processing mode: closes body pipe sender if needed and forwards
    // server trailers to client.
    absl::AnyInvocable<Poll<absl::Status>()> NonProcessingMode(
        ServerMetadataHandle metadata);
    // Handles trailers-only RPC trailing metadata in observability mode.
    absl::AnyInvocable<Poll<absl::Status>()> TrailersOnlyObservabilityMode(
        ServerMetadataHandle metadata, Timestamp start_time);
    // Handles normal trailing metadata in observability mode.
    absl::AnyInvocable<Poll<absl::Status>()> ObservabilityMode(
        ServerMetadataHandle metadata, Timestamp start_time);
    // Handles trailers-only RPC trailing metadata in normal processing mode.
    absl::AnyInvocable<Poll<absl::Status>()> TrailersOnlyNormalMode(
        ServerMetadataHandle metadata, Timestamp start_time,
        absl::StatusOr<ExtProcResponse> response);
    absl::AnyInvocable<Poll<absl::Status>()> NormalMode(
        ServerMetadataHandle metadata, Timestamp start_time,
        absl::StatusOr<ExtProcResponse> response);

    RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
  };

  // Orchestrates server-to-client body messages across both directions:
  // backend server -> ext_proc server, and ext_proc response -> client.
  class ServerMessageProcessor : public RefCounted<ServerMessageProcessor> {
   public:
    explicit ServerMessageProcessor(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call)
        : call_(std::move(call)) {}

    // Prepares the ProcessingRequest protobuf message for server response body
    // and sends it over the ext_proc stream.
    static absl::AnyInvocable<Poll<absl::Status>()> SendServerMessageRequest(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call,
        const MessageHandle& message);

    // Processes server-to-client messages on the backend-to-extproc path.
    absl::AnyInvocable<Poll<absl::Status>()> ProcessFromServerToExtProcServer();
    // Processes the ext_proc server response for server-to-client body
    // messages.
    absl::AnyInvocable<Poll<absl::Status>()> ProcessFromExtProcServerToClient(
        absl::StatusOr<ExtProcResponse> response);

   private:
    // Forwards server message to client without ext_proc processing.
    static absl::AnyInvocable<Poll<absl::Status>()> NonProcessingMode(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message);
    // Handles server-to-client message in observability mode.
    static absl::AnyInvocable<Poll<absl::Status>()> ObservabilityMode(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message);
    // Intercepts server-to-client message in normal mode.
    static absl::AnyInvocable<Poll<absl::Status>()> NormalMode(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message);

    // Passes through server message directly to client if stream failure is
    // non-fatal, or returns error status if fatal.
    static absl::Status PassThroughServerMessage(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message);

    // Processes a server message by sending a request to ext_proc if stream is
    // open, and passing through the message to the client.
    static absl::AnyInvocable<Poll<absl::Status>()> ProcessServerMessage(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message,
        bool observability_mode);

    RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
  };

  // Orchestrates client-to-server body messages across both directions:
  // client -> ext_proc server, and ext_proc response -> backend server.
  class ClientMessageProcessor : public RefCounted<ClientMessageProcessor> {
   public:
    ClientMessageProcessor(RefCountedPtr<ExtProcFilter::ExtProcCall> call,
                           ::google_protobuf_Struct* attributes)
        : call_(std::move(call)), attributes_(attributes) {}

    // Prepares the ProcessingRequest protobuf message for client body messages.
    static absl::AnyInvocable<Poll<absl::Status>()> SendClientMessageRequest(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call,
        const MessageHandle& message, bool end_of_stream,
        bool end_of_stream_without_message,
        ::google_protobuf_Struct* attributes);

    // Processes client-to-server messages on the client-to-extproc path.
    absl::AnyInvocable<Poll<absl::Status>()> ProcessFromClientToExtProcServer();
    // Processes the ext_proc server response for client-to-server body messages
    // and forwards mutated messages/finish sends to the backend.
    absl::AnyInvocable<Poll<absl::Status>()> ProcessFromExtProcServerToClient(
        absl::StatusOr<ExtProcResponse> result);

   private:
    // Intercepts client-to-server messages in non processing mode.
    absl::AnyInvocable<Poll<absl::Status>()> NonProcessingMode();
    // Handles client-to-server message in observability mode.
    static absl::AnyInvocable<Poll<absl::Status>()> ObservabilityMode(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call,
        ::google_protobuf_Struct* attributes, MessageHandle message);
    // Intercepts client-to-server message in normal mode.
    static absl::AnyInvocable<Poll<absl::Status>()> NormalModeSendOnly(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call,
        ::google_protobuf_Struct* attributes, MessageHandle message);

    // Processes a client message by sending a request to ext_proc if stream is
    // open, and forwarding to backend server based on mode.
    static absl::AnyInvocable<Poll<absl::Status>()> ProcessClientMessage(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message,
        ::google_protobuf_Struct* attributes, bool observability_mode);
    // Sends client half-close request to ext_proc or finishes sends based on
    // mode.
    static absl::AnyInvocable<Poll<absl::Status>()> SendClientHalfClose(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call,
        ::google_protobuf_Struct* attributes, bool observability_mode);

    RefCountedPtr<ExtProcFilter::ExtProcCall> call_;
    ::google_protobuf_Struct* attributes_;
  };

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

  // Continuously pulls response messages from the external processor
  // side-stream and dispatches them to their respective response processors.
  absl::AnyInvocable<Poll<absl::Status>()> PullMessagesFromSideStream();

  // Sends a message to the external processor side-stream.
  // Coordinates client-side and server-side message sources so that only one
  // send is in-flight on streaming_call_ at a time, using a single Waker
  // without any queue or vector allocations.
  absl::AnyInvocable<Poll<absl::Status>()> SendMessageToSideStream(
      absl::AnyInvocable<absl::StatusOr<std::string>()> payload_generator);

  // Handles the request path (Client to Server).
  absl::AnyInvocable<Poll<absl::Status>()> ClientToServerCall();

  // Handles the response path (Server to Client).
  // This function sets up a pipeline to process server initial metadata,
  // response messages, and server trailing metadata, potentially intercepting
  // and mutating them via the ext_proc server.
  //
  // It also watches for ext_proc stream errors and aborts the call if a failure
  // occurs and fail-open is not allowed.
  absl::AnyInvocable<Poll<absl::Status>()> ServerToClientCall();

  // Parses and processes an incoming response message payload from the
  // side-stream.
  absl::AnyInvocable<Poll<absl::Status>()> ProcessSideStreamResponse(
      absl::string_view payload);

  // Handles transport status updates/closure on the ext_proc side-stream.
  void HandleSideStreamStatus(absl::Status status);

  const Config& config() const { return *ext_proc_filter_->config_; }

  bool IsFirstMessageOnStream() {
    return std::exchange(is_first_message_on_ext_proc_stream_, false);
  }

  bool IsFailOpenAllowed() const {
    const bool allow = config().failure_mode_allow.value_or(false);
    if (config().observability_mode) return allow;
    return allow && !first_body_message_sent_;
  }

  // Returns true if the stream closed with an error and fail-open mode is not
  // permitted for this call (i.e. the stream error must fail the RPC).
  bool IsStreamFailureFatal() const {
    if (IsFailOpenAllowed()) return false;
    MutexLock lock(&mu_);
    return stream_status_value_.has_value() && !stream_status_value_->ok();
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

  bool IsStreamClosed() const {
    MutexLock lock(&mu_);
    return stream_status_value_.has_value();
  }

  absl::Status GetStreamStatus() const {
    MutexLock lock(&mu_);
    return stream_status_value_.value_or(absl::OkStatus());
  }

  void SetStreamStatus(absl::Status status) {
    MutexLock lock(&mu_);
    if (!stream_status_value_.has_value()) {
      stream_status_value_ = status;
      stream_status_.Set();
    }
  }

  // Evaluates the status to return when the external processor stream is
  // closed or when a send fails. Respects IsFailOpenAllowed() (which handles
  // both failure_mode_allow and observability_mode) by returning OkStatus()
  // when fail-open is permitted.
  absl::Status GetStreamClosedStatus(
      absl::Status default_error = absl::CancelledError("Stream closed")) {
    MutexLock lock(&mu_);
    if (stream_status_value_.has_value()) {
      return *stream_status_value_;
    }
    if (IsFailOpenAllowed()) {
      return absl::OkStatus();
    }
    return default_error;
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

  void SetStreamError(absl::Status status) {
    if (!status.ok()) {
      auto error_md = CancelledServerMetadataFromStatus(status);
      handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
      if (child_call_started_) {
        initiator_.SpawnCancel();
      }
    }
    SetStreamStatus(status);
    CompleteOutstandingProcessors(status);
    CloseStream();
  }

  void CloseStream() {
    RefCountedPtr<XdsStreamingCallPromiseWrapper> streaming_call;
    {
      MutexLock lock(&mu_);
      if (!stream_status_value_.has_value()) {
        stream_status_value_ = absl::OkStatus();
        stream_status_.Set();
      }
      streaming_call = std::move(streaming_call_);
    }
    ext_proc_send_state_.store(SendState::kSendFailed);
    ext_proc_send_waker_.Wakeup();
    streaming_call.reset();
  }

  void Orphaned() override { CloseStream(); }

  void CompleteOutstandingProcessors(absl::StatusOr<ExtProcResponse> response);

  // Flags tracking whether the respective ext_proc response messages have
  // been received from the external processor.
  bool request_headers_received_ = false;
  bool response_headers_received_ = false;
  bool response_trailers_received_ = false;

  // Inter-activity latches for synchronizing metadata between the main call
  // pipeline and ext_proc response processing tasks.
  InterActivityLatch<ClientMetadataHandle> client_initial_metadata_latch_;
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
  bool child_call_started_ = false;
  InterActivityLatch<void> stream_status_;
  std::optional<absl::Status> stream_status_value_ ABSL_GUARDED_BY(mu_);

  // Atomic send state for lock-free coordination between client-side and
  // server-side message senders.
  std::atomic<SendState> ext_proc_send_state_{SendState::kIdle};
  // Waker for queuing send promises when a send operation is already in flight.
  Waker ext_proc_send_waker_;

  mutable Mutex mu_;

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
    if (ext_proc_filter_->event_engine_ != nullptr) {
      ext_proc_filter_->event_engine_->RunAfter(
          config().deferred_close_timeout,
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

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::PullMessagesFromSideStream() {
  return Seq(
      Loop([self = Ref()]() -> Promise<LoopCtl<absl::Status>> {
        RefCountedPtr<XdsStreamingCallPromiseWrapper> streaming_call;
        {
          MutexLock lock(&self->mu_);
          streaming_call = self->streaming_call_;
        }
        if (streaming_call == nullptr) {
          return Immediate(LoopCtl<absl::Status>(absl::OkStatus()));
        }
        return Seq(
            streaming_call->PullMessage(),
            [self](std::optional<std::string> msg)
                -> Promise<LoopCtl<absl::Status>> {
              if (!msg.has_value()) {
                return Immediate(LoopCtl<absl::Status>(absl::OkStatus()));
              }
              return Seq(self->ProcessSideStreamResponse(*msg),
                         [](absl::Status status) -> LoopCtl<absl::Status> {
                           if (!status.ok()) return status;
                           return Continue();
                         });
            });
      }),
      [self = Ref()](absl::Status status) -> Promise<absl::Status> {
        if (!status.ok()) return Immediate(status);
        RefCountedPtr<XdsStreamingCallPromiseWrapper> streaming_call;
        {
          MutexLock lock(&self->mu_);
          streaming_call = self->streaming_call_;
        }
        if (streaming_call == nullptr) {
          return Immediate(self->GetStreamClosedStatus(absl::OkStatus()));
        }
        return streaming_call->PullServerTrailingMetadata();
      },
      [self = Ref()](absl::Status status) -> absl::Status {
        self->HandleSideStreamStatus(status);
        absl::Status final_status = self->GetStreamClosedStatus(status);
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProcCall " << self.get()
            << " PullMessagesFromSideStream finished with status: "
            << final_status;
        return final_status;
      });
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ProcessSideStreamResponse(
    absl::string_view payload) {
  // In observability mode, we only log the message and ignore it.
  // We must continue reading the stream to keep it alive.
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this
        << " message received in observability mode (ignored), size="
        << payload.size();
    return Immediate(absl::OkStatus());
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " message received, size=" << payload.size();
  // Parse the response from the external processor.
  auto parsed_response = ExtProcResponse::Parse(payload);
  if (!parsed_response.ok()) {
    SetStreamError(parsed_response.status());
    return Immediate(parsed_response.status());
  }
  // If the server requests a drain, we half-close the stream to signal
  // we are done sending requests.
  if ((*parsed_response).request_drain) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this << " received request_drain=true";
    drain_requested_ = true;
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
  // Dispatch the parsed response to the appropriate processor based on the
  // response type.
  const auto& processing_mode = *config().processing_mode;
  auto return_error = [this](absl::string_view message)
      -> absl::AnyInvocable<Poll<absl::Status>()> {
    auto error = absl::InternalError(message);
    SetStreamError(error);
    return Immediate(error);
  };
  return Match(
      (*parsed_response).response,
      [&](const ExtProcResponse::ImmediateResponse&)
          -> absl::AnyInvocable<Poll<absl::Status>()> {
        if (config().disable_immediate_response || !server_trailers_sent_) {
          return return_error(
              config().disable_immediate_response
                  ? "unhandled immediate response due to config disabled it"
                  : "Immediate response received but trailers not sent to "
                    "ext_proc");
        }
        if (processing_mode.send_response_trailers) {
          response_trailers_received_ = true;
        }
        return MakeRefCounted<ServerTrailingMetadataProcessor>(Ref())
            ->ProcessFromExtProcServerToClient(std::move(*parsed_response));
      },
      [&](const ExtProcResponse::RequestHeaders&)
          -> absl::AnyInvocable<Poll<absl::Status>()> {
        if (!processing_mode.send_request_headers) {
          return return_error(
              "Received request headers response but request headers are "
              "disabled");
        }
        if (processing_mode.send_request_headers) {
          request_headers_received_ = true;
        }
        return MakeRefCounted<ClientInitialMetadataProcessor>(Ref())
            ->ProcessFromExtProcServerToClient(std::move(*parsed_response));
      },
      [&](const ExtProcResponse::ResponseHeaders&)
          -> absl::AnyInvocable<Poll<absl::Status>()> {
        if (!processing_mode.send_response_headers) {
          return return_error(
              "Received response headers response but response headers are "
              "disabled");
        }
        if (processing_mode.send_response_headers) {
          response_headers_received_ = true;
        }
        if (is_trailers_only_) {
          return MakeRefCounted<ServerTrailingMetadataProcessor>(Ref())
              ->ProcessFromExtProcServerToClient(std::move(*parsed_response));
        }
        return MakeRefCounted<ServerInitialMetadataProcessor>(Ref())
            ->ProcessFromExtProcServerToClient(std::move(*parsed_response));
      },
      [&](const ExtProcResponse::ResponseTrailers&)
          -> absl::AnyInvocable<Poll<absl::Status>()> {
        if (!processing_mode.send_response_trailers) {
          return return_error(
              "Received response trailers response but response trailers are "
              "disabled");
        }
        if (is_trailers_only_) {
          return return_error(
              "Received response trailers response in a Trailers-Only call");
        }
        if (processing_mode.send_response_headers &&
            !response_headers_received_) {
          return return_error(
              "Received response trailers response before response headers "
              "response");
        }
        const bool s2c_body_outstanding =
            processing_mode.send_response_body && outstanding_s2c_messages_ > 0;
        if (s2c_body_outstanding) {
          return return_error(
              "Received response trailers response before all outstanding "
              "response body responses were received");
        }
        if (processing_mode.send_response_trailers) {
          response_trailers_received_ = true;
        }
        return MakeRefCounted<ServerTrailingMetadataProcessor>(Ref())
            ->ProcessFromExtProcServerToClient(std::move(*parsed_response));
      },
      [&](const ExtProcResponse::RequestBody& request_body)
          -> absl::AnyInvocable<Poll<absl::Status>()> {
        if (!processing_mode.send_request_body) {
          return return_error(
              "Received request body response but request body is disabled");
        }
        if (processing_mode.send_request_headers &&
            !request_headers_received_) {
          return return_error(
              "Received request body response before request headers "
              "response");
        }
        if (!DecrementOutstandingClientToServerMessages()) {
          return return_error(
              "Received unexpected request body response from external "
              "processor");
        }
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: Parsed request body response, eos: "
            << request_body.mutation.end_of_stream << ", eos_without_msg: "
            << request_body.mutation.end_of_stream_without_message;
        if (request_body.mutation.end_of_stream_without_message) {
          if (!c2s_writes_done_) {
            return return_error("Client sends closed by external processor");
          }
          ext_proc_set_eos_ = true;
        } else if (request_body.mutation.end_of_stream) {
          ext_proc_set_eos_ = true;
        }
        return MakeRefCounted<ClientMessageProcessor>(Ref(),
                                                      /*attributes=*/nullptr)
            ->ProcessFromExtProcServerToClient(std::move(*parsed_response));
      },
      [&](const ExtProcResponse::ResponseBody&)
          -> absl::AnyInvocable<Poll<absl::Status>()> {
        if (!processing_mode.send_response_body) {
          return return_error(
              "Received response body response but response body is disabled");
        }
        if (is_trailers_only_) {
          return return_error(
              "Received response body response in a Trailers-Only call");
        }
        if (processing_mode.send_response_headers &&
            !response_headers_received_) {
          return return_error(
              "Received response body response before response headers "
              "response");
        }
        if (processing_mode.send_response_trailers &&
            response_trailers_received_) {
          return return_error(
              "Received response body response after response trailers "
              "response");
        }
        if (outstanding_s2c_messages_ == 0) {
          return return_error(
              "Received unexpected response body response from external "
              "processor");
        }
        bool should_close = false;
        DecrementOutstandingServerToClientMessages(&should_close);
        return MakeRefCounted<ServerMessageProcessor>(Ref())
            ->ProcessFromExtProcServerToClient(std::move(*parsed_response));
      },
      [](std::monostate) -> absl::AnyInvocable<Poll<absl::Status>()> {
        return Immediate(absl::OkStatus());
      });
}

void ExtProcFilter::ExtProcCall::HandleSideStreamStatus(absl::Status status) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " status received: " << status;
  const bool has_outstanding_messages =
      outstanding_c2s_messages_ > 0 || outstanding_s2c_messages_ > 0;
  const bool must_drain = !config().observability_mode &&
                          (config().processing_mode->send_request_body ||
                           config().processing_mode->send_response_body);
  const bool drain_requested = drain_requested_;
  if (status.ok()) {
    if (must_drain && !drain_requested) {
      status = absl::InternalError("Stream closed cleanly without drain");
    } else if (has_outstanding_messages && !config().observability_mode) {
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
      stream_status_value_ = should_propagate_error ? status : absl::OkStatus();
      stream_status_.Set();
    }
  }
  if (!already_closed) {
    if (should_propagate_error) {
      auto error_md = CancelledServerMetadataFromStatus(status);
      handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
      if (child_call_started_) {
        initiator_.SpawnCancel();
      }
      CompleteOutstandingProcessors(status);
    } else {
      CompleteOutstandingProcessors(ExtProcResponse{});
    }
    CloseStream();
  }
}

void ExtProcFilter::ExtProcCall::CompleteOutstandingProcessors(
    absl::StatusOr<ExtProcResponse> response) {
  const auto& processing_mode = *config().processing_mode;
  if (processing_mode.send_request_headers && !request_headers_received_ &&
      client_initial_metadata_latch_.IsSet()) {
    auto promise = MakeRefCounted<ClientInitialMetadataProcessor>(Ref())
                       ->ProcessFromExtProcServerToClient(response);
    (void)promise();
  }
  if (processing_mode.send_response_headers && !response_headers_received_ &&
      server_initial_metadata_latch_.IsSet()) {
    auto promise = MakeRefCounted<ServerInitialMetadataProcessor>(Ref())
                       ->ProcessFromExtProcServerToClient(response);
    (void)promise();
  }
  if (processing_mode.send_response_trailers && !response_trailers_received_ &&
      server_trailing_metadata_latch_.IsSet()) {
    auto promise = MakeRefCounted<ServerTrailingMetadataProcessor>(Ref())
                       ->ProcessFromExtProcServerToClient(response);
    (void)promise();
  }
}

// This function role is to:
// - send the message to the ext proc server and wait for the send to get
// complete and then propagate the status
// - if a message is already in progress then wait for the in flight message to
// get complete and then send the previous one if the stream is not closed
// - Handle the failure mode allow
absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::SendMessageToSideStream(
    absl::AnyInvocable<absl::StatusOr<std::string>()> payload_generator) {
  return [this, ext_proc_call = Ref(),
          payload_generator = std::move(payload_generator),
          send_promise = absl::AnyInvocable<Poll<StatusFlag>()>()]() mutable
             -> Poll<absl::Status> {
    // On the initial poll, send_promise is nullptr. On subsequent polls while
    // an inner send is in-flight on the underlying transport, send_promise is
    // already initialized.
    if (send_promise == nullptr) {
      SendState expected = SendState::kIdle;
      if (!ext_proc_send_state_.compare_exchange_strong(
              expected, SendState::kSendInFlight)) {
        if (expected == SendState::kSendFailed) {
          return GetStreamClosedStatus();
        }
        ext_proc_send_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
        return Pending{};
      }
      // Generate the protobuf payload.
      auto payload = payload_generator();
      if (!payload.ok()) {
        ext_proc_send_state_.store(SendState::kIdle);
        ext_proc_send_waker_.Wakeup();
        return payload.status();
      }
      RefCountedPtr<XdsStreamingCallPromiseWrapper> streaming_call;
      {
        MutexLock lock(&mu_);
        streaming_call = streaming_call_;
      }
      if (streaming_call == nullptr) {
        ext_proc_send_state_.store(SendState::kIdle);
        ext_proc_send_waker_.Wakeup();
        return GetStreamClosedStatus();
      }
      // Start the send on the underlying transport wrapper.
      send_promise = streaming_call->PushMessage(std::move(*payload));
    }
    // Poll the in-flight send promise until it completes.
    auto poll = send_promise();
    if (poll.pending()) return Pending{};
    // Send completed. Release the in-flight slot and wake any waiting sender.
    ext_proc_send_state_.store(SendState::kIdle);
    ext_proc_send_waker_.Wakeup();
    // If the send failed, wait for the stream status from gRPC and evaluate
    // fail-open.
    if (!poll.value().ok()) {
      if (ext_proc_call->stream_status_.Wait()().pending()) {
        return Pending{};
      }
      return ext_proc_call->GetStreamClosedStatus(
          absl::InternalError("Send failed"));
    }
    return absl::OkStatus();
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
      TrySeq(MakeRefCounted<ServerInitialMetadataProcessor>(Ref())
                 ->ProcessFromServerToExtProcServer(),
             MakeRefCounted<ServerMessageProcessor>(Ref())
                 ->ProcessFromServerToExtProcServer(),
             MakeRefCounted<ServerTrailingMetadataProcessor>(Ref())
                 ->ProcessFromServerToExtProcServer());
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
            << (self->config().failure_mode_allow.has_value()
                    ? (*self->config().failure_mode_allow ? "true" : "false")
                    : "unset");
        if (!status.ok()) {
          return Immediate(status);
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
        if (self->child_call_started_) {
          self->initiator_.SpawnCancel();
        }
        self->CloseStream();
      }
      return *status;
    }
    return Pending{};
  };
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientToServerCall() {
  return Map(
      TrySeq(MakeRefCounted<ClientInitialMetadataProcessor>(Ref())
                 ->ProcessFromClientToExtProcServer(),
             MakeRefCounted<ClientMessageProcessor>(Ref(), request_attributes_)
                 ->ProcessFromClientToExtProcServer()),
      [self = Ref()](auto status) -> absl::Status {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProcCall " << self.get()
            << " ClientToServerCall finished with status: " << status;
        return status;
      });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::Run() {
  return Map(TryJoin<absl::StatusOr>(ClientToServerCall(),
                                     PullMessagesFromSideStream()),
             [self = Ref()](auto status) -> absl::Status {
               GRPC_TRACE_LOG(ext_proc_filter, INFO)
                   << "ExtProcCall " << self.get()
                   << " Run() finished with status: " << status.status();
               return status.status();
             });
}

//
// ExtProcFilter::ExtProcCall::ClientInitialMetadataProcessor
//

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ClientInitialMetadataProcessor::ProcessFromClientToExtProcServer() {
  return TrySeq(
      call_->handler_.PullClientInitialMetadata(),
      [self = Ref()](ClientMetadataHandle metadata) mutable
          -> absl::AnyInvocable<Poll<absl::Status>()> {
        if (!self->call_->config().processing_mode->send_request_headers) {
          return self->NonProcessingMode(std::move(metadata));
        } else if (self->call_->config().observability_mode) {
          return Seq(self->SendAndHandleClientInitialMetadata(metadata),
                     [self, metadata = std::move(metadata)](
                         absl::Status status) mutable
                         -> absl::AnyInvocable<Poll<absl::Status>()> {
                       if (!status.ok()) return Immediate(status);
                       return self->ObservabilityMode(std::move(metadata),
                                                      Timestamp::Now());
                     });
        } else {
          auto send_promise =
              self->SendAndHandleClientInitialMetadata(metadata);
          self->call_->client_initial_metadata_latch_.Set(std::move(metadata));
          return send_promise;
        }
      });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ClientInitialMetadataProcessor::ProcessFromExtProcServerToClient(
        absl::StatusOr<ExtProcResponse> response) {
  return Seq(call_->client_initial_metadata_latch_.Wait(),
             [self = Ref(), response = std::move(response)](
                 ClientMetadataHandle metadata) mutable
                 -> absl::AnyInvocable<Poll<absl::Status>()> {
               return self->NormalMode(std::move(metadata), Timestamp::Now(),
                                       std::move(response));
             });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ClientInitialMetadataProcessor::SendClientInitialMetadataRequest(
        const ClientMetadataHandle& metadata,
        absl::string_view default_authority) {
  return call_->SendMessageToSideStream(
      [call = call_, metadata = metadata.get(),
       default_authority = std::string(default_authority)]() mutable {
        std::optional<ExtProcProcessingMode> processing_mode;
        if (call->IsFirstMessageOnStream()) {
          processing_mode = call->config().processing_mode;
        }
        upb::Arena arena;
        auto* header_attributes = CreateExtProcAttributesProtoStruct(
            arena.ptr(), call->config().request_attributes, *metadata,
            default_authority);
        return CreateExtProcClientHeadersRequest(
            arena.ptr(), metadata, call->config().forwarding_allowed_headers,
            call->config().forwarding_disallowed_headers, header_attributes,
            call->config().observability_mode, processing_mode);
      });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ClientInitialMetadataProcessor::SendAndHandleClientInitialMetadata(
        const ClientMetadataHandle& metadata) {
  return Seq(SendClientInitialMetadataRequest(
                 metadata,
                 call_->ext_proc_filter_->default_authority_.as_string_view()),
             [call = call_](absl::Status status) mutable -> absl::Status {
               if (!status.ok() || call->IsStreamClosed()) {
                 return call->GetStreamClosedStatus(status);
               }
               return absl::OkStatus();
             });
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientInitialMetadataProcessor::StartChildCall(
    ClientMetadataHandle metadata, ::google_protobuf_Struct* attributes,
    Timestamp start_time) {
  if (start_time != Timestamp::InfPast()) {
    call_->ext_proc_filter_->RecordClientHeadersDuration(
        (Timestamp::Now() - start_time).seconds());
  }
  call_->child_call_started_ = true;
  call_->initiator_ = call_->ext_proc_filter_->MakeChildCall(
      std::move(metadata), call_->handler_.arena()->Ref());
  call_->handler_.AddChildCall(call_->initiator_);
  // Spawn background task to handle server-to-client path.
  call_->initiator_.SpawnInfallible(
      "server_to_client", [call = call_]() mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: server_to_client task started";
        return call->initiator_.CancelIfFails(call->ServerToClientCall());
      });
  return Immediate(absl::OkStatus());
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientInitialMetadataProcessor::NonProcessingMode(
    ClientMetadataHandle metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Client initial metadata received (non-processing):\n"
      << metadata->DebugString();
  const auto& processing_mode = *call_->config().processing_mode;
  ::google_protobuf_Struct* attributes = nullptr;
  if (processing_mode.send_request_body &&
      !call_->config().request_attributes.empty()) {
    auto* arena = call_->handler_.arena()->New<upb::Arena>();
    attributes = CreateExtProcAttributesProtoStruct(
        arena->ptr(), call_->config().request_attributes, *metadata,
        call_->ext_proc_filter_->default_authority_.as_string_view());
  }
  call_->request_attributes_ = attributes;
  return StartChildCall(std::move(metadata), attributes);
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientInitialMetadataProcessor::ObservabilityMode(
    ClientMetadataHandle metadata, Timestamp start_time) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Client initial metadata received (observability):\n"
      << metadata->DebugString();
  return StartChildCall(std::move(metadata),
                        /*attributes=*/nullptr, start_time);
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientInitialMetadataProcessor::NormalMode(
    ClientMetadataHandle metadata, Timestamp start_time,
    absl::StatusOr<ExtProcResponse> response) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: Client initial metadata received:\n"
      << metadata->DebugString();
  if (!response.ok()) {
    return Immediate(response.status());
  }
  if (auto status = ApplyHeaderMutations<ExtProcResponse::RequestHeaders>(
          *response, call_->config().mutation_rules, *metadata);
      !status.ok()) {
    return Immediate(status);
  }
  return StartChildCall(std::move(metadata),
                        /*attributes=*/nullptr, start_time);
}

//
// ExtProcFilter::ExtProcCall::ServerInitialMetadataProcessor
//

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ServerInitialMetadataProcessor::SendServerInitialMetadataRequest(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call,
        const ServerMetadataHandle& metadata, bool end_of_stream) {
  if (call->IsStreamClosed() || call->ext_proc_stream_half_closed_) {
    return Immediate(absl::OkStatus());
  }
  auto* metadata_ptr = metadata.get();
  return call->SendMessageToSideStream([call, metadata_ptr,
                                        end_of_stream]() mutable {
    std::optional<ExtProcProcessingMode> processing_mode;
    if (call->IsFirstMessageOnStream()) {
      processing_mode = call->config().processing_mode;
    }
    upb::Arena arena;
    return CreateExtProcServerHeadersRequest(
        arena.ptr(), metadata_ptr, call->config().forwarding_allowed_headers,
        call->config().forwarding_disallowed_headers,
        /*attributes=*/nullptr, call->config().observability_mode,
        processing_mode, end_of_stream);
  });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ServerInitialMetadataProcessor::ProcessFromServerToExtProcServer() {
  return Seq(
      call_->initiator_.PullServerInitialMetadata(),
      [self = Ref()](std::optional<ServerMetadataHandle> metadata) mutable
          -> absl::AnyInvocable<Poll<absl::Status>()> {
        if (!metadata.has_value()) {
          self->call_->is_trailers_only_ = true;
          return Immediate(absl::OkStatus());
        }
        if (!self->call_->config().processing_mode->send_response_headers ||
            self->call_->IsStreamClosed()) {
          return self->NonProcessingMode(std::move(*metadata));
        } else if (self->call_->config().observability_mode) {
          return Seq(SendServerInitialMetadataRequest(self->call_, *metadata),
                     [self, metadata = std::move(metadata)](
                         absl::Status status) mutable
                         -> absl::AnyInvocable<Poll<absl::Status>()> {
                       if (!status.ok()) return Immediate(status);
                       return self->ObservabilityMode(std::move(*metadata),
                                                      Timestamp::Now());
                     });
        } else if (self->call_->drain_requested_) {
          return self->DrainMode(std::move(*metadata));
        } else {
          auto send_promise =
              SendServerInitialMetadataRequest(self->call_, *metadata);
          self->call_->server_initial_metadata_latch_.Set(std::move(*metadata));
          return send_promise;
        }
      });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ServerInitialMetadataProcessor::ProcessFromExtProcServerToClient(
        absl::StatusOr<ExtProcResponse> response) {
  return Seq(call_->server_initial_metadata_latch_.Wait(),
             [self = Ref(), response = std::move(response)](
                 ServerMetadataHandle metadata) mutable
                 -> absl::AnyInvocable<Poll<absl::Status>()> {
               return self->NormalMode(std::move(metadata), Timestamp::Now(),
                                       std::move(response));
             });
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerInitialMetadataProcessor::NonProcessingMode(
    ServerMetadataHandle metadata) {
  if (call_->IsStreamFailureFatal()) {
    return Immediate(call_->GetStreamStatus());
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ServerInitialMetadataNonProcessingMode metadata: "
      << metadata->DebugString();
  call_->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
  return Immediate(absl::OkStatus());
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerInitialMetadataProcessor::ObservabilityMode(
    ServerMetadataHandle metadata, Timestamp start_time) {
  call_->ext_proc_filter_->RecordServerHeadersDuration(
      (Timestamp::Now() - start_time).seconds());
  call_->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
  return Immediate(absl::OkStatus());
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerInitialMetadataProcessor::NormalMode(
    ServerMetadataHandle metadata, Timestamp start_time,
    absl::StatusOr<ExtProcResponse> response) {
  if (!response.ok()) {
    return Immediate(response.status());
  }
  if (auto status = ApplyHeaderMutations<ExtProcResponse::ResponseHeaders>(
          *response, call_->config().mutation_rules, *metadata);
      !status.ok()) {
    return Immediate(status);
  }
  if (!call_->IsFailOpenAllowed() && call_->IsStreamClosed()) {
    return Immediate(call_->GetStreamStatus());
  }
  call_->ext_proc_filter_->RecordServerHeadersDuration(
      (Timestamp::Now() - start_time).seconds());
  call_->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
  return Immediate(absl::OkStatus());
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerInitialMetadataProcessor::DrainMode(
    ServerMetadataHandle metadata) {
  return Map(
      call_->WaitForStreamStatus(),
      [call = call_, metadata = std::move(metadata)](
          absl::Status status) mutable -> absl::Status {
        if (call->IsStreamFailureFatal()) {
          return status;
        }
        call->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
        return absl::OkStatus();
      });
}

//
// ExtProcFilter::ExtProcCall::ServerTrailingMetadataProcessor
//

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ServerTrailingMetadataProcessor::SendServerTrailingMetadataRequest(
        RefCountedPtr<ExtProcFilter::ExtProcCall> call,
        const ServerMetadataHandle& metadata) {
  if (call->IsStreamClosed() || call->ext_proc_stream_half_closed_) {
    return Immediate(absl::OkStatus());
  }
  auto* metadata_ptr = metadata.get();
  return Map(call->SendMessageToSideStream([call, metadata_ptr]() mutable {
    std::optional<ExtProcProcessingMode> processing_mode;
    if (call->IsFirstMessageOnStream()) {
      processing_mode = call->config().processing_mode;
    }
    upb::Arena arena;
    return CreateExtProcServerTrailersRequest(
        arena.ptr(), metadata_ptr, call->config().forwarding_allowed_headers,
        call->config().forwarding_disallowed_headers,
        /*attributes=*/nullptr, call->config().observability_mode,
        processing_mode);
  }),
             [call](absl::Status status) {
               if (status.ok()) {
                 call->server_trailers_sent_ = true;
               }
               return status;
             });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ServerTrailingMetadataProcessor::ProcessFromServerToExtProcServer() {
  if (call_->IsStreamClosed() || call_->ext_proc_stream_half_closed_) {
    absl::Status error = call_->GetStreamStatus();
    if (!error.ok() && !call_->IsFailOpenAllowed()) {
      return Immediate(error);
    }
  }
  if (call_->is_trailers_only_) {
    return TrailersOnly();
  }
  return NormalTrailers();
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ServerTrailingMetadataProcessor::ProcessFromExtProcServerToClient(
        absl::StatusOr<ExtProcResponse> response) {
  return Seq(
      call_->server_trailing_metadata_latch_.Wait(),
      [self = Ref(),
       response = std::move(response)](ServerMetadataHandle metadata) mutable
          -> absl::AnyInvocable<Poll<absl::Status>()> {
        if (metadata == nullptr) {
          return Immediate(absl::OkStatus());
        }
        if (self->call_->is_trailers_only_) {
          return self->TrailersOnlyNormalMode(
              std::move(metadata), Timestamp::Now(), std::move(response));
        }
        return self->NormalMode(std::move(metadata), Timestamp::Now(),
                                std::move(response));
      });
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerTrailingMetadataProcessor::TrailersOnly() {
  return Seq(
      call_->initiator_.PullServerTrailingMetadata(),
      [self = Ref()](ServerMetadataHandle metadata) mutable
          -> absl::AnyInvocable<Poll<absl::Status>()> {
        const bool send_headers =
            self->call_->config().processing_mode->send_response_headers &&
            !self->call_->IsStreamClosed() &&
            !self->call_->ext_proc_stream_half_closed_;
        if (!send_headers) {
          self->call_->server_trailing_metadata_latch_.Set(nullptr);
          return self->NonProcessingMode(std::move(metadata));
        } else if (self->call_->config().observability_mode) {
          Timestamp start_time = Timestamp::Now();
          return Seq(
              ServerInitialMetadataProcessor::SendServerInitialMetadataRequest(
                  self->call_, metadata,
                  /*end_of_stream=*/true),
              [self, metadata = std::move(metadata),
               start_time](absl::Status status) mutable
                  -> absl::AnyInvocable<Poll<absl::Status>()> {
                if (!status.ok() && self->call_->IsStreamFailureFatal()) {
                  return Immediate(status);
                }
                return self->TrailersOnlyObservabilityMode(std::move(metadata),
                                                           start_time);
              });
        } else if (self->call_->drain_requested_) {
          return self->DrainMode(std::move(metadata));
        } else {
          auto send_promise =
              ServerInitialMetadataProcessor::SendServerInitialMetadataRequest(
                  self->call_, metadata,
                  /*end_of_stream=*/true);
          self->call_->server_trailing_metadata_latch_.Set(std::move(metadata));
          return Seq(std::move(send_promise),
                     [self](absl::Status status) mutable -> absl::Status {
                       return status.ok() ? absl::OkStatus() : status;
                     });
        }
      });
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerTrailingMetadataProcessor::NormalTrailers() {
  return Seq(
      call_->initiator_.PullServerTrailingMetadata(),
      [self = Ref()](ServerMetadataHandle metadata) mutable
          -> absl::AnyInvocable<Poll<absl::Status>()> {
        if (!IsStatusOk(*metadata)) {
          self->call_->handler_.SpawnPushServerTrailingMetadata(
              std::move(metadata));
          return Immediate(absl::OkStatus());
        }
        const bool send_trailers =
            self->call_->config().processing_mode->send_response_trailers &&
            !self->call_->IsStreamClosed() &&
            !self->call_->ext_proc_stream_half_closed_;
        if (!send_trailers) {
          self->call_->server_trailing_metadata_latch_.Set(nullptr);
          return self->NonProcessingMode(std::move(metadata));
        } else if (self->call_->config().observability_mode) {
          Timestamp start_time = Timestamp::Now();
          return Seq(SendServerTrailingMetadataRequest(self->call_, metadata),
                     [self, metadata = std::move(metadata),
                      start_time](absl::Status status) mutable
                         -> absl::AnyInvocable<Poll<absl::Status>()> {
                       if (!status.ok()) return Immediate(status);
                       return self->ObservabilityMode(std::move(metadata),
                                                      start_time);
                     });
        } else if (self->call_->drain_requested_) {
          return self->DrainMode(std::move(metadata));
        } else {
          return self->NormalTrailersHelper(std::move(metadata));
        }
      });
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerTrailingMetadataProcessor::DrainMode(
    ServerMetadataHandle metadata) {
  return Seq(
      call_->WaitForStreamStatus(),
      [call = call_, metadata = std::move(metadata)](
          absl::Status status) mutable -> absl::Status {
        if (call->IsStreamFailureFatal()) {
          return status;
        }
        call->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
        return absl::OkStatus();
      });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ServerTrailingMetadataProcessor::NormalTrailersHelper(
        ServerMetadataHandle metadata) {
  auto send_promise = SendServerTrailingMetadataRequest(call_, metadata);
  call_->server_trailing_metadata_latch_.Set(std::move(metadata));
  return Seq(std::move(send_promise),
             [](absl::Status status) mutable -> absl::Status {
               return status.ok() ? absl::OkStatus() : status;
             });
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerTrailingMetadataProcessor::NonProcessingMode(
    ServerMetadataHandle metadata) {
  return [call = call_, metadata = std::move(metadata)]() mutable {
    if (call != nullptr && call->IsStreamClosed() &&
        call->IsStreamFailureFatal()) {
      return call->GetStreamStatus();
    }
    call->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
    return absl::OkStatus();
  };
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ServerTrailingMetadataProcessor::TrailersOnlyObservabilityMode(
        ServerMetadataHandle metadata, Timestamp start_time) {
  return [call = call_, metadata = std::move(metadata), start_time]() mutable {
    call->ext_proc_filter_->RecordServerHeadersDuration(
        (Timestamp::Now() - start_time).seconds());
    call->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
    return absl::OkStatus();
  };
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerTrailingMetadataProcessor::ObservabilityMode(
    ServerMetadataHandle metadata, Timestamp start_time) {
  return [call = call_, metadata = std::move(metadata), start_time]() mutable {
    call->ext_proc_filter_->RecordServerTrailersDuration(
        (Timestamp::Now() - start_time).seconds());
    call->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
    return absl::OkStatus();
  };
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ServerTrailingMetadataProcessor::TrailersOnlyNormalMode(
        ServerMetadataHandle metadata, Timestamp start_time,
        absl::StatusOr<ExtProcResponse> response) {
  const auto& config = call_->config();
  if (response.ok() &&
      std::holds_alternative<ExtProcResponse::ImmediateResponse>(
          response->response)) {
    const auto& immediate =
        std::get<ExtProcResponse::ImmediateResponse>(response->response);
    auto error_md = CancelledServerMetadataFromStatus(
        static_cast<grpc_status_code>(immediate.status), immediate.details);
    (void)ApplyHeaderMutations<ExtProcResponse::ImmediateResponse>(
        *response, config.mutation_rules, *error_md);
    call_->handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
    return Immediate(absl::OkStatus());
  }
  if (!response.ok()) {
    return Immediate(response.status());
  }
  if (auto status = ApplyHeaderMutations<ExtProcResponse::ResponseHeaders>(
          *response, config.mutation_rules, *metadata);
      !status.ok()) {
    return Immediate(status);
  }
  if (!call_->IsFailOpenAllowed() && call_->IsStreamClosed()) {
    return Immediate(call_->GetStreamStatus());
  }
  call_->ext_proc_filter_->RecordServerHeadersDuration(
      (Timestamp::Now() - start_time).seconds());
  call_->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
  return Immediate(absl::OkStatus());
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerTrailingMetadataProcessor::NormalMode(
    ServerMetadataHandle metadata, Timestamp start_time,
    absl::StatusOr<ExtProcResponse> response) {
  const auto& config = call_->config();
  if (response.ok() &&
      std::holds_alternative<ExtProcResponse::ImmediateResponse>(
          response->response)) {
    const auto& immediate =
        std::get<ExtProcResponse::ImmediateResponse>(response->response);
    auto error_md = CancelledServerMetadataFromStatus(
        static_cast<grpc_status_code>(immediate.status), immediate.details);
    (void)ApplyHeaderMutations<ExtProcResponse::ImmediateResponse>(
        *response, config.mutation_rules, *error_md);
    call_->handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
    return Immediate(absl::OkStatus());
  }
  if (!response.ok()) {
    return Immediate(response.status());
  }
  if (auto status = ApplyHeaderMutations<ExtProcResponse::ResponseTrailers>(
          *response, config.mutation_rules, *metadata);
      !status.ok()) {
    return Immediate(status);
  }
  if (!call_->IsFailOpenAllowed() && call_->IsStreamClosed()) {
    return Immediate(call_->GetStreamStatus());
  }
  call_->ext_proc_filter_->RecordServerTrailersDuration(
      (Timestamp::Now() - start_time).seconds());
  call_->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
  return Immediate(absl::OkStatus());
}

//
// ExtProcFilter::ExtProcCall::ServerMessageProcessor
//

absl::Status
ExtProcFilter::ExtProcCall::ServerMessageProcessor::PassThroughServerMessage(
    RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message) {
  if (call->IsStreamFailureFatal()) {
    return call->GetStreamStatus();
  }
  call->handler_.SpawnPushMessage(std::move(message));
  return absl::OkStatus();
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerMessageProcessor::ProcessServerMessage(
    RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message,
    bool observability_mode) {
  const bool send_message =
      !call->IsStreamClosed() && !call->ext_proc_stream_half_closed_;
  if (send_message) {
    return Seq(SendServerMessageRequest(call, message),
               [call, message = std::move(message), observability_mode](
                   absl::Status status) mutable -> absl::Status {
                 if (status.ok() && !call->IsStreamClosed()) {
                   if (observability_mode) {
                     call->handler_.SpawnPushMessage(std::move(message));
                   }
                   return absl::OkStatus();
                 }
                 return PassThroughServerMessage(call, std::move(message));
               });
  }
  return Immediate(PassThroughServerMessage(call, std::move(message)));
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerMessageProcessor::SendServerMessageRequest(
    RefCountedPtr<ExtProcFilter::ExtProcCall> call,
    const MessageHandle& message) {
  if (!call->config().observability_mode) {
    call->outstanding_s2c_messages_++;
  }
  std::string message_bytes;
  if (message != nullptr) {
    message_bytes = message->payload()->JoinIntoString();
  }
  return Seq(call->SendMessageToSideStream([ext_proc_call = call,
                                            message_bytes = std::move(
                                                message_bytes)]() mutable {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProc: ServerToClientMessages body message intercepted";
    std::optional<ExtProcProcessingMode> processing_mode;
    if (ext_proc_call->IsFirstMessageOnStream()) {
      processing_mode = ext_proc_call->config().processing_mode;
    }
    upb::Arena arena;
    return CreateExtProcServerBodyRequest(
        arena.ptr(), message_bytes, /*attributes=*/nullptr,
        ext_proc_call->config().observability_mode, processing_mode);
  }),
             [ext_proc_call = call](absl::Status status) {
               if (status.ok()) {
                 ext_proc_call->first_body_message_sent_ = true;
               }
               return status;
             });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ServerMessageProcessor::ProcessFromServerToExtProcServer() {
  if (call_->is_trailers_only_) {
    return Immediate(absl::OkStatus());
  }
  return Seq(ForEach(MessagesFrom(call_->initiator_),
                     [call = call_](MessageHandle message) mutable
                         -> absl::AnyInvocable<Poll<absl::Status>()> {
                       const bool send_body =
                           call->config().processing_mode->send_response_body &&
                           !call->IsStreamClosed();
                       if (!send_body) {
                         return NonProcessingMode(call, std::move(message));
                       } else if (call->config().observability_mode) {
                         return ObservabilityMode(call, std::move(message));
                       } else {
                         return NormalMode(call, std::move(message));
                       }
                     }),
             [call = call_]() {
               call->s2c_writes_done_ = true;
               return absl::OkStatus();
             });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ServerMessageProcessor::ProcessFromExtProcServerToClient(
        absl::StatusOr<ExtProcResponse> response) {
  if (call_->is_trailers_only_) {
    return Immediate(absl::OkStatus());
  }
  if (!response.ok()) {
    return Immediate(response.status());
  }
  const auto& response_body =
      std::get<ExtProcResponse::ResponseBody>((*response).response);
  auto slice = Slice::FromCopiedString(response_body.mutation.body);
  auto new_msg = call_->handler_.arena()->MakePooled<Message>(
      SliceBuffer(std::move(slice)), /*flags=*/0);
  call_->handler_.SpawnPushMessage(std::move(new_msg));
  return Immediate(absl::OkStatus());
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerMessageProcessor::NonProcessingMode(
    RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message) {
  call->handler_.SpawnPushMessage(std::move(message));
  return Immediate(absl::OkStatus());
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerMessageProcessor::ObservabilityMode(
    RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message) {
  return ProcessServerMessage(call, std::move(message),
                              /*observability_mode=*/true);
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ServerMessageProcessor::NormalMode(
    RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message) {
  if (call->drain_requested_) {
    return Seq(call->WaitForStreamStatus(),
               [call, message = std::move(message)](
                   absl::Status status) mutable -> absl::Status {
                 if (call->IsStreamFailureFatal()) {
                   return status;
                 }
                 call->handler_.SpawnPushMessage(std::move(message));
                 return absl::OkStatus();
               });
  }
  return ProcessServerMessage(call, std::move(message),
                              /*observability_mode=*/false);
}

//
// ExtProcFilter::ExtProcCall::ClientMessageProcessor
//

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientMessageProcessor::SendClientMessageRequest(
    RefCountedPtr<ExtProcFilter::ExtProcCall> call,
    const MessageHandle& message, bool end_of_stream,
    bool end_of_stream_without_message, ::google_protobuf_Struct* attributes) {
  std::string message_bytes;
  if (message != nullptr) {
    message_bytes = message->payload()->JoinIntoString();
  }
  if (!call->config().observability_mode) {
    call->outstanding_c2s_messages_++;
  }
  if (end_of_stream_without_message) {
    call->half_close_initiated_ = true;
  }
  return Map(
      call->SendMessageToSideStream(
          [ext_proc_call = call, message_bytes = std::move(message_bytes),
           end_of_stream, end_of_stream_without_message, attributes]() mutable {
            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                << "ExtProc: ClientToServerMessages body message intercepted "
                   "(observability mode)";
            std::optional<ExtProcProcessingMode> processing_mode;
            if (ext_proc_call->IsFirstMessageOnStream()) {
              processing_mode = ext_proc_call->config().processing_mode;
            }
            upb::Arena arena;
            if (attributes == nullptr) {
              attributes = ext_proc_call->request_attributes_;
            }
            return CreateExtProcClientBodyRequest(
                arena.ptr(), message_bytes, attributes,
                ext_proc_call->config().observability_mode, processing_mode,
                end_of_stream, end_of_stream_without_message);
          }),
      [ext_proc_call = call](absl::Status status) {
        if (status.ok()) {
          ext_proc_call->first_body_message_sent_ = true;
        }
        return status;
      });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ClientMessageProcessor::ProcessFromClientToExtProcServer() {
  const bool send_request_body =
      call_->config().processing_mode->send_request_body &&
      !call_->IsStreamClosed();
  if (!send_request_body) {
    return NonProcessingMode();
  }
  return TrySeq(ForEach(MessagesFrom(call_->handler_),
                        [call = call_, attributes = attributes_](
                            MessageHandle message) mutable
                            -> absl::AnyInvocable<Poll<absl::Status>()> {
                          if (call->config().observability_mode) {
                            return ObservabilityMode(call, attributes,
                                                     std::move(message));
                          } else {
                            return NormalModeSendOnly(call, attributes,
                                                      std::move(message));
                          }
                        }),
                [call = call_, attributes = attributes_]() mutable
                    -> absl::AnyInvocable<Poll<absl::Status>()> {
                  return SendClientHalfClose(
                      call, attributes,
                      /*observability_mode=*/call->config().observability_mode);
                });
}

absl::AnyInvocable<Poll<absl::Status>()> ExtProcFilter::ExtProcCall::
    ClientMessageProcessor::ProcessFromExtProcServerToClient(
        absl::StatusOr<ExtProcResponse> result) {
  const bool send_request_body =
      call_->config().processing_mode->send_request_body &&
      !call_->IsStreamClosed();
  if (!send_request_body || call_->config().observability_mode) {
    return Immediate(absl::OkStatus());
  }
  if (!result.ok()) {
    absl::Status closed_status = call_->GetStreamClosedStatus(result.status());
    return Immediate(closed_status);
  }
  if (const auto* request_body =
          std::get_if<ExtProcResponse::RequestBody>(&result->response)) {
    if (!request_body->mutation.end_of_stream_without_message) {
      auto slice = Slice::FromCopiedString(request_body->mutation.body);
      auto new_msg = call_->initiator_.arena()->MakePooled<Message>(
          SliceBuffer(std::move(slice)), /*flags=*/0);
      call_->initiator_.SpawnPushMessage(std::move(new_msg));
    }
    if (request_body->mutation.end_of_stream ||
        request_body->mutation.end_of_stream_without_message) {
      Timestamp start_time = Timestamp::Now();
      if (call_->c2s_writes_done_ || !call_->IsStreamClosed()) {
        call_->ext_proc_filter_->RecordClientHalfCloseDuration(
            (Timestamp::Now() - start_time).seconds());
        call_->initiator_.SpawnFinishSends();
      }
    }
  }
  return Immediate(absl::OkStatus());
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientMessageProcessor::NonProcessingMode() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProc: ClientToServerMessagesNonProcessingMode started";
  return Seq(
      ForEach(MessagesFrom(call_->handler_),
              [call = call_](MessageHandle message) mutable -> absl::Status {
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << "ExtProc: ClientToServerMessagesNonProcessingMode got "
                       "message";
                if (call->ext_proc_set_eos_) {
                  return absl::InternalError(
                      "Client sends closed by external processor");
                }
                call->initiator_.SpawnPushMessage(std::move(message));
                return absl::OkStatus();
              }),
      [call = call_]() mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ClientToServerMessagesNonProcessingMode finished "
               "sends";
        call->initiator_.SpawnFinishSends();
        return absl::OkStatus();
      });
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientMessageProcessor::ProcessClientMessage(
    RefCountedPtr<ExtProcFilter::ExtProcCall> call, MessageHandle message,
    ::google_protobuf_Struct* attributes, bool observability_mode) {
  // TODO(rishesh): removed thi check once PH2 work is done
  if (call->ext_proc_set_eos_) {
    return Immediate(
        absl::InternalError("Client sends closed by external processor"));
  }
  if (!call->IsStreamClosed() && !call->ext_proc_stream_half_closed_) {
    return Seq(SendClientMessageRequest(call, message,
                                        /*end_of_stream=*/false,
                                        /*end_of_stream_without_message=*/false,
                                        attributes),
               [call, message = std::move(message), observability_mode](
                   absl::Status status) mutable -> absl::Status {
                 if (!status.ok() || call->IsStreamClosed()) {
                   absl::Status closed_status =
                       call->GetStreamClosedStatus(status);
                   if (!closed_status.ok()) {
                     return closed_status;
                   }
                   call->initiator_.SpawnPushMessage(std::move(message));
                 } else if (observability_mode) {
                   call->initiator_.SpawnPushMessage(std::move(message));
                 }
                 return absl::OkStatus();
               });
  }
  absl::Status closed_status = call->GetStreamClosedStatus(absl::OkStatus());
  if (!closed_status.ok()) {
    return Immediate(closed_status);
  }
  call->initiator_.SpawnPushMessage(std::move(message));
  return Immediate(absl::OkStatus());
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientMessageProcessor::SendClientHalfClose(
    RefCountedPtr<ExtProcFilter::ExtProcCall> call,
    ::google_protobuf_Struct* attributes, bool observability_mode) {
  Timestamp start_time = Timestamp::Now();
  call->c2s_writes_done_ = true;
  if (call->ext_proc_set_eos_) {
    return Immediate(absl::OkStatus());
  }
  if (!observability_mode && call->drain_requested_) {
    call->ext_proc_filter_->RecordClientHalfCloseDuration(
        (Timestamp::Now() - start_time).seconds());
    call->initiator_.SpawnFinishSends();
    call->c2s_writes_done_ = true;
    return Immediate(absl::OkStatus());
  }
  if (!call->IsStreamClosed() && !call->ext_proc_stream_half_closed_) {
    MessageHandle null_msg = nullptr;
    return Seq(
        SendClientMessageRequest(call, null_msg,
                                 /*end_of_stream=*/false,
                                 /*end_of_stream_without_message=*/true,
                                 attributes),
        [call, start_time,
         observability_mode](absl::Status status) mutable -> absl::Status {
          if (!status.ok() || call->IsStreamClosed()) {
            return status;
          }
          call->ext_proc_filter_->RecordClientHalfCloseDuration(
              (Timestamp::Now() - start_time).seconds());
          if (!status.ok() || call->IsStreamClosed() || observability_mode) {
            call->initiator_.SpawnFinishSends();
          }
          return absl::OkStatus();
        });
  }
  absl::Status closed_status = call->GetStreamClosedStatus(absl::OkStatus());
  if (!closed_status.ok()) {
    return Immediate(closed_status);
  }
  call->ext_proc_filter_->RecordClientHalfCloseDuration(
      (Timestamp::Now() - start_time).seconds());
  call->initiator_.SpawnFinishSends();
  call->c2s_writes_done_ = true;
  return Immediate(absl::OkStatus());
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientMessageProcessor::ObservabilityMode(
    RefCountedPtr<ExtProcFilter::ExtProcCall> call,
    ::google_protobuf_Struct* attributes, MessageHandle message) {
  return ProcessClientMessage(call, std::move(message), attributes,
                              /*observability_mode=*/true);
}

absl::AnyInvocable<Poll<absl::Status>()>
ExtProcFilter::ExtProcCall::ClientMessageProcessor::NormalModeSendOnly(
    RefCountedPtr<ExtProcFilter::ExtProcCall> call,
    ::google_protobuf_Struct* attributes, MessageHandle message) {
  if (call->drain_requested_) {
    return Seq(call->WaitForStreamStatus(),
               [call, message = std::move(message)](
                   absl::Status /*status*/) mutable -> absl::Status {
                 if (call->IsStreamFailureFatal()) {
                   return call->GetStreamStatus();
                 }
                 if (message != nullptr) {
                   call->initiator_.SpawnPushMessage(std::move(message));
                 }
                 return absl::OkStatus();
               });
  }
  return ProcessClientMessage(call, std::move(message), attributes,
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
      stats_plugin_group_(
          args.GetObjectRef<GlobalStatsPluginRegistry::StatsPluginGroup>()) {}

ExtProcFilter::~ExtProcFilter() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcFilter " << this << " destroyed";
}

bool ExtProcFilter::StartTransportOp(grpc_transport_op* op) {
  if (!op->disconnect_with_error.ok()) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcFilter " << this
        << " StartTransportOp disconnect_with_error: "
        << op->disconnect_with_error;
    event_engine_.reset();
    config_.reset();
  }
  return false;
}

void ExtProcFilter::RecordClientHeadersDuration(double duration_seconds) const {
  if (stats_plugin_group_ != nullptr) {
    stats_plugin_group_->RecordHistogram(
        kMetricClientExtProcClientHeadersDuration, duration_seconds, {target_},
        {});
  }
}

void ExtProcFilter::RecordClientHalfCloseDuration(
    double duration_seconds) const {
  if (stats_plugin_group_ != nullptr) {
    stats_plugin_group_->RecordHistogram(
        kMetricClientExtProcClientHalfCloseDuration, duration_seconds,
        {target_}, {});
  }
}

void ExtProcFilter::RecordServerHeadersDuration(double duration_seconds) const {
  if (stats_plugin_group_ != nullptr) {
    stats_plugin_group_->RecordHistogram(
        kMetricClientExtProcServerHeadersDuration, duration_seconds, {target_},
        {});
  }
}

void ExtProcFilter::RecordServerTrailersDuration(
    double duration_seconds) const {
  if (stats_plugin_group_ != nullptr) {
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
        return ArenaPromise<absl::Status>(ext_proc_call->Run());
      });
}

}  // namespace grpc_core
