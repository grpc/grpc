/*
 *
 * Copyright 2016 gRPC authors.
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

#ifdef GRPC_UV

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/lib/iomgr/pollset_custom.h"

#include <uv.h>

/* Indicates that grpc_pollset_work should run an iteration of the UV loop
   before running callbacks. This defaults to 1, and should be disabled if
   grpc_pollset_work will be called within the callstack of uv_run */
int grpc_pollset_work_run_loop = 1;

static bool g_kicked = false;

typedef struct uv_poller_handle {
  uv_timer_t poll_timer;
  uv_timer_t kick_timer;
  int refs;
} uv_poller_handle;

static uv_poller_handle* g_handle;

static void init() {
  g_handle = (uv_poller_handle*)gpr_malloc(sizeof(uv_poller_handle));
  g_handle->refs = 2;
  uv_timer_init(uv_default_loop(), &g_handle->poll_timer);
  uv_timer_init(uv_default_loop(), &g_handle->kick_timer);
}

static void empty_timer_cb(uv_timer_t* handle) {}

static void kick_timer_cb(uv_timer_t* handle) { g_kicked = false; }

static void run_loop(size_t timeout) {
  if (grpc_pollset_work_run_loop) {
    if (timeout == 0) {
      uv_run(uv_default_loop(), UV_RUN_NOWAIT);
    } else {
      uv_timer_start(&g_handle->poll_timer, empty_timer_cb, timeout, 0);
      uv_run(uv_default_loop(), UV_RUN_ONCE);
      uv_timer_stop(&g_handle->poll_timer);
    }
  }
}

static void kick() {
  if (!g_kicked) {
    g_kicked = true;
    uv_timer_start(&g_handle->kick_timer, kick_timer_cb, 0, 0);
  }
}

static void close_timer_cb(uv_handle_t* handle) {
  g_handle->refs--;
  if (g_handle->refs == 0) {
    gpr_free(g_handle);
  }
}

static void shutdown() {
  uv_close((uv_handle_t*)&g_handle->poll_timer, close_timer_cb);
  uv_close((uv_handle_t*)&g_handle->kick_timer, close_timer_cb);
  if (grpc_pollset_work_run_loop) {
    GPR_ASSERT(uv_run(uv_default_loop(), UV_RUN_DEFAULT) == 0);
  }
}

grpc_custom_poller_vtable uv_pollset_vtable = {init, run_loop, kick, shutdown};

#endif /* GRPC_UV */
