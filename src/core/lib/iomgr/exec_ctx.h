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

#ifndef GRPC_CORE_LIB_IOMGR_EXEC_CTX_H
#define GRPC_CORE_LIB_IOMGR_EXEC_CTX_H

#include "src/core/lib/iomgr/closure.h"

/* #define GRPC_EXECUTION_CONTEXT_SANITIZER 1 */

/** A workqueue represents a list of work to be executed asynchronously.
    Forward declared here to avoid a circular dependency with workqueue.h. */
struct grpc_workqueue;
typedef struct grpc_workqueue grpc_workqueue;

#ifndef GRPC_EXECUTION_CONTEXT_SANITIZER
/** Execution context.
 *  A bag of data that collects information along a callstack.
 *  Generally created at public API entry points, and passed down as
 *  pointer to child functions that manipulate it.
 *
 *  Specific responsibilities (this may grow in the future):
 *  - track a list of work that needs to be delayed until the top of the
 *    call stack (this provides a convenient mechanism to run callbacks
 *    without worrying about locking issues)
 *  - provide a decision maker (via grpc_exec_ctx_ready_to_finish) that provides
 *    signal as to whether a borrowed thread should continue to do work or
 *    should actively try to finish up and get this thread back to its owner
 *
 *  CONVENTIONS:
 *  Instance of this must ALWAYS be constructed on the stack, never
 *  heap allocated. Instances and pointers to them must always be called
 *  exec_ctx. Instances are always passed as the first argument
 *  to a function that takes it, and always as a pointer (grpc_exec_ctx
 *  is never copied).
 */
struct grpc_exec_ctx {
  grpc_closure_list closure_list;
  bool cached_ready_to_finish;
  void *check_ready_to_finish_arg;
  bool (*check_ready_to_finish)(grpc_exec_ctx *exec_ctx, void *arg);
};

#define GRPC_EXEC_CTX_INIT_WITH_FINISH_CHECK(finish_check, finish_check_arg) \
  { GRPC_CLOSURE_LIST_INIT, false, finish_check_arg, finish_check }
#else
struct grpc_exec_ctx {
  bool cached_ready_to_finish;
  void *check_ready_to_finish_arg;
  bool (*check_ready_to_finish)(grpc_exec_ctx *exec_ctx, void *arg);
};
#define GRPC_EXEC_CTX_INIT_WITH_FINISH_CHECK(finish_check, finish_check_arg) \
  { false, finish_check_arg, finish_check }
#endif

#define GRPC_EXEC_CTX_INIT \
  GRPC_EXEC_CTX_INIT_WITH_FINISH_CHECK(grpc_never_ready_to_finish, NULL)

/** Flush any work that has been enqueued onto this grpc_exec_ctx.
 *  Caller must guarantee that no interfering locks are held.
 *  Returns true if work was performed, false otherwise. */
bool grpc_exec_ctx_flush(grpc_exec_ctx *exec_ctx);
/** Finish any pending work for a grpc_exec_ctx. Must be called before
 *  the instance is destroyed, or work may be lost. */
void grpc_exec_ctx_finish(grpc_exec_ctx *exec_ctx);
/** Add a closure to be executed at the next flush/finish point */
void grpc_exec_ctx_enqueue(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                           bool success,
                           grpc_workqueue *offload_target_or_null);
/** Returns true if we'd like to leave this execution context as soon as
    possible: useful for deciding whether to do something more or not depending
    on outside context */
bool grpc_exec_ctx_ready_to_finish(grpc_exec_ctx *exec_ctx);
/** A finish check that is never ready to finish */
bool grpc_never_ready_to_finish(grpc_exec_ctx *exec_ctx, void *arg_ignored);
/** A finish check that is always ready to finish */
bool grpc_always_ready_to_finish(grpc_exec_ctx *exec_ctx, void *arg_ignored);
/** Add a list of closures to be executed at the next flush/finish point.
 *  Leaves \a list empty. */
void grpc_exec_ctx_enqueue_list(grpc_exec_ctx *exec_ctx,
                                grpc_closure_list *list,
                                grpc_workqueue *offload_target_or_null);

void grpc_exec_ctx_global_init(void);

void grpc_exec_ctx_global_init(void);
void grpc_exec_ctx_global_shutdown(void);

#endif /* GRPC_CORE_LIB_IOMGR_EXEC_CTX_H */
