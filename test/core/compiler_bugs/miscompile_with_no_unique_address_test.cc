// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "gtest/gtest.h"

// Make a template argument to test which bit pattern remains in A's destructor
// to try and detect similar bugs in non-MSAN builds (none have been detected
// yet thankfully)
template <int kInit>
class A {
 public:
  ~A() { EXPECT_EQ(a_, kInit); }
  int a_ = kInit;
};
template <class T, int kInit>
class P : A<kInit> {
 public:
  explicit P(T b) : b_(b) {}
  // clang 11 with MSAN miscompiles this and marks A::a_ as uninitialized during
  // P::~P() if GPR_NO_UNIQUE_ADDRESS is [[no_unique_address]] - so this test
  // stands to ensure that we have a working definition for this compiler so
  // that we don't flag false negatives elsewhere in the codebase.
  GPR_NO_UNIQUE_ADDRESS T b_;
};

template <int kInit, class T>
void c(T a) {
  P<T, kInit> _(a);
}

TEST(Miscompile, Zero) {
  c<0>([] {});
}

TEST(Miscompile, One) {
  c<1>([] {});
}

TEST(Miscompile, MinusOne) {
  c<-1>([] {});
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
