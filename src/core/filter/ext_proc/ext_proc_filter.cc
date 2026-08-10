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

#include <atomic>
#include <cstdint>
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
#include "src/core/lib/promise/inter_activity_latch.h"
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
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/streaming_call_promise_wrapper.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/base/thread_annotations.h"
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

// High-Level Architecture of 3 Concurrent Pipeline Loops across 2 Activities:
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
//  LOOP 2: Side-Stream Pull Pipeline Loop [HandleReadFromSideStreamLoop()]
//  +-----------------------------------------------------------------------+
//  | Seq(                                                                  |
//  |     Loop: streaming_call_->PullMessage()                              |
//  |       -> ProcessSideStreamResponse(),                                 |
//  |     streaming_call_->PullServerTrailingMetadata(),                    |
//  |     HandleSideStreamStatus(status))                                   |
//  +-----------------------------------------------------------------------+
//
//  [ Activity 2: initiator_ ] (Spawned on child call startup in
//  StartChildCall()) LOOP 3: Read-From-Server Response Pipeline Loop
//  [SpawnReadFromServerLoop()]
//  +-----------------------------------------------------------------------+
//  | TrySeq(                                                               |
//  |     initiator_.PullServerInitialMetadata()                            |
//  |       -> HandleInitialMetadataFromServer(),                           |
//  |     Race(                                                             |
//  |         initiator_.PullServerTrailingMetadata()                       |
//  |           -> HandleTrailingMetadataFromServer(),                      |
//  |         ForEach(MessagesFrom(initiator_))                             |
//  |           -> HandleMessageFromServer()))                              |
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
  auto HandleReadFromClientLoop();

  // Handle the read-from-ext_proc-side-stream loop on handler_.
  // Called when the ExtProcCall is created.
  //
  // Handles all events on the call, forwarding data to either handler_
  // (client) or initiator_ (server).
  auto HandleReadFromSideStreamLoop();

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
  void StartChildCall(ClientMetadataHandle metadata);

  // Prepares the ProcessingRequest protobuf message for client body messages.
  ArenaPromise<StatusFlag> SendClientMessageRequest(
      const MessageHandle& message, bool end_of_stream,
      bool end_of_stream_without_message);
  // Prepares the ProcessingRequest protobuf message for server response body
  // and sends it over the ext_proc stream.
  ArenaPromise<StatusFlag> SendServerMessageRequest(
      const MessageHandle& message);

  // Sends a message to the external processor side-stream.
  // Coordinates client-side and server-side message sources so that only one
  // send is in-flight on streaming_call_ at a time, using a single Waker
  // without any queue or vector allocations.
  auto SendMessageToSideStream(std::string payload);

  // Parses and processes an incoming response message payload from the
  // side-stream.
  ArenaPromise<StatusFlag> ProcessSideStreamResponse(absl::string_view payload);

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
    return is_first_message_on_side_stream_.exchange(false,
                                                     std::memory_order_acq_rel);
  }

  bool IsFailOpenAllowed() const {
    const bool allow = config().failure_mode_allow.value_or(false);
    if (config().observability_mode) return allow;
    return allow && !first_body_message_sent_.load(std::memory_order_acquire);
  }

  // Returns true if the external processor side-stream has terminated (cleanly
  // or with error).
  bool IsSideStreamClosed() const { return side_stream_closed_latch_.IsSet(); }

  // Returns true if the side-stream closed with an error and fail-open mode is
  // not permitted for this call (meaning the side-stream failure must fail the
  // data plane RPC).
  bool IsSideStreamFailureFatal() const {
    if (IsFailOpenAllowed()) return false;
    MutexLock lock(&side_stream_mu_);
    return side_stream_status_.has_value() && !side_stream_status_->ok();
  }

  // Evaluates the final status of the side-stream to return for the filter.
  // Respects IsFailOpenAllowed() by returning OkStatus() when fail-open is
  // permitted even if the side-stream failed.
  absl::Status GetSideStreamClosedStatus(
      absl::Status default_error = absl::CancelledError("Side-stream closed")) {
    MutexLock lock(&side_stream_mu_);
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
                 if (self->IsSideStreamFailureFatal()) {
                   return Failure{};
                 }
                 return Success{};
               });
  }

  // Fails the intercepted data plane RPC with the given error status:
  // 1. Pushes error trailing metadata downstream to the client.
  // 2. Cancels any active upstream child call.
  // 3. Records the error status on the side-stream and marks it closed.
  void CancelCallWithError(absl::Status status) {
    MutexLock lock(&side_stream_mu_);
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
    }
  }

  // Idempotently closes the out-of-band side-stream to the external processor.
  // Wakes any pending side-stream senders and resets the side-stream call
  // object.
  void CloseSideStream() {
    if (!IsSideStreamClosed()) {
      MutexLock lock(&side_stream_mu_);
      side_stream_status_ = absl::OkStatus();
      side_stream_closed_latch_.Set();
    }
    auto streaming_call = std::move(streaming_call_);
    MutexLock lock(&ext_proc_send_mu_);
    ext_proc_send_state_ = SendState::kSendFailed;
    ext_proc_send_waiters_.WakeupAsync();
    streaming_call.reset();
  }

  void Orphaned() override { CloseSideStream(); }

  std::string DebugTag() const;

  // Flags tracking whether the respective ext_proc response messages have
  // been received from the external processor side-stream. Synchronized by
  // the handler_ activity.
  bool request_headers_received_ = false;
  bool response_headers_received_ = false;
  bool response_trailers_received_ = false;

  // Client initial metadata stored during request header processing.
  // Synchronized by the handler_ activity.
  ClientMetadataHandle client_initial_metadata_;
  InterActivityLatch<ServerMetadataHandle> server_initial_metadata_latch_;
  InterActivityLatch<ServerMetadataHandle> server_trailing_metadata_latch_;

  // Timestamps recorded when events arrive from the data plane, used to
  // measure delay introduced by the external processor in normal mode.
  Timestamp client_initial_metadata_start_time_ = Timestamp::InfPast();
  Timestamp client_half_close_start_time_ = Timestamp::InfPast();
  std::atomic<int64_t> server_initial_metadata_start_time_millis_{0};
  std::atomic<int64_t> server_trailing_metadata_start_time_millis_{0};

  // Temporary UPB arena holding request attributes until the first client body
  // request is sent to the sidestream. Synchronized by the handler_ activity.
  upb::Arena request_attributes_arena_;
  // Request attributes generated during request header processing to be
  // attached to subsequent request body processing requests. Synchronized by
  // the handler_ activity.
  ::google_protobuf_Struct* request_attributes_ = nullptr;
  // Indicates whether a stream drain operation has been requested by the
  // filter.
  InterActivityLatch<void> drain_requested_latch_;
  // True if no messages have been sent on the external processor side-stream
  // yet. Used to include overall processing_mode in the initial stream header
  // request.
  std::atomic<bool> is_first_message_on_side_stream_{true};
  // Tracks whether the first body message has been sent on the side-stream,
  // used for fail-open determination.
  std::atomic<bool> first_body_message_sent_{false};
  // TODO(rishesh): Need to remove this once PH2 work is done.
  // Number of messages sent to ext_proc that are awaiting response processing
  // in S2C and C2S directions respectively.
  std::atomic<size_t> outstanding_s2c_messages_{0};
  size_t outstanding_c2s_messages_ = 0;
  // Data plane stream state flags tracking directional write completion,
  // half-close, and trailers-only RPC mode.
  bool c2s_writes_done_ = false;
  std::atomic<bool> is_trailers_only_{false};
  // Indicates server trailing metadata was dispatched to side-stream.
  std::atomic<bool> server_trailers_sent_to_side_stream_{false};
  // Set by external processor server when it requests end of client sends
  // (EOS). Synchronized by the handler_ activity.
  bool ext_proc_closed_client_sends_ = false;
  // Indicates that sends on the external processor side-stream have been
  // half-closed.
  std::atomic<bool> side_stream_half_closed_{false};
  // Tracks terminal status of the external processor side-stream.
  mutable Mutex side_stream_mu_;
  std::optional<absl::Status> side_stream_status_
      ABSL_GUARDED_BY(side_stream_mu_);
  // Latch signaled when the side-stream is closed or drained.
  InterActivityLatch<void> side_stream_closed_latch_;

  // Mutex-protected send state and waiters for coordination between
  // client-side and server-side message senders on the side-stream.
  mutable Mutex ext_proc_send_mu_;
  SendState ext_proc_send_state_ ABSL_GUARDED_BY(ext_proc_send_mu_) =
      SendState::kIdle;
  WaitSet ext_proc_send_waiters_ ABSL_GUARDED_BY(ext_proc_send_mu_);

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

ArenaPromise<StatusFlag> ExtProcFilter::ExtProcCall::ProcessSideStreamResponse(
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
    drain_requested_latch_.Set();
    side_stream_half_closed_.store(true, std::memory_order_release);
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
      << DebugTag() << "status received: " << status;
  if (IsSideStreamClosed()) return;
  const bool has_outstanding_messages =
      outstanding_c2s_messages_ > 0 ||
      outstanding_s2c_messages_.load(std::memory_order_relaxed) > 0;
  const bool must_drain =
      !config().observability_mode && (processing_mode().send_request_body ||
                                       processing_mode().send_response_body);
  // Check if a clean stream closure violated draining or message-in-flight
  // requirements.
  if (status.ok()) {
    if (must_drain && !drain_requested_latch_.IsSet()) {
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
  // unblocking latches and forwarding unmutated metadata.
  const bool trailers_only = is_trailers_only_.load(std::memory_order_acquire);
  if (processing_mode().send_request_headers && !request_headers_received_ &&
      client_initial_metadata_ != nullptr) {
    (void)HandleClientInitialMetadataFromSidestream(
        ExtProcResponse::RequestHeaders{});
  }
  if (processing_mode().send_response_headers && !response_headers_received_ &&
      (server_initial_metadata_latch_.IsSet() ||
       (trailers_only && server_trailing_metadata_latch_.IsSet()))) {
    auto promise = HandleServerInitialMetadataFromSidestream(
        ExtProcResponse::ResponseHeaders{});
    (void)promise();
  }
  if (processing_mode().send_response_trailers && !trailers_only &&
      !response_trailers_received_ && server_trailing_metadata_latch_.IsSet()) {
    auto promise = HandleServerTrailingMetadataFromSidestream(
        ExtProcResponse::ResponseTrailers{});
    (void)promise();
  }
  CloseSideStream();
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
        MutexLock lock(&self->ext_proc_send_mu_);
        if (self->ext_proc_send_state_ == SendState::kSendFailed) {
          return Failure{};
        }
        if (self->ext_proc_send_state_ != SendState::kIdle) {
          self->ext_proc_send_waiters_.AddPending(
              GetContext<Activity>()->MakeNonOwningWaker());
          return Pending{};
        }
        self->ext_proc_send_state_ = SendState::kSendInFlight;
        return Success{};
      },
      // Safely acquire streaming_call_ and push the payload.
      [self = WeakRef(), payload = std::move(payload)](
          StatusFlag status) mutable -> ArenaPromise<StatusFlag> {
        if (!status.ok()) {
          return Immediate(StatusFlag(Failure{}));
        }
        // CloseSideStream() moves out and resets streaming_call_, so it may be
        // null if the side-stream closed while this send was queued or
        // executing.
        if (self->streaming_call_ == nullptr) {
          return Immediate(StatusFlag(Failure{}));
        }
        return self->streaming_call_->PushMessage(std::move(payload));
      },
      // Reset send state and wake up any waiting senders.
      [self = WeakRef()](StatusFlag status) {
        MutexLock lock(&self->ext_proc_send_mu_);
        self->ext_proc_send_state_ =
            status.ok() ? SendState::kIdle : SendState::kSendFailed;
        self->ext_proc_send_waiters_.WakeupAsync();
        return Success{};
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

// Handles the response path (Server to Client).
auto ExtProcFilter::ExtProcCall::Run() {
  return Seq(TryJoin<absl::StatusOr>(HandleReadFromClientLoop(),
                                     HandleReadFromSideStreamLoop()),
             [self = Ref()](
                 absl::StatusOr<std::tuple<Empty, Empty>> res) -> absl::Status {
               GRPC_TRACE_LOG(ext_proc_filter, INFO)
                   << self->DebugTag()
                   << "Run() finished with status: " << res.ok();
               return self->GetSideStreamClosedStatus();
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
  initiator_.SpawnGuarded("read_from_server", [self = WeakRef()]() {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << self->DebugTag() << "read_from_server task started";
    return TrySeq(
        self->initiator_.PullServerInitialMetadata(),
        [self](std::optional<ServerMetadataHandle> metadata) {
          return self->HandleInitialMetadataFromServer(std::move(metadata));
        },
        [self]() {
          return Race(Seq(self->initiator_.PullServerTrailingMetadata(),
                          [self](ServerMetadataHandle metadata) {
                            return self->HandleTrailingMetadataFromServer(
                                std::move(metadata));
                          }),
                      Seq(ForEach(MessagesFrom(self->initiator_),
                                  [self](MessageHandle message) {
                                    return self->HandleMessageFromServer(
                                        std::move(message));
                                  }),
                          [](StatusFlag status) -> ArenaPromise<StatusFlag> {
                            if (!status.ok()) {
                              return Immediate(status);
                            }
                            return Never<StatusFlag>();
                          }));
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
        << DebugTag()
        << "Skipping client initial metadata (processing mode disabled)";
    // If request body will be sent later and request attributes are
    // configured, extract initial attributes from client metadata.
    if (processing_mode().send_request_body &&
        !config().request_attributes.empty()) {
      request_attributes_ = CreateExtProcAttributesProtoStruct(
          request_attributes_arena_.ptr(), config().request_attributes,
          *metadata, ext_proc_filter_->default_authority_.as_string_view());
    }
    // Directly start downstream child call with unmodified client metadata.
    StartChildCall(std::move(metadata));
    return Immediate(StatusFlag(Success{}));
  }
  // Construct ext_proc request for client initial metadata.
  // Include processing mode in the request if this is the first message on the
  // stream.
  std::optional<ExtProcProcessingMode> processing_mode;
  if (IsFirstMessageOnSideStream()) {
    processing_mode = config().processing_mode;
  }
  upb::Arena arena;
  auto* header_attributes = CreateExtProcAttributesProtoStruct(
      arena.ptr(), config().request_attributes, *metadata,
      ext_proc_filter_->default_authority_.as_string_view());
  auto payload = CreateExtProcClientHeadersRequest(
      arena.ptr(), metadata.get(), config().forwarding_allowed_headers,
      config().forwarding_disallowed_headers, header_attributes,
      config().observability_mode, processing_mode);
  if (!payload.ok()) {
    CancelCallWithError(payload.status());
    return Immediate(StatusFlag(Failure{}));
  }
  // In observability mode, send to the child call in parallel with
  // sending to the sidestream.
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "observability mode: starting child call";
    StartChildCall(std::move(metadata));
  } else {
    client_initial_metadata_start_time_ = Timestamp::Now();
    client_initial_metadata_ = std::move(metadata);
  }
  // Send the serialized request payload over the side-stream.
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Sending client initial metadata to sidestream";
  return SendMessageToSideStream(std::move(*payload));
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

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleInitialMetadataFromServer(
    std::optional<ServerMetadataHandle> metadata) {
  if (!metadata.has_value()) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "No server initial metadata (trailers-only response)";
    is_trailers_only_.store(true, std::memory_order_release);
    return Immediate(StatusFlag(Success{}));
  }
  if (!processing_mode().send_response_headers || IsSideStreamClosed()) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag()
        << "Skipping server initial metadata (processing disabled "
           "or stream closed)";
    if (IsSideStreamFailureFatal()) {
      return Immediate(StatusFlag(Failure{}));
    }
    handler_.SpawnPushServerInitialMetadata(std::move(*metadata));
    return Immediate(StatusFlag(Success{}));
  }
  ArenaPromise<StatusFlag> send_promise;
  if (IsSideStreamClosed() ||
      side_stream_half_closed_.load(std::memory_order_acquire)) {
    send_promise = Immediate(StatusFlag(Success{}));
  } else {
    std::optional<ExtProcProcessingMode> processing_mode;
    if (IsFirstMessageOnSideStream()) {
      processing_mode = config().processing_mode;
    }
    upb::Arena arena;
    auto payload = CreateExtProcServerHeadersRequest(
        arena.ptr(), metadata->get(), config().forwarding_allowed_headers,
        config().forwarding_disallowed_headers,
        /*attributes=*/nullptr, config().observability_mode, processing_mode,
        /*end_of_stream=*/false);
    if (!payload.ok()) {
      CancelCallWithError(payload.status());
      return Immediate(StatusFlag(Failure{}));
    }
    send_promise = SendMessageToSideStream(std::move(*payload));
  }
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "Sending server initial metadata (observability mode)";
    handler_.SpawnPushServerInitialMetadata(std::move(*metadata));
    return send_promise;
  }
  if (drain_requested_latch_.IsSet()) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "Handling server initial metadata in drain mode";
    return TrySeq(
        WaitForSideStreamClosed(),
        [self = WeakRef(),
         metadata = std::move(*metadata)]() mutable -> StatusFlag {
          self->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
          return Success{};
        });
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Sending server initial metadata (normal mode)";
  server_initial_metadata_start_time_millis_.store(
      Timestamp::Now().milliseconds_after_process_epoch(),
      std::memory_order_release);
  server_initial_metadata_latch_.Set(std::move(*metadata));
  return send_promise;
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleTrailingMetadataFromServer(
    ServerMetadataHandle metadata) {
  if (IsSideStreamFailureFatal()) {
    return Immediate(StatusFlag(Failure{}));
  }
  // If trailing status is not OK (e.g. error from downstream), pass
  // trailers through directly.
  if (!IsStatusOk(*metadata)) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "Passing through non-OK server trailing metadata";
    handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
    return Immediate(StatusFlag(Success{}));
  }
  const bool trailers_only = is_trailers_only_.load(std::memory_order_acquire);
  const bool send_metadata = trailers_only
                                 ? processing_mode().send_response_headers
                                 : processing_mode().send_response_trailers;
  if (!send_metadata || IsSideStreamClosed()) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag()
        << "Skipping server trailing metadata (processing "
           "disabled or stream closed)";
    if (trailers_only) {
      server_trailing_metadata_latch_.Set(nullptr);
    }
    handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
    return Immediate(StatusFlag(Success{}));
  }
  ArenaPromise<StatusFlag> send_promise;
  if (IsSideStreamClosed() ||
      side_stream_half_closed_.load(std::memory_order_acquire)) {
    send_promise = Immediate(StatusFlag(Success{}));
  } else {
    // Include processing mode if this is the first message on the stream.
    std::optional<ExtProcProcessingMode> processing_mode;
    if (IsFirstMessageOnSideStream()) {
      processing_mode = config().processing_mode;
    }
    upb::Arena arena;
    auto payload =
        trailers_only ? CreateExtProcServerHeadersRequest(
                            arena.ptr(), metadata.get(),
                            config().forwarding_allowed_headers,
                            config().forwarding_disallowed_headers,
                            /*attributes=*/nullptr, config().observability_mode,
                            processing_mode, /*end_of_stream=*/true)
                      : CreateExtProcServerTrailersRequest(
                            arena.ptr(), metadata.get(),
                            config().forwarding_allowed_headers,
                            config().forwarding_disallowed_headers,
                            /*attributes=*/nullptr, config().observability_mode,
                            processing_mode);
    if (!payload.ok()) {
      CancelCallWithError(payload.status());
      return Immediate(StatusFlag(Failure{}));
    }
    send_promise = Seq(SendMessageToSideStream(std::move(*payload)),
                       [self = WeakRef()](StatusFlag status) {
                         if (status.ok()) {
                           self->server_trailers_sent_to_side_stream_.store(
                               true, std::memory_order_release);
                         }
                         return status;
                       });
  }
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag()
        << "Sending server trailing metadata (observability mode)";
    handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
    return send_promise;
  }
  if (drain_requested_latch_.IsSet()) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "Handling server trailing metadata in drain mode";
    return TrySeq(
        WaitForSideStreamClosed(),
        [self = WeakRef(),
         metadata = std::move(metadata)]() mutable -> StatusFlag {
          self->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
          return Success{};
        });
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Sending server trailing metadata (normal mode)";
  server_trailing_metadata_start_time_millis_.store(
      Timestamp::Now().milliseconds_after_process_epoch(),
      std::memory_order_release);
  server_trailing_metadata_latch_.Set(std::move(metadata));
  return send_promise;
}

//
// ExtProcFilter::ExtProcCall Server Message Processing
//

ArenaPromise<StatusFlag> ExtProcFilter::ExtProcCall::HandleMessageFromServer(
    MessageHandle message) {
  const bool send_body =
      processing_mode().send_response_body && !IsSideStreamClosed();
  if (!send_body) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "Server message non-processing mode";
    if (IsSideStreamFailureFatal()) {
      return Immediate(StatusFlag(Failure{}));
    }
    handler_.SpawnPushMessage(std::move(message));
    return Immediate(StatusFlag(Success{}));
  }
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "Server message observability mode";
    if (!IsSideStreamClosed() &&
        !side_stream_half_closed_.load(std::memory_order_acquire)) {
      return Seq(SendServerMessageRequest(message),
                 [self = WeakRef(), message = std::move(message)](
                     StatusFlag) mutable -> StatusFlag {
                   self->handler_.SpawnPushMessage(std::move(message));
                   return Success{};
                 });
    }
    if (IsSideStreamFailureFatal()) {
      return Immediate(StatusFlag(Failure{}));
    }
    handler_.SpawnPushMessage(std::move(message));
    return Immediate(StatusFlag(Success{}));
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Server message normal mode";
  if (drain_requested_latch_.IsSet()) {
    return TrySeq(WaitForSideStreamClosed(),
                  [self = WeakRef(),
                   message = std::move(message)]() mutable -> StatusFlag {
                    self->handler_.SpawnPushMessage(std::move(message));
                    return Success{};
                  });
  }
  if (!IsSideStreamClosed() &&
      !side_stream_half_closed_.load(std::memory_order_acquire)) {
    return Seq(SendServerMessageRequest(message),
               [self = WeakRef(), message = std::move(message)](
                   StatusFlag status) mutable -> StatusFlag {
                 if (!status.ok() || self->IsSideStreamClosed()) {
                   if (self->IsSideStreamFailureFatal()) {
                     return Failure{};
                   }
                   self->handler_.SpawnPushMessage(std::move(message));
                 }
                 // TODO(rishesh, roth): Hopping across activities via
                 // SpawnPushMessage breaks
                 // flow control in Call v3. This must be updated to use an
                 // inter-activity pipe or block the calling promise before
                 // using the ext_proc filter in a v3 stack.
                 return Success{};
               });
  }
  if (IsSideStreamFailureFatal()) {
    return Immediate(StatusFlag(Failure{}));
  }
  handler_.SpawnPushMessage(std::move(message));
  return Immediate(StatusFlag(Success{}));
}

ArenaPromise<StatusFlag> ExtProcFilter::ExtProcCall::SendServerMessageRequest(
    const MessageHandle& message) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Sending server body message request to side-stream";
  if (IsSideStreamClosed() ||
      side_stream_half_closed_.load(std::memory_order_acquire)) {
    return Immediate(StatusFlag(Success{}));
  }
  std::string message_bytes;
  if (message != nullptr) {
    message_bytes = message->payload()->JoinIntoString();
  }
  if (!config().observability_mode) {
    ++outstanding_s2c_messages_;
  }
  std::optional<ExtProcProcessingMode> processing_mode;
  if (IsFirstMessageOnSideStream()) {
    processing_mode = config().processing_mode;
  }
  upb::Arena arena;
  auto payload = CreateExtProcServerBodyRequest(
      arena.ptr(), message_bytes, /*attributes=*/nullptr,
      config().observability_mode, processing_mode);
  if (!payload.ok()) {
    CancelCallWithError(payload.status());
    return Immediate(StatusFlag(Failure{}));
  }
  first_body_message_sent_.store(true, std::memory_order_release);
  return SendMessageToSideStream(std::move(*payload));
}

ArenaPromise<StatusFlag> ExtProcFilter::ExtProcCall::HandleMessageFromClient(
    MessageHandle message) {
  // TODO(rishesh): If the external processor has already closed client sends
  // (via end_of_stream or end_of_stream_without_message in ProcessingResponse),
  // any subsequent message from the client cannot be processed. Since message
  // dropping is not yet supported in Call v3, fail the call here. Remove this
  // once PH2 is implemented.
  if (ext_proc_closed_client_sends_) {
    CancelCallWithError(
        absl::InternalError("Client sends closed by external processor"));
    return Immediate(StatusFlag(Failure{}));
  }
  const bool send_request_body =
      processing_mode().send_request_body && !IsSideStreamClosed();
  if (!send_request_body) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag()
        << "Client message non-processing mode (processing disabled or "
           "closed)";
    initiator_.SpawnPushMessage(std::move(message));
    return Immediate(StatusFlag(Success{}));
  }
  if (config().observability_mode) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << DebugTag() << "Client message observability mode";
    if (!IsSideStreamClosed() &&
        !side_stream_half_closed_.load(std::memory_order_acquire)) {
      return Seq(
          SendClientMessageRequest(message,
                                   /*end_of_stream=*/false,
                                   /*end_of_stream_without_message=*/false),
          [self = WeakRef(),
           message = std::move(message)](StatusFlag) mutable -> StatusFlag {
            self->initiator_.SpawnPushMessage(std::move(message));
            return Success{};
          });
    }
    if (IsSideStreamFailureFatal()) {
      return Immediate(StatusFlag(Failure{}));
    }
    initiator_.SpawnPushMessage(std::move(message));
    return Immediate(StatusFlag(Success{}));
  }
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Client message normal mode";
  if (drain_requested_latch_.IsSet()) {
    return TrySeq(WaitForSideStreamClosed(),
                  [self = WeakRef(),
                   message = std::move(message)]() mutable -> StatusFlag {
                    self->initiator_.SpawnPushMessage(std::move(message));
                    return Success{};
                  });
  }
  if (!IsSideStreamClosed() &&
      !side_stream_half_closed_.load(std::memory_order_acquire)) {
    return Seq(
        SendClientMessageRequest(message,
                                 /*end_of_stream=*/false,
                                 /*end_of_stream_without_message=*/false),
        [self = WeakRef(), message = std::move(message)](
            StatusFlag status) mutable -> StatusFlag {
          if (!status.ok() || self->IsSideStreamClosed()) {
            if (self->IsSideStreamFailureFatal()) {
              return Failure{};
            }
            self->initiator_.SpawnPushMessage(std::move(message));
          }
          // TODO(rishesh, roth): Hopping across activities via SpawnPushMessage
          // breaks flow control in Call v3. This must be updated to use an
          // inter-activity pipe or block the calling promise before using the
          // ext_proc filter in a v3 stack.
          return Success{};
        });
  }
  if (IsSideStreamFailureFatal()) {
    return Immediate(StatusFlag(Failure{}));
  }
  initiator_.SpawnPushMessage(std::move(message));
  return Immediate(StatusFlag(Success{}));
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleHalfCloseFromClient() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "HandleHalfCloseFromClient invoked";
  const bool send_request_body =
      processing_mode().send_request_body && !IsSideStreamClosed();
  if (!send_request_body) {
    initiator_.SpawnFinishSends();
    c2s_writes_done_ = true;
    return Immediate(StatusFlag(Success{}));
  }
  c2s_writes_done_ = true;
  if (ext_proc_closed_client_sends_) {
    return Immediate(StatusFlag(Success{}));
  }
  if (!config().observability_mode && drain_requested_latch_.IsSet()) {
    initiator_.SpawnFinishSends();
    c2s_writes_done_ = true;
    return Immediate(StatusFlag(Success{}));
  }
  if (!config().observability_mode) {
    client_half_close_start_time_ = Timestamp::Now();
  }
  if (!IsSideStreamClosed() &&
      !side_stream_half_closed_.load(std::memory_order_acquire)) {
    MessageHandle null_msg = nullptr;
    return Seq(
        SendClientMessageRequest(null_msg,
                                 /*end_of_stream=*/false,
                                 /*end_of_stream_without_message=*/true),
        [self = WeakRef()](StatusFlag status) mutable -> StatusFlag {
          if (!status.ok() && self->IsSideStreamFailureFatal()) {
            return Failure{};
          }
          if (!status.ok() || self->IsSideStreamClosed() ||
              self->config().observability_mode) {
            if (!self->config().observability_mode &&
                self->client_half_close_start_time_ != Timestamp::InfPast()) {
              self->ext_proc_filter_->RecordClientHalfCloseDuration(
                  (Timestamp::Now() - self->client_half_close_start_time_)
                      .seconds());
            }
            self->initiator_.SpawnFinishSends();
          }
          return Success{};
        });
  }
  if (IsSideStreamFailureFatal()) {
    return Immediate(StatusFlag(Failure{}));
  }
  initiator_.SpawnFinishSends();
  c2s_writes_done_ = true;
  return Immediate(StatusFlag(Success{}));
}

ArenaPromise<StatusFlag> ExtProcFilter::ExtProcCall::SendClientMessageRequest(
    const MessageHandle& message, bool end_of_stream,
    bool end_of_stream_without_message) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag() << "Sending client body message request to side-stream";
  std::string message_bytes;
  if (message != nullptr) {
    message_bytes = message->payload()->JoinIntoString();
  }
  if (!config().observability_mode) {
    ++outstanding_c2s_messages_;
  }
  std::optional<ExtProcProcessingMode> processing_mode;
  if (IsFirstMessageOnSideStream()) {
    processing_mode = config().processing_mode;
  }
  upb::Arena arena;
  auto payload = CreateExtProcClientBodyRequest(
      arena.ptr(), message_bytes, request_attributes_,
      config().observability_mode, processing_mode, end_of_stream,
      end_of_stream_without_message);
  request_attributes_ = nullptr;
  if (!payload.ok()) {
    CancelCallWithError(payload.status());
    return Immediate(StatusFlag(Failure{}));
  }
  first_body_message_sent_.store(true, std::memory_order_release);
  return SendMessageToSideStream(std::move(*payload));
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
  request_headers_received_ = true;
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
  if (processing_mode().send_request_headers && !request_headers_received_) {
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
  if (response.mutation.end_of_stream_without_message) {
    // TODO(rishesh): If the client is still sending messages on the data plane
    // (!c2s_writes_done_) when the external processor closes client sends
    // without a message (end_of_stream_without_message), future client messages
    // cannot be processed because no further responses will be received from
    // the side-stream. Since message dropping is not yet supported in Call v3,
    // fail the call here. Remove this once PH2 is implemented.
    if (!c2s_writes_done_) {
      CancelCallWithError(
          absl::InternalError("Client sends closed by external processor"));
      return Failure{};
    }
    ext_proc_closed_client_sends_ = true;
  } else if (response.mutation.end_of_stream) {
    ext_proc_closed_client_sends_ = true;
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

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleServerInitialMetadataFromSidestream(
    const ExtProcResponse::ResponseHeaders& response) {
  if (!processing_mode().send_response_headers) {
    CancelCallWithError(absl::InternalError(
        "Received response headers response but response headers are "
        "disabled"));
    return Immediate(StatusFlag(Failure{}));
  }
  response_headers_received_ = true;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag()
      << "Processing external processor response for server initial "
         "metadata";
  const bool trailers_only = is_trailers_only_.load(std::memory_order_acquire);
  auto latch_wait = trailers_only ? server_trailing_metadata_latch_.Wait()
                                  : server_initial_metadata_latch_.Wait();
  return Seq(
      std::move(latch_wait),
      [self = WeakRef(), response,
       trailers_only](ServerMetadataHandle metadata) mutable -> StatusFlag {
        if (auto status = ApplyHeaderMutations(
                response.mutation, self->config().mutation_rules, *metadata);
            !status.ok()) {
          self->CancelCallWithError(status);
          return Failure{};
        }
        if (!self->IsFailOpenAllowed() && self->IsSideStreamClosed()) {
          return Failure{};
        }
        if (trailers_only) {
          int64_t start_millis =
              self->server_trailing_metadata_start_time_millis_.load(
                  std::memory_order_acquire);
          if (start_millis != 0) {
            self->ext_proc_filter_->RecordServerTrailersDuration(
                (Timestamp::Now() -
                 Timestamp::FromMillisecondsAfterProcessEpoch(start_millis))
                    .seconds());
          }
          self->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
        } else {
          int64_t start_millis =
              self->server_initial_metadata_start_time_millis_.load(
                  std::memory_order_acquire);
          if (start_millis != 0) {
            self->ext_proc_filter_->RecordServerHeadersDuration(
                (Timestamp::Now() -
                 Timestamp::FromMillisecondsAfterProcessEpoch(start_millis))
                    .seconds());
          }
          self->handler_.SpawnPushServerInitialMetadata(std::move(metadata));
        }
        return Success{};
      });
}

StatusFlag ExtProcFilter::ExtProcCall::HandleServerMessageFromSidestream(
    const ExtProcResponse::ResponseBody& response) {
  if (!processing_mode().send_response_body) {
    CancelCallWithError(absl::InternalError(
        "Received response body response but response body is disabled"));
    return Failure{};
  }
  if (is_trailers_only_.load(std::memory_order_acquire)) {
    CancelCallWithError(absl::InternalError(
        "Received response body response in a Trailers-Only call"));
    return Failure{};
  }
  if (processing_mode().send_response_headers && !response_headers_received_) {
    CancelCallWithError(absl::InternalError(
        "Received response body response before response headers response"));
    return Failure{};
  }
  if (processing_mode().send_response_trailers && response_trailers_received_) {
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
  handler_.SpawnPushMessage(std::move(new_msg));
  return Success{};
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleServerTrailingMetadataFromSidestream(
    const ExtProcResponse::ResponseTrailers& response) {
  if (!processing_mode().send_response_trailers) {
    CancelCallWithError(absl::InternalError(
        "Received response trailers response but response trailers are "
        "disabled"));
    return Immediate(StatusFlag(Failure{}));
  }
  if (is_trailers_only_.load(std::memory_order_acquire)) {
    CancelCallWithError(absl::InternalError(
        "Received response trailers response in a Trailers-Only call"));
    return Immediate(StatusFlag(Failure{}));
  }
  if (processing_mode().send_response_headers && !response_headers_received_) {
    CancelCallWithError(absl::InternalError(
        "Received response trailers response before response headers "
        "response"));
    return Immediate(StatusFlag(Failure{}));
  }
  const bool s2c_body_outstanding =
      processing_mode().send_response_body &&
      (outstanding_s2c_messages_.load(std::memory_order_relaxed) > 0);
  if (s2c_body_outstanding) {
    CancelCallWithError(absl::InternalError(
        "Received response trailers response before all outstanding "
        "response body responses were received"));
    return Immediate(StatusFlag(Failure{}));
  }
  response_trailers_received_ = true;
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << DebugTag()
      << "Processing external processor response for server trailing "
         "metadata";
  return Seq(
      server_trailing_metadata_latch_.Wait(),
      [self = WeakRef(),
       response](ServerMetadataHandle metadata) mutable -> StatusFlag {
        if (auto status = ApplyHeaderMutations(
                response.mutation, self->config().mutation_rules, *metadata);
            !status.ok()) {
          self->CancelCallWithError(status);
          return Failure{};
        }
        int64_t start_millis =
            self->server_trailing_metadata_start_time_millis_.load(
                std::memory_order_acquire);
        if (start_millis != 0) {
          self->ext_proc_filter_->RecordServerTrailersDuration(
              (Timestamp::Now() -
               Timestamp::FromMillisecondsAfterProcessEpoch(start_millis))
                  .seconds());
        }
        self->handler_.SpawnPushServerTrailingMetadata(std::move(metadata));
        return Success{};
      });
}

ArenaPromise<StatusFlag>
ExtProcFilter::ExtProcCall::HandleImmediateResponseFromSidestream(
    const ExtProcResponse::ImmediateResponse& response) {
  if (config().disable_immediate_response ||
      !server_trailers_sent_to_side_stream_.load(std::memory_order_acquire)) {
    CancelCallWithError(absl::InternalError(
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
      << DebugTag() << "Processing external processor immediate response";
  return Seq(
      server_trailing_metadata_latch_.Wait(),
      [self = WeakRef(),
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
      [handler, ext_proc_filter = RefAsSubclass<ExtProcFilter>()]() mutable
          -> ArenaPromise<absl::Status> {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "[" << Activity::current()->DebugTag()
            << " ext_proc_filter=" << ext_proc_filter.get()
            << "] InterceptCall promise chain start";
        auto transport = ext_proc_filter->channel()->transport();
        // This shouldn't ever happen; added as a defensive check.
        if (transport == nullptr) {
          return Immediate(
              absl::InternalError("External processor transport unavailable"));
        }
        auto ext_proc_call = MakeRefCounted<ExtProcCall>(
            ext_proc_filter, std::move(transport), handler);
        return ext_proc_call->Run();
      });
}

}  // namespace grpc_core
