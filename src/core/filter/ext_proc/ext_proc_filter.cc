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

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/filter/ext_proc/ext_proc_messages.h"
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
#include "src/core/telemetry/metrics.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/streaming_call_promise_wrapper.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

//
// ExtProcFilter::TelemetryDomain
//

ExtProcFilter::TelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::TelemetryDomain::kClientHeadersDuration =
        ExtProcFilter::TelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.client_ext_proc.client_headers_duration",
            "Time between when the ext_proc filter sees the client's headers "
            "and when it allows those headers to continue on to the next "
            "filter.",
            "s", 60, 20);

ExtProcFilter::TelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::TelemetryDomain::kClientHalfCloseDuration =
        ExtProcFilter::TelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.client_ext_proc.client_half_close_duration",
            "Time between when the ext_proc filter sees the client's "
            "half-close and when it allows that half-close to continue on to "
            "the next filter.",
            "s", 60, 20);

ExtProcFilter::TelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::TelemetryDomain::kServerHeadersDuration =
        ExtProcFilter::TelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.client_ext_proc.server_headers_duration",
            "Time between when the ext_proc filter sees the server's headers "
            "and when it allows those headers to continue on to the next "
            "filter.",
            "s", 60, 20);

ExtProcFilter::TelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    ExtProcFilter::TelemetryDomain::kServerTrailersDuration =
        ExtProcFilter::TelemetryDomain::RegisterHistogram<
            ExponentialHistogramShape>(
            "grpc.client_ext_proc.server_trailers_duration",
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
//  LOOP 2: Read-From-Server Pipeline Loop [HandleReadFromServerActivityLoop()]
//  +-----------------------------------------------------------------------+
//  | Seq(                                                                  |
//  |     TrySeq(                                                           |
//  |         server_initial_metadata_latch_.Wait()                         |
//  |           -> HandleInitialMetadataFromServer(),                       |
//  |         If(has_initial_metadata,                                      |
//  |            ForEach(server_to_client_messages_.receiver)               |
//  |              -> HandleMessageFromServer())),                          |
//  |     server_trailing_metadata_latch_.Wait()                            |
//  |       -> HandleTrailingMetadataFromServer())                          |
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
//  | Seq(                                                                  |
//  |     initiator_.CancelIfFails(TrySeq(                                  |
//  |         initiator_.PullServerInitialMetadata()                        |
//  |           -> server_initial_metadata_latch_.Set(),                    |
//  |         If(has_initial_metadata,                                      |
//  |            ForEach(MessagesFrom(initiator_))                          |
//  |              -> server_to_client_messages_.sender.Push(),              |
//  |            server_to_client_messages_.sender.MarkClosed()))),         |
//  |     initiator_.PullServerTrailingMetadata()                           |
//  |       -> server_trailing_metadata_latch_.Set())                       |
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
    // Expecting request headers response from external processor.
    kExpectHeaders,
    // Expecting request body response(s) from external processor.
    kExpectBody,
    // Expecting no further request responses from external processor.
    kExpectNothing,
  };

  enum class SideStreamResponseEventState {
    // Expecting response headers response from external processor.
    kExpectHeaders,
    // Expecting response body or trailers response from external processor.
    kExpectBodyOrTrailers,
    // Expecting response trailers response from external processor.
    kExpectTrailers,
    // Expecting no further response responses from external processor.
    kExpectNothing,
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
  auto HandleReadFromServerActivityLoop();

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

  // Fails the intercepted data plane RPC with the given error status:
  // 1. Pushes error trailing metadata downstream to the client.
  // 2. Cancels any active upstream child call.
  // 3. Marks the side-stream closed.
  void CancelCallWithError(absl::Status status) {
    if (!side_stream_closed_latch_.is_set()) {
      if (!status.ok()) {
        auto error_md = CancelledServerMetadataFromStatus(status);
        handler_.SpawnPushServerTrailingMetadata(std::move(error_md));
        if (initiator_.is_set()) {
          initiator_.SpawnCancel();
        }
      }
      side_stream_closed_latch_.Set();
      ext_proc_send_state_ = SideStreamSendState::kSendFailed;
      ext_proc_send_waiter_.Wake();
    }
  }

  void Orphaned() override {}

  static SideStreamRequestEventState InitialRequestEventState(
      const Config& config) {
    if (config.observability_mode) {
      return SideStreamRequestEventState::kExpectNothing;
    }
    if (config.processing_mode.has_value() &&
        config.processing_mode->send_request_headers) {
      return SideStreamRequestEventState::kExpectHeaders;
    }
    if (config.processing_mode.has_value() &&
        config.processing_mode->send_request_body) {
      return SideStreamRequestEventState::kExpectBody;
    }
    return SideStreamRequestEventState::kExpectNothing;
  }

  static SideStreamResponseEventState InitialResponseEventState(
      const Config& config) {
    if (config.observability_mode) {
      return SideStreamResponseEventState::kExpectNothing;
    }
    if (config.processing_mode.has_value() &&
        config.processing_mode->send_response_headers) {
      return SideStreamResponseEventState::kExpectHeaders;
    }
    if (config.processing_mode.has_value() &&
        config.processing_mode->send_response_body) {
      return SideStreamResponseEventState::kExpectBodyOrTrailers;
    }
    if (config.processing_mode.has_value() &&
        config.processing_mode->send_response_trailers) {
      return SideStreamResponseEventState::kExpectTrailers;
    }
    return SideStreamResponseEventState::kExpectNothing;
  }

  std::string DebugTag() const;

  // Track event states for request and response side-stream messages.
  // Synchronized by the handler_ activity.
  SideStreamRequestEventState request_event_state_;
  SideStreamResponseEventState response_event_state_;

  // Metadata and message handles stored during request/response processing.
  // Synchronized by the handler_ activity.
  ClientMetadataHandle client_initial_metadata_;
  MessageHandle client_message_;
  ServerMetadataHandle server_initial_metadata_;
  MessageHandle server_message_;
  ServerMetadataHandle server_trailing_metadata_;

  // Inter-activity communication mechanisms between initiator_ and handler_.
  InterActivityLatch<std::optional<ServerMetadataHandle>>
      server_initial_metadata_latch_;
  InterActivityPipe<MessageHandle, 1> server_to_client_messages_;
  InterActivityLatch<ServerMetadataHandle> server_trailing_metadata_latch_;

  // Timestamps recorded when events arrive from the data plane, used to
  // measure delay introduced by the external processor in normal mode.
  // Synchronized by the handler_ activity.
  Timestamp client_initial_metadata_start_time_;
  Timestamp client_half_close_start_time_;
  Timestamp server_initial_metadata_start_time_;
  Timestamp server_trailing_metadata_start_time_;

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
  // Latch signaled when the side-stream is closed or drained.
  Latch<void> side_stream_closed_latch_;

  // Send state and waiters for coordinating message sends on the side-stream
  // within the handler_ activity.
  SideStreamSendState ext_proc_send_state_ = SideStreamSendState::kIdle;
  IntraActivityWaiter ext_proc_send_waiter_;

  CallHandler handler_;
  CallInitiator initiator_;
  RefCountedPtr<XdsStreamingCallPromiseWrapper> streaming_call_;
  RefCountedPtr<ExtProcFilter> ext_proc_filter_;
};

ExtProcFilter::ExtProcCall::ExtProcCall(
    RefCountedPtr<ExtProcFilter> ext_proc_filter,
    RefCountedPtr<XdsTransportFactory::XdsTransport> transport,
    CallHandler handler)
    : request_event_state_(InitialRequestEventState(*ext_proc_filter->config_)),
      response_event_state_(
          InitialResponseEventState(*ext_proc_filter->config_)),
      handler_(handler),
      ext_proc_filter_(std::move(ext_proc_filter)) {
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
  return Seq(
      // Wait until send state is kIdle, then mark kSendInFlight.
      [self = WeakRef()]() -> Poll<StatusFlag> {
        if (self->streaming_call_ == nullptr ||
            self->ext_proc_send_state_ == SideStreamSendState::kSendFailed ||
            self->side_stream_closed_latch_.is_set() ||
            self->drain_requested_) {
          return Failure{};
        }
        if (self->ext_proc_send_state_ != SideStreamSendState::kIdle) {
          return self->ext_proc_send_waiter_.pending();
        }
        self->ext_proc_send_state_ = SideStreamSendState::kSendInFlight;
        return Success{};
      },
      // Safely acquire streaming_call_ and push the payload.
      [self = WeakRef(),
       payload = std::move(payload)](StatusFlag status) mutable {
        return If(
            !status.ok() || self->streaming_call_ == nullptr ||
                self->ext_proc_send_state_ ==
                    SideStreamSendState::kSendFailed ||
                self->side_stream_closed_latch_.is_set() ||
                self->drain_requested_,
            Immediate(StatusFlag(Success{})),
            [self, payload = std::move(payload)]() mutable {
              return self->streaming_call_->PushMessage(std::move(payload));
            });
      },
      // Reset send state and wake up any waiting senders.
      [self = WeakRef()](StatusFlag status) -> StatusFlag {
        self->ext_proc_send_state_ =
            status.ok() && !self->side_stream_closed_latch_.is_set()
                ? SideStreamSendState::kIdle
                : SideStreamSendState::kSendFailed;
        self->ext_proc_send_waiter_.Wake();
        return Success{};
      });
}

// Spawns the read-from-server loop on initiator_.
// Called from StartChildCall().
//
// Pulls server initial metadata, messages, and trailing metadata from
// downstream and immediately forwards them across inter-activity mechanisms
// to the handler_ activity.
void ExtProcFilter::ExtProcCall::SpawnReadFromServerLoop() {
  initiator_.SpawnInfallible("read_from_server", [self = WeakRef()]() {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << self->DebugTag() << "read_from_server task started";
    return Seq(
        self->initiator_.CancelIfFails(TrySeq(
            self->initiator_.PullServerInitialMetadata(),
            [self](std::optional<ServerMetadataHandle> metadata) {
              const bool has_md = metadata.has_value();
              self->server_initial_metadata_latch_.Set(std::move(metadata));
              return Seq(
                  If(
                      has_md,
                      [self]() {
                        return ForEach(
                            MessagesFrom(self->initiator_),
                            [self](MessageHandle message) {
                              return Map(
                                  self->server_to_client_messages_.sender.Push(
                                      std::move(message)),
                                  [](bool x) { return StatusFlag(x); });
                            });
                      },
                      Immediate(StatusFlag(Success{}))),
                  [self](StatusFlag status) {
                    self->server_to_client_messages_.sender.MarkClosed();
                    return status;
                  });
            })),
        self->initiator_.PullServerTrailingMetadata(),
        [self](ServerMetadataHandle metadata) {
          self->server_trailing_metadata_latch_.Set(std::move(metadata));
        });
  });
}

void ExtProcFilter::ExtProcCall::StartChildCall(ClientMetadataHandle metadata) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Starting downstream child call";
  initiator_ = ext_proc_filter_->MakeChildCall(std::move(metadata),
                                               handler_.arena()->Ref());
  handler_.AddChildCall(initiator_);
  ext_proc_filter_->RecordClientHeadersDuration(
      (Timestamp::Now() - client_initial_metadata_start_time_).seconds());
  SpawnReadFromServerLoop();
}

//
// Read-from-sidestream Event Handlers
//

StatusFlag
ExtProcFilter::ExtProcCall::HandleClientInitialMetadataFromSidestream(
    const ExtProcResponse::RequestHeaders& response) {
  if (request_event_state_ != SideStreamRequestEventState::kExpectHeaders) {
    CancelCallWithError(
        absl::InternalError("Received unexpected request headers response from "
                            "external processor"));
    return Failure{};
  }
  request_event_state_ = processing_mode().send_request_body
                             ? SideStreamRequestEventState::kExpectBody
                             : SideStreamRequestEventState::kExpectNothing;
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
  if (request_event_state_ != SideStreamRequestEventState::kExpectBody) {
    CancelCallWithError(absl::InternalError(
        "Received unexpected request body response from external processor"));
    return Failure{};
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Parsed request body response, eos: "
      << response.mutation.end_of_stream << ", eos_without_msg: "
      << response.mutation.end_of_stream_without_message;
  // Handle message, if any.
  if (!response.mutation.end_of_stream ||
      !response.mutation.end_of_stream_without_message) {
    // TODO(rishesh): Remove this check when we stop using the
    // v3-to-v1 adaptor layer.
    if (outstanding_c2s_messages_ == 0) {
      CancelCallWithError(absl::InternalError(
          "Received unexpected request body response from external processor"));
      return Failure{};
    }
    --outstanding_c2s_messages_;
    auto slice = Slice::FromCopiedString(response.mutation.body);
    auto new_msg = initiator_.arena()->MakePooled<Message>(
        SliceBuffer(std::move(slice)), /*flags=*/0);
    // TODO(rishesh, roth): Spawning this push into the activity means that we
    // won't have flow control feedback here in a pure v3 stack, so we need to
    // fix it before we finish the v3 migration.
    initiator_.SpawnPushMessage(std::move(new_msg));
  }
  // Handle EOS.
  if (response.mutation.end_of_stream) {
    // TODO(rishesh): Remove this check when we stop using the
    // v3-to-v1 adaptor layer.
    if (outstanding_c2s_messages_ > 0 ||
        (response.mutation.end_of_stream_without_message &&
         !c2s_writes_done_)) {
      CancelCallWithError(
          absl::InternalError("Client sends closed by external processor"));
      return Failure{};
    }
    request_event_state_ = SideStreamRequestEventState::kExpectNothing;
    initiator_.SpawnFinishSends();
    ext_proc_filter_->RecordClientHalfCloseDuration(
        (Timestamp::Now() - client_half_close_start_time_).seconds());
  }
  return Success{};
}

StatusFlag
ExtProcFilter::ExtProcCall::HandleServerInitialMetadataFromSidestream(
    const ExtProcResponse::ResponseHeaders& response) {
  if (response_event_state_ != SideStreamResponseEventState::kExpectHeaders) {
    CancelCallWithError(absl::InternalError(
        "Received unexpected response headers response from external "
        "processor"));
    return Failure{};
  }
  if (is_trailers_only_) {
    response_event_state_ = SideStreamResponseEventState::kExpectNothing;
  } else if (processing_mode().send_response_body) {
    response_event_state_ = SideStreamResponseEventState::kExpectBodyOrTrailers;
  } else if (processing_mode().send_response_trailers) {
    response_event_state_ = SideStreamResponseEventState::kExpectTrailers;
  } else {
    response_event_state_ = SideStreamResponseEventState::kExpectNothing;
  }
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
    handler_.SpawnPushServerTrailingMetadata(
        std::move(server_trailing_metadata_));
    ext_proc_filter_->RecordServerTrailersDuration(
        (Timestamp::Now() - server_trailing_metadata_start_time_).seconds());
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
  handler_.SpawnPushServerInitialMetadata(std::move(server_initial_metadata_));
  ext_proc_filter_->RecordServerHeadersDuration(
      (Timestamp::Now() - server_initial_metadata_start_time_).seconds());
  return Success{};
}

StatusFlag ExtProcFilter::ExtProcCall::HandleServerMessageFromSidestream(
    const ExtProcResponse::ResponseBody& response) {
  if (response_event_state_ !=
      SideStreamResponseEventState::kExpectBodyOrTrailers) {
    CancelCallWithError(absl::InternalError(
        "Received unexpected response body response from external processor"));
    return Failure{};
  }
  // TODO(rishesh): Remove this check when we stop using the v3-to-v1 bridge.
  // We need this check in the short term because of the restriction that the
  // ext_proc server cannot change the number of messages on the stream.
  // However, once that restriction is removed, this check will no longer be
  // correct: even if the server sends a trailers-only response, the ext_proc
  // server should still be allowed to insert its own headers and messages
  // before sending status.
  if (is_trailers_only_) {
    CancelCallWithError(absl::InternalError(
        "Received response body response in a Trailers-Only call"));
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
  // won't have flow control feedback here in a pure v3 stack, so we need to fix
  // it before we finish the v3 migration.
  handler_.SpawnPushMessage(std::move(new_msg));
  return Success{};
}

StatusFlag
ExtProcFilter::ExtProcCall::HandleServerTrailingMetadataFromSidestream(
    const ExtProcResponse::ResponseTrailers& response) {
  if (response_event_state_ !=
          SideStreamResponseEventState::kExpectBodyOrTrailers &&
      response_event_state_ != SideStreamResponseEventState::kExpectTrailers) {
    CancelCallWithError(absl::InternalError(
        "Received unexpected response trailers response from external "
        "processor"));
    return Failure{};
  }
  if (outstanding_s2c_messages_ > 0) {
    CancelCallWithError(absl::InternalError(
        "Received response trailers response before all outstanding "
        "response body responses were received"));
    return Failure{};
  }
  response_event_state_ = SideStreamResponseEventState::kExpectNothing;
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
  handler_.SpawnPushServerTrailingMetadata(
      std::move(server_trailing_metadata_));
  ext_proc_filter_->RecordServerTrailersDuration(
      (Timestamp::Now() - server_trailing_metadata_start_time_).seconds());
  return Success{};
}

StatusFlag ExtProcFilter::ExtProcCall::HandleImmediateResponseFromSidestream(
    const ExtProcResponse::ImmediateResponse& response) {
  if (config().disable_immediate_response) {
    CancelCallWithError(absl::InternalError(
        "unhandled immediate response due to config disabled it"));
    return Failure{};
  }
  request_event_state_ = SideStreamRequestEventState::kExpectNothing;
  response_event_state_ = SideStreamResponseEventState::kExpectNothing;
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
    HandleSideStreamStatus(parsed_response.status());
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
  if (request_event_state_ == SideStreamRequestEventState::kExpectHeaders &&
      client_initial_metadata_ != nullptr) {
    (void)HandleClientInitialMetadataFromSidestream(
        ExtProcResponse::RequestHeaders{});
  }
  if (response_event_state_ == SideStreamResponseEventState::kExpectHeaders &&
      ((!is_trailers_only_ && server_initial_metadata_ != nullptr) ||
       (is_trailers_only_ && server_trailing_metadata_ != nullptr))) {
    (void)HandleServerInitialMetadataFromSidestream(
        ExtProcResponse::ResponseHeaders{});
  }
  if ((response_event_state_ ==
           SideStreamResponseEventState::kExpectBodyOrTrailers ||
       response_event_state_ ==
           SideStreamResponseEventState::kExpectTrailers) &&
      !is_trailers_only_ && server_trailing_metadata_ != nullptr) {
    (void)HandleServerTrailingMetadataFromSidestream(
        ExtProcResponse::ResponseTrailers{});
  }
  if (!side_stream_closed_latch_.is_set()) {
    side_stream_closed_latch_.Set();
  }
}

//
// Read-from-client Event Handlers
//

auto ExtProcFilter::ExtProcCall::HandleInitialMetadataFromClient(
    ClientMetadataHandle metadata) {
  client_initial_metadata_start_time_ = Timestamp::Now();
  client_initial_metadata_ = std::move(metadata);
  absl::StatusOr<std::string> payload = "";
  if (processing_mode().send_request_headers) {
    // Construct ext_proc request for client initial metadata.
    // Include processing mode in the request if this is the first message
    // on the stream.
    std::optional<ExtProcProcessingMode> processing_mode;
    if (IsFirstMessageOnSideStream()) {
      processing_mode = config().processing_mode;
    }
    upb::Arena arena;
    auto* header_attributes = CreateExtProcAttributesProtoStruct(
        arena.ptr(), config().request_attributes, *client_initial_metadata_,
        ext_proc_filter_->default_authority_.as_string_view());
    payload = CreateExtProcClientHeadersRequest(
        arena.ptr(), client_initial_metadata_.get(),
        config().forwarding_allowed_headers,
        config().forwarding_disallowed_headers, header_attributes,
        config().observability_mode, processing_mode);
  }
  // If request body will be sent later and request attributes are
  // configured, extract initial attributes from client metadata.
  else if (processing_mode().send_request_body &&
           !config().request_attributes.empty()) {
    request_attributes_ = CreateExtProcAttributesProtoStruct(
        request_attributes_arena_.ptr(), config().request_attributes,
        *client_initial_metadata_,
        ext_proc_filter_->default_authority_.as_string_view());
  }
  // Grab status so that we can std::move(payload) for the lambda below.
  absl::Status error = payload.status();
  return If(
      !error.ok(),
      // Failed to construct sidestream message.
      [self = WeakRef(), error]() {
        self->HandleSideStreamStatus(error);
        return Immediate(StatusFlag(self->IsFailOpenAllowed()));
      },
      // Either did not attempt to construct sidestream message,
      // or successfully constructed it.
      [self = WeakRef(), payload = std::move(payload)]() mutable {
        const bool send_request_headers =
            self->processing_mode().send_request_headers;
        if (!send_request_headers || self->config().observability_mode) {
          self->StartChildCall(std::move(self->client_initial_metadata_));
        }
        return If(
            send_request_headers,
            [self, payload = std::move(*payload)]() mutable {
              // Send the serialized request payload over the side-stream.
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << self->DebugTag()
                  << "Sending client initial metadata to sidestream";
              return self->SendMessageToSideStream(std::move(payload));
            },
            [self]() {
              GRPC_TRACE_LOG(ext_proc_filter, INFO)
                  << self->DebugTag()
                  << "Skipping client initial metadata (processing mode "
                     "disabled)";
              return Immediate(StatusFlag(Success{}));
            });
      });
}

auto ExtProcFilter::ExtProcCall::HandleMessageFromClient(
    MessageHandle message) {
  client_message_ = std::move(message);
  const bool send_request_body = processing_mode().send_request_body &&
                                 !side_stream_closed_latch_.is_set();
  absl::StatusOr<std::string> payload = "";
  if (!send_request_body) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag()
        << "Client message non-processing mode (processing disabled or "
           "closed)";
  } else if (!config().observability_mode &&
             request_event_state_ ==
                 SideStreamRequestEventState::kExpectNothing) {
    // TODO(rishesh): If the external processor has already closed client
    // sends (via end_of_stream or end_of_stream_without_message in
    // ProcessingResponse), any subsequent message from the client cannot be
    // processed. Since message dropping is not yet supported in Call v3,
    // fail the call here. Remove this once PH2 is implemented.
    payload = absl::InternalError("Client sends closed by external processor");
  } else if (!drain_requested_) {
    // Construct message for sidestream.
    std::string message_bytes;
    if (client_message_ != nullptr) {
      message_bytes = client_message_->payload()->JoinIntoString();
    }
    if (!config().observability_mode) {
      ++outstanding_c2s_messages_;
    }
    std::optional<ExtProcProcessingMode> processing_mode;
    if (IsFirstMessageOnSideStream()) {
      processing_mode = config().processing_mode;
    }
    upb::Arena arena;
    payload = CreateExtProcClientBodyRequest(
        arena.ptr(), message_bytes, request_attributes_,
        config().observability_mode, processing_mode,
        /*end_of_stream=*/false,
        /*end_of_stream_without_message=*/false);
    request_attributes_ = nullptr;
  }
  // Grab status so that we can std::move(payload) for the lambda below.
  absl::Status error = payload.status();
  return If(
      !error.ok(),
      [self = WeakRef(), error]() {
        self->HandleSideStreamStatus(error);
        if (self->IsFailOpenAllowed()) {
          // TODO(rishesh, roth): Spawning this push into the activity means
          // that we won't have flow control feedback here in a pure v3
          // stack, so we need to fix it before we finish the v3 migration.
          self->initiator_.SpawnPushMessage(std::move(self->client_message_));
          return Immediate(StatusFlag(Success{}));
        }
        return Immediate(StatusFlag(Failure{}));
      },
      [self = WeakRef(), send_request_body,
       payload = std::move(payload)]() mutable {
        return If(
            !send_request_body,
            [self]() {
              // TODO(rishesh, roth): Spawning this push into the activity means
              // that we won't have flow control feedback here in a pure v3
              // stack, so we need to fix it before we finish the v3 migration.
              self->initiator_.SpawnPushMessage(
                  std::move(self->client_message_));
              return Immediate(StatusFlag(Success{}));
            },
            [self, payload = std::move(payload)]() mutable {
              return If(
                  self->drain_requested_,
                  [self]() {
                    return TrySeq(self->side_stream_closed_latch_.Wait(),
                                  [self]() mutable -> StatusFlag {
                                    // TODO(rishesh, roth): Spawning this push
                                    // into the activity means that we won't
                                    // have flow control feedback here in a
                                    // pure v3 stack, so we need to fix it
                                    // before we finish the v3 migration.
                                    self->initiator_.SpawnPushMessage(
                                        std::move(self->client_message_));
                                    return Success{};
                                  });
                  },
                  [self, payload = std::move(payload)]() mutable {
                    self->first_body_message_sent_ = true;
                    if (self->config().observability_mode) {
                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                          << self->DebugTag()
                          << "Client message observability mode";
                      // TODO(rishesh, roth): Spawning this push into the
                      // activity means that we won't have flow control feedback
                      // here in a pure v3 stack, so we need to fix it before we
                      // finish the v3 migration.
                      self->initiator_.SpawnPushMessage(
                          std::move(self->client_message_));
                    }
                    return self->SendMessageToSideStream(std::move(*payload));
                  });
            });
      });
}

auto ExtProcFilter::ExtProcCall::HandleHalfCloseFromClient() {
  client_half_close_start_time_ = Timestamp::Now();
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "HandleHalfCloseFromClient invoked";
  c2s_writes_done_ = true;
  const bool send_request_body = processing_mode().send_request_body &&
                                 !side_stream_closed_latch_.is_set();
  absl::StatusOr<std::string> payload = "";
  const bool send_to_sidestream =
      send_request_body &&
      (config().observability_mode ||
       request_event_state_ != SideStreamRequestEventState::kExpectNothing) &&
      !drain_requested_;
  if (send_to_sidestream) {
    if (!config().observability_mode) {
      ++outstanding_c2s_messages_;
    }
    std::optional<ExtProcProcessingMode> processing_mode;
    if (IsFirstMessageOnSideStream()) {
      processing_mode = config().processing_mode;
    }
    upb::Arena arena;
    payload = CreateExtProcClientBodyRequest(
        arena.ptr(), /*body=*/"", request_attributes_,
        config().observability_mode, processing_mode,
        /*end_of_stream=*/true,
        /*end_of_stream_without_message=*/true);
    request_attributes_ = nullptr;
  }
  absl::Status error = payload.status();
  return If(
      !error.ok(),
      [self = WeakRef(), error]() {
        self->HandleSideStreamStatus(error);
        if (self->IsFailOpenAllowed()) {
          self->initiator_.SpawnFinishSends();
          self->ext_proc_filter_->RecordClientHalfCloseDuration(
              (Timestamp::Now() - self->client_half_close_start_time_)
                  .seconds());
          return Immediate(StatusFlag(Success{}));
        }
        return Immediate(StatusFlag(Failure{}));
      },
      [self = WeakRef(), send_to_sidestream,
       payload = std::move(payload)]() mutable {
        return If(
            send_to_sidestream,
            [self, payload = std::move(*payload)]() mutable {
              self->first_body_message_sent_ = true;
              if (self->config().observability_mode) {
                self->initiator_.SpawnFinishSends();
                self->ext_proc_filter_->RecordClientHalfCloseDuration(
                    (Timestamp::Now() - self->client_half_close_start_time_)
                        .seconds());
              }
              return self->SendMessageToSideStream(std::move(payload));
            },
            [self]() {
              return If(
                  self->drain_requested_,
                  [self]() {
                    return TrySeq(
                        self->side_stream_closed_latch_.Wait(),
                        [self]() mutable -> StatusFlag {
                          self->initiator_.SpawnFinishSends();
                          self->ext_proc_filter_->RecordClientHalfCloseDuration(
                              (Timestamp::Now() -
                               self->client_half_close_start_time_)
                                  .seconds());
                          return Success{};
                        });
                  },
                  [self]() {
                    self->initiator_.SpawnFinishSends();
                    self->ext_proc_filter_->RecordClientHalfCloseDuration(
                        (Timestamp::Now() - self->client_half_close_start_time_)
                            .seconds());
                    return Immediate(StatusFlag(Success{}));
                  });
            });
      });
}

//
// Read-from-server Event Handlers
//

auto ExtProcFilter::ExtProcCall::HandleInitialMetadataFromServer(
    std::optional<ServerMetadataHandle> metadata) {
  server_initial_metadata_start_time_ = Timestamp::Now();
  const bool is_trailers_only = !metadata.has_value();
  if (is_trailers_only) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "No server initial metadata (trailers-only response)";
    is_trailers_only_ = true;
  } else {
    server_initial_metadata_ = std::move(*metadata);
  }
  const bool send_response_headers =
      !is_trailers_only && processing_mode().send_response_headers &&
      !side_stream_closed_latch_.is_set() && !drain_requested_;
  absl::StatusOr<std::string> payload = "";
  if (send_response_headers) {
    // Include processing mode if this is the first message on the
    // stream.
    std::optional<ExtProcProcessingMode> processing_mode;
    if (IsFirstMessageOnSideStream()) {
      processing_mode = config().processing_mode;
    }
    upb::Arena arena;
    payload = CreateExtProcServerHeadersRequest(
        arena.ptr(), server_initial_metadata_.get(),
        config().forwarding_allowed_headers,
        config().forwarding_disallowed_headers,
        /*attributes=*/nullptr, config().observability_mode, processing_mode,
        /*end_of_stream=*/false);
  }
  absl::Status error = payload.status();
  return If(
      !error.ok(),
      [self = WeakRef(), error]() {
        self->HandleSideStreamStatus(error);
        return Immediate(StatusFlag(self->IsFailOpenAllowed()));
      },
      [self = WeakRef(), is_trailers_only, send_response_headers,
       payload = std::move(payload)]() mutable {
        return If(
            is_trailers_only, Immediate(StatusFlag(Success{})),
            [self, send_response_headers,
             payload = std::move(payload)]() mutable {
              if (!send_response_headers || self->config().observability_mode) {
                self->handler_.PushServerInitialMetadata(
                    std::move(self->server_initial_metadata_));
                self->ext_proc_filter_->RecordServerHeadersDuration(
                    (Timestamp::Now() -
                     self->server_initial_metadata_start_time_)
                        .seconds());
              }
              return If(
                  send_response_headers,
                  [self, payload = std::move(payload)]() mutable {
                    if (self->config().observability_mode) {
                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                          << self->DebugTag()
                          << "Sending server initial metadata (observability "
                             "mode)";
                    } else {
                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                          << self->DebugTag()
                          << "Sending server initial metadata (normal mode)";
                    }
                    return self->SendMessageToSideStream(std::move(*payload));
                  },
                  [self]() {
                    GRPC_TRACE_LOG(ext_proc_filter, INFO)
                        << self->DebugTag()
                        << "Skipping server initial metadata (processing "
                           "disabled, stream closed, or drain mode)";
                    return Immediate(StatusFlag(Success{}));
                  });
            });
      });
}

auto ExtProcFilter::ExtProcCall::HandleTrailingMetadataFromServer(
    ServerMetadataHandle metadata) {
  server_trailing_metadata_start_time_ = Timestamp::Now();
  server_trailing_metadata_ = std::move(metadata);
  const bool send_metadata =
      IsStatusOk(*server_trailing_metadata_) &&
      !side_stream_closed_latch_.is_set() &&
      (is_trailers_only_ ? processing_mode().send_response_headers
                         : processing_mode().send_response_trailers);
  const bool send_to_sidestream = send_metadata && !drain_requested_;
  absl::StatusOr<std::string> payload = "";
  if (send_to_sidestream) {
    // Include processing mode if this is the first message on
    // the stream.
    std::optional<ExtProcProcessingMode> processing_mode;
    if (IsFirstMessageOnSideStream()) {
      processing_mode = config().processing_mode;
    }
    upb::Arena arena;
    payload = is_trailers_only_
                  ? CreateExtProcServerHeadersRequest(
                        arena.ptr(), server_trailing_metadata_.get(),
                        config().forwarding_allowed_headers,
                        config().forwarding_disallowed_headers,
                        /*attributes=*/nullptr, config().observability_mode,
                        processing_mode, /*end_of_stream=*/true)
                  : CreateExtProcServerTrailersRequest(
                        arena.ptr(), server_trailing_metadata_.get(),
                        config().forwarding_allowed_headers,
                        config().forwarding_disallowed_headers,
                        /*attributes=*/nullptr, config().observability_mode,
                        processing_mode);
  }
  absl::Status error = payload.status();
  return If(
      !error.ok(),
      [self = WeakRef(), error]() {
        self->HandleSideStreamStatus(error);
        return Immediate(StatusFlag(self->IsFailOpenAllowed()));
      },
      [self = WeakRef(), send_to_sidestream,
       payload = std::move(payload)]() mutable {
        return If(
            send_to_sidestream,
            [self, payload = std::move(*payload)]() mutable {
              if (self->config().observability_mode) {
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << self->DebugTag()
                    << "Sending server trailing metadata (observability mode)";
                self->handler_.PushServerTrailingMetadata(
                    std::move(self->server_trailing_metadata_));
                self->ext_proc_filter_->RecordServerTrailersDuration(
                    (Timestamp::Now() -
                     self->server_trailing_metadata_start_time_)
                        .seconds());
              } else {
                GRPC_TRACE_LOG(ext_proc_filter, INFO)
                    << self->DebugTag()
                    << "Sending server trailing metadata (normal mode)";
              }
              return self->SendMessageToSideStream(std::move(payload));
            },
            [self]() {
              return If(
                  self->drain_requested_,
                  [self]() {
                    GRPC_TRACE_LOG(ext_proc_filter, INFO)
                        << self->DebugTag()
                        << "Handling server trailing metadata in drain mode";
                    return TrySeq(
                        self->side_stream_closed_latch_.Wait(),
                        [self]() mutable -> StatusFlag {
                          self->handler_.PushServerTrailingMetadata(
                              std::move(self->server_trailing_metadata_));
                          self->ext_proc_filter_->RecordServerTrailersDuration(
                              (Timestamp::Now() -
                               self->server_trailing_metadata_start_time_)
                                  .seconds());
                          return Success{};
                        });
                  },
                  [self]() {
                    GRPC_TRACE_LOG(ext_proc_filter, INFO)
                        << self->DebugTag()
                        << "Skipping server trailing metadata (processing "
                           "disabled or stream closed)";
                    self->handler_.PushServerTrailingMetadata(
                        std::move(self->server_trailing_metadata_));
                    self->ext_proc_filter_->RecordServerTrailersDuration(
                        (Timestamp::Now() -
                         self->server_trailing_metadata_start_time_)
                            .seconds());
                    return Immediate(StatusFlag(Success{}));
                  });
            });
      });
}

//
// ExtProcFilter::ExtProcCall Server Message Processing
//

auto ExtProcFilter::ExtProcCall::HandleMessageFromServer(
    MessageHandle message) {
  server_message_ = std::move(message);
  const bool send_body = processing_mode().send_response_body &&
                         !side_stream_closed_latch_.is_set();
  absl::StatusOr<std::string> payload = "";
  if (!send_body) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "Server message non-processing mode";
  } else if (!drain_requested_) {
    // Construct message for sidestream.
    std::string message_bytes;
    if (server_message_ != nullptr) {
      message_bytes = server_message_->payload()->JoinIntoString();
    }
    if (!config().observability_mode) {
      ++outstanding_s2c_messages_;
    }
    std::optional<ExtProcProcessingMode> processing_mode;
    if (IsFirstMessageOnSideStream()) {
      processing_mode = config().processing_mode;
    }
    upb::Arena arena;
    payload = CreateExtProcServerBodyRequest(
        arena.ptr(), message_bytes, /*attributes=*/nullptr,
        config().observability_mode, processing_mode);
  }
  absl::Status error = payload.status();
  return If(
      !error.ok(),
      [self = WeakRef(), error]() {
        self->HandleSideStreamStatus(error);
        if (self->IsFailOpenAllowed()) {
          // TODO(rishesh, roth): Spawning this push into the activity means
          // that we won't have flow control feedback here in a pure v3
          // stack, so we need to fix it before we finish the v3 migration.
          self->handler_.SpawnPushMessage(std::move(self->server_message_));
          return Immediate(StatusFlag(Success{}));
        }
        return Immediate(StatusFlag(Failure{}));
      },
      [self = WeakRef(), send_body, payload = std::move(payload)]() mutable {
        return If(
            !send_body,
            [self]() {
              // TODO(rishesh, roth): Spawning this push into the activity means
              // that we won't have flow control feedback here in a pure v3
              // stack, so we need to fix it before we finish the v3 migration.
              self->handler_.SpawnPushMessage(std::move(self->server_message_));
              return Immediate(StatusFlag(Success{}));
            },
            [self, payload = std::move(payload)]() mutable {
              return If(
                  self->drain_requested_,
                  [self]() {
                    return TrySeq(self->side_stream_closed_latch_.Wait(),
                                  [self]() mutable -> StatusFlag {
                                    // TODO(rishesh, roth): Spawning this push
                                    // into the activity means that we won't
                                    // have flow control feedback here in a
                                    // pure v3 stack, so we need to fix it
                                    // before we finish the v3 migration.
                                    self->handler_.SpawnPushMessage(
                                        std::move(self->server_message_));
                                    return Success{};
                                  });
                  },
                  [self, payload = std::move(payload)]() mutable {
                    self->first_body_message_sent_ = true;
                    if (self->config().observability_mode) {
                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                          << self->DebugTag()
                          << "Server message observability mode";
                      // TODO(rishesh, roth): Spawning this push into the
                      // activity means that we won't have flow control feedback
                      // here in a pure v3 stack, so we need to fix it before we
                      // finish the v3 migration.
                      self->handler_.SpawnPushMessage(
                          std::move(self->server_message_));
                    } else {
                      self->server_message_.reset();
                    }
                    return self->SendMessageToSideStream(std::move(*payload));
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
auto ExtProcFilter::ExtProcCall::HandleReadFromServerActivityLoop() {
  return Seq(
      TrySeq(server_initial_metadata_latch_.Wait(),
             [self = WeakRef()](std::optional<ServerMetadataHandle> metadata) {
               const bool has_md = metadata.has_value();
               return TrySeq(
                   self->HandleInitialMetadataFromServer(std::move(metadata)),
                   [self, has_md]() {
                     return If(
                         has_md,
                         [self]() {
                           return ForEach(
                               std::move(
                                   self->server_to_client_messages_.receiver),
                               [self](MessageHandle message) {
                                 return self->HandleMessageFromServer(
                                     std::move(message));
                               });
                         },
                         []() -> StatusFlag { return Success{}; });
                   });
             }),
      server_trailing_metadata_latch_.Wait(),
      [self = WeakRef()](ServerMetadataHandle metadata) {
        return self->HandleTrailingMetadataFromServer(std::move(metadata));
      });
}

// Continuously pulls response messages from the external processor side-stream
// and dispatches them until the stream closes or an error occurs.
auto ExtProcFilter::ExtProcCall::HandleReadFromSideStreamLoop() {
  return Seq(
      // Loop reading response messages from the side-stream until end-of-stream
      // or error.
      Loop([self = WeakRef()]() -> Promise<LoopCtl<StatusFlag>> {
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
  return Map(TryJoin<absl::StatusOr>(HandleReadFromClientLoop(),
                                     HandleReadFromServerActivityLoop(),
                                     HandleReadFromSideStreamLoop()),
             [](const auto& res) { return res.status(); });
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
      telemetry_storage_([&]() -> InstrumentStorageRefPtr<TelemetryDomain> {
        auto stats_plugin_group =
            args.GetObjectRef<GlobalStatsPluginRegistry::StatsPluginGroup>();
        if (stats_plugin_group == nullptr) return nullptr;
        auto scope = stats_plugin_group->GetCollectionScope();
        if (scope == nullptr) return nullptr;
        return TelemetryDomain::GetStorage(
            std::move(scope), args.GetString(GRPC_ARG_SERVER_URI).value_or(""));
      }()) {}

ExtProcFilter::~ExtProcFilter() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcFilter " << this << " destroyed";
}

void ExtProcFilter::RecordClientHeadersDuration(double duration_seconds) const {
  if (telemetry_storage_ != nullptr) {
    telemetry_storage_->Increment(TelemetryDomain::kClientHeadersDuration,
                                  static_cast<int64_t>(duration_seconds));
  }
}

void ExtProcFilter::RecordClientHalfCloseDuration(
    double duration_seconds) const {
  if (telemetry_storage_ != nullptr) {
    telemetry_storage_->Increment(TelemetryDomain::kClientHalfCloseDuration,
                                  static_cast<int64_t>(duration_seconds));
  }
}

void ExtProcFilter::RecordServerHeadersDuration(double duration_seconds) const {
  if (telemetry_storage_ != nullptr) {
    telemetry_storage_->Increment(TelemetryDomain::kServerHeadersDuration,
                                  static_cast<int64_t>(duration_seconds));
  }
}

void ExtProcFilter::RecordServerTrailersDuration(
    double duration_seconds) const {
  if (telemetry_storage_ != nullptr) {
    telemetry_storage_->Increment(TelemetryDomain::kServerTrailersDuration,
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
