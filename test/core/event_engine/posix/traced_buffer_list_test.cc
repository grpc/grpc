// Copyright 2022 The gRPC Authors
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

#include "src/core/lib/event_engine/posix_engine/traced_buffer_list.h"

#include <grpc/support/atm.h>
#include <grpc/support/time.h>
#include <linux/errqueue.h>
#include <time.h>

#include <memory>
#include <thread>

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/time.h"
#include "src/core/util/useful.h"

#ifdef GRPC_LINUX_ERRQUEUE

#define NUM_ELEM 5

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

namespace grpc_event_engine {
namespace experimental {
namespace {

using WriteEvent = EventEngine::Endpoint::WriteEvent;
using WriteEventSink = EventEngine::Endpoint::WriteEventSink;
using WriteMetric = EventEngine::Endpoint::WriteMetric;
using ::testing::Eq;

// Tests that remaining WriteEventSink is handled on shutdown.
TEST(BufferListTest, TestShutdownFlushesAll) {
  TracedBufferList traced_buffers;
  int count = 0;
  constexpr int kNumIterations = 10;
  for (int i = 0; i < kNumIterations; ++i) {
    traced_buffers.AddNewEntry(
        /*seq_no=*/i, /*posix_interface=*/nullptr, /*fd=*/FileDescriptor(),
        WriteEventSink(nullptr, {WriteEvent::kClosed},
                       [&count](WriteEvent event, absl::Time /*time*/,
                                std::vector<WriteMetric> /*metrics*/) {
                         EXPECT_EQ(event, WriteEvent::kClosed);
                         ++count;
                       }));
  }
  traced_buffers.Shutdown(std::nullopt);
  EXPECT_EQ(count, kNumIterations);
  EXPECT_THAT(traced_buffers.Size(), Eq(0));
}

// Tests that remaining WriteEventSink is handled on shutdown.
TEST(BufferListTest, TestShutdownFlushesRemaining) {
  TracedBufferList traced_buffers;
  bool invoked;
  WriteEventSink sink(nullptr, {WriteEvent::kClosed},
                      [&invoked](WriteEvent event, absl::Time /*time*/,
                                 std::vector<WriteMetric> /*metrics*/) {
                        EXPECT_EQ(event, WriteEvent::kClosed);
                        invoked = true;
                      });
  traced_buffers.Shutdown(std::move(sink));
  EXPECT_TRUE(invoked);
  EXPECT_THAT(traced_buffers.Size(), Eq(0));
}

// Tests that adding a new entry invokes the SendMsg Event
TEST(BufferListTest, AddingNewEntryInvokesSendMsgEvent) {
  TracedBufferList traced_buffers;
  int count = 0;
  constexpr int kNumIterations = 10;
  for (int i = 0; i < kNumIterations; ++i) {
    traced_buffers.AddNewEntry(
        /*seq_no=*/i, /*posix_interface=*/nullptr, /*fd=*/FileDescriptor(),
        WriteEventSink(nullptr, {WriteEvent::kSendMsg},
                       [&count](WriteEvent event, absl::Time /*time*/,
                                std::vector<WriteMetric> /*metrics*/) {
                         EXPECT_EQ(event, WriteEvent::kSendMsg);
                         ++count;
                       }));
  }
  EXPECT_EQ(count, kNumIterations);
  EXPECT_THAT(traced_buffers.Size(), Eq(10));
}

// Tests that processing a timestamp with SCM_TSTAMP_SCHED invokes kScheduled
// event.
TEST(BufferListTest, ProcessTimestampScheduled) {
  TracedBufferList traced_buffers;
  int count = 0;
  constexpr int kNumIterations = 10;
  for (int i = 0; i < kNumIterations; ++i) {
    traced_buffers.AddNewEntry(
        /*seq_no=*/i, /*posix_interface=*/nullptr, /*fd=*/FileDescriptor(),
        WriteEventSink(nullptr, {WriteEvent::kScheduled},
                       [&count](WriteEvent event, absl::Time /*time*/,
                                std::vector<WriteMetric> /*metrics*/) {
                         EXPECT_EQ(event, WriteEvent::kScheduled);
                         ++count;
                       }));
  }
  EXPECT_THAT(traced_buffers.Size(), Eq(10));
  struct sock_extended_err serr;
  serr.ee_data = 10;
  serr.ee_info = SCM_TSTAMP_SCHED;
  struct scm_timestamping tss;
  traced_buffers.ProcessTimestamp(&serr, nullptr, &tss);
  EXPECT_EQ(count, 10);
  EXPECT_THAT(traced_buffers.Size(), Eq(10));
}

// Tests that processing a timestamp with SCM_TSTAMP_SND invokes kSent event.
TEST(BufferListTest, ProcessTimestampSent) {
  TracedBufferList traced_buffers;
  int count = 0;
  constexpr int kNumIterations = 10;
  for (int i = 0; i < kNumIterations; ++i) {
    traced_buffers.AddNewEntry(
        /*seq_no=*/i, /*posix_interface=*/nullptr, /*fd=*/FileDescriptor(),
        WriteEventSink(nullptr, {WriteEvent::kSent},
                       [&count](WriteEvent event, absl::Time /*time*/,
                                std::vector<WriteMetric> /*metrics*/) {
                         EXPECT_EQ(event, WriteEvent::kSent);
                         ++count;
                       }));
  }
  EXPECT_THAT(traced_buffers.Size(), Eq(10));
  struct sock_extended_err serr;
  serr.ee_data = 10;
  serr.ee_info = SCM_TSTAMP_SND;
  struct scm_timestamping tss;
  traced_buffers.ProcessTimestamp(&serr, nullptr, &tss);
  EXPECT_EQ(count, 10);
  EXPECT_THAT(traced_buffers.Size(), Eq(10));
}

// Tests that processing a timestamp with SCM_TSTAMP_ACK invokes kAcked event.
TEST(BufferListTest, ProcessTimestampsAcked) {
  TracedBufferList traced_buffers;
  int count = 0;
  constexpr int kNumIterations = 10;
  for (int i = 0; i < kNumIterations; ++i) {
    traced_buffers.AddNewEntry(
        /*seq_no=*/i, /*posix_interface=*/nullptr, /*fd=*/FileDescriptor(),
        WriteEventSink(nullptr, {WriteEvent::kAcked},
                       [&count](WriteEvent event, absl::Time /*time*/,
                                std::vector<WriteMetric> /*metrics*/) {
                         EXPECT_EQ(event, WriteEvent::kAcked);
                         ++count;
                       }));
  }
  EXPECT_THAT(traced_buffers.Size(), Eq(10));
  struct sock_extended_err serr;
  serr.ee_data = 10;
  serr.ee_info = SCM_TSTAMP_ACK;
  struct scm_timestamping tss;
  traced_buffers.ProcessTimestamp(&serr, nullptr, &tss);
  EXPECT_EQ(count, 10);
  EXPECT_THAT(traced_buffers.Size(), Eq(0));
}

// Tests that traced buffers that don't see updates get timed out.
TEST(BufferListTest, TimedOut) {
  TracedBufferList::TestOnlySetMaxPendingAckTime(grpc_core::Duration::Zero());
  TracedBufferList traced_buffers;
  constexpr int kNumIterations = 10;
  for (int i = 0; i < kNumIterations; ++i) {
    traced_buffers.AddNewEntry(
        /*seq_no=*/i + 1, /*posix_interface=*/nullptr, /*fd=*/FileDescriptor(),
        WriteEventSink(nullptr, {WriteEvent::kAcked},
                       [](WriteEvent /*event*/, absl::Time /*time*/,
                          std::vector<WriteMetric> /*metrics*/) {
                         // should not reach here
                         ASSERT_FALSE(true);
                       }));
  }
  EXPECT_THAT(traced_buffers.Size(), Eq(10));
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  struct sock_extended_err serr;
  serr.ee_data = 0;
  serr.ee_info = SCM_TSTAMP_SND;
  struct scm_timestamping tss;
  traced_buffers.ProcessTimestamp(&serr, nullptr, &tss);
  // All buffers should be deleted
  EXPECT_THAT(traced_buffers.Size(), Eq(0));
  TracedBufferList::TestOnlySetMaxPendingAckTime(
      grpc_core::Duration::Seconds(10));
}

}  // namespace

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#else  // GRPC_LINUX_ERRQUEUE

int main(int /*argc*/, char** /*argv*/) { return 0; }

#endif  // GRPC_LINUX_ERRQUEUE
