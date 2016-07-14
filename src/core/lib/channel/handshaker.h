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

#ifndef GRPC_CORE_LIB_CHANNEL_HANDSHAKER_H
#define GRPC_CORE_LIB_CHANNEL_HANDSHAKER_H

#include <grpc/impl/codegen/time.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"

//
// grpc_handshaker -- API for initial handshaking for a new connection
//

// FIXME: document

typedef struct grpc_handshaker grpc_handshaker;

typedef void (*grpc_handshaker_done_cb)(grpc_exec_ctx* exec_ctx,
                                        grpc_endpoint* endpoint, void* arg);

struct grpc_handshaker_vtable {
  void (*destroy)(grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker);

  void (*shutdown)(grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker);

  void (*do_handshake)(grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker,
                       grpc_endpoint* endpoint, gpr_timespec deadline,
                       grpc_handshaker_done_cb cb, void* arg);
};

struct grpc_handshaker {
  const struct grpc_handshaker_vtable* vtable;
};

// Called by concrete implementations to initialize the base struct.
void grpc_handshaker_init(const struct grpc_handshaker_vtable* vtable,
                          grpc_handshaker* handshaker);

// Convenient wrappers for invoking methods via the vtable.
void grpc_handshaker_destroy(grpc_exec_ctx* exec_ctx,
                             grpc_handshaker* handshaker);
void grpc_handshaker_shutdown(grpc_exec_ctx* exec_ctx,
                              grpc_handshaker* handshaker);
void grpc_handshaker_do_handshake(grpc_exec_ctx* exec_ctx,
                                  grpc_handshaker* handshaker,
                                  grpc_endpoint* endpoint,
                                  gpr_timespec deadline,
                                  grpc_handshaker_done_cb cb, void* arg);

//
// grpc_handshake_manager -- manages a set of handshakers
//

typedef struct grpc_handshake_manager grpc_handshake_manager;

grpc_handshake_manager* grpc_handshake_manager_create();

// Handshakers will be invoked in the order added.
void grpc_handshake_manager_add(grpc_handshaker* handshaker,
                                grpc_handshake_manager* mgr);

void grpc_handshake_manager_destroy(grpc_exec_ctx* exec_ctx,
                                    grpc_handshake_manager* mgr);

void grpc_handshake_manager_shutdown(grpc_exec_ctx* exec_ctx,
                                     grpc_handshake_manager* mgr);

void grpc_handshake_manager_do_handshake(grpc_exec_ctx* exec_ctx,
                                         grpc_handshake_manager* mgr,
                                         grpc_endpoint* endpoint,
                                         gpr_timespec deadline,
                                         grpc_handshaker_done_cb cb, void* arg);

#endif /* GRPC_CORE_LIB_CHANNEL_HANDSHAKER_H */
