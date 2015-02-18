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

/* Posix implementation for gpr threads. */

#include <grpc/support/port_platform.h>

#ifdef GPR_WIN32

#include <windows.h>
#include <string.h>
#include <grpc/support/alloc.h>
#include <grpc/support/thd.h>

struct thd_arg {
  void (*body)(void *arg); /* body of a thread */
  void *arg;               /* argument to a thread */
};

/* Body of every thread started via gpr_thd_new. */
static DWORD WINAPI thread_body(void *v) {
  struct thd_arg a = *(struct thd_arg *)v;
  gpr_free(v);
  (*a.body)(a.arg);
  return 0;
}

int gpr_thd_new(gpr_thd_id *t, void (*thd_body)(void *arg), void *arg,
                const gpr_thd_options *options) {
  HANDLE handle;
  DWORD thread_id;
  struct thd_arg *a = gpr_malloc(sizeof(*a));
  a->body = thd_body;
  a->arg = arg;
  *t = 0;
  handle = CreateThread(NULL, 64 * 1024, thread_body, a, 0, &thread_id);
  if (handle == NULL) {
    gpr_free(a);
  } else {
    CloseHandle(handle); /* threads are "detached" */
  }
  *t = (gpr_thd_id)thread_id;
  return handle != NULL;
}

gpr_thd_options gpr_thd_options_default(void) {
  gpr_thd_options options;
  memset(&options, 0, sizeof(options));
  return options;
}

gpr_thd_id gpr_thd_currentid(void) {
  return (gpr_thd_id)GetCurrentThreadId();
}

#endif /* GPR_WIN32 */
