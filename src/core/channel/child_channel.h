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

#ifndef GRPC_INTERNAL_CORE_CHANNEL_CHILD_CHANNEL_H
#define GRPC_INTERNAL_CORE_CHANNEL_CHILD_CHANNEL_H

#include "src/core/channel/channel_stack.h"

/* helper for filters that need to host child channel stacks... handles
   lifetime and upwards propagation cleanly */

extern const grpc_channel_filter grpc_child_channel_top_filter;

typedef grpc_channel_stack grpc_child_channel;
typedef grpc_call_stack grpc_child_call;

/* filters[0] must be &grpc_child_channel_top_filter */
grpc_child_channel *grpc_child_channel_create(
    grpc_channel_element *parent, const grpc_channel_filter **filters,
    size_t filter_count, const grpc_channel_args *args,
    grpc_mdctx *metadata_context);
void grpc_child_channel_handle_op(grpc_child_channel *channel,
                                  grpc_channel_op *op);
grpc_channel_element *grpc_child_channel_get_bottom_element(
    grpc_child_channel *channel);
void grpc_child_channel_destroy(grpc_child_channel *channel,
                                int wait_for_callbacks);

grpc_child_call *grpc_child_channel_create_call(grpc_child_channel *channel,
                                                grpc_call_element *parent);
grpc_call_element *grpc_child_call_get_top_element(grpc_child_call *call);
void grpc_child_call_destroy(grpc_child_call *call);

#endif  /* GRPC_INTERNAL_CORE_CHANNEL_CHILD_CHANNEL_H */
