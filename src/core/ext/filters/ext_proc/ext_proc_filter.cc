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
}

void ExtProcFilter::ExtProcCall::OnRequestSent(bool ok) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " request sent ok=" << ok;
  
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
}

}  // namespace grpc_core