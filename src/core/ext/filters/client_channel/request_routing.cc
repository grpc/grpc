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
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/server_address.h"
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

//
// RequestRouter::Request::ResolverResultWaiter
//

// Handles waiting for a resolver result.
// Used only for the first call on an idle channel.
class RequestRouter::Request::ResolverResultWaiter {
 public:
  explicit ResolverResultWaiter(Request* request)
      : request_router_(request->request_router_),
        request_(request),
        tracer_enabled_(request_router_->tracer_->enabled()) {
    if (tracer_enabled_) {
      gpr_log(GPR_INFO,
              "request_router=%p request=%p: deferring pick pending resolver "
              "result",
              request_router_, request);
    }
    // Add closure to be run when a resolver result is available.
    GRPC_CLOSURE_INIT(&done_closure_, &DoneLocked, this,
                      grpc_combiner_scheduler(request_router_->combiner_));
    AddToWaitingList();
    // Set cancellation closure, so that we abort if the call is cancelled.
    GRPC_CLOSURE_INIT(&cancel_closure_, &CancelLocked, this,
                      grpc_combiner_scheduler(request_router_->combiner_));
    grpc_call_combiner_set_notify_on_cancel(request->call_combiner_,
                                            &cancel_closure_);
  }

 private:
  // Adds done_closure_ to
  // request_router_->waiting_for_resolver_result_closures_.
  void AddToWaitingList() {
    grpc_closure_list_append(
        &request_router_->waiting_for_resolver_result_closures_, &done_closure_,
        GRPC_ERROR_NONE);
  }

  // Invoked when a resolver result is available.
  static void DoneLocked(void* arg, grpc_error* error) {
    ResolverResultWaiter* self = static_cast<ResolverResultWaiter*>(arg);
    RequestRouter* request_router = self->request_router_;
    // If CancelLocked() has already run, delete ourselves without doing
    // anything.  Note that the call stack may have already been destroyed,
    // so it's not safe to access anything in state_.
    if (GPR_UNLIKELY(self->finished_)) {
      if (self->tracer_enabled_) {
        gpr_log(GPR_INFO,
                "request_router=%p: call cancelled before resolver result",
                request_router);
      }
      Delete(self);
      return;
    }
    // Otherwise, process the resolver result.
    Request* request = self->request_;
    if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
      if (self->tracer_enabled_) {
        gpr_log(GPR_INFO,
                "request_router=%p request=%p: resolver failed to return data",
                request_router, request);
      }
      GRPC_CLOSURE_RUN(request->on_route_done_, GRPC_ERROR_REF(error));
    } else if (GPR_UNLIKELY(request_router->resolver_ == nullptr)) {
      // Shutting down.
      if (self->tracer_enabled_) {
        gpr_log(GPR_INFO, "request_router=%p request=%p: resolver disconnected",
                request_router, request);
      }
      GRPC_CLOSURE_RUN(request->on_route_done_,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Disconnected"));
    } else if (GPR_UNLIKELY(request_router->lb_policy_ == nullptr)) {
      // Transient resolver failure.
      // If call has wait_for_ready=true, try again; otherwise, fail.
      if (*request->pick_.initial_metadata_flags &
          GRPC_INITIAL_METADATA_WAIT_FOR_READY) {
        if (self->tracer_enabled_) {
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
        if (self->tracer_enabled_) {
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
      if (self->tracer_enabled_) {
        gpr_log(GPR_INFO,
                "request_router=%p request=%p: resolver returned, doing LB "
                "pick",
                request_router, request);
      }
      request->ProcessServiceConfigAndStartLbPickLocked();
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
    Request* request = self->request_;
    // If we are being cancelled, immediately invoke on_route_done_
    // to propagate the error back to the caller.
    if (error != GRPC_ERROR_NONE) {
      if (self->tracer_enabled_) {
        gpr_log(GPR_INFO,
                "request_router=%p request=%p: cancelling call waiting for "
                "name resolution",
                request_router, request);
      }
      // Note: Although we are not in the call combiner here, we are
      // basically stealing the call combiner from the pending pick, so
      // it's safe to run on_route_done_ here -- we are essentially
      // calling it here instead of calling it in DoneLocked().
      GRPC_CLOSURE_RUN(request->on_route_done_,
                       GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                           "Pick cancelled", &error, 1));
    }
    self->finished_ = true;
  }

  RequestRouter* request_router_;
  Request* request_;
  const bool tracer_enabled_;
  grpc_closure done_closure_;
  grpc_closure cancel_closure_;
  bool finished_ = false;
};

//
// RequestRouter::Request::AsyncPickCanceller
//

// Handles the call combiner cancellation callback for an async LB pick.
class RequestRouter::Request::AsyncPickCanceller {
 public:
  explicit AsyncPickCanceller(Request* request)
      : request_router_(request->request_router_),
        request_(request),
        tracer_enabled_(request_router_->tracer_->enabled()) {
    GRPC_CALL_STACK_REF(request->owning_call_, "pick_callback_cancel");
    // Set cancellation closure, so that we abort if the call is cancelled.
    GRPC_CLOSURE_INIT(&cancel_closure_, &CancelLocked, this,
                      grpc_combiner_scheduler(request_router_->combiner_));
    grpc_call_combiner_set_notify_on_cancel(request->call_combiner_,
                                            &cancel_closure_);
  }

  void MarkFinishedLocked() {
    finished_ = true;
    GRPC_CALL_STACK_UNREF(request_->owning_call_, "pick_callback_cancel");
  }

 private:
  // Invoked when the call is cancelled.
  // Note: This runs under the client_channel combiner, but will NOT be
  // holding the call combiner.
  static void CancelLocked(void* arg, grpc_error* error) {
    AsyncPickCanceller* self = static_cast<AsyncPickCanceller*>(arg);
    Request* request = self->request_;
    RequestRouter* request_router = self->request_router_;
    if (!self->finished_) {
      // Note: request_router->lb_policy_ may have changed since we started our
      // pick, in which case we will be cancelling the pick on a policy other
      // than the one we started it on.  However, this will just be a no-op.
      if (error != GRPC_ERROR_NONE && request_router->lb_policy_ != nullptr) {
        if (self->tracer_enabled_) {
          gpr_log(GPR_INFO,
                  "request_router=%p request=%p: cancelling pick from LB "
                  "policy %p",
                  request_router, request, request_router->lb_policy_.get());
        }
        request_router->lb_policy_->CancelPickLocked(&request->pick_,
                                                     GRPC_ERROR_REF(error));
      }
      request->pick_canceller_ = nullptr;
      GRPC_CALL_STACK_UNREF(request->owning_call_, "pick_callback_cancel");
    }
    Delete(self);
  }

  RequestRouter* request_router_;
  Request* request_;
  const bool tracer_enabled_;
  grpc_closure cancel_closure_;
  bool finished_ = false;
};

//
// RequestRouter::Request
//

RequestRouter::Request::Request(grpc_call_stack* owning_call,
                                grpc_call_combiner* call_combiner,
                                grpc_polling_entity* pollent,
                                grpc_metadata_batch* send_initial_metadata,
                                uint32_t* send_initial_metadata_flags,
                                ApplyServiceConfigCallback apply_service_config,
                                void* apply_service_config_user_data,
                                grpc_closure* on_route_done)
    : owning_call_(owning_call),
      call_combiner_(call_combiner),
      pollent_(pollent),
      apply_service_config_(apply_service_config),
      apply_service_config_user_data_(apply_service_config_user_data),
      on_route_done_(on_route_done) {
  pick_.initial_metadata = send_initial_metadata;
  pick_.initial_metadata_flags = send_initial_metadata_flags;
}

RequestRouter::Request::~Request() {
  if (pick_.connected_subchannel != nullptr) {
    pick_.connected_subchannel.reset();
  }
  for (size_t i = 0; i < GRPC_CONTEXT_COUNT; ++i) {
    if (pick_.subchannel_call_context[i].destroy != nullptr) {
      pick_.subchannel_call_context[i].destroy(
          pick_.subchannel_call_context[i].value);
    }
  }
}

// Invoked once resolver results are available.
void RequestRouter::Request::ProcessServiceConfigAndStartLbPickLocked() {
  // Get service config data if needed.
  if (!apply_service_config_(apply_service_config_user_data_)) return;
  // Start LB pick.
  StartLbPickLocked();
}

void RequestRouter::Request::MaybeAddCallToInterestedPartiesLocked() {
  if (!pollent_added_to_interested_parties_) {
    pollent_added_to_interested_parties_ = true;
    grpc_polling_entity_add_to_pollset_set(
        pollent_, request_router_->interested_parties_);
  }
}

void RequestRouter::Request::MaybeRemoveCallFromInterestedPartiesLocked() {
  if (pollent_added_to_interested_parties_) {
    pollent_added_to_interested_parties_ = false;
    grpc_polling_entity_del_from_pollset_set(
        pollent_, request_router_->interested_parties_);
  }
}

// Starts a pick on the LB policy.
void RequestRouter::Request::StartLbPickLocked() {
  if (request_router_->tracer_->enabled()) {
    gpr_log(GPR_INFO,
            "request_router=%p request=%p: starting pick on lb_policy=%p",
            request_router_, this, request_router_->lb_policy_.get());
  }
  GRPC_CLOSURE_INIT(&on_pick_done_, &LbPickDoneLocked, this,
                    grpc_combiner_scheduler(request_router_->combiner_));
  pick_.on_complete = &on_pick_done_;
  GRPC_CALL_STACK_REF(owning_call_, "pick_callback");
  grpc_error* error = GRPC_ERROR_NONE;
  const bool pick_done =
      request_router_->lb_policy_->PickLocked(&pick_, &error);
  if (pick_done) {
    // Pick completed synchronously.
    if (request_router_->tracer_->enabled()) {
      gpr_log(GPR_INFO,
              "request_router=%p request=%p: pick completed synchronously",
              request_router_, this);
    }
    GRPC_CLOSURE_RUN(on_route_done_, error);
    GRPC_CALL_STACK_UNREF(owning_call_, "pick_callback");
  } else {
    // Pick will be returned asynchronously.
    // Add the request's polling entity to the request_router's
    // interested_parties, so that the I/O of the LB policy can be done
    // under it.  It will be removed in LbPickDoneLocked().
    MaybeAddCallToInterestedPartiesLocked();
    // Request notification on call cancellation.
    // We allocate a separate object to track cancellation, since the
    // cancellation closure might still be pending when we need to reuse
    // the memory in which this Request object is stored for a subsequent
    // retry attempt.
    pick_canceller_ = New<AsyncPickCanceller>(this);
  }
}

// Callback invoked by LoadBalancingPolicy::PickLocked() for async picks.
// Unrefs the LB policy and invokes on_route_done_.
void RequestRouter::Request::LbPickDoneLocked(void* arg, grpc_error* error) {
  Request* self = static_cast<Request*>(arg);
  RequestRouter* request_router = self->request_router_;
  if (request_router->tracer_->enabled()) {
    gpr_log(GPR_INFO,
            "request_router=%p request=%p: pick completed asynchronously",
            request_router, self);
  }
  self->MaybeRemoveCallFromInterestedPartiesLocked();
  if (self->pick_canceller_ != nullptr) {
    self->pick_canceller_->MarkFinishedLocked();
  }
  GRPC_CLOSURE_RUN(self->on_route_done_, GRPC_ERROR_REF(error));
  GRPC_CALL_STACK_UNREF(self->owning_call_, "pick_callback");
}

//
// RequestRouter::LbConnectivityWatcher
//

class RequestRouter::LbConnectivityWatcher {
 public:
  LbConnectivityWatcher(RequestRouter* request_router,
                        grpc_connectivity_state state,
                        LoadBalancingPolicy* lb_policy,
                        grpc_channel_stack* owning_stack,
                        grpc_combiner* combiner)
      : request_router_(request_router),
        state_(state),
        lb_policy_(lb_policy),
        owning_stack_(owning_stack) {
    GRPC_CHANNEL_STACK_REF(owning_stack_, "LbConnectivityWatcher");
    GRPC_CLOSURE_INIT(&on_changed_, &OnLbPolicyStateChangedLocked, this,
                      grpc_combiner_scheduler(combiner));
    lb_policy_->NotifyOnStateChangeLocked(&state_, &on_changed_);
  }

  ~LbConnectivityWatcher() {
    GRPC_CHANNEL_STACK_UNREF(owning_stack_, "LbConnectivityWatcher");
  }

 private:
  static void OnLbPolicyStateChangedLocked(void* arg, grpc_error* error) {
    LbConnectivityWatcher* self = static_cast<LbConnectivityWatcher*>(arg);
    // If the notification is not for the current policy, we're stale,
    // so delete ourselves.
    if (self->lb_policy_ != self->request_router_->lb_policy_.get()) {
      Delete(self);
      return;
    }
    // Otherwise, process notification.
    if (self->request_router_->tracer_->enabled()) {
      gpr_log(GPR_INFO, "request_router=%p: lb_policy=%p state changed to %s",
              self->request_router_, self->lb_policy_,
              grpc_connectivity_state_name(self->state_));
    }
    self->request_router_->SetConnectivityStateLocked(
        self->state_, GRPC_ERROR_REF(error), "lb_changed");
    // If shutting down, terminate watch.
    if (self->state_ == GRPC_CHANNEL_SHUTDOWN) {
      Delete(self);
      return;
    }
    // Renew watch.
    self->lb_policy_->NotifyOnStateChangeLocked(&self->state_,
                                                &self->on_changed_);
  }

  RequestRouter* request_router_;
  grpc_connectivity_state state_;
  // LB policy address. No ref held, so not safe to dereference unless
  // it happens to match request_router->lb_policy_.
  LoadBalancingPolicy* lb_policy_;
  grpc_channel_stack* owning_stack_;
  grpc_closure on_changed_;
};

//
// RequestRounter::ReresolutionRequestHandler
//

class RequestRouter::ReresolutionRequestHandler {
 public:
  ReresolutionRequestHandler(RequestRouter* request_router,
                             LoadBalancingPolicy* lb_policy,
                             grpc_channel_stack* owning_stack,
                             grpc_combiner* combiner)
      : request_router_(request_router),
        lb_policy_(lb_policy),
        owning_stack_(owning_stack) {
    GRPC_CHANNEL_STACK_REF(owning_stack_, "ReresolutionRequestHandler");
    GRPC_CLOSURE_INIT(&closure_, &OnRequestReresolutionLocked, this,
                      grpc_combiner_scheduler(combiner));
    lb_policy_->SetReresolutionClosureLocked(&closure_);
  }

 private:
  static void OnRequestReresolutionLocked(void* arg, grpc_error* error) {
    ReresolutionRequestHandler* self =
        static_cast<ReresolutionRequestHandler*>(arg);
    RequestRouter* request_router = self->request_router_;
    // If this invocation is for a stale LB policy, treat it as an LB shutdown
    // signal.
    if (self->lb_policy_ != request_router->lb_policy_.get() ||
        error != GRPC_ERROR_NONE || request_router->resolver_ == nullptr) {
      GRPC_CHANNEL_STACK_UNREF(request_router->owning_stack_,
                               "ReresolutionRequestHandler");
      Delete(self);
      return;
    }
    if (request_router->tracer_->enabled()) {
      gpr_log(GPR_INFO, "request_router=%p: started name re-resolving",
              request_router);
    }
    request_router->resolver_->RequestReresolutionLocked();
    // Give back the closure to the LB policy.
    self->lb_policy_->SetReresolutionClosureLocked(&self->closure_);
  }

  RequestRouter* request_router_;
  // LB policy address. No ref held, so not safe to dereference unless
  // it happens to match request_router->lb_policy_.
  LoadBalancingPolicy* lb_policy_;
  grpc_channel_stack* owning_stack_;
  grpc_closure closure_;
};

//
// RequestRouter
//

RequestRouter::RequestRouter(
    grpc_channel_stack* owning_stack, grpc_combiner* combiner,
    grpc_client_channel_factory* client_channel_factory,
    grpc_pollset_set* interested_parties, TraceFlag* tracer,
    ProcessResolverResultCallback process_resolver_result,
    void* process_resolver_result_user_data, const char* target_uri,
    const grpc_channel_args* args, grpc_error** error)
    : owning_stack_(owning_stack),
      combiner_(combiner),
      client_channel_factory_(client_channel_factory),
      interested_parties_(interested_parties),
      tracer_(tracer),
      process_resolver_result_(process_resolver_result),
      process_resolver_result_user_data_(process_resolver_result_user_data) {
  GRPC_CLOSURE_INIT(&on_resolver_result_changed_,
                    &RequestRouter::OnResolverResultChangedLocked, this,
                    grpc_combiner_scheduler(combiner));
  grpc_connectivity_state_init(&state_tracker_, GRPC_CHANNEL_IDLE,
                               "request_router");
  grpc_channel_args* new_args = nullptr;
  if (process_resolver_result == nullptr) {
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

namespace {

const char* GetChannelConnectivityStateChangeString(
    grpc_connectivity_state state) {
  switch (state) {
    case GRPC_CHANNEL_IDLE:
      return "Channel state change to IDLE";
    case GRPC_CHANNEL_CONNECTING:
      return "Channel state change to CONNECTING";
    case GRPC_CHANNEL_READY:
      return "Channel state change to READY";
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      return "Channel state change to TRANSIENT_FAILURE";
    case GRPC_CHANNEL_SHUTDOWN:
      return "Channel state change to SHUTDOWN";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

}  // namespace

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
  if (channelz_node_ != nullptr) {
    channelz_node_->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string(
            GetChannelConnectivityStateChangeString(state)));
  }
  grpc_connectivity_state_set(&state_tracker_, state, error, reason);
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
              "request_router=%p: spontaneous shutdown from resolver %p", this,
              resolver_.get());
    }
    resolver_.reset();
    SetConnectivityStateLocked(GRPC_CHANNEL_SHUTDOWN,
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

// Creates a new LB policy, replacing any previous one.
// If the new policy is created successfully, sets *connectivity_state and
// *connectivity_error to its initial connectivity state; otherwise,
// leaves them unchanged.
void RequestRouter::CreateNewLbPolicyLocked(
    const char* lb_policy_name, grpc_json* lb_config,
    grpc_connectivity_state* connectivity_state,
    grpc_error** connectivity_error, TraceStringVector* trace_strings) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner_;
  lb_policy_args.client_channel_factory = client_channel_factory_;
  lb_policy_args.args = resolver_result_;
  lb_policy_args.lb_config = lb_config;
  OrphanablePtr<LoadBalancingPolicy> new_lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(lb_policy_name,
                                                             lb_policy_args);
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
        gpr_log(GPR_INFO, "request_router=%p: shutting down lb_policy=%p", this,
                lb_policy_.get());
      }
      grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                       interested_parties_);
      lb_policy_->HandOffPendingPicksLocked(new_lb_policy.get());
    }
    lb_policy_ = std::move(new_lb_policy);
    grpc_pollset_set_add_pollset_set(lb_policy_->interested_parties(),
                                     interested_parties_);
    // Create re-resolution request handler for the new LB policy.  It
    // will delete itself when no longer needed.
    New<ReresolutionRequestHandler>(this, lb_policy_.get(), owning_stack_,
                                    combiner_);
    // Get the new LB policy's initial connectivity state and start a
    // connectivity watch.
    GRPC_ERROR_UNREF(*connectivity_error);
    *connectivity_state =
        lb_policy_->CheckConnectivityLocked(connectivity_error);
    if (exit_idle_when_lb_policy_arrives_) {
      lb_policy_->ExitIdleLocked();
      exit_idle_when_lb_policy_arrives_ = false;
    }
    // Create new watcher.  It will delete itself when done.
    New<LbConnectivityWatcher>(this, *connectivity_state, lb_policy_.get(),
                               owning_stack_, combiner_);
  }
}

void RequestRouter::MaybeAddTraceMessagesForAddressChangesLocked(
    TraceStringVector* trace_strings) {
  const ServerAddressList* addresses =
      FindServerAddressListChannelArg(resolver_result_);
  const bool resolution_contains_addresses =
      addresses != nullptr && addresses->size() > 0;
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
  RequestRouter* self = static_cast<RequestRouter*>(arg);
  if (self->tracer_->enabled()) {
    const char* disposition =
        self->resolver_result_ != nullptr
            ? ""
            : (error == GRPC_ERROR_NONE ? " (transient error)"
                                        : " (resolver shutdown)");
    gpr_log(GPR_INFO,
            "request_router=%p: got resolver result: resolver_result=%p "
            "error=%s%s",
            self, self->resolver_result_, grpc_error_string(error),
            disposition);
  }
  // Handle shutdown.
  if (error != GRPC_ERROR_NONE || self->resolver_ == nullptr) {
    self->OnResolverShutdownLocked(GRPC_ERROR_REF(error));
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
  if (self->resolver_result_ == nullptr) {
    if (self->tracer_->enabled()) {
      gpr_log(GPR_INFO, "request_router=%p: resolver transient failure", self);
    }
    // Don't override connectivity state if we already have an LB policy.
    if (self->lb_policy_ != nullptr) set_connectivity_state = false;
  } else {
    // Parse the resolver result.
    const char* lb_policy_name = nullptr;
    grpc_json* lb_policy_config = nullptr;
    const bool service_config_changed = self->process_resolver_result_(
        self->process_resolver_result_user_data_, *self->resolver_result_,
        &lb_policy_name, &lb_policy_config);
    GPR_ASSERT(lb_policy_name != nullptr);
    // Check to see if we're already using the right LB policy.
    const bool lb_policy_name_changed =
        self->lb_policy_ == nullptr ||
        strcmp(self->lb_policy_->name(), lb_policy_name) != 0;
    if (self->lb_policy_ != nullptr && !lb_policy_name_changed) {
      // Continue using the same LB policy.  Update with new addresses.
      if (self->tracer_->enabled()) {
        gpr_log(GPR_INFO,
                "request_router=%p: updating existing LB policy \"%s\" (%p)",
                self, lb_policy_name, self->lb_policy_.get());
      }
      self->lb_policy_->UpdateLocked(*self->resolver_result_, lb_policy_config);
      // No need to set the channel's connectivity state; the existing
      // watch on the LB policy will take care of that.
      set_connectivity_state = false;
    } else {
      // Instantiate new LB policy.
      self->CreateNewLbPolicyLocked(lb_policy_name, lb_policy_config,
                                    &connectivity_state, &connectivity_error,
                                    &trace_strings);
    }
    // Add channel trace event.
    if (self->channelz_node_ != nullptr) {
      if (service_config_changed) {
        // TODO(ncteisen): might be worth somehow including a snippet of the
        // config in the trace, at the risk of bloating the trace logs.
        trace_strings.push_back(gpr_strdup("Service config changed"));
      }
      self->MaybeAddTraceMessagesForAddressChangesLocked(&trace_strings);
      self->ConcatenateAndAddChannelTraceLocked(&trace_strings);
    }
    // Clean up.
    grpc_channel_args_destroy(self->resolver_result_);
    self->resolver_result_ = nullptr;
  }
  // Set the channel's connectivity state if needed.
  if (set_connectivity_state) {
    self->SetConnectivityStateLocked(connectivity_state, connectivity_error,
                                     "resolver_result");
  } else {
    GRPC_ERROR_UNREF(connectivity_error);
  }
  // Invoke closures that were waiting for results and renew the watch.
  GRPC_CLOSURE_LIST_SCHED(&self->waiting_for_resolver_result_closures_);
  self->resolver_->NextLocked(&self->resolver_result_,
                              &self->on_resolver_result_changed_);
}

void RequestRouter::RouteCallLocked(Request* request) {
  GPR_ASSERT(request->pick_.connected_subchannel == nullptr);
  request->request_router_ = this;
  if (lb_policy_ != nullptr) {
    // We already have resolver results, so process the service config
    // and start an LB pick.
    request->ProcessServiceConfigAndStartLbPickLocked();
  } else if (resolver_ == nullptr) {
    GRPC_CLOSURE_RUN(request->on_route_done_,
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING("Disconnected"));
  } else {
    // We do not yet have an LB policy, so wait for a resolver result.
    if (!started_resolving_) {
      StartResolvingLocked();
    }
    // Create a new waiter, which will delete itself when done.
    New<Request::ResolverResultWaiter>(request);
    // Add the request's polling entity to the request_router's
    // interested_parties, so that the I/O of the resolver can be done
    // under it.  It will be removed in LbPickDoneLocked().
    request->MaybeAddCallToInterestedPartiesLocked();
  }
}

void RequestRouter::ShutdownLocked(grpc_error* error) {
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

void RequestRouter::NotifyOnConnectivityStateChange(
    grpc_connectivity_state* state, grpc_closure* closure) {
  grpc_connectivity_state_notify_on_state_change(&state_tracker_, state,
                                                 closure);
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

void RequestRouter::ResetConnectionBackoffLocked() {
  if (resolver_ != nullptr) {
    resolver_->ResetBackoffLocked();
    resolver_->RequestReresolutionLocked();
  }
  if (lb_policy_ != nullptr) {
    lb_policy_->ResetBackoffLocked();
  }
}

}  // namespace grpc_core
