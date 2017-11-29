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
  void (*body)(void* arg); /* body of a thread */
  void* arg;               /* argument to a thread */
  HANDLE join_event;       /* if joinable, the join event */
  int joinable;            /* true if not detached */
};

static thread_local struct thd_info* g_thd_info;

/* Destroys a thread info */
static void destroy_thread(struct thd_info* t) {
  if (t->joinable) CloseHandle(t->join_event);
  gpr_free(t);
}

/* Body of every thread started via gpr_thd_new. */
static DWORD WINAPI thread_body(void* v) {
  g_thd_info = (struct thd_info*)v;
  g_thd_info->body(g_thd_info->arg);
  if (g_thd_info->joinable) {
    BOOL ret = SetEvent(g_thd_info->join_event);
    GPR_ASSERT(ret);
  } else {
    destroy_thread(g_thd_info);
  }
  return 0;
}

int gpr_thd_new(gpr_thd_id* t, void (*thd_body)(void* arg), void* arg,
                const gpr_thd_options* options) {
  HANDLE handle;
  struct thd_info* info = (struct thd_info*)gpr_malloc(sizeof(*info));
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
  struct thd_info* info = (struct thd_info*)t;
  DWORD ret = WaitForSingleObject(info->join_event, INFINITE);
  GPR_ASSERT(ret == WAIT_OBJECT_0);
  destroy_thread(info);
}

#endif /* GPR_WINDOWS */
