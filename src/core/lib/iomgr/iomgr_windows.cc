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

#ifdef GRPC_WINSOCK_SOCKET

#include "src/core/lib/iomgr/sockaddr_windows.h"

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/pollset_windows.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/timer.h"

extern grpc_tcp_server_vtable grpc_windows_tcp_server_vtable;
extern grpc_tcp_client_vtable grpc_windows_tcp_client_vtable;
extern grpc_timer_vtable grpc_generic_timer_vtable;
extern grpc_pollset_vtable grpc_windows_pollset_vtable;
extern grpc_pollset_set_vtable grpc_windows_pollset_set_vtable;
extern grpc_address_resolver_vtable grpc_windows_resolver_vtable;

/* Windows' io manager is going to be fully designed using IO completion
   ports. All of what we're doing here is basically make sure that
   Windows sockets are initialized in and out. */

static void winsock_init(void) {
  WSADATA wsaData;
  int status = WSAStartup(MAKEWORD(2, 0), &wsaData);
  GPR_ASSERT(status == 0);
}

static void winsock_shutdown(void) {
  int status = WSACleanup();
  GPR_ASSERT(status == 0);
}

static void iomgr_platform_init(void) {
  winsock_init();
  grpc_iocp_init();
  grpc_pollset_global_init();
}

static void iomgr_platform_flush(void) { grpc_iocp_flush(); }

static void iomgr_platform_shutdown(void) {
  grpc_pollset_global_shutdown();
  grpc_iocp_shutdown();
  winsock_shutdown();
}

static void iomgr_platform_shutdown_background_closure(void) {}

static bool iomgr_platform_is_any_background_poller_thread(void) {
  return false;
}

static grpc_iomgr_platform_vtable vtable = {
    iomgr_platform_init, iomgr_platform_flush, iomgr_platform_shutdown,
    iomgr_platform_shutdown_background_closure,
    iomgr_platform_is_any_background_poller_thread};

void grpc_set_default_iomgr_platform() {
  grpc_set_tcp_client_impl(&grpc_windows_tcp_client_vtable);
  grpc_set_tcp_server_impl(&grpc_windows_tcp_server_vtable);
  grpc_set_timer_impl(&grpc_generic_timer_vtable);
  grpc_set_pollset_vtable(&grpc_windows_pollset_vtable);
  grpc_set_pollset_set_vtable(&grpc_windows_pollset_set_vtable);
  grpc_set_resolver_impl(&grpc_windows_resolver_vtable);
  grpc_set_iomgr_platform_vtable(&vtable);
}

bool grpc_iomgr_run_in_background() { return false; }

#endif /* GRPC_WINSOCK_SOCKET */
