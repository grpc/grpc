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

#include "src/core/lib/promise/promise.h"

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "src/core/channelz/property_list.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/upb_utils.h"
#include "src/proto/grpc/channelz/v2/promise.upb.h"
#include "src/proto/grpc/channelz/v2/property_list.upb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {

TEST(PromiseTest, Works) {
  Promise<int> x = []() { return 42; };
  EXPECT_EQ(x(), Poll<int>(42));
}

TEST(PromiseTest, Immediate) { EXPECT_EQ(Immediate(42)(), Poll<int>(42)); }

TEST(PromiseTest, AssertResultType) {
  EXPECT_EQ(AssertResultType<int>(Immediate(42))(), Poll<int>(42));
  // Fails to compile: AssertResultType<int>(Immediate(std::string("hello")));
  // Fails to compile: AssertResultType<int>(Immediate(42.9));
}

namespace {

template <typename T, typename F, typename = void>
struct CanAssertResultType : std::false_type {};

template <typename T, typename F>
struct CanAssertResultType<
    T, F, std::void_t<decltype(GRPC_ASSERT_RESULT_TYPE(T)(std::declval<F>()))>>
    : std::true_type {};

class MoveCountingPromise {
 public:
  MoveCountingPromise() = delete;
  MoveCountingPromise(const int value, int* const move_count)
      : value_(value), move_count_(move_count) {}
  MoveCountingPromise(const MoveCountingPromise&) = delete;
  MoveCountingPromise& operator=(const MoveCountingPromise&) = delete;
  MoveCountingPromise(MoveCountingPromise&& other) noexcept
      : value_(other.value_), move_count_(other.move_count_) {
    ++(*move_count_);
  }
  MoveCountingPromise& operator=(MoveCountingPromise&&) = delete;

  Poll<int> operator()() { return value_; }

 private:
  const int value_;
  int* const move_count_;
};

MoveCountingPromise MakeMoveCountingPromise(const int value,
                                            int* const move_count) {
  return GRPC_ASSERT_RESULT_TYPE(int)(MoveCountingPromise(value, move_count));
}

}  // namespace

TEST(PromiseTest, GrpcAssertResultTypeMacro) {
  // Verifies value forwarding, comma-containing result types, and debug-only
  // type checking.
  EXPECT_EQ(GRPC_ASSERT_RESULT_TYPE(int)(Immediate(42))(), Poll<int>(42));
  EXPECT_EQ(NowOrNever(GRPC_ASSERT_RESULT_TYPE(std::pair<int, int>)(
                Immediate(std::make_pair(1, 2)))),
            std::make_optional(std::make_pair(1, 2)));

  EXPECT_TRUE((CanAssertResultType<int, decltype(Immediate(42))>::value));
#ifdef NDEBUG
  EXPECT_TRUE((CanAssertResultType<int, decltype(Immediate(42.9))>::value));
#else
  EXPECT_FALSE((CanAssertResultType<int, decltype(Immediate(42.9))>::value));
#endif
}

TEST(PromiseTest, GrpcAssertResultTypeMacroMoveCount) {
  // Verifies returning through GRPC_ASSERT_RESULT_TYPE elides moves in NDEBUG
  // builds and costs one move in debug builds.
  int move_count = 0;
  MoveCountingPromise promise = MakeMoveCountingPromise(7, &move_count);
  EXPECT_EQ(promise(), Poll<int>(7));
#ifdef NDEBUG
  EXPECT_EQ(move_count, 0);
#else
  EXPECT_EQ(move_count, 1);
#endif
}

TEST(PromiseTest, NowOrNever) {
  EXPECT_EQ(NowOrNever(Immediate(42)), std::optional<int>(42));
}

TEST(PromiseTest, CanConvertToProto) {
  auto x = []() { return 42; };
  EXPECT_FALSE(promise_detail::kHasToProtoMethod<decltype(x)>);
  upb_Arena* arena = upb_Arena_New();
  grpc_channelz_v2_Promise* promise_proto = grpc_channelz_v2_Promise_new(arena);
  PromiseAsProto(x, promise_proto, arena);
  EXPECT_EQ(grpc_channelz_v2_Promise_promise_case(promise_proto),
            grpc_channelz_v2_Promise_promise_unknown_promise);
  upb_Arena_Free(arena);
}

TEST(PromiseTest, CanCustomizeProtoConversion) {
  class FooPromise {
   public:
    channelz::PropertyList ChannelzProperties() const {
      return channelz::PropertyList().Set("foo", "bar");
    }
  };
  EXPECT_FALSE(promise_detail::kHasToProtoMethod<FooPromise>);
  EXPECT_TRUE(promise_detail::kHasChannelzPropertiesMethod<FooPromise>);
  upb_Arena* arena = upb_Arena_New();
  grpc_channelz_v2_Promise* promise_proto = grpc_channelz_v2_Promise_new(arena);
  PromiseAsProto(FooPromise(), promise_proto, arena);
  ASSERT_EQ(grpc_channelz_v2_Promise_promise_case(promise_proto),
            grpc_channelz_v2_Promise_promise_custom_promise);
  auto* custom_promise = grpc_channelz_v2_Promise_custom_promise(promise_proto);
  auto* properties = grpc_channelz_v2_Promise_Custom_properties(custom_promise);
  size_t size;
  const grpc_channelz_v2_PropertyList_Element* const* elements =
      grpc_channelz_v2_PropertyList_properties(properties, &size);
  EXPECT_EQ(size, 1);
  const auto* element = elements[0];
  EXPECT_EQ("foo", UpbStringToAbsl(
                       grpc_channelz_v2_PropertyList_Element_key(element)));
  const grpc_channelz_v2_PropertyValue* val =
      grpc_channelz_v2_PropertyList_Element_value(element);
  EXPECT_EQ(UpbStringToAbsl(grpc_channelz_v2_PropertyValue_string_value(val)),
            "bar");
  upb_Arena_Free(arena);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
