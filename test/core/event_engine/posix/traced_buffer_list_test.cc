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

#include <atomic>

#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "src/core/lib/iomgr/port.h"
#include "test/core/util/test_config.h"

#ifdef GRPC_LINUX_ERRQUEUE

#define NUM_ELEM 5

namespace grpc_event_engine {
namespace posix_engine {
namespace {

void TestShutdownFlushesListVerifier(void* arg, Timestamps* /*ts*/,
                                     absl::Status status) {
  ASSERT_TRUE(status.ok());
  ASSERT_NE(arg, nullptr);
  int* done = reinterpret_cast<int*>(arg);
  *done = 1;
}

void TestVerifierCalledOnAckVerifier(void* arg, Timestamps* ts,
                                     absl::Status status) {
  ASSERT_TRUE(status.ok());
  ASSERT_NE(arg, nullptr);
  ASSERT_EQ(ts->acked_time.time.clock_type, GPR_CLOCK_REALTIME);
  ASSERT_EQ(ts->acked_time.time.tv_sec, 123);
  ASSERT_EQ(ts->acked_time.time.tv_nsec, 456);
  ASSERT_GT(ts->info.length, 0);
  int* done = reinterpret_cast<int*>(arg);
  *done = 1;
}

}  // namespace

// Tests that all TracedBuffer elements in the list are flushed out on shutdown.
// Also tests that arg is passed correctly.
TEST(BufferListTest, TestShutdownFlushesList) {
  TcpSetWriteTimestampsCallback(TestShutdownFlushesListVerifier);
  TracedBufferList traced_buffers;
  int verifier_called[NUM_ELEM];
  for (auto i = 0; i < NUM_ELEM; i++) {
    verifier_called[i] = 0;
    traced_buffers.AddNewEntry(i, 0, static_cast<void*>(&verifier_called[i]));
  }
  traced_buffers.Shutdown(nullptr, absl::OkStatus());
  for (auto i = 0; i < NUM_ELEM; i++) {
    ASSERT_EQ(verifier_called[i], 1);
  }
  ASSERT_TRUE(traced_buffers.Size() == 0);
}

// Tests that the timestamp verifier is called on an ACK timestamp.
TEST(BufferListTest, TestVerifierCalledOnAck) {
  struct sock_extended_err serr;
  serr.ee_data = 213;
  serr.ee_info = SCM_TSTAMP_ACK;
  struct scm_timestamping tss;
  tss.ts[0].tv_sec = 123;
  tss.ts[0].tv_nsec = 456;
  TcpSetWriteTimestampsCallback(TestVerifierCalledOnAckVerifier);
  TracedBufferList traced_buffers;
  int verifier_called = 0;
  traced_buffers.AddNewEntry(213, 0, &verifier_called);
  traced_buffers.ProcessTimestamp(&serr, nullptr, &tss);
  ASSERT_EQ(verifier_called, 1);
  ASSERT_TRUE(traced_buffers.Size() == 0);
  traced_buffers.Shutdown(nullptr, absl::OkStatus());
  ASSERT_TRUE(traced_buffers.Size() == 0);
}

// Tests that ProcessTimestamp called after Shutdown does nothing.
TEST(BufferListTest, TestProcessTimestampAfterShutdown) {
  struct sock_extended_err serr;
  serr.ee_data = 213;
  serr.ee_info = SCM_TSTAMP_ACK;
  struct scm_timestamping tss;
  tss.ts[0].tv_sec = 123;
  tss.ts[0].tv_nsec = 456;
  TcpSetWriteTimestampsCallback(TestShutdownFlushesListVerifier);
  TracedBufferList traced_buffers;
  int verifier_called = 0;

  traced_buffers.AddNewEntry(213, 0, &verifier_called);
  ASSERT_TRUE(traced_buffers.Size() == 1);
  traced_buffers.Shutdown(nullptr, absl::OkStatus());
  ASSERT_TRUE(traced_buffers.Size() == 0);
  // Check that the callback was executed after first Shutdown.
  ASSERT_EQ(verifier_called, 1);
  verifier_called = 0;
  traced_buffers.Shutdown(nullptr, absl::OkStatus());
  ASSERT_TRUE(traced_buffers.Size() == 0);
  // Second Shutdown should not execute the callback.
  ASSERT_EQ(verifier_called, 0);
  traced_buffers.ProcessTimestamp(&serr, nullptr, &tss);
  // A ProcessTimestamp after Shutdown should not execute the callback.
  ASSERT_EQ(verifier_called, 0);
}

}  // namespace posix_engine
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#else /* GRPC_LINUX_ERRQUEUE */

int main(int /*argc*/, char** /*argv*/) { return 0; }

#endif /* GRPC_LINUX_ERRQUEUE */
