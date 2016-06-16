/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/ext/client_config/lb_policy.h"

#define WEAK_REF_BITS 16

void grpc_lb_policy_init(grpc_lb_policy *policy,
                         const grpc_lb_policy_vtable *vtable) {
  policy->vtable = vtable;
  gpr_atm_no_barrier_store(&policy->ref_pair, 1 << WEAK_REF_BITS);
  policy->interested_parties = grpc_pollset_set_create();
}

#ifdef GRPC_LB_POLICY_REFCOUNT_DEBUG
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
#ifdef GRPC_LB_POLICY_REFCOUNT_DEBUG
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "LB_POLICY: 0x%" PRIxPTR " %12s 0x%" PRIxPTR " -> 0x%" PRIxPTR
          " [%s]",
          (intptr_t)c, purpose, old_val, old_val + delta, reason);
#endif
  return old_val;
}

void grpc_lb_policy_ref(grpc_lb_policy *policy REF_FUNC_EXTRA_ARGS) {
  ref_mutate(policy, 1 << WEAK_REF_BITS, 0 REF_MUTATE_PASS_ARGS("STRONG_REF"));
}

void grpc_lb_policy_unref(grpc_exec_ctx *exec_ctx,
                          grpc_lb_policy *policy REF_FUNC_EXTRA_ARGS) {
  gpr_atm old_val =
      ref_mutate(policy, (gpr_atm)1 - (gpr_atm)(1 << WEAK_REF_BITS),
                 1 REF_MUTATE_PASS_ARGS("STRONG_UNREF"));
  gpr_atm mask = ~(gpr_atm)((1 << WEAK_REF_BITS) - 1);
  gpr_atm check = 1 << WEAK_REF_BITS;
  if ((old_val & mask) == check) {
    policy->vtable->shutdown(exec_ctx, policy);
  }
  grpc_lb_policy_weak_unref(exec_ctx,
                            policy REF_FUNC_PASS_ARGS("strong-unref"));
}

void grpc_lb_policy_weak_ref(grpc_lb_policy *policy REF_FUNC_EXTRA_ARGS) {
  ref_mutate(policy, 1, 0 REF_MUTATE_PASS_ARGS("WEAK_REF"));
}

void grpc_lb_policy_weak_unref(grpc_exec_ctx *exec_ctx,
                               grpc_lb_policy *policy REF_FUNC_EXTRA_ARGS) {
  gpr_atm old_val =
      ref_mutate(policy, -(gpr_atm)1, 1 REF_MUTATE_PASS_ARGS("WEAK_UNREF"));
  if (old_val == 1) {
    grpc_pollset_set_destroy(policy->interested_parties);
    policy->vtable->destroy(exec_ctx, policy);
  }
}

int grpc_lb_policy_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                        grpc_polling_entity *pollent,
                        grpc_metadata_batch *initial_metadata,
                        uint32_t initial_metadata_flags,
                        grpc_connected_subchannel **target,
                        grpc_closure *on_complete) {
  return policy->vtable->pick(exec_ctx, policy, pollent, initial_metadata,
                              initial_metadata_flags, target, on_complete);
}

void grpc_lb_policy_cancel_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                                grpc_connected_subchannel **target) {
  policy->vtable->cancel_pick(exec_ctx, policy, target);
}

void grpc_lb_policy_cancel_picks(grpc_exec_ctx *exec_ctx,
                                 grpc_lb_policy *policy,
                                 uint32_t initial_metadata_flags_mask,
                                 uint32_t initial_metadata_flags_eq) {
  policy->vtable->cancel_picks(exec_ctx, policy, initial_metadata_flags_mask,
                               initial_metadata_flags_eq);
}

void grpc_lb_policy_exit_idle(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy) {
  policy->vtable->exit_idle(exec_ctx, policy);
}

void grpc_lb_policy_ping_one(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                             grpc_closure *closure) {
  policy->vtable->ping_one(exec_ctx, policy, closure);
}

void grpc_lb_policy_notify_on_state_change(grpc_exec_ctx *exec_ctx,
                                           grpc_lb_policy *policy,
                                           grpc_connectivity_state *state,
                                           grpc_closure *closure) {
  policy->vtable->notify_on_state_change(exec_ctx, policy, state, closure);
}

grpc_connectivity_state grpc_lb_policy_check_connectivity(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
    grpc_error **connectivity_error) {
  return policy->vtable->check_connectivity(exec_ctx, policy,
                                            connectivity_error);
}
