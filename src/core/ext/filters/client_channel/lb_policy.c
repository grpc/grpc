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

#define WEAK_REF_BITS 16

#ifndef NDEBUG
grpc_tracer_flag grpc_trace_lb_policy_refcount =
    GRPC_TRACER_INITIALIZER(false, "lb_policy_refcount");
#endif

void grpc_lb_policy_init(grpc_lb_policy *policy,
                         const grpc_lb_policy_vtable *vtable,
                         grpc_combiner *combiner) {
  policy->vtable = vtable;
  gpr_atm_no_barrier_store(&policy->ref_pair, 1 << WEAK_REF_BITS);
  policy->interested_parties = grpc_pollset_set_create();
  policy->combiner = GRPC_COMBINER_REF(combiner, "lb_policy");
}

#ifndef NDEBUG
#define REF_FUNC_EXTRA_ARGS , const char *file, int line, const char *reason
#define REF_MUTATE_EXTRA_ARGS REF_FUNC_EXTRA_ARGS, const char *purpose
#define REF_FUNC_PASS_ARGS(new_reason) , file, line, new_reason
#define REF_MUTATE_PASS_ARGS(purpose) , file, line, reason, purpose
#else
#define REF_FUNC_EXTRA_ARGS
#define REF_MUTATE_EXTRA_ARGS
#define REF_FUNC_PASS_ARGS(new_reason)
#define REF_MUTATE_PASS_ARGS(x)
#endif

static gpr_atm ref_mutate(grpc_lb_policy *c, gpr_atm delta,
                          int barrier REF_MUTATE_EXTRA_ARGS) {
  gpr_atm old_val = barrier ? gpr_atm_full_fetch_add(&c->ref_pair, delta)
                            : gpr_atm_no_barrier_fetch_add(&c->ref_pair, delta);
#ifndef NDEBUG
  if (GRPC_TRACER_ON(grpc_trace_lb_policy_refcount)) {
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "LB_POLICY: %p %12s 0x%" PRIxPTR " -> 0x%" PRIxPTR " [%s]", c,
            purpose, old_val, old_val + delta, reason);
  }
#endif
  return old_val;
}

void grpc_lb_policy_ref(grpc_lb_policy *policy REF_FUNC_EXTRA_ARGS) {
  ref_mutate(policy, 1 << WEAK_REF_BITS, 0 REF_MUTATE_PASS_ARGS("STRONG_REF"));
}

static void shutdown_locked(grpc_exec_ctx *exec_ctx, void *arg,
                            grpc_error *error) {
  grpc_lb_policy *policy = arg;
  policy->vtable->shutdown_locked(exec_ctx, policy);
  GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, policy, "strong-unref");
}

void grpc_lb_policy_unref(grpc_exec_ctx *exec_ctx,
                          grpc_lb_policy *policy REF_FUNC_EXTRA_ARGS) {
  gpr_atm old_val =
      ref_mutate(policy, (gpr_atm)1 - (gpr_atm)(1 << WEAK_REF_BITS),
                 1 REF_MUTATE_PASS_ARGS("STRONG_UNREF"));
  gpr_atm mask = ~(gpr_atm)((1 << WEAK_REF_BITS) - 1);
  gpr_atm check = 1 << WEAK_REF_BITS;
  if ((old_val & mask) == check) {
    GRPC_CLOSURE_SCHED(exec_ctx, GRPC_CLOSURE_CREATE(
                                     shutdown_locked, policy,
                                     grpc_combiner_scheduler(policy->combiner)),
                       GRPC_ERROR_NONE);
  } else {
    grpc_lb_policy_weak_unref(exec_ctx,
                              policy REF_FUNC_PASS_ARGS("strong-unref"));
  }
}

void grpc_lb_policy_weak_ref(grpc_lb_policy *policy REF_FUNC_EXTRA_ARGS) {
  ref_mutate(policy, 1, 0 REF_MUTATE_PASS_ARGS("WEAK_REF"));
}

void grpc_lb_policy_weak_unref(grpc_exec_ctx *exec_ctx,
                               grpc_lb_policy *policy REF_FUNC_EXTRA_ARGS) {
  gpr_atm old_val =
      ref_mutate(policy, -(gpr_atm)1, 1 REF_MUTATE_PASS_ARGS("WEAK_UNREF"));
  if (old_val == 1) {
    grpc_pollset_set_destroy(exec_ctx, policy->interested_parties);
    grpc_combiner *combiner = policy->combiner;
    policy->vtable->destroy(exec_ctx, policy);
    GRPC_COMBINER_UNREF(exec_ctx, combiner, "lb_policy");
  }
}

int grpc_lb_policy_pick_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                               const grpc_lb_policy_pick_args *pick_args,
                               grpc_connected_subchannel **target,
                               grpc_call_context_element *context,
                               void **user_data, grpc_closure *on_complete) {
  return policy->vtable->pick_locked(exec_ctx, policy, pick_args, target,
                                     context, user_data, on_complete);
}

void grpc_lb_policy_cancel_pick_locked(grpc_exec_ctx *exec_ctx,
                                       grpc_lb_policy *policy,
                                       grpc_connected_subchannel **target,
                                       grpc_error *error) {
  policy->vtable->cancel_pick_locked(exec_ctx, policy, target, error);
}

void grpc_lb_policy_cancel_picks_locked(grpc_exec_ctx *exec_ctx,
                                        grpc_lb_policy *policy,
                                        uint32_t initial_metadata_flags_mask,
                                        uint32_t initial_metadata_flags_eq,
                                        grpc_error *error) {
  policy->vtable->cancel_picks_locked(exec_ctx, policy,
                                      initial_metadata_flags_mask,
                                      initial_metadata_flags_eq, error);
}

void grpc_lb_policy_exit_idle_locked(grpc_exec_ctx *exec_ctx,
                                     grpc_lb_policy *policy) {
  policy->vtable->exit_idle_locked(exec_ctx, policy);
}

void grpc_lb_policy_ping_one_locked(grpc_exec_ctx *exec_ctx,
                                    grpc_lb_policy *policy,
                                    grpc_closure *closure) {
  policy->vtable->ping_one_locked(exec_ctx, policy, closure);
}

void grpc_lb_policy_notify_on_state_change_locked(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
    grpc_connectivity_state *state, grpc_closure *closure) {
  policy->vtable->notify_on_state_change_locked(exec_ctx, policy, state,
                                                closure);
}

grpc_connectivity_state grpc_lb_policy_check_connectivity_locked(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
    grpc_error **connectivity_error) {
  return policy->vtable->check_connectivity_locked(exec_ctx, policy,
                                                   connectivity_error);
}

void grpc_lb_policy_update_locked(grpc_exec_ctx *exec_ctx,
                                  grpc_lb_policy *policy,
                                  const grpc_lb_policy_args *lb_policy_args) {
  policy->vtable->update_locked(exec_ctx, policy, lb_policy_args);
}
