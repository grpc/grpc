//
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
//

#include "src/core/telemetry/call_tracer.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>

#include <array>
#include <cstring>
#include <vector>

#include "fuzztest/fuzztest.h"  // keep it here to keep gRPC sanity happy
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

namespace grpc_core {

// Global environment for gRPC lifecycle management.
class GrpcEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { grpc_init(); }
  void TearDown() override { grpc_shutdown(); }
};

::testing::Environment* const grpc_env =
    ::testing::AddGlobalTestEnvironment(new GrpcEnvironment);

namespace {

class CallTracerTest : public ::testing::Test {
 protected:
  RefCountedPtr<Arena> arena_ = SimpleArenaAllocator()->MakeArena();
  std::vector<std::string> annotation_logger_;
};

TEST_F(CallTracerTest, BasicClientCallTracer) {
  FakeClientCallTracer client_call_tracer(&annotation_logger_);
  SetClientCallTracer(arena_.get(), {&client_call_tracer});
  arena_->GetContext<CallSpan>()->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_, std::vector<std::string>{"Test"});
}

TEST_F(CallTracerTest, MultipleClientCallTracers) {
  promise_detail::Context<Arena> arena_ctx(arena_.get());
  FakeClientCallTracer client_call_tracer1(&annotation_logger_);
  FakeClientCallTracer client_call_tracer2(&annotation_logger_);
  FakeClientCallTracer client_call_tracer3(&annotation_logger_);
  SetClientCallTracer(
      arena_.get(),
      std::vector<ClientCallTracerInterface*>{
          &client_call_tracer1, &client_call_tracer2, &client_call_tracer3});
  arena_->GetContext<CallSpan>()->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_,
            std::vector<std::string>({"Test", "Test", "Test"}));
}

TEST_F(CallTracerTest, MultipleClientCallAttemptTracers) {
  promise_detail::Context<Arena> arena_ctx(arena_.get());
  FakeClientCallTracer client_call_tracer1(&annotation_logger_);
  FakeClientCallTracer client_call_tracer2(&annotation_logger_);
  FakeClientCallTracer client_call_tracer3(&annotation_logger_);
  SetClientCallTracer(
      arena_.get(),
      std::vector<ClientCallTracerInterface*>{
          &client_call_tracer1, &client_call_tracer2, &client_call_tracer3});
  auto* attempt_tracer =
      arena_->GetContext<ClientCallTracer>()->StartNewAttempt(
          true /* is_transparent_retry */);
  attempt_tracer->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_,
            std::vector<std::string>({"Test", "Test", "Test"}));
  attempt_tracer->RecordEnd();
}

TEST_F(CallTracerTest, BasicServerCallTracerTest) {
  FakeServerCallTracer server_call_tracer(&annotation_logger_);
  SetServerCallTracer(arena_.get(), {&server_call_tracer});
  arena_->GetContext<CallSpan>()->RecordAnnotation("Test");
  arena_->GetContext<CallSpan>()->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_, std::vector<std::string>({"Test", "Test"}));
}

TEST_F(CallTracerTest, MultipleServerCallTracers) {
  promise_detail::Context<Arena> arena_ctx(arena_.get());
  FakeServerCallTracer server_call_tracer1(&annotation_logger_);
  FakeServerCallTracer server_call_tracer2(&annotation_logger_);
  FakeServerCallTracer server_call_tracer3(&annotation_logger_);
  SetServerCallTracer(
      arena_.get(),
      std::vector<ServerCallTracerInterface*>{
          &server_call_tracer1, &server_call_tracer2, &server_call_tracer3});
  arena_->GetContext<CallSpan>()->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_,
            std::vector<std::string>({"Test", "Test", "Test"}));
}

TEST_F(CallTracerTest, ExtractResolvedAddressIPv4) {
  sockaddr_in addr4;
  memset(&addr4, 0, sizeof(addr4));
  addr4.sin_family = AF_INET;
  addr4.sin_port = htons(8080);
  uint8_t ip[4] = {192, 168, 1, 1};
  memcpy(&addr4.sin_addr.s_addr, ip, 4);

  grpc_event_engine::experimental::EventEngine::ResolvedAddress addr(
      reinterpret_cast<const sockaddr*>(&addr4), sizeof(addr4));

  auto ip_addr = TcpCallTracer::ExtractResolvedAddress(addr);

  EXPECT_EQ(ip_addr.family, AF_INET);
  EXPECT_EQ(ip_addr.port, 8080);
  EXPECT_EQ(ip_addr.ip[0], 192);
  EXPECT_EQ(ip_addr.ip[1], 168);
  EXPECT_EQ(ip_addr.ip[2], 1);
  EXPECT_EQ(ip_addr.ip[3], 1);
}

TEST_F(CallTracerTest, ExtractResolvedAddressIPv6) {
  sockaddr_in6 addr6;
  memset(&addr6, 0, sizeof(addr6));
  addr6.sin6_family = AF_INET6;
  addr6.sin6_port = htons(9090);
  uint8_t ip6[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                     0,    0,    0,    0,    0, 0, 0, 0x01};
  memcpy(&addr6.sin6_addr.s6_addr, ip6, 16);

  grpc_event_engine::experimental::EventEngine::ResolvedAddress addr(
      reinterpret_cast<const sockaddr*>(&addr6), sizeof(addr6));

  auto ip_addr = TcpCallTracer::ExtractResolvedAddress(addr);

  EXPECT_EQ(ip_addr.family, AF_INET6);
  EXPECT_EQ(ip_addr.port, 9090);
  EXPECT_EQ(ip_addr.ip[0], 0x20);
  EXPECT_EQ(ip_addr.ip[1], 0x01);
  EXPECT_EQ(ip_addr.ip[2], 0x0d);
  EXPECT_EQ(ip_addr.ip[3], 0xb8);
  EXPECT_EQ(ip_addr.ip[15], 0x01);
}

}  // namespace

// Fuzz test for ExtractResolvedAddress.
void FuzzExtractResolvedAddress(
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr) {
  auto ip_addr = TcpCallTracer::ExtractResolvedAddress(addr);
  if (ip_addr.family != 0) {
    EXPECT_EQ(ip_addr.family, addr.address()->sa_family);
  }
}

// Helper to create arbitrary ResolvedAddress.
fuzztest::Domain<grpc_event_engine::experimental::EventEngine::ResolvedAddress>
ArbitraryResolvedAddress() {
  return fuzztest::Map(
      [](uint16_t family, uint16_t port, std::array<uint8_t, 16> ip) {
        if (family == AF_INET) {
          sockaddr_in addr4;
          std::memset(&addr4, 0, sizeof(addr4));
          addr4.sin_family = AF_INET;
          addr4.sin_port = port;
          std::memcpy(&addr4.sin_addr.s_addr, ip.data(), 4);
          return grpc_event_engine::experimental::EventEngine::ResolvedAddress(
              reinterpret_cast<const sockaddr*>(&addr4), sizeof(addr4));
        } else if (family == AF_INET6) {
          sockaddr_in6 addr6;
          std::memset(&addr6, 0, sizeof(addr6));
          addr6.sin6_family = AF_INET6;
          addr6.sin6_port = port;
          std::memcpy(&addr6.sin6_addr.s6_addr, ip.data(), 16);
          return grpc_event_engine::experimental::EventEngine::ResolvedAddress(
              reinterpret_cast<const sockaddr*>(&addr6), sizeof(addr6));
        } else {
          sockaddr addr;
          std::memset(&addr, 0, sizeof(addr));
          addr.sa_family = family;
          return grpc_event_engine::experimental::EventEngine::ResolvedAddress(
              &addr, sizeof(addr));
        }
      },
      fuzztest::Arbitrary<uint16_t>(), fuzztest::Arbitrary<uint16_t>(),
      fuzztest::Arbitrary<std::array<uint8_t, 16>>());
}

FUZZ_TEST(CallTracerFuzzTest, FuzzExtractResolvedAddress)
    .WithDomains(ArbitraryResolvedAddress());

}  // namespace grpc_core
