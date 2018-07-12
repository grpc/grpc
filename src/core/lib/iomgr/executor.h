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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gpr/spinlock.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/closure.h"

typedef struct {
  gpr_mu mu;
  size_t id;  // For debugging purposes
  gpr_cv cv;
  grpc_closure_list elems;
  size_t depth;  // Number of closures in the closure list
  bool shutdown;
  bool queued_long_job;
  grpc_core::Thread thd;
} ThreadState;

typedef enum { GRPC_EXECUTOR_SHORT, GRPC_EXECUTOR_LONG } GrpcExecutorJobType;

class GrpcExecutor {
 public:
  GrpcExecutor(const char* executor_name);

  void Init();

  /** Is the executor multi-threaded? */
  bool IsThreaded() const;

  /* Enable/disable threading - must be called after Init and Shutdown() */
  void SetThreading(bool threading);

  /** Shutdown the executor, running all pending work as part of the call */
  void Shutdown();

  /** Enqueue the closure onto the executor. is_short is true if the closure is
   * a short job (i.e expected to not block and complete quickly) */
  void Enqueue(grpc_closure* closure, grpc_error* error, bool is_short);

 private:
  static size_t RunClosures(grpc_closure_list list);
  static void ThreadMain(void* arg);

  const char* name_;
  ThreadState* thd_state_;
  size_t max_threads_;
  gpr_atm num_threads_;
  gpr_spinlock adding_thread_lock_;
};

// == Global executor functions ==

void grpc_executor_init();

grpc_closure_scheduler* grpc_executor_scheduler(GrpcExecutorJobType job_type);

void grpc_executor_shutdown();

bool grpc_executor_is_threaded();

void grpc_executor_set_threading(bool enable);

#endif /* GRPC_CORE_LIB_IOMGR_EXECUTOR_H */
