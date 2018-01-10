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

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/lib/iomgr/combiner.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_lb_policy_refcount(
    false, "lb_policy_refcount");

void grpc_lb_policy_init(grpc_lb_policy* policy,
                         const grpc_lb_policy_vtable* vtable,
                         grpc_combiner* combiner) {
  policy->vtable = vtable;
  gpr_ref_init(&policy->refs, 1);
  policy->interested_parties = grpc_pollset_set_create();
  policy->combiner = GRPC_COMBINER_REF(combiner, "lb_policy");
}

#ifndef NDEBUG
void grpc_lb_policy_ref(grpc_lb_policy* lb_policy, const char* file, int line,
                        const char* reason) {
  if (grpc_trace_lb_policy_refcount.enabled()) {
    gpr_atm old_refs = gpr_atm_no_barrier_load(&lb_policy->refs.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "LB_POLICY:%p   ref %" PRIdPTR " -> %" PRIdPTR " %s", lb_policy,
            old_refs, old_refs + 1, reason);
  }
#else
void grpc_lb_policy_ref(grpc_lb_policy* lb_policy) {
#endif
  gpr_ref(&lb_policy->refs);
}

#ifndef NDEBUG
void grpc_lb_policy_unref(grpc_lb_policy* lb_policy, const char* file, int line,
                          const char* reason) {
  if (grpc_trace_lb_policy_refcount.enabled()) {
    gpr_atm old_refs = gpr_atm_no_barrier_load(&lb_policy->refs.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "LB_POLICY:%p unref %" PRIdPTR " -> %" PRIdPTR " %s", lb_policy,
            old_refs, old_refs - 1, reason);
  }
#else
void grpc_lb_policy_unref(grpc_lb_policy* lb_policy) {
#endif
  if (gpr_unref(&lb_policy->refs)) {
    grpc_pollset_set_destroy(lb_policy->interested_parties);
    grpc_combiner* combiner = lb_policy->combiner;
    lb_policy->vtable->destroy(lb_policy);
    GRPC_COMBINER_UNREF(combiner, "lb_policy");
  }
}

void grpc_lb_policy_shutdown_locked(grpc_lb_policy* policy,
                                    grpc_lb_policy* new_policy) {
  policy->vtable->shutdown_locked(policy, new_policy);
}

int grpc_lb_policy_pick_locked(grpc_lb_policy* policy,
                               grpc_lb_policy_pick_state* pick) {
  return policy->vtable->pick_locked(policy, pick);
}

void grpc_lb_policy_cancel_pick_locked(grpc_lb_policy* policy,
                                       grpc_lb_policy_pick_state* pick,
                                       grpc_error* error) {
  policy->vtable->cancel_pick_locked(policy, pick, error);
}

void grpc_lb_policy_cancel_picks_locked(grpc_lb_policy* policy,
                                        uint32_t initial_metadata_flags_mask,
                                        uint32_t initial_metadata_flags_eq,
                                        grpc_error* error) {
  policy->vtable->cancel_picks_locked(policy, initial_metadata_flags_mask,
                                      initial_metadata_flags_eq, error);
}

void grpc_lb_policy_exit_idle_locked(grpc_lb_policy* policy) {
  policy->vtable->exit_idle_locked(policy);
}

void grpc_lb_policy_ping_one_locked(grpc_lb_policy* policy,
                                    grpc_closure* on_initiate,
                                    grpc_closure* on_ack) {
  policy->vtable->ping_one_locked(policy, on_initiate, on_ack);
}

void grpc_lb_policy_notify_on_state_change_locked(
    grpc_lb_policy* policy, grpc_connectivity_state* state,
    grpc_closure* closure) {
  policy->vtable->notify_on_state_change_locked(policy, state, closure);
}

grpc_connectivity_state grpc_lb_policy_check_connectivity_locked(
    grpc_lb_policy* policy, grpc_error** connectivity_error) {
  return policy->vtable->check_connectivity_locked(policy, connectivity_error);
}

void grpc_lb_policy_update_locked(grpc_lb_policy* policy,
                                  const grpc_lb_policy_args* lb_policy_args) {
  policy->vtable->update_locked(policy, lb_policy_args);
}

void grpc_lb_policy_set_reresolve_closure_locked(
    grpc_lb_policy* policy, grpc_closure* request_reresolution) {
  policy->vtable->set_reresolve_closure_locked(policy, request_reresolution);
}

void grpc_lb_policy_try_reresolve(grpc_lb_policy* policy,
                                  grpc_core::TraceFlag* grpc_lb_trace,
                                  grpc_error* error) {
  if (policy->request_reresolution != nullptr) {
    GRPC_CLOSURE_SCHED(policy->request_reresolution, error);
    policy->request_reresolution = nullptr;
    if (grpc_lb_trace->enabled()) {
      gpr_log(GPR_DEBUG,
              "%s %p: scheduling re-resolution closure with error=%s.",
              grpc_lb_trace->name(), policy, grpc_error_string(error));
    }
  } else {
    if (grpc_lb_trace->enabled() && error == GRPC_ERROR_NONE) {
      gpr_log(GPR_DEBUG, "%s %p: re-resolution already in progress.",
              grpc_lb_trace->name(), policy);
    }
  }
}
