/*
 *
 * Copyright 2015 gRPC authors.
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

/* Test of gpr thread local storage support. */

#include "src/core/lib/gpr/tls.h"

#include <array>

#include <gtest/gtest.h>

#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/test_config.h"

struct BiggerThanMachineWord {
  size_t a, b;
  uint8_t c;
};

static GPR_THREAD_LOCAL(BiggerThanMachineWord) test_var;
// Fails to compile: static GPR_THREAD_LOCAL(std::unique_ptr<char>) non_trivial;

namespace {
void thd_body(void*) {
  for (size_t i = 0; i < 100000; i++) {
    BiggerThanMachineWord next = {i, i, uint8_t(i)};
    test_var = next;
    BiggerThanMachineWord read = test_var;
    ASSERT_EQ(read.a, i);
    ASSERT_EQ(read.b, i);
    ASSERT_EQ(read.c, uint8_t(i)) << i;
  }
}

TEST(ThreadLocal, ReadWrite) {
  std::array<grpc_core::Thread, 100> threads;
  for (grpc_core::Thread& th : threads) {
    th = grpc_core::Thread("grpc_tls_test", thd_body, nullptr);
    th.Start();
  }
  for (grpc_core::Thread& th : threads) {
    th.Join();
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
