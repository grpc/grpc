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

#include "src/core/ext/transport/chaotic_good_legacy/chaotic_good_transport.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>

#include <memory>
#include <optional>

#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chaotic_good_legacy/config.h"
#include "src/core/ext/transport/chaotic_good_legacy/frame.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/map.h"
#include "src/core/telemetry/metrics.h"
#include "test/core/transport/chaotic_good_legacy/transport_test.h"
#include "test/core/transport/util/mock_promise_endpoint.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

using testing::HasSubstr;

using grpc_core::util::testing::MockPromiseEndpoint;

namespace grpc_core {
namespace chaotic_good_legacy {
namespace testing {

namespace {

RefCountedPtr<ChaoticGoodTransport> MakeTransport(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    PromiseEndpoint control_endpoint, int max_receive_message_length) {
  auto args =
      CoreConfiguration::Get()
          .channel_args_preconditioning()
          .PreconditionChannelArgs(nullptr)
          .SetObject<grpc_event_engine::experimental::EventEngine>(event_engine)
          .Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, max_receive_message_length);
  Config config(args);
  return MakeRefCounted<ChaoticGoodTransport>(
      std::move(control_endpoint), config.TakePendingDataEndpoints(),
      std::move(event_engine),
      args.GetObjectRef<GlobalStatsPluginRegistry::StatsPluginGroup>(),
      config.MakeTransportOptions(), config.tracing_enabled(),
      /*socket_node=*/nullptr);
}

}  // namespace

TEST_F(TransportTest, DeserializeFrameRejectsPayloadLengthMismatch) {
  MockPromiseEndpoint control_endpoint(1);
  auto transport = MakeTransport(event_engine(),
                                 std::move(control_endpoint.promise_endpoint),
                                 /*max_receive_message_length=*/-1);
  auto header = FrameHeader::Parse(
      SerializedFrameHeader(FrameType::kSettings, 0, 0, 100).data());
  ASSERT_TRUE(header.ok());
  SliceBuffer payload;
  payload.Append(
      grpc_event_engine::experimental::internal::SliceCast<Slice>(Zeros(50)));
  auto result =
      transport->DeserializeFrame<SettingsFrame>(*header, std::move(payload));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
  EXPECT_THAT(result.status().message(), HasSubstr("Invalid payload length"));
}

TEST_F(TransportTest, ReadFrameBytesRejectsOversizedPayload) {
  MockPromiseEndpoint control_endpoint(1);
  auto transport = MakeTransport(event_engine(),
                                 std::move(control_endpoint.promise_endpoint),
                                 /*max_receive_message_length=*/100);
  control_endpoint.ExpectRead(
      {SerializedFrameHeader(FrameType::kMessage, 0, 1, 200)},
      event_engine().get());
  std::optional<absl::StatusOr<IncomingFrame>> read_result;
  auto activity = MakeActivity(
      [transport = transport.get(), &read_result]() {
        return Map(transport->ReadFrameBytes(),
                   [&read_result](absl::StatusOr<IncomingFrame> result) {
                     read_result.emplace(std::move(result));
                     return absl::OkStatus();
                   });
      },
      EventEngineWakeupScheduler(event_engine()), [](absl::Status) {},
      MakeArena());
  event_engine()->TickUntilIdle();
  ASSERT_TRUE(read_result.has_value());
  ASSERT_FALSE(read_result->ok());
  EXPECT_EQ(read_result->status().code(), absl::StatusCode::kResourceExhausted);
  EXPECT_THAT(read_result->status().message(), HasSubstr("larger than max"));
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
