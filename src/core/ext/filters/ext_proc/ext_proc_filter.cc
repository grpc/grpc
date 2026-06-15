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
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/debug/trace_flags.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
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
}

ExtProcFilter::ExtProcCall::~ExtProcCall() {
  streaming_call_.reset();
  channel_.reset();
}

void ExtProcFilter::ExtProcCall::SendMessageLocked(std::string payload) {
  MutexLock lock(&mu_);
  if (orphaned_) return;
  if (streaming_call_ == nullptr) {
    const char* method = "/envoy.service.ext_proc.v3.ExternalProcessor/Process";
    streaming_call_ = channel()->transport()->CreateStreamingCall(
        method, std::make_unique<StreamEventHandler>(WeakRef()));
    GRPC_CHECK(streaming_call_ != nullptr);
    GRPC_TRACE_LOG(ext_proc_filter, INFO)
        << "ext_proc server " << channel()->server()->server_uri()
        << ": starting ext_proc call (ext_proc_call=" << this
        << ", streaming_call=" << streaming_call_.get() << ")";
    streaming_call_->StartRecvMessage();
  }
  streaming_call_->SendMessage(std::move(payload));
}

void ExtProcFilter::ExtProcCall::OnRequestSent() {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " request sent";
}

void ExtProcFilter::ExtProcCall::OnStatusReceived(absl::Status status) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " status received: " << status;
  if (!status.ok()) {
    if (processing_mode_.send_request_headers &&
        !request_headers_latch_.IsSet()) {
      if (!failure_mode_allow_) {
        request_headers_latch_.Set(status);
      } else {
        request_headers_latch_.Set(ExtProcResponse{});
      }
    }
  }
  MutexLock lock(&mu_);
  streaming_call_.reset();
}

void ExtProcFilter::ExtProcCall::OnRecvMessage(absl::string_view payload) {
  GRPC_TRACE_LOG(ext_proc_filter, INFO)
      << "ExtProcCall " << this << " message received, size=" << payload.size();
  upb::Arena arena;
  auto* response = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      payload.data(), payload.size(), arena.ptr());
  if (response == nullptr) {
    LOG(ERROR) << "Failed to parse ProcessingResponse";
    if (processing_mode_.send_request_headers &&
        !request_headers_latch_.IsSet()) {
      request_headers_latch_.Set(
          absl::InternalError("Failed to parse ProcessingResponse"));
    }
  } else {
    auto parsed_response_or =
        ParseExtProcResponse(response, observability_mode_);
    if (!parsed_response_or.ok()) {
      LOG(ERROR) << "Failed to validate ProcessingResponse: "
                 << parsed_response_or.status();
      if (processing_mode_.send_request_headers &&
          !request_headers_latch_.IsSet()) {
        request_headers_latch_.Set(parsed_response_or.status());
      }
    } else {
      auto parsed_response = std::move(*parsed_response_or);
      if (parsed_response.immediate_response.has_value()) {
        if (processing_mode_.send_request_headers &&
            !request_headers_latch_.IsSet()) {
          request_headers_latch_.Set(std::move(parsed_response));
        }
      } else if (parsed_response.request_headers.has_value()) {
        if (processing_mode_.send_request_headers &&
            !request_headers_latch_.IsSet()) {
          request_headers_latch_.Set(std::move(parsed_response));
        }
      }
    }
  }
  MutexLock lock(&mu_);
  if (!orphaned_ && streaming_call_ != nullptr) {
    streaming_call_->StartRecvMessage();
  }
}

void ExtProcFilter::ExtProcCall::Orphaned() {
  MutexLock lock(&mu_);
  orphaned_ = true;
  streaming_call_.reset();
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

auto ExtProcFilter::ServerToClient(CallHandler handler, CallInitiator initiator,
                                   RefCountedPtr<ExtProcCall> ext_proc_call) {
  return Seq(
      initiator.CancelIfFails(TrySeq(
          initiator.PullServerInitialMetadata(),
          [handler,
           initiator](std::optional<ServerMetadataHandle> metadata) mutable {
            const bool has_md = metadata.has_value();
            GRPC_TRACE_LOG(ext_proc_filter, INFO)
                << "ExtProc: ServerToClient initial metadata received, "
                   "present: "
                << has_md;
            return If(
                has_md,
                [handler, initiator, md = std::move(metadata)]() mutable {
                  GRPC_TRACE_LOG(ext_proc_filter, INFO)
                      << "ExtProc: ServerToClient initial metadata content:\n"
                      << (*md)->DebugString();
                  handler.SpawnPushServerInitialMetadata(std::move(*md));
                  return ForEach(
                      MessagesFrom(initiator),
                      [handler](MessageHandle message) mutable {
                        GRPC_TRACE_LOG(ext_proc_filter, INFO)
                            << "ExtProc: ServerToClient message intercepted:\n"
                            << message->DebugString();
                        handler.SpawnPushMessage(std::move(message));
                        return Success{};
                      });
                },
                []() -> StatusFlag { return Success{}; });
          })),
      initiator.PullServerTrailingMetadata(),
      [handler](ServerMetadataHandle md) mutable {
        GRPC_TRACE_LOG(ext_proc_filter, INFO)
            << "ExtProc: ServerTrailingMetadata received:\n"
            << md->DebugString();
        handler.SpawnPushServerTrailingMetadata(std::move(md));
        return absl::OkStatus();
      });
}

auto ExtProcFilter::ClientToServerMessage(
    CallHandler handler, CallInitiator initiator,
    RefCountedPtr<ExtProcCall> ext_proc_call) {
  return TrySeq(ForEach(MessagesFrom(handler),
                        [initiator](MessageHandle message) mutable {
                          GRPC_TRACE_LOG(ext_proc_filter, INFO)
                              << "ExtProc: ClientToServer message received:\n"
                              << message->DebugString();
                          initiator.SpawnPushMessage(std::move(message));
                          return absl::OkStatus();
                        }),
                [initiator]() mutable {
                  GRPC_TRACE_LOG(ext_proc_filter, INFO)
                      << "ExtProc: ClientToServer finished sends";
                  initiator.SpawnFinishSends();
                  return absl::OkStatus();
                });
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
        self->channel(), self->config()->processing_mode,
        self->config()->observability_mode, self->config()->failure_mode_allow,
        self->config()->deferred_close_timeout);
    return TrySeq(
        handler.PullClientInitialMetadata(),
        [self, handler, ext_proc_call,
         send_headers = self->config()->processing_mode.send_request_headers](
            ClientMetadataHandle metadata) mutable {
          GRPC_TRACE_LOG(ext_proc_filter, INFO)
              << "ExtProc: Client initial metadata received:\n"
              << metadata->DebugString();
          auto shared_metadata =
              std::make_shared<ClientMetadataHandle>(std::move(metadata));
          return TrySeq(
              If(
                  send_headers,
                  [self, ext_proc_call, shared_metadata]() {
                    GRPC_TRACE_LOG(ext_proc_filter, INFO)
                        << "ExtProc: Sending client initial metadata";
                    bool is_first = ext_proc_call->TestAndSetIsFirstMessage();
                    upb::Arena serialization_arena;
                    std::string serialized = CreateExtProcRequest(
                        serialization_arena.ptr(),
                        ExtProcRequestType::kClientHeaders,
                        shared_metadata->get(),
                        self->config()->forwarding_allowed_headers,
                        self->config()->forwarding_disallowed_headers,
                        ParseAttributes(
                            serialization_arena.ptr(),
                            self->config()->request_attributes,
                            **shared_metadata,
                            self->default_authority_.as_string_view()),
                        self->config()->observability_mode, is_first,
                        self->config()->processing_mode.send_request_body,
                        self->config()->processing_mode.send_response_body);
                    ext_proc_call->SendMessageLocked(std::move(serialized));
                    return If(
                        self->config()->observability_mode,
                        [shared_metadata]() {
                          return [shared_metadata]()
                                     -> Poll<
                                         absl::StatusOr<ClientMetadataHandle>> {
                            return absl::StatusOr<ClientMetadataHandle>(
                                std::move(*shared_metadata));
                          };
                        },
                        [self, ext_proc_call, shared_metadata]() {
                          return Map(
                              ext_proc_call->request_headers_latch_.Wait(),
                              [self, shared_metadata](
                                  absl::StatusOr<ExtProcResponse> response_or)
                                  -> absl::StatusOr<ClientMetadataHandle> {
                                if (!response_or.ok()) {
                                  return response_or.status();
                                }
                                const auto& response = *response_or;
                                if (response.request_headers.has_value()) {
                                  if (!response.request_headers->ok()) {
                                    return response.request_headers->status();
                                  }
                                  const auto& mutations =
                                      **response.request_headers;
                                  const auto* rules =
                                      self->config()->mutation_rules.has_value()
                                          ? &self->config()
                                                 ->mutation_rules.value()
                                          : nullptr;
                                  auto status = ApplyHeaderMutations(
                                      mutations, rules, **shared_metadata);
                                  if (!status.ok()) {
                                    return status;
                                  }
                                }
                                return std::move(*shared_metadata);
                              });
                        });
                  },
                  [shared_metadata]() {
                    return [shared_metadata]()
                               -> Poll<absl::StatusOr<ClientMetadataHandle>> {
                      return absl::StatusOr<ClientMetadataHandle>(
                          std::move(*shared_metadata));
                    };
                  }),
              [self, handler,
               ext_proc_call](ClientMetadataHandle metadata) mutable {
                CallInitiator initiator = self->MakeChildCall(
                    std::move(metadata), handler.arena()->Ref());
                handler.AddChildCall(initiator);
                initiator.SpawnGuarded(
                    "server_to_client",
                    [self, handler, initiator, ext_proc_call]() mutable {
                      GRPC_TRACE_LOG(ext_proc_filter, INFO)
                          << "ExtProc: server_to_client task started";
                      return self->ServerToClient(handler, initiator,
                                                  ext_proc_call);
                    });
                return self->ClientToServerMessage(handler, initiator,
                                                   ext_proc_call);
              });
        });
  });
}

}  // namespace grpc_core