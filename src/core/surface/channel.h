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

grpc_channel *grpc_channel_create_from_filters(
    const grpc_channel_filter **filters, size_t count,
    const grpc_channel_args *args, grpc_mdctx *mdctx, int is_client);

grpc_channel_stack *grpc_channel_get_channel_stack(grpc_channel *channel);
grpc_mdctx *grpc_channel_get_metadata_context(grpc_channel *channel);
grpc_mdstr *grpc_channel_get_status_string(grpc_channel *channel);
grpc_mdstr *grpc_channel_get_message_string(grpc_channel *channel);

void grpc_client_channel_closed(grpc_channel_element *elem);

void grpc_channel_internal_ref(grpc_channel *channel);
void grpc_channel_internal_unref(grpc_channel *channel);

#endif  /* GRPC_INTERNAL_CORE_SURFACE_CHANNEL_H */
