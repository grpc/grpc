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
#include "gtest/gtest.h"
#include "src/core/util/json/json.h"

namespace grpc_core {
namespace channelz {

TEST(PropertyListTest, EmptyList) {
  PropertyList props;
  Json::Object json_obj = props.TakeJsonObject();
  EXPECT_TRUE(json_obj.empty());
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
}

TEST(PropertyListTest, SetJsonObject) {
  PropertyList props;
  Json::Object inner_obj;
  inner_obj["inner_key"] = Json::FromString("inner_value");
  props.Set("obj_key", std::move(inner_obj));
  Json::Object json_obj = props.TakeJsonObject();
  ASSERT_EQ(json_obj.size(), 1);
  auto it_obj = json_obj.find("obj_key");
  ASSERT_NE(it_obj, json_obj.end());
  ASSERT_EQ(it_obj->second.type(), Json::Type::kObject);
  const auto& retrieved_inner_obj = it_obj->second.object();
  ASSERT_EQ(retrieved_inner_obj.size(), 1);
  EXPECT_EQ(retrieved_inner_obj.at("inner_key").string(), "inner_value");
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
  EXPECT_THAT(it->second.string(),
              ::testing::StartsWith("2023-01-01T00:00:00.0"));
}

}  // namespace channelz
}  // namespace grpc_core