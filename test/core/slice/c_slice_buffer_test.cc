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

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <stddef.h>

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

static constexpr size_t kTotalDataLength = 4096;

TEST(SliceBufferTest, Add) {
  grpc_slice_buffer buf;
  grpc_slice aaa = grpc_slice_from_copied_string("aaa");
  grpc_slice bb = grpc_slice_from_copied_string("bb");
  size_t i;

  grpc_slice_buffer_init(&buf);
  for (i = 0; i < 10; i++) {
    grpc_slice_ref(aaa);
    grpc_slice_ref(bb);
    grpc_slice_buffer_add(&buf, aaa);
    grpc_slice_buffer_add(&buf, bb);
  }
  ASSERT_GT(buf.count, 0);
  ASSERT_EQ(buf.length, 50);
  grpc_slice_buffer_reset_and_unref(&buf);
  ASSERT_EQ(buf.count, 0);
  ASSERT_EQ(buf.length, 0);
  for (i = 0; i < 10; i++) {
    grpc_slice_ref(aaa);
    grpc_slice_ref(bb);
    grpc_slice_buffer_add(&buf, aaa);
    grpc_slice_buffer_add(&buf, bb);
  }
  ASSERT_GT(buf.count, 0);
  ASSERT_EQ(buf.length, 50);
  for (i = 0; i < 10; i++) {
    grpc_slice_buffer_pop(&buf);
    grpc_slice_unref(aaa);
    grpc_slice_unref(bb);
  }
  ASSERT_EQ(buf.count, 0);
  ASSERT_EQ(buf.length, 0);
  grpc_slice_buffer_destroy(&buf);
}

static void free_data(void* data, size_t len) {
  ASSERT_EQ(len, kTotalDataLength);
  gpr_free(data);
}

TEST(SliceBufferTest, AddContiguousSlices) {
  grpc_slice_buffer buf;
  grpc_slice_buffer_init(&buf);
  char* data = reinterpret_cast<char*>(gpr_malloc(kTotalDataLength));
  ASSERT_NE(data, nullptr);
  grpc_slice a = grpc_slice_new_with_len(data, kTotalDataLength, free_data);
  grpc_slice s1 = grpc_slice_split_head(&a, kTotalDataLength / 4);
  grpc_slice s2 = grpc_slice_split_head(&a, kTotalDataLength / 4);
  grpc_slice s3 = grpc_slice_split_head(&a, kTotalDataLength / 4);
  grpc_slice_buffer_add(&buf, s1);
  ASSERT_EQ(buf.count, 1);
  ASSERT_EQ(buf.length, kTotalDataLength / 4);
  grpc_slice_buffer_add(&buf, s2);
  ASSERT_EQ(buf.count, 1);
  ASSERT_EQ(buf.length, kTotalDataLength / 2);
  grpc_slice_buffer_add(&buf, s3);
  ASSERT_EQ(buf.count, 1);
  ASSERT_EQ(buf.length, 3 * kTotalDataLength / 4);
  grpc_slice_buffer_add(&buf, a);
  ASSERT_EQ(buf.count, 1);
  ASSERT_EQ(buf.length, kTotalDataLength);
  grpc_slice_buffer_destroy(&buf);
}

TEST(SliceBufferTest, MoveFirst) {
  grpc_slice slices[3];
  grpc_slice_buffer src;
  grpc_slice_buffer dst;
  int idx = 0;
  size_t src_len = 0;
  size_t dst_len = 0;

  slices[0] = grpc_slice_from_copied_string("aaa");
  slices[1] = grpc_slice_from_copied_string("bbbb");
  slices[2] = grpc_slice_from_copied_string("ccc");

  grpc_slice_buffer_init(&src);
  grpc_slice_buffer_init(&dst);
  for (idx = 0; idx < 3; idx++) {
    grpc_slice_ref(slices[idx]);
    // For this test, it is important that we add each slice at a new
    // slice index
    grpc_slice_buffer_add_indexed(&src, slices[idx]);
    grpc_slice_buffer_add_indexed(&dst, slices[idx]);
  }

  // Case 1: Move more than the first slice's length from src to dst
  src_len = src.length;
  dst_len = dst.length;
  grpc_slice_buffer_move_first(&src, 4, &dst);
  src_len -= 4;
  dst_len += 4;
  ASSERT_EQ(src.length, src_len);
  ASSERT_EQ(dst.length, dst_len);

  // src now has two slices ["bbb"] and  ["ccc"]
  // Case 2: Move the first slice from src to dst
  grpc_slice_buffer_move_first(&src, 3, &dst);
  src_len -= 3;
  dst_len += 3;
  ASSERT_EQ(src.length, src_len);
  ASSERT_EQ(dst.length, dst_len);

  // src now has one slice ["ccc"]
  // Case 3: Move less than the first slice's length from src to dst
  grpc_slice_buffer_move_first(&src, 2, &dst);
  src_len -= 2;
  dst_len += 2;
  ASSERT_EQ(src.length, src_len);
  ASSERT_EQ(dst.length, dst_len);
}

TEST(SliceBufferTest, First) {
  grpc_slice slices[3];
  slices[0] = grpc_slice_from_copied_string("aaa");
  slices[1] = grpc_slice_from_copied_string("bbbb");
  slices[2] = grpc_slice_from_copied_string("ccccc");

  grpc_slice_buffer buf;
  grpc_slice_buffer_init(&buf);
  for (int idx = 0; idx < 3; ++idx) {
    grpc_slice_ref(slices[idx]);
    grpc_slice_buffer_add_indexed(&buf, slices[idx]);
  }

  grpc_slice* first = grpc_slice_buffer_peek_first(&buf);
  ASSERT_EQ(GRPC_SLICE_LENGTH(*first), GRPC_SLICE_LENGTH(slices[0]));
  ASSERT_EQ(buf.count, 3);
  ASSERT_EQ(buf.length, 12);

  grpc_slice_buffer_sub_first(&buf, 1, 2);
  first = grpc_slice_buffer_peek_first(&buf);
  ASSERT_EQ(GRPC_SLICE_LENGTH(*first), 1);
  ASSERT_EQ(buf.count, 3);
  ASSERT_EQ(buf.length, 10);

  grpc_slice_buffer_remove_first(&buf);
  first = grpc_slice_buffer_peek_first(&buf);
  ASSERT_EQ(GRPC_SLICE_LENGTH(*first), GRPC_SLICE_LENGTH(slices[1]));
  ASSERT_EQ(buf.count, 2);
  ASSERT_EQ(buf.length, 9);

  grpc_slice_buffer_remove_first(&buf);
  first = grpc_slice_buffer_peek_first(&buf);
  ASSERT_EQ(GRPC_SLICE_LENGTH(*first), GRPC_SLICE_LENGTH(slices[2]));
  ASSERT_EQ(buf.count, 1);
  ASSERT_EQ(buf.length, 5);

  grpc_slice_buffer_remove_first(&buf);
  ASSERT_EQ(buf.count, 0);
  ASSERT_EQ(buf.length, 0);
}

TEST(SliceBufferTest, TinyAddEmptyBuffer) {
  grpc_slice_buffer buf;
  grpc_slice_buffer_init(&buf);

  // Tiny add to an empty buffer.
  uint8_t* p = grpc_slice_buffer_tiny_add(&buf, 5);
  ASSERT_NE(p, nullptr);
  memcpy(p, "hello", 5);

  ASSERT_EQ(buf.count, 1);
  ASSERT_EQ(buf.length, 5);

  grpc_slice slice = grpc_slice_buffer_peek_first(&buf)[0];
  ASSERT_EQ(GRPC_SLICE_LENGTH(slice), 5);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(slice), "hello", 5), 0);

  grpc_slice_buffer_destroy(&buf);
}

TEST(SliceBufferTest, TinyAddAppendToInlined) {
  grpc_slice_buffer buf;
  grpc_slice_buffer_init(&buf);

  // First tiny add
  uint8_t* p1 = grpc_slice_buffer_tiny_add(&buf, 5);
  memcpy(p1, "hello", 5);
  ASSERT_EQ(buf.count, 1);
  ASSERT_EQ(buf.length, 5);

  // Second tiny add should append to the same slice.
  uint8_t* p2 = grpc_slice_buffer_tiny_add(&buf, 6);
  ASSERT_EQ(p2, p1 + 5);
  memcpy(p2, " world", 6);

  ASSERT_EQ(buf.count, 1);
  ASSERT_EQ(buf.length, 11);

  grpc_slice slice = grpc_slice_buffer_peek_first(&buf)[0];
  ASSERT_EQ(GRPC_SLICE_LENGTH(slice), 11);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(slice), "hello world", 11), 0);

  grpc_slice_buffer_destroy(&buf);
}

TEST(SliceBufferTest, TinyAddNotEnoughSpace) {
  grpc_slice_buffer buf;
  grpc_slice_buffer_init(&buf);

  const size_t first_add_size = GRPC_SLICE_INLINED_SIZE - 3;
  const size_t second_add_size = 5;

  uint8_t* p1 = grpc_slice_buffer_tiny_add(&buf, first_add_size);
  memset(p1, 'a', first_add_size);
  ASSERT_EQ(buf.count, 1);
  ASSERT_EQ(buf.length, first_add_size);

  // This one should fall back to a new slice.
  uint8_t* p2 = grpc_slice_buffer_tiny_add(&buf, second_add_size);
  ASSERT_NE(p2, p1 + first_add_size);
  memset(p2, 'b', second_add_size);

  ASSERT_EQ(buf.count, 2);
  ASSERT_EQ(buf.length, first_add_size + second_add_size);

  // verify first slice
  grpc_slice slice1 = grpc_slice_buffer_peek_first(&buf)[0];
  ASSERT_EQ(GRPC_SLICE_LENGTH(slice1), first_add_size);

  // verify second slice
  grpc_slice slice2 = buf.slices[1];
  ASSERT_EQ(GRPC_SLICE_LENGTH(slice2), second_add_size);

  grpc_slice_buffer_destroy(&buf);
}

TEST(SliceBufferTest, TinyAddAfterRefcounted) {
  grpc_slice_buffer buf;
  grpc_slice_buffer_init(&buf);

  // Add a refcounted slice (one that is longer than INLINED_SIZE)
  grpc_slice s = grpc_slice_from_copied_string(
      "this is a refcounted string that is large enough");
  size_t initial_len = GRPC_SLICE_LENGTH(s);
  grpc_slice_buffer_add(&buf, s);

  ASSERT_EQ(buf.count, 1);
  ASSERT_EQ(buf.length, initial_len);

  // Tiny add should create a new slice, since previous is refcounted
  uint8_t* p = grpc_slice_buffer_tiny_add(&buf, 5);
  memcpy(p, "hello", 5);

  ASSERT_EQ(buf.count, 2);
  ASSERT_EQ(buf.length, initial_len + 5);

  // verify second slice
  grpc_slice slice2 = buf.slices[1];
  ASSERT_EQ(GRPC_SLICE_LENGTH(slice2), 5);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(slice2), "hello", 5), 0);

  grpc_slice_buffer_destroy(&buf);
}

TEST(SliceBufferTest, TinyAddEmbiggen) {
  grpc_slice_buffer buf;
  grpc_slice_buffer_init(&buf);

  // Fill up the buffer capacity to trigger embiggen logic.
  // capacity starts at GRPC_SLICE_BUFFER_INLINE_ELEMENTS.
  for (size_t i = 0; i < GRPC_SLICE_BUFFER_INLINE_ELEMENTS + 1; ++i) {
    grpc_slice s = grpc_slice_from_copied_string(
        "large enough string to be refcounted so tiny add creates new slice");
    grpc_slice_buffer_add(&buf, s);
  }

  size_t initial_count = buf.count;
  size_t initial_len = buf.length;

  uint8_t* p = grpc_slice_buffer_tiny_add(&buf, 5);
  memcpy(p, "hello", 5);

  ASSERT_EQ(buf.count, initial_count + 1);
  ASSERT_EQ(buf.length, initial_len + 5);

  grpc_slice slice_last = buf.slices[buf.count - 1];
  ASSERT_EQ(GRPC_SLICE_LENGTH(slice_last), 5);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(slice_last), "hello", 5), 0);

  grpc_slice_buffer_destroy(&buf);
}
int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
