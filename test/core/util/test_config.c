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

#include "test/core/util/test_config.h"

#include <grpc/support/port_platform.h>
#include <grpc/support/log.h>
#include <stdlib.h>
#include <signal.h>

#if GPR_GETPID_IN_UNISTD_H
#include <unistd.h>
static int seed(void) { return getpid(); }
#endif

#if GPR_GETPID_IN_PROCESS_H
#include <process.h>
static int seed(void) { return _getpid(); }
#endif

#if GPR_WINDOWS_CRASH_HANDLER
LONG crash_handler(struct _EXCEPTION_POINTERS* ex_info) {
  gpr_log(GPR_DEBUG, "Exception handler called, dumping information");
  while (ex_info->ExceptionRecord) {
    DWORD code = ex_info->ExceptionRecord->ExceptionCode;
    DWORD flgs = ex_info->ExceptionRecord->ExceptionFlags;
    PVOID addr = ex_info->ExceptionRecord->ExceptionAddress;
    gpr_log("code: %x - flags: %d - address: %p", code, flgs, addr);
    ex_info->ExceptionRecord = ex_info->ExceptionRecord->ExceptionRecord;
  }
  if (IsDebuggerPresent()) {
    __debugbreak();
  } else {
    _exit(1);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

void abort_handler(int sig) {
  gpr_log(GPR_DEBUG, "Abort handler called.");
  if (IsDebuggerPresent()) {
    __debugbreak();
  } else {
    _exit(1);
  }
}

static void install_crash_handler() {
  SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER) crash_handler);
  _set_abort_behavior(0, _WRITE_ABORT_MSG);
  _set_abort_behavior(0, _CALL_REPORTFAULT);
  signal(SIGABRT, abort_handler);
}
#else
static void install_crash_handler() { }
#endif

void grpc_test_init(int argc, char **argv) {
  install_crash_handler();
  gpr_log(GPR_DEBUG, "test slowdown: machine=%f build=%f total=%f",
          (double)GRPC_TEST_SLOWDOWN_MACHINE_FACTOR,
          (double)GRPC_TEST_SLOWDOWN_BUILD_FACTOR,
          (double)GRPC_TEST_SLOWDOWN_FACTOR);
  /* seed rng with pid, so we don't end up with the same random numbers as a
     concurrently running test binary */
  srand(seed());
}
