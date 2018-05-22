/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_FORK

#include <string.h>

#include <grpc/fork.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/fork.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/lib/surface/init.h"

/*
 * NOTE: FORKING IS NOT GENERALLY SUPPORTED, THIS IS ONLY INTENDED TO WORK
 *       AROUND VERY SPECIFIC USE CASES.
 */

namespace {
bool skipped_handler = true;
bool registered_handlers = false;
}  // namespace

void grpc_prefork() {
  grpc_core::ExecCtx exec_ctx;
  skipped_handler = true;
  if (!grpc_is_initialized()) {
    return;
  }
  if (!grpc_core::Fork::Enabled()) {
    gpr_log(GPR_ERROR,
            "Fork support not enabled; try running with the "
            "environment variable GRPC_ENABLE_FORK_SUPPORT=1");
    return;
  }
  if (!grpc_core::Fork::BlockExecCtx()) {
    gpr_log(GPR_INFO,
            "Other threads are currently calling into gRPC, skipping fork() "
            "handlers");
    return;
  }
  grpc_timer_manager_set_threading(false);
  grpc_executor_set_threading(false);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::Fork::AwaitThreads();
  skipped_handler = false;
}

void grpc_postfork_parent() {
  if (!skipped_handler) {
    grpc_core::Fork::AllowExecCtx();
    grpc_core::ExecCtx exec_ctx;
    grpc_timer_manager_set_threading(true);
    grpc_executor_set_threading(true);
  }
}

void grpc_postfork_child() {
  if (!skipped_handler) {
    grpc_core::Fork::AllowExecCtx();
    grpc_core::ExecCtx exec_ctx;
    grpc_timer_manager_set_threading(true);
    grpc_executor_set_threading(true);
  }
}

void grpc_fork_handlers_auto_register() {
  if (grpc_core::Fork::Enabled() & !registered_handlers) {
#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
    pthread_atfork(grpc_prefork, grpc_postfork_parent, grpc_postfork_child);
    registered_handlers = true;
#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  }
}

#endif  // GRPC_POSIX_FORK
