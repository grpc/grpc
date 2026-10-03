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

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

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

bool IsAllDigits(absl::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
    return absl::ascii_isdigit(static_cast<unsigned char>(c));
  });
}

void CheckPort(absl::string_view port_str) {
  int port;
  ASSERT_TRUE(IsAllDigits(port_str)) << "bad port: " << port_str;
  ASSERT_TRUE(absl::SimpleAtoi(port_str, &port));
  EXPECT_GT(port, 0);
  EXPECT_LE(port, 65535);
}

bool IsIpAddress(absl::string_view address) {
  return absl::StartsWith(address, "ipv4:") ||
         absl::StartsWith(address, "ipv6:");
}

// Verifies that `address` is in the canonical gRPC address URI format. Both
// the chttp2 transport (grpc_sockaddr_to_uri) and the PH2 transport
// (ResolvedAddressToURI) must produce exactly this format:
//   ipv4:<a.b.c.d>:<port>
//   ipv6:%5B<addr>%5D:<port>
//   unix:<path>          (path is empty for unnamed sockets, e.g. socketpair)
//   unix-abstract:<name>
void CheckAddressFormat(absl::string_view address) {
  SCOPED_TRACE(absl::StrCat("address: ", address));
  absl::string_view rest = address;
  if (absl::ConsumePrefix(&rest, "ipv4:")) {
    std::vector<absl::string_view> host_port = absl::StrSplit(rest, ':');
    ASSERT_EQ(host_port.size(), 2u);
    std::vector<absl::string_view> octets = absl::StrSplit(host_port[0], '.');
    ASSERT_EQ(octets.size(), 4u);
    for (absl::string_view octet : octets) {
      int value;
      ASSERT_TRUE(IsAllDigits(octet)) << "bad octet: " << octet;
      ASSERT_TRUE(absl::SimpleAtoi(octet, &value));
      EXPECT_LE(value, 255);
    }
    CheckPort(host_port[1]);
  } else if (absl::ConsumePrefix(&rest, "ipv6:%5B")) {
    size_t close = rest.rfind("%5D:");
    ASSERT_NE(close, absl::string_view::npos);
    EXPECT_FALSE(rest.substr(0, close).empty());
    CheckPort(rest.substr(close + 4));
  } else if (absl::ConsumePrefix(&rest, "unix:") ||
             absl::ConsumePrefix(&rest, "unix-abstract:")) {
    // Any path is acceptable.
  } else {
    ADD_FAILURE() << "unexpected address format";
  }
}

// Checks the local address reported by a call against its peer address.
void CheckLocalAddress(absl::string_view local_address,
                       absl::string_view peer) {
  SCOPED_TRACE(absl::StrCat("local: ", local_address, " peer: ", peer));
  // Transports that don't report a peer address (e.g. inproc, chaotic_good)
  // don't report a local address either.
  if (!IsIpAddress(peer) && !absl::StartsWith(peer, "unix")) {
    VLOG(2) << "Transport does not report addresses; skipping local address "
               "format check";
    return;
  }
  CheckAddressFormat(local_address);
  // Local and peer addresses are formatted by the same code, so they must use
  // the same scheme family.
  if (IsIpAddress(peer)) {
    EXPECT_TRUE(IsIpAddress(local_address));
#ifndef GPR_WINDOWS
    // The two ends of a TCP connection never share an ip:port, so this
    // catches the local address accidentally being populated from the peer.
    // Not checked on Windows: WindowsEventEngine::CreateEndpointFromWinSocket
    // (used by the socket-pair fixtures) reports the socket's local address as
    // its peer address, so the two strings are equal there.
    EXPECT_NE(local_address, peer);
#endif  // GPR_WINDOWS
  } else {
    EXPECT_THAT(std::string(local_address), StartsWith("unix"));
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
