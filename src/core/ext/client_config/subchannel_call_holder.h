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

#ifndef GRPC_CORE_EXT_CLIENT_CONFIG_SUBCHANNEL_CALL_HOLDER_H
#define GRPC_CORE_EXT_CLIENT_CONFIG_SUBCHANNEL_CALL_HOLDER_H

#include "src/core/ext/client_config/subchannel.h"
#include "src/core/lib/iomgr/polling_entity.h"

/** Pick a subchannel for grpc_subchannel_call_holder;
    Return 1 if subchannel is available immediately (in which case on_ready
    should not be called), or 0 otherwise (in which case on_ready should be
    called when the subchannel is available) */
typedef int (*grpc_subchannel_call_holder_pick_subchannel)(
    grpc_exec_ctx *exec_ctx, void *arg, grpc_metadata_batch *initial_metadata,
    uint32_t initial_metadata_flags,
    grpc_connected_subchannel **connected_subchannel, grpc_closure *on_ready);

typedef enum {
  GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING,
  GRPC_SUBCHANNEL_CALL_HOLDER_PICKING_SUBCHANNEL
} grpc_subchannel_call_holder_creation_phase;

/** Wrapper for holding a pointer to grpc_subchannel_call, and the
    associated machinery to create such a pointer.
    Handles queueing of stream ops until a call object is ready, waiting
    for initial metadata before trying to create a call object,
    and handling cancellation gracefully.

    The channel filter uses this as their call_data. */
typedef struct grpc_subchannel_call_holder {
  /** either 0 for no call, 1 for cancelled, or a pointer to a
      grpc_subchannel_call */
  gpr_atm subchannel_call;
  /** Helper function to choose the subchannel on which to create
      the call object. Channel filter delegates to the load
      balancing policy (once it's ready). */
  grpc_subchannel_call_holder_pick_subchannel pick_subchannel;
  void *pick_subchannel_arg;

  gpr_mu mu;

  grpc_subchannel_call_holder_creation_phase creation_phase;
  grpc_connected_subchannel *connected_subchannel;
  grpc_polling_entity *pollent;

  grpc_transport_stream_op *waiting_ops;
  size_t waiting_ops_count;
  size_t waiting_ops_capacity;

  grpc_closure next_step;

  grpc_call_stack *owning_call;
} grpc_subchannel_call_holder;

void grpc_subchannel_call_holder_init(
    grpc_subchannel_call_holder *holder,
    grpc_subchannel_call_holder_pick_subchannel pick_subchannel,
    void *pick_subchannel_arg, grpc_call_stack *owning_call);
void grpc_subchannel_call_holder_destroy(grpc_exec_ctx *exec_ctx,
                                         grpc_subchannel_call_holder *holder);

void grpc_subchannel_call_holder_perform_op(grpc_exec_ctx *exec_ctx,
                                            grpc_subchannel_call_holder *holder,
                                            grpc_transport_stream_op *op);
char *grpc_subchannel_call_holder_get_peer(grpc_exec_ctx *exec_ctx,
                                           grpc_subchannel_call_holder *holder);

#endif /* GRPC_CORE_EXT_CLIENT_CONFIG_SUBCHANNEL_CALL_HOLDER_H */
