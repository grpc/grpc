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

#ifdef GRPC_POSIX_SOCKET_TCP
#include <errno.h>
#include <fcntl.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <string>

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint_pair.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/util/string.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"

namespace grpc_event_engine {
namespace experimental {

void CreateSockets(int sv[2]) {
  int flags;
  grpc_create_socketpair_if_unix(sv);
  flags = fcntl(sv[0], F_GETFL, 0);
  CHECK_EQ(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK), 0);
  flags = fcntl(sv[1], F_GETFL, 0);
  CHECK_EQ(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK), 0);
  CHECK(grpc_set_socket_no_sigpipe_if_possible(sv[0]) == absl::OkStatus());
  CHECK(grpc_set_socket_no_sigpipe_if_possible(sv[1]) == absl::OkStatus());
}

EndpointPair CreateEndpointPair(grpc_core::ChannelArgs& args,
                                ThreadPool* thread_pool) {
  std::unique_ptr<EventEngine::Endpoint> client_endpoint = nullptr;
  std::unique_ptr<EventEngine::Endpoint> server_endpoint = nullptr;

  int sv[2];
  CreateSockets(sv);
  std::string server_addr = "socketpair-server";
  std::string client_addr = "socketpair-client";
  grpc_fd* client_fd = grpc_fd_create(sv[1], "fixture:client", false);
  grpc_fd* server_fd = grpc_fd_create(sv[0], "fixture:server", false);

  auto ee = GetDefaultEventEngine();
  auto* supports_fd = QueryExtension<EventEngineSupportsFdExtension>(
      /*engine=*/ee.get());
  if (supports_fd != nullptr) {
    client_endpoint = supports_fd->CreateEndpointFromFd(
        grpc_fd_wrapped_fd(client_fd), ChannelArgsEndpointConfig(args));
    server_endpoint = supports_fd->CreateEndpointFromFd(
        grpc_fd_wrapped_fd(server_fd), ChannelArgsEndpointConfig(args));
  }

  return EndpointPair(std::move(client_endpoint), std::move(server_endpoint));
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif