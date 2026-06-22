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
#include "src/core/xds/xds_client/serialized_streaming_call.h"
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
  explicit ExtProcCall(RefCountedPtr<ExtProcChannel> channel)
      : channel_(std::move(channel)) {
    const char* method = "/envoy.service.ext_proc.v3.ExternalProcessor/Process";
    streaming_call_ = MakeOrphanable<SerializedStreamingCall>(
        channel_->transport(), method,
        std::make_unique<StreamEventHandler>(WeakRef()));
    streaming_call_->StartRecvMessage();
  }

  ~ExtProcCall() override { streaming_call_.reset(); }

  absl::AnyInvocable<Poll<absl::Status>()> SendMessageLocked(
      bool condition, absl::AnyInvocable<std::string()> payload_generator)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
    if (!condition) {
      return []() -> Poll<absl::Status> { return absl::OkStatus(); };
    }
    if (stream_closed_ || streaming_call_ == nullptr) {
      return []() -> Poll<absl::Status> {
        return absl::CancelledError("Stream closed");
      };
    }
    return streaming_call_->Send(payload_generator());
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

  void OnRequestSent(bool ok) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this << " request sent ok=" << ok;
  }

  void OnRecvMessage(absl::string_view payload) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this
        << " message received, size=" << payload.size();
  }

  void OnStatusReceived(absl::Status status) {
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ExtProcCall " << this << " status received: " << status;
    MutexLock lock(&mu_);
    stream_closed_ = true;
    stream_status_ = std::move(status);
  }

  void Orphaned() override {
    OrphanablePtr<SerializedStreamingCall> call_to_reset;
    {
      MutexLock lock(&mu_);
      stream_closed_ = true;
      call_to_reset = std::move(streaming_call_);
    }
    call_to_reset.reset();
  }

  RefCountedPtr<ExtProcChannel> channel_;
  OrphanablePtr<SerializedStreamingCall> streaming_call_;
  Mutex mu_;
  bool stream_closed_ ABSL_GUARDED_BY(&mu_) = false;
  absl::Status stream_status_ ABSL_GUARDED_BY(&mu_);
};

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