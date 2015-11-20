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

#ifndef GRPC_INTERNAL_CORE_CLIENT_CONFIG_SUBCHANNEL_H
#define GRPC_INTERNAL_CORE_CLIENT_CONFIG_SUBCHANNEL_H

#include "src/core/channel/channel_stack.h"
#include "src/core/client_config/connector.h"
#include "src/core/transport/connectivity_state.h"

/** A (sub-)channel that knows how to connect to exactly one target
    address. Provides a target for load balancing. */
typedef struct grpc_subchannel grpc_subchannel;
typedef struct grpc_subchannel_call grpc_subchannel_call;
typedef struct grpc_subchannel_args grpc_subchannel_args;

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
#define GRPC_SUBCHANNEL_REF(p, r) \
  grpc_subchannel_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_UNREF(cl, p, r) \
  grpc_subchannel_unref((cl), (p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_CALL_REF(p, r) \
  grpc_subchannel_call_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_CALL_UNREF(cl, p, r) \
  grpc_subchannel_call_unref((cl), (p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_REF_EXTRA_ARGS \
  , const char *file, int line, const char *reason
#else
#define GRPC_SUBCHANNEL_REF(p, r) grpc_subchannel_ref((p))
#define GRPC_SUBCHANNEL_UNREF(cl, p, r) grpc_subchannel_unref((cl), (p))
#define GRPC_SUBCHANNEL_CALL_REF(p, r) grpc_subchannel_call_ref((p))
#define GRPC_SUBCHANNEL_CALL_UNREF(cl, p, r) \
  grpc_subchannel_call_unref((cl), (p))
#define GRPC_SUBCHANNEL_REF_EXTRA_ARGS
#endif

void grpc_subchannel_ref(grpc_subchannel *channel
                             GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
void grpc_subchannel_unref(grpc_exec_ctx *exec_ctx,
                           grpc_subchannel *channel
                               GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
void grpc_subchannel_call_ref(grpc_subchannel_call *call
                                  GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
void grpc_subchannel_call_unref(grpc_exec_ctx *exec_ctx,
                                grpc_subchannel_call *call
                                    GRPC_SUBCHANNEL_REF_EXTRA_ARGS);

/** construct a subchannel call (possibly asynchronously).
 *
 * If the returned status is 1, the call will return immediately and \a target
 * will point to a connected \a subchannel_call instance. Note that \a notify
 * will \em not be invoked in this case.
 * Otherwise, if the returned status is 0, the subchannel call will be created
 * asynchronously, invoking the \a notify callback upon completion. */
int grpc_subchannel_create_call(grpc_exec_ctx *exec_ctx,
                                grpc_subchannel *subchannel,
                                grpc_pollset *pollset, gpr_atm *target,
                                grpc_closure *notify);

/** cancel \a call in the waiting state. */
void grpc_subchannel_cancel_create_call(grpc_exec_ctx *exec_ctx,
                                        grpc_subchannel *subchannel,
                                        gpr_atm *target);

/** process a transport level op */
void grpc_subchannel_process_transport_op(grpc_exec_ctx *exec_ctx,
                                          grpc_subchannel *subchannel,
                                          grpc_transport_op *op);

/** poll the current connectivity state of a channel */
grpc_connectivity_state grpc_subchannel_check_connectivity(
    grpc_subchannel *channel);

/** call notify when the connectivity state of a channel changes from *state.
    Updates *state with the new state of the channel */
void grpc_subchannel_notify_on_state_change(grpc_exec_ctx *exec_ctx,
                                            grpc_subchannel *channel,
                                            grpc_connectivity_state *state,
                                            grpc_closure *notify);

/** Remove \a subscribed_notify from the list of closures to be called on a
 * state change if present, returning 1. Otherwise, nothing is done and return
 * 0. */
int grpc_subchannel_state_change_unsubscribe(grpc_exec_ctx *exec_ctx,
                                             grpc_subchannel *channel,
                                             grpc_closure *subscribed_notify);

/** express interest in \a channel's activities through \a pollset. */
void grpc_subchannel_add_interested_party(grpc_exec_ctx *exec_ctx,
                                          grpc_subchannel *channel,
                                          grpc_pollset *pollset);
/** stop following \a channel's activity through \a pollset. */
void grpc_subchannel_del_interested_party(grpc_exec_ctx *exec_ctx,
                                          grpc_subchannel *channel,
                                          grpc_pollset *pollset);

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
  /** Channel filters for this channel - wrapped factories will likely
      want to mutate this */
  const grpc_channel_filter **filters;
  /** The number of filters in the above array */
  size_t filter_count;
  /** Channel arguments to be supplied to the newly created channel */
  const grpc_channel_args *args;
  /** Address to connect to */
  struct sockaddr *addr;
  size_t addr_len;
  /** master channel */
  grpc_channel *master;
};

/** create a subchannel given a connector */
grpc_subchannel *grpc_subchannel_create(grpc_connector *connector,
                                        grpc_subchannel_args *args);

/** Return the master channel associated with the subchannel */
grpc_channel *grpc_subchannel_get_master(grpc_subchannel *subchannel);

#endif /* GRPC_INTERNAL_CORE_CLIENT_CONFIG_SUBCHANNEL_H */
