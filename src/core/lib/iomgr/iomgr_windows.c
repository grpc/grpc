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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include "src/core/lib/iomgr/sockaddr_windows.h"

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/pollset_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"

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

void grpc_iomgr_platform_init(void) {
  winsock_init();
  grpc_iocp_init();
  grpc_pollset_global_init();
}

void grpc_iomgr_platform_flush(void) { grpc_iocp_flush(); }

void grpc_iomgr_platform_shutdown(void) {
  grpc_pollset_global_shutdown();
  grpc_iocp_shutdown();
  winsock_shutdown();
}

#endif /* GRPC_WINSOCK_SOCKET */
