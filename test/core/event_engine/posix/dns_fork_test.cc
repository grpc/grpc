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

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/util/notification.h"
#include "test/core/event_engine/posix/dns_server.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

namespace grpc_event_engine::experimental {

constexpr absl::string_view kHost = "fork_test";
constexpr std::array<uint8_t, 4> kIPv4 = {1, 1, 1, 1};
constexpr std::array<uint8_t, 16> kIPv6 = {1, 1, 1, 1, 2, 2, 2, 2,
                                           3, 3, 3, 3, 4, 4, 4, 4};

MATCHER_P2(ResolvedTo, ipv4, ipv6, "") {
  if (IsIpv6LoopbackAvailable()) {
    return ::testing::ExplainMatchResult(
        ::testing::UnorderedElementsAre(ipv4, ipv6), arg, result_listener);
  }
  return ::testing::ExplainMatchResult(::testing::ElementsAre(ipv4), arg,
                                       result_listener);
}

std::vector<uint8_t> GetAddressForQuestion(const DnsQuestion& q) {
  if (absl::StartsWith(q.qname, kHost)) {
    if (q.qtype == 1) {
      return {kIPv4.begin(), kIPv4.end()};
    } else if (q.qtype == 28) {
      return {kIPv6.begin(), kIPv6.end()};
    }
  }
  return {};
}

class LookupCallback {
 public:
  explicit LookupCallback(absl::string_view label) : label_(label) {}

  EventEngine::DNSResolver::LookupHostnameCallback lookup_hostname_callback() {
    return [self = this](const auto& addresses) {
      if (addresses.ok()) {
        self->result_.emplace();
        for (const auto& address : addresses.value()) {
          auto resolved = ResolvedAddressToString(address);
          self->result_->emplace_back(
              resolved.ok() ? resolved.value() : resolved.status().ToString());
        }
        LOG(INFO) << "[" << self->label_ << "] Hostname resolved to "
                  << absl::StrJoin(self->result_.value(), ", ");
      } else {
        self->result_ = addresses.status();
        LOG(INFO) << "[" << self->label_ << "] Failed with "
                  << self->result_.status();
      }
      self->notification_.Notify();
    };
  }

  absl::StatusOr<std::vector<std::string>> result() {
    notification_.WaitForNotification();
    return result_;
  }

 private:
  std::string label_;
  absl::StatusOr<std::vector<std::string>> result_;
  grpc_core::Notification notification_;
};

#ifdef GRPC_ENABLE_FORK_SUPPORT

class DnsForkTest : public testing::Test {
 protected:
  void SetUp() override {
    event_engine_ =
        std::static_pointer_cast<PosixEventEngine>(GetDefaultEventEngine());
    ASSERT_NE(event_engine_, nullptr);
  }

  void TearDown() override { factory_.reset(); }

  std::shared_ptr<PosixEventEngine> event_engine_;
  std::unique_ptr<GrpcPolledFdFactory> factory_;
};

// AresResolver should work across fork
TEST_F(DnsForkTest, DnsLookupAcrossForkInParent) {
  auto dns_server = DnsServer::Start(grpc_pick_unused_port_or_die());
  auto resolver =
      event_engine_->GetDNSResolver({.dns_server = dns_server->address()});
  ASSERT_TRUE(resolver.ok()) << resolver.status();
  LookupCallback callback("DnsLookupAcrossForkInParent");
  resolver->get()->LookupHostname(callback.lookup_hostname_callback(), kHost,
                                  "443");
  DnsQuestion question = dns_server->WaitForQuestion();
  ASSERT_THAT(question.qname, ::testing::StartsWith(kHost));
  // A or AAAA
  ASSERT_THAT(question.qtype, ::testing::AnyOf(1, 28));
  ASSERT_EQ(question.qclass, 1);
  // Do the fork
  event_engine_->BeforeFork();
  LOG(INFO) << "------------------------";
  LOG(INFO) << "         Forking        ";
  LOG(INFO) << "------------------------";
  event_engine_->AfterFork(PosixEventEngine::OnForkRole::kParent);
  dns_server->SetResponder(GetAddressForQuestion);
  auto result = callback.result();
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(
      result.value(),
      ResolvedTo("1.1.1.1:443", "[101:101:202:202:303:303:404:404]:443"));
}

// Request sent before fork will fail because of Ares shutdown. Afterwards
// it should still be possible to make new requests.
TEST_F(DnsForkTest, DnsLookupAcrossForkInChild) {
  auto dns_server = DnsServer::Start(grpc_pick_unused_port_or_die());
  auto resolver =
      event_engine_->GetDNSResolver({.dns_server = dns_server->address()});
  ASSERT_TRUE(resolver.ok()) << resolver.status();
  LookupCallback callback("DnsLookupAcrossForkInChild pre-fork");
  resolver->get()->LookupHostname(callback.lookup_hostname_callback(), kHost,
                                  "443");
  DnsQuestion question = dns_server->WaitForQuestion();
  LOG(INFO) << "Pre fork question " << question.id;
  ASSERT_THAT(question.qname, ::testing::StartsWith(kHost));
  // Expected questions are A or AAAA
  ASSERT_THAT(question.qtype, ::testing::AnyOf(1, 28));
  ASSERT_EQ(question.qclass, 1);
  // Do the fork
  event_engine_->BeforeFork();
  LOG(INFO) << "------------------------";
  LOG(INFO) << "         Forking        ";
  LOG(INFO) << "------------------------";
  event_engine_->AfterFork(PosixEventEngine::OnForkRole::kChild);
  auto result = callback.result();
  // Request is cancelled on fork
  ASSERT_TRUE(absl::IsUnknown(result.status())) << result.status();
  dns_server->SetResponder(GetAddressForQuestion);
  LookupCallback cb2("DnsLookupAcrossForkInChild post-fork");
  resolver->get()->LookupHostname(cb2.lookup_hostname_callback(), kHost, "443");
  result = cb2.result();
  LOG(INFO) << "Post-fork lookup done";
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(
      result.value(),
      ResolvedTo("1.1.1.1:443", "[101:101:202:202:303:303:404:404]:443"));
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