/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_IOMGR_EXEC_CTX_H
#define GRPC_CORE_LIB_IOMGR_EXEC_CTX_H

#include "src/core/lib/iomgr/closure.h"

/* #define GRPC_EXECUTION_CONTEXT_SANITIZER 1 */

/** A workqueue represents a list of work to be executed asynchronously.
    Forward declared here to avoid a circular dependency with workqueue.h. */
typedef struct grpc_workqueue grpc_workqueue;
typedef struct grpc_combiner grpc_combiner;

/* This exec_ctx is ready to return: either pre-populated, or cached as soon as
   the finish_check returns true */
#define GRPC_EXEC_CTX_FLAG_IS_FINISHED 1
/* The exec_ctx's thread is (potentially) owned by a call or channel: care
   should be given to not delete said call/channel from this exec_ctx */
#define GRPC_EXEC_CTX_FLAG_THREAD_RESOURCE_LOOP 2

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
 *  - Instance of this must ALWAYS be constructed on the stack, never
 *    heap allocated.
 *  - Instances and pointers to them must always be called exec_ctx.
 *  - Instances are always passed as the first argument to a function that
 *    takes it, and always as a pointer (grpc_exec_ctx is never copied).
 */
struct grpc_exec_ctx {
  grpc_closure_list closure_list;
  /** currently active combiner: updated only via combiner.c */
  grpc_combiner *active_combiner;
  /** last active combiner in the active combiner list */
  grpc_combiner *last_combiner;
  uintptr_t flags;
  void *check_ready_to_finish_arg;
  bool (*check_ready_to_finish)(grpc_exec_ctx *exec_ctx, void *arg);
};

/* initializer for grpc_exec_ctx:
   prefer to use GRPC_EXEC_CTX_INIT whenever possible */
#define GRPC_EXEC_CTX_INITIALIZER(flags, finish_check, finish_check_arg) \
  { GRPC_CLOSURE_LIST_INIT, NULL, NULL, flags, finish_check_arg, finish_check }

/* initialize an execution context at the top level of an API call into grpc
   (this is safe to use elsewhere, though possibly not as efficient) */
#define GRPC_EXEC_CTX_INIT \
  GRPC_EXEC_CTX_INITIALIZER(GRPC_EXEC_CTX_FLAG_IS_FINISHED, NULL, NULL)

extern grpc_closure_scheduler *grpc_schedule_on_exec_ctx;

bool grpc_exec_ctx_has_work(grpc_exec_ctx *exec_ctx);

/** Flush any work that has been enqueued onto this grpc_exec_ctx.
 *  Caller must guarantee that no interfering locks are held.
 *  Returns true if work was performed, false otherwise. */
bool grpc_exec_ctx_flush(grpc_exec_ctx *exec_ctx);
/** Finish any pending work for a grpc_exec_ctx. Must be called before
 *  the instance is destroyed, or work may be lost. */
void grpc_exec_ctx_finish(grpc_exec_ctx *exec_ctx);
/** Returns true if we'd like to leave this execution context as soon as
    possible: useful for deciding whether to do something more or not depending
    on outside context */
bool grpc_exec_ctx_ready_to_finish(grpc_exec_ctx *exec_ctx);
/** A finish check that is never ready to finish */
bool grpc_never_ready_to_finish(grpc_exec_ctx *exec_ctx, void *arg_ignored);
/** A finish check that is always ready to finish */
bool grpc_always_ready_to_finish(grpc_exec_ctx *exec_ctx, void *arg_ignored);

void grpc_exec_ctx_global_init(void);

void grpc_exec_ctx_global_init(void);
void grpc_exec_ctx_global_shutdown(void);

#endif /* GRPC_CORE_LIB_IOMGR_EXEC_CTX_H */
