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

#include "src/core/client_config/lb_policy.h"

void grpc_lb_policy_init(grpc_lb_policy *policy,
                         const grpc_lb_policy_vtable *vtable) {
  policy->vtable = vtable;
  gpr_ref_init(&policy->refs, 1);
}

#ifdef GRPC_LB_POLICY_REFCOUNT_DEBUG
void grpc_lb_policy_ref(grpc_lb_policy *policy, const char *file, int line,
                        const char *reason) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "LB_POLICY:%p   ref %d -> %d %s",
          policy, (int)policy->refs.count, (int)policy->refs.count + 1, reason);
#else
void grpc_lb_policy_ref(grpc_lb_policy *policy) {
#endif
  gpr_ref(&policy->refs);
}

#ifdef GRPC_LB_POLICY_REFCOUNT_DEBUG
void grpc_lb_policy_unref(grpc_lb_policy *policy,
                          grpc_closure_list *closure_list, const char *file,
                          int line, const char *reason) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "LB_POLICY:%p unref %d -> %d %s",
          policy, (int)policy->refs.count, (int)policy->refs.count - 1, reason);
#else
void grpc_lb_policy_unref(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy) {
#endif
  if (gpr_unref(&policy->refs)) {
    policy->vtable->destroy(exec_ctx, policy);
  }
}

void grpc_lb_policy_shutdown(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy) {
  policy->vtable->shutdown(exec_ctx, policy);
}

int grpc_lb_policy_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                        grpc_pollset *pollset,
                        grpc_metadata_batch *initial_metadata,
                        grpc_connected_subchannel **target, grpc_closure *on_complete) {
  return policy->vtable->pick(exec_ctx, policy, pollset, initial_metadata,
                              target, on_complete);
}

void grpc_lb_policy_cancel_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                                grpc_connected_subchannel **target) {
  policy->vtable->cancel_pick(exec_ctx, policy, target);
}

void grpc_lb_policy_broadcast(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                              grpc_transport_op *op) {
  policy->vtable->broadcast(exec_ctx, policy, op);
}

void grpc_lb_policy_exit_idle(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy) {
  policy->vtable->exit_idle(exec_ctx, policy);
}

void grpc_lb_policy_notify_on_state_change(grpc_exec_ctx *exec_ctx,
                                           grpc_lb_policy *policy,
                                           grpc_connectivity_state *state,
                                           grpc_closure *closure) {
  policy->vtable->notify_on_state_change(exec_ctx, policy, state, closure);
}

grpc_connectivity_state grpc_lb_policy_check_connectivity(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy) {
  return policy->vtable->check_connectivity(exec_ctx, policy);
}
