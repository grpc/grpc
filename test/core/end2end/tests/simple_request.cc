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

#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>

#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

using testing::HasSubstr;
using testing::StartsWith;

namespace grpc_core {
namespace {
void CheckPeer(std::string peer_name) {
  // If the peer name is a uds path, then check if it is filled
  if (absl::StartsWith(peer_name, "unix:/")) {
    EXPECT_THAT(peer_name, StartsWith("unix:/tmp/grpc_fullstack_test."));
  }
}

bool IsIpAddress(absl::string_view address) {
  return absl::StartsWith(address, "ipv4:") ||
         absl::StartsWith(address, "ipv6:");
}

bool IsUnixAddress(absl::string_view address) {
  return absl::StartsWith(address, "unix:") ||
         absl::StartsWith(address, "unix-abstract:");
}

// Checks the local address reported by a call against its peer address. The
// exact address format is covered by lower-level tests (e.g.
// sockaddr_utils_test), so this only checks that the local address is
// plumbed through and is distinct from the peer address.
void CheckLocalAddress(absl::string_view local_address,
                       absl::string_view peer) {
  SCOPED_TRACE(absl::StrCat("local: ", local_address, " peer: ", peer));
  // Transports that don't report a peer address (e.g. inproc, chaotic_good)
  // don't report a local address either. The vrpc fixtures report an empty
  // peer instead of "unknown": their server transport runs over a
  // SessionEndpoint (tunneled through a gRPC call, not a socket), whose
  // GetPeerAddress() is unimplemented and returns an empty address, which the
  // iomgr endpoint shim converts to "".
  if (peer.empty() || peer == "unknown") {
    EXPECT_EQ(local_address, "unknown");
    return;
  }
  // Both addresses describe the same socket, so they are either both IP or
  // both unix addresses. The exact unix scheme is not compared: a server bound
  // to a unix-abstract address sees an unbound client as "unix:".
  if (IsIpAddress(peer)) {
    EXPECT_TRUE(IsIpAddress(local_address));
#ifndef GPR_WINDOWS
    // The two ends of a TCP connection never share an ip:port, so this
    // catches the local address accidentally being populated from the peer.
    // Not checked for unix sockets, where both ends of a socketpair are
    // "unix:", nor on Windows, where
    // WindowsEventEngine::CreateEndpointFromWinSocket (used by the socket-pair
    // fixtures) reports the socket's local address as its peer address.
    EXPECT_NE(local_address, peer);
#endif  // GPR_WINDOWS
  } else if (IsUnixAddress(peer)) {
    EXPECT_TRUE(IsUnixAddress(local_address));
  } else if (absl::StartsWith(peer, "socketpair-") ||
             absl::StartsWith(peer, "fd:")) {
    // Some fixtures wrap an already-connected socket and label the peer with a
    // synthetic name instead of querying its address:
    //  - Chttp2SocketPair* use grpc_iomgr_create_endpoint_pair(), which names
    //    the peers "socketpair-client" / "socketpair-server".
    //  - Chttp2Fd uses grpc_server_add_channel_from_fd(), which names the peer
    //    "fd:<fd>".
    // The local address is still read from the socket, so it should be a real
    // address.
    EXPECT_TRUE(IsIpAddress(local_address) || IsUnixAddress(local_address));
  } else {
    ADD_FAILURE() << "Unexpected peer address scheme";
  }
}

void SimpleRequestBody(CoreEnd2endTest& test) {
  // Force early initialization of the fixture (and its background calls, if
  // any) before taking the baseline global stats snapshot.
  test.client();

  auto before = global_stats().Collect();
  auto c = test.NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  EXPECT_NE(c.GetPeer(), std::nullopt);
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  EXPECT_NE(s.GetPeer(), std::nullopt);
  CheckPeer(*s.GetPeer());
  EXPECT_NE(c.GetPeer(), std::nullopt);
  CheckPeer(*c.GetPeer());
  ASSERT_NE(s.GetLocalAddress(), std::nullopt);
  CheckLocalAddress(*s.GetLocalAddress(), *s.GetPeer());

  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_THAT(server_status.error_string(), HasSubstr("xyz"));
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  // The local address is only propagated on the server side; the client
  // reports "unknown" even after receiving server initial metadata.
  EXPECT_EQ(c.GetLocalAddress(), "unknown");
  uint64_t expected_calls = 1;
  if ((test.test_config()->feature_mask &
       FEATURE_MASK_SUPPORTS_REQUEST_PROXYING) &&
      !(test.test_config()->feature_mask & FEATURE_MASK_IS_VIRTUAL_RPC)) {
    expected_calls *= 2;
  }
  auto after = global_stats().Collect();
  VLOG(2) << StatsAsJson(after.get());
  EXPECT_EQ(after->client_calls_created - before->client_calls_created,
            expected_calls);
  EXPECT_EQ(after->server_calls_created - before->server_calls_created,
            expected_calls);
}

CORE_END2END_TEST(CoreEnd2endTests, SimpleRequest) { SimpleRequestBody(*this); }

CORE_END2END_TEST(CoreEnd2endTests, SimpleRequest10) {
  for (int i = 0; i < 10; i++) {
    SimpleRequestBody(*this);
  }
}
}  // namespace
}  // namespace grpc_core
