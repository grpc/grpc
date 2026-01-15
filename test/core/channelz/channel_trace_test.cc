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

#include "src/core/channelz/channel_trace.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <stdlib.h>

#include <list>
#include <ostream>
#include <string>
#include <thread>

#include "src/core/util/upb_utils.h"
#include "src/proto/grpc/channelz/v2/channelz.upb.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/synchronization/notification.h"

namespace grpc_core {
namespace channelz {
namespace testing {

const int kEventListMemoryLimit = 1024 * 1024;

std::vector<const grpc_channelz_v2_TraceEvent*> GetTraceEvents(
    grpc_channelz_v2_Entity* entity) {
  size_t size;
  const grpc_channelz_v2_TraceEvent* const* trace =
      grpc_channelz_v2_Entity_trace(entity, &size);
  std::vector<const grpc_channelz_v2_TraceEvent*> events;
  for (size_t i = 0; i < size; ++i) {
    events.push_back(trace[i]);
  }
  return events;
}

MATCHER_P(IsTraceEvent, description, "is trace event") {
  if (arg == nullptr) {
    *result_listener << "is null";
    return false;
  }
  std::string actual_desc =
      UpbStringToStdString(grpc_channelz_v2_TraceEvent_description(arg));
  if (actual_desc != description) {
    *result_listener << "description is \"" << actual_desc << "\"";
    return false;
  }
  return true;
}

TEST(ChannelTracerTest, BasicProtoTest) {
  ChannelTrace tracer(kEventListMemoryLimit);
  tracer.NewNode("one").Commit();
  tracer.NewNode("two").Commit();
  tracer.NewNode("three").Commit();
  tracer.NewNode("four").Commit();
  {
    upb_Arena* arena = upb_Arena_New();
    grpc_channelz_v2_Entity* entity = grpc_channelz_v2_Entity_new(arena);
    tracer.Render(entity, arena);
    EXPECT_THAT(
        GetTraceEvents(entity),
        ::testing::ElementsAre(IsTraceEvent("one"), IsTraceEvent("two"),
                               IsTraceEvent("three"), IsTraceEvent("four")));
    upb_Arena_Free(arena);
  }
  tracer.NewNode("five").Commit();
  tracer.NewNode("six").Commit();
  {
    upb_Arena* arena = upb_Arena_New();
    grpc_channelz_v2_Entity* entity = grpc_channelz_v2_Entity_new(arena);
    tracer.Render(entity, arena);
    EXPECT_THAT(
        GetTraceEvents(entity),
        ::testing::ElementsAre(IsTraceEvent("one"), IsTraceEvent("two"),
                               IsTraceEvent("three"), IsTraceEvent("four"),
                               IsTraceEvent("five"), IsTraceEvent("six")));
    upb_Arena_Free(arena);
  }
}

TEST(ChannelTracerTest, StreamingOutputTest) {
  ChannelTrace tracer(kEventListMemoryLimit);
  GRPC_CHANNELZ_LOG(tracer) << "one";
  GRPC_CHANNELZ_LOG(tracer) << "two";
  GRPC_CHANNELZ_LOG(tracer) << "three";
  GRPC_CHANNELZ_LOG(tracer) << "four";
  {
    upb_Arena* arena = upb_Arena_New();
    grpc_channelz_v2_Entity* entity = grpc_channelz_v2_Entity_new(arena);
    tracer.Render(entity, arena);
    EXPECT_THAT(
        GetTraceEvents(entity),
        ::testing::ElementsAre(IsTraceEvent("one"), IsTraceEvent("two"),
                               IsTraceEvent("three"), IsTraceEvent("four")));
    upb_Arena_Free(arena);
  }
  GRPC_CHANNELZ_LOG(tracer) << "five";
  GRPC_CHANNELZ_LOG(tracer) << "six";
  {
    upb_Arena* arena = upb_Arena_New();
    grpc_channelz_v2_Entity* entity = grpc_channelz_v2_Entity_new(arena);
    tracer.Render(entity, arena);
    EXPECT_THAT(
        GetTraceEvents(entity),
        ::testing::ElementsAre(IsTraceEvent("one"), IsTraceEvent("two"),
                               IsTraceEvent("three"), IsTraceEvent("four"),
                               IsTraceEvent("five"), IsTraceEvent("six")));
    upb_Arena_Free(arena);
  }
}

TEST(ChannelTracerTest, TestSmallMemoryLimitProto) {
  // Set a very small memory limit for the trace.
  const int kSmallMemoryLimit = 1;
  ChannelTrace tracer(kSmallMemoryLimit);
  const size_t kNumEvents = 4;
  // Add a few trace events. These should be immediately garbage collected
  // from the event list due to the small memory limit.
  for (size_t i = 0; i < kNumEvents; ++i) {
    tracer.NewNode("trace").Commit();
  }
  upb_Arena* arena = upb_Arena_New();
  grpc_channelz_v2_Entity* entity = grpc_channelz_v2_Entity_new(arena);
  // Render the trace to the proto.
  tracer.Render(entity, arena);
  size_t size;
  grpc_channelz_v2_Entity_trace(entity, &size);
  ASSERT_EQ(size, 0);
  upb_Arena_Free(arena);
}

// Tests that the code is thread-safe.
TEST(ChannelTracerTest, ThreadSafety) {
  ChannelTrace tracer(kEventListMemoryLimit);
  absl::Notification done;
  std::vector<std::unique_ptr<std::thread>> threads;
  for (size_t i = 0; i < 10; ++i) {
    threads.push_back(std::make_unique<std::thread>([&]() {
      do {
        for (int i = 0; i < 100; i++) {
          if (done.HasBeenNotified()) return;
          tracer.NewNode("trace").Commit();
        }
        absl::SleepFor(absl::Milliseconds(1));
      } while (!done.HasBeenNotified());
    }));
  }
  for (size_t i = 0; i < 10; ++i) {
    absl::SleepFor(absl::Milliseconds(1));
    upb_Arena* arena = upb_Arena_New();
    grpc_channelz_v2_Entity* entity = grpc_channelz_v2_Entity_new(arena);
    tracer.Render(entity, arena);
    upb_Arena_Free(arena);
  }
  done.Notify();
  for (const auto& thd : threads) {
    thd->join();
  }
}

}  // namespace testing
}  // namespace channelz
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
