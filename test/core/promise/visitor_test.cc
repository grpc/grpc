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

#include "src/core/lib/promise/visitor.h"
#include <gtest/gtest.h>

namespace grpc_core {

TEST(VisitorTest, Visit1) {
  EXPECT_EQ(Visitor([](int) { return 1; },
                    [](double) { return 2; })(absl::variant<int, double>(1))(),
            Poll<int>(1));
}

TEST(VisitorTest, Visit2) {
  EXPECT_EQ(Visitor([](int) { return 1; }, [](double) { return 2; })(
                absl::variant<int, double>(1.0))(),
            Poll<int>(2));
}

TEST(VisitorTest, VisitFactory1) {
  EXPECT_EQ(Visitor([](int) { return []() { return 1; }; },
                    [](double) { return []() { return 2; }; })(
                absl::variant<int, double>(1))(),
            Poll<int>(1));
}

TEST(VisitorTest, VisitFactory2) {
  EXPECT_EQ(Visitor([](int) { return []() { return 1; }; },
                    [](double) { return []() { return 2; }; })(
                absl::variant<int, double>(1.0))(),
            Poll<int>(2));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
