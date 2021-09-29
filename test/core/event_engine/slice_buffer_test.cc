// Copyright 2021 The gRPC Authors
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

#include "grpc/event_engine/slice_buffer.h"

#include <gmock/gmock.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "test/core/util/test_config.h"

using ::grpc_event_engine::experimental::Slice;
using ::grpc_event_engine::experimental::SliceBuffer;

TEST(SliceBufferTest, CanEnumerateZeroItems) {
  grpc_slice_buffer sb_internal;
  grpc_slice_buffer_init(&sb_internal);
  SliceBuffer sb(&sb_internal);
  int item_cnt = 0;
  sb.enumerate(
      [&item_cnt](uint8_t* start, size_t len, size_t idx) { item_cnt++; });
  ASSERT_EQ(item_cnt, 0);
  grpc_slice_buffer_destroy(&sb_internal);
}

TEST(SliceBufferTest, CopyIsShallow) {
  grpc_slice_buffer sb_internal;
  grpc_slice_buffer_init(&sb_internal);
  SliceBuffer sb(&sb_internal);
  SliceBuffer sb_copy(sb);
  ASSERT_EQ(sb.raw_slice_buffer(), sb_copy.raw_slice_buffer());
  grpc_slice_buffer_destroy(&sb_internal);
}

TEST(SliceBufferTest, MovingSlicesAddsToOneAndRemovesFromOther) {
  grpc_slice_buffer sb_internal;
  grpc_slice_buffer_init(&sb_internal);
  SliceBuffer sb(&sb_internal);
  grpc_slice_buffer sb_internal_2;
  grpc_slice_buffer_init(&sb_internal_2);
  SliceBuffer sb2(&sb_internal_2);
  sb.add(Slice(42));
  sb2.add(sb.take_first());
  EXPECT_EQ(sb.length(), 0);
  EXPECT_EQ(sb2.length(), 42);
  grpc_slice_buffer_destroy(&sb_internal_2);
  grpc_slice_buffer_destroy(&sb_internal);
}

TEST(SliceBufferTest, DeletingSliceBufferDoesNotAffectInternalBuffer) {}

TEST(SliceBufferTest, TakeAndUndoTakeYieldsTheOriginalState) {}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
