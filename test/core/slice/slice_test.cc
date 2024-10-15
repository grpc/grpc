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

#include "src/core/lib/slice/slice.h"

#include <grpc/slice.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/util/memory.h"
#include "src/core/util/no_destruct.h"
#include "test/core/test_util/build.h"

TEST(GrpcSliceTest, MallocReturnsSomethingSensible) {
  // Calls grpc_slice_create for various lengths and verifies the internals for
  // consistency.
  size_t length;
  size_t i;
  grpc_slice slice;

  for (length = 0; length <= 1024; length++) {
    slice = grpc_slice_malloc(length);
    // If there is a length, slice.data must be non-NULL. If length is zero
    // we don't care.
    if (length > GRPC_SLICE_INLINED_SIZE) {
      EXPECT_NE(slice.data.refcounted.bytes, nullptr);
    }
    // Returned slice length must be what was requested.
    EXPECT_EQ(GRPC_SLICE_LENGTH(slice), length);
    // We must be able to write to every byte of the data
    for (i = 0; i < length; i++) {
      GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
    }
    // And finally we must succeed in destroying the slice
    grpc_slice_unref(slice);
  }
}

static void do_nothing(void* /*ignored*/) {}

TEST(GrpcSliceTest, SliceNewReturnsSomethingSensible) {
  uint8_t x;

  grpc_slice slice = grpc_slice_new(&x, 1, do_nothing);
  EXPECT_NE(slice.refcount, nullptr);
  EXPECT_EQ(slice.data.refcounted.bytes, &x);
  EXPECT_EQ(slice.data.refcounted.length, 1);
  grpc_slice_unref(slice);
}

// destroy function that sets a mark to indicate it was called.
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

  // unref should cause destroy function to run.
  grpc_slice_unref(slice);
  EXPECT_EQ(marker, 1);
}

static int do_nothing_with_len_1_calls = 0;

static void do_nothing_with_len_1(void* /*ignored*/, size_t len) {
  EXPECT_EQ(len, 1);
  do_nothing_with_len_1_calls++;
}

TEST(GrpcSliceTest, SliceNewWithLenReturnsSomethingSensible) {
  uint8_t x;
  int num_refs = 5;  // To test adding/removing an arbitrary number of refs
  int i;

  grpc_slice slice = grpc_slice_new_with_len(&x, 1, do_nothing_with_len_1);
  EXPECT_NE(slice.refcount,
            nullptr);  // ref count is initialized to 1 at this point
  EXPECT_EQ(slice.data.refcounted.bytes, &x);
  EXPECT_EQ(slice.data.refcounted.length, 1);
  EXPECT_EQ(do_nothing_with_len_1_calls, 0);

  // Add an arbitrary number of refs to the slice and remoe the refs. This is to
  // make sure that that the destroy callback (i.e do_nothing_with_len_1()) is
  // not called until the last unref operation
  for (i = 0; i < num_refs; i++) {
    grpc_slice_ref(slice);
  }
  for (i = 0; i < num_refs; i++) {
    grpc_slice_unref(slice);
  }
  EXPECT_EQ(do_nothing_with_len_1_calls, 0);  // Shouldn't be called yet

  // last unref
  grpc_slice_unref(slice);
  EXPECT_EQ(do_nothing_with_len_1_calls, 1);
}

class GrpcSliceSizedTest : public ::testing::TestWithParam<size_t> {};

TEST_P(GrpcSliceSizedTest, SliceSubWorks) {
  const auto length = GetParam();

  grpc_slice slice;
  grpc_slice sub;
  unsigned i, j, k;

  // Create a slice in which each byte is equal to the distance from it to the
  // beginning of the slice.
  slice = grpc_slice_malloc(length);
  for (i = 0; i < length; i++) {
    GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
  }

  // Ensure that for all subsets length is correct and that we start on the
  // correct byte. Additionally check that no copies were made.
  for (i = 0; i < length; i++) {
    for (j = i; j < length; j++) {
      sub = grpc_slice_sub(slice, i, j);
      EXPECT_EQ(GRPC_SLICE_LENGTH(sub), j - i);
      for (k = 0; k < j - i; k++) {
        EXPECT_EQ(GRPC_SLICE_START_PTR(sub)[k], (uint8_t)(i + k));
      }
      grpc_slice_unref(sub);
    }
  }
  grpc_slice_unref(slice);
}

static void check_head_tail(grpc_slice slice, grpc_slice head,
                            grpc_slice tail) {
  EXPECT_EQ(GRPC_SLICE_LENGTH(slice),
            GRPC_SLICE_LENGTH(head) + GRPC_SLICE_LENGTH(tail));
  EXPECT_EQ(0, memcmp(GRPC_SLICE_START_PTR(slice), GRPC_SLICE_START_PTR(head),
                      GRPC_SLICE_LENGTH(head)));
  EXPECT_EQ(0, memcmp(GRPC_SLICE_START_PTR(slice) + GRPC_SLICE_LENGTH(head),
                      GRPC_SLICE_START_PTR(tail), GRPC_SLICE_LENGTH(tail)));
}

TEST_P(GrpcSliceSizedTest, SliceSplitHeadWorks) {
  const auto length = GetParam();

  grpc_slice slice;
  grpc_slice head, tail;
  size_t i;

  LOG(INFO) << "length=" << length;

  // Create a slice in which each byte is equal to the distance from it to the
  // beginning of the slice.
  slice = grpc_slice_malloc(length);
  for (i = 0; i < length; i++) {
    GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
  }

  // Ensure that for all subsets length is correct and that we start on the
  // correct byte. Additionally check that no copies were made.
  for (i = 0; i < length; i++) {
    tail = grpc_slice_ref(slice);
    head = grpc_slice_split_head(&tail, i);
    check_head_tail(slice, head, tail);
    grpc_slice_unref(tail);
    grpc_slice_unref(head);
  }

  grpc_slice_unref(slice);
}

TEST_P(GrpcSliceSizedTest, SliceSplitTailWorks) {
  const auto length = GetParam();

  grpc_slice slice;
  grpc_slice head, tail;
  size_t i;

  LOG(INFO) << "length=" << length;

  // Create a slice in which each byte is equal to the distance from it to the
  // beginning of the slice.
  slice = grpc_slice_malloc(length);
  for (i = 0; i < length; i++) {
    GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
  }

  // Ensure that for all subsets length is correct and that we start on the
  // correct byte. Additionally check that no copies were made.
  for (i = 0; i < length; i++) {
    head = grpc_slice_ref(slice);
    tail = grpc_slice_split_tail(&head, i);
    check_head_tail(slice, head, tail);
    grpc_slice_unref(tail);
    grpc_slice_unref(head);
  }

  grpc_slice_unref(slice);
}

INSTANTIATE_TEST_SUITE_P(GrpcSliceSizedTest, GrpcSliceSizedTest,
                         ::testing::ValuesIn([] {
                           std::vector<size_t> out;
                           for (size_t i = 0; i < 128; i++) {
                             out.push_back(i);
                           }
                           return out;
                         }()),
                         [](const testing::TestParamInfo<size_t>& info) {
                           return std::to_string(info.param);
                         });

TEST(GrpcSliceTest, SliceFromCopiedString) {
  static const char* text = "HELLO WORLD!";
  grpc_slice slice;

  slice = grpc_slice_from_copied_string(text);
  EXPECT_EQ(strlen(text), GRPC_SLICE_LENGTH(slice));
  EXPECT_EQ(
      0, memcmp(text, GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice)));
  grpc_slice_unref(slice);
}

TEST(GrpcSliceTest, MovedStringSlice) {
  // Small string should be inlined.
  constexpr char kSmallStr[] = "hello12345";
  char* small_ptr = strdup(kSmallStr);
  grpc_slice small =
      grpc_slice_from_moved_string(grpc_core::UniquePtr<char>(small_ptr));
  EXPECT_EQ(GRPC_SLICE_LENGTH(small), strlen(kSmallStr));
  EXPECT_NE(GRPC_SLICE_START_PTR(small), reinterpret_cast<uint8_t*>(small_ptr));
  grpc_slice_unref(small);

  // Large string should be move the reference.
  constexpr char kSLargeStr[] = "hello123456789123456789123456789";
  char* large_ptr = strdup(kSLargeStr);
  grpc_slice large =
      grpc_slice_from_moved_string(grpc_core::UniquePtr<char>(large_ptr));
  EXPECT_EQ(GRPC_SLICE_LENGTH(large), strlen(kSLargeStr));
  EXPECT_EQ(GRPC_SLICE_START_PTR(large), reinterpret_cast<uint8_t*>(large_ptr));
  grpc_slice_unref(large);

  // Moved buffer must respect the provided length not the actual length of the
  // string.
  large_ptr = strdup(kSLargeStr);
  small = grpc_slice_from_moved_buffer(grpc_core::UniquePtr<char>(large_ptr),
                                       strlen(kSmallStr));
  EXPECT_EQ(GRPC_SLICE_LENGTH(small), strlen(kSmallStr));
  EXPECT_NE(GRPC_SLICE_START_PTR(small), reinterpret_cast<uint8_t*>(large_ptr));
  grpc_slice_unref(small);
}

TEST(GrpcSliceTest, StringViewFromSlice) {
  constexpr char kStr[] = "foo";
  absl::string_view sv(
      grpc_core::StringViewFromSlice(grpc_slice_from_static_string(kStr)));
  EXPECT_EQ(sv, kStr);
}

namespace grpc_core {
namespace {

TEST(SliceTest, FromSmallCopiedString) {
  Slice slice = Slice::FromCopiedString("hello");
  EXPECT_EQ(slice[0], 'h');
  EXPECT_EQ(slice[1], 'e');
  EXPECT_EQ(slice[2], 'l');
  EXPECT_EQ(slice[3], 'l');
  EXPECT_EQ(slice[4], 'o');
  EXPECT_EQ(slice.size(), 5);
  EXPECT_EQ(slice.length(), 5);
  EXPECT_EQ(slice.as_string_view(), "hello");
  EXPECT_EQ(0, memcmp(slice.data(), "hello", 5));
}

class SliceSizedTest : public ::testing::TestWithParam<size_t> {};

std::string RandomString(size_t length) {
  std::string str;
  std::random_device r;
  for (size_t i = 0; i < length; ++i) {
    str.push_back(static_cast<char>(r()));
  }
  return str;
}

TEST_P(SliceSizedTest, FromCopiedString) {
  const std::string str = RandomString(GetParam());
  Slice slice = Slice::FromCopiedString(str);

  EXPECT_EQ(slice.size(), str.size());
  EXPECT_EQ(slice.length(), str.size());
  EXPECT_EQ(slice.as_string_view(), str);
  EXPECT_EQ(0, memcmp(slice.data(), str.data(), str.size()));
  for (size_t i = 0; i < str.size(); ++i) {
    EXPECT_EQ(slice[i], uint8_t(str[i]));
  }

  EXPECT_TRUE(slice.is_equivalent(slice.Ref()));
  EXPECT_TRUE(slice.is_equivalent(slice.AsOwned()));
  EXPECT_TRUE(slice.is_equivalent(slice.Ref().TakeOwned()));
}

INSTANTIATE_TEST_SUITE_P(SliceSizedTest, SliceSizedTest,
                         ::testing::ValuesIn([] {
                           std::vector<size_t> out;
                           size_t i = 1;
                           size_t j = 1;
                           while (i < 1024 * 1024) {
                             out.push_back(j);
                             size_t n = i + j;
                             i = j;
                             j = n;
                           }
                           return out;
                         }()),
                         [](const ::testing::TestParamInfo<size_t>& info) {
                           return std::to_string(info.param);
                         });

class TakeUniquelyOwnedTest
    : public ::testing::TestWithParam<std::function<Slice()>> {};

TEST_P(TakeUniquelyOwnedTest, TakeUniquelyOwned) {
  auto owned = GetParam()().TakeUniquelyOwned();
  auto* refcount = owned.c_slice().refcount;
  if (refcount != nullptr && refcount != grpc_slice_refcount::NoopRefcount()) {
    EXPECT_TRUE(refcount->IsUnique());
  }
}

INSTANTIATE_TEST_SUITE_P(
    TakeUniquelyOwnedTest, TakeUniquelyOwnedTest,
    ::testing::Values(
        []() {
          static const NoDestruct<std::string> big('a', 1024);
          return Slice::FromStaticBuffer(big->data(), big->size());
        },
        []() {
          static const NoDestruct<std::string> big('a', 1024);
          return Slice::FromCopiedBuffer(big->data(), big->size());
        },
        []() {
          static const NoDestruct<std::string> big('a', 1024);
          static const NoDestruct<Slice> big_slice(
              Slice::FromCopiedBuffer(big->data(), big->size()));
          return big_slice->Ref();
        },
        []() { return Slice::FromStaticString("hello"); }));

size_t SumSlice(const Slice& slice) {
  size_t x = 0;
  for (size_t i = 0; i < slice.size(); ++i) {
    x += slice[i];
  }
  return x;
}

TEST(SliceTest, ExternalAsOwned) {
  auto external_string = std::make_unique<std::string>(RandomString(1024));
  Slice slice = Slice::FromExternalString(*external_string);
  const size_t initial_sum = SumSlice(slice);
  Slice owned = slice.AsOwned();
  EXPECT_EQ(initial_sum, SumSlice(owned));
  external_string.reset();
  // In ASAN (where we can be sure that it'll crash), go ahead and read the
  // bytes we just deleted.
  if (BuiltUnderAsan()) {
    ASSERT_DEATH(
        {
          size_t sum = SumSlice(slice);
          VLOG(2) << sum;
        },
        "");
  }
  EXPECT_EQ(initial_sum, SumSlice(owned));
}

TEST(SliceTest, ExternalTakeOwned) {
  std::unique_ptr<std::string> external_string(
      new std::string(RandomString(1024)));
  SumSlice(Slice::FromExternalString(*external_string).TakeOwned());
}

TEST(SliceTest, StaticSlice) {
  static const char* hello = "hello";
  StaticSlice slice = StaticSlice::FromStaticString(hello);
  EXPECT_EQ(slice[0], 'h');
  EXPECT_EQ(slice[1], 'e');
  EXPECT_EQ(slice[2], 'l');
  EXPECT_EQ(slice[3], 'l');
  EXPECT_EQ(slice[4], 'o');
  EXPECT_EQ(slice.size(), 5);
  EXPECT_EQ(slice.length(), 5);
  EXPECT_EQ(slice.as_string_view(), "hello");
  EXPECT_EQ(0, memcmp(slice.data(), "hello", 5));
  EXPECT_EQ(reinterpret_cast<const uint8_t*>(hello), slice.data());
}

TEST(SliceTest, SliceEquality) {
  auto a = Slice::FromCopiedString(
      "hello world 123456789123456789123456789123456789123456789");
  auto b = Slice::FromCopiedString(
      "hello world 123456789123456789123456789123456789123456789");
  auto c = Slice::FromCopiedString(
      "this is not the same as the other two strings!!!!!!!!!!!!");
  EXPECT_FALSE(a.is_equivalent(b));
  EXPECT_FALSE(b.is_equivalent(a));
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_NE(b, c);
  EXPECT_EQ(a, "hello world 123456789123456789123456789123456789123456789");
  EXPECT_NE(a, "pfoooey");
  EXPECT_EQ(c, "this is not the same as the other two strings!!!!!!!!!!!!");
  EXPECT_EQ("hello world 123456789123456789123456789123456789123456789", a);
  EXPECT_NE("pfoooey", a);
  EXPECT_EQ("this is not the same as the other two strings!!!!!!!!!!!!", c);
}

TEST(SliceTest, LetsGetMutable) {
  auto slice = MutableSlice::FromCopiedString("hello");
  EXPECT_EQ(slice[0], 'h');
  EXPECT_EQ(slice[1], 'e');
  EXPECT_EQ(slice[2], 'l');
  EXPECT_EQ(slice[3], 'l');
  EXPECT_EQ(slice[4], 'o');
  EXPECT_EQ(slice.size(), 5);
  EXPECT_EQ(slice.length(), 5);
  EXPECT_EQ(slice.as_string_view(), "hello");
  EXPECT_EQ(0, memcmp(slice.data(), "hello", 5));
  slice[2] = 'm';
  EXPECT_EQ(slice.as_string_view(), "hemlo");
  for (auto& c : slice) c++;
  EXPECT_EQ(slice.as_string_view(), "ifnmp");
}

TEST(SliceTest, SliceCastWorks) {
  using ::grpc_event_engine::experimental::internal::SliceCast;
  Slice test = Slice::FromCopiedString("hello world!");
  const grpc_slice& slice = SliceCast<grpc_slice>(test);
  EXPECT_EQ(&slice, &test.c_slice());
  const Slice& other = SliceCast<Slice>(slice);
  EXPECT_EQ(&other, &test);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
