/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_FORK

#include <string.h>

#include <grpc/fork.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/fork.h"
#include "src/core/lib/support/thd_internal.h"

int grpc_prefork() {
  if (!grpc_fork_support_enabled()) {
    gpr_log(GPR_ERROR,
            "Fork support not enabled; try running with the "
            "environment variable GRPC_ENABLE_FORK_SUPPORT=1");
    return 0;
  }
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_timer_manager_set_threading(false);
  grpc_executor_set_threading(&exec_ctx, false);
  grpc_exec_ctx_finish(&exec_ctx);
  if (!gpr_await_threads(
          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                       gpr_time_from_seconds(3, GPR_TIMESPAN)))) {
    gpr_log(GPR_ERROR, "gRPC thread still active! Cannot fork!");
    return 0;
  }
  return 1;
}

void grpc_postfork_parent() {
  grpc_timer_manager_set_threading(true);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_executor_set_threading(&exec_ctx, true);
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_postfork_child() {
  grpc_timer_manager_set_threading(true);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_executor_set_threading(&exec_ctx, true);
  grpc_exec_ctx_finish(&exec_ctx);

  grpc_wakeup_fds_postfork();
  grpc_fork_engine();
}

#endif  // GRPC_POSIX_FORK
