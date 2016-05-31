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

#ifndef GRPC_CORE_EXT_CLIENT_CONFIG_LB_POLICY_H
#define GRPC_CORE_EXT_CLIENT_CONFIG_LB_POLICY_H

#include "src/core/ext/client_config/subchannel.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/transport/connectivity_state.h"

/** A load balancing policy: specified by a vtable and a struct (which
    is expected to be extended to contain some parameters) */
typedef struct grpc_lb_policy grpc_lb_policy;
typedef struct grpc_lb_policy_vtable grpc_lb_policy_vtable;

typedef void (*grpc_lb_completion)(void *cb_arg, grpc_subchannel *subchannel,
                                   grpc_status_code status, const char *errmsg);

struct grpc_lb_policy {
  const grpc_lb_policy_vtable *vtable;
  gpr_atm ref_pair;
  /* owned pointer to interested parties in load balancing decisions */
  grpc_pollset_set *interested_parties;
};

struct grpc_lb_policy_vtable {
  void (*destroy)(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy);

  void (*shutdown)(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy);

  /** implement grpc_lb_policy_pick */
  int (*pick)(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
              grpc_polling_entity *pollent,
              grpc_metadata_batch *initial_metadata,
              uint32_t initial_metadata_flags,
              grpc_connected_subchannel **target, grpc_closure *on_complete);
  void (*cancel_pick)(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                      grpc_connected_subchannel **target);
  void (*cancel_picks)(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                       uint32_t initial_metadata_flags_mask,
                       uint32_t initial_metadata_flags_eq);

  void (*ping_one)(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                   grpc_closure *closure);

  /** try to enter a READY connectivity state */
  void (*exit_idle)(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy);

  /** check the current connectivity of the lb_policy */
  grpc_connectivity_state (*check_connectivity)(grpc_exec_ctx *exec_ctx,
                                                grpc_lb_policy *policy);

  /** call notify when the connectivity state of a channel changes from *state.
      Updates *state with the new state of the policy */
  void (*notify_on_state_change)(grpc_exec_ctx *exec_ctx,
                                 grpc_lb_policy *policy,
                                 grpc_connectivity_state *state,
                                 grpc_closure *closure);
};

/*#define GRPC_LB_POLICY_REFCOUNT_DEBUG*/
#ifdef GRPC_LB_POLICY_REFCOUNT_DEBUG
#define GRPC_LB_POLICY_REF(p, r) \
  grpc_lb_policy_ref((p), __FILE__, __LINE__, (r))
#define GRPC_LB_POLICY_UNREF(exec_ctx, p, r) \
  grpc_lb_policy_unref((exec_ctx), (p), __FILE__, __LINE__, (r))
#define GRPC_LB_POLICY_WEAK_REF(p, r) \
  grpc_lb_policy_weak_ref((p), __FILE__, __LINE__, (r))
#define GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, p, r) \
  grpc_lb_policy_weak_unref((exec_ctx), (p), __FILE__, __LINE__, (r))
void grpc_lb_policy_ref(grpc_lb_policy *policy, const char *file, int line,
                        const char *reason);
void grpc_lb_policy_unref(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                          const char *file, int line, const char *reason);
void grpc_lb_policy_weak_ref(grpc_lb_policy *policy, const char *file, int line,
                             const char *reason);
void grpc_lb_policy_weak_unref(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                               const char *file, int line, const char *reason);
#else
#define GRPC_LB_POLICY_REF(p, r) grpc_lb_policy_ref((p))
#define GRPC_LB_POLICY_UNREF(cl, p, r) grpc_lb_policy_unref((cl), (p))
#define GRPC_LB_POLICY_WEAK_REF(p, r) grpc_lb_policy_weak_ref((p))
#define GRPC_LB_POLICY_WEAK_UNREF(cl, p, r) grpc_lb_policy_weak_unref((cl), (p))
void grpc_lb_policy_ref(grpc_lb_policy *policy);
void grpc_lb_policy_unref(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy);
void grpc_lb_policy_weak_ref(grpc_lb_policy *policy);
void grpc_lb_policy_weak_unref(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy);
#endif

/** called by concrete implementations to initialize the base struct */
void grpc_lb_policy_init(grpc_lb_policy *policy,
                         const grpc_lb_policy_vtable *vtable);

/** Given initial metadata in \a initial_metadata, find an appropriate
    target for this rpc, and 'return' it by calling \a on_complete after setting
    \a target.
    Picking can be asynchronous. Any IO should be done under \a pollset. */
int grpc_lb_policy_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                        grpc_polling_entity *pollent,
                        grpc_metadata_batch *initial_metadata,
                        uint32_t initial_metadata_flags,
                        grpc_connected_subchannel **target,
                        grpc_closure *on_complete);

void grpc_lb_policy_ping_one(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                             grpc_closure *closure);

void grpc_lb_policy_cancel_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                                grpc_connected_subchannel **target);

/** Cancel all pending picks which have:
    (initial_metadata_flags & initial_metadata_flags_mask) ==
         initial_metadata_flags_eq */
void grpc_lb_policy_cancel_picks(grpc_exec_ctx *exec_ctx,
                                 grpc_lb_policy *policy,
                                 uint32_t initial_metadata_flags_mask,
                                 uint32_t initial_metadata_flags_eq);

void grpc_lb_policy_exit_idle(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy);

void grpc_lb_policy_notify_on_state_change(grpc_exec_ctx *exec_ctx,
                                           grpc_lb_policy *policy,
                                           grpc_connectivity_state *state,
                                           grpc_closure *closure);

grpc_connectivity_state grpc_lb_policy_check_connectivity(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy);

#endif /* GRPC_CORE_EXT_CLIENT_CONFIG_LB_POLICY_H */
