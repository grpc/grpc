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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINSOCK_SOCKET

#include <grpc/support/thd.h>

#include "src/core/iomgr/alarm_internal.h"
#include "src/core/iomgr/iomgr_internal.h"
#include "src/core/iomgr/pollset_windows.h"

void grpc_pollset_init(grpc_pollset *pollset) {
  gpr_mu_init(&pollset->mu);
  gpr_cv_init(&pollset->cv);
}

void grpc_pollset_shutdown(grpc_pollset *pollset,
                           void (*shutdown_done)(void *arg),
                           void *shutdown_done_arg) {
  shutdown_done(shutdown_done_arg);
}

void grpc_pollset_destroy(grpc_pollset *pollset) {
  gpr_mu_destroy(&pollset->mu);
  gpr_cv_destroy(&pollset->cv);
}

int grpc_pollset_work(grpc_pollset *pollset, gpr_timespec deadline) {
  gpr_timespec now;
  now = gpr_now();
  if (gpr_time_cmp(now, deadline) > 0) {
    return 0;
  }
  if (grpc_maybe_call_delayed_callbacks(NULL, 1)) {
    return 1;
  }
  if (grpc_alarm_check(NULL, now, &deadline)) {
    return 1;
  }
  return 0;
}

void grpc_pollset_kick(grpc_pollset *p) { }

#endif  /* GPR_WINSOCK_SOCKET */
