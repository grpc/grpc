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

/** A (sub-)channel that knows how to connect to exactly one target
    address. Provides a target for load balancing. */
typedef struct grpc_subchannel grpc_subchannel;
typedef struct grpc_subchannel_call grpc_subchannel_call;

void grpc_subchannel_ref(grpc_subchannel *channel);
void grpc_subchannel_unref(grpc_subchannel *channel);

void grpc_subchannel_call_ref(grpc_subchannel_call *call);
void grpc_subchannel_call_unref(grpc_subchannel_call *call);

/** poll the current connectivity state of a channel */
grpc_connectivity_state grpc_subchannel_check_connectivity(
    grpc_subchannel *channel);

/** call notify when the connectivity state of a channel changes from *state.
    Updates *state with the new state of the channel */
void grpc_subchannel_notify_on_state_change(grpc_subchannel *channel,
                                            grpc_connectivity_state *state,
                                            grpc_iomgr_closure *notify);

/** construct a call (possibly asynchronously) */
void grpc_subchannel_create_call(grpc_subchannel *subchannel,
                                 grpc_call_element *parent,
                                 grpc_transport_stream_op *initial_op,
                                 grpc_subchannel_call **target,
                                 grpc_iomgr_closure *notify);

/** continue processing a transport op */
void grpc_subchannel_call_process_op(grpc_subchannel_call *subchannel_call,
                                     grpc_transport_stream_op *op);

/** create a subchannel given a connector */
grpc_subchannel *grpc_subchannel_create(grpc_connector *connector);

#endif /* GRPC_INTERNAL_CORE_CLIENT_CONFIG_SUBCHANNEL_H */
