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

#include "src/core/channelz/property_list.h"

#include "gmock/gmock.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "gtest/gtest.h"
#include "src/core/util/json/json.h"
#include "src/proto/grpc/channelz/v2/property_list.upb.h"
#include "upb/mem/arena.hpp"

namespace grpc_core {
namespace channelz {

TEST(PropertyListTest, EmptyList) {
  PropertyList props;
  Json::Object json_obj = props.TakeJsonObject();
  EXPECT_TRUE(json_obj.empty());
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 0);
}

TEST(PropertyListTest, SetStringView) {
  PropertyList props;
  props.Set("key1", absl::string_view("value1"));
  Json::Object json_obj = props.TakeJsonObject();
  ASSERT_EQ(json_obj.size(), 1);
  auto it = json_obj.find("key1");
  ASSERT_NE(it, json_obj.end());
  EXPECT_EQ(it->second.type(), Json::Type::kString);
  EXPECT_EQ(it->second.string(), "value1");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 1);
  grpc_channelz_v2_PropertyValue* value;
  ASSERT_TRUE(grpc_channelz_v2_PropertyList_properties_get(
      upb_proto, StdStringToUpbString("key1"), &value));
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(value));
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyValue_string_value(value)),
      "value1");
}

TEST(PropertyListTest, SetStdString) {
  PropertyList props;
  props.Set("key2", std::string("value2"));
  Json::Object json_obj = props.TakeJsonObject();
  ASSERT_EQ(json_obj.size(), 1);
  auto it = json_obj.find("key2");
  ASSERT_NE(it, json_obj.end());
  EXPECT_EQ(it->second.type(), Json::Type::kString);
  EXPECT_EQ(it->second.string(), "value2");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 1);
  grpc_channelz_v2_PropertyValue* value;
  ASSERT_TRUE(grpc_channelz_v2_PropertyList_properties_get(
      upb_proto, StdStringToUpbString("key2"), &value));
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(value));
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyValue_string_value(value)),
      "value2");
}

TEST(PropertyListTest, SetArithmetic) {
  PropertyList props;
  props.Set("int_key", 123);
  props.Set("double_key", 45.67);
  Json::Object json_obj = props.TakeJsonObject();
  ASSERT_EQ(json_obj.size(), 2);

  auto it_int = json_obj.find("int_key");
  ASSERT_NE(it_int, json_obj.end());
  EXPECT_EQ(it_int->second.type(), Json::Type::kNumber);
  EXPECT_EQ(it_int->second.string(), "123");

  auto it_double = json_obj.find("double_key");
  ASSERT_NE(it_double, json_obj.end());
  EXPECT_EQ(it_double->second.type(), Json::Type::kNumber);
  EXPECT_EQ(it_double->second.string(), "45.67");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 2);
  grpc_channelz_v2_PropertyValue* value;
  ASSERT_TRUE(grpc_channelz_v2_PropertyList_properties_get(
      upb_proto, StdStringToUpbString("int_key"), &value));
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_int64_value(value));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_int64_value(value), 123);
  ASSERT_TRUE(grpc_channelz_v2_PropertyList_properties_get(
      upb_proto, StdStringToUpbString("double_key"), &value));
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_double_value(value));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_double_value(value), 45.67);
}

TEST(PropertyListTest, SetDuration) {
  PropertyList props;
  props.Set("duration_key", Duration::Seconds(5));
  Json::Object json_obj = props.TakeJsonObject();
  ASSERT_EQ(json_obj.size(), 1);
  auto it = json_obj.find("duration_key");
  ASSERT_NE(it, json_obj.end());
  EXPECT_EQ(it->second.type(), Json::Type::kString);
  EXPECT_EQ(it->second.string(), "5.000000000s");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 1);
  grpc_channelz_v2_PropertyValue* value;
  ASSERT_TRUE(grpc_channelz_v2_PropertyList_properties_get(
      upb_proto, StdStringToUpbString("duration_key"), &value));
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_duration_value(value));
  EXPECT_EQ(google_protobuf_Duration_seconds(
                grpc_channelz_v2_PropertyValue_duration_value(value)),
            5);
  EXPECT_EQ(google_protobuf_Duration_nanos(
                grpc_channelz_v2_PropertyValue_duration_value(value)),
            0);
}

TEST(PropertyListTest, SetTimestamp) {
  PropertyList props;
  // Using a known epoch time for consistent testing.
  // January 1, 2023 00:00:00 UTC
  gpr_timespec ts_known = {1672531200, 0, GPR_CLOCK_REALTIME};
  Timestamp timestamp = Timestamp::FromTimespecRoundDown(ts_known);
  props.Set("timestamp_key", timestamp);
  Json::Object json_obj = props.TakeJsonObject();
  ASSERT_EQ(json_obj.size(), 1);
  auto it = json_obj.find("timestamp_key");
  ASSERT_NE(it, json_obj.end());
  EXPECT_EQ(it->second.type(), Json::Type::kString);
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 1);
  grpc_channelz_v2_PropertyValue* value;
  ASSERT_TRUE(grpc_channelz_v2_PropertyList_properties_get(
      upb_proto, StdStringToUpbString("timestamp_key"), &value));
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_timestamp_value(value));
  const auto* timestamp_value =
      grpc_channelz_v2_PropertyValue_timestamp_value(value);
  double actual_time =
      static_cast<double>(google_protobuf_Timestamp_seconds(timestamp_value)) +
      static_cast<double>(google_protobuf_Timestamp_nanos(timestamp_value)) /
          1e9;
  EXPECT_NEAR(actual_time, 1672531200.0, 10.0);
}

TEST(PropertyListTest, SetOptionalStdString) {
  // Test with value
  {
    PropertyList props;
    props.Set("optional_key", std::optional<std::string>("optional_value"));
    Json::Object json_obj = props.TakeJsonObject();
    ASSERT_EQ(json_obj.size(), 1);
    auto it = json_obj.find("optional_key");
    ASSERT_NE(it, json_obj.end());
    EXPECT_EQ(it->second.type(), Json::Type::kString);
    EXPECT_EQ(it->second.string(), "optional_value");
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 1);
    grpc_channelz_v2_PropertyValue* value;
    ASSERT_TRUE(grpc_channelz_v2_PropertyList_properties_get(
        upb_proto, StdStringToUpbString("optional_key"), &value));
    EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(value));
    EXPECT_EQ(UpbStringToStdString(
                  grpc_channelz_v2_PropertyValue_string_value(value)),
              "optional_value");
  }
  // Test with nullopt
  {
    PropertyList props;
    props.Set("optional_key", std::optional<std::string>(std::nullopt));
    Json::Object json_obj = props.TakeJsonObject();
    EXPECT_TRUE(json_obj.empty());
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 0);
  }
}

TEST(PropertyListTest, SetOptionalStringView) {
  // Test with value
  {
    PropertyList props;
    props.Set("optional_key",
              std::optional<absl::string_view>("optional_value"));
    Json::Object json_obj = props.TakeJsonObject();
    ASSERT_EQ(json_obj.size(), 1);
    auto it = json_obj.find("optional_key");
    ASSERT_NE(it, json_obj.end());
    EXPECT_EQ(it->second.type(), Json::Type::kString);
    EXPECT_EQ(it->second.string(), "optional_value");
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 1);
    grpc_channelz_v2_PropertyValue* value;
    ASSERT_TRUE(grpc_channelz_v2_PropertyList_properties_get(
        upb_proto, StdStringToUpbString("optional_key"), &value));
    EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(value));
    EXPECT_EQ(UpbStringToStdString(
                  grpc_channelz_v2_PropertyValue_string_value(value)),
              "optional_value");
  }
  // Test with nullopt
  {
    PropertyList props;
    props.Set("optional_key", std::optional<absl::string_view>(std::nullopt));
    Json::Object json_obj = props.TakeJsonObject();
    EXPECT_TRUE(json_obj.empty());
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 0);
  }
}

TEST(PropertyListTest, SetOptionalDouble) {
  // Test with value
  {
    PropertyList props;
    props.Set("optional_key", std::optional<double>(45.67));
    Json::Object json_obj = props.TakeJsonObject();
    ASSERT_EQ(json_obj.size(), 1);
    auto it = json_obj.find("optional_key");
    ASSERT_NE(it, json_obj.end());
    EXPECT_EQ(it->second.type(), Json::Type::kNumber);
    EXPECT_EQ(it->second.string(), "45.67");
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 1);
    grpc_channelz_v2_PropertyValue* value;
    ASSERT_TRUE(grpc_channelz_v2_PropertyList_properties_get(
        upb_proto, StdStringToUpbString("optional_key"), &value));
    EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_double_value(value));
    EXPECT_EQ(grpc_channelz_v2_PropertyValue_double_value(value), 45.67);
  }
  // Test with nullopt
  {
    PropertyList props;
    props.Set("optional_key", std::optional<double>(std::nullopt));
    Json::Object json_obj = props.TakeJsonObject();
    EXPECT_TRUE(json_obj.empty());
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 0);
  }
}

TEST(PropertyListTest, SetOptionalInt) {
  // Test with value
  {
    PropertyList props;
    props.Set("optional_key", std::optional<int>(123));
    Json::Object json_obj = props.TakeJsonObject();
    ASSERT_EQ(json_obj.size(), 1);
    auto it = json_obj.find("optional_key");
    ASSERT_NE(it, json_obj.end());
    EXPECT_EQ(it->second.type(), Json::Type::kNumber);
    EXPECT_EQ(it->second.string(), "123");
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 1);
    grpc_channelz_v2_PropertyValue* value;
    ASSERT_TRUE(grpc_channelz_v2_PropertyList_properties_get(
        upb_proto, StdStringToUpbString("optional_key"), &value));
    EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_int64_value(value));
    EXPECT_EQ(grpc_channelz_v2_PropertyValue_int64_value(value), 123);
  }
  // Test with nullopt
  {
    PropertyList props;
    props.Set("optional_key", std::optional<int>(std::nullopt));
    Json::Object json_obj = props.TakeJsonObject();
    EXPECT_TRUE(json_obj.empty());
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 0);
  }
}

TEST(PropertyListTest, SetOptionalTimestamp) {
  // Test with value
  {
    PropertyList props;
    // Using a known epoch time for consistent testing.
    // January 1, 2023 00:00:00 UTC
    gpr_timespec ts_known = {1672531200, 0, GPR_CLOCK_REALTIME};
    Timestamp timestamp = Timestamp::FromTimespecRoundDown(ts_known);
    props.Set("optional_key", std::optional<Timestamp>(timestamp));
    Json::Object json_obj = props.TakeJsonObject();
    ASSERT_EQ(json_obj.size(), 1);
    auto it = json_obj.find("optional_key");
    ASSERT_NE(it, json_obj.end());
    EXPECT_EQ(it->second.type(), Json::Type::kString);
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 1);
    grpc_channelz_v2_PropertyValue* value;
    ASSERT_TRUE(grpc_channelz_v2_PropertyList_properties_get(
        upb_proto, StdStringToUpbString("optional_key"), &value));
    EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_timestamp_value(value));
    const auto* timestamp_value =
        grpc_channelz_v2_PropertyValue_timestamp_value(value);
    double actual_time =
        static_cast<double>(
            google_protobuf_Timestamp_seconds(timestamp_value)) +
        static_cast<double>(google_protobuf_Timestamp_nanos(timestamp_value)) /
            1e9;
    EXPECT_NEAR(actual_time, 1672531200.0, 10.0);
  }
  // Test with nullopt
  {
    PropertyList props;
    props.Set("optional_key", std::optional<Timestamp>(std::nullopt));
    Json::Object json_obj = props.TakeJsonObject();
    EXPECT_TRUE(json_obj.empty());
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    EXPECT_EQ(grpc_channelz_v2_PropertyList_properties_size(upb_proto), 0);
  }
}

}  // namespace channelz
}  // namespace grpc_core