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

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/iomgr_custom.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/timer_custom.h"

#include <uv.h>

static void timer_close_callback(uv_handle_t* handle) { gpr_free(handle); }

static void stop_uv_timer(uv_timer_t* handle) {
  uv_timer_stop(handle);
  uv_unref((uv_handle_t*)handle);
  uv_close((uv_handle_t*)handle, timer_close_callback);
}

void run_expired_timer(uv_timer_t* handle) {
  grpc_custom_timer* timer_wrapper = (grpc_custom_timer*)handle->data;
  grpc_custom_timer_callback(timer_wrapper, GRPC_ERROR_NONE);
}

static void timer_start(grpc_custom_timer* t) {
  uv_timer_t* uv_timer;
  uv_timer = (uv_timer_t*)gpr_malloc(sizeof(uv_timer_t));
  uv_timer_init(uv_default_loop(), uv_timer);
  uv_timer->data = t;
  t->timer = (void*)uv_timer;
  uv_timer_start(uv_timer, run_expired_timer, t->timeout_ms, 0);
}

static void timer_stop(grpc_custom_timer* t) {
  stop_uv_timer((uv_timer_t*)t->timer);
}

grpc_custom_timer_vtable uv_timer_vtable = {timer_start, timer_stop};

#endif
