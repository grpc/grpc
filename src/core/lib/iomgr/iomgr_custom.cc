/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/thd_id.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_custom.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset_custom.h"
#include "src/core/lib/iomgr/pollset_set_custom.h"
#include "src/core/lib/iomgr/resolve_address_custom.h"

gpr_thd_id g_init_thread;

static void iomgr_platform_init(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_executor_set_threading(false);
  g_init_thread = gpr_thd_currentid();
  grpc_pollset_global_init();
}
static void iomgr_platform_flush(void) {}
static void iomgr_platform_shutdown(void) { grpc_pollset_global_shutdown(); }
static void iomgr_platform_shutdown_background_closure(void) {}

static grpc_iomgr_platform_vtable vtable = {
    iomgr_platform_init, iomgr_platform_flush, iomgr_platform_shutdown,
    iomgr_platform_shutdown_background_closure};

void grpc_custom_iomgr_init(grpc_socket_vtable* socket,
                            grpc_custom_resolver_vtable* resolver,
                            grpc_custom_timer_vtable* timer,
                            grpc_custom_poller_vtable* poller) {
  grpc_custom_endpoint_init(socket);
  grpc_custom_timer_init(timer);
  grpc_custom_pollset_init(poller);
  grpc_custom_pollset_set_init();
  grpc_custom_resolver_init(resolver);
  grpc_set_iomgr_platform_vtable(&vtable);
}

#ifdef GRPC_CUSTOM_SOCKET
grpc_iomgr_platform_vtable* grpc_default_iomgr_platform_vtable() {
  return &vtable;
}
#endif
