//
// Copyright 2023 gRPC authors.
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

#include "src/core/resolver/endpoint_addresses.h"

#include <grpc/support/port_platform.h>

#include <set>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/util/uri.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

grpc_resolved_address MakeAddress(absl::string_view address_uri) {
  auto uri = URI::Parse(address_uri);
  CHECK(uri.ok());
  grpc_resolved_address address;
  CHECK(grpc_parse_uri(*uri, &address));
  return address;
}

MATCHER_P(EqualsAddress, address_str, "") {
  auto addr = grpc_sockaddr_to_uri(&arg);
  if (!addr.ok()) {
    *result_listener << "grpc_sockaddr_to_uri() failed";
    return false;
  }
  return ::testing::ExplainMatchResult(*addr, address_str, result_listener);
}

TEST(ResolvedAddressLessThan, Basic) {
  std::set<grpc_resolved_address, ResolvedAddressLessThan> address_set;
  address_set.insert(MakeAddress("ipv4:127.0.0.2:443"));
  address_set.insert(MakeAddress("ipv4:127.0.0.3:443"));
  address_set.insert(MakeAddress("ipv4:127.0.0.1:443"));
  EXPECT_THAT(address_set,
              ::testing::ElementsAre(EqualsAddress("ipv4:127.0.0.1:443"),
                                     EqualsAddress("ipv4:127.0.0.2:443"),
                                     EqualsAddress("ipv4:127.0.0.3:443")));
}

TEST(EndpointAddressSet, Basic) {
  EndpointAddressSet set1({MakeAddress("ipv4:127.0.0.2:443"),
                           MakeAddress("ipv4:127.0.0.3:443"),
                           MakeAddress("ipv4:127.0.0.1:443")});
  EXPECT_TRUE(set1 == set1);
  EXPECT_FALSE(set1 < set1);
  EXPECT_EQ(set1.ToString(), "{127.0.0.1:443, 127.0.0.2:443, 127.0.0.3:443}");
  EndpointAddressSet set2({MakeAddress("ipv4:127.0.0.4:443"),
                           MakeAddress("ipv4:127.0.0.6:443"),
                           MakeAddress("ipv4:127.0.0.5:443")});
  EXPECT_FALSE(set1 == set2);
  EXPECT_TRUE(set1 < set2);
  EXPECT_FALSE(set2 < set1);
  EXPECT_EQ(set2.ToString(), "{127.0.0.4:443, 127.0.0.5:443, 127.0.0.6:443}");
}

TEST(EndpointAddressSet, Subset) {
  EndpointAddressSet set1({MakeAddress("ipv4:127.0.0.2:443"),
                           MakeAddress("ipv4:127.0.0.3:443"),
                           MakeAddress("ipv4:127.0.0.1:443")});
  EXPECT_EQ(set1.ToString(), "{127.0.0.1:443, 127.0.0.2:443, 127.0.0.3:443}");
  EndpointAddressSet set2(
      {MakeAddress("ipv4:127.0.0.2:443"), MakeAddress("ipv4:127.0.0.1:443")});
  EXPECT_EQ(set2.ToString(), "{127.0.0.1:443, 127.0.0.2:443}");
  EXPECT_FALSE(set1 == set2);
  EXPECT_FALSE(set1 < set2);
  EXPECT_TRUE(set2 < set1);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
