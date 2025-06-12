// Copyright 2025 The gRPC Authors
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

#include <gmock/gmock.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/util/notification.h"
#include "test/core/event_engine/posix/dns_server.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

namespace grpc_event_engine::experimental {

#ifdef GRPC_ENABLE_FORK_SUPPORT

constexpr absl::string_view kHost = "fork_test";

MATCHER_P2(ResolvedTo, ipv4, ipv6, "") {
  if (IsIpv6LoopbackAvailable()) {
    return ::testing::ExplainMatchResult(
        ::testing::UnorderedElementsAre(ipv4, ipv6), arg, result_listener);
  }
  return ::testing::ExplainMatchResult(::testing::ElementsAre(ipv4), arg,
                                       result_listener);
}

class LookupCallback {
 public:
  explicit LookupCallback(absl::string_view label) : label_(label) {}

  EventEngine::DNSResolver::LookupHostnameCallback lookup_hostname_callback() {
    return [this](const auto& addresses) {
      ++times_called_;
      if (addresses.ok()) {
        result_.emplace();
        for (const auto& address : addresses.value()) {
          auto resolved = ResolvedAddressToString(address);
          result_->emplace_back(resolved.ok() ? *resolved
                                              : resolved.status().ToString());
        }
        LOG(INFO) << "[" << label_ << "] Hostname resolved to "
                  << absl::StrJoin(*result_, ", ");
      } else {
        result_ = addresses.status();
        LOG(INFO) << "[" << label_ << "] Failed with " << result_.status();
      }
      notification_.Notify();
    };
  }

  absl::StatusOr<std::vector<std::string>> result() {
    notification_.WaitForNotification();
    return result_;
  }

  int times_got_called() const { return times_called_; }

 private:
  std::string label_;
  absl::StatusOr<std::vector<std::string>> result_;
  grpc_core::Notification notification_;
  int times_called_ = 0;
};

// In a parent process, request made before fork can be resolved post-fork
TEST(DnsForkTest, DnsLookupAcrossForkInParent) {
  auto dns_server = DnsServer::Start(grpc_pick_unused_port_or_die());
  auto ee = std::static_pointer_cast<PosixEventEngine>(GetDefaultEventEngine());
  auto resolver = ee->GetDNSResolver({.dns_server = dns_server->address()});
  ASSERT_TRUE(resolver.ok()) << resolver.status();
  std::array callbacks = {
      LookupCallback("DnsLookupAcrossForkInParent pre-fork 1"),
      LookupCallback("DnsLookupAcrossForkInParent pre-fork 2"),
      LookupCallback("DnsLookupAcrossForkInParent pre-fork 3")};
  for (auto& callback : callbacks) {
    resolver->get()->LookupHostname(callback.lookup_hostname_callback(), kHost,
                                    "443");
  }
  DnsQuestion question = dns_server->WaitForQuestion(kHost);
  // This will be fully qualified domain name, so we only check the prefix
  ASSERT_THAT(question.qname, ::testing::StartsWith(kHost));
  // A or AAAA
  ASSERT_THAT(question.qtype, ::testing::AnyOf(1, 28));
  ASSERT_EQ(question.qclass, 1);
  // Do the fork
  ee->BeforeFork();
  LOG(INFO) << "------------------------";
  LOG(INFO) << "         Forking        ";
  LOG(INFO) << "------------------------";
  ee->AfterFork(PosixEventEngine::OnForkRole::kParent);
  dns_server->SetIPv4Response(kHost, {1, 1, 1, 1});
  for (auto& callback : callbacks) {
    auto result = callback.result();
    ASSERT_TRUE(result.ok()) << result.status();
    EXPECT_THAT(
        result.value(),
        ResolvedTo("1.1.1.1:443", "[101:101:101:101:101:101:101:101]:443"));
  }
  // Ensure callbacks were only called once
  for (auto& callback : callbacks) {
    EXPECT_EQ(callback.times_got_called(), 1);
  }
}

// In a child process, a request made before fork will fail because of the Ares
// shutdown. Requests made post fork will succeed.
TEST(DnsForkTest, DnsLookupAcrossForkInChild) {
  auto dns_server = DnsServer::Start(grpc_pick_unused_port_or_die());
  auto ee = std::static_pointer_cast<PosixEventEngine>(GetDefaultEventEngine());
  auto resolver = ee->GetDNSResolver({.dns_server = dns_server->address()});
  ASSERT_TRUE(resolver.ok()) << resolver.status();
  // Ensure all 3 callbacks are cancelled
  std::array callbacks = {
      LookupCallback("DnsLookupAcrossForkInChild pre-fork 1"),
      LookupCallback("DnsLookupAcrossForkInChild pre-fork 2"),
      LookupCallback("DnsLookupAcrossForkInChild pre-fork 3")};
  for (auto& callback : callbacks) {
    resolver->get()->LookupHostname(callback.lookup_hostname_callback(), kHost,
                                    "443");
  }
  LookupCallback host2_cb("Host2 callback");
  resolver->get()->LookupHostname(host2_cb.lookup_hostname_callback(), "host2",
                                  "443");
  dns_server->SetIPv4Response("host2", {9, 9, 9, 9});
  auto result = host2_cb.result();
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(
      result.value(),
      ResolvedTo("9.9.9.9:443", "[909:909:909:909:909:909:909:909]:443"));
  DnsQuestion question = dns_server->WaitForQuestion(kHost);
  LOG(INFO) << "Pre fork question " << question.id;
  ASSERT_THAT(question.qname, ::testing::StartsWith(kHost));
  // Expected questions are A or AAAA
  ASSERT_THAT(question.qtype, ::testing::AnyOf(1, 28));
  ASSERT_EQ(question.qclass, 1);
  // Do the fork
  ee->BeforeFork();
  LOG(INFO) << "------------------------";
  LOG(INFO) << "         Forking        ";
  LOG(INFO) << "------------------------";
  ee->AfterFork(PosixEventEngine::OnForkRole::kChild);
  for (auto& callback : callbacks) {
    result = callback.result();
    // Request is cancelled on fork
    ASSERT_TRUE(absl::IsCancelled(result.status())) << result.status();
  }
  dns_server->SetIPv4Response(kHost, {2, 2, 2, 2});
  LookupCallback cb2("DnsLookupAcrossForkInChild post-fork");
  resolver->get()->LookupHostname(cb2.lookup_hostname_callback(), kHost, "443");
  result = cb2.result();
  LOG(INFO) << "Post-fork lookup done";
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(
      result.value(),
      ResolvedTo("2.2.2.2:443", "[202:202:202:202:202:202:202:202]:443"));
  // Ensure callbacks were only called once
  EXPECT_EQ(host2_cb.times_got_called(), 1);
  for (auto& callback : callbacks) {
    EXPECT_EQ(callback.times_got_called(), 1);
  }
}

#else  // GRPC_ENABLE_FORK_SUPPORT

TEST(AresResolverTest, Skipped) { GTEST_SKIP() << "Fork is disabled"; }

#endif  // GRPC_ENABLE_FORK_SUPPORT

}  // namespace grpc_event_engine::experimental

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}