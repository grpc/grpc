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
