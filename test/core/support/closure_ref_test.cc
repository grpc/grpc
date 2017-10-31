/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/support/closure_ref.h"
#include <gtest/gtest.h>
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

static int counter;

class Incrementer {
 public:
  Incrementer(int* p) : p_(p) {}
  void Inc() { ++*p_; }

 private:
  int* const p_;
};

void IncCounter() { ++counter; }
void IncCounterBy(int n) { counter += n; }

TEST(ClosureRef, SimpleTests) {
  // simple closures around functions, member functions
  counter = 0;
  ClosureRef<> cb1 =
      AcquiresNoLocks::MakeClosureWithArgs<>::FromFreeFunction<IncCounter>();
  ClosureRef<int> cb2 = AcquiresNoLocks::MakeClosureWithArgs<
      int>::FromFreeFunction<IncCounterBy>();
  Incrementer incrementer(&counter);
  ClosureRef<> cb3 = AcquiresNoLocks::MakeClosureWithArgs<>::FromMemberFunction<
      Incrementer, &Incrementer::Inc>(&incrementer);

  cb1.UnsafeRun();
  cb2.UnsafeRun(2);
  cb3.UnsafeRun();

  EXPECT_EQ(4, counter);

  // empty closure test
  ClosureRef<> empty;
  EXPECT_DEATH_IF_SUPPORTED(empty.UnsafeRun(), "");
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
