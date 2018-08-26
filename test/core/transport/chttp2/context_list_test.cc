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

#include "src/core/ext/transport/chttp2/transport/context_list.h"

#include <grpc/grpc.h>

#include "test/core/util/test_config.h"

static void TestExecuteFlushesListVerifier(void* arg,
                                           grpc_core::Timestamps* ts) {
  GPR_ASSERT(arg != nullptr);
  gpr_atm* done = reinterpret_cast<gpr_atm*>(arg);
  gpr_atm_rel_store(done, static_cast<gpr_atm>(1));
}

/** Tests that all ContextList elements in the list are flushed out on
 * execute.
 * Also tests that arg is passed correctly.
 */
static void TestExecuteFlushesList() {
  grpc_core::ContextList* list = nullptr;
  grpc_http2_set_write_timestamps_callback(TestExecuteFlushesListVerifier);
#define NUM_ELEM 5
  grpc_chttp2_stream s[NUM_ELEM];
  gpr_atm verifier_called[NUM_ELEM];
  for (auto i = 0; i < NUM_ELEM; i++) {
    s[i].context = &verifier_called[i];
    gpr_atm_rel_store(&verifier_called[i], static_cast<gpr_atm>(0));
    grpc_core::ContextList::Append(&list, &s[i]);
  }
  grpc_core::Timestamps ts;
  grpc_core::ContextList::Execute(list, &ts, GRPC_ERROR_NONE);
  for (auto i = 0; i < NUM_ELEM; i++) {
    GPR_ASSERT(gpr_atm_acq_load(&verifier_called[i]) ==
               static_cast<gpr_atm>(1));
  }
}

static void TestContextList() { TestExecuteFlushesList(); }

int main(int argc, char** argv) {
  grpc_init();
  TestContextList();
  grpc_shutdown();
  return 0;
}
