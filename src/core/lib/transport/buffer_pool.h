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

#ifndef GRPC_CORE_LIB_TRANSPORT_BUFFER_POOL_H
#define GRPC_CORE_LIB_TRANSPORT_BUFFER_POOL_H

#include "src/core/lib/iomgr/exec_ctx.h"

#define GRPC_MEMORY_MIN 0
#define GRPC_MEMORY_MAX 10000
#define GRPC_MEMORY_DONT_CARE -1

typedef struct grpc_buffer_pool_user grpc_buffer_pool_user;
typedef struct grpc_buffer_pool grpc_buffer_pool;

grpc_buffer_pool_user *grpc_buffer_pool_register_user(grpc_buffer_pool *pool);
void grpc_buffer_pool_unregister_user(grpc_buffer_pool *pool,
                                      grpc_buffer_pool_user *user);

void grpc_buffer_pool_acquire(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *pool,
                              size_t amount, grpc_closure *on_ready);
void grpc_buffer_pool_release(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *pool,
                              size_t amount);

int16_t grpc_buffer_pool_query(grpc_buffer_pool *pool);

#endif
