//
// Copyright 2025 gRPC authors.
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

#include "src/core/filter/composite/composite_filter.h"

#include "src/core/call/interception_chain.h"
#include "src/core/filter/filter_chain.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

std::string CompositeFilter::ExecuteFilterAction::ToString() const {
  std::vector<std::string> parts;
  for (const auto& [_, config] : filter_chain_) {
    parts.push_back(config == nullptr ? "<null>" : config->ToString());
  }
  return absl::StrCat("{", absl::StrJoin(parts, ", "), "}");
}

const grpc_channel_filter CompositeFilter::kFilterVtable =
    MakePromiseBasedFilter<
        CompositeFilter, FilterEndpoint::kClient,
        kFilterExaminesServerInitialMetadata | kFilterExaminesOutboundMessages |
            kFilterExaminesInboundMessages | kFilterExaminesCallContext>();

absl::StatusOr<RefCountedPtr<CompositeFilter>> CompositeFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args filter_args) {
  if (filter_args.config()->type() != Config::Type()) {
    return absl::InternalError("composite filter config has wrong type");
  }
  auto config = filter_args.config().TakeAsSubclass<const Config>();
  return MakeRefCounted<CompositeFilter>(args, std::move(config),
                                         std::move(filter_args));
}

namespace {

// An adaptor to use an InterceptionChainBuilder as a FilterChainBuilder
// when dealing with xDS HTTP filters.
class InterceptionChainBuilderWrapper final : public FilterChainBuilder {
 public:
  explicit InterceptionChainBuilderWrapper(InterceptionChainBuilder& builder)
      : builder_(builder) {}

  absl::StatusOr<RefCountedPtr<FilterChain>> Build() override {
    return absl::UnimplementedError("should never be called");
  }

 private:
  void AddFilter(const FilterHandle& filter_handle,
                 RefCountedPtr<const FilterConfig> config) override {
    filter_handle.AddToBuilder(&builder_, std::move(config));
  }

  InterceptionChainBuilder& builder_;
};

}  // namespace

CompositeFilter::CompositeFilter(const ChannelArgs& args,
                                 RefCountedPtr<const Config> config,
                                 ChannelFilter::Args filter_args)
    : config_(std::move(config)) {
  // Populate filter_chain_map_ from config_.
  config_->matcher->ForEachAction([&](const XdsMatcher::Action& action) {
    if (action.type() != ExecuteFilterAction::Type()) return;
    const auto& execute_filter_action =
        DownCast<const ExecuteFilterAction&>(action);
    InterceptionChainBuilder builder(args, filter_args.blackboard());
    InterceptionChainBuilderWrapper builder_wrapper(builder);
    for (const auto& [filter_impl, filter_config] :
         execute_filter_action.filter_chain()) {
      filter_impl->AddFilter(builder_wrapper, filter_config);
    }
    filter_chain_map_[&execute_filter_action] =
        builder.Build(wrapped_destination());
  });
}

void CompositeFilter::InterceptCall(
    UnstartedCallHandler unstarted_call_handler) {
  // Consume the call coming to us from the client side.
  CallHandler handler = Consume(std::move(unstarted_call_handler));
  handler.SpawnGuarded(
      "choose_filter_chain",
      [self = RefAsSubclass<CompositeFilter>(), handler]() mutable {
        return TrySeq(
            handler.PullClientInitialMetadata(),
            [handler, self](ClientMetadataHandle metadata) {
              // Use the matcher to find an action to use for this call.
              XdsMatcher::Result actions;
              if (!self->config_->matcher->FindMatches(
                      RpcMatchContext(metadata.get()), actions)) {
                return absl::UnavailableError(
                    "no match found in composite filter");
              }
              if (actions.size() != 1) {
                return absl::InternalError(
                    "composite filter: matcher succeeded but did "
                    "not return actions");
              }
              auto& action = actions.front();
              // If the action is SkipFilter, then we forward the
              // call to the next filter without sending it
              // through any child filter chain.
              if (action->type() == SkipFilterAction::Type()) {
                GRPC_TRACE_LOG(channel, INFO)
                    << "[composite " << self.get()
                    << "]: found SkipFilter, starting child call";
                CallInitiator initiator = self->MakeChildCall(
                    std::move(metadata), GetContext<Arena>()->Ref());
                ForwardCall(handler, initiator);
                return absl::OkStatus();
              }
              // If it's not SkipFilter, it must be ExecuteFilterAction.
              if (action->type() != ExecuteFilterAction::Type()) {
                return absl::InternalError(
                    "composite filter encountered unknown action type");
              }
              const auto& execute_filter_action =
                  DownCast<const ExecuteFilterAction&>(*action);
              // Determine if we're sampled.  If not, forward the
              // call to the next filter without sending it
              // through any child filter chain.
              if (execute_filter_action.sample_per_million() < 1000000) {
                uint32_t random_value =
                    absl::Uniform<uint32_t>(SharedBitGen(), 0, 1000000);
                bool sampled =
                    random_value < execute_filter_action.sample_per_million();
                if (!sampled) {
                  GRPC_TRACE_LOG(channel, INFO)
                      << "[composite " << self.get()
                      << "]: not sampled, starting child call";
                  CallInitiator initiator = self->MakeChildCall(
                      std::move(metadata), GetContext<Arena>()->Ref());
                  ForwardCall(handler, initiator);
                  return absl::OkStatus();
                }
              }
              // Find interception chain to use.
              auto it = self->filter_chain_map_.find(&execute_filter_action);
              if (it == self->filter_chain_map_.end()) {
                return absl::InternalError("no filter chain found for action");
              }
              GRPC_TRACE_LOG(channel, INFO)
                  << "[composite " << self.get()
                  << "]: starting call on filter chain";
              auto& unstarted_destination = it->second;
              if (!unstarted_destination.ok()) {
                return unstarted_destination.status();
              }
              auto [initiator, unstarted_handler] =
                  MakeCallPair(std::move(metadata), GetContext<Arena>()->Ref());
              (*unstarted_destination)->StartCall(std::move(unstarted_handler));
              ForwardCall(handler, initiator);
              return absl::OkStatus();
            });
      });
}

}  // namespace grpc_core
