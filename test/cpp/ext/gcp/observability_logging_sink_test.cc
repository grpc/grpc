//
// Copyright 2022 gRPC authors.
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

#include "src/cpp/ext/gcp/observability_logging_sink.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "test/core/util/test_config.h"

namespace grpc {
namespace internal {

namespace {

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigEmpty) {
  const char* json_str = R"json({
      "cloud_logging": {
      }
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status("unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value());
  // client test
  EXPECT_EQ(sink.Parse(true, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
  // server test
  EXPECT_EQ(sink.Parse(false, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
}

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigClientWildCardEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "client_rpc_events": [
          {
            "methods": ["*"],
            "max_metadata_bytes": 4096,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status("unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value());
  // client test
  EXPECT_EQ(sink.Parse(true, "foo/bar"), LoggingSink::ParsedConfig(4096, 4096));
  // server test
  EXPECT_EQ(sink.Parse(false, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
}

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigBadPath) {
  const char* json_str = R"json({
      "cloud_logging": {
        "client_rpc_events": [
          {
            "methods": ["*"],
            "max_metadata_bytes": 4096,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status("unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value());
  EXPECT_EQ(sink.Parse(true, "foo"), LoggingSink::ParsedConfig(0, 0));
}

TEST(GcpObservabilityLoggingSinkTest,
     LoggingConfigClientWildCardServiceEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "client_rpc_events": [
          {
            "methods": ["service/*"],
            "max_metadata_bytes": 4096,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status("unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value());
  // client test
  EXPECT_EQ(sink.Parse(true, "service/bar"),
            LoggingSink::ParsedConfig(4096, 4096));
  EXPECT_EQ(sink.Parse(true, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
  // server test
  EXPECT_EQ(sink.Parse(false, "service/bar"), LoggingSink::ParsedConfig(0, 0));
  EXPECT_EQ(sink.Parse(false, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
}

TEST(GcpObservabilityLoggingSinkTest,
     LoggingConfigClientMultipleMethodEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "client_rpc_events": [
          {
            "methods": ["foo/bar", "foo/baz"],
            "max_metadata_bytes": 4096,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status("unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value());
  // client test
  EXPECT_EQ(sink.Parse(true, "foo/bar"), LoggingSink::ParsedConfig(4096, 4096));
  EXPECT_EQ(sink.Parse(true, "foo/baz"), LoggingSink::ParsedConfig(4096, 4096));
  // server test
  EXPECT_EQ(sink.Parse(false, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
  EXPECT_EQ(sink.Parse(false, "foo/baz"), LoggingSink::ParsedConfig(0, 0));
}

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigClientMultipleEventEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "client_rpc_events": [
          {
            "methods": ["foo/bar"],
            "max_metadata_bytes": 4096,
            "max_message_bytes": 4096
          },
          {
            "methods": ["foo/baz"],
            "max_metadata_bytes": 2048,
            "max_message_bytes": 2048
          }
        ]
      }
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status("unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value());
  // client test
  EXPECT_EQ(sink.Parse(true, "foo/bar"), LoggingSink::ParsedConfig(4096, 4096));
  EXPECT_EQ(sink.Parse(true, "foo/baz"), LoggingSink::ParsedConfig(2048, 2048));
  // server test
  EXPECT_EQ(sink.Parse(false, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
  EXPECT_EQ(sink.Parse(false, "foo/baz"), LoggingSink::ParsedConfig(0, 0));
}

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigServerWildCardEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "server_rpc_events": [
          {
            "methods": ["*"],
            "max_metadata_bytes": 4096,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status("unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value());
  // client test
  EXPECT_EQ(sink.Parse(true, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
  // server test
  EXPECT_EQ(sink.Parse(false, "foo/bar"),
            LoggingSink::ParsedConfig(4096, 4096));
}

TEST(GcpObservabilityLoggingSinkTest,
     LoggingConfigServerWildCardServiceEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "server_rpc_events": [
          {
            "methods": ["service/*"],
            "max_metadata_bytes": 4096,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status("unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value());
  // client test
  EXPECT_EQ(sink.Parse(true, "service/bar"), LoggingSink::ParsedConfig(0, 0));
  EXPECT_EQ(sink.Parse(true, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
  // server test
  EXPECT_EQ(sink.Parse(false, "service/bar"),
            LoggingSink::ParsedConfig(4096, 4096));
  EXPECT_EQ(sink.Parse(false, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
}

TEST(GcpObservabilityLoggingSinkTest,
     LoggingConfigServerMultipleMethodEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "server_rpc_events": [
          {
            "methods": ["foo/bar", "foo/baz"],
            "max_metadata_bytes": 4096,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status("unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value());
  // client test
  EXPECT_EQ(sink.Parse(true, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
  EXPECT_EQ(sink.Parse(true, "foo/baz"), LoggingSink::ParsedConfig(0, 0));
  // server test
  EXPECT_EQ(sink.Parse(false, "foo/bar"),
            LoggingSink::ParsedConfig(4096, 4096));
  EXPECT_EQ(sink.Parse(false, "foo/baz"),
            LoggingSink::ParsedConfig(4096, 4096));
}

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigServerMultipleEventEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "server_rpc_events": [
          {
            "methods": ["foo/bar"],
            "max_metadata_bytes": 4096,
            "max_message_bytes": 4096
          },
          {
            "methods": ["foo/baz"],
            "max_metadata_bytes": 2048,
            "max_message_bytes": 2048
          }
        ]
      }
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status("unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value());
  // client test
  EXPECT_EQ(sink.Parse(true, "foo/bar"), LoggingSink::ParsedConfig(0, 0));
  EXPECT_EQ(sink.Parse(true, "foo/baz"), LoggingSink::ParsedConfig(0, 0));
  // server test
  EXPECT_EQ(sink.Parse(false, "foo/bar"),
            LoggingSink::ParsedConfig(4096, 4096));
  EXPECT_EQ(sink.Parse(false, "foo/baz"),
            LoggingSink::ParsedConfig(2048, 2048));
}

}  // namespace

}  // namespace internal
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
