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
#ifdef GPR_WINDOWS

#include <winsock2.h>
#include <ws2tcpip.h>

#include "absl/status/status.h"

#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/iomgr/error.h"
#include "test/core/event_engine/windows/create_sockpair.h"

namespace grpc_event_engine {
namespace experimental {

sockaddr_in GetSomeIpv4LoopbackAddress() {
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_family = AF_INET;
  return addr;
}

void CreateSockpair(SOCKET sockpair[2], DWORD flags) {
  SOCKET svr_sock = INVALID_SOCKET;
  SOCKET lst_sock = INVALID_SOCKET;
  SOCKET cli_sock = INVALID_SOCKET;
  auto addr = GetSomeIpv4LoopbackAddress();
  int addr_len = sizeof(addr);

  lst_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, flags);
  GPR_ASSERT(lst_sock != INVALID_SOCKET);

  GPR_ASSERT(bind(lst_sock, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR);
  GPR_ASSERT(listen(lst_sock, SOMAXCONN) != SOCKET_ERROR);
  GPR_ASSERT(getsockname(lst_sock, (sockaddr*)&addr, &addr_len) !=
             SOCKET_ERROR);

  cli_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, flags);
  GPR_ASSERT(cli_sock != INVALID_SOCKET);

  auto result =
      WSAConnect(cli_sock, (sockaddr*)&addr, addr_len, NULL, NULL, NULL, NULL);
  if (result != 0) {
    gpr_log(GPR_DEBUG, "%s",
            GRPC_WSA_ERROR(WSAGetLastError(), "Failed in WSAConnect")
                .ToString()
                .c_str());
    abort();
  }
  svr_sock = accept(lst_sock, (sockaddr*)&addr, &addr_len);
  GPR_ASSERT(svr_sock != INVALID_SOCKET);
  closesocket(lst_sock);
  // TODO(hork): see if we can migrate this to IPv6, or break up the socket prep
  // stages.
  // Historical note: This method creates an ipv4 sockpair, which cannot
  // be made dual stack. This was silently preventing TCP_NODELAY from being
  // enabled, but not causing an unrecoverable error. So this is left as a
  // logged status. WSAEINVAL is expected.
  auto status = PrepareSocket(cli_sock);
  // if (!status.ok()) {
  //   gpr_log(GPR_DEBUG, "Error preparing client socket: %s",
  //           status.ToString().c_str());
  // }
  status = PrepareSocket(svr_sock);
  // if (!status.ok()) {
  //   gpr_log(GPR_DEBUG, "Error preparing server socket: %s",
  //           status.ToString().c_str());
  // }

  sockpair[0] = svr_sock;
  sockpair[1] = cli_sock;
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
