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

#ifndef GRPC_CORE_LIB_IOMGR_EXECUTOR_H
#define GRPC_CORE_LIB_IOMGR_EXECUTOR_H

#include "src/core/lib/iomgr/closure.h"

/** Initialize the global executor.
 *
 * This mechanism is meant to outsource work (grpc_closure instances) to a
 * thread, for those cases where blocking isn't an option but there isn't a
 * non-blocking solution available. */
void grpc_executor_init(grpc_exec_ctx *exec_ctx);

extern grpc_closure_scheduler *grpc_executor_scheduler;

/** Shutdown the executor, running all pending work as part of the call */
void grpc_executor_shutdown(grpc_exec_ctx *exec_ctx);

/** Is the executor multi-threaded? */
bool grpc_executor_is_threaded();

/* enable/disable threading - must be called after grpc_executor_init and before
   grpc_executor_shutdown */
void grpc_executor_set_threading(grpc_exec_ctx *exec_ctx, bool enable);

#endif /* GRPC_CORE_LIB_IOMGR_EXECUTOR_H */
