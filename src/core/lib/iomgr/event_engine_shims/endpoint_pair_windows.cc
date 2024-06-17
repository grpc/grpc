//
//
// Copyright 2024 gRPC authors.
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
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET
#include <errno.h>
#include <fcntl.h>
#include <grpc/event_engine/event_engine.h>
#include <string.h>

#include <memory>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/event_engine/windows/windows_endpoint.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint_pair.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#include "src/core/lib/resource_quota/api.h"
#include "absl/log/check.h"
#include "absl/log/log.h"

namespace grpc_event_engine {
namespace experimental {

void CreateSockets(SOCKET sv[2]) {
  SOCKET svr_sock = INVALID_SOCKET;
  SOCKET lst_sock = INVALID_SOCKET;
  SOCKET cli_sock = INVALID_SOCKET;
  SOCKADDR_IN addr;
  int addr_len = sizeof(addr);

  lst_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                       grpc_get_default_wsa_socket_flags());
  CHECK(lst_sock != INVALID_SOCKET);

  memset(&addr, 0, sizeof(addr));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_family = AF_INET;
  CHECK(bind(lst_sock, (grpc_sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR);
  CHECK(listen(lst_sock, SOMAXCONN) != SOCKET_ERROR);
  CHECK(getsockname(lst_sock, (grpc_sockaddr*)&addr, &addr_len) !=
        SOCKET_ERROR);

  cli_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                       ::grpc_get_default_wsa_socket_flags());
  CHECK(cli_sock != INVALID_SOCKET);

  CHECK(WSAConnect(cli_sock, (grpc_sockaddr*)&addr, addr_len, NULL, NULL, NULL,
                   NULL) == 0);
  svr_sock = accept(lst_sock, (grpc_sockaddr*)&addr, &addr_len);
  CHECK(svr_sock != INVALID_SOCKET);

  closesocket(lst_sock);
  grpc_error_handle error = grpc_tcp_prepare_socket(cli_sock);
  if (!error.ok()) {
    LOG(INFO) << "Prepare cli_sock failed with error: "
              << grpc_core::StatusToString(error);
  }
  error = grpc_tcp_prepare_socket(svr_sock);
  if (!error.ok()) {
    LOG(INFO) << "Prepare svr_sock failed with error: "
              << grpc_core::StatusToString(error);
  }

  sv[1] = cli_sock;
  sv[0] = svr_sock;
}

EndpointPair CreateEndpointPair(grpc_core::ChannelArgs& args,
                                ThreadPool* thread_pool) {
  std::unique_ptr<EventEngine::Endpoint> client_endpoint = nullptr;
  std::unique_ptr<EventEngine::Endpoint> server_endpoint = nullptr;

  SOCKET sv[2];
  CreateSockets(sv);
  auto client_socket = std::make_unique<WinSocket>(sv[1], thread_pool);
  auto server_socket = std::make_unique<WinSocket>(sv[0], thread_pool);
  std::string server_addr = "endpoint:server";
  std::string client_addr = "endpoint:client";
  grpc_core::MemoryQuota quota = grpc_core::MemoryQuota("foo");
  client_endpoint = std::make_unique<WindowsEndpoint>(
      EventEngine::ResolvedAddress(
          reinterpret_cast<const sockaddr*>(server_addr.data()),
          server_addr.size()),
      std::move(client_socket), quota.CreateMemoryAllocator("client"),
      ChannelArgsEndpointConfig(args), thread_pool, GetDefaultEventEngine());
  server_endpoint = std::make_unique<WindowsEndpoint>(
      EventEngine::ResolvedAddress(
          reinterpret_cast<const sockaddr*>(client_addr.data()),
          server_addr.size()),
      std::move(server_socket), quota.CreateMemoryAllocator("server"),
      ChannelArgsEndpointConfig(args), thread_pool, GetDefaultEventEngine());

  return EndpointPair(std::move(client_endpoint), std::move(server_endpoint));
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif
