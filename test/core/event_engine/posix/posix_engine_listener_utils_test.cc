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

#include <grpc/event_engine/event_engine.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <list>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

#include <ifaddrs.h>

#include "absl/log/log.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_listener_utils.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "test/core/test_util/port.h"

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
    EXPECT_FALSE(IsSockAddrLinkLocal(&((*socket).addr)));
    close(socket->sock.Fd());
  }
}

TEST(PosixEngineListenerUtils, ListenerContainerIpv4LinkLocalTest) {
  sockaddr_in addr4;
  memset(&addr4, 0, sizeof(addr4));
  addr4.sin_family = AF_INET;
  addr4.sin_addr.s_addr = htonl(0xA9FE0101);  // 169.254.1.1
  EventEngine::ResolvedAddress resolved_addr4(
      reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4));
  EXPECT_TRUE(IsSockAddrLinkLocal(&resolved_addr4));

  addr4.sin_addr.s_addr = htonl(0xA9FE1010);  // 169.254.16.16
  EventEngine::ResolvedAddress resolved_addr4_mid1(
      reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4));
  EXPECT_TRUE(IsSockAddrLinkLocal(&resolved_addr4_mid1));

  addr4.sin_addr.s_addr = htonl(0xA9FE8080);  // 169.254.128.128
  EventEngine::ResolvedAddress resolved_addr4_mid2(
      reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4));
  EXPECT_TRUE(IsSockAddrLinkLocal(&resolved_addr4_mid2));

  addr4.sin_addr.s_addr = htonl(0xA9FEFFFF);  // 169.254.255.255
  EventEngine::ResolvedAddress resolved_addr4_max(
      reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4));
  EXPECT_TRUE(IsSockAddrLinkLocal(&resolved_addr4_max));

  addr4.sin_addr.s_addr = htonl(0xA9000101);  // 169.0.1.1 (Not link-local)
  EventEngine::ResolvedAddress resolved_addr4_not_ll(
      reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4));
  EXPECT_FALSE(IsSockAddrLinkLocal(&resolved_addr4_not_ll));

  addr4.sin_addr.s_addr = htonl(0xAC100101);  // 172.16.1.1 (Not link-local)
  EventEngine::ResolvedAddress resolved_addr4_not_ll2(
      reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4));
  EXPECT_FALSE(IsSockAddrLinkLocal(&resolved_addr4_not_ll2));
}

TEST(PosixEngineListenerUtils, ListenerContainerIpv6LinkLocalTest) {
  sockaddr_in6 addr6;
  memset(&addr6, 0, sizeof(addr6));
  addr6.sin6_family = AF_INET6;
  // fe80::1
  uint8_t fe80_1[] = {0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  memcpy(&addr6.sin6_addr.s6_addr, fe80_1, 16);
  EventEngine::ResolvedAddress resolved_addr6(
      reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6));
  EXPECT_TRUE(IsSockAddrLinkLocal(&resolved_addr6));

  uint8_t fea0_1[] = {0xfe, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  memcpy(&addr6.sin6_addr.s6_addr, fea0_1, 16);
  EventEngine::ResolvedAddress resolved_addr6_mid1(
      reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6));
  EXPECT_TRUE(IsSockAddrLinkLocal(&resolved_addr6_mid1));

  uint8_t fe90_1234[] = {0xfe, 0x90, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc,
                         0xde, 0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
  memcpy(&addr6.sin6_addr.s6_addr, fe90_1234, 16);
  EventEngine::ResolvedAddress resolved_addr6_mid2(
      reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6));
  EXPECT_TRUE(IsSockAddrLinkLocal(&resolved_addr6_mid2));

  // febf:ffff:ffff:ffff:ffff:ffff:ffff:ffff (Max link-local)
  uint8_t febf_ffff[] = {0xfe, 0xbf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  memcpy(&addr6.sin6_addr.s6_addr, febf_ffff, 16);
  EventEngine::ResolvedAddress resolved_addr6_max(
      reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6));
  EXPECT_TRUE(IsSockAddrLinkLocal(&resolved_addr6_max));

  // fe7f::1 (Not link-local)
  uint8_t fe7f_1[] = {0xfe, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  memcpy(&addr6.sin6_addr.s6_addr, fe7f_1, 16);
  EventEngine::ResolvedAddress resolved_addr6_not_ll(
      reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6));
  EXPECT_FALSE(IsSockAddrLinkLocal(&resolved_addr6_not_ll));

  // 2001:db8::1 (Not link-local)
  uint8_t db8_1[] = {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  memcpy(&addr6.sin6_addr.s6_addr, db8_1, 16);
  EventEngine::ResolvedAddress resolved_addr6_not_ll2(
      reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6));
  EXPECT_FALSE(IsSockAddrLinkLocal(&resolved_addr6_not_ll2));
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
    LOG(INFO) << "Skipping ListenerAddAllLocalAddressesTest because the "
                 "machine does not have interfaces configured for listening.";
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
    // configured for listening. In that case, don't fail test.
    LOG(INFO) << "Skipping ListenerAddAllLocalAddressesTest because the "
                 "machine does not have Ipv6/Ipv6 interfaces configured for "
                 "listening.";
    return;
  }
  // Some sockets have been created and bound to interfaces on the machine.
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
    EXPECT_FALSE(IsSockAddrLinkLocal(&((*socket).addr)));
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
