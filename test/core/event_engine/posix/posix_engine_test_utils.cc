// Copyright 2022 gRPC Authors
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

#include "test/core/event_engine/posix/posix_engine_test_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/util/crash.h"

namespace grpc_event_engine {
namespace experimental {

using ResolvedAddress =
    grpc_event_engine::experimental::EventEngine::ResolvedAddress;

using grpc_event_engine::experimental::EventEngineSupportsFdExtension::
    PosixApis;

// Creates a client socket and blocks until it connects to the specified
// server address. The function abort fails upon encountering errors.
EventEngine::FileDescriptor ConnectToServerOrDie(
    const grpc_event_engine::experimental::EventEngineSupportsFdExtension::
        PosixApis& event_engine,
    const ResolvedAddress& server_address) {
  EventEngine::FileDescriptor client_fd;
  int one = 1;
  int flags;

  client_fd = event_engine.(AF_INET6, SOCK_STREAM, 0);
  client_fd.setsockopt(SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  // Make fd non-blocking.
  flags = client_fd.fcntl(F_GETFL, 0);
  client_fd.fcntl(F_SETFL, flags | O_NONBLOCK);

  if (client_fd.connect(const_cast<struct sockaddr*>(server_address.address()),
                        server_address.size()) == -1) {
    if (errno == EINPROGRESS) {
      struct pollfd pfd;
      pfd.fd = client_fd.file_descriptor_for_polling();
      pfd.events = POLLOUT;
      pfd.revents = 0;
      if (poll(&pfd, 1, -1) == -1) {
        LOG(ERROR) << "poll() failed during connect; errno=" << errno;
        abort();
      }
    } else {
      grpc_core::Crash(
          absl::StrFormat("Failed to connect to the server (errno=%d)", errno));
    }
  }
  return client_fd;
}

}  // namespace experimental
}  // namespace grpc_event_engine
