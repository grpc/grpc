/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>

#include <arpa/inet.h>
#include <gflags/gflags.h>
#include <gmock/gmock.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

#include <address_sorting/address_sorting.h>
#include "test/cpp/util/subprocess.h"
#include "test/cpp/util/test_config.h"

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace {

struct TestAddress {
  std::string dest_addr;
  int family;
};

grpc_resolved_address TestAddressToGrpcResolvedAddress(TestAddress test_addr) {
  char* host;
  char* port;
  grpc_resolved_address resolved_addr;
  gpr_split_host_port(test_addr.dest_addr.c_str(), &host, &port);
  if (test_addr.family == AF_INET) {
    sockaddr_in in_dest;
    memset(&in_dest, 0, sizeof(sockaddr_in));
    in_dest.sin_port = htons(atoi(port));
    in_dest.sin_family = AF_INET;
    GPR_ASSERT(inet_pton(AF_INET, host, &in_dest.sin_addr) == 1);
    memcpy(&resolved_addr.addr, &in_dest, sizeof(sockaddr_in));
    resolved_addr.len = sizeof(sockaddr_in);
  } else {
    GPR_ASSERT(test_addr.family == AF_INET6);
    sockaddr_in6 in6_dest;
    memset(&in6_dest, 0, sizeof(sockaddr_in6));
    in6_dest.sin6_port = htons(atoi(port));
    in6_dest.sin6_family = AF_INET6;
    GPR_ASSERT(inet_pton(AF_INET6, host, &in6_dest.sin6_addr) == 1);
    memcpy(&resolved_addr.addr, &in6_dest, sizeof(sockaddr_in6));
    resolved_addr.len = sizeof(sockaddr_in6);
  }
  gpr_free(host);
  gpr_free(port);
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
    char* ip_addr_str;
    grpc_resolved_address dest_addr_as_resolved_addr;
    memcpy(&dest_addr_as_resolved_addr.addr, dest_addr, dest_addr->len);
    dest_addr_as_resolved_addr.len = dest_addr->len;
    grpc_sockaddr_to_string(&ip_addr_str, &dest_addr_as_resolved_addr,
                            false /* normalize */);
    auto it = dest_addr_to_src_addr_.find(ip_addr_str);
    if (it == dest_addr_to_src_addr_.end()) {
      gpr_log(GPR_DEBUG, "can't find |%s| in dest to src map", ip_addr_str);
      gpr_free(ip_addr_str);
      return false;
    }
    gpr_free(ip_addr_str);
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

static bool mock_source_addr_factory_wrapper_get_source_addr(
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

grpc_lb_addresses* BuildLbAddrInputs(std::vector<TestAddress> test_addrs) {
  grpc_lb_addresses* lb_addrs = grpc_lb_addresses_create(0, nullptr);
  lb_addrs->addresses =
      (grpc_lb_address*)gpr_zalloc(sizeof(grpc_lb_address) * test_addrs.size());
  lb_addrs->num_addresses = test_addrs.size();
  for (size_t i = 0; i < test_addrs.size(); i++) {
    lb_addrs->addresses[i].address =
        TestAddressToGrpcResolvedAddress(test_addrs[i]);
  }
  return lb_addrs;
}

void VerifyLbAddrOutputs(grpc_lb_addresses* lb_addrs,
                         std::vector<std::string> expected_addrs) {
  EXPECT_EQ(lb_addrs->num_addresses, expected_addrs.size());
  for (size_t i = 0; i < lb_addrs->num_addresses; i++) {
    char* ip_addr_str;
    grpc_sockaddr_to_string(&ip_addr_str, &lb_addrs->addresses[i].address,
                            false /* normalize */);
    EXPECT_EQ(expected_addrs[i], ip_addr_str);
    gpr_free(ip_addr_str);
  }
  grpc_core::ExecCtx exec_ctx;
  grpc_lb_addresses_destroy(lb_addrs);
}

}  // namespace

/* Tests for rule 1 */
TEST(AddressSortingTest, TestDepriotizesUnreachableAddresses) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"1.2.3.4:443", {"4.3.2.1:443", AF_INET}},
      });
  auto* lb_addrs = BuildLbAddrInputs({
      {"1.2.3.4:443", AF_INET},
      {"5.6.7.8:443", AF_INET},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "1.2.3.4:443",
                                    "5.6.7.8:443",
                                });
}

TEST(AddressSortingTest, TestDepriotizesUnsupportedDomainIpv6) {
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
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "1.2.3.4:443",
                                    "[2607:f8b0:400a:801::1002]:443",
                                });
}

TEST(AddressSortingTest, TestDepriotizesUnsupportedDomainIpv4) {
  bool ipv4_supported = false;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"1.2.3.4:443", {"4.3.2.1:0", AF_INET}},
          {"[2607:f8b0:400a:801::1002]:443", {"[fec0::1234]:0", AF_INET6}},
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[2607:f8b0:400a:801::1002]:443", AF_INET6},
      {"1.2.3.4:443", AF_INET},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[2607:f8b0:400a:801::1002]:443",
                                    "1.2.3.4:443",
                                });
}

/* Tests for rule 2 */

TEST(AddressSortingTest, TestDepriotizesNonMatchingScope) {
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
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[2000:f8b0:400a:801::1002]:443", AF_INET6},
      {"[fec0::5000]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[fec0::5000]:443",
                                    "[2000:f8b0:400a:801::1002]:443",
                                });
}

/* Tests for rule 5 */

TEST(AddressSortingTest, TestUsesLabelFromDefaultTable) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[2002::5001]:443", {"[2001::5002]:0", AF_INET6}},
          {"[2001::5001]:443",
           {"[2001::5002]:0", AF_INET6}},  // matching labels
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[2002::5001]:443", AF_INET6},
      {"[2001::5001]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[2001::5001]:443",
                                    "[2002::5001]:443",
                                });
}

/* Flip the input on the test above to reorder the sort function's
 * comparator's inputs. */
TEST(AddressSortingTest, TestUsesLabelFromDefaultTableInputFlipped) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[2002::5001]:443", {"[2001::5002]:0", AF_INET6}},
          {"[2001::5001]:443",
           {"[2001::5002]:0", AF_INET6}},  // matching labels
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[2001::5001]:443", AF_INET6},
      {"[2002::5001]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[2001::5001]:443",
                                    "[2002::5001]:443",
                                });
}

/* Tests for rule 6 */

TEST(AddressSortingTest,
     TestUsesDestinationWithHigherPrecedenceWithAnIpv4Address) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe::5001]:443", {"[3ffe::5002]:0", AF_INET6}},
          {"1.2.3.4:443", {"5.6.7.8:0", AF_INET}},
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[3ffe::5001]:443", AF_INET6},
      {"1.2.3.4:443", AF_INET},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(
      lb_addrs, {
                    // The AF_INET address should be IPv4-mapped by the sort,
                    // and IPv4-mapped
                    // addresses have higher precedence than 3ffe::/16 by spec.
                    "1.2.3.4:443",
                    "[3ffe::5001]:443",
                });
}

TEST(AddressSortingTest,
     TestUsesDestinationWithHigherPrecedenceWithV4CompatAndLocalhostAddress) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
// Handle unique observed behavior of inet_ntop(v4-compatible-address) on OS X.
#if GPR_APPLE == 1
  const char* v4_compat_dest = "[::0.0.0.2]:443";
  const char* v4_compat_src = "[::0.0.0.2]:0";
#else
  const char* v4_compat_dest = "[::2]:443";
  const char* v4_compat_src = "[::2]:0";
#endif
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[::1]:443", {"[::1]:0", AF_INET6}},
          {v4_compat_dest, {v4_compat_src, AF_INET6}},
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {v4_compat_dest, AF_INET6},
      {"[::1]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[::1]:443",
                                    v4_compat_dest,
                                });
}

TEST(AddressSortingTest,
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
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[1234::2]:443", AF_INET6},
      {"[::1]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(
      lb_addrs,
      {
          // ::1 should match the localhost precedence entry and be prioritized
          "[::1]:443",
          "[1234::2]:443",
      });
}

TEST(AddressSortingTest,
     TestUsesDestinationWithHigherPrecedenceWith2000PrefixedAddress) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[2001::1234]:443", {"[2001::5678]:0", AF_INET6}},
          {"[2000::5001]:443", {"[2000::5002]:0", AF_INET6}},
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[2001::1234]:443", AF_INET6},
      {"[2000::5001]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(
      lb_addrs, {
                    // The 2000::/16 address should match the ::/0 prefix rule
                    "[2000::5001]:443",
                    "[2001::1234]:443",
                });
}

TEST(
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
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[2001::1231]:443", AF_INET6},
      {"[2000::5001]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[2000::5001]:443",
                                    "[2001::1231]:443",
                                });
}

TEST(AddressSortingTest,
     TestUsesDestinationWithHigherPrecedenceWithLinkAndSiteLocalAddresses) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[fec0::1234]:443", {"[fec0::5678]:0", AF_INET6}},
          {"[fc00::5001]:443", {"[fc00::5002]:0", AF_INET6}},
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[fec0::1234]:443", AF_INET6},
      {"[fc00::5001]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[fc00::5001]:443",
                                    "[fec0::1234]:443",
                                });
}

TEST(
    AddressSortingTest,
    TestUsesDestinationWithHigherPrecedenceWithCatchAllAndAndV4MappedAddresses) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[::ffff:0.0.0.2]:443", {"[::ffff:0.0.0.3]:0", AF_INET6}},
          {"[1234::2]:443", {"[1234::3]:0", AF_INET6}},
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[::ffff:0.0.0.2]:443", AF_INET6},
      {"[1234::2]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    // ::ffff:0:2 should match the v4-mapped
                                    // precedence entry and be deprioritized.
                                    "[1234::2]:443",
                                    "[::ffff:0.0.0.2]:443",
                                });
}

/* Tests for rule 8 */

TEST(AddressSortingTest, TestPrefersSmallerScope) {
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
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[3ffe::5001]:443", AF_INET6},
      {"[fec0::1234]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[fec0::1234]:443",
                                    "[3ffe::5001]:443",
                                });
}

/* Tests for rule 9 */

TEST(AddressSortingTest, TestPrefersLongestMatchingSrcDstPrefix) {
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
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[3ffe:5001::]:443", AF_INET6},
      {"[3ffe:1234::]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe:1234::]:443",
                                    "[3ffe:5001::]:443",
                                });
}

TEST(AddressSortingTest,
     TestPrefersLongestMatchingSrcDstPrefixMatchesWholeAddress) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe::1234]:443", {"[3ffe::1235]:0", AF_INET6}},
          {"[3ffe::5001]:443", {"[3ffe::4321]:0", AF_INET6}},
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[3ffe::5001]:443", AF_INET6},
      {"[3ffe::1234]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe::1234]:443",
                                    "[3ffe::5001]:443",
                                });
}

TEST(AddressSortingTest, TestPrefersLongestPrefixStressInnerBytePrefix) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe:8000::]:443", {"[3ffe:C000::]:0", AF_INET6}},
          {"[3ffe:2000::]:443", {"[3ffe:3000::]:0", AF_INET6}},
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[3ffe:8000::]:443", AF_INET6},
      {"[3ffe:2000::]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe:2000::]:443",
                                    "[3ffe:8000::]:443",
                                });
}

TEST(AddressSortingTest, TestPrefersLongestPrefixDiffersOnHighestBitOfByte) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe:6::]:443", {"[3ffe:8::]:0", AF_INET6}},
          {"[3ffe:c::]:443", {"[3ffe:8::]:0", AF_INET6}},
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[3ffe:6::]:443", AF_INET6},
      {"[3ffe:c::]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe:c::]:443",
                                    "[3ffe:6::]:443",
                                });
}

TEST(AddressSortingTest, TestPrefersLongestPrefixDiffersByLastBit) {
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
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[3ffe:1111:1111:1110::]:443", AF_INET6},
      {"[3ffe:1111:1111:1111::]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe:1111:1111:1111::]:443",
                                    "[3ffe:1111:1111:1110::]:443",
                                });
}

/* Tests for rule 10 */

TEST(AddressSortingTest, TestStableSort) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[3ffe::1234]:443", {"[3ffe::1236]:0", AF_INET6}},
          {"[3ffe::1235]:443", {"[3ffe::1237]:0", AF_INET6}},
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[3ffe::1234]:443", AF_INET6},
      {"[3ffe::1235]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe::1234]:443",
                                    "[3ffe::1235]:443",
                                });
}

TEST(AddressSortingTest, TestStableSortFiveElements) {
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
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[3ffe::1231]:443", AF_INET6},
      {"[3ffe::1232]:443", AF_INET6},
      {"[3ffe::1233]:443", AF_INET6},
      {"[3ffe::1234]:443", AF_INET6},
      {"[3ffe::1235]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe::1231]:443",
                                    "[3ffe::1232]:443",
                                    "[3ffe::1233]:443",
                                    "[3ffe::1234]:443",
                                    "[3ffe::1235]:443",
                                });
}

TEST(AddressSortingTest, TestStableSortNoSrcAddrsExist) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(ipv4_supported, ipv6_supported, {});
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[3ffe::1231]:443", AF_INET6},
      {"[3ffe::1232]:443", AF_INET6},
      {"[3ffe::1233]:443", AF_INET6},
      {"[3ffe::1234]:443", AF_INET6},
      {"[3ffe::1235]:443", AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[3ffe::1231]:443",
                                    "[3ffe::1232]:443",
                                    "[3ffe::1233]:443",
                                    "[3ffe::1234]:443",
                                    "[3ffe::1235]:443",
                                });
}

TEST(AddressSortingTest, TestStableSortNoSrcAddrsExistWithIpv4) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
  OverrideAddressSortingSourceAddrFactory(ipv4_supported, ipv6_supported, {});
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[::ffff:5.6.7.8]:443", AF_INET6},
      {"1.2.3.4:443", AF_INET},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs, {
                                    "[::ffff:5.6.7.8]:443",
                                    "1.2.3.4:443",
                                });
}

TEST(AddressSortingTest, TestStableSortV4CompatAndSiteLocalAddresses) {
  bool ipv4_supported = true;
  bool ipv6_supported = true;
// Handle unique observed behavior of inet_ntop(v4-compatible-address) on OS X.
#if GPR_APPLE == 1
  const char* v4_compat_dest = "[::0.0.0.2]:443";
  const char* v4_compat_src = "[::0.0.0.3]:0";
#else
  const char* v4_compat_dest = "[::2]:443";
  const char* v4_compat_src = "[::3]:0";
#endif
  OverrideAddressSortingSourceAddrFactory(
      ipv4_supported, ipv6_supported,
      {
          {"[fec0::2000]:443", {"[fec0::2001]:0", AF_INET6}},
          {v4_compat_dest, {v4_compat_src, AF_INET6}},
      });
  grpc_lb_addresses* lb_addrs = BuildLbAddrInputs({
      {"[fec0::2000]:443", AF_INET6},
      {v4_compat_dest, AF_INET6},
  });
  grpc_cares_wrapper_test_only_address_sorting_sort(lb_addrs);
  VerifyLbAddrOutputs(lb_addrs,
                      {
                          // The sort should be stable since
                          // v4-compatible has same precedence as site-local.
                          "[fec0::2000]:443",
                          v4_compat_dest,
                      });
}

int main(int argc, char** argv) {
  char* resolver = gpr_getenv("GRPC_DNS_RESOLVER");
  if (resolver == nullptr || strlen(resolver) == 0) {
    gpr_setenv("GRPC_DNS_RESOLVER", "ares");
  } else if (strcmp("ares", resolver)) {
    gpr_log(GPR_INFO, "GRPC_DNS_RESOLVER != ares: %s.", resolver);
  }
  gpr_free(resolver);
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  // Test sequential and nested inits and shutdowns.
  grpc_init();
  grpc_init();
  grpc_shutdown();
  grpc_shutdown();
  grpc_init();
  grpc_shutdown();
  return result;
}
