//
//
// Copyright 2017 gRPC authors.
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

#include "test/core/test_util/socket_use_after_close_detector.h"

#include <errno.h>
#include <fcntl.h>
#include <grpc/support/port_platform.h>
#include <string.h>

// IWYU pragma: no_include <arpa/inet.h>
// IWYU pragma: no_include <unistd.h>

#include <grpc/support/sync.h>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "test/core/test_util/port.h"

#define BAD_SOCKET_RETURN_VAL (-1)

namespace {

#ifdef GPR_WINDOWS
void OpenAndCloseSocketsStressLoop(int port, gpr_event* done_ev) {
  // TODO(apolcyn): re-enable this on windows if we can debug the failure.
  // Previously, this was causing test flakes for a while b/c bind calls
  // would fail with WSAEACCESS. Not clear if we were just making windows
  // unhappy.
  LOG(INFO) << "OpenAndCloseSocketsStressLoop is a no-op for windows";
  return;
}
#else
void OpenAndCloseSocketsStressLoop(int port, gpr_event* done_ev) {
  // The goal of this loop is to catch socket
  // "use after close" bugs within the c-ares resolver by acting
  // like some separate thread doing I/O.
  // It's goal is to try to hit race conditions whereby:
  //    1) The c-ares resolver closes a socket.
  //    2) This loop opens a socket with (coincidentally) the same handle.
  //    3) the c-ares resolver mistakenly uses that same socket without
  //       realizing that its closed.
  //    4) This loop performs an operation on that socket that should
  //       succeed but instead fails because of what the c-ares
  //       resolver did in the meantime.
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port);
  (reinterpret_cast<char*>(&addr.sin6_addr))[15] = 1;
  for (;;) {
    if (gpr_event_get(done_ev)) {
      return;
    }
    std::vector<int> sockets;
    // First open a bunch of sockets, bind and listen
    // '50' is an arbitrary number that, experimentally,
    // has a good chance of catching bugs.
    for (size_t i = 0; i < 50; i++) {
      int s = socket(AF_INET6, SOCK_STREAM, 0);
      int val = 1;
      ASSERT_TRUE(setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val)) ==
                  0)
          << "Failed to set socketopt reuseport";
      ASSERT_TRUE(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) ==
                  0)
          << "Failed to set socket reuseaddr";
      ASSERT_TRUE(fcntl(s, F_SETFL, O_NONBLOCK) == 0)
          << "Failed to set socket non-blocking";
      ASSERT_TRUE(s != BAD_SOCKET_RETURN_VAL)
          << "Failed to create TCP ipv6 socket";
      ASSERT_TRUE(bind(s, (const sockaddr*)&addr, sizeof(addr)) == 0)
          << "Failed to bind socket " + std::to_string(s) +
                 " to [::1]:" + std::to_string(port) +
                 ". errno: " + std::to_string(errno);
      ASSERT_TRUE(listen(s, 1) == 0) << "Failed to listen on socket " +
                                            std::to_string(s) +
                                            ". errno: " + std::to_string(errno);
      sockets.push_back(s);
    }
    // Do a non-blocking accept followed by a close on all of those sockets.
    // Do this in a separate loop to try to induce a time window to hit races.
    for (size_t i = 0; i < sockets.size(); i++) {
      if (accept(sockets[i], nullptr, nullptr)) {
        // If e.g. a "shutdown" was called on this fd from another thread,
        // then this accept call should fail with an unexpected error.
        ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK)
            << "OpenAndCloseSocketsStressLoop accept on socket " +
                   std::to_string(sockets[i]) +
                   " failed in "
                   "an unexpected way. "
                   "errno: " +
                   std::to_string(errno) +
                   ". Socket use-after-close bugs are likely.";
      }
      ASSERT_TRUE(close(sockets[i]) == 0)
          << "Failed to close socket: " + std::to_string(sockets[i]) +
                 ". errno: " + std::to_string(errno);
    }
  }
}
#endif

}  // namespace

namespace grpc_core {
namespace testing {

SocketUseAfterCloseDetector::SocketUseAfterCloseDetector() {
  int port = grpc_pick_unused_port_or_die();
  gpr_event_init(&done_ev_);
  thread_ = std::make_unique<std::thread>(OpenAndCloseSocketsStressLoop, port,
                                          &done_ev_);
}

SocketUseAfterCloseDetector::~SocketUseAfterCloseDetector() {
  gpr_event_set(&done_ev_, reinterpret_cast<void*>(1));
  thread_->join();
}

}  // namespace testing
}  // namespace grpc_core
