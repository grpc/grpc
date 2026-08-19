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
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/impl/channel_arg_names.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/call/security_context.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/filter/ext_proc/ext_proc_messages.h"
#include "src/core/handshaker/endpoint_info/endpoint_info_handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/debug/trace_impl.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/inter_activity_pipe.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/promise/wait_set.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/host_port.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/time.h"
#include "src/core/util/uri.h"
#include "src/core/xds/grpc/streaming_call_promise_wrapper.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

//
// ExtProcFilter::ClientTelemetryDomain
//

ExtProcFilter::ClientTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::ClientTelemetryDomain::kClientHeadersDuration =
        ExtProcFilter::ClientTelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.client_ext_proc.client_headers_duration",
            "Time between when the ext_proc filter sees the client's headers "
            "and when it allows those headers to continue on to the next "
            "filter.",
            "s", 60, 20);

ExtProcFilter::ClientTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::ClientTelemetryDomain::kClientHalfCloseDuration =
        ExtProcFilter::ClientTelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.client_ext_proc.client_half_close_duration",
            "Time between when the ext_proc filter sees the client's "
            "half-close and when it allows that half-close to continue on to "
            "the next filter.",
            "s", 60, 20);

ExtProcFilter::ClientTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::ClientTelemetryDomain::kServerHeadersDuration =
        ExtProcFilter::ClientTelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.client_ext_proc.server_headers_duration",
            "Time between when the ext_proc filter sees the server's headers "
            "and when it allows those headers to continue on to the next "
            "filter.",
            "s", 60, 20);

ExtProcFilter::ClientTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::ClientTelemetryDomain::kServerTrailersDuration =
        ExtProcFilter::ClientTelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.client_ext_proc.server_trailers_duration",
            "Time between when the ext_proc filter sees the server's "
            "trailers and when it allows those trailers to continue on to "
            "the next filter.",
            "s", 60, 20);

//
// ExtProcFilter::ServerTelemetryDomain
//

ExtProcFilter::ServerTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::ServerTelemetryDomain::kClientHeadersDuration =
        ExtProcFilter::ServerTelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.server_ext_proc.client_headers_duration",
            "Time between when the ext_proc filter sees the client's headers "
            "and when it allows those headers to continue on to the next "
            "filter.",
            "s", 60, 20);

ExtProcFilter::ServerTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::ServerTelemetryDomain::kClientHalfCloseDuration =
        ExtProcFilter::ServerTelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.server_ext_proc.client_half_close_duration",
            "Time between when the ext_proc filter sees the client's "
            "half-close and when it allows that half-close to continue on to "
            "the next filter.",
            "s", 60, 20);

ExtProcFilter::ServerTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::ServerTelemetryDomain::kServerHeadersDuration =
        ExtProcFilter::ServerTelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.server_ext_proc.server_headers_duration",
            "Time between when the ext_proc filter sees the server's headers "
            "and when it allows those headers to continue on to the next "
            "filter.",
            "s", 60, 20);

ExtProcFilter::ServerTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::ServerTelemetryDomain::kServerTrailersDuration =
        ExtProcFilter::ServerTelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.server_ext_proc.server_trailers_duration",
            "Time between when the ext_proc filter sees the server's "
            "trailers and when it allows those trailers to continue on to "
            "the next filter.",
            "s", 60, 20);

namespace {

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

// High-Level Architecture of Concurrent Pipeline Loops across Activities:
//
//  [ Activity 1: handler_ ]
//  LOOP 1: Read-From-Client Pipeline Loop [HandleReadFromClientLoop()]
//  +-----------------------------------------------------------------------+
//  | TrySeq(                                                               |
//  |     handler_.PullClientInitialMetadata()                              |
//  |       -> HandleInitialMetadataFromClient(),                           |
//  |     ForEach(MessagesFrom(handler_))                                   |
//  |       -> HandleMessageFromClient(),                                   |
//  |     HandleHalfCloseFromClient())                                      |
//  +-----------------------------------------------------------------------+
//                                     ||
//                     Joined via TryJoin() in Run()
//                                     ||
//  LOOP 2: Read-From-Server Pipeline Loop [HandleReadFromServerLoop()]
//  +-----------------------------------------------------------------------+
//  | Race(                                                                 |
//  |     // Branch 1: Trailing metadata (early arrival or after messages)  |
//  |     Seq(                                                              |
//  |         server_trailing_metadata_latch_.Wait(),                       |
//  |         If(is_early,                                                  |
//  |            HandleTrailingMetadataFromServer(),                        |
//  |            Seq(WaitFor(kMessagesComplete),                            |
//  |                HandleTrailingMetadataFromServer()))),                 |
//  |     // Branch 2: Initial metadata and message pipeline                |
//  |     TrySeq(                                                           |
//  |         server_initial_metadata_latch_.Wait()                         |
//  |           -> HandleInitialMetadataFromServer(),                       |
//  |         ForEach(server_to_client_messages_.receiver)                  |
//  |           -> HandleMessageFromServer(),                               |
//  |         Signal(kMessagesComplete)))                                   |
//  +-----------------------------------------------------------------------+
//                                     ||
//                     Joined via TryJoin() in Run()
//                                     ||
//  LOOP 3: Side-Stream Pull Pipeline Loop [HandleReadFromSideStreamLoop()]
//  +-----------------------------------------------------------------------+
//  | Seq(                                                                  |
//  |     Loop: streaming_call_->PullMessage()                              |
//  |       -> ProcessSideStreamResponse(),                                 |
//  |     streaming_call_->PullServerTrailingMetadata(),                    |
//  |     HandleSideStreamStatus(status))                                   |
//  +-----------------------------------------------------------------------+
//
//  [ Activity 2: initiator_ ] (Spawned on child call startup in
//  StartChildCall())
//  LOOP 4: Downstream Server Event Forwarding Loop
//  [SpawnReadFromServerLoop()]
//  +-----------------------------------------------------------------------+
//  | Race(                                                                 |
//  |     // Branch 1: Trailing metadata / early cancellation               |
//  |     Seq(                                                              |
//  |         initiator_.PullServerTrailingMetadata(),                      |
//  |         server_to_client_messages_.sender.MarkClosed(),               |
//  |         server_trailing_metadata_latch_.Set()),                       |
//  |     // Branch 2: Initial metadata and streaming messages              |
//  |     TrySeq(                                                           |
//  |         initiator_.PullServerInitialMetadata()                        |
//  |           -> server_initial_metadata_latch_.Set(),                    |
//  |         ForEach(MessagesFrom(initiator_))                             |
//  |           -> server_to_client_messages_.sender.Push()))               |
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
  auto Run();

 private:
  // Tracks the state of outgoing message sends on the ext_proc side-stream.
  enum class SideStreamSendState {
    // Initial state: no message send is currently in flight.
    kIdle,
    // A message send operation has been claimed and is currently in flight on
    // the underlying transport wrapper.
    kSendInFlight,
    // The stream has been closed or terminated due to an error, preventing
    // subsequent sends.
    kSendFailed,
  };

  enum class SideStreamRequestEventState {
    // Initial state. Allowed side-stream request events:
    // - Request headers: transitions to kHeadersReceived.
    kInit,
    // Headers received. Allowed side-stream request events:
    // - Request body: if EOS, transitions to kHalfCloseReceived; otherwise,
    //   stays in this state.
    kHeadersReceived,
    // Received half-close. Allowed side-stream request events: none.
    kHalfCloseReceived,
  };

  enum class SideStreamResponseEventState {
    // Initial state. Allowed side-stream response events:
    // - Response headers: transitions to kHeadersReceived.
    kInit,
    // Headers received. Allowed side-stream response events:
    // - Response body: stays in this state.
    // - Response trailers: transitions to kTrailersReceived.
    kHeadersReceived,
    // Received trailers. Allowed side-stream response events: none.
    kTrailersReceived,
  };

  enum class ServerReadEventState {
    // Initial state: awaiting server initial metadata.
    kInit,
    // Initial metadata has been received from the downstream server.
    kInitialMetadataReceived,
    // All messages from the downstream server have been received and processed.
    kMessagesComplete,
  };

  // Handle the read-from-client loop on handler_.
  // Called when the ExtProcCall is created.
  //
  // Handles client initial metadata, client messages, and half-close.
  // Sends each event to the ext_proc side-stream and/or to the server,
  // based on the configuration.
  auto HandleReadFromClientLoop();

  // Handle the read-from-server loop on handler_.
  // Called when the ExtProcCall is created.
  //
  // Handles server initial metadata, server messages, and server
  // trailing metadata received from the initiator_ activity.
  // Sends each event to the ext_proc side-stream and/or to the client,
  // based on the configuration.
  auto HandleReadFromServerLoop();

  // Handle the read-from-ext_proc-side-stream loop on handler_.
  // Called when the ExtProcCall is created.
  //
  // Handles all events on the call, forwarding data to either handler_
  // (client) or initiator_ (server).
  auto HandleReadFromSideStreamLoop();

  // Spawns the read-from-server loop on initiator_.
  // Called from StartChildCall().
  //
  // Pulls server initial metadata, messages, and trailing metadata from
  // downstream and immediately forwards them across inter-activity mechanisms
  // to the handler_ activity.
  void SpawnReadFromServerLoop();

  // Read-from-client event handlers
  auto HandleInitialMetadataFromClient(ClientMetadataHandle metadata);
  auto HandleMessageFromClient(MessageHandle message);
  auto HandleHalfCloseFromClient();

  // Read-from-server event handlers
  auto HandleInitialMetadataFromServer(
      std::optional<ServerMetadataHandle> metadata);
  auto HandleMessageFromServer(MessageHandle message);
  auto HandleTrailingMetadataFromServer(ServerMetadataHandle metadata);

  // Read-from-sidestream event handlers
  StatusFlag HandleClientInitialMetadataFromSidestream(
      const ExtProcResponse::RequestHeaders& response);
  StatusFlag HandleClientMessageFromSidestream(
      const ExtProcResponse::RequestBody& response);
  StatusFlag HandleServerInitialMetadataFromSidestream(
      const ExtProcResponse::ResponseHeaders& response);
  StatusFlag HandleServerMessageFromSidestream(
      const ExtProcResponse::ResponseBody& response);
  StatusFlag HandleServerTrailingMetadataFromSidestream(
      const ExtProcResponse::ResponseTrailers& response);
  StatusFlag HandleImmediateResponseFromSidestream(
      const ExtProcResponse::ImmediateResponse& response);

  // Initializes and starts the child call to the backend server, and spawns
  // the background task for the server-to-client response path.
  void StartChildCall(ClientMetadataHandle metadata);

  // Sends a message to the external processor side-stream.
  // Coordinates client-side and server-side message sources within the handler_
  // activity so that only one send is in-flight on streaming_call_ at a time,
  // using a single Waker without any queue or vector allocations.
  auto SendMessageToSideStream(std::string payload);

  // Parses and processes an incoming response message payload from the
  // side-stream.
  auto ProcessSideStreamResponse(absl::string_view payload);

  // Handles transport status updates/closure on the ext_proc side-stream.
  void HandleSideStreamStatus(absl::Status status);

  const Config& config() const { return *ext_proc_filter_->config_; }

  const ProcessingMode& processing_mode() const {
    return *config().processing_mode;
  }

  // Returns true if this is the first message being sent on the side-stream,
  // resetting the internal flag. Used to attach ProcessingMode on the first
  // request.
  bool IsFirstMessageOnSideStream() {
    return std::exchange(is_first_message_on_side_stream_, false);
  }

  bool IsFailOpenAllowed() const {
    const bool allow = config().failure_mode_allow.value_or(false);
    if (config().observability_mode) return allow;
    return allow && !first_body_message_sent_;
  }

  // Returns true if the external processor side-stream has terminated (cleanly
  // or with error).
  bool IsSideStreamClosed() const { return side_stream_closed_latch_.is_set(); }

  // Returns true if the side-stream closed with an error and fail-open mode is
  // not permitted for this call (meaning the side-stream failure must fail the
  // data plane RPC).
  bool IsSideStreamFailureFatal() const {
    if (IsFailOpenAllowed()) return false;
    return side_stream_status_.has_value() && !side_stream_status_->ok();
  }

  // Evaluates a side-stream operation's status. If the operation failed and
  // the side-stream failure is fatal (fail-open is disabled), returns
  // Failure{}; otherwise returns Success{}.
  StatusFlag EvaluateSideStreamStatus(StatusFlag status = Failure{}) const {
    if (!status.ok() && IsSideStreamFailureFatal()) {
      return Failure{};
    }
    return Success{};
  }

  // Evaluates the final status of the side-stream to return for the filter.
  // Respects IsFailOpenAllowed() by returning OkStatus() when fail-open is
  // permitted even if the side-stream failed.
  absl::Status GetSideStreamClosedStatus(
      absl::Status default_error = absl::CancelledError("Side-stream closed")) {
    if (side_stream_status_.has_value()) {
      if (side_stream_status_->ok() || IsFailOpenAllowed()) {
        return absl::OkStatus();
      }
      return *side_stream_status_;
    }
    if (IsFailOpenAllowed()) {
      return absl::OkStatus();
    }
    return default_error;
  }

  // In drain mode or error handling, returns a promise that resolves once the
  // out-of-band side-stream has terminated, yielding its effective status.
  auto WaitForSideStreamClosed() {
    return Seq(side_stream_closed_latch_.Wait(),
               [self = WeakRef()](Empty) -> StatusFlag {
                 return self->EvaluateSideStreamStatus();
               });
  }

  // Fails the intercepted data plane RPC with the given error status:
  // 1. Pushes error trailing metadata downstream to the client.
  // 2. Cancels any active upstream child call.
  // 3. Records the error status on the side-stream and marks it closed.
  void CancelCallWithError(absl::Status status) {
    if (!IsSideStreamClosed()) {
      if (!status.ok()) {
        auto error_md = CancelledServerMetadataFromStatus(status);
        handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
        if (initiator_.is_set()) {
          initiator_.SpawnCancel();
        }
      }
      side_stream_status_ = status;
      side_stream_closed_latch_.Set();
      ext_proc_send_state_ = SideStreamSendState::kSendFailed;
      ext_proc_send_waiters_.TakeWakeupSet().Wakeup();
    }
  }

  // Idempotently closes the out-of-band side-stream to the external processor.
  // Wakes any pending side-stream senders and resets the side-stream call
  // object.
  void CloseSideStream() {
    if (!IsSideStreamClosed()) {
      side_stream_status_ = absl::OkStatus();
      side_stream_closed_latch_.Set();
      auto streaming_call = std::move(streaming_call_);
      ext_proc_send_state_ = SideStreamSendState::kSendFailed;
      ext_proc_send_waiters_.TakeWakeupSet().Wakeup();
      streaming_call.reset();
    }
  }

  // Extracts connection attributes (such as source address/port and TLS
  // security properties) for server-side CEL attributes in A103.
  std::optional<ExtProcConnectionAttributes> GetConnectionAttributes() const {
    if (!ext_proc_filter_->is_server()) return std::nullopt;
    ExtProcConnectionAttributes attributes;
    attributes.source_address = std::string(ext_proc_filter_->source_address());
    attributes.source_port = ext_proc_filter_->source_port();
    auto* sec_ctx = MaybeGetContext<grpc_server_security_context>();
    if (sec_ctx != nullptr && sec_ctx->auth_context != nullptr) {
      auto get_auth_prop = [&](const char* prop_name) -> std::string {
        grpc_auth_property_iterator it =
            grpc_auth_context_find_properties_by_name(
                sec_ctx->auth_context.get(), prop_name);
        const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
        if (prop != nullptr) {
          return std::string(prop->value, prop->value_length);
        }
        return "";
      };
      attributes.requested_server_name =
          get_auth_prop(GRPC_SSL_SERVER_NAME_PROPERTY_NAME);
      attributes.tls_version =
          get_auth_prop(GRPC_SSL_TLS_VERSION_PROPERTY_NAME);
      attributes.sha256_peer_certificate_digest =
          get_auth_prop(GRPC_SSL_PEER_SHA256_PROPERTY_NAME);
    }
    return attributes;
  }

  void Orphaned() override { CloseSideStream(); }

  std::string DebugTag() const;

  // Track event states for request and response side-stream messages.
  // Synchronized by the handler_ activity.
  SideStreamRequestEventState request_event_state_ =
      SideStreamRequestEventState::kInit;
  SideStreamResponseEventState response_event_state_ =
      SideStreamResponseEventState::kInit;

  // Metadata stored during request/response processing.
  // Synchronized by the handler_ activity.
  ClientMetadataHandle client_initial_metadata_;
  ServerMetadataHandle server_initial_metadata_;
  ServerMetadataHandle server_trailing_metadata_;

  // Inter-activity communication mechanisms between initiator_ and handler_.
  InterActivityLatch<std::optional<ServerMetadataHandle>>
      server_initial_metadata_latch_;
  InterActivityPipe<MessageHandle, 1> server_to_client_messages_;
  InterActivityLatch<ServerMetadataHandle> server_trailing_metadata_latch_;

  // Timestamps recorded when events arrive from the data plane, used to
  // measure delay introduced by the external processor in normal mode.
  // Synchronized by the handler_ activity.
  Timestamp client_initial_metadata_start_time_ = Timestamp::InfPast();
  Timestamp client_half_close_start_time_ = Timestamp::InfPast();
  Timestamp server_initial_metadata_start_time_ = Timestamp::InfPast();
  Timestamp server_trailing_metadata_start_time_ = Timestamp::InfPast();

  // Temporary UPB arena holding request attributes until the first client body
  // request is sent to the sidestream. Synchronized by the handler_ activity.
  upb::Arena request_attributes_arena_;
  // Request attributes generated during request header processing to be
  // attached to subsequent request body processing requests. Synchronized by
  // the handler_ activity.
  ::google_protobuf_Struct* request_attributes_ = nullptr;
  // Indicates whether a stream drain operation has been requested by the
  // filter. Synchronized by the handler_ activity.
  bool drain_requested_ = false;
  // True if no messages have been sent on the external processor side-stream
  // yet. Used to include overall processing_mode in the initial stream header
  // request. Synchronized by the handler_ activity.
  bool is_first_message_on_side_stream_ = true;
  // Tracks whether the first body message has been sent on the side-stream,
  // used for fail-open determination. Synchronized by the handler_ activity.
  bool first_body_message_sent_ = false;
  // TODO(rishesh): Need to remove this once PH2 work is done.
  // Number of messages sent to ext_proc that are awaiting response processing
  // in S2C and C2S directions respectively. Synchronized by the handler_
  // activity.
  size_t outstanding_s2c_messages_ = 0;
  size_t outstanding_c2s_messages_ = 0;
  // Data plane stream state flags tracking directional write completion,
  // half-close, and trailers-only RPC mode. Synchronized by the handler_
  // activity.
  bool c2s_writes_done_ = false;
  bool is_trailers_only_ = false;
  Waker server_trailing_metadata_waker_;
  // Indicates server trailing metadata was dispatched to side-stream.
  // Synchronized by the handler_ activity.
  bool server_trailers_sent_to_side_stream_ = false;
  // Set by external processor server when it requests end of client sends
  // (EOS). Synchronized by the handler_ activity.
  bool ext_proc_closed_client_sends_ = false;
  // Tracks terminal status of the external processor side-stream.
  // Synchronized by the handler_ activity.
  std::optional<absl::Status> side_stream_status_;
  // Latch signaled when the side-stream is closed or drained.
  Latch<void> side_stream_closed_latch_;

  // Send state and waiters for coordinating message sends on the side-stream
  // within the handler_ activity.
  SideStreamSendState ext_proc_send_state_ = SideStreamSendState::kIdle;
  // Tracks the event state of data plane reads from the downstream server on
  // the handler_ activity. Used to coordinate trailing metadata handling with
  // in-flight message processing.
  ServerReadEventState server_read_event_state_ = ServerReadEventState::kInit;
  WaitSet ext_proc_send_waiters_;

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

std::string ExtProcFilter::ExtProcCall::DebugTag() const {
  std::string tag;
  StrAppend(tag, "[");
  StrAppend(tag, Activity::current() != nullptr
                     ? Activity::current()->DebugTag()
                     : "<unknown>");
  StrAppend(tag, " ext_proc_filter=0x");
  StrAppend(tag, absl::StrCat(absl::Hex(
                     reinterpret_cast<uintptr_t>(ext_proc_filter_.get()))));
  StrAppend(tag, " ext_proc_call=0x");
  StrAppend(tag, absl::StrCat(absl::Hex(reinterpret_cast<uintptr_t>(this))));
  StrAppend(tag, "] ");
  return tag;
}

ExtProcFilter::ExtProcCall::~ExtProcCall() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "ExtProcCall destroyed";
  if (config().deferred_close_timeout != Duration::Zero() &&
      config().observability_mode) {
    ext_proc_filter_->event_engine_->RunAfter(
        config().deferred_close_timeout,
        [call = std::move(streaming_call_)]() mutable {
          // An ExecCtx is required on EventEngine threads when destroying the
          // underlying streaming call, as its cancellation schedules completion
          // closures.
          ExecCtx exec_ctx;
          call.reset();
        });
  } else {
    streaming_call_.reset();
  }
}

// This function role is to:
// - send the message to the ext proc server and wait for the send to get
// complete and then propagate the status
// - if a message is already in progress then wait for the in flight message to
// get complete and then send the previous one if the stream is not closed
// - Handle the failure mode allow
auto ExtProcFilter::ExtProcCall::SendMessageToSideStream(std::string payload) {
  auto payload_ptr = std::make_shared<std::string>(std::move(payload));
  return Seq(
      // Wait until send state is kIdle, then mark kSendInFlight.
      [self = WeakRef()]() -> Poll<StatusFlag> {
        if (self->streaming_call_ == nullptr ||
            self->ext_proc_send_state_ == SideStreamSendState::kSendFailed ||
            self->IsSideStreamClosed() || self->drain_requested_) {
          return Failure{};
        }
        if (self->ext_proc_send_state_ != SideStreamSendState::kIdle) {
          return self->ext_proc_send_waiters_.AddPending(
              GetContext<Activity>()->MakeNonOwningWaker());
        }
        self->ext_proc_send_state_ = SideStreamSendState::kSendInFlight;
        return Success{};
      },
      // Safely acquire streaming_call_ and push the payload.
      [self = WeakRef(), payload_ptr](StatusFlag status) mutable {
        return If(
            !status.ok() || self->streaming_call_ == nullptr ||
                self->ext_proc_send_state_ ==
                    SideStreamSendState::kSendFailed ||
                self->IsSideStreamClosed() || self->drain_requested_,
            [self]() { return Immediate(self->EvaluateSideStreamStatus()); },
            [self, payload_ptr]() mutable {
              return self->streaming_call_->PushMessage(
                  std::move(*payload_ptr));
            });
      },
      // Reset send state and wake up any waiting senders.
      [self = WeakRef()](StatusFlag status) -> StatusFlag {
        self->ext_proc_send_state_ = status.ok() && !self->IsSideStreamClosed()
                                         ? SideStreamSendState::kIdle
                                         : SideStreamSendState::kSendFailed;
        self->ext_proc_send_waiters_.TakeWakeupSet().Wakeup();
        return self->EvaluateSideStreamStatus(status);
      });
}

// Spawns the read-from-server loop on initiator_.
// Called from StartChildCall().
//
// Pulls server initial metadata, messages, and trailing metadata from
// downstream and immediately forwards them across inter-activity mechanisms
// to the handler_ activity.
void ExtProcFilter::ExtProcCall::SpawnReadFromServerLoop() {
  initiator_.SpawnGuarded("read_from_server", [self = WeakRef()]() {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << self->DebugTag() << "read_from_server task started";
    return Race(
        Seq(self->initiator_.PullServerTrailingMetadata(),
            [self](ServerMetadataHandle metadata) -> StatusFlag {
              self->server_to_client_messages_.sender.MarkClosed();
              self->server_trailing_metadata_latch_.Set(std::move(metadata));
              return Success{};
            }),
        TrySeq(self->initiator_.PullServerInitialMetadata(),
               [self](std::optional<ServerMetadataHandle> metadata) {
                 self->server_initial_metadata_latch_.Set(std::move(metadata));
                 return Seq(
                     ForEach(MessagesFrom(self->initiator_),
                             [self](MessageHandle message) {
                               return Map(
                                   self->server_to_client_messages_.sender.Push(
                                       std::move(message)),
                                   [](bool x) { return StatusFlag(x); });
                             }),
                     [](StatusFlag status) {
                       return If(
                           !status.ok(),
                           [status]() { return Immediate(status); },
                           []() { return Never<StatusFlag>(); });
                     });
               }));
  });
}

void ExtProcFilter::ExtProcCall::StartChildCall(ClientMetadataHandle metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Starting downstream child call";
  if (client_initial_metadata_start_time_ != Timestamp::InfPast()) {
    ext_proc_filter_->RecordClientHeadersDuration(
        (Timestamp::Now() - client_initial_metadata_start_time_).seconds());
  }
  initiator_ = ext_proc_filter_->MakeChildCall(std::move(metadata),
                                               handler_.arena()->Ref());
  handler_.AddChildCall(initiator_);
  SpawnReadFromServerLoop();
}

//
// Read-from-sidestream Event Handlers
//

StatusFlag
ExtProcFilter::ExtProcCall::HandleClientInitialMetadataFromSidestream(
    const ExtProcResponse::RequestHeaders& response) {
  if (!processing_mode().send_request_headers) {
    CancelCallWithError(absl::InternalError(
        "Received request headers response but request headers are disabled"));
    return Failure{};
  }
  if (request_event_state_ != SideStreamRequestEventState::kInit) {
    CancelCallWithError(
        absl::InternalError("Received unexpected request headers response from "
                            "external processor"));
    return Failure{};
  }
  request_event_state_ = SideStreamRequestEventState::kHeadersReceived;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag()
      << "Processing external processor response for client initial "
         "metadata";
  if (auto status =
          ApplyHeaderMutations(response.mutation, config().mutation_rules,
                               *client_initial_metadata_);
      !status.ok()) {
    CancelCallWithError(status);
    return Failure{};
  }
  StartChildCall(std::move(client_initial_metadata_));
  return Success{};
}

StatusFlag ExtProcFilter::ExtProcCall::HandleClientMessageFromSidestream(
    const ExtProcResponse::RequestBody& response) {
  if (!processing_mode().send_request_body) {
    CancelCallWithError(absl::InternalError(
        "Received request body response but request body is disabled"));
    return Failure{};
  }
  if (processing_mode().send_request_headers &&
      request_event_state_ != SideStreamRequestEventState::kHeadersReceived) {
    CancelCallWithError(absl::InternalError(
        "Received request body response before request headers response"));
    return Failure{};
  }
  if (outstanding_c2s_messages_ == 0) {
    CancelCallWithError(absl::InternalError(
        "Received unexpected request body response from external processor"));
    return Failure{};
  }
  --outstanding_c2s_messages_;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Parsed request body response, eos: "
      << response.mutation.end_of_stream << ", eos_without_msg: "
      << response.mutation.end_of_stream_without_message;
  if (response.mutation.end_of_stream) {
    ext_proc_closed_client_sends_ = true;
    if (response.mutation.end_of_stream_without_message && !c2s_writes_done_) {
      // TODO(rishesh): If the client is still sending messages on the data
      // plane (!c2s_writes_done_) when the external processor closes client
      // sends without a message (end_of_stream_without_message), future client
      // messages cannot be processed because no further responses will be
      // received from the side-stream. Since message dropping is not yet
      // supported in Call v3, fail the call here. Remove this once PH2 is
      // implemented.
      CancelCallWithError(
          absl::InternalError("Client sends closed by external processor"));
      return Failure{};
    }
  }
  const bool send_request_body =
      processing_mode().send_request_body && !IsSideStreamClosed();
  if (!send_request_body || config().observability_mode) {
    return Success{};
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Processing external processor response for client body";
  if (!response.mutation.end_of_stream_without_message) {
    auto slice = Slice::FromCopiedString(response.mutation.body);
    auto new_msg = initiator_.arena()->MakePooled<Message>(
        SliceBuffer(std::move(slice)), /*flags=*/0);
    // TODO(rishesh, roth): Spawning this push into the activity means that we
    // don't have flow control feedback here due to a limitation of the v3-to-v1
    // adaptor layers.
    initiator_.SpawnPushMessage(std::move(new_msg));
  }
  if (response.mutation.end_of_stream ||
      response.mutation.end_of_stream_without_message) {
    if (c2s_writes_done_ || !IsSideStreamClosed()) {
      if (client_half_close_start_time_ != Timestamp::InfPast()) {
        ext_proc_filter_->RecordClientHalfCloseDuration(
            (Timestamp::Now() - client_half_close_start_time_).seconds());
      }
      initiator_.SpawnFinishSends();
    }
  }
  return Success{};
}

StatusFlag
ExtProcFilter::ExtProcCall::HandleServerInitialMetadataFromSidestream(
    const ExtProcResponse::ResponseHeaders& response) {
  if (!processing_mode().send_response_headers) {
    CancelCallWithError(absl::InternalError(
        "Received response headers response but response headers are "
        "disabled"));
    return Failure{};
  }
  if (response_event_state_ != SideStreamResponseEventState::kInit) {
    CancelCallWithError(absl::InternalError(
        "Received unexpected response headers response from external "
        "processor"));
    return Failure{};
  }
  response_event_state_ = SideStreamResponseEventState::kHeadersReceived;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag()
      << "Processing external processor response for server initial "
         "metadata";
  if (is_trailers_only_) {
    if (server_trailing_metadata_ == nullptr) {
      CancelCallWithError(absl::InternalError(
          "Server trailers not found in trailers-only response"));
      return Failure{};
    }
    if (auto status =
            ApplyHeaderMutations(response.mutation, config().mutation_rules,
                                 *server_trailing_metadata_);
        !status.ok()) {
      CancelCallWithError(status);
      return Failure{};
    }
    if (!IsFailOpenAllowed() && IsSideStreamClosed()) {
      return Failure{};
    }
    if (server_trailing_metadata_start_time_ != Timestamp::InfPast()) {
      ext_proc_filter_->RecordServerTrailersDuration(
          (Timestamp::Now() - server_trailing_metadata_start_time_).seconds());
    }
    handler_.SpawnPushServerTrailingMetadata(
        std::move(server_trailing_metadata_));
    return Success{};
  }
  if (server_initial_metadata_ == nullptr) {
    CancelCallWithError(
        absl::InternalError("Server initial metadata not found"));
    return Failure{};
  }
  if (auto status =
          ApplyHeaderMutations(response.mutation, config().mutation_rules,
                               *server_initial_metadata_);
      !status.ok()) {
    CancelCallWithError(status);
    return Failure{};
  }
  if (!IsFailOpenAllowed() && IsSideStreamClosed()) {
    return Failure{};
  }
  if (server_initial_metadata_start_time_ != Timestamp::InfPast()) {
    ext_proc_filter_->RecordServerHeadersDuration(
        (Timestamp::Now() - server_initial_metadata_start_time_).seconds());
  }
  handler_.SpawnPushServerInitialMetadata(std::move(server_initial_metadata_));
  return Success{};
}

StatusFlag ExtProcFilter::ExtProcCall::HandleServerMessageFromSidestream(
    const ExtProcResponse::ResponseBody& response) {
  if (!processing_mode().send_response_body) {
    CancelCallWithError(absl::InternalError(
        "Received response body response but response body is disabled"));
    return Failure{};
  }
  if (is_trailers_only_) {
    CancelCallWithError(absl::InternalError(
        "Received response body response in a Trailers-Only call"));
    return Failure{};
  }
  if (processing_mode().send_response_headers &&
      response_event_state_ != SideStreamResponseEventState::kHeadersReceived) {
    CancelCallWithError(absl::InternalError(
        "Received response body response before response headers response"));
    return Failure{};
  }
  if (processing_mode().send_response_trailers &&
      response_event_state_ ==
          SideStreamResponseEventState::kTrailersReceived) {
    CancelCallWithError(absl::InternalError(
        "Received response body response after response trailers response"));
    return Failure{};
  }
  if (outstanding_s2c_messages_ == 0) {
    CancelCallWithError(absl::InternalError(
        "Received unexpected response body response from external processor"));
    return Failure{};
  }
  --outstanding_s2c_messages_;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Processing external processor response for server body";
  auto slice = Slice::FromCopiedString(response.mutation.body);
  auto new_msg = handler_.arena()->MakePooled<Message>(
      SliceBuffer(std::move(slice)), /*flags=*/0);
  // TODO(rishesh, roth): Spawning this push into the activity means that we
  // don't have flow control feedback here due to a limitation of the v3-to-v1
  // adaptor layers.
  handler_.SpawnPushMessage(std::move(new_msg));
  return Success{};
}

StatusFlag
ExtProcFilter::ExtProcCall::HandleServerTrailingMetadataFromSidestream(
    const ExtProcResponse::ResponseTrailers& response) {
  if (!processing_mode().send_response_trailers) {
    CancelCallWithError(absl::InternalError(
        "Received response trailers response but response trailers are "
        "disabled"));
    return Failure{};
  }
  if (is_trailers_only_) {
    CancelCallWithError(absl::InternalError(
        "Received response trailers response in a Trailers-Only call"));
    return Failure{};
  }
  if (processing_mode().send_response_headers &&
      response_event_state_ != SideStreamResponseEventState::kHeadersReceived) {
    CancelCallWithError(absl::InternalError(
        "Received response trailers response before response headers "
        "response"));
    return Failure{};
  }
  if (processing_mode().send_response_body && outstanding_s2c_messages_ > 0) {
    CancelCallWithError(absl::InternalError(
        "Received response trailers response before all outstanding "
        "response body responses were received"));
    return Failure{};
  }
  response_event_state_ = SideStreamResponseEventState::kTrailersReceived;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag()
      << "Processing external processor response for server trailing "
         "metadata";
  if (server_trailing_metadata_ == nullptr) {
    CancelCallWithError(
        absl::InternalError("Server trailing metadata not found"));
    return Failure{};
  }
  if (auto status =
          ApplyHeaderMutations(response.mutation, config().mutation_rules,
                               *server_trailing_metadata_);
      !status.ok()) {
    CancelCallWithError(status);
    return Failure{};
  }
  if (server_trailing_metadata_start_time_ != Timestamp::InfPast()) {
    ext_proc_filter_->RecordServerTrailersDuration(
        (Timestamp::Now() - server_trailing_metadata_start_time_).seconds());
  }
  handler_.SpawnPushServerTrailingMetadata(
      std::move(server_trailing_metadata_));
  return Success{};
}

StatusFlag ExtProcFilter::ExtProcCall::HandleImmediateResponseFromSidestream(
    const ExtProcResponse::ImmediateResponse& response) {
  if (config().disable_immediate_response) {
    CancelCallWithError(absl::InternalError(
        "unhandled immediate response due to config disabled it"));
    return Failure{};
  }
  if (processing_mode().send_response_trailers) {
    response_event_state_ = SideStreamResponseEventState::kTrailersReceived;
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Processing external processor immediate response";
  auto error_md = CancelledServerMetadataFromStatus(
      static_cast<grpc_status_code>(response.status), response.details);
  (void)ApplyHeaderMutations(response.mutation, config().mutation_rules,
                             *error_md);
  handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
  return Success{};
}

auto ExtProcFilter::ExtProcCall::ProcessSideStreamResponse(
    absl::string_view payload) {
  // In observability mode, we only log the message and ignore it.
  // We must continue reading the stream to keep it alive.
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag()
        << "message received in observability mode (ignored), size="
        << payload.size();
    return Immediate(StatusFlag(Success{}));
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "message received, size=" << payload.size();
  // Parse the response from the external processor.
  auto parsed_response = ExtProcResponse::Parse(payload);
  if (!parsed_response.ok()) {
    CancelCallWithError(parsed_response.status());
    return Immediate(StatusFlag(Failure{}));
  }
  // If the server requests a drain, we half-close the stream to signal
  // we are done sending requests.
  if (parsed_response->request_drain) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "received request_drain=true";
    drain_requested_ = true;
    if (streaming_call_ != nullptr) {
      GRPC_TRACE_LOG(ext_proc_filter, INFO)
          << DebugTag() << "sending half-close";
      streaming_call_->SendHalfClose();
    }
  }
  // Dispatch the parsed response to the appropriate processor based on the
  // response type.
  return Match(
      (*parsed_response).response,
      [&](const ExtProcResponse::ImmediateResponse& response) {
        return Immediate(HandleImmediateResponseFromSidestream(response));
      },
      [&](const ExtProcResponse::RequestHeaders& response) {
        return Immediate(HandleClientInitialMetadataFromSidestream(response));
      },
      [&](const ExtProcResponse::ResponseHeaders& response) {
        return Immediate(HandleServerInitialMetadataFromSidestream(response));
      },
      [&](const ExtProcResponse::ResponseTrailers& response) {
        return Immediate(HandleServerTrailingMetadataFromSidestream(response));
      },
      [&](const ExtProcResponse::RequestBody& response) {
        return Immediate(HandleClientMessageFromSidestream(response));
      },
      [&](const ExtProcResponse::ResponseBody& response) {
        return Immediate(HandleServerMessageFromSidestream(response));
      },
      [](std::monostate) { return Immediate(StatusFlag(Success{})); });
}

void ExtProcFilter::ExtProcCall::HandleSideStreamStatus(absl::Status status) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "status received: " << status;
  if (IsSideStreamClosed()) return;
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
  const bool fail_data_plane_stream = !status.ok() && !IsFailOpenAllowed();
  if (fail_data_plane_stream) {
    CancelCallWithError(status);
    return;
  }
  // Not failing, so make sure we process any outstanding processors by
  // forwarding unmutated metadata.
  if (processing_mode().send_request_headers &&
      request_event_state_ == SideStreamRequestEventState::kInit &&
      client_initial_metadata_ != nullptr) {
    (void)HandleClientInitialMetadataFromSidestream(
        ExtProcResponse::RequestHeaders{});
  }
  if (processing_mode().send_response_headers &&
      response_event_state_ == SideStreamResponseEventState::kInit &&
      ((!is_trailers_only_ && server_initial_metadata_ != nullptr) ||
       (is_trailers_only_ && server_trailing_metadata_ != nullptr))) {
    (void)HandleServerInitialMetadataFromSidestream(
        ExtProcResponse::ResponseHeaders{});
  }
  if (processing_mode().send_response_trailers && !is_trailers_only_ &&
      response_event_state_ !=
          SideStreamResponseEventState::kTrailersReceived &&
      server_trailing_metadata_ != nullptr) {
    (void)HandleServerTrailingMetadataFromSidestream(
        ExtProcResponse::ResponseTrailers{});
  }
  CloseSideStream();
}

//
// Read-from-client Event Handlers
//

auto ExtProcFilter::ExtProcCall::HandleInitialMetadataFromClient(
    ClientMetadataHandle metadata) {
  auto md = std::make_shared<ClientMetadataHandle>(std::move(metadata));
  return If(
      !processing_mode().send_request_headers,
      [self = WeakRef(), md]() mutable {
        // If request header processing is disabled, forward metadata directly
        // without calling ext_proc.
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << self->DebugTag()
            << "Skipping client initial metadata (processing mode disabled)";
        // If request body will be sent later and request attributes are
        // configured, extract initial attributes from client metadata.
        if (self->processing_mode().send_request_body &&
            !self->config().request_attributes.empty()) {
          self->request_attributes_ = CreateExtProcAttributesProtoStruct(
              self->request_attributes_arena_.ptr(),
              self->config().request_attributes, **md,
              self->ext_proc_filter_->default_authority_.as_string_view(),
              self->GetConnectionAttributes());
        }
        // Directly start downstream child call with unmodified client metadata.
        self->StartChildCall(std::move(*md));
        return Immediate(StatusFlag(Success{}));
      },
      [self = WeakRef(), md]() mutable {
        // Construct ext_proc request for client initial metadata.
        // Include processing mode in the request if this is the first message
        // on the stream.
        std::optional<ExtProcProcessingMode> processing_mode;
        if (self->IsFirstMessageOnSideStream()) {
          processing_mode = self->config().processing_mode;
        }
        upb::Arena arena;
        auto* header_attributes = CreateExtProcAttributesProtoStruct(
            arena.ptr(), self->config().request_attributes, **md,
            self->ext_proc_filter_->default_authority_.as_string_view(),
            self->GetConnectionAttributes());
        auto payload = CreateExtProcClientHeadersRequest(
            arena.ptr(), (*md).get(), self->config().forwarding_allowed_headers,
            self->config().forwarding_disallowed_headers, header_attributes,
            self->config().observability_mode, processing_mode);
        return If(
            !payload.ok(),
            [self, status = payload.status()]() {
              self->CancelCallWithError(status);
              return Immediate(StatusFlag(Failure{}));
            },
            [self, payload = std::move(*payload), md]() mutable {
              // In observability mode, send to the child call in parallel with
              // sending to the sidestream.
              if (self->config().observability_mode) {
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << self->DebugTag()
                    << "observability mode: starting child call";
                self->StartChildCall(std::move(*md));
              } else {
                self->client_initial_metadata_start_time_ = Timestamp::Now();
                self->client_initial_metadata_ = std::move(*md);
              }
              // Send the serialized request payload over the side-stream.
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << self->DebugTag()
                  << "Sending client initial metadata to sidestream";
              return self->SendMessageToSideStream(std::move(payload));
            });
      });
}

auto ExtProcFilter::ExtProcCall::HandleMessageFromClient(
    MessageHandle message) {
  const bool send_request_body =
      processing_mode().send_request_body && !IsSideStreamClosed();
  auto msg = std::make_shared<MessageHandle>(std::move(message));
  return If(
      !send_request_body,
      [self = WeakRef(), msg]() mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << self->DebugTag()
            << "Client message non-processing mode (processing disabled or "
               "closed)";
        // TODO(rishesh, roth): Spawning this push into the activity means that
        // we don't have flow control feedback here due to a limitation of the
        // v3-to-v1 adaptor layers.
        self->initiator_.SpawnPushMessage(std::move(*msg));
        return Immediate(StatusFlag(Success{}));
      },
      [self = WeakRef(), msg]() mutable {
        // TODO(rishesh): If the external processor has already closed client
        // sends (via end_of_stream or end_of_stream_without_message in
        // ProcessingResponse), any subsequent message from the client cannot be
        // processed. Since message dropping is not yet supported in Call v3,
        // fail the call here. Remove this once PH2 is implemented.
        return If(
            self->ext_proc_closed_client_sends_,
            [self]() {
              self->CancelCallWithError(absl::InternalError(
                  "Client sends closed by external processor"));
              return Immediate(StatusFlag(Failure{}));
            },
            [self, msg]() mutable {
              return If(
                  self->drain_requested_,
                  [self, msg]() mutable {
                    return TrySeq(
                        self->WaitForSideStreamClosed(),
                        [self, msg]() mutable -> StatusFlag {
                          // TODO(rishesh, roth): Spawning this push into the
                          // activity means that we don't have flow control
                          // feedback here due to a limitation of the v3-to-v1
                          // adaptor layers.
                          self->initiator_.SpawnPushMessage(std::move(*msg));
                          return Success{};
                        });
                  },
                  [self, msg]() mutable {
                    // Construct message for sidestream.
                    std::string message_bytes;
                    if (*msg != nullptr) {
                      message_bytes = (*msg)->payload()->JoinIntoString();
                    }
                    if (!self->config().observability_mode) {
                      ++self->outstanding_c2s_messages_;
                    }
                    std::optional<ExtProcProcessingMode> processing_mode;
                    if (self->IsFirstMessageOnSideStream()) {
                      processing_mode = self->config().processing_mode;
                    }
                    upb::Arena arena;
                    auto payload = CreateExtProcClientBodyRequest(
                        arena.ptr(), message_bytes, self->request_attributes_,
                        self->config().observability_mode, processing_mode,
                        /*end_of_stream=*/false,
                        /*end_of_stream_without_message=*/false);
                    self->request_attributes_ = nullptr;
                    return If(
                        !payload.ok(),
                        [self, status = payload.status()]() {
                          self->CancelCallWithError(status);
                          return Immediate(StatusFlag(Failure{}));
                        },
                        [self, payload = std::move(*payload), msg]() mutable {
                          self->first_body_message_sent_ = true;
                          if (self->config().observability_mode) {
                            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                                << self->DebugTag()
                                << "Client message observability mode";
                            // TODO(rishesh, roth): In observability mode, we
                            // ideally want to wait for both the message write
                            // to the child call and message send to the
                            // ext_proc side stream to complete before fetching
                            // the next message for proper flow control.
                            // However, returning a direct
                            // initiator_.PushMessage() promise here causes a
                            // deadlock due to a limitation of the v3-to-v1
                            // adaptor layers, where the parent call batch
                            // completion is blocked by the handler promise
                            // execution. If we do not make them sequential and
                            // spawn the push instead, some tests become flaky
                            // in observability cases. Therefore, we spawn the
                            // push into the initiator activity and
                            // sequentially send to the sidestream. We need to
                            // check and revisit this once the adaptor layers
                            // support full Call v3 flow control.
                            self->initiator_.SpawnPushMessage(std::move(*msg));
                          }
                          return self->SendMessageToSideStream(
                              std::move(payload));
                        });
                  });
            });
      });
}

auto ExtProcFilter::ExtProcCall::HandleHalfCloseFromClient() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "HandleHalfCloseFromClient invoked";
  const bool send_request_body =
      processing_mode().send_request_body && !IsSideStreamClosed();
  return If(
      !send_request_body,
      [self = WeakRef()]() {
        self->initiator_.SpawnFinishSends();
        self->c2s_writes_done_ = true;
        return Immediate(StatusFlag(Success{}));
      },
      [self = WeakRef()]() {
        self->c2s_writes_done_ = true;
        return If(
            self->ext_proc_closed_client_sends_,
            []() { return Immediate(StatusFlag(Success{})); },
            [self]() {
              return If(
                  !self->config().observability_mode && self->drain_requested_,
                  [self]() {
                    return TrySeq(self->WaitForSideStreamClosed(),
                                  [self]() mutable -> StatusFlag {
                                    self->initiator_.SpawnFinishSends();
                                    return Success{};
                                  });
                  },
                  [self]() {
                    if (!self->config().observability_mode) {
                      self->client_half_close_start_time_ = Timestamp::Now();
                      ++self->outstanding_c2s_messages_;
                    }
                    std::optional<ExtProcProcessingMode> processing_mode;
                    if (self->IsFirstMessageOnSideStream()) {
                      processing_mode = self->config().processing_mode;
                    }
                    upb::Arena arena;
                    auto payload = CreateExtProcClientBodyRequest(
                        arena.ptr(), /*body=*/"", self->request_attributes_,
                        self->config().observability_mode, processing_mode,
                        /*end_of_stream=*/false,
                        /*end_of_stream_without_message=*/true);
                    self->request_attributes_ = nullptr;
                    return If(
                        !payload.ok(),
                        [self, status = payload.status()]() {
                          self->CancelCallWithError(status);
                          return Immediate(StatusFlag(Failure{}));
                        },
                        [self, payload = std::move(*payload)]() mutable {
                          self->first_body_message_sent_ = true;
                          return Seq(
                              self->SendMessageToSideStream(std::move(payload)),
                              [self](StatusFlag status) mutable -> StatusFlag {
                                if (!status.ok()) return Failure{};
                                if (self->IsSideStreamClosed() ||
                                    self->config().observability_mode) {
                                  if (!self->config().observability_mode &&
                                      self->client_half_close_start_time_ !=
                                          Timestamp::InfPast()) {
                                    self->ext_proc_filter_
                                        ->RecordClientHalfCloseDuration(
                                            (Timestamp::Now() -
                                             self->client_half_close_start_time_)
                                                .seconds());
                                  }
                                  self->initiator_.SpawnFinishSends();
                                }
                                return Success{};
                              });
                        });
                  });
            });
      });
}

//
// Read-from-server Event Handlers
//

auto ExtProcFilter::ExtProcCall::HandleInitialMetadataFromServer(
    std::optional<ServerMetadataHandle> metadata) {
  auto md = std::make_shared<std::optional<ServerMetadataHandle>>(
      std::move(metadata));
  return If(
      !md->has_value(),
      [self = WeakRef()]() {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << self->DebugTag()
            << "No server initial metadata (trailers-only response)";
        self->is_trailers_only_ = true;
        return Immediate(StatusFlag(Success{}));
      },
      [self = WeakRef(), md]() mutable {
        self->server_read_event_state_ =
            ServerReadEventState::kInitialMetadataReceived;
        return If(
            !self->processing_mode().send_response_headers ||
                self->IsSideStreamClosed() || self->drain_requested_,
            [self, md]() mutable {
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << self->DebugTag()
                  << "Skipping server initial metadata (processing disabled, "
                     "stream closed, or drain mode)";
              if (self->IsSideStreamFailureFatal()) {
                return Immediate(StatusFlag(Failure{}));
              }
              self->handler_.SpawnPushServerInitialMetadata(std::move(**md));
              return Immediate(StatusFlag(Success{}));
            },
            [self, md]() mutable {
              // Include processing mode if this is the first message on the
              // stream.
              std::optional<ExtProcProcessingMode> processing_mode;
              if (self->IsFirstMessageOnSideStream()) {
                processing_mode = self->config().processing_mode;
              }
              upb::Arena arena;
              auto payload = CreateExtProcServerHeadersRequest(
                  arena.ptr(), (*md)->get(),
                  self->config().forwarding_allowed_headers,
                  self->config().forwarding_disallowed_headers,
                  /*attributes=*/nullptr, self->config().observability_mode,
                  processing_mode,
                  /*end_of_stream=*/false);
              return If(
                  !payload.ok(),
                  [self, status = payload.status()]() {
                    self->CancelCallWithError(status);
                    return Immediate(StatusFlag(Failure{}));
                  },
                  [self, payload = std::move(*payload), md]() mutable {
                    if (self->config().observability_mode) {
                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                          << self->DebugTag()
                          << "Sending server initial metadata (observability "
                             "mode)";
                      self->handler_.SpawnPushServerInitialMetadata(
                          std::move(**md));
                    } else {
                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                          << self->DebugTag()
                          << "Sending server initial metadata (normal mode)";
                      self->server_initial_metadata_start_time_ =
                          Timestamp::Now();
                      self->server_initial_metadata_ = std::move(**md);
                    }
                    return self->SendMessageToSideStream(std::move(payload));
                  });
            });
      });
}

auto ExtProcFilter::ExtProcCall::HandleTrailingMetadataFromServer(
    ServerMetadataHandle metadata) {
  const bool send_metadata = is_trailers_only_
                                 ? processing_mode().send_response_headers
                                 : processing_mode().send_response_trailers;
  auto md = std::make_shared<ServerMetadataHandle>(std::move(metadata));
  return If(
      IsSideStreamFailureFatal(),
      []() { return Immediate(StatusFlag(Failure{})); },
      [self = WeakRef(), md, send_metadata]() mutable {
        return If(
            !IsStatusOk(**md),
            [self, md]() mutable {
              // If trailing status is not OK (e.g. error from downstream), pass
              // trailers through directly.
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << self->DebugTag()
                  << "Passing through non-OK server trailing metadata";
              self->handler_.SpawnPushServerTrailingMetadata(std::move(*md));
              return Immediate(StatusFlag(Success{}));
            },
            [self, md, send_metadata]() mutable {
              return If(
                  !send_metadata || self->IsSideStreamClosed(),
                  [self, md]() mutable {
                    GRPC_TRACE_LOG(ext_proc_filter, INFO)
                        << self->DebugTag()
                        << "Skipping server trailing metadata (processing "
                           "disabled or stream closed)";
                    self->handler_.SpawnPushServerTrailingMetadata(
                        std::move(*md));
                    return Immediate(StatusFlag(Success{}));
                  },
                  [self, md]() mutable {
                    // Include processing mode if this is the first message on
                    // the stream.
                    std::optional<ExtProcProcessingMode> processing_mode;
                    if (self->IsFirstMessageOnSideStream()) {
                      processing_mode = self->config().processing_mode;
                    }
                    upb::Arena arena;
                    auto payload =
                        self->is_trailers_only_
                            ? CreateExtProcServerHeadersRequest(
                                  arena.ptr(), (*md).get(),
                                  self->config().forwarding_allowed_headers,
                                  self->config().forwarding_disallowed_headers,
                                  /*attributes=*/nullptr,
                                  self->config().observability_mode,
                                  processing_mode, /*end_of_stream=*/true)
                            : CreateExtProcServerTrailersRequest(
                                  arena.ptr(), (*md).get(),
                                  self->config().forwarding_allowed_headers,
                                  self->config().forwarding_disallowed_headers,
                                  /*attributes=*/nullptr,
                                  self->config().observability_mode,
                                  processing_mode);
                    return If(
                        !payload.ok(),
                        [self, status = payload.status()]() {
                          self->CancelCallWithError(status);
                          return Immediate(StatusFlag(Failure{}));
                        },
                        [self,
                         payload_ptr =
                             std::make_shared<std::string>(std::move(*payload)),
                         md]() mutable {
                          self->server_trailers_sent_to_side_stream_ = true;
                          return If(
                              self->config().observability_mode,
                              [self, payload_ptr, md]() mutable {
                                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                                    << self->DebugTag()
                                    << "Sending server trailing metadata "
                                       "(observability mode)";
                                return Seq(
                                    self->SendMessageToSideStream(
                                        std::move(*payload_ptr)),
                                    [self,
                                     md](StatusFlag) mutable -> StatusFlag {
                                      self->handler_
                                          .SpawnPushServerTrailingMetadata(
                                              std::move(*md));
                                      return Success{};
                                    });
                              },
                              [self, payload_ptr, md]() mutable {
                                return If(
                                    self->drain_requested_,
                                    [self, md]() mutable {
                                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                                          << self->DebugTag()
                                          << "Handling server trailing "
                                             "metadata in drain mode";
                                      return TrySeq(
                                          self->WaitForSideStreamClosed(),
                                          [self, md]() mutable -> StatusFlag {
                                            self->handler_
                                                .SpawnPushServerTrailingMetadata(
                                                    std::move(*md));
                                            return Success{};
                                          });
                                    },
                                    [self, payload_ptr, md]() mutable {
                                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                                          << self->DebugTag()
                                          << "Sending server trailing metadata "
                                             "(normal mode)";
                                      self->server_trailing_metadata_start_time_ =
                                          Timestamp::Now();
                                      self->server_trailing_metadata_ =
                                          std::move(*md);
                                      return self->SendMessageToSideStream(
                                          std::move(*payload_ptr));
                                    });
                              });
                        });
                  });
            });
      });
}

//
// ExtProcFilter::ExtProcCall Server Message Processing
//

auto ExtProcFilter::ExtProcCall::HandleMessageFromServer(
    MessageHandle message) {
  const bool send_body =
      processing_mode().send_response_body && !IsSideStreamClosed();
  auto msg = std::make_shared<MessageHandle>(std::move(message));
  return If(
      !send_body,
      [self = WeakRef(), msg]() mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << self->DebugTag() << "Server message non-processing mode";
        if (self->IsSideStreamFailureFatal()) {
          return Immediate(StatusFlag(Failure{}));
        }
        // TODO(rishesh, roth): Spawning this push into the activity means that
        // we don't have flow control feedback here due to a limitation of the
        // v3-to-v1 adaptor layers.
        self->handler_.SpawnPushMessage(std::move(*msg));
        return Immediate(StatusFlag(Success{}));
      },
      [self = WeakRef(), msg]() mutable {
        return If(
            self->drain_requested_,
            [self, msg]() mutable {
              return TrySeq(self->WaitForSideStreamClosed(),
                            [self, msg]() mutable -> StatusFlag {
                              // TODO(rishesh, roth): Spawning this push into
                              // the activity means that we don't have flow
                              // control feedback here due to a limitation of
                              // the v3-to-v1 adaptor layers.
                              self->handler_.SpawnPushMessage(std::move(*msg));
                              return Success{};
                            });
            },
            [self, msg]() mutable {
              // Construct message for sidestream.
              std::string message_bytes;
              if (*msg != nullptr) {
                message_bytes = (*msg)->payload()->JoinIntoString();
              }
              if (!self->config().observability_mode) {
                ++self->outstanding_s2c_messages_;
              }
              std::optional<ExtProcProcessingMode> processing_mode;
              if (self->IsFirstMessageOnSideStream()) {
                processing_mode = self->config().processing_mode;
              }
              upb::Arena arena;
              auto payload = CreateExtProcServerBodyRequest(
                  arena.ptr(), message_bytes, /*attributes=*/nullptr,
                  self->config().observability_mode, processing_mode);
              return If(
                  !payload.ok(),
                  [self, status = payload.status()]() {
                    self->CancelCallWithError(status);
                    return Immediate(StatusFlag(Failure{}));
                  },
                  [self, payload = std::move(*payload), msg]() mutable {
                    self->first_body_message_sent_ = true;
                    if (self->config().observability_mode) {
                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                          << self->DebugTag()
                          << "Server message observability mode";
                      // TODO(rishesh, roth): In observability mode, we ideally
                      // want to wait for both the message write to the client
                      // (handler) and message send to the ext_proc side stream
                      // to complete before fetching the next message for proper
                      // flow control.
                      // However, returning a direct handler_.PushMessage()
                      // promise here causes a deadlock due to a limitation of
                      // the v3-to-v1 adaptor layers, where batch completion is
                      // blocked by the promise execution. If we do not make
                      // them sequential and spawn the push instead, some tests
                      // become flaky in observability cases. Therefore, we
                      // spawn the push into the handler activity and
                      // sequentially send to the sidestream. We need to check
                      // and revisit this once the adaptor layers support full
                      // Call v3 flow control.
                      self->handler_.SpawnPushMessage(std::move(*msg));
                    }
                    return self->SendMessageToSideStream(std::move(payload));
                  });
            });
      });
}

// Handle the read-from-client loop on handler_.
// Called when the ExtProcCall is created.
//
// Handles client initial metadata, client messages, and half-close.
// Sends each event to the ext_proc side-stream and/or to the server,
// based on the configuration.
auto ExtProcFilter::ExtProcCall::HandleReadFromClientLoop() {
  return TrySeq(
      handler_.PullClientInitialMetadata(),
      [self = WeakRef()](ClientMetadataHandle metadata) {
        return self->HandleInitialMetadataFromClient(std::move(metadata));
      },
      [self = WeakRef()]() {
        return ForEach(
            MessagesFrom(self->handler_), [self](MessageHandle message) {
              return self->HandleMessageFromClient(std::move(message));
            });
      },
      [self = WeakRef()]() { return self->HandleHalfCloseFromClient(); });
}

// Handle the read-from-server loop on handler_.
// Called when the ExtProcCall is created.
//
// Handles server initial metadata, server messages, and server
// trailing metadata received from the initiator_ activity.
// Sends each event to the ext_proc side-stream and/or to the client,
// based on the configuration.
auto ExtProcFilter::ExtProcCall::HandleReadFromServerLoop() {
  return Race(
      // Branch 1: Trailing metadata from downstream server.
      //
      // Trailing metadata can arrive early in two distinct cases:
      // 1. Trailers-only response: downstream returns trailing metadata without
      //    initial metadata or messages.
      // 2. Downstream cancellation/error: downstream aborts mid-stream with
      //    non-OK trailing metadata.
      //
      // In either early-arrival case (server_read_event_state_ ==
      // ServerReadEventState::kInit || !IsStatusOk(*metadata)), Branch 1
      // completes immediately, winning the race and cancelling Branch 2 (the
      // message/metadata pipeline) so the call terminates promptly.
      //
      // In a normal OK streaming response, downstream delivers trailing
      // metadata immediately after the last message on the wire. If Branch 1
      // were to resolve immediately, the Race combinator would cancel Branch 2
      // before ForEach can finish reading and dispatching in-flight messages
      // from the inter-activity pipe. Therefore, for OK streams, Branch 1
      // yields until Branch 2 signals that all buffered messages have been
      // processed (server_read_event_state_ ==
      // ServerReadEventState::kMessagesComplete).
      Seq(server_trailing_metadata_latch_.Wait(),
          [self = WeakRef()](ServerMetadataHandle metadata) {
            const bool is_early =
                self->server_read_event_state_ == ServerReadEventState::kInit ||
                !IsStatusOk(*metadata);
            if (self->server_read_event_state_ == ServerReadEventState::kInit) {
              self->is_trailers_only_ = true;
            }
            auto md =
                std::make_shared<ServerMetadataHandle>(std::move(metadata));
            return If(
                is_early,
                [self, md]() mutable {
                  return self->HandleTrailingMetadataFromServer(std::move(*md));
                },
                [self, md]() mutable {
                  return Seq(
                      [self]() -> Poll<StatusFlag> {
                        if (self->server_read_event_state_ !=
                            ServerReadEventState::kMessagesComplete) {
                          self->server_trailing_metadata_waker_ =
                              GetContext<Activity>()->MakeNonOwningWaker();
                          return Pending{};
                        }
                        return Success{};
                      },
                      [self, md]() mutable {
                        return self->HandleTrailingMetadataFromServer(
                            std::move(*md));
                      });
                });
          }),
      // Branch 2: Sequential pipeline: initial metadata -> messages
      TrySeq(
          server_initial_metadata_latch_.Wait(),
          [self = WeakRef()](std::optional<ServerMetadataHandle> metadata) {
            return self->HandleInitialMetadataFromServer(std::move(metadata));
          },
          [self = WeakRef()]() {
            return Seq(
                ForEach(std::move(self->server_to_client_messages_.receiver),
                        [self](MessageHandle message) {
                          return self->HandleMessageFromServer(
                              std::move(message));
                        }),
                [self](StatusFlag status) {
                  self->server_read_event_state_ =
                      ServerReadEventState::kMessagesComplete;
                  self->server_trailing_metadata_waker_.Wakeup();
                  return If(
                      status.ok(), []() { return Never<StatusFlag>(); },
                      [status]() { return Immediate(status); });
                });
          }));
}

// Continuously pulls response messages from the external processor side-stream
// and dispatches them until the stream closes or an error occurs.
auto ExtProcFilter::ExtProcCall::HandleReadFromSideStreamLoop() {
  return Seq(
      // Loop reading response messages from the side-stream until end-of-stream
      // or error.
      Loop([self = WeakRef()]() -> Promise<LoopCtl<StatusFlag>> {
        // CloseSideStream() moves out and resets streaming_call_, so it may be
        // null if the side-stream was closed while this loop was running. If
        // so, terminate the read loop cleanly.
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
      [self = WeakRef()](StatusFlag) -> Promise<absl::Status> {
        if (self->streaming_call_ == nullptr) {
          return Immediate(absl::InternalError("Side stream unavailable"));
        }
        return self->streaming_call_->PullServerTrailingMetadata();
      },
      // Handle stream closure and resolve final status.
      [self = WeakRef()](absl::Status status) -> StatusFlag {
        self->HandleSideStreamStatus(status);
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << self->DebugTag()
            << "HandleReadFromSideStreamLoop finished with status: " << status;
        return StatusFlag(status.ok() || self->IsFailOpenAllowed());
      });
}

// Runs the client-read, server-read, and side-stream-read loops until all
// complete.
auto ExtProcFilter::ExtProcCall::Run() {
  return Seq(
      TryJoin<absl::StatusOr>(HandleReadFromClientLoop(),
                              HandleReadFromServerLoop(),
                              HandleReadFromSideStreamLoop()),
      // Holds a strong reference to keep ExtProcCall alive throughout the
      // duration of the promise chain until all loops complete. Once all loops
      // join and this callback finishes, the strong ref count drops to 0,
      // triggering Orphaned() to close the side stream.
      [self = Ref()](
          absl::StatusOr<std::tuple<Empty, Empty, Empty>> res) -> absl::Status {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << self->DebugTag() << "Run() finished with status: " << res.ok();
        return self->GetSideStreamClosedStatus();
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
      is_server_(args.GetBool(GRPC_ARG_IS_SERVER_FILTER_STACK).value_or(false)),
      config_(std::move(config)),
      event_engine_(
          args.GetObjectRef<grpc_event_engine::experimental::EventEngine>()),
      default_authority_(Slice::FromCopiedString(
          args.GetString(is_server_ ? GRPC_ARG_SERVER_URI
                                    : GRPC_ARG_DEFAULT_AUTHORITY)
              .value_or(""))),
      telemetry_storage_([&]() -> TelemetryStorage {
        auto stats_plugin_group =
            args.GetObjectRef<GlobalStatsPluginRegistry::StatsPluginGroup>();
        if (stats_plugin_group == nullptr) return std::monostate{};
        auto scope = stats_plugin_group->GetCollectionScope();
        if (scope == nullptr) return std::monostate{};
        if (is_server_) {
          return ServerTelemetryDomain::GetStorage(std::move(scope));
        }
        return ClientTelemetryDomain::GetStorage(
            std::move(scope), args.GetString(GRPC_ARG_SERVER_URI).value_or(""));
      }()) {
  if (is_server_) {
    std::optional<absl::string_view> peer_uri =
        args.GetString(GRPC_ARG_ENDPOINT_PEER_ADDRESS);
    if (peer_uri.has_value()) {
      auto uri = URI::Parse(*peer_uri);
      if (uri.ok()) {
        absl::string_view host_view;
        absl::string_view port_view;
        if (SplitHostPort(uri->path(), &host_view, &port_view)) {
          source_address_ = std::string(host_view);
          int port = 0;
          if (absl::SimpleAtoi(port_view, &port)) {
            source_port_ = port;
          }
        } else {
          source_address_ = uri->path();
        }
      }
    }
  }
}

ExtProcFilter::~ExtProcFilter() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcFilter " << this << " destroyed";
}

void ExtProcFilter::RecordDuration(
    ClientTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
        client_metric,
    ServerTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
        server_metric,
    double duration_seconds) const {
  Match(
      telemetry_storage_, [](const std::monostate&) {},
      [duration_seconds,
       client_metric](const InstrumentStorageRefPtr<ClientTelemetryDomain>& s) {
        if (s != nullptr) {
          s->Increment(client_metric, static_cast<int64_t>(duration_seconds));
        }
      },
      [duration_seconds,
       server_metric](const InstrumentStorageRefPtr<ServerTelemetryDomain>& s) {
        if (s != nullptr) {
          s->Increment(server_metric, static_cast<int64_t>(duration_seconds));
        }
      });
}

void ExtProcFilter::RecordClientHeadersDuration(double duration_seconds) const {
  RecordDuration(ClientTelemetryDomain::kClientHeadersDuration,
                 ServerTelemetryDomain::kClientHeadersDuration,
                 duration_seconds);
}

void ExtProcFilter::RecordClientHalfCloseDuration(
    double duration_seconds) const {
  RecordDuration(ClientTelemetryDomain::kClientHalfCloseDuration,
                 ServerTelemetryDomain::kClientHalfCloseDuration,
                 duration_seconds);
}

void ExtProcFilter::RecordServerHeadersDuration(double duration_seconds) const {
  RecordDuration(ClientTelemetryDomain::kServerHeadersDuration,
                 ServerTelemetryDomain::kServerHeadersDuration,
                 duration_seconds);
}

void ExtProcFilter::RecordServerTrailersDuration(
    double duration_seconds) const {
  RecordDuration(ClientTelemetryDomain::kServerTrailersDuration,
                 ServerTelemetryDomain::kServerTrailersDuration,
                 duration_seconds);
}

void ExtProcFilter::InterceptCall(UnstartedCallHandler unstarted_call_handler) {
  if (!IsProcessingEnabled(config_->processing_mode)) {
    PassThrough(std::move(unstarted_call_handler));
    return;
  }
  CallHandler handler = Consume(std::move(unstarted_call_handler));
  handler.SpawnGuarded(
      "ext_proc_call",
      [handler, ext_proc_filter = RefAsSubclass<ExtProcFilter>()]() mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "[" << Activity::current()->DebugTag()
            << " ext_proc_filter=" << ext_proc_filter.get()
            << "] InterceptCall promise chain start";
        auto ext_proc_call = MakeRefCounted<ExtProcCall>(
            ext_proc_filter, ext_proc_filter->channel()->transport(), handler);
        return ext_proc_call->Run();
      });
}

}  // namespace grpc_core
