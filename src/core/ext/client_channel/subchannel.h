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

#ifndef GRPC_CORE_EXT_CLIENT_CHANNEL_SUBCHANNEL_H
#define GRPC_CORE_EXT_CLIENT_CHANNEL_SUBCHANNEL_H

#include "src/core/ext/client_channel/connector.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata.h"

// Channel arg containing a grpc_resolved_address to connect to.
#define GRPC_ARG_SUBCHANNEL_ADDRESS "grpc.subchannel_address"

/** A (sub-)channel that knows how to connect to exactly one target
    address. Provides a target for load balancing. */
typedef struct grpc_subchannel grpc_subchannel;
typedef struct grpc_connected_subchannel grpc_connected_subchannel;
typedef struct grpc_subchannel_call grpc_subchannel_call;
typedef struct grpc_subchannel_args grpc_subchannel_args;

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
#define GRPC_SUBCHANNEL_REF(p, r) \
  grpc_subchannel_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(p, r) \
  grpc_subchannel_ref_from_weak_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_UNREF(cl, p, r) \
  grpc_subchannel_unref((cl), (p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_WEAK_REF(p, r) \
  grpc_subchannel_weak_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_WEAK_UNREF(cl, p, r) \
  grpc_subchannel_weak_unref((cl), (p), __FILE__, __LINE__, (r))
#define GRPC_CONNECTED_SUBCHANNEL_REF(p, r) \
  grpc_connected_subchannel_ref((p), __FILE__, __LINE__, (r))
#define GRPC_CONNECTED_SUBCHANNEL_UNREF(cl, p, r) \
  grpc_connected_subchannel_unref((cl), (p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_CALL_REF(p, r) \
  grpc_subchannel_call_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_CALL_UNREF(cl, p, r) \
  grpc_subchannel_call_unref((cl), (p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_REF_EXTRA_ARGS \
  , const char *file, int line, const char *reason
#else
#define GRPC_SUBCHANNEL_REF(p, r) grpc_subchannel_ref((p))
#define GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(p, r) \
  grpc_subchannel_ref_from_weak_ref((p))
#define GRPC_SUBCHANNEL_UNREF(cl, p, r) grpc_subchannel_unref((cl), (p))
#define GRPC_SUBCHANNEL_WEAK_REF(p, r) grpc_subchannel_weak_ref((p))
#define GRPC_SUBCHANNEL_WEAK_UNREF(cl, p, r) \
  grpc_subchannel_weak_unref((cl), (p))
#define GRPC_CONNECTED_SUBCHANNEL_REF(p, r) grpc_connected_subchannel_ref((p))
#define GRPC_CONNECTED_SUBCHANNEL_UNREF(cl, p, r) \
  grpc_connected_subchannel_unref((cl), (p))
#define GRPC_SUBCHANNEL_CALL_REF(p, r) grpc_subchannel_call_ref((p))
#define GRPC_SUBCHANNEL_CALL_UNREF(cl, p, r) \
  grpc_subchannel_call_unref((cl), (p))
#define GRPC_SUBCHANNEL_REF_EXTRA_ARGS
#endif

grpc_subchannel *grpc_subchannel_ref(
    grpc_subchannel *channel GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
grpc_subchannel *grpc_subchannel_ref_from_weak_ref(
    grpc_subchannel *channel GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
void grpc_subchannel_unref(grpc_exec_ctx *exec_ctx,
                           grpc_subchannel *channel
                               GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
grpc_subchannel *grpc_subchannel_weak_ref(
    grpc_subchannel *channel GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
void grpc_subchannel_weak_unref(grpc_exec_ctx *exec_ctx,
                                grpc_subchannel *channel
                                    GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
grpc_connected_subchannel *grpc_connected_subchannel_ref(
    grpc_connected_subchannel *channel GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
void grpc_connected_subchannel_unref(grpc_exec_ctx *exec_ctx,
                                     grpc_connected_subchannel *channel
                                         GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
void grpc_subchannel_call_ref(
    grpc_subchannel_call *call GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
void grpc_subchannel_call_unref(grpc_exec_ctx *exec_ctx,
                                grpc_subchannel_call *call
                                    GRPC_SUBCHANNEL_REF_EXTRA_ARGS);

/** construct a subchannel call */
grpc_error *grpc_connected_subchannel_create_call(
    grpc_exec_ctx *exec_ctx, grpc_connected_subchannel *connected_subchannel,
    grpc_polling_entity *pollent, grpc_mdstr *path, gpr_timespec start_time,
    gpr_timespec deadline, grpc_subchannel_call **subchannel_call);

/** process a transport level op */
void grpc_connected_subchannel_process_transport_op(
    grpc_exec_ctx *exec_ctx, grpc_connected_subchannel *subchannel,
    grpc_transport_op *op);

/** poll the current connectivity state of a channel */
grpc_connectivity_state grpc_subchannel_check_connectivity(
    grpc_subchannel *channel, grpc_error **error);

/** call notify when the connectivity state of a channel changes from *state.
    Updates *state with the new state of the channel */
void grpc_subchannel_notify_on_state_change(
    grpc_exec_ctx *exec_ctx, grpc_subchannel *channel,
    grpc_pollset_set *interested_parties, grpc_connectivity_state *state,
    grpc_closure *notify);
void grpc_connected_subchannel_notify_on_state_change(
    grpc_exec_ctx *exec_ctx, grpc_connected_subchannel *channel,
    grpc_pollset_set *interested_parties, grpc_connectivity_state *state,
    grpc_closure *notify);
void grpc_connected_subchannel_ping(grpc_exec_ctx *exec_ctx,
                                    grpc_connected_subchannel *channel,
                                    grpc_closure *notify);

/** retrieve the grpc_connected_subchannel - or NULL if called before
    the subchannel becomes connected */
grpc_connected_subchannel *grpc_subchannel_get_connected_subchannel(
    grpc_subchannel *subchannel);

/** continue processing a transport op */
void grpc_subchannel_call_process_op(grpc_exec_ctx *exec_ctx,
                                     grpc_subchannel_call *subchannel_call,
                                     grpc_transport_stream_op *op);

/** continue querying for peer */
char *grpc_subchannel_call_get_peer(grpc_exec_ctx *exec_ctx,
                                    grpc_subchannel_call *subchannel_call);

grpc_call_stack *grpc_subchannel_call_get_call_stack(
    grpc_subchannel_call *subchannel_call);

struct grpc_subchannel_args {
  /* When updating this struct, also update subchannel_index.c */

  /** Channel filters for this channel - wrapped factories will likely
      want to mutate this */
  const grpc_channel_filter **filters;
  /** The number of filters in the above array */
  size_t filter_count;
  /** Channel arguments to be supplied to the newly created channel */
  const grpc_channel_args *args;
};

/** create a subchannel given a connector */
grpc_subchannel *grpc_subchannel_create(grpc_exec_ctx *exec_ctx,
                                        grpc_connector *connector,
                                        const grpc_subchannel_args *args);

/// Sets \a addr from \a args.
void grpc_get_subchannel_address_arg(const grpc_channel_args *args,
                                     grpc_resolved_address *addr);

/// Returns a new channel arg encoding the subchannel address as a string.
/// Caller is responsible for freeing the string.
grpc_arg grpc_create_subchannel_address_arg(const grpc_resolved_address *addr);

#endif /* GRPC_CORE_EXT_CLIENT_CHANNEL_SUBCHANNEL_H */
