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

#include "src/core/client_channel/load_balanced_call_destination.h"

#include "absl/log/log.h"

#include "src/core/client_channel/client_channel.h"
#include "src/core/client_channel/client_channel_internal.h"
#include "src/core/client_channel/lb_metadata.h"
#include "src/core/client_channel/subchannel.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/telemetry/call_tracer.h"

namespace grpc_core {

namespace {

void MaybeCreateCallAttemptTracer(bool is_transparent_retry) {
  auto* call_tracer = MaybeGetContext<ClientCallTracer>();
  if (call_tracer == nullptr) return;
  auto* tracer = call_tracer->StartNewAttempt(is_transparent_retry);
  SetContext<CallTracerInterface>(tracer);
}

class LbCallState : public ClientChannelLbCallState {
 public:
  void* Alloc(size_t size) override { return GetContext<Arena>()->Alloc(size); }

  // Internal API to allow first-party LB policies to access per-call
  // attributes set by the ConfigSelector.
  ServiceConfigCallData::CallAttributeInterface* GetCallAttribute(
      UniqueTypeName type) const override {
    auto* service_config_call_data = GetContext<ServiceConfigCallData>();
    return service_config_call_data->GetCallAttribute(type);
  }

  ClientCallTracer::CallAttemptTracer* GetCallAttemptTracer() const override {
    return GetContext<ClientCallTracer::CallAttemptTracer>();
  }
};

// TODO(roth): Remove this in favor of the gprpp Match() function once
// we can do that without breaking lock annotations.
template <typename T>
T HandlePickResult(
    LoadBalancingPolicy::PickResult* result,
    std::function<T(LoadBalancingPolicy::PickResult::Complete*)> complete_func,
    std::function<T(LoadBalancingPolicy::PickResult::Queue*)> queue_func,
    std::function<T(LoadBalancingPolicy::PickResult::Fail*)> fail_func,
    std::function<T(LoadBalancingPolicy::PickResult::Drop*)> drop_func) {
  auto* complete_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Complete>(&result->result);
  if (complete_pick != nullptr) {
    return complete_func(complete_pick);
  }
  auto* queue_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Queue>(&result->result);
  if (queue_pick != nullptr) {
    return queue_func(queue_pick);
  }
  auto* fail_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Fail>(&result->result);
  if (fail_pick != nullptr) {
    return fail_func(fail_pick);
  }
  auto* drop_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Drop>(&result->result);
  CHECK(drop_pick != nullptr);
  return drop_func(drop_pick);
}

// Does an LB pick for a call.  Returns one of the following things:
// - Continue{}, meaning to queue the pick
// - a non-OK status, meaning to fail the call
// - a call destination, meaning that the pick is complete
// When the pick is complete, pushes client_initial_metadata onto
// call_initiator.  Also adds the subchannel call tracker (if any) to
// context.
LoopCtl<absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>> PickSubchannel(
    LoadBalancingPolicy::SubchannelPicker& picker,
    UnstartedCallHandler& unstarted_handler) {
  // Perform LB pick.
  auto& client_initial_metadata =
      unstarted_handler.UnprocessedClientInitialMetadata();
  LoadBalancingPolicy::PickArgs pick_args;
  Slice* path = client_initial_metadata.get_pointer(HttpPathMetadata());
  CHECK(path != nullptr);
  pick_args.path = path->as_string_view();
  LbCallState lb_call_state;
  pick_args.call_state = &lb_call_state;
  LbMetadata initial_metadata(&client_initial_metadata);
  pick_args.initial_metadata = &initial_metadata;
  auto result = picker.Pick(pick_args);
  // Handle result.
  return HandlePickResult<
      LoopCtl<absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>>>(
      &result,
      // CompletePick
      [&](LoadBalancingPolicy::PickResult::Complete* complete_pick)
          -> LoopCtl<absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>> {
        GRPC_TRACE_LOG(client_channel_lb_call, INFO)
            << "client_channel: " << GetContext<Activity>()->DebugTag()
            << " pick succeeded: subchannel="
            << complete_pick->subchannel.get();
        CHECK(complete_pick->subchannel != nullptr);
        // Grab a ref to the call destination while we're still
        // holding the data plane mutex.
        auto call_destination =
            DownCast<SubchannelInterfaceWithCallDestination*>(
                complete_pick->subchannel.get())
                ->call_destination();
        // If the subchannel has no call destination (e.g., if the
        // subchannel has moved out of state READY but the LB policy hasn't
        // yet seen that change and given us a new picker), then just
        // queue the pick.  We'll try again as soon as we get a new picker.
        if (call_destination == nullptr) {
          GRPC_TRACE_LOG(client_channel_lb_call, INFO)
              << "client_channel: " << GetContext<Activity>()->DebugTag()
              << " returned by LB picker has no connected subchannel; queueing "
                 "pick";
          return Continue{};
        }
        // If the LB policy returned a call tracker, inform it that the
        // call is starting and add it to context, so that we can notify
        // it when the call finishes.
        if (complete_pick->subchannel_call_tracker != nullptr) {
          complete_pick->subchannel_call_tracker->Start();
          SetContext(complete_pick->subchannel_call_tracker.release());
        }
        // Apply metadata mutations, if any.
        MetadataMutationHandler::Apply(complete_pick->metadata_mutations,
                                       &client_initial_metadata);
        MaybeOverrideAuthority(std::move(complete_pick->authority_override),
                               &client_initial_metadata);
        // Return the connected subchannel.
        return call_destination;
      },
      // QueuePick
      [&](LoadBalancingPolicy::PickResult::Queue* /*queue_pick*/) {
        GRPC_TRACE_LOG(client_channel_lb_call, INFO)
            << "client_channel: " << GetContext<Activity>()->DebugTag()
            << " pick queued";
        return Continue{};
      },
      // FailPick
      [&](LoadBalancingPolicy::PickResult::Fail* fail_pick)
          -> LoopCtl<absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>> {
        GRPC_TRACE_LOG(client_channel_lb_call, INFO)
            << "client_channel: " << GetContext<Activity>()->DebugTag()
            << " pick failed: " << fail_pick->status;
        // If wait_for_ready is false, then the error indicates the RPC
        // attempt's final status.
        if (!unstarted_handler.UnprocessedClientInitialMetadata()
                 .GetOrCreatePointer(WaitForReady())
                 ->value) {
          return MaybeRewriteIllegalStatusCode(std::move(fail_pick->status),
                                               "LB pick");
        }
        // If wait_for_ready is true, then queue to retry when we get a new
        // picker.
        return Continue{};
      },
      // DropPick
      [&](LoadBalancingPolicy::PickResult::Drop* drop_pick)
          -> LoopCtl<absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>> {
        GRPC_TRACE_LOG(client_channel_lb_call, INFO)
            << "client_channel: " << GetContext<Activity>()->DebugTag()
            << " pick dropped: " << drop_pick->status;
        return grpc_error_set_int(MaybeRewriteIllegalStatusCode(
                                      std::move(drop_pick->status), "LB drop"),
                                  StatusIntProperty::kLbPolicyDrop, 1);
      });
}

}  // namespace

void LoadBalancedCallDestination::StartCall(
    UnstartedCallHandler unstarted_handler) {
  // If there is a call tracer, create a call attempt tracer.
  bool* is_transparent_retry_metadata =
      unstarted_handler.UnprocessedClientInitialMetadata().get_pointer(
          IsTransparentRetry());
  bool is_transparent_retry = is_transparent_retry_metadata != nullptr
                                  ? *is_transparent_retry_metadata
                                  : false;
  MaybeCreateCallAttemptTracer(is_transparent_retry);
  // Spawn a promise to do the LB pick.
  // This will eventually start the call.
  unstarted_handler.SpawnGuardedUntilCallCompletes(
      "lb_pick", [unstarted_handler, picker = picker_]() mutable {
        return Map(
            // Wait for the LB picker.
            CheckDelayed(Loop(
                [last_picker =
                     RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>(),
                 unstarted_handler, picker]() mutable {
                  return Map(
                      picker.Next(last_picker),
                      [unstarted_handler, &last_picker](
                          RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>
                              picker) mutable {
                        CHECK_NE(picker.get(), nullptr);
                        last_picker = std::move(picker);
                        // Returns 3 possible things:
                        // - Continue to queue the pick
                        // - non-OK status to fail the pick
                        // - a connected subchannel to complete the pick
                        return PickSubchannel(*last_picker, unstarted_handler);
                      });
                })),
            // Create call stack on the connected subchannel.
            [unstarted_handler](
                std::tuple<
                    absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>,
                    bool>
                    pick_result) {
              auto& call_destination = std::get<0>(pick_result);
              const bool was_queued = std::get<1>(pick_result);
              if (!call_destination.ok()) {
                return call_destination.status();
              }
              // LB pick is done, so indicate that we've committed.
              auto* on_commit = MaybeGetContext<LbOnCommit>();
              if (on_commit != nullptr && *on_commit != nullptr) {
                (*on_commit)();
              }
              // If it was queued, add a trace annotation.
              if (was_queued) {
                auto* tracer =
                    MaybeGetContext<ClientCallTracer::CallAttemptTracer>();
                if (tracer != nullptr) {
                  tracer->RecordAnnotation("Delayed LB pick complete.");
                }
              }
              // Delegate to connected subchannel.
              // TODO(ctiller): need to insert LbCallTracingFilter at the top of
              // the stack
              (*call_destination)->StartCall(unstarted_handler);
              return absl::OkStatus();
            });
      });
}

void RegisterLoadBalancedCallDestination(CoreConfiguration::Builder* builder) {
  class LoadBalancedCallDestinationFactory final
      : public ClientChannel::CallDestinationFactory {
   public:
    RefCountedPtr<UnstartedCallDestination> CreateCallDestination(
        ClientChannel::PickerObservable picker) override {
      return MakeRefCounted<LoadBalancedCallDestination>(std::move(picker));
    }
  };

  builder->channel_args_preconditioning()->RegisterStage([](ChannelArgs args) {
    return args.SetObject(
        NoDestructSingleton<LoadBalancedCallDestinationFactory>::Get());
  });
}

}  // namespace grpc_core
