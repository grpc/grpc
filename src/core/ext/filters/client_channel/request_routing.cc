/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/request_routing.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/method_params.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/service_config.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_metadata.h"

namespace grpc_core {

namespace {

// FIXME: make this a class?
struct ReresolutionRequestArgs {
  RequestRouter* request_router;
  // LB policy address. Used for logging only; no ref held, so not
  // safe to dereference (may be deleted at any time).
  LoadBalancingPolicy* lb_policy;
  grpc_closure closure;
};

// FIXME: make this a class?
struct LbConnectivityWatchState {
  RequestRouter* request_router;
  grpc_closure on_changed;
  grpc_connectivity_state state;
  LoadBalancingPolicy* lb_policy;
};

}  // namespace

//
// RequestRouter::Request::ResolverResultWaiter
//

// Handles waiting for a resolver result.
// Used only for the first call on an idle channel.
class RequestRouter::Request::ResolverResultWaiter {
 public:
  explicit ResolverResultWaiter(State* state)
      : request_router_(state->request_router), state_(state) {
    if (request_router_->tracer_->enabled()) {
      gpr_log(GPR_INFO,
              "request_router=%p request=%p: deferring pick pending resolver "
              "result", request_router_, state->request);
    }
    // Add closure to be run when a resolver result is available.
    GRPC_CLOSURE_INIT(&done_closure_, &ResolverResultWaiter::DoneLocked, this,
                      grpc_combiner_scheduler(request_router_->combiner_));
    AddToWaitingList();
    // Set cancellation closure, so that we abort if the call is cancelled.
    GRPC_CLOSURE_INIT(&cancel_closure_, &ResolverResultWaiter::CancelLocked,
                      this,
                      grpc_combiner_scheduler(request_router_->combiner_));
    grpc_call_combiner_set_notify_on_cancel(state_->request->call_combiner_,
                                            &cancel_closure_);
  }

 private:
  // Adds done_closure_ to
  // request_router_->waiting_for_resolver_result_closures_.
  void AddToWaitingList() {
    grpc_closure_list_append(
        &request_router_->waiting_for_resolver_result_closures_,
        &done_closure_, GRPC_ERROR_NONE);
  }

  // Invoked when a resolver result is available.
  static void DoneLocked(void* arg, grpc_error* error) {
    ResolverResultWaiter* self = static_cast<ResolverResultWaiter*>(arg);
    RequestRouter* request_router = self->request_router_;
    // If CancelLocked() has already run, delete ourselves without doing
    // anything.  Note that the call stack may have already been destroyed,
    // so it's not safe to access anything in state_.
    if (GPR_UNLIKELY(self->finished_)) {
      if (request_router->tracer_->enabled()) {
        gpr_log(GPR_INFO,
                "request_router=%p: call cancelled before resolver result",
                request_router);
      }
      Delete(self);
      return;
    }
    // Otherwise, process the resolver result.
    Request* request = self->state_->request;
    if (error != GRPC_ERROR_NONE) {
      if (request_router->tracer_->enabled()) {
        gpr_log(GPR_INFO,
                "request_router=%p request=%p: resolver failed to return data",
                request_router, request);
      }
      GRPC_CLOSURE_RUN(request->on_route_done_, GRPC_ERROR_REF(error));
    } else if (request_router->resolver_ == nullptr) {
      // Shutting down.
      if (request_router->tracer_->enabled()) {
        gpr_log(GPR_INFO, "request_router=%p request=%p: resolver disconnected",
                request_router, request);
      }
      GRPC_CLOSURE_RUN(request->on_route_done_,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Disconnected"));
    } else if (request_router->lb_policy_ == nullptr) {
      // Transient resolver failure.
      // If call has wait_for_ready=true, try again; otherwise, fail.
      if (*request->pick_.initial_metadata_flags &
          GRPC_INITIAL_METADATA_WAIT_FOR_READY) {
        if (request_router->tracer_->enabled()) {
          gpr_log(GPR_INFO,
                  "request_router=%p request=%p: resolver returned but no LB "
                  "policy; wait_for_ready=true; trying again",
                  request_router, request);
        }
        // Re-add ourselves to the waiting list.
        self->AddToWaitingList();
        // Return early so that we don't set finished_ to true below.
        return;
      } else {
        if (request_router->tracer_->enabled()) {
          gpr_log(GPR_INFO,
                  "request_router=%p request=%p: resolver returned but no LB "
                  "policy; wait_for_ready=false; failing",
                  request_router, request);
        }
        GRPC_CLOSURE_RUN(
            request->on_route_done_,
            grpc_error_set_int(
                GRPC_ERROR_CREATE_FROM_STATIC_STRING("Name resolution failure"),
                GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
      }
    } else {
      if (request_router->tracer_->enabled()) {
        gpr_log(GPR_INFO,
                "request_router=%p request=%p: resolver returned, doing LB "
                "pick", request_router, request);
      }
      request->ProcessServiceConfigAndStartLbPickLocked(self->state_);
    }
    self->finished_ = true;
  }

  // Invoked when the call is cancelled.
  // Note: This runs under the client_channel combiner, but will NOT be
  // holding the call combiner.
  static void CancelLocked(void* arg, grpc_error* error) {
    ResolverResultWaiter* self = static_cast<ResolverResultWaiter*>(arg);
    RequestRouter* request_router = self->request_router_;
    // If DoneLocked() has already run, delete ourselves without doing anything.
    if (self->finished_) {
      Delete(self);
      return;
    }
    Request* request = self->state_->request;
    // If we are being cancelled, immediately invoke on_route_done_
    // to propagate the error back to the caller.
    if (error != GRPC_ERROR_NONE) {
      if (request_router->tracer_->enabled()) {
        gpr_log(GPR_INFO,
                "request_router=%p request=%p: cancelling call waiting for "
                "name resolution", request_router, request);
      }
      // Note: Although we are not in the call combiner here, we are
      // basically stealing the call combiner from the pending pick, so
      // it's safe to call pick_done_locked() here -- we are essentially
      // calling it here instead of calling it in DoneLocked().
      GRPC_CLOSURE_RUN(request->on_route_done_,
                       GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                           "Pick cancelled", &error, 1));
    }
    self->finished_ = true;
  }

  RequestRouter* request_router_;
  State* state_;
  grpc_closure done_closure_;
  grpc_closure cancel_closure_;
  bool finished_ = false;
};

//
// RequestRouter::Request
//

// Invoked once resolver results are available.
void RequestRouter::Request::ProcessServiceConfigAndStartLbPickLocked(
    State* state) {
  // Get service config data if needed.
  if (on_service_config_ != nullptr) {
    GRPC_CLOSURE_RUN(on_service_config_, GRPC_ERROR_NONE);
  }
  // Start LB pick.
  StartLbPickLocked(state);
}

void RequestRouter::Request::MaybeAddCallToInterestedPartiesLocked(
    State* state) {
  if (!pollent_added_to_interested_parties_ &&
      !state->pollent_added_to_interested_parties) {
    pollent_added_to_interested_parties_ = true;
    state->pollent_added_to_interested_parties = true;
    grpc_polling_entity_add_to_pollset_set(
        pollent_, state->request_router->interested_parties_);
  }
}

void RequestRouter::Request::MaybeRemoveCallFromInterestedPartiesLocked(
    State* state) {
  if (state->pollent_added_to_interested_parties) {
    pollent_added_to_interested_parties_ = false;
    state->pollent_added_to_interested_parties = false;
    grpc_polling_entity_del_from_pollset_set(
        pollent_, state->request_router->interested_parties_);
  }
}

// Starts a pick on the LB policy.
void RequestRouter::Request::StartLbPickLocked(State* state) {
  RequestRouter* request_router = state->request_router;
  if (request_router->tracer_->enabled()) {
    gpr_log(GPR_INFO,
            "request_router=%p request=%p: starting pick on lb_policy=%p",
            request_router, this, request_router->lb_policy_.get());
  }
  GRPC_CLOSURE_INIT(&state->on_pick_done, &Request::LbPickDoneLocked, state,
                    grpc_combiner_scheduler(request_router->combiner_));
  pick_.on_complete = &state->on_pick_done;
  GRPC_CALL_STACK_REF(owning_call_, "pick_callback");
  grpc_error* error = GRPC_ERROR_NONE;
  const bool pick_done = request_router->lb_policy_->PickLocked(&pick_, &error);
  if (pick_done) {
    // Pick completed synchronously.
    if (request_router->tracer_->enabled()) {
      gpr_log(GPR_INFO,
              "request_router=%p request=%p: pick completed synchronously",
              request_router, this);
    }
    GRPC_CLOSURE_RUN(on_route_done_, error);
    GRPC_CALL_STACK_UNREF(owning_call_, "pick_callback");
  } else {
    // Pick will be returned asynchronously.
    // Add the request's polling entity to the request_router's
    // interested_parties, so that the I/O of the LB policy can be done
    // under it.  It will be removed in LbPickDoneLocked().
    MaybeAddCallToInterestedPartiesLocked(state);
    // Request notification on call cancellation.
    GRPC_CALL_STACK_REF(owning_call_, "pick_callback_cancel");
    GRPC_CLOSURE_INIT(&state->on_cancel, &Request::LbPickCancelLocked, state,
                      grpc_combiner_scheduler(request_router->combiner_));
    grpc_call_combiner_set_notify_on_cancel(call_combiner_, &state->on_cancel);
  }
}

// Callback invoked by LoadBalancingPolicy::PickLocked() for async picks.
// Unrefs the LB policy and invokes on_route_done_.
void RequestRouter::Request::LbPickDoneLocked(void* arg, grpc_error* error) {
  State* state = static_cast<State*>(arg);
  Request* request = state->request;
  RequestRouter* request_router = state->request_router;
  if (request_router->tracer_->enabled()) {
    gpr_log(GPR_INFO,
            "request_router=%p request=%p: pick completed asynchronously",
            request_router, request);
  }
  request->MaybeRemoveCallFromInterestedPartiesLocked(state);
  GRPC_CLOSURE_RUN(request->on_route_done_, GRPC_ERROR_REF(error));
  GRPC_CALL_STACK_UNREF(request->owning_call_, "pick_callback");
}

// Note: This runs under the client_channel combiner, but will NOT be
// holding the call combiner.
void RequestRouter::Request::LbPickCancelLocked(void* arg, grpc_error* error) {
  State* state = static_cast<State*>(arg);
  Request* request = state->request;
  RequestRouter* request_router = state->request_router;
  // Note: request_router->lb_policy_ may have changed since we started our
  // pick, in which case we will be cancelling the pick on a policy other than
  // the one we started it on.  However, this will just be a no-op.
  if (error != GRPC_ERROR_NONE && request_router->lb_policy_ != nullptr) {
    if (request_router->tracer_->enabled()) {
      gpr_log(GPR_INFO,
              "request_router=%p request=%p: cancelling pick from LB policy %p",
              request_router, request, request_router->lb_policy_.get());
    }
    request_router->lb_policy_->CancelPickLocked(&request->pick_,
                                                 GRPC_ERROR_REF(error));
  }
  GRPC_CALL_STACK_UNREF(request->owning_call_, "pick_callback_cancel");
}

RequestRouter::Request::State* RequestRouter::Request::AddState(
    RequestRouter* request_router) {
  state_.emplace_back();
  State& state = state_[state_.size() - 1];
  state.request = this;
  state.request_router = request_router;
  return &state;
}

//
// RequestRouter
//

RequestRouter::RequestRouter(
    grpc_channel_stack* owning_stack, grpc_combiner* combiner,
    grpc_client_channel_factory* client_channel_factory,
    grpc_pollset_set* interested_parties, TraceFlag* tracer,
    channelz::ClientChannelNode* channelz_node,
    grpc_closure* on_resolver_result, bool request_service_config,
    const char* target_uri, grpc_channel_args* args, grpc_error** error)
    : owning_stack_(owning_stack), combiner_(combiner),
      client_channel_factory_(client_channel_factory),
      interested_parties_(interested_parties), tracer_(tracer),
      channelz_node_(channelz_node), on_resolver_result_(on_resolver_result) {
  GRPC_CLOSURE_INIT(&on_resolver_result_changed_,
                    &RequestRouter::OnResolverResultChangedLocked, this,
                    grpc_combiner_scheduler(combiner));
  grpc_connectivity_state_init(&state_tracker_, GRPC_CHANNEL_IDLE,
                               "request_router");
  grpc_channel_args* new_args = nullptr;
  if (!request_service_config) {
    grpc_arg arg = grpc_channel_arg_integer_create(
        const_cast<char*>(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION), 0);
    new_args = grpc_channel_args_copy_and_add(args, &arg, 1);
  }
  resolver_ = ResolverRegistry::CreateResolver(
      target_uri, (new_args == nullptr ? args : new_args), interested_parties_,
      combiner_);
  grpc_channel_args_destroy(new_args);
  if (resolver_ == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("resolver creation failed");
  }
}

RequestRouter::~RequestRouter() {
  if (resolver_ != nullptr) {
    // The only way we can get here is if we never started resolving,
    // because we take a ref to the channel stack when we start
    // resolving and do not release it until the resolver callback is
    // invoked after the resolver shuts down.
    resolver_.reset();
  }
  if (lb_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                     interested_parties_);
    lb_policy_.reset();
  }
  if (client_channel_factory_ != nullptr) {
    grpc_client_channel_factory_unref(client_channel_factory_);
  }
  grpc_connectivity_state_destroy(&state_tracker_);
}

void RequestRouter::SetConnectivityStateLocked(grpc_connectivity_state state,
                                               grpc_error* error,
                                               const char* reason) {
  if (lb_policy_ != nullptr) {
    if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      // Cancel picks with wait_for_ready=false.
      lb_policy_->CancelMatchingPicksLocked(
          /* mask= */ GRPC_INITIAL_METADATA_WAIT_FOR_READY,
          /* check= */ 0, GRPC_ERROR_REF(error));
    } else if (state == GRPC_CHANNEL_SHUTDOWN) {
      // Cancel all picks.
      lb_policy_->CancelMatchingPicksLocked(/* mask= */ 0, /* check= */ 0,
                                            GRPC_ERROR_REF(error));
    }
  }
  if (tracer_->enabled()) {
    gpr_log(GPR_INFO, "request_router=%p: setting connectivity state to %s",
            this, grpc_connectivity_state_name(state));
  }
  grpc_connectivity_state_set(&state_tracker_, state, error, reason);
}

void RequestRouter::OnLbPolicyStateChangedLocked(void* arg, grpc_error* error) {
  LbConnectivityWatchState* w = static_cast<LbConnectivityWatchState*>(arg);
  // Check if the notification is for the current LB policy.
  if (w->lb_policy == w->request_router->lb_policy_.get()) {
    if (w->request_router->tracer_->enabled()) {
      gpr_log(GPR_INFO, "request_router=%p: lb_policy=%p state changed to %s",
              w->request_router, w->lb_policy,
              grpc_connectivity_state_name(w->state));
    }
    w->request_router->SetConnectivityStateLocked(
        w->state, GRPC_ERROR_REF(error), "lb_changed");
    if (w->state != GRPC_CHANNEL_SHUTDOWN) {
      w->request_router->WatchLbPolicyLocked(w->state);
    }
  }
// FIXME: don't free every time!  reuse the same object unless we've
// switched LB policies
  GRPC_CHANNEL_STACK_UNREF(w->request_router->owning_stack_, "watch_lb_policy");
  Delete(w);
}

void RequestRouter::WatchLbPolicyLocked(grpc_connectivity_state current_state) {
  LbConnectivityWatchState* w = New<LbConnectivityWatchState>();
  GRPC_CHANNEL_STACK_REF(owning_stack_, "watch_lb_policy");
  w->request_router = this;
  GRPC_CLOSURE_INIT(&w->on_changed,
                    &RequestRouter::OnLbPolicyStateChangedLocked, w,
                    grpc_combiner_scheduler(combiner_));
  w->state = current_state;
  w->lb_policy = lb_policy_.get();
  lb_policy_->NotifyOnStateChangeLocked(&w->state, &w->on_changed);
}

void RequestRouter::StartResolvingLocked() {
  if (tracer_->enabled()) {
    gpr_log(GPR_INFO, "request_router=%p: starting name resolution", this);
  }
  GPR_ASSERT(!started_resolving_);
  started_resolving_ = true;
  GRPC_CHANNEL_STACK_REF(owning_stack_, "resolver");
  resolver_->NextLocked(&resolver_result_, &on_resolver_result_changed_);
}

// Invoked from the resolver NextLocked() callback when the resolver
// is shutting down.
void RequestRouter::OnResolverShutdownLocked(grpc_error* error) {
  if (tracer_->enabled()) {
    gpr_log(GPR_INFO, "request_router=%p: shutting down", this);
  }
  if (lb_policy_ != nullptr) {
    if (tracer_->enabled()) {
      gpr_log(GPR_INFO, "request_router=%p: shutting down lb_policy=%p", this,
              lb_policy_.get());
    }
    grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                     interested_parties_);
    lb_policy_.reset();
  }
  if (resolver_ != nullptr) {
    // This should never happen; it can only be triggered by a resolver
    // implementation spotaneously deciding to report shutdown without
    // being orphaned.  This code is included just to be defensive.
    if (tracer_->enabled()) {
      gpr_log(GPR_INFO,
              "request_router=%p: spontaneous shutdown from resolver %p",
              this, resolver_.get());
    }
    resolver_.reset();
    SetConnectivityStateLocked(
        GRPC_CHANNEL_SHUTDOWN,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "Resolver spontaneous shutdown", &error, 1),
        "resolver_spontaneous_shutdown");
  }
  grpc_closure_list_fail_all(&waiting_for_resolver_result_closures_,
                             GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                 "Channel disconnected", &error, 1));
  GRPC_CLOSURE_LIST_SCHED(&waiting_for_resolver_result_closures_);
  GRPC_CHANNEL_STACK_UNREF(owning_stack_, "resolver");
  grpc_channel_args_destroy(resolver_result_);
  resolver_result_ = nullptr;
  GRPC_ERROR_UNREF(error);
}

// Returns the LB policy name from the resolver result.
const char* RequestRouter::GetLbPolicyNameFromResolverResultLocked() {
  // Find LB policy name in channel args.
  const grpc_arg* channel_arg =
      grpc_channel_args_find(resolver_result_, GRPC_ARG_LB_POLICY_NAME);
  const char* lb_policy_name = grpc_channel_arg_get_string(channel_arg);
  // Special case: If at least one balancer address is present, we use
  // the grpclb policy, regardless of what the resolver actually specified.
  channel_arg =
      grpc_channel_args_find(resolver_result_, GRPC_ARG_LB_ADDRESSES);
  if (channel_arg != nullptr && channel_arg->type == GRPC_ARG_POINTER) {
    grpc_lb_addresses* addresses =
        static_cast<grpc_lb_addresses*>(channel_arg->value.pointer.p);
    if (grpc_lb_addresses_contains_balancer_address(*addresses)) {
      if (lb_policy_name != nullptr &&
          gpr_stricmp(lb_policy_name, "grpclb") != 0) {
        gpr_log(GPR_INFO,
                "resolver requested LB policy %s but provided at least one "
                "balancer address -- forcing use of grpclb LB policy",
                lb_policy_name);
      }
      lb_policy_name = "grpclb";
    }
  }
  // Use pick_first if nothing was specified and we didn't select grpclb
  // above.
  if (lb_policy_name == nullptr) lb_policy_name = "pick_first";
  return lb_policy_name;
}

void RequestRouter::OnRequestReresolutionLocked(void* arg, grpc_error* error) {
  ReresolutionRequestArgs* args = static_cast<ReresolutionRequestArgs*>(arg);
  RequestRouter* request_router = args->request_router;
  // If this invocation is for a stale LB policy, treat it as an LB shutdown
  // signal.
  if (args->lb_policy != request_router->lb_policy_.get() ||
      error != GRPC_ERROR_NONE || request_router->resolver_ == nullptr) {
    GRPC_CHANNEL_STACK_UNREF(request_router->owning_stack_, "re-resolution");
    Delete(args);
    return;
  }
  if (request_router->tracer_->enabled()) {
    gpr_log(GPR_INFO, "request_router=%p: started name re-resolving",
            request_router);
  }
  request_router->resolver_->RequestReresolutionLocked();
  // Give back the closure to the LB policy.
  request_router->lb_policy_->SetReresolutionClosureLocked(&args->closure);
}

// Creates a new LB policy, replacing any previous one.
// If the new policy is created successfully, sets *connectivity_state and
// *connectivity_error to its initial connectivity state; otherwise,
// leaves them unchanged.
void RequestRouter::CreateNewLbPolicyLocked(
    const char* lb_policy_name, grpc_connectivity_state* connectivity_state,
    grpc_error** connectivity_error, TraceStringVector* trace_strings) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner_;
  lb_policy_args.client_channel_factory = client_channel_factory_;
  lb_policy_args.args = resolver_result_;
  OrphanablePtr<LoadBalancingPolicy> new_lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          lb_policy_name, lb_policy_args);
  if (GPR_UNLIKELY(new_lb_policy == nullptr)) {
    gpr_log(GPR_ERROR, "could not create LB policy \"%s\"", lb_policy_name);
    if (channelz_node_ != nullptr) {
      char* str;
      gpr_asprintf(&str, "Could not create LB policy \'%s\'", lb_policy_name);
      trace_strings->push_back(str);
    }
  } else {
    if (tracer_->enabled()) {
      gpr_log(GPR_INFO, "request_router=%p: created new LB policy \"%s\" (%p)",
              this, lb_policy_name, new_lb_policy.get());
    }
    if (channelz_node_ != nullptr) {
      char* str;
      gpr_asprintf(&str, "Created new LB policy \'%s\'", lb_policy_name);
      trace_strings->push_back(str);
    }
    // Swap out the LB policy and update the fds in interested_parties_.
    if (lb_policy_ != nullptr) {
      if (tracer_->enabled()) {
        gpr_log(GPR_INFO, "request_router=%p: shutting down lb_policy=%p",
                this, lb_policy_.get());
      }
      grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                       interested_parties_);
      lb_policy_->HandOffPendingPicksLocked(new_lb_policy.get());
    }
    lb_policy_ = std::move(new_lb_policy);
    grpc_pollset_set_add_pollset_set(lb_policy_->interested_parties(),
                                     interested_parties_);
    // Set up re-resolution callback.
    ReresolutionRequestArgs* args = New<ReresolutionRequestArgs>();
    args->request_router = this;
    args->lb_policy = lb_policy_.get();
    GRPC_CLOSURE_INIT(&args->closure, &OnRequestReresolutionLocked, args,
                      grpc_combiner_scheduler(combiner_));
    GRPC_CHANNEL_STACK_REF(owning_stack_, "re-resolution");
    lb_policy_->SetReresolutionClosureLocked(&args->closure);
    // Get the new LB policy's initial connectivity state and start a
    // connectivity watch.
    GRPC_ERROR_UNREF(*connectivity_error);
    *connectivity_state =
        lb_policy_->CheckConnectivityLocked(connectivity_error);
    if (exit_idle_when_lb_policy_arrives_) {
      lb_policy_->ExitIdleLocked();
      exit_idle_when_lb_policy_arrives_ = false;
    }
    WatchLbPolicyLocked(*connectivity_state);
  }
}

void RequestRouter::MaybeAddTraceMessagesForAddressChangesLocked(
    TraceStringVector* trace_strings) {
  bool resolution_contains_addresses = false;
  const grpc_arg* channel_arg =
      grpc_channel_args_find(resolver_result_, GRPC_ARG_LB_ADDRESSES);
  if (channel_arg != nullptr && channel_arg->type == GRPC_ARG_POINTER) {
    grpc_lb_addresses* addresses =
        static_cast<grpc_lb_addresses*>(channel_arg->value.pointer.p);
    if (addresses->num_addresses > 0) {
      resolution_contains_addresses = true;
    }
  }
  if (!resolution_contains_addresses &&
      previous_resolution_contained_addresses_) {
    trace_strings->push_back(gpr_strdup("Address list became empty"));
  } else if (resolution_contains_addresses &&
             !previous_resolution_contained_addresses_) {
    trace_strings->push_back(gpr_strdup("Address list became non-empty"));
  }
  previous_resolution_contained_addresses_ = resolution_contains_addresses;
}

void RequestRouter::ConcatenateAndAddChannelTraceLocked(
    TraceStringVector* trace_strings) const {
  if (!trace_strings->empty()) {
    gpr_strvec v;
    gpr_strvec_init(&v);
    gpr_strvec_add(&v, gpr_strdup("Resolution event: "));
    bool is_first = 1;
    for (size_t i = 0; i < trace_strings->size(); ++i) {
      if (!is_first) gpr_strvec_add(&v, gpr_strdup(", "));
      is_first = false;
      gpr_strvec_add(&v, (*trace_strings)[i]);
    }
    char* flat;
    size_t flat_len = 0;
    flat = gpr_strvec_flatten(&v, &flat_len);
    channelz_node_->AddTraceEvent(
        grpc_core::channelz::ChannelTrace::Severity::Info,
        grpc_slice_new(flat, flat_len, gpr_free));
    gpr_strvec_destroy(&v);
  }
}

// Callback invoked when a resolver result is available.
void RequestRouter::OnResolverResultChangedLocked(void* arg,
                                                  grpc_error* error) {
  RequestRouter* request_router = static_cast<RequestRouter*>(arg);
  if (request_router->tracer_->enabled()) {
    const char* disposition =
        request_router->resolver_result_ != nullptr
            ? ""
            : (error == GRPC_ERROR_NONE ? " (transient error)"
                                        : " (resolver shutdown)");
    gpr_log(GPR_INFO,
            "request_router=%p: got resolver result: resolver_result=%p "
            "error=%s%s",
            request_router, request_router->resolver_result_,
            grpc_error_string(error), disposition);
  }
  // Handle shutdown.
  if (error != GRPC_ERROR_NONE || request_router->resolver_ == nullptr) {
    request_router->OnResolverShutdownLocked(GRPC_ERROR_REF(error));
    return;
  }
  // Data used to set the channel's connectivity state.
  bool set_connectivity_state = true;
  // We only want to trace the address resolution in the follow cases:
  // (a) Address resolution resulted in service config change.
  // (b) Address resolution that causes number of backends to go from
  //     zero to non-zero.
  // (c) Address resolution that causes number of backends to go from
  //     non-zero to zero.
  // (d) Address resolution that causes a new LB policy to be created.
  //
  // we track a list of strings to eventually be concatenated and traced.
  TraceStringVector trace_strings;
  grpc_connectivity_state connectivity_state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  grpc_error* connectivity_error =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("No load balancing policy");
  // resolver_result_ will be null in the case of a transient
  // resolution error.  In that case, we don't have any new result to
  // process, which means that we keep using the previous result (if any).
  if (request_router->resolver_result_ == nullptr) {
    if (request_router->tracer_->enabled()) {
      gpr_log(GPR_INFO, "request_router=%p: resolver transient failure",
              request_router);
    }
  } else {
    const char* lb_policy_name =
        request_router->GetLbPolicyNameFromResolverResultLocked();
    // Check to see if we're already using the right LB policy.
    const bool lb_policy_name_changed =
        request_router->lb_policy_ == nullptr ||
        gpr_stricmp(request_router->lb_policy_->name(), lb_policy_name) != 0;
    if (request_router->lb_policy_ != nullptr && !lb_policy_name_changed) {
      // Continue using the same LB policy.  Update with new addresses.
      if (request_router->tracer_->enabled()) {
        gpr_log(GPR_INFO,
                "request_router=%p: updating existing LB policy \"%s\" (%p)",
                request_router, lb_policy_name,
                request_router->lb_policy_.get());
      }
      request_router->lb_policy_->UpdateLocked(
          *request_router->resolver_result_);
      // No need to set the channel's connectivity state; the existing
      // watch on the LB policy will take care of that.
      set_connectivity_state = false;
    } else {
      // Instantiate new LB policy.
      request_router->CreateNewLbPolicyLocked(
          lb_policy_name, &connectivity_state, &connectivity_error,
          &trace_strings);
    }
// FIXME: do we need to handle service config update here, for the
// purposes of channel tracing?
    if (request_router->channelz_node_ != nullptr) {
      request_router->MaybeAddTraceMessagesForAddressChangesLocked(
          &trace_strings);
      request_router->ConcatenateAndAddChannelTraceLocked(&trace_strings);
    }
    // Invoke on_resolver_result_.
    GRPC_CLOSURE_RUN(request_router->on_resolver_result_, GRPC_ERROR_NONE);
    // Clean up.
    grpc_channel_args_destroy(request_router->resolver_result_);
    request_router->resolver_result_ = nullptr;
  }
  // Set the channel's connectivity state if needed.
  if (set_connectivity_state) {
    request_router->SetConnectivityStateLocked(
        connectivity_state, connectivity_error, "resolver_result");
  } else {
    GRPC_ERROR_UNREF(connectivity_error);
  }
  // Invoke closures that were waiting for results and renew the watch.
  GRPC_CLOSURE_LIST_SCHED(
      &request_router->waiting_for_resolver_result_closures_);
  request_router->resolver_->NextLocked(
      &request_router->resolver_result_,
      &request_router->on_resolver_result_changed_);
}

void RequestRouter::RouteCallLocked(Request* request) {
  GPR_ASSERT(request->pick_.connected_subchannel == nullptr);
  Request::State* state = request->AddState(this);
  if (lb_policy_ != nullptr) {
    // We already have resolver results, so process the service config
    // and start an LB pick.
    request->ProcessServiceConfigAndStartLbPickLocked(state);
  } else if (resolver_ == nullptr) {
    GRPC_CLOSURE_RUN(request->on_route_done_,
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING("Disconnected"));
  } else {
    // We do not yet have an LB policy, so wait for a resolver result.
    if (!started_resolving_) {
      StartResolvingLocked();
    }
    // Create a new waiter, which will delete itself when done.
    New<Request::ResolverResultWaiter>(state);
    // Add the request's polling entity to the request_router's
    // interested_parties, so that the I/O of the resolver can be done
    // under it.  It will be removed in LbPickDoneLocked().
    request->MaybeAddCallToInterestedPartiesLocked(state);
  }
}

void RequestRouter::Shutdown(grpc_error* error) {
  if (resolver_ != nullptr) {
    SetConnectivityStateLocked(GRPC_CHANNEL_SHUTDOWN, GRPC_ERROR_REF(error),
                               "disconnect");
    resolver_.reset();
    if (!started_resolving_) {
      grpc_closure_list_fail_all(&waiting_for_resolver_result_closures_,
                                 GRPC_ERROR_REF(error));
      GRPC_CLOSURE_LIST_SCHED(&waiting_for_resolver_result_closures_);
    }
    if (lb_policy_ != nullptr) {
      grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                       interested_parties_);
      lb_policy_.reset();
    }
  }
  GRPC_ERROR_UNREF(error);
}

grpc_connectivity_state RequestRouter::GetConnectivityState() {
  return grpc_connectivity_state_check(&state_tracker_);
}

void RequestRouter::ExitIdleLocked() {
  if (lb_policy_ != nullptr) {
    lb_policy_->ExitIdleLocked();
  } else {
    exit_idle_when_lb_policy_arrives_ = true;
    if (!started_resolving_ && resolver_ != nullptr) {
      StartResolvingLocked();
    }
  }
}

}  // namespace grpc_core
