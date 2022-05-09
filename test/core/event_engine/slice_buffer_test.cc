// Copyright 2022 The gRPC Authors
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "grpc/event_engine/memory_allocator.h"
#include <grpc/grpc.h>

#include "src/core/lib/slice/slice.h"
#include "test/core/util/test_config.h"

using ::grpc_event_engine::experimental::Slice;
using ::grpc_event_engine::experimental::SliceBuffer;

static constexpr int kNewSliceLength = 100;

Slice MakeSlice(size_t len) {
  GPR_ASSERT(len > 0);
  std::string contents(len, 'a');
  return Slice(Slice::FromExternalString(contents));
}

TEST(SliceBufferTest, AddAndRemoveTest) {
  grpc_slice_buffer sb_internal;
  grpc_slice_buffer_init(&sb_internal);
  SliceBuffer sb(&sb_internal);
  Slice first_slice = MakeSlice(kNewSliceLength);
  Slice second_slice = MakeSlice(kNewSliceLength);
  Slice first_slice_copy = first_slice.Copy();
  sb.Add(std::move(first_slice));
  sb.Add(std::move(second_slice));
  ASSERT_EQ(sb.Count(), 2);
  ASSERT_EQ(sb.Length(), 2 * kNewSliceLength);
  Slice popped = sb.TakeFirst();
  ASSERT_EQ(popped, first_slice_copy);
  ASSERT_EQ(sb.Count(), 1);
  ASSERT_EQ(sb.Length(), kNewSliceLength);
  sb.UndoTakeFirst(std::move(popped));
  ASSERT_EQ(sb.Count(), 2);
  ASSERT_EQ(sb.Length(), 2 * kNewSliceLength);
  sb.Clear();
  ASSERT_EQ(sb.Count(), 0);
  ASSERT_EQ(sb.Length(), 0);
}

TEST(SliceBufferTest, SliceRefTest) {
  grpc_slice_buffer sb_internal;
  grpc_slice_buffer_init(&sb_internal);
  SliceBuffer sb(&sb_internal);
  Slice first_slice = MakeSlice(kNewSliceLength);
  Slice second_slice = MakeSlice(kNewSliceLength + 1);
  Slice first_slice_copy = first_slice.Copy();
  Slice second_slice_copy = second_slice.Copy();
  ASSERT_EQ(sb.AddIndexed(std::move(first_slice)), 0);
  ASSERT_EQ(sb.AddIndexed(std::move(second_slice)), 1);
  Slice first_reffed = sb.RefSlice(0);
  Slice second_reffed = sb.RefSlice(1);
  ASSERT_EQ(first_reffed, first_slice_copy);
  ASSERT_EQ(second_reffed, second_slice_copy);
  ASSERT_EQ(sb.Count(), 2);
  ASSERT_EQ(sb.Length(), 2 * kNewSliceLength + 1);
  sb.Clear();
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
