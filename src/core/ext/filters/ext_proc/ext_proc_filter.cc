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
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

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
             [handler, pipe_owner](ServerMetadataHandle md) mutable {
               pipe_owner->server_trailing_metadata.Set(std::move(md));
               return Seq(
                   pipe_owner->server_trailing_metadata.Wait(),
                   [handler](ServerMetadataHandle md) mutable {
                     handler.SpawnPushServerTrailingMetadata(std::move(md));
                     return absl::OkStatus();
                   });
             });
}

auto ExtProcFilter::ServerToClientMessages(CallHandler handler,
                                           CallInitiator initiator,
                                           PipeOwner* pipe_owner) {
  struct ConsumerAction {
    mutable CallHandler handler;
    auto operator()(MessageHandle message) const {
      handler.SpawnPushMessage(std::move(message));
      return Map([]() { return Empty{}; },
                 [](Empty) { return absl::OkStatus(); });
    }
  };

  auto consumer =
      Seq(ForEach(std::move(pipe_owner->server_to_client_messages.receiver),
                  ConsumerAction{handler}),
          [](absl::Status status) mutable { return status; });

  auto producer =
      Seq(ForEach(MessagesFrom(initiator),
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

  return Map(TryJoin<absl::StatusOr>(std::move(producer), std::move(consumer)),
             [](auto result) -> absl::Status {
               if (!result.ok()) return result.status();
               return absl::OkStatus();
             });
}

auto ExtProcFilter::ServerInitialMetadata(CallHandler handler,
                                          CallInitiator initiator,
                                          PipeOwner* pipe_owner) {
  return TrySeq(
      initiator.PullServerInitialMetadata(),
      [self = RefAsSubclass<ExtProcFilter>(), handler, initiator,
       pipe_owner](std::optional<ServerMetadataHandle> metadata) mutable {
        pipe_owner->server_initial_metadata.Set(std::move(metadata));
        return Seq(
            pipe_owner->server_initial_metadata.Wait(),
            [self, handler, initiator,
             pipe_owner](std::optional<ServerMetadataHandle> metadata) mutable {
              const bool has_md = metadata.has_value();
              return If(
                  has_md,
                  [self, handler, initiator, pipe_owner,
                   md = std::move(metadata)]() mutable {
                    handler.SpawnPushServerInitialMetadata(std::move(*md));
                    return Seq(self->ServerToClientMessages(handler, initiator,
                                                            pipe_owner),
                               self->ServerTrailingMetadata(handler, initiator,
                                                            pipe_owner));
                  },
                  [self, handler, initiator, pipe_owner]() mutable {
                    return self->ServerTrailingMetadata(handler, initiator,
                                                        pipe_owner);
                  });
            });
      });
}

auto ExtProcFilter::ClientToServerMessages(CallHandler handler,
                                           CallInitiator initiator,
                                           PipeOwner* pipe_owner) {
  struct ConsumerAction {
    mutable CallInitiator initiator;
    auto operator()(MessageHandle message) const {
      initiator.SpawnPushMessage(std::move(message));
      return Map([]() { return Empty{}; },
                 [](Empty) { return absl::OkStatus(); });
    }
  };

  auto consumer =
      Seq(ForEach(std::move(pipe_owner->client_to_server_messages.receiver),
                  ConsumerAction{initiator}),
          [initiator](absl::Status status) mutable {
            initiator.SpawnFinishSends();
            return status;
          });

  auto producer =
      Seq(ForEach(MessagesFrom(handler),
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

  return Map(TryJoin<absl::StatusOr>(std::move(producer), std::move(consumer)),
             [](auto result) -> absl::Status {
               if (!result.ok()) return result.status();
               return absl::OkStatus();
             });
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
        pipe_owner->client_initial_metadata.Set(std::move(metadata));
        return Seq(
            pipe_owner->client_initial_metadata.Wait(),
            [self, handler, pipe_owner](ClientMetadataHandle metadata) mutable {
              CallInitiator initiator = self->MakeChildCall(
                  std::move(metadata), handler.arena()->Ref());
              handler.AddChildCall(initiator);

              initiator.SpawnGuarded(
                  "server_to_client",
                  [self, handler, initiator, pipe_owner]() mutable {
                    return self->ServerInitialMetadata(handler, initiator,
                                                       pipe_owner);
                  });

              return self->ClientToServerMessages(handler, initiator,
                                                  pipe_owner);
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