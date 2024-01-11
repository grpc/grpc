//
//
// Copyright 2015 gRPC authors.
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
//
//

#include "src/core/ext/transport/chttp2/transport/varint.h"

#include <memory>

#include "gtest/gtest.h"

#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

template <uint8_t kPrefixBits>
static void test_varint(uint32_t value, uint8_t prefix_or,
                        const char* expect_bytes, size_t expect_length) {
  grpc_core::VarintWriter<kPrefixBits> w(value);
  grpc_slice expect =
      grpc_slice_from_copied_buffer(expect_bytes, expect_length);
  grpc_slice slice;
  gpr_log(GPR_DEBUG, "Test: 0x%08x", value);
  ASSERT_EQ(w.length(), expect_length);
  slice = grpc_slice_malloc(w.length());
  w.Write(prefix_or, GRPC_SLICE_START_PTR(slice));
  ASSERT_TRUE(grpc_slice_eq(expect, slice));
  grpc_slice_unref(expect);
  grpc_slice_unref(slice);
}

#define TEST_VARINT(value, prefix_bits, prefix_or, expect) \
  test_varint<prefix_bits>(value, prefix_or, expect, sizeof(expect) - 1)

TEST(VarintTest, MainTest) {
  TEST_VARINT(0, 1, 0, "\x00");
  TEST_VARINT(128, 1, 0, "\x7f\x01");
  TEST_VARINT(16384, 1, 0, "\x7f\x81\x7f");
  TEST_VARINT(2097152, 1, 0, "\x7f\x81\xff\x7f");
  TEST_VARINT(268435456, 1, 0, "\x7f\x81\xff\xff\x7f");
  TEST_VARINT(0xffffffff, 1, 0, "\x7f\x80\xff\xff\xff\x0f");
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
