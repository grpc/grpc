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

/* Windows implementation for gpr threads. */

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <string.h>

#if defined(_MSC_VER)
#define thread_local __declspec(thread)
#elif defined(__GNUC__)
#define thread_local __thread
#else
#error "Unknown compiler - please file a bug report"
#endif

struct thd_info {
  void (*body)(void *arg); /* body of a thread */
  void *arg;               /* argument to a thread */
  HANDLE join_event;       /* if joinable, the join event */
  int joinable;            /* true if not detached */
};

static thread_local struct thd_info *g_thd_info;

/* Destroys a thread info */
static void destroy_thread(struct thd_info *t) {
  if (t->joinable) CloseHandle(t->join_event);
  gpr_free(t);
}

/* Body of every thread started via gpr_thd_new. */
static DWORD WINAPI thread_body(void *v) {
  g_thd_info = (struct thd_info *)v;
  g_thd_info->body(g_thd_info->arg);
  if (g_thd_info->joinable) {
    BOOL ret = SetEvent(g_thd_info->join_event);
    GPR_ASSERT(ret);
  } else {
    destroy_thread(g_thd_info);
  }
  return 0;
}

int gpr_thd_new(gpr_thd_id *t, void (*thd_body)(void *arg), void *arg,
                const gpr_thd_options *options) {
  HANDLE handle;
  struct thd_info *info = gpr_malloc(sizeof(*info));
  info->body = thd_body;
  info->arg = arg;
  *t = 0;
  if (gpr_thd_options_is_joinable(options)) {
    info->joinable = 1;
    info->join_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (info->join_event == NULL) {
      gpr_free(info);
      return 0;
    }
  } else {
    info->joinable = 0;
  }
  handle = CreateThread(NULL, 64 * 1024, thread_body, info, 0, NULL);
  if (handle == NULL) {
    destroy_thread(info);
  } else {
    *t = (gpr_thd_id)info;
    CloseHandle(handle);
  }
  return handle != NULL;
}

gpr_thd_id gpr_thd_currentid(void) { return (gpr_thd_id)g_thd_info; }

void gpr_thd_join(gpr_thd_id t) {
  struct thd_info *info = (struct thd_info *)t;
  DWORD ret = WaitForSingleObject(info->join_event, INFINITE);
  GPR_ASSERT(ret == WAIT_OBJECT_0);
  destroy_thread(info);
}

#endif /* GPR_WINDOWS */
