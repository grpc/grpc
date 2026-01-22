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

#include "google/protobuf/duration.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/upb_utils.h"
#include "src/proto/grpc/channelz/v2/property_list.upb.h"
#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace channelz {

namespace {
// Helper to get a property from a property list.
const grpc_channelz_v2_PropertyValue* GetProperty(
    const grpc_channelz_v2_PropertyList* property_list, absl::string_view key) {
  size_t num_properties;
  const auto* const* properties =
      grpc_channelz_v2_PropertyList_properties(property_list, &num_properties);
  for (size_t i = 0; i < num_properties; ++i) {
    const auto* element = properties[i];
    upb_StringView element_key =
        grpc_channelz_v2_PropertyList_Element_key(element);
    if (absl::string_view(element_key.data, element_key.size) == key) {
      return grpc_channelz_v2_PropertyList_Element_value(element);
    }
  }
  return nullptr;
}
}  // namespace

TEST(PropertyListTest, EmptyList) {
  PropertyList props;
  Json::Object json_obj = props.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  EXPECT_TRUE(json_obj.empty());
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  size_t size;
  grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
  EXPECT_EQ(size, 0);
}

TEST(PropertyListTest, SetStringView) {
  PropertyList props;
  props.Set("key1", absl::string_view("value1"));
  Json::Object json_obj = props.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 1);
  auto it = json_obj.find("key1");
  ASSERT_NE(it, json_obj.end());
  EXPECT_EQ(it->second.type(), Json::Type::kString);
  EXPECT_EQ(it->second.string(), "value1");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  size_t size;
  grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
  EXPECT_EQ(size, 1);
  const auto* value = GetProperty(upb_proto, "key1");
  ASSERT_NE(value, nullptr);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(value));
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyValue_string_value(value)),
      "value1");
}

TEST(PropertyListTest, SetStdString) {
  PropertyList props;
  props.Set("key2", std::string("value2"));
  Json::Object json_obj = props.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 1);
  auto it = json_obj.find("key2");
  ASSERT_NE(it, json_obj.end());
  EXPECT_EQ(it->second.type(), Json::Type::kString);
  EXPECT_EQ(it->second.string(), "value2");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  size_t size;
  grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
  EXPECT_EQ(size, 1);
  const auto* value = GetProperty(upb_proto, "key2");
  ASSERT_NE(value, nullptr);
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
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 2);

  EXPECT_EQ(json_obj["int_key"].type(), Json::Type::kNumber);
  EXPECT_EQ(json_obj["int_key"].string(), "123");
  EXPECT_EQ(json_obj["double_key"].type(), Json::Type::kNumber);
  EXPECT_EQ(json_obj["double_key"].string(), "45.67");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  size_t size;
  grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
  EXPECT_EQ(size, 2);
  const auto* int_value = GetProperty(upb_proto, "int_key");
  ASSERT_NE(int_value, nullptr);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_int64_value(int_value));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_int64_value(int_value), 123);
  const auto* double_value = GetProperty(upb_proto, "double_key");
  ASSERT_NE(double_value, nullptr);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_double_value(double_value));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_double_value(double_value), 45.67);
}

TEST(PropertyListTest, SetDuration) {
  PropertyList props;
  props.Set("duration_key", Duration::Seconds(5));
  Json::Object json_obj = props.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 1);
  auto it = json_obj.find("duration_key");
  ASSERT_NE(it, json_obj.end());
  EXPECT_EQ(it->second.type(), Json::Type::kString);
  EXPECT_EQ(it->second.string(), "5.000000000s");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  size_t size;
  grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
  EXPECT_EQ(size, 1);
  const auto* value = GetProperty(upb_proto, "duration_key");
  ASSERT_NE(value, nullptr);
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
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 1);
  auto it = json_obj.find("timestamp_key");
  ASSERT_NE(it, json_obj.end());
  EXPECT_EQ(it->second.type(), Json::Type::kString);
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  size_t size;
  grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
  EXPECT_EQ(size, 1);
  const auto* value = GetProperty(upb_proto, "timestamp_key");
  ASSERT_NE(value, nullptr);
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
    LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
    ASSERT_EQ(json_obj.size(), 1);
    auto it = json_obj.find("optional_key");
    ASSERT_NE(it, json_obj.end());
    EXPECT_EQ(it->second.type(), Json::Type::kString);
    EXPECT_EQ(it->second.string(), "optional_value");
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    size_t size;
    grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
    EXPECT_EQ(size, 1);
    const auto* value = GetProperty(upb_proto, "optional_key");
    ASSERT_NE(value, nullptr);
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
    LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
    EXPECT_TRUE(json_obj.empty());
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    size_t size;
    grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
    EXPECT_EQ(size, 0);
  }
}

TEST(PropertyListTest, SetOptionalStringView) {
  // Test with value
  {
    PropertyList props;
    props.Set("optional_key",
              std::optional<absl::string_view>("optional_value"));
    Json::Object json_obj = props.TakeJsonObject();
    LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
    ASSERT_EQ(json_obj.size(), 1);
    auto it = json_obj.find("optional_key");
    ASSERT_NE(it, json_obj.end());
    EXPECT_EQ(it->second.type(), Json::Type::kString);
    EXPECT_EQ(it->second.string(), "optional_value");
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    size_t size;
    grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
    EXPECT_EQ(size, 1);
    const auto* value = GetProperty(upb_proto, "optional_key");
    ASSERT_NE(value, nullptr);
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
    LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
    EXPECT_TRUE(json_obj.empty());
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    size_t size;
    grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
    EXPECT_EQ(size, 0);
  }
}

TEST(PropertyListTest, SetOptionalDouble) {
  // Test with value
  {
    PropertyList props;
    props.Set("optional_key", std::optional<double>(45.67));
    Json::Object json_obj = props.TakeJsonObject();
    LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
    ASSERT_EQ(json_obj.size(), 1);
    auto it = json_obj.find("optional_key");
    ASSERT_NE(it, json_obj.end());
    EXPECT_EQ(it->second.type(), Json::Type::kNumber);
    EXPECT_EQ(it->second.string(), "45.67");
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    size_t size;
    grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
    EXPECT_EQ(size, 1);
    const auto* value = GetProperty(upb_proto, "optional_key");
    ASSERT_NE(value, nullptr);
    EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_double_value(value));
    EXPECT_EQ(grpc_channelz_v2_PropertyValue_double_value(value), 45.67);
  }
  // Test with nullopt
  {
    PropertyList props;
    props.Set("optional_key", std::optional<double>(std::nullopt));
    Json::Object json_obj = props.TakeJsonObject();
    LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
    EXPECT_TRUE(json_obj.empty());
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    size_t size;
    grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
    EXPECT_EQ(size, 0);
  }
}

TEST(PropertyListTest, SetOptionalInt) {
  // Test with value
  {
    PropertyList props;
    props.Set("optional_key", std::optional<int>(123));
    Json::Object json_obj = props.TakeJsonObject();
    LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
    ASSERT_EQ(json_obj.size(), 1);
    auto it = json_obj.find("optional_key");
    ASSERT_NE(it, json_obj.end());
    EXPECT_EQ(it->second.type(), Json::Type::kNumber);
    EXPECT_EQ(it->second.string(), "123");
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    size_t size;
    grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
    EXPECT_EQ(size, 1);
    const auto* value = GetProperty(upb_proto, "optional_key");
    ASSERT_NE(value, nullptr);
    EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_int64_value(value));
    EXPECT_EQ(grpc_channelz_v2_PropertyValue_int64_value(value), 123);
  }
  // Test with nullopt
  {
    PropertyList props;
    props.Set("optional_key", std::optional<int>(std::nullopt));
    Json::Object json_obj = props.TakeJsonObject();
    LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
    EXPECT_TRUE(json_obj.empty());
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    size_t size;
    grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
    EXPECT_EQ(size, 0);
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
    LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
    ASSERT_EQ(json_obj.size(), 1);
    auto it = json_obj.find("optional_key");
    ASSERT_NE(it, json_obj.end());
    EXPECT_EQ(it->second.type(), Json::Type::kString);
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    size_t size;
    grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
    EXPECT_EQ(size, 1);
    const auto* value = GetProperty(upb_proto, "optional_key");
    ASSERT_NE(value, nullptr);
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
    LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
    EXPECT_TRUE(json_obj.empty());
    upb::Arena arena;
    auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
    ASSERT_NE(upb_proto, nullptr);
    props.FillUpbProto(upb_proto, arena.ptr());
    size_t size;
    grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
    EXPECT_EQ(size, 0);
  }
}

TEST(PropertyListTest, Merge) {
  PropertyList props1;
  props1.Set("key1", "value1");
  props1.Set("key2", 123);
  PropertyList props2;
  props2.Set("key2", "new_value");
  props2.Set("key3", true);
  props1.Merge(std::move(props2));
  Json::Object json_obj = props1.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 3);
  EXPECT_EQ(json_obj["key1"].string(), "value1");
  EXPECT_EQ(json_obj["key2"].string(), "123");
  EXPECT_EQ(json_obj["key3"].type(), Json::Type::kBoolean);
  EXPECT_EQ(json_obj["key3"].boolean(), true);
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props1.FillUpbProto(upb_proto, arena.ptr());
  size_t size;
  const auto* const* props =
      grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
  EXPECT_EQ(size, 4);
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyList_Element_key(props[0])),
      "key1");
  EXPECT_EQ(UpbStringToStdString(grpc_channelz_v2_PropertyValue_string_value(
                grpc_channelz_v2_PropertyList_Element_value(props[0]))),
            "value1");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyList_Element_key(props[1])),
      "key2");
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_int64_value(
                grpc_channelz_v2_PropertyList_Element_value(props[1])),
            123);
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyList_Element_key(props[2])),
      "key2");
  EXPECT_EQ(UpbStringToStdString(grpc_channelz_v2_PropertyValue_string_value(
                grpc_channelz_v2_PropertyList_Element_value(props[2]))),
            "new_value");
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyList_Element_key(props[3])),
      "key3");
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_bool_value(
                grpc_channelz_v2_PropertyList_Element_value(props[3])),
            true);
}

TEST(PropertyListTest, SetAbslStatus) {
  PropertyList props;
  props.Set("status_key", absl::Status(absl::StatusCode::kUnknown, "error"));
  Json::Object json_obj = props.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 1);
  auto it = json_obj.find("status_key");
  ASSERT_NE(it, json_obj.end());
  EXPECT_EQ(it->second.type(), Json::Type::kString);
  EXPECT_EQ(it->second.string(), "UNKNOWN: error");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  size_t size;
  grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
  EXPECT_EQ(size, 1);
  const auto* value = GetProperty(upb_proto, "status_key");
  ASSERT_NE(value, nullptr);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(value));
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyValue_string_value(value)),
      "UNKNOWN: error");
}

TEST(PropertyListTest, SetUint64) {
  PropertyList props;
  props.Set("uint64_key", static_cast<uint64_t>(123));
  Json::Object json_obj = props.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 1);
  auto it = json_obj.find("uint64_key");
  ASSERT_NE(it, json_obj.end());
  EXPECT_EQ(it->second.type(), Json::Type::kNumber);
  EXPECT_EQ(it->second.string(), "123");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  size_t size;
  grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
  EXPECT_EQ(size, 1);
  const auto* value = GetProperty(upb_proto, "uint64_key");
  ASSERT_NE(value, nullptr);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_uint64_value(value));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_uint64_value(value), 123);
}

TEST(PropertyListTest, NulloptIsNoOp) {
  PropertyList props;
  props.Set("key1", "value1");
  props.Set("key1", std::optional<std::string>(std::nullopt));
  Json::Object json_obj = props.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 1);
  EXPECT_EQ(json_obj["key1"].string(), "value1");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  size_t size;
  grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
  EXPECT_EQ(size, 1);
  const auto* value = GetProperty(upb_proto, "key1");
  ASSERT_NE(value, nullptr);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(value));
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyValue_string_value(value)),
      "value1");
}

TEST(PropertyListTest, NestedPropertyList) {
  PropertyList props;
  PropertyList nested_props;
  nested_props.Set("nested_key", "nested_value");
  props.Set("nested_list", std::move(nested_props));
  Json::Object json_obj = props.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 1);
  auto it = json_obj.find("nested_list");
  ASSERT_NE(it, json_obj.end());
  EXPECT_EQ(it->second.type(), Json::Type::kObject);
  const auto& nested_json_obj = it->second.object();
  ASSERT_EQ(nested_json_obj.size(), 1);
  EXPECT_EQ(nested_json_obj.at("nested_key").string(), "nested_value");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyList_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  props.FillUpbProto(upb_proto, arena.ptr());
  size_t size;
  grpc_channelz_v2_PropertyList_properties(upb_proto, &size);
  EXPECT_EQ(size, 1);
  const auto* value = GetProperty(upb_proto, "nested_list");
  ASSERT_NE(value, nullptr);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_any_value(value));
  const auto* any = grpc_channelz_v2_PropertyValue_any_value(value);
  EXPECT_EQ(UpbStringToStdString(google_protobuf_Any_type_url(any)),
            "type.googleapis.com/grpc.channelz.v2.PropertyList");
}

TEST(PropertyGridTest, EmptyGrid) {
  PropertyGrid grid;
  Json::Object json_obj = grid.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 2);
  EXPECT_TRUE(json_obj.at("columns").array().empty());
  EXPECT_TRUE(json_obj.at("rows").array().empty());
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyGrid_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  grid.FillUpbProto(upb_proto, arena.ptr());
  size_t num_cols;
  grpc_channelz_v2_PropertyGrid_columns(upb_proto, &num_cols);
  EXPECT_EQ(num_cols, 0);
  size_t num_rows;
  grpc_channelz_v2_PropertyGrid_rows(upb_proto, &num_rows);
  EXPECT_EQ(num_rows, 0);
}

TEST(PropertyGridTest, SimpleGrid) {
  PropertyGrid grid;
  grid.Set("col1", "row1", "val1");
  grid.Set("col2", "row1", 123);
  grid.Set("col1", "row2", true);
  grid.Set("col2", "row2", Duration::Seconds(1));

  Json::Object json_obj = grid.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 2);
  auto it_cols = json_obj.find("columns");
  ASSERT_NE(it_cols, json_obj.end());
  auto it_rows = json_obj.find("rows");
  ASSERT_NE(it_rows, json_obj.end());

  const Json::Array& cols = it_cols->second.array();
  ASSERT_EQ(cols.size(), 2);
  const Json::Array& rows = it_rows->second.array();
  ASSERT_EQ(rows.size(), 2);

  EXPECT_EQ(cols[0].string(), "col1");
  EXPECT_EQ(cols[1].string(), "col2");

  const auto& row1_json = rows[0].object();
  const auto& row2_json = rows[1].object();

  // Row 1
  EXPECT_EQ(row1_json.at("name").string(), "row1");
  const Json::Array& row1_values = row1_json.at("cells").array();
  ASSERT_EQ(row1_values.size(), 2);
  EXPECT_EQ(row1_values[0].string(), "val1");
  EXPECT_EQ(row1_values[1].string(), "123");

  // Row 2
  EXPECT_EQ(row2_json.at("name").string(), "row2");
  const Json::Array& row2_values = row2_json.at("cells").array();
  ASSERT_EQ(row2_values.size(), 2);
  EXPECT_EQ(row2_values[0].type(), Json::Type::kBoolean);
  EXPECT_EQ(row2_values[0].boolean(), true);
  EXPECT_EQ(row2_values[1].string(), "1.000000000s");

  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyGrid_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  grid.FillUpbProto(upb_proto, arena.ptr());

  size_t num_cols;
  const upb_StringView* upb_cols =
      grpc_channelz_v2_PropertyGrid_columns(upb_proto, &num_cols);
  EXPECT_EQ(num_cols, 2);
  EXPECT_EQ(UpbStringToStdString(upb_cols[0]), "col1");
  EXPECT_EQ(UpbStringToStdString(upb_cols[1]), "col2");

  size_t num_rows;
  const grpc_channelz_v2_PropertyGrid_Row* const* const upb_rows =
      grpc_channelz_v2_PropertyGrid_rows(upb_proto, &num_rows);
  EXPECT_EQ(num_rows, 2);

  const auto* upb_row1 = upb_rows[0];
  const auto* upb_row2 = upb_rows[1];

  // Row 1
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyGrid_Row_label(upb_row1)),
      "row1");
  size_t num_values1;
  const grpc_channelz_v2_PropertyValue* const* values1 =
      grpc_channelz_v2_PropertyGrid_Row_value(upb_row1, &num_values1);
  ASSERT_EQ(num_values1, 2);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(values1[0]));
  EXPECT_EQ(UpbStringToStdString(
                grpc_channelz_v2_PropertyValue_string_value(values1[0])),
            "val1");
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_int64_value(values1[1]));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_int64_value(values1[1]), 123);

  // Row 2
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyGrid_Row_label(upb_row2)),
      "row2");
  size_t num_values2;
  const grpc_channelz_v2_PropertyValue* const* values2 =
      grpc_channelz_v2_PropertyGrid_Row_value(upb_row2, &num_values2);
  ASSERT_EQ(num_values2, 2);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_bool_value(values2[0]));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_bool_value(values2[0]), true);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_duration_value(values2[1]));
  EXPECT_EQ(google_protobuf_Duration_seconds(
                grpc_channelz_v2_PropertyValue_duration_value(values2[1])),
            1);
}

TEST(PropertyGridTest, SetRow) {
  PropertyGrid grid;
  PropertyList row_props;
  row_props.Set("col1", "val1");
  row_props.Set("col2", 123);
  grid.SetRow("row1", std::move(row_props));
  Json::Object json_obj = grid.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 2);
  const auto& cols = json_obj["columns"].array();
  const auto& rows = json_obj["rows"].array();
  ASSERT_EQ(cols.size(), 2);
  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(cols[0].string(), "col1");
  EXPECT_EQ(cols[1].string(), "col2");
  const auto& row1_json = rows[0].object();
  EXPECT_EQ(row1_json.at("name").string(), "row1");
  const auto& row1_values = row1_json.at("cells").array();
  ASSERT_EQ(row1_values.size(), 2);
  EXPECT_EQ(row1_values[0].string(), "val1");
  EXPECT_EQ(row1_values[1].string(), "123");
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyGrid_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  grid.FillUpbProto(upb_proto, arena.ptr());
  size_t num_cols;
  const upb_StringView* upb_cols =
      grpc_channelz_v2_PropertyGrid_columns(upb_proto, &num_cols);
  EXPECT_EQ(num_cols, 2);
  EXPECT_EQ(UpbStringToStdString(upb_cols[0]), "col1");
  EXPECT_EQ(UpbStringToStdString(upb_cols[1]), "col2");
  size_t num_rows;
  const grpc_channelz_v2_PropertyGrid_Row* const* const upb_rows =
      grpc_channelz_v2_PropertyGrid_rows(upb_proto, &num_rows);
  EXPECT_EQ(num_rows, 1);
  const auto* upb_row1 = upb_rows[0];
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyGrid_Row_label(upb_row1)),
      "row1");
  size_t num_values1;
  const grpc_channelz_v2_PropertyValue* const* values1 =
      grpc_channelz_v2_PropertyGrid_Row_value(upb_row1, &num_values1);
  ASSERT_EQ(num_values1, 2);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(values1[0]));
  EXPECT_EQ(UpbStringToStdString(
                grpc_channelz_v2_PropertyValue_string_value(values1[0])),
            "val1");
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_int64_value(values1[1]));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_int64_value(values1[1]), 123);
}

TEST(PropertyGridTest, SetColumn) {
  PropertyGrid grid;
  PropertyList col_props;
  col_props.Set("row1", "val1");
  col_props.Set("row2", true);
  grid.SetColumn("col1", std::move(col_props));
  Json::Object json_obj = grid.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 2);
  const auto& cols = json_obj["columns"].array();
  const auto& rows = json_obj["rows"].array();
  ASSERT_EQ(cols.size(), 1);
  ASSERT_EQ(rows.size(), 2);
  EXPECT_EQ(cols[0].string(), "col1");
  const auto& row1_json = rows[0].object();
  EXPECT_EQ(row1_json.at("name").string(), "row1");
  const auto& row1_values = row1_json.at("cells").array();
  ASSERT_EQ(row1_values.size(), 1);
  EXPECT_EQ(row1_values[0].string(), "val1");
  const auto& row2_json = rows[1].object();
  EXPECT_EQ(row2_json.at("name").string(), "row2");
  const auto& row2_values = row2_json.at("cells").array();
  ASSERT_EQ(row2_values.size(), 1);
  EXPECT_EQ(row2_values[0].boolean(), true);
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyGrid_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  grid.FillUpbProto(upb_proto, arena.ptr());
  size_t num_cols;
  const upb_StringView* upb_cols =
      grpc_channelz_v2_PropertyGrid_columns(upb_proto, &num_cols);
  EXPECT_EQ(num_cols, 1);
  EXPECT_EQ(UpbStringToStdString(upb_cols[0]), "col1");
  size_t num_rows;
  const grpc_channelz_v2_PropertyGrid_Row* const* const upb_rows =
      grpc_channelz_v2_PropertyGrid_rows(upb_proto, &num_rows);
  EXPECT_EQ(num_rows, 2);
  const auto* upb_row1 = upb_rows[0];
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyGrid_Row_label(upb_row1)),
      "row1");
  size_t num_values1;
  const grpc_channelz_v2_PropertyValue* const* values1 =
      grpc_channelz_v2_PropertyGrid_Row_value(upb_row1, &num_values1);
  ASSERT_EQ(num_values1, 1);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(values1[0]));
  EXPECT_EQ(UpbStringToStdString(
                grpc_channelz_v2_PropertyValue_string_value(values1[0])),
            "val1");
  const auto* upb_row2 = upb_rows[1];
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyGrid_Row_label(upb_row2)),
      "row2");
  size_t num_values2;
  const grpc_channelz_v2_PropertyValue* const* values2 =
      grpc_channelz_v2_PropertyGrid_Row_value(upb_row2, &num_values2);
  ASSERT_EQ(num_values2, 1);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_bool_value(values2[0]));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_bool_value(values2[0]), true);
}

TEST(PropertyGridTest, MissingCells) {
  PropertyGrid grid;
  grid.Set("col1", "row1", "val1");
  grid.Set("col2", "row2", "val2");

  Json::Object json_obj = grid.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 2);
  auto it_cols = json_obj.find("columns");
  ASSERT_NE(it_cols, json_obj.end());
  auto it_rows = json_obj.find("rows");
  ASSERT_NE(it_rows, json_obj.end());

  const Json::Array& cols = it_cols->second.array();
  ASSERT_EQ(cols.size(), 2);
  EXPECT_EQ(cols[0].string(), "col1");
  EXPECT_EQ(cols[1].string(), "col2");

  const Json::Array& rows = it_rows->second.array();
  ASSERT_EQ(rows.size(), 2);

  const auto& row1_json = rows[0].object();
  EXPECT_EQ(row1_json.at("name").string(), "row1");
  const Json::Array& row1_values = row1_json.at("cells").array();
  ASSERT_EQ(row1_values.size(), 2);
  EXPECT_EQ(row1_values[0].string(), "val1");
  EXPECT_EQ(row1_values[1].type(), Json::Type::kNull);

  const auto& row2_json = rows[1].object();
  EXPECT_EQ(row2_json.at("name").string(), "row2");
  const Json::Array& row2_values = row2_json.at("cells").array();
  ASSERT_EQ(row2_values.size(), 2);
  EXPECT_EQ(row2_values[0].type(), Json::Type::kNull);
  EXPECT_EQ(row2_values[1].string(), "val2");

  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyGrid_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  grid.FillUpbProto(upb_proto, arena.ptr());

  size_t num_cols;
  const upb_StringView* upb_cols =
      grpc_channelz_v2_PropertyGrid_columns(upb_proto, &num_cols);
  EXPECT_EQ(num_cols, 2);
  EXPECT_EQ(UpbStringToStdString(upb_cols[0]), "col1");
  EXPECT_EQ(UpbStringToStdString(upb_cols[1]), "col2");

  size_t num_rows;
  const grpc_channelz_v2_PropertyGrid_Row* const* const upb_rows =
      grpc_channelz_v2_PropertyGrid_rows(upb_proto, &num_rows);
  EXPECT_EQ(num_rows, 2);

  const auto* upb_row1 = upb_rows[0];
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyGrid_Row_label(upb_row1)),
      "row1");
  size_t num_values1;
  const grpc_channelz_v2_PropertyValue* const* values1 =
      grpc_channelz_v2_PropertyGrid_Row_value(upb_row1, &num_values1);
  ASSERT_EQ(num_values1, 2);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(values1[0]));
  EXPECT_EQ(UpbStringToStdString(
                grpc_channelz_v2_PropertyValue_string_value(values1[0])),
            "val1");
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_empty_value(values1[1]));

  const auto* upb_row2 = upb_rows[1];
  EXPECT_EQ(
      UpbStringToStdString(grpc_channelz_v2_PropertyGrid_Row_label(upb_row2)),
      "row2");
  size_t num_values2;
  const grpc_channelz_v2_PropertyValue* const* values2 =
      grpc_channelz_v2_PropertyGrid_Row_value(upb_row2, &num_values2);
  ASSERT_EQ(num_values2, 2);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_empty_value(values2[0]));
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(values2[1]));
  EXPECT_EQ(UpbStringToStdString(
                grpc_channelz_v2_PropertyValue_string_value(values2[1])),
            "val2");
}

TEST(PropertyTableTest, EmptyTable) {
  PropertyTable table;
  Json::Object json_obj = table.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 2);
  EXPECT_TRUE(json_obj.at("columns").array().empty());
  EXPECT_TRUE(json_obj.at("rows").array().empty());
  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyTable_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  table.FillUpbProto(upb_proto, arena.ptr());
  size_t num_cols;
  grpc_channelz_v2_PropertyTable_columns(upb_proto, &num_cols);
  EXPECT_EQ(num_cols, 0);
  size_t num_rows;
  grpc_channelz_v2_PropertyTable_rows(upb_proto, &num_rows);
  EXPECT_EQ(num_rows, 0);
}

TEST(PropertyTableTest, SimpleTable) {
  PropertyTable table;
  table.Set("col1", 0, "val1");
  table.Set("col2", 0, 123);
  table.Set("col1", 1, true);
  table.Set("col2", 1, Duration::Seconds(1));

  Json::Object json_obj = table.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 2);
  auto it_cols = json_obj.find("columns");
  ASSERT_NE(it_cols, json_obj.end());
  auto it_rows = json_obj.find("rows");
  ASSERT_NE(it_rows, json_obj.end());

  const Json::Array& cols = it_cols->second.array();
  ASSERT_EQ(cols.size(), 2);
  EXPECT_EQ(cols[0].string(), "col1");
  EXPECT_EQ(cols[1].string(), "col2");

  const Json::Array& rows = it_rows->second.array();
  ASSERT_EQ(rows.size(), 2);

  const Json::Array& row1_values = rows[0].array();
  ASSERT_EQ(row1_values.size(), 2);
  EXPECT_EQ(row1_values[0].string(), "val1");
  EXPECT_EQ(row1_values[1].string(), "123");

  const Json::Array& row2_values = rows[1].array();
  ASSERT_EQ(row2_values.size(), 2);
  EXPECT_EQ(row2_values[0].type(), Json::Type::kBoolean);
  EXPECT_EQ(row2_values[0].boolean(), true);
  EXPECT_EQ(row2_values[1].string(), "1.000000000s");

  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyTable_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  table.FillUpbProto(upb_proto, arena.ptr());

  size_t num_cols;
  const upb_StringView* upb_cols =
      grpc_channelz_v2_PropertyTable_columns(upb_proto, &num_cols);
  EXPECT_EQ(num_cols, 2);
  EXPECT_EQ(UpbStringToStdString(upb_cols[0]), "col1");
  EXPECT_EQ(UpbStringToStdString(upb_cols[1]), "col2");

  size_t num_rows;
  const grpc_channelz_v2_PropertyTable_Row* const* upb_rows =
      grpc_channelz_v2_PropertyTable_rows(upb_proto, &num_rows);
  EXPECT_EQ(num_rows, 2);

  const grpc_channelz_v2_PropertyTable_Row* upb_row1 = upb_rows[0];
  size_t num_values1;
  const grpc_channelz_v2_PropertyValue* const* values1 =
      grpc_channelz_v2_PropertyTable_Row_value(upb_row1, &num_values1);
  ASSERT_EQ(num_values1, 2);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(values1[0]));
  EXPECT_EQ(UpbStringToStdString(
                grpc_channelz_v2_PropertyValue_string_value(values1[0])),
            "val1");
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_int64_value(values1[1]));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_int64_value(values1[1]), 123);

  const grpc_channelz_v2_PropertyTable_Row* upb_row2 = upb_rows[1];
  size_t num_values2;
  const grpc_channelz_v2_PropertyValue* const* values2 =
      grpc_channelz_v2_PropertyTable_Row_value(upb_row2, &num_values2);
  ASSERT_EQ(num_values2, 2);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_bool_value(values2[0]));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_bool_value(values2[0]), true);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_duration_value(values2[1]));
  EXPECT_EQ(google_protobuf_Duration_seconds(
                grpc_channelz_v2_PropertyValue_duration_value(values2[1])),
            1);
}

TEST(PropertyTableTest, AppendRow) {
  PropertyTable table;
  table.Set("col1", 0, "val1");
  table.Set("col2", 0, 123);
  PropertyList row2_props;
  row2_props.Set("col1", true);
  row2_props.Set("col2", Duration::Seconds(1));
  table.AppendRow(std::move(row2_props));

  Json::Object json_obj = table.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));

  upb::Arena arena;

  auto* upb_proto = grpc_channelz_v2_PropertyTable_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  table.FillUpbProto(upb_proto, arena.ptr());

  size_t num_cols;
  const upb_StringView* upb_cols =
      grpc_channelz_v2_PropertyTable_columns(upb_proto, &num_cols);
  EXPECT_EQ(num_cols, 2);
  EXPECT_EQ(UpbStringToStdString(upb_cols[0]), "col1");
  EXPECT_EQ(UpbStringToStdString(upb_cols[1]), "col2");

  size_t num_rows;
  const grpc_channelz_v2_PropertyTable_Row* const* upb_rows =
      grpc_channelz_v2_PropertyTable_rows(upb_proto, &num_rows);
  EXPECT_EQ(num_rows, 2);

  const grpc_channelz_v2_PropertyTable_Row* upb_row1 = upb_rows[0];
  size_t num_values1;
  const grpc_channelz_v2_PropertyValue* const* values1 =
      grpc_channelz_v2_PropertyTable_Row_value(upb_row1, &num_values1);
  ASSERT_EQ(num_values1, 2);
  const auto* val1_col_wise = values1[0];
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(val1_col_wise));
  EXPECT_EQ(UpbStringToStdString(
                grpc_channelz_v2_PropertyValue_string_value(val1_col_wise)),
            "val1");
  const auto* val2_col_wise = values1[1];
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_int64_value(val2_col_wise));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_int64_value(val2_col_wise), 123);

  const grpc_channelz_v2_PropertyTable_Row* upb_row2 = upb_rows[1];
  size_t num_values2;
  const grpc_channelz_v2_PropertyValue* const* values2 =
      grpc_channelz_v2_PropertyTable_Row_value(upb_row2, &num_values2);
  ASSERT_EQ(num_values2, 2);
  const auto* val3_col_wise = values2[0];
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_bool_value(val3_col_wise));
  EXPECT_EQ(grpc_channelz_v2_PropertyValue_bool_value(val3_col_wise), true);
  const auto* val4_col_wise = values2[1];
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_duration_value(val4_col_wise));
  EXPECT_EQ(google_protobuf_Duration_seconds(
                grpc_channelz_v2_PropertyValue_duration_value(val4_col_wise)),
            1);
}

TEST(PropertyTableTest, MissingCells) {
  PropertyTable table;
  table.Set("col1", 0, "val1");
  table.Set("col2", 1, "val2");

  Json::Object json_obj = table.TakeJsonObject();
  LOG(INFO) << "json_obj = " << JsonDump(Json::FromObject(json_obj));
  ASSERT_EQ(json_obj.size(), 2);
  auto it_cols = json_obj.find("columns");
  ASSERT_NE(it_cols, json_obj.end());
  auto it_rows = json_obj.find("rows");
  ASSERT_NE(it_rows, json_obj.end());

  const Json::Array& cols = it_cols->second.array();
  ASSERT_EQ(cols.size(), 2);
  EXPECT_EQ(cols[0].string(), "col1");
  EXPECT_EQ(cols[1].string(), "col2");

  const Json::Array& rows = it_rows->second.array();
  ASSERT_EQ(rows.size(), 2);

  const Json::Array& row1_values = rows[0].array();
  ASSERT_EQ(row1_values.size(), 2);
  EXPECT_EQ(row1_values[0].string(), "val1");
  EXPECT_EQ(row1_values[1].type(), Json::Type::kNull);

  const Json::Array& row2_values = rows[1].array();
  ASSERT_EQ(row2_values.size(), 2);
  EXPECT_EQ(row2_values[0].type(), Json::Type::kNull);
  EXPECT_EQ(row2_values[1].string(), "val2");

  upb::Arena arena;
  auto* upb_proto = grpc_channelz_v2_PropertyTable_new(arena.ptr());
  ASSERT_NE(upb_proto, nullptr);
  table.FillUpbProto(upb_proto, arena.ptr());

  size_t num_cols;
  const upb_StringView* upb_cols =
      grpc_channelz_v2_PropertyTable_columns(upb_proto, &num_cols);
  EXPECT_EQ(num_cols, 2);
  EXPECT_EQ(UpbStringToStdString(upb_cols[0]), "col1");
  EXPECT_EQ(UpbStringToStdString(upb_cols[1]), "col2");

  size_t num_rows;
  const grpc_channelz_v2_PropertyTable_Row* const* upb_rows =
      grpc_channelz_v2_PropertyTable_rows(upb_proto, &num_rows);
  EXPECT_EQ(num_rows, 2);

  const auto* upb_row1 = upb_rows[0];
  size_t num_values1;
  const grpc_channelz_v2_PropertyValue* const* values1 =
      grpc_channelz_v2_PropertyTable_Row_value(upb_row1, &num_values1);
  ASSERT_EQ(num_values1, 2);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(values1[0]));
  EXPECT_EQ(UpbStringToStdString(
                grpc_channelz_v2_PropertyValue_string_value(values1[0])),
            "val1");
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_empty_value(values1[1]));

  const auto* upb_row2 = upb_rows[1];
  size_t num_values2;
  const grpc_channelz_v2_PropertyValue* const* values2 =
      grpc_channelz_v2_PropertyTable_Row_value(upb_row2, &num_values2);
  ASSERT_EQ(num_values2, 2);
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_empty_value(values2[0]));
  EXPECT_TRUE(grpc_channelz_v2_PropertyValue_has_string_value(values2[1]));
  EXPECT_EQ(UpbStringToStdString(
                grpc_channelz_v2_PropertyValue_string_value(values2[1])),
            "val2");
}

}  // namespace channelz
}  // namespace grpc_core
