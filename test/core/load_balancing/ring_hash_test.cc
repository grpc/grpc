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

#include "src/core/load_balancing/ring_hash/ring_hash.h"

#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/core/lib/iomgr/port.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/json/json.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/xxhash_inline.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/span.h"

namespace grpc_core {
namespace testing {
namespace {

// TODO(roth): I created this file when I fixed a bug and wrote only a
// very basic test and the test needed for that bug.  When we have time,
// we need a lot more tests here to cover all of the policy's functionality.

class RingHashTest : public LoadBalancingPolicyTest {
 protected:
  RingHashTest() : LoadBalancingPolicyTest("ring_hash_experimental") {}

  static RefCountedPtr<LoadBalancingPolicy::Config> MakeRingHashConfig(
      int min_ring_size = 0, int max_ring_size = 0,
      const std::string& request_hash_header = "") {
    Json::Object fields;
    if (min_ring_size > 0) {
      fields["minRingSize"] = Json::FromString(absl::StrCat(min_ring_size));
    }
    if (max_ring_size > 0) {
      fields["maxRingSize"] = Json::FromString(absl::StrCat(max_ring_size));
    }
    if (!request_hash_header.empty()) {
      fields["requestHashHeader"] = Json::FromString(request_hash_header);
    }
    return MakeConfig(Json::FromArray({Json::FromObject(
        {{"ring_hash_experimental", Json::FromObject(fields)}})}));
  }

  // Returns an attribute whose request hash is the hash of the ring entry
  // with the given index for an endpoint with the given hash key.  Ring
  // entries are named "<hash_key>_<index>".
  RequestHashAttribute* MakeHashAttributeForRingEntry(
      absl::string_view hash_key, size_t index) {
    std::string entry = absl::StrCat(hash_key, "_", index);
    uint64_t hash = XXH64(entry.data(), entry.size(), 0);
    attribute_storage_.emplace_back(
        std::make_unique<RequestHashAttribute>(hash));
    return attribute_storage_.back().get();
  }

  RequestHashAttribute* MakeHashAttributeForString(absl::string_view key) {
    return MakeHashAttributeForRingEntry(key, 0);
  }

  RequestHashAttribute* MakeHashAttribute(absl::string_view address) {
    return MakeHashAttributeForString(absl::StripPrefix(address, "ipv4:"));
  }

  // Checks that the default hash key (used when an endpoint has no hash
  // key attribute) of an endpoint with the given addresses is
  // expected_hash_key.  The key determines the endpoint's position on the
  // ring, so a change in its format would move traffic between endpoints.
  //
  // The endpoint is paired with one other endpoint, which has an explicit
  // hash key, so that its ring entries don't depend on its address.  We
  // pick using the hashes of kNumPicks ring entries for expected_hash_key.
  // Since the picker is IDLE, each pick triggers a connection attempt on
  // the endpoint that owns the ring entry.  If the actual key were
  // different, about half of these picks would land on the other endpoint.
  //
  // The other endpoint comes first, and its address is the IPv4 form of
  // the IPv4-mapped IPv6 address used below.  If endpoints or subchannels
  // were identified by normalized address, the IPv4-mapped endpoint would
  // be merged into the other one (ring_hash keeps the first of duplicate
  // endpoints) or share its subchannel, and the checks below would fail.
  void ExpectDefaultHashKey(
      absl::Span<const absl::string_view> endpoint_addresses,
      absl::string_view expected_hash_key,
      SourceLocation location = SourceLocation()) {
    constexpr absl::string_view kOtherAddress = "ipv4:127.0.0.1:441";
    constexpr size_t kNumPicks = 30;
    ASSERT_FALSE(endpoint_addresses.empty())
        << location.file() << ":" << location.line();
    for (absl::string_view address : endpoint_addresses) {
      ASSERT_NE(address, kOtherAddress)
          << location.file() << ":" << location.line();
    }
    const std::array<EndpointAddresses, 2> endpoints = {
        MakeEndpointAddresses(
            {kOtherAddress},
            ChannelArgs().Set(GRPC_ARG_RING_HASH_ENDPOINT_HASH_KEY, "other")),
        MakeEndpointAddresses(endpoint_addresses)};
    // With a minimum ring size of 1024, each endpoint gets 512 ring entries,
    // so all kNumPicks entries exist.
    const auto config = MakeRingHashConfig(/*min_ring_size=*/1024);
    EXPECT_EQ(ApplyUpdate(BuildUpdate(endpoints, config), lb_policy()),
              absl::OkStatus())
        << location.file() << ":" << location.line();
    auto picker = ExpectState(GRPC_CHANNEL_IDLE, absl::OkStatus(), location);
    for (size_t i = 0; i < kNumPicks; ++i) {
      ExpectPickQueued(picker.get(),
                       {MakeHashAttributeForRingEntry(expected_hash_key, i)},
                       {}, location);
    }
    SubchannelState* subchannel = FindSubchannel(endpoint_addresses.front());
    ASSERT_NE(subchannel, nullptr) << location.file() << ":" << location.line();
    EXPECT_TRUE(subchannel->ConnectionRequested())
        << location.file() << ":" << location.line();
    EXPECT_EQ(FindSubchannel(kOtherAddress), nullptr)
        << location.file() << ":" << location.line();
  }

  std::vector<std::unique_ptr<RequestHashAttribute>> attribute_storage_;
};

TEST_F(RingHashTest, Basic) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses, MakeRingHashConfig()), lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  auto* address0_attribute = MakeHashAttribute(kAddresses[0]);
  ExpectPickQueued(picker.get(), {address0_attribute});
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {address0_attribute});
  EXPECT_EQ(nullptr, FindSubchannel(kAddresses[1]));
  EXPECT_EQ(nullptr, FindSubchannel(kAddresses[2]));
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {address0_attribute});
  EXPECT_EQ(address, kAddresses[0]);
}

TEST_F(RingHashTest, SameAddressListedMultipleTimes) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:441"};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses, MakeRingHashConfig()), lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  auto* address0_attribute = MakeHashAttribute(kAddresses[0]);
  ExpectPickQueued(picker.get(), {address0_attribute});
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {address0_attribute});
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {address0_attribute});
  EXPECT_EQ(address, kAddresses[0]);
}

TEST_F(RingHashTest, MultipleAddressesPerEndpoint) {
  constexpr std::array<absl::string_view, 2> kEndpoint1Addresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  constexpr std::array<absl::string_view, 2> kEndpoint2Addresses = {
      "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};
  const std::array<EndpointAddresses, 2> kEndpoints = {
      MakeEndpointAddresses(kEndpoint1Addresses),
      MakeEndpointAddresses(kEndpoint2Addresses)};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kEndpoints, MakeRingHashConfig()), lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // Normal connection to first address of the first endpoint.
  auto* address0_attribute = MakeHashAttribute(kEndpoint1Addresses[0]);
  ExpectPickQueued(picker.get(), {address0_attribute});
  auto* subchannel = FindSubchannel(kEndpoint1Addresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  auto* subchannel2 = FindSubchannel(kEndpoint1Addresses[1]);
  ASSERT_NE(subchannel2, nullptr);
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {address0_attribute});
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {address0_attribute});
  EXPECT_EQ(address, kEndpoint1Addresses[0]);
  // Now that connection fails.
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  ExpectReresolutionRequest();
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  EXPECT_FALSE(subchannel->ConnectionRequested());
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // The LB policy will try to reconnect when it gets another pick.
  ExpectPickQueued(picker.get(), {address0_attribute});
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {address0_attribute});
  // The connection attempt fails.
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("ugh"));
  // The PF child policy will try to connect to the second address for the
  // endpoint.
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {address0_attribute});
  subchannel2->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  address = ExpectPickComplete(picker.get(), {address0_attribute});
  EXPECT_EQ(address, kEndpoint1Addresses[1]);
}

TEST_F(RingHashTest,
       TriggersConnectionAttemptsInConnectingAndTransientFailureWithoutPicks) {
  const std::array<absl::string_view, 4> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443",
      "ipv4:127.0.0.1:444"};
  std::array<SubchannelState*, 4> subchannels;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    subchannels[i] = CreateSubchannel(kAddresses[i]);
  }
  absl::flat_hash_set<SubchannelState*> failed_subchannels;
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses, MakeRingHashConfig()), lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // Do a pick for subchannel 0.  This will trigger a connection attempt,
  // which will fail.
  auto* address0_attribute = MakeHashAttribute(kAddresses[0]);
  ExpectPickQueued(picker.get(), {address0_attribute});
  EXPECT_TRUE(subchannels[0]->ConnectionRequested());
  for (size_t i = 1; i < subchannels.size(); ++i) {
    EXPECT_FALSE(subchannels[i]->ConnectionRequested()) << "index " << i;
  }
  subchannels[0]->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  for (size_t i = 0; i < subchannels.size(); ++i) {
    EXPECT_FALSE(subchannels[i]->ConnectionRequested()) << "index " << i;
  }
  subchannels[0]->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(subchannels[0]);
  // With one subchannel in state TF and the rest in IDLE, we report
  // CONNECTING.  This should automatically trigger a connection attempt
  // on exactly one other subchannel, even without picks.
  ExpectReresolutionRequest();
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  SubchannelState* connecting_subchannel = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(connecting_subchannel, nullptr) << "index " << i;
      connecting_subchannel = subchannels[i];
    }
  }
  ASSERT_NE(connecting_subchannel, nullptr);
  // This subchannel will also fail to connect.
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  for (size_t i = 0; i < subchannels.size(); ++i) {
    EXPECT_FALSE(subchannels[i]->ConnectionRequested());
  }
  connecting_subchannel->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(connecting_subchannel);
  // Now that there are two subchannels in TF, the policy will report TF
  // to the channel.  It will also trigger a connection attempt on exactly
  // one more subchannel, still without any picks.
  ExpectReresolutionRequest();
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  connecting_subchannel = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(connecting_subchannel, nullptr) << "index " << i;
      connecting_subchannel = subchannels[i];
    }
  }
  ASSERT_NE(connecting_subchannel, nullptr);
  ASSERT_FALSE(failed_subchannels.contains(connecting_subchannel));
  // This subchannel will also fail to connect.
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  for (size_t i = 0; i < subchannels.size(); ++i) {
    EXPECT_FALSE(subchannels[i]->ConnectionRequested());
  }
  connecting_subchannel->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(connecting_subchannel);
  // The policy will once again report TF.  It will also trigger a connection
  // attempt on the last subchannel, again without any picks.
  ExpectReresolutionRequest();
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  connecting_subchannel = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(connecting_subchannel, nullptr) << "index " << i;
      connecting_subchannel = subchannels[i];
    }
  }
  ASSERT_NE(connecting_subchannel, nullptr);
  ASSERT_FALSE(failed_subchannels.contains(connecting_subchannel));
  // This subchannel will also fail to connect.
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  for (size_t i = 0; i < subchannels.size(); ++i) {
    EXPECT_FALSE(subchannels[i]->ConnectionRequested());
  }
  connecting_subchannel->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(connecting_subchannel);
  ExpectReresolutionRequest();
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  // Now one of the subchannels goes IDLE.  The pick_first child will
  // trigger a new connection attempt, which will succeed this time.
  subchannels[2]->SetConnectivityState(GRPC_CHANNEL_IDLE);
  EXPECT_TRUE(subchannels[2]->ConnectionRequested());
  subchannels[2]->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  subchannels[2]->SetConnectivityState(GRPC_CHANNEL_READY);
  // Now the policy will report READY.
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {address0_attribute});
  EXPECT_EQ(address, kAddresses[2]);
}

TEST_F(RingHashTest, EndpointHashKeys) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  const std::array<absl::string_view, 3> kHashKeys = {"foo", "bar", "baz"};
  std::vector<EndpointAddresses> endpoints;
  for (size_t i = 0; i < 3; ++i) {
    endpoints.push_back(MakeEndpointAddresses(
        {kAddresses[i]},
        ChannelArgs().Set(GRPC_ARG_RING_HASH_ENDPOINT_HASH_KEY, kHashKeys[i])));
  };
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(endpoints, MakeRingHashConfig()), lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  auto* hash_attribute = MakeHashAttributeForString(kHashKeys[1]);
  ExpectPickQueued(picker.get(), {hash_attribute});
  auto* subchannel = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {hash_attribute});
  EXPECT_EQ(nullptr, FindSubchannel(kAddresses[0]));
  EXPECT_EQ(nullptr, FindSubchannel(kAddresses[2]));
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {hash_attribute});
  EXPECT_EQ(address, kAddresses[1]);
}

// An endpoint without a hash key attribute uses its first address as its
// hash key: host:port for IP addresses (IPv6 in brackets, IPv4-mapped IPv6
// not converted to IPv4), the path for unix addresses, a NUL byte followed
// by the name for unix-abstract addresses, and cid:port for vsock
// addresses.  The tests above use IPv4; the tests below pin the current
// format for the other address types.  Characters that need it are
// percent-encoded in the input URIs, so the tests also catch a key derived
// without percent-decoding.

TEST_F(RingHashTest, DefaultHashKeyUsesFirstAddress) {
  ExpectDefaultHashKey({"ipv6:%5B2001:db8::1%5D:441", "ipv4:127.0.0.1:442"},
                       "[2001:db8::1]:441");
}

TEST_F(RingHashTest, DefaultHashKeyForIpv6Address) {
  ExpectDefaultHashKey({"ipv6:%5B2001:db8::1%5D:441"}, "[2001:db8::1]:441");
}

TEST_F(RingHashTest, DefaultHashKeyForIpv6AddressWithZone) {
  // Zone 25 is written as "%25", so decoding the key a second time would
  // change it.
  ExpectDefaultHashKey({"ipv6:%5Bfe80::1%2525%5D:441"}, "[fe80::1%25]:441");
}

TEST_F(RingHashTest, DefaultHashKeyForIpv4MappedIpv6Address) {
  // The address is not converted to IPv4, and the endpoint stays separate
  // from the other endpoint, whose address is the IPv4 form.
  ExpectDefaultHashKey({"ipv6:%5B::ffff:127.0.0.1%5D:441"},
                       "[::ffff:127.0.0.1]:441");
}

#ifdef GRPC_HAVE_UNIX_SOCKET
TEST_F(RingHashTest, DefaultHashKeyForUnixAddress) {
  // The path contains an encoded "%" and an encoded space, so a key that
  // was decoded zero times or twice would be different.
  ExpectDefaultHashKey({"unix:/tmp/ring%2520hash%20test.sock"},
                       "/tmp/ring%20hash test.sock");
}

TEST_F(RingHashTest, DefaultHashKeyForUnixAbstractAddress) {
  // The key starts with a NUL byte.  As above, the name contains an
  // encoded "%" and an encoded space.  It also contains a NUL byte
  // (encoded as "%00"), so a key built from a C string would be cut short.
  constexpr absl::string_view kNul("\0", 1);
  ExpectDefaultHashKey({"unix-abstract:ring%00%2520hash%20test"},
                       absl::StrCat(kNul, "ring", kNul, "%20hash test"));
}
#endif  // GRPC_HAVE_UNIX_SOCKET

#ifdef GRPC_HAVE_VSOCK
TEST_F(RingHashTest, DefaultHashKeyForVsockAddress) {
  ExpectDefaultHashKey({"vsock:3:441"}, "3:441");
}
#endif  // GRPC_HAVE_VSOCK

TEST_F(RingHashTest, PickFailsWithoutRequestHashAttribute) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses, MakeRingHashConfig()), lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(picker.get(), [&](const absl::Status& status) {
    EXPECT_EQ(status, absl::InternalError("hash attribute not present"));
  });
}

TEST_F(RingHashTest, RequestHashHeaderNotEnabled) {
  ScopedEnvVar env("GRPC_EXPERIMENTAL_RING_HASH_SET_REQUEST_HASH_KEY", "false");
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses, MakeRingHashConfig(0, 0, "foo")),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(picker.get(), [&](const absl::Status& status) {
    EXPECT_EQ(status, absl::InternalError("hash attribute not present"));
  });
}

TEST_F(RingHashTest, RequestHashHeader) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses, MakeRingHashConfig(0, 0, "foo")),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  std::string hash_key =
      absl::StrCat(absl::StripPrefix(kAddresses[0], "ipv4:"), "_0");
  std::map<std::string, std::string> metadata = {{"foo", hash_key}};
  ExpectPickQueued(picker.get(), /*call_attributes=*/{}, metadata);
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {}, metadata);
  EXPECT_EQ(nullptr, FindSubchannel(kAddresses[1]));
  EXPECT_EQ(nullptr, FindSubchannel(kAddresses[2]));
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, metadata);
  EXPECT_EQ(address, kAddresses[0]);
}

TEST_F(RingHashTest, RequestHashHeaderNotPresent) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses, MakeRingHashConfig(0, 0, "foo")),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get());
  // It will randomly pick one.
  size_t index = 0;
  SubchannelState* subchannel = nullptr;
  for (; index < kAddresses.size(); ++index) {
    subchannel = FindSubchannel(kAddresses[index]);
    if (subchannel != nullptr) {
      LOG(INFO) << "Randomly picked subchannel index " << index;
      break;
    }
  }
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get());
  // No other subchannels should have been created yet.
  for (size_t i = 0; i < kAddresses.size(); ++i) {
    if (i != index) EXPECT_EQ(nullptr, FindSubchannel(kAddresses[i]));
  }
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get());
  EXPECT_EQ(address, kAddresses[index]);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
