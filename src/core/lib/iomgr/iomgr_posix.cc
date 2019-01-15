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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_IOMGR

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/iomgr_posix.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/timer.h"

extern grpc_tcp_server_vtable grpc_posix_tcp_server_vtable;
extern grpc_tcp_client_vtable grpc_posix_tcp_client_vtable;
extern grpc_timer_vtable grpc_generic_timer_vtable;
extern grpc_pollset_vtable grpc_posix_pollset_vtable;
extern grpc_pollset_set_vtable grpc_posix_pollset_set_vtable;
extern grpc_address_resolver_vtable grpc_posix_resolver_vtable;

static void iomgr_platform_init(void) {
  grpc_wakeup_fd_global_init();
  grpc_event_engine_init();
}

static void iomgr_platform_flush(void) {}

static void iomgr_platform_shutdown(void) {
  grpc_event_engine_shutdown();
  grpc_wakeup_fd_global_destroy();
}

static void iomgr_platform_shutdown_background_closure(void) {
  grpc_shutdown_background_closure();
}

static bool iomgr_platform_is_any_background_poller_thread(void) {
  return grpc_is_any_background_poller_thread();
}

static grpc_iomgr_platform_vtable vtable = {
    iomgr_platform_init, iomgr_platform_flush, iomgr_platform_shutdown,
    iomgr_platform_shutdown_background_closure,
    iomgr_platform_is_any_background_poller_thread};

void grpc_set_default_iomgr_platform() {
  grpc_set_tcp_client_impl(&grpc_posix_tcp_client_vtable);
  grpc_set_tcp_server_impl(&grpc_posix_tcp_server_vtable);
  grpc_set_timer_impl(&grpc_generic_timer_vtable);
  grpc_set_pollset_vtable(&grpc_posix_pollset_vtable);
  grpc_set_pollset_set_vtable(&grpc_posix_pollset_set_vtable);
  grpc_set_resolver_impl(&grpc_posix_resolver_vtable);
  grpc_set_iomgr_platform_vtable(&vtable);
}

#endif /* GRPC_POSIX_SOCKET_IOMGR */
