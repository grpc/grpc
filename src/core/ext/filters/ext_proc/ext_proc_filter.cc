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

auto ExtProcFilter::ClientInitialMetadata(CallHandler handler){}
auto ExtProcFilter::ClientToServerMessages(CallHandler handler){}
auto ExtProcFilter::ServerToClientMessages(CallHandler handler){}
auto ExtProcFilter::ServerInitialMetadata(CallHandler handler){}
auto ExtProcFilter::ServerTrailingMetadata(CallHandler handler){}

void ExtProcFilter::InterceptCall(UnstartedCallHandler unstarted_call_handler) {
  // Consume the call coming to us from the client side.
  // This yields a handler that can be used to interact with the client-side
  // of the call.
  CallHandler handler = Consume(std::move(unstarted_call_handler));
  handler.SpawnGuarded("ext_proc_call", [self = RefAsSubclass<ExtProcFilter>(),
                                         handler]() mutable {
    // Step 1: Wait for and pull the initial metadata from the client.
    return TrySeq(
        handler.PullClientInitialMetadata(),
        [handler, self](ClientMetadataHandle metadata) mutable {
          // Create a Latch to hold the metadata while we asynchronously
          // process (e.g., mutate) it before forwarding it downstream.
          auto metadata_latch = std::make_shared<Latch<ClientMetadataHandle>>();

          // Spawn a background task to process the metadata.
          // In reality, this could be an async RPC to an external processor.
          handler.SpawnInfallible(
              "ext_proc_process_metadata",
              [metadata_latch, metadata = std::move(metadata), self]() mutable {
                metadata->Log(
                    [&](absl::string_view key, absl::string_view value) {
                      GRPC_TRACE_LOG(channel, INFO)
                          << "[ext_proc rishesh-------" << self.get()
                          << "]: key: " << key << ", value: " << value;
                    });
                // Once modifications are complete, set the latch to
                // unblock the main call pipeline and forward the mutate data.
                if (g_test_ext_proc_metadata_modifier != nullptr) {
                  g_test_ext_proc_metadata_modifier(metadata.get());
                }
                metadata_latch->Set(std::move(metadata));
                return Empty{};
              });

          // Step 2: Suspend pipeline execution until the Latch signals that
          // the optionally modified metadata is available.
          return Seq(metadata_latch->Wait(), [handler, self, metadata_latch](
                                                 ClientMetadataHandle
                                                     metadata) mutable {
            // Create the child call using the newly mutated metadata
            CallInitiator initiator = self->MakeChildCall(
                std::move(metadata), GetContext<Arena>()->Ref());
            handler.AddChildCall(initiator);

            // 1. Client-to-server messages via Pipe.
            auto message_pipe = std::make_shared<Pipe<MessageHandle>>();
            handler.SpawnInfallible(
                "read_client_messages", [handler, message_pipe]() mutable {
                  return Seq(
                      ForEach(MessagesFrom(handler),
                              [message_pipe](MessageHandle msg) mutable {
                                return Map(
                                    message_pipe->sender.Push(std::move(msg)),
                                    [](bool) { return Success{}; });
                              }),
                      []() {});
                });

            handler.SpawnInfallible(
                "ext_proc_process_client_messages",
                [message_pipe, initiator]() mutable {
                  return Seq(
                      ForEach(std::move(message_pipe->receiver),
                              [initiator](MessageHandle msg) mutable {
                                if (g_test_ext_proc_message_modifier !=
                                    nullptr) {
                                  g_test_ext_proc_message_modifier(&msg);
                                }
                                initiator.SpawnPushMessage(std::move(msg));
                                return Success{};
                              }),
                      [initiator]() mutable { initiator.SpawnFinishSends(); });
                });

            // 2. Server-to-client messages and metadata.
            initiator.SpawnInfallible("read_the_things", [initiator,
                                                          handler]() mutable {
              return Seq(
                  initiator.CancelIfFails(TrySeq(
                      initiator.PullServerInitialMetadata(),
                      [handler, initiator](
                          std::optional<ServerMetadataHandle> md) mutable {
                        const bool has_md = md.has_value();
                        return If(
                            has_md,
                            [handler, initiator, md = std::move(md)]() mutable {
                              handler.SpawnPushServerInitialMetadata(
                                  std::move(*md));
                              return ForEach(
                                  MessagesFrom(initiator),
                                  [handler](MessageHandle msg) mutable {
                                    handler.SpawnPushMessage(std::move(msg));
                                    return Success{};
                                  });
                            },
                            []() -> StatusFlag { return Success{}; });
                      })),
                  initiator.PullServerTrailingMetadata(),
                  [handler](ServerMetadataHandle md) mutable {
                    handler.SpawnPushServerTrailingMetadata(std::move(md));
                  });
            });

            return absl::OkStatus();
          });
        });
  });
}

}  // namespace grpc_core