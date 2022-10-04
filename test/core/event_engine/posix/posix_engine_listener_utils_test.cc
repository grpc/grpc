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

#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>

#include <list>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/un.h>
#endif

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/posix_engine/posix_engine_listener_utils.h"

namespace grpc_event_engine {
namespace posix_engine {

namespace {

class TestListenerSocketsContainer : public ListenerSocketsContainer {
 public:
  // Adds a socket to the internal db of sockets associated with a listener.
  void AddSocket(ListenerSocket socket) override { sockets_.push_back(socket); }

  absl::StatusOr<ListenerSocket> FindSocket(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
      /*addr*/) {
    GPR_ASSERT(false && "unimplemented");
  }

  // Remove and close socket from the internal db of sockets associated with
  // a listener.
  void RemoveSocket(int /*fd*/) { GPR_ASSERT(false && "unimplemented"); }

  const std::list<ListenerSocket>& Sockets() { return sockets_; }

 private:
  std::list<ListenerSocket> sockets_;
};

}  // namespace

TEST(PosixEngineListenerUtils, SockAddrPortTest) {
  EventEngine::ResolvedAddress wild6 = SockaddrMakeWild6(20);
  EventEngine::ResolvedAddress wild4 = SockaddrMakeWild4(20);
  // Verify the string description matches the expected wildcard address with
  // correct port number.
  EXPECT_EQ(SockaddrToString(&wild6, true).value(), "[::]:20");
  EXPECT_EQ(SockaddrToString(&wild4, true).value(), "0.0.0.0:20");
  // Update the port values.
  ASSERT_TRUE(SockaddrSetPort(wild4, 21));
  ASSERT_TRUE(SockaddrSetPort(wild6, 22));
  // Read back the port values.
  EXPECT_EQ(SockaddrGetPort(wild4), 21);
  EXPECT_EQ(SockaddrGetPort(wild6), 22);
  // Ensure the string description reflects the updated port values.
  EXPECT_EQ(SockaddrToString(&wild4, true).value(), "0.0.0.0:21");
  EXPECT_EQ(SockaddrToString(&wild6, true).value(), "[::]:22");
}

}  // namespace posix_engine
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#else /* GRPC_POSIX_SOCKET_UTILS_COMMON */

int main(int argc, char** argv) { return 1; }

#endif /* GRPC_POSIX_SOCKET_UTILS_COMMON */
