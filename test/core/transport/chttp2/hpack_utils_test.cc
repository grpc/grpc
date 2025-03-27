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

#include <random>
#include <unordered_map>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_index.h"

namespace grpc_core {
namespace testing {

static void VerifyAsciiHeaderSize(const char* key, const char* value,
                                  bool intern_key, bool intern_value) {
  grpc_mdelem elem = grpc_mdelem_from_slices(
      maybe_intern(grpc_slice_from_static_string(key), intern_key),
      maybe_intern(grpc_slice_from_static_string(value), intern_value));
  size_t elem_size = MetadataSizeInHPackTable(elem, false);
  size_t expected_size = 32 + strlen(key) + strlen(value);
  CHECK(expected_size == elem_size);
  GRPC_MDELEM_UNREF(elem);
}

static void VerifyBinaryHeaderSize(const char* key, const uint8_t* value,
                                   size_t value_len, bool intern_key,
                                   bool intern_value) {
  grpc_mdelem elem = grpc_mdelem_from_slices(
      maybe_intern(grpc_slice_from_static_string(key), intern_key),
      maybe_intern(grpc_slice_from_static_buffer(value, value_len),
                   intern_value));
  CHECK(grpc_is_binary_header(GRPC_MDKEY(elem)));
  size_t elem_size = MetadataSizeInHPackTable(elem, false);
  grpc_slice value_slice = grpc_slice_from_copied_buffer(
      reinterpret_cast<const char*>(value), value_len);
  grpc_slice base64_encoded = grpc_chttp2_base64_encode(value_slice);
  size_t expected_size = 32 + strlen(key) + GRPC_SLICE_LENGTH(base64_encoded);
  CHECK(expected_size == elem_size);
  grpc_slice_unref(value_slice);
  grpc_slice_unref(base64_encoded);
  GRPC_MDELEM_UNREF(elem);
}

struct Param {
  bool intern_key;
  bool intern_value;
}

class MetadataTest : public ::testing::TestWithParam<Param> {
};

#define BUFFER_SIZE 64
TEST_P(MetadataTest, MetadataSize) {
  const bool intern_key = GetParam().intern_key;
  const bool intern_value = GetParam().intern_value;
  LOG(INFO) << "test_mdelem_size: intern_key=" << intern_key
            << " intern_value=" << intern_value;
  grpc_init();
  ExecCtx exec_ctx;

  uint8_t binary_value[BUFFER_SIZE] = {0};
  for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
    binary_value[i] = i;
  }

  verify_ascii_header_size("hello", "world", intern_key, intern_value);
  verify_ascii_header_size("hello", "worldxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                           intern_key, intern_value);
  verify_ascii_header_size(":scheme", "http", intern_key, intern_value);

  for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
    verify_binary_header_size("hello-bin", binary_value, i, intern_key,
                              intern_value);
  }

  grpc_shutdown();
}

INSTANTIATE_TEST_SUITE_P(MetadataTestSuite, MetadataTest,
                         ::testing::Values(Param{false, false},
                                           Param{false, true},
                                           Param{true, false},
                                           Param{true, true}));

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
