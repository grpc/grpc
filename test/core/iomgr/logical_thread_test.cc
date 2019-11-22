/*
 *
 * Copyright 2019 gRPC authors.
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

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/logical_thread.h"
#include "test/core/util/test_config.h"

namespace {
TEST(LogicalThreadTest, NoOp) {
  auto lock = grpc_core::MakeRefCounted<grpc_core::LogicalThread>();
}

TEST(LogicalThreadTest, ExecuteOne) {
  auto lock = grpc_core::MakeRefCounted<grpc_core::LogicalThread>();
  gpr_event done;
  gpr_event_init(&done);
  lock->Run([&done]() { gpr_event_set(&done, (void*)1); }, DEBUG_LOCATION);
  GPR_ASSERT(gpr_event_wait(&done, grpc_timeout_seconds_to_deadline(5)) !=
             nullptr);
}

typedef struct {
  size_t ctr;
  grpc_core::RefCountedPtr<grpc_core::LogicalThread> lock;
  gpr_event done;
} thd_args;

typedef struct {
  size_t* ctr;
  size_t value;
} ex_args;

void execute_many_loop(void* a) {
  thd_args* args = static_cast<thd_args*>(a);
  size_t n = 1;
  for (size_t i = 0; i < 10; i++) {
    for (size_t j = 0; j < 10000; j++) {
      ex_args* c = static_cast<ex_args*>(gpr_malloc(sizeof(*c)));
      c->ctr = &args->ctr;
      c->value = n++;
      args->lock->Run(
          [c]() {
            GPR_ASSERT(*c->ctr == c->value - 1);
            *c->ctr = c->value;
            gpr_free(c);
          },
          DEBUG_LOCATION);
    }
    // sleep for a little bit, to test other threads picking up the load
    gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  }
  args->lock->Run([args]() { gpr_event_set(&args->done, (void*)1); },
                  DEBUG_LOCATION);
}

TEST(LogicalThreadTest, ExecuteMany) {
  auto lock = grpc_core::MakeRefCounted<grpc_core::LogicalThread>();
  grpc_core::Thread thds[100];
  thd_args ta[GPR_ARRAY_SIZE(thds)];
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    ta[i].ctr = 0;
    ta[i].lock = lock;
    gpr_event_init(&ta[i].done);
    thds[i] = grpc_core::Thread("grpc_execute_many", execute_many_loop, &ta[i]);
    thds[i].Start();
  }
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    GPR_ASSERT(gpr_event_wait(&ta[i].done,
                              gpr_inf_future(GPR_CLOCK_REALTIME)) != nullptr);
    thds[i].Join();
  }
}
}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int retval = RUN_ALL_TESTS();
  grpc_shutdown();
  return retval;
}
