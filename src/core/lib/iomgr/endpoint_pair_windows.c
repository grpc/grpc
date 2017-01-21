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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <grpc/support/log.h>
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_windows.h"

static void create_sockets(SOCKET sv[2]) {
  SOCKET svr_sock = INVALID_SOCKET;
  SOCKET lst_sock = INVALID_SOCKET;
  SOCKET cli_sock = INVALID_SOCKET;
  SOCKADDR_IN addr;
  int addr_len = sizeof(addr);

  lst_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                       WSA_FLAG_OVERLAPPED);
  GPR_ASSERT(lst_sock != INVALID_SOCKET);

  memset(&addr, 0, sizeof(addr));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_family = AF_INET;
  GPR_ASSERT(bind(lst_sock, (struct sockaddr *)&addr, sizeof(addr)) !=
             SOCKET_ERROR);
  GPR_ASSERT(listen(lst_sock, SOMAXCONN) != SOCKET_ERROR);
  GPR_ASSERT(getsockname(lst_sock, (struct sockaddr *)&addr, &addr_len) !=
             SOCKET_ERROR);

  cli_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                       WSA_FLAG_OVERLAPPED);
  GPR_ASSERT(cli_sock != INVALID_SOCKET);

  GPR_ASSERT(WSAConnect(cli_sock, (struct sockaddr *)&addr, addr_len, NULL,
                        NULL, NULL, NULL) == 0);
  svr_sock = accept(lst_sock, (struct sockaddr *)&addr, &addr_len);
  GPR_ASSERT(svr_sock != INVALID_SOCKET);

  closesocket(lst_sock);
  grpc_tcp_prepare_socket(cli_sock);
  grpc_tcp_prepare_socket(svr_sock);

  sv[1] = cli_sock;
  sv[0] = svr_sock;
}

grpc_endpoint_pair grpc_iomgr_create_endpoint_pair(
    const char *name, grpc_resource_quota *resource_quota,
    size_t read_slice_size) {
  SOCKET sv[2];
  grpc_endpoint_pair p;
  create_sockets(sv);
  p.client = grpc_tcp_create(grpc_winsocket_create(sv[1], "endpoint:client"),
                             resource_quota, "endpoint:server");
  p.server = grpc_tcp_create(grpc_winsocket_create(sv[0], "endpoint:server"),
                             resource_quota, "endpoint:client");
  return p;
}

#endif
