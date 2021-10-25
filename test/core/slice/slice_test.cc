/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <gtest/gtest.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/slice/slice_internal.h"

TEST(GrpcSliceTest, MallocReturnsSomethingSensible) {
  /* Calls grpc_slice_create for various lengths and verifies the internals for
     consistency. */
  size_t length;
  size_t i;
  grpc_slice slice;

  for (length = 0; length <= 1024; length++) {
    slice = grpc_slice_malloc(length);
    /* If there is a length, slice.data must be non-NULL. If length is zero
       we don't care. */
    if (length > GRPC_SLICE_INLINED_SIZE) {
      EXPECT_NE(slice.data.refcounted.bytes, nullptr);
    }
    /* Returned slice length must be what was requested. */
    EXPECT_EQ(GRPC_SLICE_LENGTH(slice), length);
    /* We must be able to write to every byte of the data */
    for (i = 0; i < length; i++) {
      GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
    }
    /* And finally we must succeed in destroying the slice */
    grpc_slice_unref_internal(slice);
  }
}

static void do_nothing(void* /*ignored*/) {}

TEST(GrpcSliceTest, SliceNewReturnsSomethingSensible) {
  uint8_t x;

  grpc_slice slice = grpc_slice_new(&x, 1, do_nothing);
  EXPECT_NE(slice.refcount, nullptr);
  EXPECT_EQ(slice.data.refcounted.bytes, &x);
  EXPECT_EQ(slice.data.refcounted.length, 1);
  grpc_slice_unref_internal(slice);
}

/* destroy function that sets a mark to indicate it was called. */
static void set_mark(void* p) { *(static_cast<int*>(p)) = 1; }

TEST(GrpcSliceTest, SliceNewWithUserData) {
  int marker = 0;
  uint8_t buf[2];
  grpc_slice slice;

  buf[0] = 0;
  buf[1] = 1;
  slice = grpc_slice_new_with_user_data(buf, 2, set_mark, &marker);
  EXPECT_EQ(marker, 0);
  EXPECT_EQ(GRPC_SLICE_LENGTH(slice), 2);
  EXPECT_EQ(GRPC_SLICE_START_PTR(slice)[0], 0);
  EXPECT_EQ(GRPC_SLICE_START_PTR(slice)[1], 1);

  /* unref should cause destroy function to run. */
  grpc_slice_unref_internal(slice);
  EXPECT_EQ(marker, 1);
}

static int do_nothing_with_len_1_calls = 0;

static void do_nothing_with_len_1(void* /*ignored*/, size_t len) {
  EXPECT_EQ(len, 1);
  do_nothing_with_len_1_calls++;
}

TEST(GrpcSliceTest, SliceNewWithLenReturnsSomethingSensible) {
  uint8_t x;
  int num_refs = 5; /* To test adding/removing an arbitrary number of refs */
  int i;

  grpc_slice slice = grpc_slice_new_with_len(&x, 1, do_nothing_with_len_1);
  EXPECT_NE(slice.refcount, nullptr); /* ref count is initialized to 1 at this point */
  EXPECT_EQ(slice.data.refcounted.bytes, &x);
  EXPECT_EQ(slice.data.refcounted.length, 1);
  EXPECT_EQ(do_nothing_with_len_1_calls, 0);

  /* Add an arbitrary number of refs to the slice and remoe the refs. This is to
     make sure that that the destroy callback (i.e do_nothing_with_len_1()) is
     not called until the last unref operation */
  for (i = 0; i < num_refs; i++) {
    grpc_slice_ref_internal(slice);
  }
  for (i = 0; i < num_refs; i++) {
    grpc_slice_unref_internal(slice);
  }
  EXPECT_EQ(do_nothing_with_len_1_calls, 0); /* Shouldn't be called yet */

  /* last unref */
  grpc_slice_unref_internal(slice);
  EXPECT_EQ(do_nothing_with_len_1_calls, 1);
}

class GrpcSliceSizedTest : public ::testing::TestWithParam<size_t> {};

TEST_P(GrpcSliceSizedTest, SliceSubWorks) {
  const auto length = GetParam();

  grpc_slice slice;
  grpc_slice sub;
  unsigned i, j, k;

  /* Create a slice in which each byte is equal to the distance from it to the
     beginning of the slice. */
  slice = grpc_slice_malloc(length);
  for (i = 0; i < length; i++) {
    GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
  }

  /* Ensure that for all subsets length is correct and that we start on the
     correct byte. Additionally check that no copies were made. */
  for (i = 0; i < length; i++) {
    for (j = i; j < length; j++) {
      sub = grpc_slice_sub(slice, i, j);
      EXPECT_EQ(GRPC_SLICE_LENGTH(sub), j - i);
      for (k = 0; k < j - i; k++) {
        EXPECT_EQ(GRPC_SLICE_START_PTR(sub)[k], (uint8_t)(i + k));
      }
      grpc_slice_unref_internal(sub);
    }
  }
  grpc_slice_unref_internal(slice);
}

static void check_head_tail(grpc_slice slice, grpc_slice head,
                            grpc_slice tail) {
  EXPECT_EQ(GRPC_SLICE_LENGTH(slice),
             GRPC_SLICE_LENGTH(head) + GRPC_SLICE_LENGTH(tail));
  EXPECT_EQ(0, memcmp(GRPC_SLICE_START_PTR(slice),
                         GRPC_SLICE_START_PTR(head), GRPC_SLICE_LENGTH(head)));
  EXPECT_EQ(0, memcmp(GRPC_SLICE_START_PTR(slice) + GRPC_SLICE_LENGTH(head),
                         GRPC_SLICE_START_PTR(tail), GRPC_SLICE_LENGTH(tail)));
}

TEST_P(GrpcSliceSizedTest, SliceSplitHeadWorks) {
  const auto length = GetParam();

  grpc_slice slice;
  grpc_slice head, tail;
  size_t i;

  gpr_log(GPR_INFO, "length=%" PRIuPTR, length);

  /* Create a slice in which each byte is equal to the distance from it to the
     beginning of the slice. */
  slice = grpc_slice_malloc(length);
  for (i = 0; i < length; i++) {
    GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
  }

  /* Ensure that for all subsets length is correct and that we start on the
     correct byte. Additionally check that no copies were made. */
  for (i = 0; i < length; i++) {
    tail = grpc_slice_ref_internal(slice);
    head = grpc_slice_split_head(&tail, i);
    check_head_tail(slice, head, tail);
    grpc_slice_unref_internal(tail);
    grpc_slice_unref_internal(head);
  }

  grpc_slice_unref_internal(slice);
}

TEST_P(GrpcSliceSizedTest, SliceSplitTailWorks) {
  const auto length = GetParam();

  grpc_slice slice;
  grpc_slice head, tail;
  size_t i;

  gpr_log(GPR_INFO, "length=%" PRIuPTR, length);

  /* Create a slice in which each byte is equal to the distance from it to the
     beginning of the slice. */
  slice = grpc_slice_malloc(length);
  for (i = 0; i < length; i++) {
    GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
  }

  /* Ensure that for all subsets length is correct and that we start on the
     correct byte. Additionally check that no copies were made. */
  for (i = 0; i < length; i++) {
    head = grpc_slice_ref_internal(slice);
    tail = grpc_slice_split_tail(&head, i);
    check_head_tail(slice, head, tail);
    grpc_slice_unref_internal(tail);
    grpc_slice_unref_internal(head);
  }

  grpc_slice_unref_internal(slice);
}

TEST(GrpcSliceTest, SliceFromCopiedString) {
  static const char* text = "HELLO WORLD!";
  grpc_slice slice;

  slice = grpc_slice_from_copied_string(text);
  EXPECT_EQ(strlen(text), GRPC_SLICE_LENGTH(slice));
  EXPECT_EQ(
      0, memcmp(text, GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice)));
  grpc_slice_unref_internal(slice);
}

TEST(GrpcSliceTest, MovedStringSlice) {
  // Small string should be inlined.
  constexpr char kSmallStr[] = "hello12345";
  char* small_ptr = strdup(kSmallStr);
  grpc_slice small =
      grpc_slice_from_moved_string(grpc_core::UniquePtr<char>(small_ptr));
  EXPECT_EQ(GRPC_SLICE_LENGTH(small), strlen(kSmallStr));
  EXPECT_NE(GRPC_SLICE_START_PTR(small),
             reinterpret_cast<uint8_t*>(small_ptr));
  grpc_slice_unref_internal(small);

  // Large string should be move the reference.
  constexpr char kSLargeStr[] = "hello123456789123456789123456789";
  char* large_ptr = strdup(kSLargeStr);
  grpc_slice large =
      grpc_slice_from_moved_string(grpc_core::UniquePtr<char>(large_ptr));
  EXPECT_EQ(GRPC_SLICE_LENGTH(large), strlen(kSLargeStr));
  EXPECT_EQ(GRPC_SLICE_START_PTR(large),
             reinterpret_cast<uint8_t*>(large_ptr));
  grpc_slice_unref_internal(large);

  // Moved buffer must respect the provided length not the actual length of the
  // string.
  large_ptr = strdup(kSLargeStr);
  small = grpc_slice_from_moved_buffer(grpc_core::UniquePtr<char>(large_ptr),
                                       strlen(kSmallStr));
  EXPECT_EQ(GRPC_SLICE_LENGTH(small), strlen(kSmallStr));
  EXPECT_NE(GRPC_SLICE_START_PTR(small),
             reinterpret_cast<uint8_t*>(large_ptr));
  grpc_slice_unref_internal(small);
}

TEST(GrpcSliceTest, StringViewFromSlice) {
  constexpr char kStr[] = "foo";
  absl::string_view sv(
      grpc_core::StringViewFromSlice(grpc_slice_from_static_string(kStr)));
  EXPECT_EQ(std::string(sv), kStr);
}

int main(int, char**) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
