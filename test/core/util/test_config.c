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
#include "src/core/support/string.h"
#include <stdlib.h>
#include <signal.h>

double g_fixture_slowdown_factor = 1.0;

#if GPR_GETPID_IN_UNISTD_H
#include <unistd.h>
static unsigned seed(void) { return (unsigned)getpid(); }
#endif

#if GPR_GETPID_IN_PROCESS_H
#include <process.h>
static unsigned seed(void) { return _getpid(); }
#endif

#if GPR_WINDOWS_CRASH_HANDLER
LONG crash_handler(struct _EXCEPTION_POINTERS *ex_info) {
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
  SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)crash_handler);
  _set_abort_behavior(0, _WRITE_ABORT_MSG);
  _set_abort_behavior(0, _CALL_REPORTFAULT);
  signal(SIGABRT, abort_handler);
}
#elif GPR_POSIX_CRASH_HANDLER
#include <execinfo.h>
#include <stdio.h>
#include <string.h>
#include <grpc/support/useful.h>
#include <errno.h>

static char g_alt_stack[MINSIGSTKSZ];

#define MAX_FRAMES 32

/* signal safe output */
static void output_string(const char *string) {
  size_t len = strlen(string);
  ssize_t r;

  do {
    r = write(STDERR_FILENO, string, len);
  } while (r == -1 && errno == EINTR);
}

static void output_num(long num) {
  char buf[GPR_LTOA_MIN_BUFSIZE];
  gpr_ltoa(num, buf);
  output_string(buf);
}

static void crash_handler(int signum, siginfo_t *info, void *data) {
  void *addrlist[MAX_FRAMES + 1];
  int addrlen;

  output_string("\n\n\n*******************************\nCaught signal ");
  output_num(signum);
  output_string("\n");

  addrlen = backtrace(addrlist, GPR_ARRAY_SIZE(addrlist));

  if (addrlen == 0) {
    output_string("  no backtrace\n");
  } else {
    backtrace_symbols_fd(addrlist, addrlen, STDERR_FILENO);
  }

  raise(signum);
}

static void install_crash_handler() {
  stack_t ss;
  struct sigaction sa;

  memset(&ss, 0, sizeof(ss));
  memset(&sa, 0, sizeof(sa));
  ss.ss_size = sizeof(g_alt_stack);
  ss.ss_sp = g_alt_stack;
  GPR_ASSERT(sigaltstack(&ss, NULL) == 0);
  sa.sa_flags = (int)(SA_SIGINFO | SA_ONSTACK | SA_RESETHAND);
  sa.sa_sigaction = crash_handler;
  GPR_ASSERT(sigaction(SIGILL, &sa, NULL) == 0);
  GPR_ASSERT(sigaction(SIGABRT, &sa, NULL) == 0);
  GPR_ASSERT(sigaction(SIGBUS, &sa, NULL) == 0);
  GPR_ASSERT(sigaction(SIGSEGV, &sa, NULL) == 0);
}
#else
static void install_crash_handler() {}
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
