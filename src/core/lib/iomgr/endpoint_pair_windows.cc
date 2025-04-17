//
//
// Copyright 2015 gRPC authors.
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
#include <string.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/extensions/supports_win_sockets.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#include "src/core/util/crash.h"

static void create_sockets(SOCKET sv[2]) {
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
                       grpc_get_default_wsa_socket_flags());
  CHECK(cli_sock != INVALID_SOCKET);

  CHECK_EQ(WSAConnect(cli_sock, (grpc_sockaddr*)&addr, addr_len, NULL, NULL,
                      NULL, NULL),
           0);
  svr_sock = accept(lst_sock, (grpc_sockaddr*)&addr, &addr_len);
  CHECK_NE(svr_sock, INVALID_SOCKET);

  closesocket(lst_sock);
  grpc_error_handle error = grpc_tcp_prepare_socket(cli_sock);
  if (!error.ok()) {
    VLOG(2) << "Prepare cli_sock failed with error: "
            << grpc_core::StatusToString(error);
  }
  error = grpc_tcp_prepare_socket(svr_sock);
  if (!error.ok()) {
    VLOG(2) << "Prepare svr_sock failed with error: "
            << grpc_core::StatusToString(error);
  }

  sv[1] = cli_sock;
  sv[0] = svr_sock;
}

grpc_endpoint_pair grpc_iomgr_create_endpoint_pair(
    const char*, const grpc_channel_args* channel_args) {
  SOCKET sv[2];
  grpc_endpoint_pair p;
  create_sockets(sv);
  grpc_core::ExecCtx exec_ctx;
  if (grpc_core::IsPollsetAlternativeEnabled()) {
    auto new_args = grpc_core::CoreConfiguration::Get()
                        .channel_args_preconditioning()
                        .PreconditionChannelArgs(channel_args);
    auto* event_engine_supports_win_sockets =
        grpc_event_engine::experimental::QueryExtension<
            grpc_event_engine::experimental::EventEngineWindowsSocketSupport>(
            new_args
                .GetObjectRef<grpc_event_engine::experimental::EventEngine>()
                .get());
    CHECK_NE(event_engine_supports_win_sockets, nullptr)
        << "EventEngine does not support windows SOCKETS, so an endpoint pair "
           "cannot be created.";
    auto client_endpoint =
        event_engine_supports_win_sockets->CreateEndpointFromWinSocket(
            sv[1], grpc_event_engine::experimental::ChannelArgsEndpointConfig(
                       new_args));
    CHECK_NE(client_endpoint.get(), nullptr)
        << "Failed to create client endpoint";
    auto server_endpoint =
        event_engine_supports_win_sockets->CreateEndpointFromWinSocket(
            sv[0], grpc_event_engine::experimental::ChannelArgsEndpointConfig(
                       new_args));
    CHECK_NE(server_endpoint.get(), nullptr)
        << "Failed to create server endpoint";
    p.client = grpc_event_engine_endpoint_create(std::move(client_endpoint));
    p.server = grpc_event_engine_endpoint_create(std::move(server_endpoint));
  } else {
    p.client = grpc_tcp_create(grpc_winsocket_create(sv[1], "endpoint:client"),
                               "endpoint:server");
    p.server = grpc_tcp_create(grpc_winsocket_create(sv[0], "endpoint:server"),
                               "endpoint:client");
  }
  return p;
}

#endif
