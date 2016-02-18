/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPC_INTERNAL_CORE_CHANNEL_CHANNEL_INIT_H
#define GRPC_INTERNAL_CORE_CHANNEL_CHANNEL_INIT_H

#include "src/core/channel/channel_stack_builder.h"
#include "src/core/surface/channel_stack_type.h"
#include "src/core/transport/transport.h"

// This module provides a way for plugins (and the grpc core library itself)
// to register mutators for channel stacks.
// It also provides a universal entry path to run those mutators to build
// a channel stack for various subsystems.

// One stage of mutation: call channel stack builder's to influence the finally
// constructed channel stack
typedef bool (*grpc_channel_init_stage)(grpc_channel_stack_builder *builder,
                                        void *arg);

// Global initialization of the system
void grpc_channel_init_init(void);

// Register one stage of mutators.
// Stages are run in priority order (lowest to highest), and then in
// registration order (in the case of a tie).
// Stages are registered against one of the pre-determined channel stack
// types.
void grpc_channel_init_register_stage(grpc_channel_stack_type type,
                                      int priority,
                                      grpc_channel_init_stage stage,
                                      void *stage_arg);

// Finalize registration. No more calls to grpc_channel_init_register_stage are
// allowed.
void grpc_channel_init_finalize(void);
// Shutdown the channel init system
void grpc_channel_init_shutdown(void);

// Construct a channel stack of some sort: see channel_stack.h for details
void *grpc_channel_init_create_stack(
    grpc_exec_ctx *exec_ctx, grpc_channel_stack_type type, size_t prefix_bytes,
    const grpc_channel_args *args, int initial_refs, grpc_iomgr_cb_func destroy,
    void *destroy_arg, grpc_transport *optional_transport);

#endif
