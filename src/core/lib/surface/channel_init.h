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

#ifndef GRPC_CORE_LIB_SURFACE_CHANNEL_INIT_H
#define GRPC_CORE_LIB_SURFACE_CHANNEL_INIT_H

#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"

#define GRPC_CHANNEL_INIT_BUILTIN_PRIORITY 10000

#ifdef __cplusplus
extern "C" {
#endif

/// This module provides a way for plugins (and the grpc core library itself)
/// to register mutators for channel stacks.
/// It also provides a universal entry path to run those mutators to build
/// a channel stack for various subsystems.

/// One stage of mutation: call functions against \a builder to influence the
/// finally constructed channel stack
typedef bool (*grpc_channel_init_stage)(grpc_exec_ctx *exec_ctx,
                                        grpc_channel_stack_builder *builder,
                                        void *arg);

/// Global initialization of the system
void grpc_channel_init_init(void);

/// Register one stage of mutators.
/// Stages are run in priority order (lowest to highest), and then in
/// registration order (in the case of a tie).
/// Stages are registered against one of the pre-determined channel stack
/// types.
void grpc_channel_init_register_stage(grpc_channel_stack_type type,
                                      int priority,
                                      grpc_channel_init_stage stage_fn,
                                      void *stage_arg);

/// Finalize registration. No more calls to grpc_channel_init_register_stage are
/// allowed.
void grpc_channel_init_finalize(void);
/// Shutdown the channel init system
void grpc_channel_init_shutdown(void);

/// Construct a channel stack of some sort: see channel_stack.h for details
/// \a type is the type of channel stack to create
/// \a prefix_bytes is the number of bytes before the channel stack to allocate
/// \a args are configuration arguments for the channel stack
/// \a initial_refs is the initial refcount to give the channel stack
/// \a destroy and \a destroy_arg specify how to destroy the channel stack
///    if destroy_arg is NULL, the returned value from this function will be
///    substituted
/// \a optional_transport is either NULL or a constructed transport object
/// Returns a pointer to the base of the memory allocated (the actual channel
/// stack object will be prefix_bytes past that pointer)
bool grpc_channel_init_create_stack(grpc_exec_ctx *exec_ctx,
                                    grpc_channel_stack_builder *builder,
                                    grpc_channel_stack_type type);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SURFACE_CHANNEL_INIT_H */
