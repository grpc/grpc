// Copyright 2021 The gRPC Authors
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

#include "grpc/event_engine/endpoint_config.h"

#include <gmock/gmock.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "test/core/util/test_config.h"

using ::grpc_event_engine::experimental::EndpointConfig;
using namespace ::testing;

TEST(EndpointConfigTest, InsertsDefaultOnOperatorAtAccess) {
  // See https://en.cppreference.com/w/cpp/container/map/operator_at for map
  // operator[] behavior.
  // See https://en.cppreference.com/w/cpp/utility/variant for variant
  // default initialization.
  EndpointConfig config;
  EXPECT_EQ(config["arst"].index(), 0);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(config["arst"]));
}

TEST(EndpointConfigTest, CanStoreAndRetrieveValues) {
  EndpointConfig config;
  config["arst"] = EndpointConfig::IntType(1);
  EXPECT_EQ(absl::get<EndpointConfig::IntType>(config["arst"]).val(), 1);
}

TEST(EndpointConfigTest, CanClear) {
  EndpointConfig config;
  config["arst"] = EndpointConfig::IntType(9);
  config["qwfp"] = EndpointConfig::StrType("some string");
  config["questionable"] = EndpointConfig::PtrType(&config);
  EXPECT_EQ(config.size(), 3);
  config.clear();
  EXPECT_EQ(config.size(), 0);
  auto s = config["arst"];
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(s));
  EXPECT_EQ(config.size(), 1);
}

TEST(EndpointConfigTest, EnumerationVisitsAllSettings) {
  EndpointConfig config;
  config["arst"] = EndpointConfig::IntType(1);
  config["qwfp"] = EndpointConfig::StrType("two");
  config["zxcv"] = EndpointConfig::IntType(3);
  config["oien"] = EndpointConfig::StrType("four");
  MockFunction<bool(absl::string_view, const EndpointConfig::Setting&)> cb;
  EXPECT_CALL(cb, Call(_, _)).Times(4).WillRepeatedly(Return(true));
  config.enumerate(cb.AsStdFunction());
}

TEST(EndpointConfigTest, CanStopEnumerationViaCallbackReturnValue) {
  // Only two of the values should be seen before enumeration is halted.
  const int N = 2;
  EndpointConfig config;
  config["arst"] = EndpointConfig::IntType(1);
  config["qwfp"] = EndpointConfig::StrType("two");
  config["questionable"] = EndpointConfig::PtrType(&config);
  EXPECT_EQ(config.size(), 3);
  int cnt = 0;
  config.enumerate(
      [&cnt](absl::string_view key, const EndpointConfig::Setting& setting) {
        // printf("DO NOT SUBMIT: visited %s\n", key.data());
        return ++cnt != N;
      });
  EXPECT_EQ(cnt, N);
}

TEST(EndpointConfigTest, WillOverwritePreviousValues) {
  EndpointConfig config;
  config["arst"] = EndpointConfig::IntType(1);
  EXPECT_EQ(absl::get<EndpointConfig::IntType>(config["arst"]).val(), 1);
  config["arst"] = EndpointConfig::StrType("three");
  EXPECT_TRUE(absl::holds_alternative<EndpointConfig::StrType>(config["arst"]));
  EXPECT_EQ(absl::get<EndpointConfig::StrType>(config["arst"]).val(), "three");
}

TEST(EndpointConfigTest, CanCheckIfConfigContainsSettingWithoutAddingIt) {
  EndpointConfig config;
  EXPECT_FALSE(config.contains("arst"));
  EXPECT_EQ(config.size(), 0);
}

TEST(EndpointConfigTest, ContainsWorksAsExpected) {
  EndpointConfig config;
  EXPECT_FALSE(config.contains("arst"));
  config["arst"] = EndpointConfig::IntType(1);
  EXPECT_TRUE(config.contains("arst"));
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
