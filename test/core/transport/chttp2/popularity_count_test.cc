/*
 *
 * Copyright 2020 gRPC authors.
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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/popularity_count.h"

#include <array>

#include <gtest/gtest.h>

namespace grpc_core {
namespace testing {

static constexpr uint8_t kTestSize = 4;

struct Scenario {
  std::array<uint8_t, kTestSize> initial_values;
  uint8_t final_add;
  bool expectation;
};

std::ostream& operator<<(std::ostream& out, Scenario s) {
  out << "init:";
  for (size_t i = 0; i < kTestSize; i++) {
    if (i != 0) {
      out << ",";
    }
    out << static_cast<int>(s.initial_values[i]);
  }
  out << " final:" << static_cast<int>(s.final_add);
  out << " expect:" << (s.expectation ? "true" : "false");
  return out;
}

struct PopularityCountTest : public ::testing::TestWithParam<Scenario> {};

TEST_P(PopularityCountTest, Test) {
  Scenario s = GetParam();
  PopularityCount<kTestSize> pop;
  for (size_t i = 0; i < kTestSize; i++) {
    for (size_t j = 0; j < s.initial_values[i]; j++) {
      pop.AddElement(i);
    }
  }
  EXPECT_EQ(pop.AddElement(s.final_add), s.expectation);
}

INSTANTIATE_TEST_SUITE_P(InterestingTests, PopularityCountTest,
                         ::testing::Values(Scenario{{0, 0, 0, 0}, 0, true},
                                           Scenario{{64, 0, 0, 0}, 0, true},
                                           Scenario{{64, 0, 0, 0}, 1, false}));

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
