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

#ifndef GRPC_CORE_LIB_IOMGR_ASYNC_EXECUTION_LOCK_H
#define GRPC_CORE_LIB_IOMGR_ASYNC_EXECUTION_LOCK_H

#include <stddef.h>

#include <grpc/support/atm.h>
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/support/mpscq.h"

typedef struct grpc_aelock grpc_aelock;

// Provides serialized access to some resource.
// Each action queued on an aelock is executed serially in a borrowed thread.
// The actual thread executing actions may change over time (but there will only
// every be one at a time).

typedef void (*grpc_aelock_action)(grpc_exec_ctx *exec_ctx, void *arg);

// Initialize the lock, with an optional workqueue to shift load to when
// necessary
grpc_aelock *grpc_aelock_create(grpc_workqueue *optional_workqueue);
// Destroy the lock
void grpc_aelock_destroy(grpc_aelock *lock);
// Execute \a action within the lock. \a arg is the argument to pass to \a
// action and sizeof_arg is the sizeof(*arg), or 0 if arg is non-copyable.
void grpc_aelock_execute(grpc_exec_ctx *exec_ctx, grpc_aelock *lock,
                         grpc_aelock_action action, void *arg,
                         size_t sizeof_arg);

#endif /* GRPC_CORE_LIB_IOMGR_ASYNC_EXECUTION_LOCK_H */
