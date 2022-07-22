// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <grpc/support/port_platform.h>

#include "test/core/event_engine/windows/create_sockpair.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "absl/status/status.h"

#include "src/core/lib/iomgr/error.h"

// DO NOT SUBMIT(hork): cribbed from tcp_windows, it should not live here
// permanently
#if defined(__MSYS__) && defined(GPR_ARCH_64)
/* Nasty workaround for nasty bug when using the 64 bits msys compiler
   in conjunction with Microsoft Windows headers. */
#define GRPC_FIONBIO _IOW('f', 126, uint32_t)
#else
#define GRPC_FIONBIO FIONBIO
#endif

namespace {

grpc_error_handle grpc_tcp_set_non_block(SOCKET sock) {
  int status;
  uint32_t param = 1;
  DWORD ret;
  status = WSAIoctl(sock, GRPC_FIONBIO, &param, sizeof(param), NULL, 0, &ret,
                    NULL, NULL);
  return status == 0
             ? GRPC_ERROR_NONE
             : GRPC_WSA_ERROR(WSAGetLastError(), "WSAIoctl(GRPC_FIONBIO)");
}

static grpc_error_handle set_dualstack(SOCKET sock) {
  int status;
  DWORD param = 0;
  status = setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&param,
                      sizeof(param));
  return status == 0
             ? GRPC_ERROR_NONE
             : GRPC_WSA_ERROR(WSAGetLastError(), "setsockopt(IPV6_V6ONLY)");
}

static grpc_error_handle enable_socket_low_latency(SOCKET sock) {
  int status;
  BOOL param = TRUE;
  status = ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                        reinterpret_cast<char*>(&param), sizeof(param));
  if (status == SOCKET_ERROR) {
    status = WSAGetLastError();
  }
  return status == 0 ? GRPC_ERROR_NONE
                     : GRPC_WSA_ERROR(status, "setsockopt(TCP_NODELAY)");
}

absl::Status PrepareSocket(SOCKET sock) {
  // DO NOT SUBMIT(hork): cribbed from tcp_windows. this needs to live somewhere
  // in prod code
  absl::Status err;
  err = grpc_tcp_set_non_block(sock);
  if (!GRPC_ERROR_IS_NONE(err)) return err;
  // DO NOT SUBMIT(hork): WinServer does not support dual stack. How is iomgr
  // working?
  //   err = set_dualstack(sock);
  //   if (!GRPC_ERROR_IS_NONE(err)) return err;
  err = enable_socket_low_latency(sock);
  if (!GRPC_ERROR_IS_NONE(err)) return err;
  return GRPC_ERROR_NONE;
}

}  // namespace

void CreateSockpair(SOCKET sockpair[2], DWORD flags) {
  //  DO NOT SUBMIT(hork): cribbed from endpoint_pair_windows.cc. This will
  //  probably be more broadly useful elsewhere.
  SOCKET svr_sock = INVALID_SOCKET;
  SOCKET lst_sock = INVALID_SOCKET;
  SOCKET cli_sock = INVALID_SOCKET;
  SOCKADDR_IN addr;
  int addr_len = sizeof(addr);

  lst_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, flags);
  GPR_ASSERT(lst_sock != INVALID_SOCKET);

  memset(&addr, 0, sizeof(addr));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_family = AF_INET;
  GPR_ASSERT(bind(lst_sock, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR);
  GPR_ASSERT(listen(lst_sock, SOMAXCONN) != SOCKET_ERROR);
  GPR_ASSERT(getsockname(lst_sock, (sockaddr*)&addr, &addr_len) !=
             SOCKET_ERROR);

  cli_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, flags);
  GPR_ASSERT(cli_sock != INVALID_SOCKET);

  GPR_ASSERT(WSAConnect(cli_sock, (sockaddr*)&addr, addr_len, NULL, NULL, NULL,
                        NULL) == 0);
  svr_sock = accept(lst_sock, (sockaddr*)&addr, &addr_len);
  GPR_ASSERT(svr_sock != INVALID_SOCKET);

  closesocket(lst_sock);
  GPR_ASSERT(PrepareSocket(cli_sock).ok());
  GPR_ASSERT(PrepareSocket(svr_sock).ok());

  sockpair[0] = svr_sock;
  sockpair[1] = cli_sock;
}
