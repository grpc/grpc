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

// #include "src/core/call/interception_chain.h"
// #include "src/core/filter/filter_chain.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.upb.h"
#include "envoy/service/ext_proc/v3/external_processor.upb.h"
#include "src/core/call/call_spine.h"
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
// #include "src/core/util/shared_bit_gen.h"
// #include "src/core/xds/grpc/xds_http_filter.h"
// #include "absl/random/random.h"
#include "envoy/config/core/v3/base.upb.h"
#include "google/protobuf/struct.upb.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

//
// ExtProcRequest::Builder
//

ExtProcRequest::Builder::Builder(upb_Arena* arena) : arena_(arena) {
  request_ = envoy_service_ext_proc_v3_ProcessingRequest_new(arena_);
}

ExtProcRequest ExtProcRequest::Builder::Build() {
  return ExtProcRequest(arena_, request_);
}

ExtProcRequest::Builder& ExtProcRequest::Builder::SetRequestHeaders(
    envoy_config_core_v3_HeaderMap* headers, bool end_of_stream) {
  auto http_headers = envoy_service_ext_proc_v3_HttpHeaders_new(arena_);
  envoy_service_ext_proc_v3_HttpHeaders_set_headers(http_headers, headers);
  envoy_service_ext_proc_v3_HttpHeaders_set_end_of_stream(http_headers,
                                                          end_of_stream);
  envoy_service_ext_proc_v3_ProcessingRequest_set_request_headers(
      request_, http_headers);
  return *this;
}

ExtProcRequest::Builder& ExtProcRequest::Builder::SetResponseHeaders(
    envoy_config_core_v3_HeaderMap* headers, bool end_of_stream) {
  auto http_headers = envoy_service_ext_proc_v3_HttpHeaders_new(arena_);
  envoy_service_ext_proc_v3_HttpHeaders_set_headers(http_headers, headers);
  envoy_service_ext_proc_v3_HttpHeaders_set_end_of_stream(http_headers,
                                                          end_of_stream);
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_headers(
      request_, http_headers);
  return *this;
}

ExtProcRequest::Builder& ExtProcRequest::Builder::SetRequestBody(
    upb_StringView buf, bool end_of_stream) {
  envoy_service_ext_proc_v3_HttpBody* body =
      envoy_service_ext_proc_v3_HttpBody_new(arena_);
  envoy_service_ext_proc_v3_HttpBody_set_body(body, buf);
  envoy_service_ext_proc_v3_HttpBody_set_end_of_stream(body, end_of_stream);
  envoy_service_ext_proc_v3_ProcessingRequest_set_request_body(
      request_, body);
  return *this;
}

ExtProcRequest::Builder& ExtProcRequest::Builder::SetResponseBody(
    upb_StringView buf, bool end_of_stream) {
  envoy_service_ext_proc_v3_HttpBody* body =
      envoy_service_ext_proc_v3_HttpBody_new(arena_);
  envoy_service_ext_proc_v3_HttpBody_set_body(body, buf);
  envoy_service_ext_proc_v3_HttpBody_set_end_of_stream(body, end_of_stream);
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_body(
      request_, body);
  return *this;
}

ExtProcRequest::Builder& ExtProcRequest::Builder::SetResponseTrailers(
    envoy_config_core_v3_HeaderMap* trailer) {
  auto http_trailers = envoy_service_ext_proc_v3_HttpTrailers_new(arena_);
  envoy_service_ext_proc_v3_HttpTrailers_set_trailers(http_trailers, trailer);
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_trailers(
      request_, http_trailers);
  return *this;
}

ExtProcRequest::Builder& ExtProcRequest::Builder::SetObservabilityMode(
    bool mode) {
  envoy_service_ext_proc_v3_ProcessingRequest_set_observability_mode(
      request_, mode);
  return *this;
}

ExtProcRequest::Builder& ExtProcRequest::Builder::SetAttributes(
    const std::map<std::string, std::string>& attributes) {
  if (attributes.empty()) return *this;

  google_protobuf_Struct* struct_msg = google_protobuf_Struct_new(arena_);
  for (const auto& [name, value] : attributes) {
    char* name_buf = static_cast<char*>(upb_Arena_Malloc(arena_, name.size()));
    memcpy(name_buf, name.data(), name.size());
    char* value_buf = static_cast<char*>(upb_Arena_Malloc(arena_, value.size()));
    memcpy(value_buf, value.data(), value.size());
    google_protobuf_Value* val_msg = google_protobuf_Value_new(arena_);
    google_protobuf_Value_set_string_value(
        val_msg, upb_StringView_FromDataAndSize(value_buf, value.size()));
    google_protobuf_Struct_fields_set(
        struct_msg, upb_StringView_FromDataAndSize(name_buf, name.size()),
        val_msg, arena_);
  }

  envoy_service_ext_proc_v3_ProcessingRequest_attributes_set(
      request_,
      upb_StringView_FromDataAndSize("envoy.filters.http.ext_proc",
                                     sizeof("envoy.filters.http.ext_proc") - 1),
      struct_msg, arena_);

  return *this;
}

ExtProcRequest::Builder& ExtProcRequest::Builder::SetProtocolConfigRequest(
    bool is_first_message, BodySendMode mode) {
  if (!is_first_message) return *this;
  auto* protocol_config =
      envoy_service_ext_proc_v3_ProcessingRequest_mutable_protocol_config(
          request_, arena_);
  switch (mode) {
    case BodySendMode::kGrpc:
      envoy_service_ext_proc_v3_ProtocolConfiguration_set_request_body_mode(
          protocol_config,
          envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_GRPC);
      break;
    case BodySendMode::kNone:
      envoy_service_ext_proc_v3_ProtocolConfiguration_set_request_body_mode(
          protocol_config,
          envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_NONE);
      break;
  }
  return *this;
}

ExtProcRequest::Builder& ExtProcRequest::Builder::SetProtocolConfigResponse(
    bool is_first_message, BodySendMode mode) {
  if (!is_first_message) return *this;
  auto* protocol_config =
      envoy_service_ext_proc_v3_ProcessingRequest_mutable_protocol_config(
          request_, arena_);
  switch (mode) {
    case BodySendMode::kGrpc:
      envoy_service_ext_proc_v3_ProtocolConfiguration_set_response_body_mode(
          protocol_config,
          envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_GRPC);
      break;
    case BodySendMode::kNone:
      envoy_service_ext_proc_v3_ProtocolConfiguration_set_response_body_mode(
          protocol_config,
          envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_NONE);
      break;
  }
  return *this;
}

//
// ExtProcRequest
//

ExtProcRequest::ExtProcRequest(
    upb_Arena* arena, envoy_service_ext_proc_v3_ProcessingRequest* request)
    : arena_(arena), request_(request) {}

std::string ExtProcRequest::SerializeMessage() {
  size_t size;
  auto message = envoy_service_ext_proc_v3_ProcessingRequest_serialize(
      request_, arena_, &size);
  if (message == nullptr) return "";
  return std::string(message, size);
}
//
// ExtProcFilter
//

void (*g_test_ext_proc_metadata_modifier)(grpc_metadata_batch*) = nullptr;
void (*g_test_ext_proc_message_modifier)(MessageHandle*) = nullptr;

std::string ExtProcFilter::ProcessingMode::ToString() const {
  std::vector<std::string> parts;
  if (send_request_headers.has_value()) {
    parts.push_back(absl::StrCat("send_request_headers=",
                                 *send_request_headers ? "true" : "false"));
  }
  if (send_response_headers.has_value()) {
    parts.push_back(absl::StrCat("send_response_headers=",
                                 *send_response_headers ? "true" : "false"));
  }
  if (send_response_trailers.has_value()) {
    parts.push_back(absl::StrCat("send_response_trailers=",
                                 *send_response_trailers ? "true" : "false"));
  }
  if (send_request_body) parts.push_back("send_request_body=true");
  if (send_response_body) parts.push_back("send_response_body=true");
  return absl::StrCat("{", absl::StrJoin(parts, ", "), "}");
}

std::string ExtProcFilter::Config::ToString() const {
  std::vector<std::string> parts;
  if (grpc_service != nullptr) {
    parts.push_back(absl::StrCat("grpc_service=", grpc_service->ToString()));
  }
  if (failure_mode_allow) parts.push_back("failure_mode_allow=true");
  parts.push_back(absl::StrCat("processing_mode=", processing_mode.ToString()));
  if (allow_mode_override) parts.push_back("allow_mode_override=true");
  if (!allowed_override_modes.empty()) {
    std::vector<std::string> modes;
    for (const auto& mode : allowed_override_modes) {
      modes.push_back(mode.ToString());
    }
    parts.push_back(absl::StrCat("allowed_override_modes=[",
                                 absl::StrJoin(modes, ", "), "]"));
  }
  if (!request_attributes.empty()) {
    parts.push_back(absl::StrCat("request_attributes=[",
                                 absl::StrJoin(request_attributes, ", "), "]"));
  }
  if (!response_attributes.empty()) {
    parts.push_back(absl::StrCat("response_attributes=[",
                                 absl::StrJoin(response_attributes, ", "),
                                 "]"));
  }
  if (mutation_rules.has_value()) {
    parts.push_back(
        absl::StrCat("mutation_rules=", mutation_rules->ToString()));
  }
  if (!forwarding_allowed_headers.empty()) {
    std::vector<std::string> matchers;
    for (const auto& matcher : forwarding_allowed_headers) {
      matchers.push_back(matcher.ToString());
    }
    parts.push_back(absl::StrCat("forwarding_allowed_headers=[",
                                 absl::StrJoin(matchers, ", "), "]"));
  }
  if (!forwarding_disallowed_headers.empty()) {
    std::vector<std::string> matchers;
    for (const auto& matcher : forwarding_disallowed_headers) {
      matchers.push_back(matcher.ToString());
    }
    parts.push_back(absl::StrCat("forwarding_disallowed_headers=[",
                                 absl::StrJoin(matchers, ", "), "]"));
  }
  if (disable_immediate_response) {
    parts.push_back("disable_immediate_response=true");
  }
  if (observability_mode) parts.push_back("observability_mode=true");
  if (deferred_close_timeout != Duration::Zero()) {
    parts.push_back(absl::StrCat("deferred_close_timeout=",
                                 deferred_close_timeout.ToString()));
  }
  return absl::StrCat("{", absl::StrJoin(parts, ", "), "}");
}

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
    : config_(std::move(config)) {}

auto ExtProcFilter::ServerTrailingMetadata(CallHandler handler,
                                           CallInitiator initiator,
                                           PipeOwner* pipe_owner) {
  return Seq(initiator.PullServerTrailingMetadata(),
             [self = RefAsSubclass<ExtProcFilter>(), handler,
              pipe_owner](ServerMetadataHandle md) mutable {
               return If(
                   !self->config_->observability_mode,
                   [handler, pipe_owner, &md]() mutable {
                     pipe_owner->server_trailing_metadata.Set(std::move(md));
                     return Seq(pipe_owner->server_trailing_metadata.Wait(),
                                [handler](ServerMetadataHandle md) mutable {
                                  handler.SpawnPushServerTrailingMetadata(
                                      std::move(md));
                                  return absl::OkStatus();
                                });
                   },
                   [handler, &md]() mutable {
                     handler.SpawnPushServerTrailingMetadata(std::move(md));
                     return absl::OkStatus();
                   });
             });
}

auto ExtProcFilter::ServerToClientMessages(CallHandler handler,
                                           CallInitiator initiator,
                                           PipeOwner* pipe_owner) {
  return If(
      !config_->observability_mode,
      [handler, initiator, pipe_owner]() mutable {
        struct ConsumerAction {
          mutable CallHandler handler;
          auto operator()(MessageHandle message) const {
            handler.SpawnPushMessage(std::move(message));
            return []() { return absl::OkStatus(); };
          }
        };

        auto consumer = Seq(
            ForEach(std::move(pipe_owner->server_to_client_messages.receiver),
                    ConsumerAction{handler}),
            [](absl::Status status) mutable { return status; });

        auto producer = Seq(
            ForEach(MessagesFrom(initiator),
                    [pipe_owner](MessageHandle message) {
                      if (g_test_ext_proc_message_modifier != nullptr) {
                        g_test_ext_proc_message_modifier(&message);
                      }
                      return Map(
                          pipe_owner->server_to_client_messages.sender.Push(
                              std::move(message)),
                          [](bool success) -> absl::Status {
                            if (!success) {
                              return absl::InternalError("Push to pipe failed");
                            }
                            return absl::OkStatus();
                          });
                    }),
            [pipe_owner](absl::Status status) mutable {
              pipe_owner->server_to_client_messages.sender.MarkClosed();
              return status;
            });

        return Map(
            TryJoin<absl::StatusOr>(std::move(producer), std::move(consumer)),
            [](auto result) -> absl::Status {
              if (!result.ok()) return result.status();
              return absl::OkStatus();
            });
      },
      [handler, initiator]() mutable {
        return Seq(ForEach(MessagesFrom(initiator),
                           [handler](MessageHandle message) mutable {
                             if (g_test_ext_proc_message_modifier != nullptr) {
                               g_test_ext_proc_message_modifier(&message);
                             }
                             handler.SpawnPushMessage(std::move(message));
                             return []() { return absl::OkStatus(); };
                           }),
                   [](absl::Status status) mutable { return status; });
      });
}

auto ExtProcFilter::ServerInitialMetadata(CallHandler handler,
                                          CallInitiator initiator,
                                          PipeOwner* pipe_owner) {
  return TrySeq(
      initiator.PullServerInitialMetadata(),
      [self = RefAsSubclass<ExtProcFilter>(), handler, initiator, pipe_owner](
          std::optional<ServerMetadataHandle> pulled_metadata) mutable {
        const bool has_md = pulled_metadata.has_value();
        return If(
            has_md,
            [self, handler, initiator, pipe_owner,
             pulled_md = std::move(pulled_metadata)]() mutable {
              return If(
                  !self->config_->observability_mode,
                  [self, handler, initiator, pipe_owner, &pulled_md]() mutable {
                    pipe_owner->server_initial_metadata.Set(
                        std::move(pulled_md));
                    return Seq(pipe_owner->server_initial_metadata.Wait(),
                               [self, handler, initiator,
                                pipe_owner](std::optional<ServerMetadataHandle>
                                                waited_metadata) mutable {
                                 handler.SpawnPushServerInitialMetadata(
                                     std::move(*waited_metadata));
                                 return Seq(
                                     self->ServerToClientMessages(
                                         handler, initiator, pipe_owner),
                                     self->ServerTrailingMetadata(
                                         handler, initiator, pipe_owner));
                               });
                  },
                  [self, handler, initiator, pipe_owner, &pulled_md]() mutable {
                    handler.SpawnPushServerInitialMetadata(
                        std::move(*pulled_md));
                    return Seq(self->ServerToClientMessages(handler, initiator,
                                                            pipe_owner),
                               self->ServerTrailingMetadata(handler, initiator,
                                                            pipe_owner));
                  });
            },
            [self, handler, initiator, pipe_owner]() mutable {
              return self->ServerTrailingMetadata(handler, initiator,
                                                  pipe_owner);
            });
      });
}

auto ExtProcFilter::ClientToServerMessages(CallHandler handler,
                                           CallInitiator initiator,
                                           PipeOwner* pipe_owner) {
  return If(
      !config_->observability_mode,
      [handler, initiator, pipe_owner]() mutable {
        struct ConsumerAction {
          mutable CallInitiator initiator;
          auto operator()(MessageHandle message) const {
            initiator.SpawnPushMessage(std::move(message));
            return []() { return absl::OkStatus(); };
          }
        };

        auto consumer = Seq(
            ForEach(std::move(pipe_owner->client_to_server_messages.receiver),
                    ConsumerAction{initiator}),
            [initiator](absl::Status status) mutable {
              initiator.SpawnFinishSends();
              return status;
            });

        auto producer = Seq(
            ForEach(MessagesFrom(handler),
                    [pipe_owner](MessageHandle message) {
                      if (g_test_ext_proc_message_modifier != nullptr) {
                        g_test_ext_proc_message_modifier(&message);
                      }
                      return Map(
                          pipe_owner->client_to_server_messages.sender.Push(
                              std::move(message)),
                          [](bool success) -> absl::Status {
                            if (!success) {
                              return absl::InternalError("Push to pipe failed");
                            }
                            return absl::OkStatus();
                          });
                    }),
            [pipe_owner](absl::Status status) mutable {
              pipe_owner->client_to_server_messages.sender.MarkClosed();
              return status;
            });

        return Map(
            TryJoin<absl::StatusOr>(std::move(producer), std::move(consumer)),
            [](auto result) -> absl::Status {
              if (!result.ok()) return result.status();
              return absl::OkStatus();
            });
      },
      [handler, initiator]() mutable {
        return Seq(ForEach(MessagesFrom(handler),
                           [initiator](MessageHandle message) mutable {
                             if (g_test_ext_proc_message_modifier != nullptr) {
                               g_test_ext_proc_message_modifier(&message);
                             }
                             initiator.SpawnPushMessage(std::move(message));
                             return []() { return absl::OkStatus(); };
                           }),
                   [initiator](absl::Status status) mutable {
                     initiator.SpawnFinishSends();
                     return status;
                   });
      });
}

auto ExtProcFilter::StartCallLoops(CallHandler handler, PipeOwner* pipe_owner,
                                   ClientMetadataHandle metadata) {
  CallInitiator initiator =
      MakeChildCall(std::move(metadata), handler.arena()->Ref());
  handler.AddChildCall(initiator);

  initiator.SpawnGuarded(
      "server_to_client", [self = RefAsSubclass<ExtProcFilter>(), handler,
                           initiator, pipe_owner]() mutable {
        return self->ServerInitialMetadata(handler, initiator, pipe_owner);
      });

  return ClientToServerMessages(handler, initiator, pipe_owner);
}

auto ExtProcFilter::ClientInitialMetadata(CallHandler handler) {
  auto* pipe_owner = GetContext<Arena>()->ManagedNew<PipeOwner>();
  return TrySeq(
      handler.PullClientInitialMetadata(),
      [self = RefAsSubclass<ExtProcFilter>(), handler,
       pipe_owner](ClientMetadataHandle metadata) mutable {
        if (g_test_ext_proc_metadata_modifier != nullptr) {
          g_test_ext_proc_metadata_modifier(metadata.get());
        }

        return If(
            !self->config_->observability_mode,
            [self, handler, pipe_owner, &metadata]() mutable {
              pipe_owner->client_initial_metadata.Set(std::move(metadata));
              return Seq(pipe_owner->client_initial_metadata.Wait(),
                         [self, handler,
                          pipe_owner](ClientMetadataHandle metadata) mutable {
                           return self->StartCallLoops(handler, pipe_owner,
                                                       std::move(metadata));
                         });
            },
            [self, handler, pipe_owner, &metadata]() mutable {
              return self->StartCallLoops(handler, pipe_owner,
                                          std::move(metadata));
            });
      });
}

void ExtProcFilter::InterceptCall(UnstartedCallHandler unstarted_call_handler) {
  CallHandler handler = Consume(std::move(unstarted_call_handler));

  handler.SpawnGuarded("ext_proc_call", [self = RefAsSubclass<ExtProcFilter>(),
                                         handler]() mutable {
    return self->ClientInitialMetadata(handler);
  });
}

}  // namespace grpc_core