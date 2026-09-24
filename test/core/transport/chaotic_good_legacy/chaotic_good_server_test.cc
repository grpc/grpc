// Copyright 2026 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good_legacy/server/chaotic_good_server.h"

#include <memory>
#include <optional>
#include <utility>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/map.h"
#include "src/core/transport/auth_context.h"
#include "src/core/transport/auth_context_comparator_registry.h"
#include "test/core/transport/chaotic_good_legacy/transport_test.h"
#include "test/core/transport/util/mock_promise_endpoint.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"

using grpc_core::util::testing::MockPromiseEndpoint;

namespace grpc_core {
namespace chaotic_good_legacy {
namespace testing {

namespace {

RefCountedPtr<grpc_auth_context> MakeAuthContext() {
  auto ctx = MakeRefCounted<grpc_auth_context>(nullptr);
  ctx->set_protocol("test_protocol");
  return ctx;
}

}  // namespace

// TransportTest is only used for its FuzzingEventEngine fixture.
class DataConnectionListenerTest : public TransportTest {
 protected:
  // Drives one FinishDataConnection scenario: a data connection whose auth
  // context compares as `same_peer` against the control connection's auth
  // context. The pending connection must resolve to the endpoint when the
  // peers match, and to UnauthenticatedError("Auth context mismatch") when
  // they do not.
  void RunFinishDataConnectionTest(bool same_peer) {
    CoreConfiguration::RunWithSpecialConfiguration(
        [same_peer](CoreConfiguration::Builder* builder) {
          builder->auth_context_comparator_registry()->RegisterComparator(
              "test_protocol",
              std::make_unique<absl::AnyInvocable<bool(
                  const grpc_auth_context*, const grpc_auth_context*)>>(
                  [same_peer](const grpc_auth_context*,
                              const grpc_auth_context*) { return same_peer; }));
        },
        [this, same_peer]() {
          auto listener =
              MakeRefCounted<ChaoticGoodServerListener::DataConnectionListener>(
                  []() { return "conn-1"; }, Duration::Seconds(5),
                  event_engine());
          ChannelArgs args = ChannelArgs().SetObject(MakeAuthContext());
          auto pending = listener->RequestDataConnection(args);
          EXPECT_EQ(pending.id(), "conn-1");
          MockPromiseEndpoint endpoint(1);
          listener->FinishDataConnection("conn-1",
                                         std::move(endpoint.promise_endpoint),
                                         MakeAuthContext());
          std::optional<absl::StatusOr<PromiseEndpoint>> result;
          auto activity = MakeActivity(
              [pending = std::move(pending), &result]() mutable {
                return Map(pending.Await(),
                           [&result](absl::StatusOr<PromiseEndpoint> r) {
                             result.emplace(std::move(r));
                             return absl::OkStatus();
                           });
              },
              EventEngineWakeupScheduler(event_engine()), [](absl::Status) {},
              MakeArena());
          event_engine()->TickUntilIdle();
          ASSERT_TRUE(result.has_value());
          if (same_peer) {
            EXPECT_TRUE(result->ok()) << result->status();
          } else {
            ASSERT_FALSE(result->ok());
            EXPECT_EQ(result->status().code(),
                      absl::StatusCode::kUnauthenticated);
            EXPECT_THAT(result->status().message(),
                        ::testing::HasSubstr("Auth context mismatch"));
          }
        });
  }
};

TEST_F(DataConnectionListenerTest, MatchingPeerBindsConnection) {
  RunFinishDataConnectionTest(/*same_peer=*/true);
}

TEST_F(DataConnectionListenerTest, MismatchedPeerDropsConnection) {
  RunFinishDataConnectionTest(/*same_peer=*/false);
}

}  // namespace testing
}  // namespace chaotic_good_legacy
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
