/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/lib/iomgr/buffer_list.h"

#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "src/core/lib/iomgr/port.h"
#include "test/core/util/test_config.h"

#ifdef GRPC_LINUX_ERRQUEUE

static void TestShutdownFlushesListVerifier(void* arg,
                                            grpc_core::Timestamps* /*ts*/,
                                            grpc_error_handle error) {
  ASSERT_TRUE(GRPC_ERROR_IS_NONE(error));
  ASSERT_NE(arg, nullptr);
  gpr_atm* done = reinterpret_cast<gpr_atm*>(arg);
  gpr_atm_rel_store(done, static_cast<gpr_atm>(1));
}

/** Tests that all TracedBuffer elements in the list are flushed out on
 * shutdown.
 * Also tests that arg is passed correctly.
 */
TEST(BufferListTest, Testshutdownflusheslist) {
  grpc_core::grpc_tcp_set_write_timestamps_callback(
      TestShutdownFlushesListVerifier);
  grpc_core::TracedBuffer* list = nullptr;
#define NUM_ELEM 5
  gpr_atm verifier_called[NUM_ELEM];
  for (auto i = 0; i < NUM_ELEM; i++) {
    gpr_atm_rel_store(&verifier_called[i], static_cast<gpr_atm>(0));
    grpc_core::TracedBuffer::AddNewEntry(
        &list, i, 0, static_cast<void*>(&verifier_called[i]));
  }
  grpc_core::TracedBuffer::Shutdown(&list, nullptr, GRPC_ERROR_NONE);
  ASSERT_EQ(list, nullptr);
  for (auto i = 0; i < NUM_ELEM; i++) {
    ASSERT_EQ(gpr_atm_acq_load(&verifier_called[i]), static_cast<gpr_atm>(1));
  }
}

static void TestVerifierCalledOnAckVerifier(void* arg,
                                            grpc_core::Timestamps* ts,
                                            grpc_error_handle error) {
  ASSERT_TRUE(GRPC_ERROR_IS_NONE(error));
  ASSERT_NE(arg, nullptr);
  ASSERT_EQ(ts->acked_time.time.clock_type, GPR_CLOCK_REALTIME);
  ASSERT_EQ(ts->acked_time.time.tv_sec, 123);
  ASSERT_EQ(ts->acked_time.time.tv_nsec, 456);
  ASSERT_GT(ts->info.length, 0);
  gpr_atm* done = reinterpret_cast<gpr_atm*>(arg);
  gpr_atm_rel_store(done, static_cast<gpr_atm>(1));
}

/** Tests that the timestamp verifier is called on an ACK timestamp.
 */
TEST(BufferListTest, Testverifiercalledonack) {
  struct sock_extended_err serr;
  serr.ee_data = 213;
  serr.ee_info = grpc_core::SCM_TSTAMP_ACK;
  struct grpc_core::scm_timestamping tss;
  tss.ts[0].tv_sec = 123;
  tss.ts[0].tv_nsec = 456;
  grpc_core::grpc_tcp_set_write_timestamps_callback(
      TestVerifierCalledOnAckVerifier);
  grpc_core::TracedBuffer* list = nullptr;
  gpr_atm verifier_called;
  gpr_atm_rel_store(&verifier_called, static_cast<gpr_atm>(0));
  grpc_core::TracedBuffer::AddNewEntry(&list, 213, 0, &verifier_called);
  grpc_core::TracedBuffer::ProcessTimestamp(&list, &serr, nullptr, &tss);
  ASSERT_EQ(gpr_atm_acq_load(&verifier_called), static_cast<gpr_atm>(1));
  ASSERT_EQ(list, nullptr);
  grpc_core::TracedBuffer::Shutdown(&list, nullptr, GRPC_ERROR_NONE);
}

/** Tests that shutdown can be called repeatedly.
 */
TEST(BufferListTest, Testrepeatedshutdown) {
  struct sock_extended_err serr;
  serr.ee_data = 213;
  serr.ee_info = grpc_core::SCM_TSTAMP_ACK;
  struct grpc_core::scm_timestamping tss;
  tss.ts[0].tv_sec = 123;
  tss.ts[0].tv_nsec = 456;
  grpc_core::grpc_tcp_set_write_timestamps_callback(
      TestVerifierCalledOnAckVerifier);
  grpc_core::TracedBuffer* list = nullptr;
  gpr_atm verifier_called;
  gpr_atm_rel_store(&verifier_called, static_cast<gpr_atm>(0));
  grpc_core::TracedBuffer::AddNewEntry(&list, 213, 0, &verifier_called);
  grpc_core::TracedBuffer::ProcessTimestamp(&list, &serr, nullptr, &tss);
  ASSERT_EQ(gpr_atm_acq_load(&verifier_called), static_cast<gpr_atm>(1));
  ASSERT_EQ(list, nullptr);
  grpc_core::TracedBuffer::Shutdown(&list, nullptr, GRPC_ERROR_NONE);
  grpc_core::TracedBuffer::Shutdown(&list, nullptr, GRPC_ERROR_NONE);
  grpc_core::TracedBuffer::Shutdown(&list, nullptr, GRPC_ERROR_NONE);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}

#else /* GRPC_LINUX_ERRQUEUE */

int main(int /*argc*/, char** /*argv*/) { return 0; }

#endif /* GRPC_LINUX_ERRQUEUE */
