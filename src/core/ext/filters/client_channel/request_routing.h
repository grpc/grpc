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

#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

class RequestRouter {
 public:
  class Request {
   public:
    // Synchronous callback that applies the service config to a call.
    // Returns false if the call should be failed.
    typedef bool (*ApplyServiceConfigCallback)(void* user_data);

    Request(grpc_call_stack* owning_call, grpc_call_combiner* call_combiner,
            grpc_polling_entity* pollent,
            grpc_metadata_batch* send_initial_metadata,
            uint32_t* send_initial_metadata_flags,
            ApplyServiceConfigCallback apply_service_config,
            void* apply_service_config_user_data, grpc_closure* on_route_done);

    ~Request();

    // TODO(roth): It seems a bit ugly to expose this member in a
    // non-const way.  Find a better API to avoid this.
    LoadBalancingPolicy::PickState* pick() { return &pick_; }

   private:
    friend class RequestRouter;

    class ResolverResultWaiter;
    class AsyncPickCanceller;

    void ProcessServiceConfigAndStartLbPickLocked();
    void StartLbPickLocked();
    static void LbPickDoneLocked(void* arg, grpc_error* error);

    void MaybeAddCallToInterestedPartiesLocked();
    void MaybeRemoveCallFromInterestedPartiesLocked();

    // Populated by caller.
    grpc_call_stack* owning_call_;
    grpc_call_combiner* call_combiner_;
    grpc_polling_entity* pollent_;
    ApplyServiceConfigCallback apply_service_config_;
    void* apply_service_config_user_data_;
    grpc_closure* on_route_done_;
    LoadBalancingPolicy::PickState pick_;

    // Internal state.
    RequestRouter* request_router_ = nullptr;
    bool pollent_added_to_interested_parties_ = false;
    grpc_closure on_pick_done_;
    AsyncPickCanceller* pick_canceller_ = nullptr;
  };

  // Synchronous callback that takes the service config JSON string and
  // LB policy name.
  // Returns true if the service config has changed since the last result.
  typedef bool (*ProcessResolverResultCallback)(void* user_data,
                                                const grpc_channel_args& args,
                                                const char** lb_policy_name,
                                                grpc_json** lb_policy_config);

  RequestRouter(grpc_channel_stack* owning_stack, grpc_combiner* combiner,
                grpc_client_channel_factory* client_channel_factory,
                grpc_pollset_set* interested_parties, TraceFlag* tracer,
                ProcessResolverResultCallback process_resolver_result,
                void* process_resolver_result_user_data, const char* target_uri,
                const grpc_channel_args* args, grpc_error** error);

  ~RequestRouter();

  void set_channelz_node(channelz::ClientChannelNode* channelz_node) {
    channelz_node_ = channelz_node;
  }

  void RouteCallLocked(Request* request);

  // TODO(roth): Add methods to cancel picks.

  void ShutdownLocked(grpc_error* error);

  void ExitIdleLocked();
  void ResetConnectionBackoffLocked();

  grpc_connectivity_state GetConnectivityState();
  void NotifyOnConnectivityStateChange(grpc_connectivity_state* state,
                                       grpc_closure* closure);

  LoadBalancingPolicy* lb_policy() const { return lb_policy_.get(); }

 private:
  using TraceStringVector = InlinedVector<char*, 3>;

  class ReresolutionRequestHandler;
  class LbConnectivityWatcher;

  void StartResolvingLocked();
  void OnResolverShutdownLocked(grpc_error* error);
  void CreateNewLbPolicyLocked(const char* lb_policy_name, grpc_json* lb_config,
                               grpc_connectivity_state* connectivity_state,
                               grpc_error** connectivity_error,
                               TraceStringVector* trace_strings);
  void MaybeAddTraceMessagesForAddressChangesLocked(
      TraceStringVector* trace_strings);
  void ConcatenateAndAddChannelTraceLocked(
      TraceStringVector* trace_strings) const;
  static void OnResolverResultChangedLocked(void* arg, grpc_error* error);

  void SetConnectivityStateLocked(grpc_connectivity_state state,
                                  grpc_error* error, const char* reason);

  // Passed in from caller at construction time.
  grpc_channel_stack* owning_stack_;
  grpc_combiner* combiner_;
  grpc_client_channel_factory* client_channel_factory_;
  grpc_pollset_set* interested_parties_;
  TraceFlag* tracer_;

  channelz::ClientChannelNode* channelz_node_ = nullptr;

  // Resolver and associated state.
  OrphanablePtr<Resolver> resolver_;
  ProcessResolverResultCallback process_resolver_result_;
  void* process_resolver_result_user_data_;
  bool started_resolving_ = false;
  grpc_channel_args* resolver_result_ = nullptr;
  bool previous_resolution_contained_addresses_ = false;
  grpc_closure_list waiting_for_resolver_result_closures_;
  grpc_closure on_resolver_result_changed_;

  // LB policy and associated state.
  OrphanablePtr<LoadBalancingPolicy> lb_policy_;
  bool exit_idle_when_lb_policy_arrives_ = false;

  // Subchannel pool to pass to LB policy.
  RefCountedPtr<SubchannelPoolInterface> subchannel_pool_;

  grpc_connectivity_state_tracker state_tracker_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_REQUEST_ROUTING_H */
