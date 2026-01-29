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

#include <address_sorting/address_sorting.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>
#include <sys/types.h>

#include <vector>

#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/config/config_vars.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_registry.h"
#include "src/core/util/crash.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/host_port.h"
#include "src/core/util/string.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/util/subprocess.h"
#include "test/cpp/util/test_config.h"
#include "gmock/gmock.h"
#include "absl/log/log.h"

#ifndef GPR_WINDOWS
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace {

struct TestAddress {
  std::string dest_addr;
  int family;
};

grpc_resolved_address TestAddressToGrpcResolvedAddress(TestAddress test_addr) {
  std::string host;
  std::string port;
  grpc_resolved_address resolved_addr;
  grpc_core::SplitHostPort(test_addr.dest_addr.c_str(), &host, &port);
  if (test_addr.family == AF_INET) {
    sockaddr_in in_dest;
    memset(&in_dest, 0, sizeof(sockaddr_in));
    in_dest.sin_port = htons(atoi(port.c_str()));
    in_dest.sin_family = AF_INET;
    GRPC_CHECK_EQ(inet_pton(AF_INET, host.c_str(), &in_dest.sin_addr), 1);
    memcpy(&resolved_addr.addr, &in_dest, sizeof(sockaddr_in));
    resolved_addr.len = sizeof(sockaddr_in);
  } else {
    GRPC_CHECK(test_addr.family == AF_INET6);
    sockaddr_in6 in6_dest;
    memset(&in6_dest, 0, sizeof(sockaddr_in6));
    in6_dest.sin6_port = htons(atoi(port.c_str()));
    in6_dest.sin6_family = AF_INET6;
    GRPC_CHECK_EQ(inet_pton(AF_INET6, host.c_str(), &in6_dest.sin6_addr), 1);
    memcpy(&resolved_addr.addr, &in6_dest, sizeof(sockaddr_in6));
    resolved_addr.len = sizeof(sockaddr_in6);
  }
  return resolved_addr;
}

class MockSourceAddrFactory : public address_sorting_source_addr_factory {
 public:
  MockSourceAddrFactory(
      bool ipv4_supported, bool ipv6_supported,
      const std::map<std::string, TestAddress>& dest_addr_to_src_addr)
      : ipv4_supported_(ipv4_supported),
        ipv6_supported_(ipv6_supported),
        dest_addr_to_src_addr_(dest_addr_to_src_addr) {}

  bool GetSourceAddr(const address_sorting_address* dest_addr,
                     address_sorting_address* source_addr) {
    if ((address_sorting_abstract_get_family(dest_addr) ==
             ADDRESS_SORTING_AF_INET &&
         !ipv4_supported_) ||
        (address_sorting_abstract_get_family(dest_addr) ==
             ADDRESS_SORTING_AF_INET6 &&
         !ipv6_supported_)) {
      return false;
    }
    grpc_resolved_address dest_addr_as_resolved_addr;
    memcpy(&dest_addr_as_resolved_addr.addr, dest_addr, dest_addr->len);
    dest_addr_as_resolved_addr.len = dest_addr->len;
    std::string ip_addr_str =
        grpc_sockaddr_to_string(&dest_addr_as_resolved_addr,
                                false /* normalize */)
            .value();
    auto it = dest_addr_to_src_addr_.find(ip_addr_str);
    if (it == dest_addr_to_src_addr_.end()) {
      VLOG(2) << "can't find |" << ip_addr_str << "| in dest to src map";
      return false;
    }
    grpc_resolved_address source_addr_as_resolved_addr =
        TestAddressToGrpcResolvedAddress(it->second);
    memcpy(source_addr->addr, &source_addr_as_resolved_addr.addr,
           source_addr_as_resolved_addr.len);
    source_addr->len = source_addr_as_resolved_addr.len;
    return true;
  }

 private:
  // user provided test config
  bool ipv4_supported_;
  bool ipv6_supported_;
  std::map<std::string, TestAddress> dest_addr_to_src_addr_;
};

bool mock_source_addr_factory_wrapper_get_source_addr(
    address_sorting_source_addr_factory* factory,
    const address_sorting_address* dest_addr,
    address_sorting_address* source_addr) {
  MockSourceAddrFactory* mock =
      reinterpret_cast<MockSourceAddrFactory*>(factory);
  return mock->GetSourceAddr(dest_addr, source_addr);
}

void mock_source_addr_factory_wrapper_destroy(
    address_sorting_source_addr_factory* factory) {
  MockSourceAddrFactory* mock =
      reinterpret_cast<MockSourceAddrFactory*>(factory);
  delete mock;
}

const address_sorting_source_addr_factory_vtable kMockSourceAddrFactoryVtable =
    {
        mock_source_addr_factory_wrapper_get_source_addr,
        mock_source_addr_factory_wrapper_destroy,
};

void OverrideAddressSortingSourceAddrFactory(
    bool ipv4_supported, bool ipv6_supported,
    const std::map<std::string, TestAddress>& dest_addr_to_src_addr) {
  address_sorting_source_addr_factory* factory = new MockSourceAddrFactory(
      ipv4_supported, ipv6_supported, dest_addr_to_src_addr);
  factory->vtable = &kMockSourceAddrFactoryVtable;
  address_sorting_override_source_addr_factory_for_testing(factory);
}

grpc_core::EndpointAddressesList BuildLbAddrInputs(
    const std::vector<TestAddress>& test_addrs) {
  grpc_core::EndpointAddressesList addresses;
  for (const auto& addr : test_addrs) {
    addresses.emplace_back(addr.dest_addr, grpc_core::ChannelArgs());
  }
  return addresses;
}

void VerifyLbAddrOutputs(const grpc_core::EndpointAddressesList& addresses,
                         std::vector<std::string> expected_addrs) {
  EXPECT_EQ(addresses.size(), expected_addrs.size());
  for (size_t i = 0; i < addresses.size(); ++i) {
    std::string ip_addr_str = addresses[i].address();
    EXPECT_EQ(expected_addrs[i], ip_addr_str);
  }
}

// We need to run each test case inside of its own
// isolated grpc_init/grpc_shutdown pair, so that
// the "address sorting source addr factory" can be
// restored to its default for each test case.
class AddressSortingTest : public ::testing::Test {
 protected:
  void SetUp() override { grpc_init(); }
  void TearDown() override { grpc_shutdown(); }
};

// Tests for rule 1
TEST_F(AddressSortingTest, TestDepriotizesUnreachableAddresses) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"1.2.3.4:443", {"4.3.2.1:443", AF_INET}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"1.2.3.4:443", AF_INET},
      {"5.6.7.8:443", AF_INET},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "1.2.3.4:443",
                                    "5.6.7.8:443",
                                });
}

TEST_F(AddressSortingTest, TestDepriotizesUnsupportedDomainIpv6) {
  bool ipv4_supported = true;
  bool ipv6_supported = false;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"1.2.3.4:443", {"4.3.2.1:0", AF_INET}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[2607:f8b0:400a:801::1002]:443", AF_INET6},
      {"1.2.3.4:443", AF_INET},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "1.2.3.4:443",
                                    "[2607:f8b0:400a:801::1002]:443",
                                });
}

TEST_F(AddressSortingTest, TestDepriotizesUnsupportedDomainIpv4) {
  bool ipv4_supported = false;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"1.2.3.4:443", {"4.3.2.1:0", AF_INET}},
          {"[2607:f8b0:400a:801::1002]:443", {"[fec0::1234]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[2607:f8b0:400a:801::1002]:443", AF_INET6},
      {"1.2.3.4:443", AF_INET},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[2607:f8b0:400a:801::1002]:443",
                                    "1.2.3.4:443",
                                });
}

// Tests for rule 2

TEST_F(AddressSortingTest, TestDepriotizesNonMatchingScope) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[2000:f8b0:400a:801::1002]:443",
           {"[fec0::1000]:0", AF_INET6}},  // global and site-local scope
          {"[fec0::5000]:443",
           {"[fec0::5001]:0", AF_INET6}},  // site-local and site-local scope
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[2000:f8b0:400a:801::1002]:443", AF_INET6},
      {"[fec0::5000]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[fec0::5000]:443",
                                    "[2000:f8b0:400a:801::1002]:443",
                                });
}

// Tests for rule 5

TEST_F(AddressSortingTest, TestUsesLabelFromDefaultTable) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[2002::5001]:443", {"[2001::5002]:0", AF_INET6}},
          {"[2001::5001]:443",
           {"[2001::5002]:0", AF_INET6}},  // matching labels
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[2002::5001]:443", AF_INET6},
      {"[2001::5001]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[2001::5001]:443",
                                    "[2002::5001]:443",
                                });
}

// Flip the input on the test above to reorder the sort function's
// comparator's inputs.
TEST_F(AddressSortingTest, TestUsesLabelFromDefaultTableInputFlipped) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[2002::5001]:443", {"[2001::5002]:0", AF_INET6}},
          {"[2001::5001]:443",
           {"[2001::5002]:0", AF_INET6}},  // matching labels
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[2001::5001]:443", AF_INET6},
      {"[2002::5001]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[2001::5001]:443",
                                    "[2002::5001]:443",
                                });
}

// Tests for rule 6

TEST_F(AddressSortingTest,
       TestUsesDestinationWithHigherPrecedenceWithAnIpv4Address) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe::5001]:443", {"[3ffe::5002]:0", AF_INET6}},
          {"1.2.3.4:443", {"5.6.7.8:0", AF_INET}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[3ffe::5001]:443", AF_INET6},
      {"1.2.3.4:443", AF_INET},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(
      lb_addrs, {
                    // The AF_INET address should be IPv4-mapped by the sort,
                    // and IPv4-mapped
                    // addresses have higher precedence than 3ffe::/16 by spec.
                    "1.2.3.4:443",
                    "[3ffe::5001]:443",
                });
}

TEST_F(AddressSortingTest,
       TestUsesDestinationWithHigherPrecedenceWithV4CompatAndLocalhostAddress) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  const char* v4_compat_dest = "[::2]:443";
  const char* v4_compat_src = "[::2]:0";
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[::1]:443", {"[::1]:0", AF_INET6}},
          {v4_compat_dest, {v4_compat_src, AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {v4_compat_dest, AF_INET6},
      {"[::1]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  ASSERT_EQ(lb_addrs.size(), 2);
  EXPECT_EQ(
      grpc_sockaddr_to_string(&lb_addrs[0].address(), false /* normalize */)
          .value(),
      "[::1]:443");
  // We've observed some inet_ntop implementations have special representations
  // of IPv4-compatible IPv6 addresses, and others represent them as normal
  // IPv6 addresses. For the purposes of this test, we don't care which
  // representation is used.
  std::vector<std::string> acceptable_addresses = {
      "[::0.0.0.2]:443",
      "[::2]:443",
  };
  EXPECT_THAT(
      acceptable_addresses,
      ::testing::Contains(
          grpc_sockaddr_to_string(&lb_addrs[1].address(), false /* normalize */)
              .value()));
}

TEST_F(AddressSortingTest,
       TestUsesDestinationWithHigherPrecedenceWithCatchAllAndLocalhostAddress) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          // 1234::2 for src and dest to make sure that prefix matching has no
          // influence on this test.
          {"[1234::2]:443", {"[1234::2]:0", AF_INET6}},
          {"[::1]:443", {"[::1]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[1234::2]:443", AF_INET6},
      {"[::1]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(
      lb_addrs,
      {
          // ::1 should match the localhost precedence entry and be prioritized
          "[::1]:443",
          "[1234::2]:443",
      });
}

TEST_F(AddressSortingTest,
       TestUsesDestinationWithHigherPrecedenceWith2000PrefixedAddress) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[2001::1234]:443", {"[2001::5678]:0", AF_INET6}},
          {"[2000::5001]:443", {"[2000::5002]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[2001::1234]:443", AF_INET6},
      {"[2000::5001]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(
      lb_addrs, {
                    // The 2000::/16 address should match the ::/0 prefix rule
                    "[2000::5001]:443",
                    "[2001::1234]:443",
                });
}

TEST_F(
    AddressSortingTest,
    TestUsesDestinationWithHigherPrecedenceWith2000PrefixedAddressEnsurePrefixMatchHasNoEffect) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[2001::1231]:443", {"[2001::1232]:0", AF_INET6}},
          {"[2000::5001]:443", {"[2000::5002]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[2001::1231]:443", AF_INET6},
      {"[2000::5001]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[2000::5001]:443",
                                    "[2001::1231]:443",
                                });
}

TEST_F(AddressSortingTest,
       TestUsesDestinationWithHigherPrecedenceWithLinkAndSiteLocalAddresses) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[fec0::1234]:443", {"[fec0::5678]:0", AF_INET6}},
          {"[fc00::5001]:443", {"[fc00::5002]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[fec0::1234]:443", AF_INET6},
      {"[fc00::5001]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[fc00::5001]:443",
                                    "[fec0::1234]:443",
                                });
}

TEST_F(
    AddressSortingTest,
    TestUsesDestinationWithHigherPrecedenceWithCatchAllAndAndV4MappedAddresses) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  // Use embedded ipv4 addresses with leading 1's instead of zero's to be
  // compatible with inet_ntop implementations that can display such
  // addresses with leading zero's as e.g.: "::ffff:0:2", as on windows.
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[::ffff:1.1.1.2]:443", {"[::ffff:1.1.1.3]:0", AF_INET6}},
          {"[1234::2]:443", {"[1234::3]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[::ffff:1.1.1.2]:443", AF_INET6},
      {"[1234::2]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    // ::ffff:0:2 should match the v4-mapped
                                    // precedence entry and be deprioritized.
                                    "[1234::2]:443",
                                    "[::ffff:1.1.1.2]:443",
                                });
}

// Tests for rule 8

TEST_F(AddressSortingTest, TestPrefersSmallerScope) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          // Both of these destinations have the same precedence in default
          // policy
          // table.
          {"[fec0::1234]:443", {"[fec0::5678]:0", AF_INET6}},
          {"[3ffe::5001]:443", {"[3ffe::5002]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[3ffe::5001]:443", AF_INET6},
      {"[fec0::1234]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[fec0::1234]:443",
                                    "[3ffe::5001]:443",
                                });
}

// Tests for rule 9

TEST_F(AddressSortingTest, TestPrefersLongestMatchingSrcDstPrefix) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          // Both of these destinations have the same precedence in default
          // policy
          // table.
          {"[3ffe:1234::]:443", {"[3ffe:1235::]:0", AF_INET6}},
          {"[3ffe:5001::]:443", {"[3ffe:4321::]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[3ffe:5001::]:443", AF_INET6},
      {"[3ffe:1234::]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe:1234::]:443",
                                    "[3ffe:5001::]:443",
                                });
}

TEST_F(AddressSortingTest,
       TestPrefersLongestMatchingSrcDstPrefixMatchesWholeAddress) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe::1234]:443", {"[3ffe::1235]:0", AF_INET6}},
          {"[3ffe::5001]:443", {"[3ffe::4321]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[3ffe::5001]:443", AF_INET6},
      {"[3ffe::1234]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe::1234]:443",
                                    "[3ffe::5001]:443",
                                });
}

TEST_F(AddressSortingTest, TestPrefersLongestPrefixStressInnerBytePrefix) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe:8000::]:443", {"[3ffe:C000::]:0", AF_INET6}},
          {"[3ffe:2000::]:443", {"[3ffe:3000::]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[3ffe:8000::]:443", AF_INET6},
      {"[3ffe:2000::]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe:2000::]:443",
                                    "[3ffe:8000::]:443",
                                });
}

TEST_F(AddressSortingTest, TestPrefersLongestPrefixDiffersOnHighestBitOfByte) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe:6::]:443", {"[3ffe:8::]:0", AF_INET6}},
          {"[3ffe:c::]:443", {"[3ffe:8::]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[3ffe:6::]:443", AF_INET6},
      {"[3ffe:c::]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe:c::]:443",
                                    "[3ffe:6::]:443",
                                });
}

TEST_F(AddressSortingTest, TestPrefersLongestPrefixDiffersByLastBit) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe:1111:1111:1111::]:443",
           {"[3ffe:1111:1111:1111::]:0", AF_INET6}},
          {"[3ffe:1111:1111:1110::]:443",
           {"[3ffe:1111:1111:1111::]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[3ffe:1111:1111:1110::]:443", AF_INET6},
      {"[3ffe:1111:1111:1111::]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe:1111:1111:1111::]:443",
                                    "[3ffe:1111:1111:1110::]:443",
                                });
}

// Tests for rule 10

TEST_F(AddressSortingTest, TestStableSort) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe::1234]:443", {"[3ffe::1236]:0", AF_INET6}},
          {"[3ffe::1235]:443", {"[3ffe::1237]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[3ffe::1234]:443", AF_INET6},
      {"[3ffe::1235]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe::1234]:443",
                                    "[3ffe::1235]:443",
                                });
}

TEST_F(AddressSortingTest, TestStableSortFiveElements) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe::1231]:443", {"[3ffe::1201]:0", AF_INET6}},
          {"[3ffe::1232]:443", {"[3ffe::1202]:0", AF_INET6}},
          {"[3ffe::1233]:443", {"[3ffe::1203]:0", AF_INET6}},
          {"[3ffe::1234]:443", {"[3ffe::1204]:0", AF_INET6}},
          {"[3ffe::1235]:443", {"[3ffe::1205]:0", AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[3ffe::1231]:443", AF_INET6},
      {"[3ffe::1232]:443", AF_INET6},
      {"[3ffe::1233]:443", AF_INET6},
      {"[3ffe::1234]:443", AF_INET6},
      {"[3ffe::1235]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe::1231]:443",
                                    "[3ffe::1232]:443",
                                    "[3ffe::1233]:443",
                                    "[3ffe::1234]:443",
                                    "[3ffe::1235]:443",
                                });
}

TEST_F(AddressSortingTest, TestStableSortNoSrcAddrsExist) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(ipv4_supported, ipv6_supported, {});
  auto lb_addrs = BuildLbAddrInputs({
      {"[3ffe::1231]:443", AF_INET6},
      {"[3ffe::1232]:443", AF_INET6},
      {"[3ffe::1233]:443", AF_INET6},
      {"[3ffe::1234]:443", AF_INET6},
      {"[3ffe::1235]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe::1231]:443",
                                    "[3ffe::1232]:443",
                                    "[3ffe::1233]:443",
                                    "[3ffe::1234]:443",
                                    "[3ffe::1235]:443",
                                });
}

TEST_F(AddressSortingTest, TestStableSortNoSrcAddrsExistWithIpv4) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(ipv4_supported, ipv6_supported, {});
  auto lb_addrs = BuildLbAddrInputs({
      {"[::ffff:5.6.7.8]:443", AF_INET6},
      {"1.2.3.4:443", AF_INET},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[::ffff:5.6.7.8]:443",
                                    "1.2.3.4:443",
                                });
}

TEST_F(AddressSortingTest, TestStableSortV4CompatAndSiteLocalAddresses) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  const char* v4_compat_dest = "[::2]:443";
  const char* v4_compat_src = "[::3]:0";
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[fec0::2000]:443", {"[fec0::2001]:0", AF_INET6}},
          {v4_compat_dest, {v4_compat_src, AF_INET6}},
      });
  auto lb_addrs = BuildLbAddrInputs({
      {"[fec0::2000]:443", AF_INET6},
      {v4_compat_dest, AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  ASSERT_EQ(lb_addrs.size(), 2);
  // The sort should be stable since
  // v4-compatible has same precedence as site-local.
  EXPECT_EQ(
      grpc_sockaddr_to_string(&lb_addrs[0].address(), false /* normalize */)
          .value(),
      "[fec0::2000]:443");
  // We've observed some inet_ntop implementations have special representations
  // of IPv4-compatible IPv6 addresses, and others represent them as normal
  // IPv6 addresses. For the purposes of this test, we don't care which
  // representation is used.
  std::vector<std::string> acceptable_addresses = {
      "[::0.0.0.2]:443",
      "[::2]:443",
  };
  EXPECT_THAT(
      acceptable_addresses,
      ::testing::Contains(
          grpc_sockaddr_to_string(&lb_addrs[1].address(), false /* normalize */)
              .value()));
}

// TestPrefersIpv6Loopback tests the actual "address probing" code
// for the current platform, without any mocks.
// This test relies on the assumption that the ipv6 loopback address is
// available in the hosts/containers that grpc C/C++ tests run on
// (whether ipv4 loopback is available or not, an available ipv6
// loopback should be preferred).
TEST_F(AddressSortingTest, TestPrefersIpv6Loopback) {
  auto lb_addrs = BuildLbAddrInputs({
      {"[::1]:443", AF_INET6},
      {"127.0.0.1:443", AF_INET},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[::1]:443",
                                    "127.0.0.1:443",
                                });
}

// Flip the order of the inputs above and expect the same output order
// (try to rule out influence of arbitrary qsort ordering)
TEST_F(AddressSortingTest, TestPrefersIpv6LoopbackInputsFlipped) {
  auto lb_addrs = BuildLbAddrInputs({
      {"127.0.0.1:443", AF_INET},
      {"[::1]:443", AF_INET6},
  });
  grpc_cares_wrapper_address_sorting_sort(nullptr, &lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[::1]:443",
                                    "127.0.0.1:443",
                                });
}

// Try to rule out false positives in the above two tests in which
// the sorter might think that neither ipv6 or ipv4 loopback is
// available, but ipv6 loopback is still preferred only due
// to precedence table lookups.
TEST_F(AddressSortingTest, TestSorterKnowsIpv6LoopbackIsAvailable) {
  sockaddr_in6 ipv6_loopback;
  memset(&ipv6_loopback, 0, sizeof(ipv6_loopback));
  ipv6_loopback.sin6_family = AF_INET6;
  (reinterpret_cast<char*>(&ipv6_loopback.sin6_addr))[15] = 1;
  ipv6_loopback.sin6_port = htons(443);
  // Set up the source and destination parameters of
  // address_sorting_get_source_addr
  address_sorting_address sort_input_dest;
  memcpy(&sort_input_dest.addr, &ipv6_loopback, sizeof(ipv6_loopback));
  sort_input_dest.len = sizeof(ipv6_loopback);
  address_sorting_address source_for_sort_input_dest;
  memset(&source_for_sort_input_dest, 0, sizeof(source_for_sort_input_dest));
  // address_sorting_get_source_addr returns true if a source address was found
  // for the destination address, otherwise false.
  EXPECT_TRUE(address_sorting_get_source_addr_for_testing(
      &sort_input_dest, &source_for_sort_input_dest));
  // Now also check that the source address was filled in correctly.
  EXPECT_GT(source_for_sort_input_dest.len, 0u);
  sockaddr_in6* source_addr_output =
      reinterpret_cast<sockaddr_in6*>(source_for_sort_input_dest.addr);
  EXPECT_EQ(source_addr_output->sin6_family, AF_INET6);
  char* buf = static_cast<char*>(gpr_zalloc(100));
  EXPECT_NE(inet_ntop(AF_INET6, &source_addr_output->sin6_addr, buf, 100),
            nullptr)
      << "inet_ntop failed. Errno: " + std::to_string(errno);
  std::string source_addr_str(buf);
  gpr_free(buf);
  // This test
  // assumes that the source address for any loopback destination is also the
  // loopback address.
  EXPECT_EQ(source_addr_str, "::1");
}

}  // namespace

int main(int argc, char** argv) {
  grpc_core::ConfigVars::Overrides overrides;
  overrides.dns_resolver = "ares";
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  // Test sequential and nested inits and shutdowns.
  grpc_init();
  grpc_init();
  grpc_shutdown();
  grpc_shutdown();
  grpc_init();
  grpc_shutdown();
  return result;
}
