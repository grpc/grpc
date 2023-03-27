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

#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <list>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

#include <ifaddrs.h>

#include <grpc/support/log.h>

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_listener_utils.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "test/core/util/port.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

class TestListenerSocketsContainer : public ListenerSocketsContainer {
 public:
  void Append(ListenerSocket socket) override { sockets_.push_back(socket); }

  absl::StatusOr<ListenerSocket> Find(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr)
      override {
    for (auto socket = sockets_.begin(); socket != sockets_.end(); ++socket) {
      if (socket->addr.size() == addr.size() &&
          memcmp(socket->addr.address(), addr.address(), addr.size()) == 0) {
        return *socket;
      }
    }
    return absl::NotFoundError("Socket not found!");
  }

  int Size() { return static_cast<int>(sockets_.size()); }

  std::list<ListenerSocket>::const_iterator begin() { return sockets_.begin(); }
  std::list<ListenerSocket>::const_iterator end() { return sockets_.end(); }

 private:
  std::list<ListenerSocket> sockets_;
};

}  // namespace

TEST(PosixEngineListenerUtils, ListenerContainerAddWildcardAddressesTest) {
  TestListenerSocketsContainer listener_sockets;
  int port = grpc_pick_unused_port_or_die();
  ChannelArgsEndpointConfig config;
  auto result = ListenerContainerAddWildcardAddresses(
      listener_sockets, TcpOptionsFromEndpointConfig(config), port);
  EXPECT_TRUE(result.ok());
  EXPECT_GT(*result, 0);
  port = *result;
  EXPECT_GE(listener_sockets.Size(), 1);
  EXPECT_LE(listener_sockets.Size(), 2);
  for (auto socket = listener_sockets.begin(); socket != listener_sockets.end();
       ++socket) {
    ASSERT_TRUE((*socket).addr.address()->sa_family == AF_INET6 ||
                (*socket).addr.address()->sa_family == AF_INET);
    if ((*socket).addr.address()->sa_family == AF_INET6) {
      EXPECT_EQ(ResolvedAddressToNormalizedString((*socket).addr).value(),
                absl::StrCat("[::]:", std::to_string(port)));
    } else if ((*socket).addr.address()->sa_family == AF_INET) {
      EXPECT_EQ(ResolvedAddressToNormalizedString((*socket).addr).value(),
                absl::StrCat("0.0.0.0:", std::to_string(port)));
    }
    close(socket->sock.Fd());
  }
}

#ifdef GRPC_HAVE_IFADDRS
TEST(PosixEngineListenerUtils, ListenerContainerAddAllLocalAddressesTest) {
  TestListenerSocketsContainer listener_sockets;
  int port = grpc_pick_unused_port_or_die();
  ChannelArgsEndpointConfig config;
  struct ifaddrs* ifa = nullptr;
  struct ifaddrs* ifa_it;
  if (getifaddrs(&ifa) != 0 || ifa == nullptr) {
    // No ifaddresses available.
    gpr_log(GPR_INFO,
            "Skipping ListenerAddAllLocalAddressesTest because the machine "
            "does not have interfaces configured for listening.");
    return;
  }
  int num_ifaddrs = 0;
  for (ifa_it = ifa; ifa_it != nullptr; ifa_it = ifa_it->ifa_next) {
    ++num_ifaddrs;
  }
  freeifaddrs(ifa);
  auto result = ListenerContainerAddAllLocalAddresses(
      listener_sockets, TcpOptionsFromEndpointConfig(config), port);
  if (num_ifaddrs == 0 || !result.ok()) {
    // Its possible that the machine may not have any Ipv4/Ipv6 interfaces
    // configured for listening. In that case, dont fail test.
    gpr_log(GPR_INFO,
            "Skipping ListenerAddAllLocalAddressesTest because the machine "
            "does not have Ipv6/Ipv6 interfaces configured for listening.");
    return;
  }
  // Some sockets have been created and bound to interfaces on the machiene.
  // Verify that they are listening on the correct port.
  EXPECT_GT(*result, 0);
  port = *result;
  EXPECT_GE(listener_sockets.Size(), 1);
  EXPECT_LE(listener_sockets.Size(), num_ifaddrs);
  for (auto socket = listener_sockets.begin(); socket != listener_sockets.end();
       ++socket) {
    ASSERT_TRUE((*socket).addr.address()->sa_family == AF_INET6 ||
                (*socket).addr.address()->sa_family == AF_INET);
    EXPECT_EQ(ResolvedAddressGetPort((*socket).addr), port);
    close(socket->sock.Fd());
  }
}
#endif  // GRPC_HAVE_IFADDRS

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#else  // GRPC_POSIX_SOCKET_UTILS_COMMON

int main(int argc, char** argv) { return 0; }

#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON
