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

#include "src/core/lib/iomgr/port.h"

#include "src/core/lib/iomgr/buffer_list.h"

#include <grpc/grpc.h>

#include "test/core/util/test_config.h"

#ifdef GRPC_LINUX_ERRQUEUE

static void TestShutdownFlushesListVerifier(void* arg,
                                            grpc_core::Timestamps* ts,
                                            grpc_error* error) {
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(arg != nullptr);
  gpr_atm* done = reinterpret_cast<gpr_atm*>(arg);
  gpr_atm_rel_store(done, static_cast<gpr_atm>(1));
}

/** Tests that all TracedBuffer elements in the list are flushed out on
 * shutdown.
 * Also tests that arg is passed correctly.
 */
static void TestShutdownFlushesList() {
  grpc_core::grpc_tcp_set_write_timestamps_callback(
      TestShutdownFlushesListVerifier);
  grpc_core::TracedBuffer* list = nullptr;
#define NUM_ELEM 5
  gpr_atm verifier_called[NUM_ELEM];
  for (auto i = 0; i < NUM_ELEM; i++) {
    gpr_atm_rel_store(&verifier_called[i], static_cast<gpr_atm>(0));
    grpc_core::TracedBuffer::AddNewEntry(
        &list, i, static_cast<void*>(&verifier_called[i]));
  }
  grpc_core::TracedBuffer::Shutdown(&list, GRPC_ERROR_NONE);
  GPR_ASSERT(list == nullptr);
  for (auto i = 0; i < NUM_ELEM; i++) {
    GPR_ASSERT(gpr_atm_acq_load(&verifier_called[i]) ==
               static_cast<gpr_atm>(1));
  }
}

static void TestVerifierCalledOnAckVerifier(void* arg,
                                            grpc_core::Timestamps* ts,
                                            grpc_error* error) {
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(arg != nullptr);
  GPR_ASSERT(ts->acked_time.clock_type == GPR_CLOCK_REALTIME);
  GPR_ASSERT(ts->acked_time.tv_sec == 123);
  GPR_ASSERT(ts->acked_time.tv_nsec == 456);
  gpr_atm* done = reinterpret_cast<gpr_atm*>(arg);
  gpr_atm_rel_store(done, static_cast<gpr_atm>(1));
}

/** Tests that the timestamp verifier is called on an ACK timestamp.
 */
static void TestVerifierCalledOnAck() {
  struct sock_extended_err serr;
  serr.ee_data = 213;
  serr.ee_info = SCM_TSTAMP_ACK;
  struct grpc_core::scm_timestamping tss;
  tss.ts[0].tv_sec = 123;
  tss.ts[0].tv_nsec = 456;
  grpc_core::grpc_tcp_set_write_timestamps_callback(
      TestVerifierCalledOnAckVerifier);
  grpc_core::TracedBuffer* list = nullptr;
  gpr_atm verifier_called;
  gpr_atm_rel_store(&verifier_called, static_cast<gpr_atm>(0));
  grpc_core::TracedBuffer::AddNewEntry(&list, 213, &verifier_called);
  grpc_core::TracedBuffer::ProcessTimestamp(&list, &serr, &tss);
  GPR_ASSERT(gpr_atm_acq_load(&verifier_called) == static_cast<gpr_atm>(1));
  GPR_ASSERT(list == nullptr);
  grpc_core::TracedBuffer::Shutdown(&list, GRPC_ERROR_NONE);
}

static void TestTcpBufferList() {
  TestVerifierCalledOnAck();
  TestShutdownFlushesList();
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  TestTcpBufferList();
  grpc_shutdown();
  return 0;
}

#else /* GRPC_LINUX_ERRQUEUE */

int main(int argc, char** argv) { return 0; }

#endif /* GRPC_LINUX_ERRQUEUE */
