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

#ifndef GRPC_INTERNAL_CORE_SURFACE_CHANNEL_H
#define GRPC_INTERNAL_CORE_SURFACE_CHANNEL_H

#include "src/core/channel/channel_stack.h"
#include "src/core/client_config/subchannel_factory.h"

grpc_channel *grpc_channel_create_from_filters(
    grpc_exec_ctx *exec_ctx, const char *target,
    const grpc_channel_filter **filters, size_t count,
    const grpc_channel_args *args, grpc_mdctx *mdctx, int is_client);

/** Get a (borrowed) pointer to this channels underlying channel stack */
grpc_channel_stack *grpc_channel_get_channel_stack(grpc_channel *channel);

/** Get a (borrowed) pointer to the channel wide metadata context */
grpc_mdctx *grpc_channel_get_metadata_context(grpc_channel *channel);

/** Get a grpc_mdelem of grpc-status: X where X is the numeric value of
    status_code.

    The returned elem is owned by the caller. */
grpc_mdelem *grpc_channel_get_reffed_status_elem(grpc_channel *channel,
                                                 int status_code);
grpc_mdstr *grpc_channel_get_status_string(grpc_channel *channel);
grpc_mdstr *grpc_channel_get_compression_algorithm_string(
    grpc_channel *channel);
grpc_mdstr *grpc_channel_get_encodings_accepted_by_peer_string(
    grpc_channel *channel);
grpc_mdstr *grpc_channel_get_message_string(grpc_channel *channel);
gpr_uint32 grpc_channel_get_max_message_length(grpc_channel *channel);

#ifdef GRPC_CHANNEL_REF_COUNT_DEBUG
void grpc_channel_internal_ref(grpc_channel *channel, const char *reason);
void grpc_channel_internal_unref(grpc_exec_ctx *exec_ctx, grpc_channel *channel,
                                 const char *reason);
#define GRPC_CHANNEL_INTERNAL_REF(channel, reason) \
  grpc_channel_internal_ref(channel, reason)
#define GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, channel, reason) \
  grpc_channel_internal_unref(exec_ctx, channel, reason)
#else
void grpc_channel_internal_ref(grpc_channel *channel);
void grpc_channel_internal_unref(grpc_exec_ctx *exec_ctx,
                                 grpc_channel *channel);
#define GRPC_CHANNEL_INTERNAL_REF(channel, reason) \
  grpc_channel_internal_ref(channel)
#define GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, channel, reason) \
  grpc_channel_internal_unref(exec_ctx, channel)
#endif

#endif /* GRPC_INTERNAL_CORE_SURFACE_CHANNEL_H */
