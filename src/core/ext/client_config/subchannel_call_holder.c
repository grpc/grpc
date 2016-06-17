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

#include "src/core/ext/client_config/subchannel_call_holder.h"

#include <grpc/support/alloc.h>

#include "src/core/lib/profiling/timers.h"

#define GET_CALL(holder) \
  ((grpc_subchannel_call *)(gpr_atm_acq_load(&(holder)->subchannel_call)))

#define CANCELLED_CALL ((grpc_subchannel_call *)1)

static void subchannel_ready(grpc_exec_ctx *exec_ctx, void *holder,
                             bool success);
static void retry_ops(grpc_exec_ctx *exec_ctx, void *retry_ops_args,
                      bool success);

static void add_waiting_locked(grpc_subchannel_call_holder *holder,
                               grpc_transport_stream_op *op);
static void fail_locked(grpc_exec_ctx *exec_ctx,
                        grpc_subchannel_call_holder *holder);
static void retry_waiting_locked(grpc_exec_ctx *exec_ctx,
                                 grpc_subchannel_call_holder *holder);

void grpc_subchannel_call_holder_init(
    grpc_subchannel_call_holder *holder,
    grpc_subchannel_call_holder_pick_subchannel pick_subchannel,
    void *pick_subchannel_arg, grpc_call_stack *owning_call) {
  gpr_atm_rel_store(&holder->subchannel_call, 0);
  holder->pick_subchannel = pick_subchannel;
  holder->pick_subchannel_arg = pick_subchannel_arg;
  gpr_mu_init(&holder->mu);
  holder->connected_subchannel = NULL;
  holder->waiting_ops = NULL;
  holder->waiting_ops_count = 0;
  holder->waiting_ops_capacity = 0;
  holder->creation_phase = GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING;
  holder->owning_call = owning_call;
  holder->pollent = NULL;
}

void grpc_subchannel_call_holder_destroy(grpc_exec_ctx *exec_ctx,
                                         grpc_subchannel_call_holder *holder) {
  grpc_subchannel_call *call = GET_CALL(holder);
  if (call != NULL && call != CANCELLED_CALL) {
    GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, call, "holder");
  }
  GPR_ASSERT(holder->creation_phase ==
             GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING);
  gpr_mu_destroy(&holder->mu);
  GPR_ASSERT(holder->waiting_ops_count == 0);
  gpr_free(holder->waiting_ops);
}

void grpc_subchannel_call_holder_perform_op(grpc_exec_ctx *exec_ctx,
                                            grpc_subchannel_call_holder *holder,
                                            grpc_transport_stream_op *op) {
  /* try to (atomically) get the call */
  grpc_subchannel_call *call = GET_CALL(holder);
  GPR_TIMER_BEGIN("grpc_subchannel_call_holder_perform_op", 0);
  if (call == CANCELLED_CALL) {
    grpc_transport_stream_op_finish_with_failure(exec_ctx, op);
    GPR_TIMER_END("grpc_subchannel_call_holder_perform_op", 0);
    return;
  }
  if (call != NULL) {
    grpc_subchannel_call_process_op(exec_ctx, call, op);
    GPR_TIMER_END("grpc_subchannel_call_holder_perform_op", 0);
    return;
  }
  /* we failed; lock and figure out what to do */
  gpr_mu_lock(&holder->mu);
retry:
  /* need to recheck that another thread hasn't set the call */
  call = GET_CALL(holder);
  if (call == CANCELLED_CALL) {
    gpr_mu_unlock(&holder->mu);
    grpc_transport_stream_op_finish_with_failure(exec_ctx, op);
    GPR_TIMER_END("grpc_subchannel_call_holder_perform_op", 0);
    return;
  }
  if (call != NULL) {
    gpr_mu_unlock(&holder->mu);
    grpc_subchannel_call_process_op(exec_ctx, call, op);
    GPR_TIMER_END("grpc_subchannel_call_holder_perform_op", 0);
    return;
  }
  /* if this is a cancellation, then we can raise our cancelled flag */
  if (op->cancel_with_status != GRPC_STATUS_OK) {
    if (!gpr_atm_rel_cas(&holder->subchannel_call, 0, 1)) {
      goto retry;
    } else {
      switch (holder->creation_phase) {
        case GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING:
          fail_locked(exec_ctx, holder);
          break;
        case GRPC_SUBCHANNEL_CALL_HOLDER_PICKING_SUBCHANNEL:
          holder->pick_subchannel(exec_ctx, holder->pick_subchannel_arg, NULL,
                                  0, &holder->connected_subchannel, NULL);
          break;
      }
      gpr_mu_unlock(&holder->mu);
      grpc_transport_stream_op_finish_with_failure(exec_ctx, op);
      GPR_TIMER_END("grpc_subchannel_call_holder_perform_op", 0);
      return;
    }
  }
  /* if we don't have a subchannel, try to get one */
  if (holder->creation_phase == GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING &&
      holder->connected_subchannel == NULL &&
      op->send_initial_metadata != NULL) {
    holder->creation_phase = GRPC_SUBCHANNEL_CALL_HOLDER_PICKING_SUBCHANNEL;
    grpc_closure_init(&holder->next_step, subchannel_ready, holder);
    GRPC_CALL_STACK_REF(holder->owning_call, "pick_subchannel");
    if (holder->pick_subchannel(
            exec_ctx, holder->pick_subchannel_arg, op->send_initial_metadata,
            op->send_initial_metadata_flags, &holder->connected_subchannel,
            &holder->next_step)) {
      holder->creation_phase = GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING;
      GRPC_CALL_STACK_UNREF(exec_ctx, holder->owning_call, "pick_subchannel");
    }
  }
  /* if we've got a subchannel, then let's ask it to create a call */
  if (holder->creation_phase == GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING &&
      holder->connected_subchannel != NULL) {
    gpr_atm_rel_store(
        &holder->subchannel_call,
        (gpr_atm)(uintptr_t)grpc_connected_subchannel_create_call(
            exec_ctx, holder->connected_subchannel, holder->pollent));
    retry_waiting_locked(exec_ctx, holder);
    goto retry;
  }
  /* nothing to be done but wait */
  add_waiting_locked(holder, op);
  gpr_mu_unlock(&holder->mu);
  GPR_TIMER_END("grpc_subchannel_call_holder_perform_op", 0);
}

static void subchannel_ready(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  grpc_subchannel_call_holder *holder = arg;
  gpr_mu_lock(&holder->mu);
  GPR_ASSERT(holder->creation_phase ==
             GRPC_SUBCHANNEL_CALL_HOLDER_PICKING_SUBCHANNEL);
  holder->creation_phase = GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING;
  if (holder->connected_subchannel == NULL) {
    gpr_atm_no_barrier_store(&holder->subchannel_call, 1);
    fail_locked(exec_ctx, holder);
  } else if (1 == gpr_atm_acq_load(&holder->subchannel_call)) {
    /* already cancelled before subchannel became ready */
    fail_locked(exec_ctx, holder);
  } else {
    gpr_atm_rel_store(
        &holder->subchannel_call,
        (gpr_atm)(uintptr_t)grpc_connected_subchannel_create_call(
            exec_ctx, holder->connected_subchannel, holder->pollent));
    retry_waiting_locked(exec_ctx, holder);
  }
  gpr_mu_unlock(&holder->mu);
  GRPC_CALL_STACK_UNREF(exec_ctx, holder->owning_call, "pick_subchannel");
}

typedef struct {
  grpc_transport_stream_op *ops;
  size_t nops;
  grpc_subchannel_call *call;
} retry_ops_args;

static void retry_waiting_locked(grpc_exec_ctx *exec_ctx,
                                 grpc_subchannel_call_holder *holder) {
  retry_ops_args *a = gpr_malloc(sizeof(*a));
  a->ops = holder->waiting_ops;
  a->nops = holder->waiting_ops_count;
  a->call = GET_CALL(holder);
  if (a->call == CANCELLED_CALL) {
    gpr_free(a);
    fail_locked(exec_ctx, holder);
    return;
  }
  holder->waiting_ops = NULL;
  holder->waiting_ops_count = 0;
  holder->waiting_ops_capacity = 0;
  GRPC_SUBCHANNEL_CALL_REF(a->call, "retry_ops");
  grpc_exec_ctx_enqueue(exec_ctx, grpc_closure_create(retry_ops, a), true,
                        NULL);
}

static void retry_ops(grpc_exec_ctx *exec_ctx, void *args, bool success) {
  retry_ops_args *a = args;
  size_t i;
  for (i = 0; i < a->nops; i++) {
    grpc_subchannel_call_process_op(exec_ctx, a->call, &a->ops[i]);
  }
  GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, a->call, "retry_ops");
  gpr_free(a->ops);
  gpr_free(a);
}

static void add_waiting_locked(grpc_subchannel_call_holder *holder,
                               grpc_transport_stream_op *op) {
  GPR_TIMER_BEGIN("add_waiting_locked", 0);
  if (holder->waiting_ops_count == holder->waiting_ops_capacity) {
    holder->waiting_ops_capacity = GPR_MAX(3, 2 * holder->waiting_ops_capacity);
    holder->waiting_ops =
        gpr_realloc(holder->waiting_ops, holder->waiting_ops_capacity *
                                             sizeof(*holder->waiting_ops));
  }
  holder->waiting_ops[holder->waiting_ops_count++] = *op;
  GPR_TIMER_END("add_waiting_locked", 0);
}

static void fail_locked(grpc_exec_ctx *exec_ctx,
                        grpc_subchannel_call_holder *holder) {
  size_t i;
  for (i = 0; i < holder->waiting_ops_count; i++) {
    grpc_transport_stream_op_finish_with_failure(exec_ctx,
                                                 &holder->waiting_ops[i]);
  }
  holder->waiting_ops_count = 0;
}

char *grpc_subchannel_call_holder_get_peer(
    grpc_exec_ctx *exec_ctx, grpc_subchannel_call_holder *holder) {
  grpc_subchannel_call *subchannel_call = GET_CALL(holder);

  if (subchannel_call == NULL || subchannel_call == CANCELLED_CALL) {
    return NULL;
  } else {
    return grpc_subchannel_call_get_peer(exec_ctx, subchannel_call);
  }
}
