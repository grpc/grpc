/*
 *
 * Copyright 2021 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/hpack_encoder_index.h"

#include <random>
#include <unordered_map>

#include <gtest/gtest.h>

namespace grpc_core {
namespace testing {

struct TestKey {
  using Stored = uint32_t;
  uint32_t value;
  uint32_t stored() const { return value; }
  uint32_t hash() const { return value; }
  bool operator==(uint32_t other) const { return other == value; }
};

TEST(HPackEncoderIndexTest, SetAndGet) {
  HPackEncoderIndex<TestKey, 64> index;
  std::default_random_engine rng;
  std::unordered_map<uint32_t, uint32_t> last_index;
  for (uint32_t i = 0; i < 10000; i++) {
    uint32_t key = rng();
    index.Insert({key}, i);
    EXPECT_EQ(index.Lookup({key}), i);
    last_index[key] = i;
  }
  for (auto p : last_index) {
    auto r = index.Lookup({p.first});
    if (r.has_value()) {
      EXPECT_EQ(*r, p.second);
    }
  }
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
