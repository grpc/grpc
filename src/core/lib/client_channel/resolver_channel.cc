// Copyright 2024 gRPC authors.
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

#include "src/core/lib/client_channel/resolver_channel.h"

#include <memory>

namespace grpc_core {

namespace {

absl::StatusOr<std::string> UriToResolve(ChannelArgs& args) {
  // Get URI to resolve, using proxy mapper if needed.
  absl::optional<std::string> server_uri =
      args.GetOwnedString(GRPC_ARG_SERVER_URI);
  if (!server_uri.has_value()) {
    return absl::UnknownError(
        "target URI channel arg missing or wrong type in client channel "
        "filter");
  }
  std::string uri_to_resolve = CoreConfiguration::Get()
                                   .proxy_mapper_registry()
                                   .MapName(*server_uri, &args)
                                   .value_or(*server_uri);
  // Make sure the URI to resolve is valid, so that we know that
  // resolver creation will succeed later.
  if (!CoreConfiguration::Get().resolver_registry().IsValidTarget(
          uri_to_resolve)) {
    return absl::UnknownError(
        absl::StrCat("the target uri is not valid: ", uri_to_resolve));
  }
  return uri_to_resolve;
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// ResolverChannel::ResolverResultHandler

class ResolverChannel::ResolverResultHandler final
    : public Resolver::ResultHandler {
 public:
  explicit ResolverResultHandler(ResolverChannel* resolver_channel)
      : resolver_channel_(resolver_channel) {}

  void ReportResult(Resolver::Result result) override {
    resolver_channel_->UpdateResolverResultLocked(std::move(result));
  }

 private:
  ResolverChannel* const resolver_channel_;
};

///////////////////////////////////////////////////////////////////////////////
// ResolverChannel

absl::StatusOr<RefCountedPtr<ResolverChannel>> ResolverChannel::Create(
    ChannelArgs args) {
  auto uri_to_resolve = UriToResolve(args);
  if (!uri_to_resolve.ok()) return uri_to_resolve.status();
  auto work_serializer = std::make_shared<WorkSerializer>(
      args.GetObject<grpc_event_engine::experimental::EventEngine>());
  return MakeRefCounted<ResolverChannel>(
      args, CoreConfiguration::Get().resolver_registry().CreateResolver(
                uri_to_resolve.value(), args, nullptr, work_serializer,
                std::make_unique<ResolverResultHandler>(this)));
}

ResolverChannel::ResolverChannel(
    const ChannelArgs& args, std::shared_ptr<WorkSerializer> work_serializer,
    OrphanablePtr<Resolver> resolver)
    : Channel(args),
      resolved_stack_(nullptr),
      work_serializer_(std::move(work_serializer)),
      resolver_(std::move(resolver)) {
  work_serializer_->Run(
      [self = RefAsSubclass<ResolverChannel>()]() {
        self->resolver_->StartLocked();
      },
      DEBUG_LOCATION);
}

CallInitiator ResolverChannel::CreateCall(ClientMetadataHandle metadata,
                                          Arena* arena) {
  auto call = MakeCall(event_engine(), arena);
  call.initiator.SpawnInfallible(
      "wait-for-resolution", [self = RefAsSubclass<ResolverChannel>(),
                              handler = std::move(call.handler)]() mutable {
        return Map(self->resolved_stack_.Next(nullptr),
                   [handler = std::move(handler)](
                       RefCountedPtr<ResolvedStack> resolved_stack) mutable {
                     resolved_stack->StartCall(std::move(handler));
                     return Empty{};
                   });
      });
  return std::move(call.initiator);
}

void ResolverChannel::UpdateResolverResultLocked(Resolver::Result result) {
  auto resolver_callback = std::move(result.result_health_callback);
  auto resolved_stack =
      CreateResolvedStackFromResolverResult(std::move(result));
  resolved_stack_.Set(resolved_stack.value_or(nullptr));
  if (resolver_callback != nullptr) resolver_callback(resolved_stack.status());
}

absl::StatusOr<RefCountedPtr<ResolverChannel::ResolvedStack>>
ResolverChannel::CreateResolvedStackFromResolverResult(
    Resolver::Result result) {
  return nullptr;
}

}  // namespace grpc_core
