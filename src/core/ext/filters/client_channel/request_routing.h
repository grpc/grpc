/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_REQUEST_ROUTING_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_REQUEST_ROUTING_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/resolver.h"

namespace grpc_core {

class RequestRouter {
 public:

// FIXME: combine this with LoadBalancingPolicy::PickState somehow?
  class Request {
   public:
    Request(grpc_call_stack* owning_call, grpc_call_combiner* call_combiner,
            grpc_metadata_batch* send_initial_metadata,
            uint32_t* send_initial_metadata_flags,
            grpc_closure* on_service_config, grpc_closure* on_route_done)
        : owning_call_(owning_call), call_combiner_(call_combiner),
          on_service_config_(on_service_config),
          on_route_done_(on_route_done) {
      pick_.initial_metadata = send_initial_metadata;
      pick_.initial_metadata_flags = send_initial_metadata_flags;
    }

    LoadBalancingPolicy::PickState* pick() const { return &pick_; }

    GRPC_ABSTRACT_BASE_CLASS

   private:
    friend class RequestRouter;

    class ResolverResultWaiter;

    // Internal state, one per (request, request_router) pair.
    struct State {
      grpc_closure on_pick_done;
      grpc_closure on_cancel;
      Request* request;
      RequestRouter* request_router;
    };

    State* AddState(RequestRouter* request_router);

    void ProcessServiceConfigAndStartLbPickLocked(State* state);

    void StartLbPickLocked(State* state);

    static void LbPickDoneLocked(void* arg, grpc_error* error);
    static void LbPickCancelLocked(void* arg, grpc_error* error);

    // Populated by caller.
    grpc_call_stack* owning_call_;
    grpc_call_combiner* call_combiner_;
    grpc_closure* on_service_config_;
    grpc_closure* on_route_done_;
    LoadBalancingPolicy::PickState pick_;

    // Internal state.
// FIXME: should we allocate these on the arena instead?
    InlinedVector<State, 1> state_;
  };

  RequestRouter(grpc_channel_stack* owning_stack, grpc_combiner* combiner,
                grpc_client_channel_factory* client_channel_factory,
                grpc_pollset_set* interested_parties, TraceFlag* tracer,
                grpc_closure* on_resolver_result, bool request_service_config);

  ~RequestRouter();

// FIXME: avoid two-phase initialization somehow
  grpc_error* Init(const char* target_uri, grpc_channel_args* args);

  bool RouteCall(Request* request);

  void Shutdown(grpc_error* error);

  // Only valid during the call to on_resolver_result.
  grpc_channel_args* resolver_result() const { return resolver_result_; }

  const char* lb_policy_name() const {
    return lb_policy_ == nullptr ? nullptr : lb_policy_->name();
  }

 private:
  void StartResolvingLocked();
  void OnResolverShutdownLocked(grpc_error* error);
  const char* GetLbPolicyNameFromResolverResultLocked();
  void OnRequestReresolutionLocked(void* arg, grpc_error* error);
  static void OnResolverResultChangedLocked(void* arg, grpc_error* error);

  void CreateNewLbPolicyLocked(char* lb_policy_name,
                               grpc_connectivity_state* connectivity_state,
                               grpc_error** connectivity_error);
  void WatchLbPolicyLocked(grpc_connectivity_state current_state);
  static void OnLbPolicyStateChangedLocked(void* arg, grpc_error* error);

  void SetConnectivityStateLocked(grpc_connectivity_state state,
                                  grpc_error* error, const char* reason);

  // Passed in from caller at construction time.
  grpc_channel_stack* owning_stack_;
  grpc_combiner* combiner_;
  grpc_client_channel_factory* client_channel_factory_;
  grpc_pollset_set* interested_parties_;
  TraceFlag* tracer_;
  grpc_closure* on_resolver_result_;
  bool request_service_config_;

  // Resolver and associated state.
  OrphanablePtr<Resolver> resolver_;
  bool started_resolving_ = false;
  grpc_channel_args* resolver_result_ = nullptr;
  grpc_closure_list waiting_for_resolver_result_closures_;
  grpc_closure on_resolver_result_changed_;

  // LB policy and associated state.
  OrphanablePtr<LoadBalancingPolicy> lb_policy_;
  bool exit_idle_when_lb_policy_arrives_ = false;

  grpc_connectivity_state_tracker state_tracker_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_REQUEST_ROUTING_H */
