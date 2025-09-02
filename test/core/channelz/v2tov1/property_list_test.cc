// Copyright 2025 gRPC authors.
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

#include "src/core/channelz/v2tov1/property_list.h"

#include <google/protobuf/text_format.h>

#include "absl/log/check.h"
#include "fuzztest/fuzztest.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "gtest/gtest.h"
#include "src/proto/grpc/channelz/v2/property_list.pb.h"
#include "src/proto/grpc/channelz/v2/property_list.upb.h"
#include "upb/mem/arena.hpp"

namespace grpc_core {
namespace channelz {
namespace v2tov1 {
namespace {

auto ParsePropertyList(const std::string& proto, upb_Arena* arena) {
  grpc::channelz::v2::PropertyList msg;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &msg));
  std::string serialized = msg.SerializeAsString();
  return grpc_channelz_v2_PropertyList_parse(serialized.data(),
                                             serialized.size(), arena);
}

TEST(PropertyListTest, Int64Found) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(R"pb(
                                       properties: {
                                         key: "the_key"
                                         value: { int64_value: 123 }
                                       }
                                     )pb",
                                     arena.ptr());
  auto val = Int64FromPropertyList(pl, "the_key");
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 123);
}

TEST(PropertyListTest, Int64WrongType) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(R"pb(
                                       properties: {
                                         key: "the_key"
                                         value: { string_value: "123" }
                                       }
                                     )pb",
                                     arena.ptr());
  auto val = Int64FromPropertyList(pl, "the_key");
  EXPECT_FALSE(val.has_value());
}

TEST(PropertyListTest, Int64NotFound) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(R"pb(
                                       properties: {
                                         key: "another_key"
                                         value: { int64_value: 123 }
                                       }
                                     )pb",
                                     arena.ptr());
  auto val = Int64FromPropertyList(pl, "the_key");
  EXPECT_FALSE(val.has_value());
}

TEST(PropertyListTest, Uint64Found) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(R"pb(
                                       properties: {
                                         key: "the_key"
                                         value: { uint64_value: 123 }
                                       }
                                     )pb",
                                     arena.ptr());
  auto val = Int64FromPropertyList(pl, "the_key");
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 123);
}

TEST(PropertyListTest, Uint64Overflow) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(
      "properties: { key: \"the_key\" value: { uint64_value: "
      "9223372036854775808 } }",
      arena.ptr());
  auto val = Int64FromPropertyList(pl, "the_key");
  EXPECT_FALSE(val.has_value());
}

TEST(PropertyListTest, StringFound) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(R"pb(
                                       properties: {
                                         key: "the_key"
                                         value: { string_value: "the_value" }
                                       }
                                     )pb",
                                     arena.ptr());
  auto val = StringFromPropertyList(pl, "the_key");
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, "the_value");
}

TEST(PropertyListTest, StringWrongType) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(R"pb(
                                       properties: {
                                         key: "the_key"
                                         value: { int64_value: 123 }
                                       }
                                     )pb",
                                     arena.ptr());
  auto val = StringFromPropertyList(pl, "the_key");
  EXPECT_FALSE(val.has_value());
}

TEST(PropertyListTest, StringNotFound) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(R"pb(
                                       properties: {
                                         key: "another_key"
                                         value: { string_value: "the_value" }
                                       }
                                     )pb",
                                     arena.ptr());
  auto val = StringFromPropertyList(pl, "the_key");
  EXPECT_FALSE(val.has_value());
}

TEST(PropertyListTest, TimestampFound) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(
      R"pb(
        properties: {
          key: "the_key"
          value: { timestamp_value: { seconds: 123, nanos: 456 } }
        }
      )pb",
      arena.ptr());
  auto* val = TimestampFromPropertyList(pl, "the_key");
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(google_protobuf_Timestamp_seconds(val), 123);
  EXPECT_EQ(google_protobuf_Timestamp_nanos(val), 456);
}

TEST(PropertyListTest, TimestampWrongType) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(R"pb(
                                       properties: {
                                         key: "the_key"
                                         value: { int64_value: 123 }
                                       }
                                     )pb",
                                     arena.ptr());
  auto val = TimestampFromPropertyList(pl, "the_key");
  EXPECT_EQ(val, nullptr);
}

TEST(PropertyListTest, TimestampNotFound) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(
      R"pb(
        properties: {
          key: "another_key"
          value: { timestamp_value: { seconds: 123, nanos: 456 } }
        }
      )pb",
      arena.ptr());
  auto val = TimestampFromPropertyList(pl, "the_key");
  EXPECT_EQ(val, nullptr);
}

TEST(PropertyListTest, PropertyListFound) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(
      R"pb(
        properties: {
          key: "the_key"
          value: {
            any_value: {
              [type.googleapis.com/grpc.channelz.v2.PropertyList] {
                properties: {
                  key: "inner_key"
                  value: { int64_value: 42 }
                }
              }
            }
          }
        }
      )pb",
      arena.ptr());
  const auto* val = PropertyListFromPropertyList(pl, "the_key", arena.ptr());
  ASSERT_NE(val, nullptr);
  auto int_val = Int64FromPropertyList(val, "inner_key");
  ASSERT_TRUE(int_val.has_value());
  EXPECT_EQ(*int_val, 42);
}

TEST(PropertyListTest, PropertyListWrongType) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(R"pb(
                                       properties: {
                                         key: "the_key"
                                         value: { int64_value: 123 }
                                       }
                                     )pb",
                                     arena.ptr());
  auto val = PropertyListFromPropertyList(pl, "the_key", arena.ptr());
  EXPECT_EQ(val, nullptr);
}

TEST(PropertyListTest, PropertyListNotFound) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(
      R"pb(
        properties: {
          key: "another_key"
          value: {
            any_value: {
              [type.googleapis.com/grpc.channelz.v2.PropertyList] {
                properties: {
                  key: "inner_key"
                  value: { int64_value: 42 }
                }
              }
            }
          }
        }
      )pb",
      arena.ptr());
  auto val = PropertyListFromPropertyList(pl, "the_key", arena.ptr());
  EXPECT_EQ(val, nullptr);
}

TEST(PropertyListTest, DurationFound) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(
      R"pb(
        properties: {
          key: "the_key"
          value: { duration_value: { seconds: 123, nanos: 456 } }
        }
      )pb",
      arena.ptr());
  auto* val = DurationFromPropertyList(pl, "the_key");
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(google_protobuf_Duration_seconds(val), 123);
  EXPECT_EQ(google_protobuf_Duration_nanos(val), 456);
}

TEST(PropertyListTest, DurationWrongType) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(R"pb(
                                       properties: {
                                         key: "the_key"
                                         value: { int64_value: 123 }
                                       }
                                     )pb",
                                     arena.ptr());
  auto val = DurationFromPropertyList(pl, "the_key");
  EXPECT_EQ(val, nullptr);
}

TEST(PropertyListTest, DurationNotFound) {
  upb::Arena arena;
  const auto* pl = ParsePropertyList(
      R"pb(
        properties: {
          key: "another_key"
          value: { duration_value: { seconds: 123, nanos: 456 } }
        }
      )pb",
      arena.ptr());
  auto val = DurationFromPropertyList(pl, "the_key");
  EXPECT_EQ(val, nullptr);
}

void Fuzz(const grpc::channelz::v2::PropertyList& pl_msg,
          std::string property_name) {
  upb::Arena arena;
  std::string serialized = pl_msg.SerializeAsString();
  const auto* pl = grpc_channelz_v2_PropertyList_parse(
      serialized.data(), serialized.size(), arena.ptr());
  if (pl == nullptr) return;
  Int64FromPropertyList(pl, property_name);
  StringFromPropertyList(pl, property_name);
  TimestampFromPropertyList(pl, property_name);
  PropertyListFromPropertyList(pl, property_name, arena.ptr());
  DurationFromPropertyList(pl, property_name);
}
FUZZ_TEST(PropertyListTest, Fuzz);

}  // namespace
}  // namespace v2tov1
}  // namespace channelz
}  // namespace grpc_core
