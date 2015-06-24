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

#include "src/core/iomgr/iomgr.h"
#include "src/core/iomgr/sockaddr.h"
#include "src/core/transport/transport.h"

/** A (sub-)channel that knows how to connect to exactly one target
    address. Provides a target for load balancing. */
typedef struct grpc_subchannel grpc_subchannel;

/** Connectivity state of a channel.
    TODO(ctiller): move to grpc.h when we implement the public
    version of the connectivity apis */
typedef enum {
  /** channel is connecting */
  GRPC_CHANNEL_CONNECTING,
  /** channel is ready for work */
  GRPC_CHANNEL_READY,
  /** channel has seen a failure but expects to recover */
  GRPC_CHANNEL_TRANSIENT_FAILURE,
  /** channel is idle */
  GRPC_CHANNEL_IDLE,
  /** channel has seen a failure that it cannot recover from */
  GRPC_CHANNEL_FATAL_FAILURE
} grpc_connectivity_state;

void grpc_subchannel_ref(grpc_subchannel *channel);
void grpc_subchannel_unref(grpc_subchannel *channel);

/** poll the current connectivity state of a channel */
grpc_connectivity_state grpc_subchannel_check_connectivity(
    grpc_subchannel *channel);

/** call notify when the connectivity state of a channel changes from *state.
    Updates *state with the new state of the channel */
void grpc_subchannel_notify_on_state_change(grpc_subchannel *channel,
                                            grpc_connectivity_state *state,
                                            grpc_iomgr_closure *notify);

/** continue processing of transport operation \a op */
void grpc_subchannel_continue_op(grpc_subchannel *channel,
                                 grpc_transport_op *op);

#endif /* GRPC_INTERNAL_CORE_CLIENT_CONFIG_SUBCHANNEL_H */
