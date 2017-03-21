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

#ifndef GRPC_CORE_LIB_SURFACE_CHANNEL_H
#define GRPC_CORE_LIB_SURFACE_CHANNEL_H

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/surface/channel_stack_type.h"

grpc_channel *grpc_channel_create(grpc_exec_ctx *exec_ctx, const char *target,
                                  const grpc_channel_args *args,
                                  grpc_channel_stack_type channel_stack_type,
                                  grpc_transport *optional_transport);

grpc_channel *grpc_channel_create_with_builder(
    grpc_exec_ctx *exec_ctx, grpc_channel_stack_builder *builder,
    grpc_channel_stack_type channel_stack_type);

/** Create a call given a grpc_channel, in order to call \a method.
    Progress is tied to activity on \a pollset_set. The returned call object is
    meant to be used with \a grpc_call_start_batch_and_execute, which relies on
    callbacks to signal completions. \a method and \a host need
    only live through the invocation of this function. If \a parent_call is
    non-NULL, it must be a server-side call. It will be used to propagate
    properties from the server call to this new client call, depending on the
    value of \a propagation_mask (see propagation_bits.h for possible values) */
grpc_call *grpc_channel_create_pollset_set_call(
    grpc_exec_ctx *exec_ctx, grpc_channel *channel, grpc_call *parent_call,
    uint32_t propagation_mask, grpc_pollset_set *pollset_set, grpc_slice method,
    const grpc_slice *host, gpr_timespec deadline, void *reserved);

/** Get a (borrowed) pointer to this channels underlying channel stack */
grpc_channel_stack *grpc_channel_get_channel_stack(grpc_channel *channel);

/** Get a grpc_mdelem of grpc-status: X where X is the numeric value of
    status_code.

    The returned elem is owned by the caller. */
grpc_mdelem grpc_channel_get_reffed_status_elem(grpc_exec_ctx *exec_ctx,
                                                grpc_channel *channel,
                                                int status_code);

size_t grpc_channel_get_call_size_estimate(grpc_channel *channel);
void grpc_channel_update_call_size_estimate(grpc_channel *channel, size_t size);

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
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

/** Return the channel's compression options. */
grpc_compression_options grpc_channel_compression_options(
    const grpc_channel *channel);

#endif /* GRPC_CORE_LIB_SURFACE_CHANNEL_H */
