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
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <grpc/support/log.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_windows.h"

static void create_sockets(SOCKET sv[2]) {
  SOCKET svr_sock = INVALID_SOCKET;
  SOCKET lst_sock = INVALID_SOCKET;
  SOCKET cli_sock = INVALID_SOCKET;
  SOCKADDR_IN addr;
  int addr_len = sizeof(addr);

  lst_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                       grpc_get_default_wsa_socket_flags());
  GPR_ASSERT(lst_sock != INVALID_SOCKET);

  memset(&addr, 0, sizeof(addr));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_family = AF_INET;
  GPR_ASSERT(bind(lst_sock, (grpc_sockaddr*)&addr, sizeof(addr)) !=
             SOCKET_ERROR);
  GPR_ASSERT(listen(lst_sock, SOMAXCONN) != SOCKET_ERROR);
  GPR_ASSERT(getsockname(lst_sock, (grpc_sockaddr*)&addr, &addr_len) !=
             SOCKET_ERROR);

  cli_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                       grpc_get_default_wsa_socket_flags());
  GPR_ASSERT(cli_sock != INVALID_SOCKET);

  GPR_ASSERT(WSAConnect(cli_sock, (grpc_sockaddr*)&addr, addr_len, NULL, NULL,
                        NULL, NULL) == 0);
  svr_sock = accept(lst_sock, (grpc_sockaddr*)&addr, &addr_len);
  GPR_ASSERT(svr_sock != INVALID_SOCKET);

  closesocket(lst_sock);
  grpc_tcp_prepare_socket(cli_sock);
  grpc_tcp_prepare_socket(svr_sock);

  sv[1] = cli_sock;
  sv[0] = svr_sock;
}

grpc_endpoint_pair grpc_iomgr_create_endpoint_pair(
    const char*, grpc_channel_args* channel_args) {
  SOCKET sv[2];
  grpc_endpoint_pair p;
  create_sockets(sv);
  grpc_core::ExecCtx exec_ctx;
  p.client = grpc_tcp_create(grpc_winsocket_create(sv[1], "endpoint:client"),
                             channel_args, "endpoint:server");
  p.server = grpc_tcp_create(grpc_winsocket_create(sv[0], "endpoint:server"),
                             channel_args, "endpoint:client");
  return p;
}

#endif
