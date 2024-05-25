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

#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "src/core/util/json/json_reader.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace internal {

namespace {

using grpc_core::LoggingSink;

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigEmpty) {
  const char* json_str = R"json({
      "cloud_logging": {
      }
    })json";
  auto json = grpc_core::JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value(), "test", {});
  // client test
  EXPECT_FALSE(sink.FindMatch(true, "foo", "bar").ShouldLog());
  // server test
  EXPECT_FALSE(sink.FindMatch(false, "foo", "bar").ShouldLog());
}

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigClientWildCardEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "client_rpc_events": [
          {
            "methods": ["*"],
            "max_metadata_bytes": 1024,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value(), "test", {});
  // client test
  EXPECT_EQ(sink.FindMatch(true, "foo", "bar"),
            LoggingSink::Config(1024, 4096));
  // server test
  EXPECT_FALSE(sink.FindMatch(false, "foo", "bar").ShouldLog());
}

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigBadPath) {
  const char* json_str = R"json({
      "cloud_logging": {
        "client_rpc_events": [
          {
            "methods": ["*"],
            "max_metadata_bytes": 1024,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value(), "test", {});
  EXPECT_FALSE(sink.FindMatch(true, "foo", "").ShouldLog());
}

TEST(GcpObservabilityLoggingSinkTest,
     LoggingConfigClientWildCardServiceEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "client_rpc_events": [
          {
            "methods": ["service/*"],
            "max_metadata_bytes": 1024,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value(), "test", {});
  // client test
  EXPECT_EQ(sink.FindMatch(true, "service", "bar"),
            LoggingSink::Config(1024, 4096));
  EXPECT_FALSE(sink.FindMatch(true, "foo", "bar").ShouldLog());
  // server test
  EXPECT_FALSE(sink.FindMatch(false, "service", "bar").ShouldLog());
  EXPECT_FALSE(sink.FindMatch(false, "foo", "bar").ShouldLog());
}

TEST(GcpObservabilityLoggingSinkTest,
     LoggingConfigClientMultipleMethodEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "client_rpc_events": [
          {
            "methods": ["foo/bar", "foo/baz"],
            "max_metadata_bytes": 1024,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value(), "test", {});
  // client test
  EXPECT_EQ(sink.FindMatch(true, "foo", "bar"),
            LoggingSink::Config(1024, 4096));
  EXPECT_EQ(sink.FindMatch(true, "foo", "baz"),
            LoggingSink::Config(1024, 4096));
  // server test
  EXPECT_FALSE(sink.FindMatch(false, "foo", "bar").ShouldLog());
  EXPECT_FALSE(sink.FindMatch(false, "foo", "baz").ShouldLog());
}

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigClientMultipleEventEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "client_rpc_events": [
          {
            "methods": ["foo/bar"],
            "max_metadata_bytes": 1024,
            "max_message_bytes": 4096
          },
          {
            "methods": ["foo/baz"],
            "max_metadata_bytes": 512,
            "max_message_bytes": 2048
          }
        ]
      }
    })json";
  auto json = grpc_core::JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value(), "test", {});
  // client test
  EXPECT_EQ(sink.FindMatch(true, "foo", "bar"),
            LoggingSink::Config(1024, 4096));
  EXPECT_EQ(sink.FindMatch(true, "foo", "baz"), LoggingSink::Config(512, 2048));
  // server test
  EXPECT_FALSE(sink.FindMatch(false, "foo", "bar").ShouldLog());
  EXPECT_FALSE(sink.FindMatch(false, "foo", "baz").ShouldLog());
}

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigServerWildCardEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "server_rpc_events": [
          {
            "methods": ["*"],
            "max_metadata_bytes": 1024,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value(), "test", {});
  // client test
  EXPECT_FALSE(sink.FindMatch(true, "foo", "bar").ShouldLog());
  // server test
  EXPECT_EQ(sink.FindMatch(false, "foo", "bar"),
            LoggingSink::Config(1024, 4096));
}

TEST(GcpObservabilityLoggingSinkTest,
     LoggingConfigServerWildCardServiceEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "server_rpc_events": [
          {
            "methods": ["service/*"],
            "max_metadata_bytes": 1024,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value(), "test", {});
  // client test
  EXPECT_FALSE(sink.FindMatch(true, "service", "bar").ShouldLog());
  EXPECT_FALSE(sink.FindMatch(true, "foo", "bar").ShouldLog());
  // server test
  EXPECT_EQ(sink.FindMatch(false, "service", "bar"),
            LoggingSink::Config(1024, 4096));
  EXPECT_FALSE(sink.FindMatch(false, "foo", "bar").ShouldLog());
}

TEST(GcpObservabilityLoggingSinkTest,
     LoggingConfigServerMultipleMethodEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "server_rpc_events": [
          {
            "methods": ["foo/bar", "foo/baz"],
            "max_metadata_bytes": 1024,
            "max_message_bytes": 4096
          }
        ]
      }
    })json";
  auto json = grpc_core::JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value(), "test", {});
  // client test
  EXPECT_FALSE(sink.FindMatch(true, "foo", "bar").ShouldLog());
  EXPECT_FALSE(sink.FindMatch(true, "foo", "baz").ShouldLog());
  // server test
  EXPECT_EQ(sink.FindMatch(false, "foo", "bar"),
            LoggingSink::Config(1024, 4096));
  EXPECT_EQ(sink.FindMatch(false, "foo", "baz"),
            LoggingSink::Config(1024, 4096));
}

TEST(GcpObservabilityLoggingSinkTest, LoggingConfigServerMultipleEventEntries) {
  const char* json_str = R"json({
      "cloud_logging": {
        "server_rpc_events": [
          {
            "methods": ["foo/bar"],
            "max_metadata_bytes": 1024,
            "max_message_bytes": 4096
          },
          {
            "methods": ["foo/baz"],
            "max_metadata_bytes": 512,
            "max_message_bytes": 2048
          }
        ]
      }
    })json";
  auto json = grpc_core::JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ValidationErrors errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ObservabilityLoggingSink sink(config.cloud_logging.value(), "test", {});
  // client test
  EXPECT_FALSE(sink.FindMatch(true, "foo", "bar").ShouldLog());
  EXPECT_FALSE(sink.FindMatch(true, "foo", "baz").ShouldLog());
  // server test
  EXPECT_EQ(sink.FindMatch(false, "foo", "bar"),
            LoggingSink::Config(1024, 4096));
  EXPECT_EQ(sink.FindMatch(false, "foo", "baz"),
            LoggingSink::Config(512, 2048));
}

TEST(EntryToJsonStructTest, ClientHeader) {
  LoggingSink::Entry entry;
  entry.call_id = 1234;
  entry.sequence_id = 1;
  entry.type = LoggingSink::Entry::EventType::kClientHeader;
  entry.payload.metadata["key"] = "value";
  entry.payload.timeout = grpc_core::Duration::Seconds(100);
  entry.payload_truncated = true;
  entry.peer.type = LoggingSink::Entry::Address::Type::kIpv4;
  entry.peer.address = "127.0.0.1";
  entry.peer.ip_port = 12345;
  entry.authority = "authority";
  entry.service_name = "service_name";
  entry.method_name = "method_name";

  google::protobuf::Struct proto;
  EntryToJsonStructProto(std::move(entry), &proto);
  std::string output;
  ::google::protobuf::TextFormat::PrintToString(proto, &output);
  const char* pb_str =
      "fields {\n"
      "  key: \"authority\"\n"
      "  value {\n"
      "    string_value: \"authority\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"callId\"\n"
      "  value {\n"
      "    string_value: \"00000000-0000-4000-8000-0000000004d2\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"logger\"\n"
      "  value {\n"
      "    string_value: \"LOGGER_UNKNOWN\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"methodName\"\n"
      "  value {\n"
      "    string_value: \"method_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"payload\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"metadata\"\n"
      "        value {\n"
      "          struct_value {\n"
      "            fields {\n"
      "              key: \"key\"\n"
      "              value {\n"
      "                string_value: \"value\"\n"
      "              }\n"
      "            }\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"timeout\"\n"
      "        value {\n"
      "          string_value: \"100.000000000s\"\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"payloadTruncated\"\n"
      "  value {\n"
      "    bool_value: true\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"peer\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"address\"\n"
      "        value {\n"
      "          string_value: \"127.0.0.1\"\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"ipPort\"\n"
      "        value {\n"
      "          number_value: 12345\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"type\"\n"
      "        value {\n"
      "          string_value: \"TYPE_IPV4\"\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"sequenceId\"\n"
      "  value {\n"
      "    number_value: 1\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"serviceName\"\n"
      "  value {\n"
      "    string_value: \"service_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"type\"\n"
      "  value {\n"
      "    string_value: \"CLIENT_HEADER\"\n"
      "  }\n"
      "}\n";
  EXPECT_EQ(output, pb_str);
}

TEST(EntryToJsonStructTest, ServerHeader) {
  LoggingSink::Entry entry;
  entry.call_id = 1234;
  entry.sequence_id = 2;
  entry.type = LoggingSink::Entry::EventType::kServerHeader;
  entry.logger = LoggingSink::Entry::Logger::kServer;
  entry.payload.metadata["key"] = "value";
  entry.peer.type = LoggingSink::Entry::Address::Type::kIpv4;
  entry.peer.address = "127.0.0.1";
  entry.peer.ip_port = 1234;
  entry.authority = "authority";
  entry.service_name = "service_name";
  entry.method_name = "method_name";

  google::protobuf::Struct proto;
  EntryToJsonStructProto(std::move(entry), &proto);
  std::string output;
  ::google::protobuf::TextFormat::PrintToString(proto, &output);
  const char* pb_str =
      "fields {\n"
      "  key: \"authority\"\n"
      "  value {\n"
      "    string_value: \"authority\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"callId\"\n"
      "  value {\n"
      "    string_value: \"00000000-0000-4000-8000-0000000004d2\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"logger\"\n"
      "  value {\n"
      "    string_value: \"SERVER\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"methodName\"\n"
      "  value {\n"
      "    string_value: \"method_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"payload\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"metadata\"\n"
      "        value {\n"
      "          struct_value {\n"
      "            fields {\n"
      "              key: \"key\"\n"
      "              value {\n"
      "                string_value: \"value\"\n"
      "              }\n"
      "            }\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"peer\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"address\"\n"
      "        value {\n"
      "          string_value: \"127.0.0.1\"\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"ipPort\"\n"
      "        value {\n"
      "          number_value: 1234\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"type\"\n"
      "        value {\n"
      "          string_value: \"TYPE_IPV4\"\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"sequenceId\"\n"
      "  value {\n"
      "    number_value: 2\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"serviceName\"\n"
      "  value {\n"
      "    string_value: \"service_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"type\"\n"
      "  value {\n"
      "    string_value: \"SERVER_HEADER\"\n"
      "  }\n"
      "}\n";
  EXPECT_EQ(output, pb_str);
}

TEST(EntryToJsonStructTest, ClientMessage) {
  LoggingSink::Entry entry;
  entry.call_id = 1234;
  entry.sequence_id = 3;
  entry.type = LoggingSink::Entry::EventType::kClientMessage;
  entry.logger = LoggingSink::Entry::Logger::kClient;
  entry.payload.message = "hello";
  entry.payload.message_length = 5;
  entry.peer.type = LoggingSink::Entry::Address::Type::kIpv4;
  entry.peer.address = "127.0.0.1";
  entry.peer.ip_port = 1234;
  entry.authority = "authority";
  entry.service_name = "service_name";
  entry.method_name = "method_name";

  google::protobuf::Struct proto;
  EntryToJsonStructProto(std::move(entry), &proto);
  std::string output;
  ::google::protobuf::TextFormat::PrintToString(proto, &output);
  std::string pb_str = absl::StrFormat(
      "fields {\n"
      "  key: \"authority\"\n"
      "  value {\n"
      "    string_value: \"authority\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"callId\"\n"
      "  value {\n"
      "    string_value: \"00000000-0000-4000-8000-0000000004d2\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"logger\"\n"
      "  value {\n"
      "    string_value: \"CLIENT\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"methodName\"\n"
      "  value {\n"
      "    string_value: \"method_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"payload\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"message\"\n"
      "        value {\n"
      "          string_value: \"%s\"\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"messageLength\"\n"
      "        value {\n"
      "          number_value: 5\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"peer\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"address\"\n"
      "        value {\n"
      "          string_value: \"127.0.0.1\"\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"ipPort\"\n"
      "        value {\n"
      "          number_value: 1234\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"type\"\n"
      "        value {\n"
      "          string_value: \"TYPE_IPV4\"\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"sequenceId\"\n"
      "  value {\n"
      "    number_value: 3\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"serviceName\"\n"
      "  value {\n"
      "    string_value: \"service_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"type\"\n"
      "  value {\n"
      "    string_value: \"CLIENT_MESSAGE\"\n"
      "  }\n"
      "}\n",
      absl::Base64Escape("hello"));
  EXPECT_EQ(output, pb_str);
}

TEST(EntryToJsonStructTest, ServerMessage) {
  LoggingSink::Entry entry;
  entry.call_id = 1234;
  entry.sequence_id = 4;
  entry.type = LoggingSink::Entry::EventType::kServerMessage;
  entry.logger = LoggingSink::Entry::Logger::kServer;
  entry.payload.message = "world";
  entry.payload.message_length = 5;
  entry.peer.type = LoggingSink::Entry::Address::Type::kIpv4;
  entry.peer.address = "127.0.0.1";
  entry.peer.ip_port = 12345;
  entry.authority = "authority";
  entry.service_name = "service_name";
  entry.method_name = "method_name";

  google::protobuf::Struct proto;
  EntryToJsonStructProto(std::move(entry), &proto);
  std::string output;
  ::google::protobuf::TextFormat::PrintToString(proto, &output);
  std::string pb_str = absl::StrFormat(
      "fields {\n"
      "  key: \"authority\"\n"
      "  value {\n"
      "    string_value: \"authority\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"callId\"\n"
      "  value {\n"
      "    string_value: \"00000000-0000-4000-8000-0000000004d2\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"logger\"\n"
      "  value {\n"
      "    string_value: \"SERVER\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"methodName\"\n"
      "  value {\n"
      "    string_value: \"method_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"payload\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"message\"\n"
      "        value {\n"
      "          string_value: \"%s\"\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"messageLength\"\n"
      "        value {\n"
      "          number_value: 5\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"peer\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"address\"\n"
      "        value {\n"
      "          string_value: \"127.0.0.1\"\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"ipPort\"\n"
      "        value {\n"
      "          number_value: 12345\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"type\"\n"
      "        value {\n"
      "          string_value: \"TYPE_IPV4\"\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"sequenceId\"\n"
      "  value {\n"
      "    number_value: 4\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"serviceName\"\n"
      "  value {\n"
      "    string_value: \"service_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"type\"\n"
      "  value {\n"
      "    string_value: \"SERVER_MESSAGE\"\n"
      "  }\n"
      "}\n",
      absl::Base64Escape("world"));
  EXPECT_EQ(output, pb_str);
}

TEST(EntryToJsonStructTest, ClientHalfClose) {
  LoggingSink::Entry entry;
  entry.call_id = 1234;
  entry.sequence_id = 5;
  entry.type = LoggingSink::Entry::EventType::kClientHalfClose;
  entry.logger = LoggingSink::Entry::Logger::kClient;
  entry.peer.type = LoggingSink::Entry::Address::Type::kIpv4;
  entry.peer.address = "127.0.0.1";
  entry.peer.ip_port = 1234;
  entry.authority = "authority";
  entry.service_name = "service_name";
  entry.method_name = "method_name";

  google::protobuf::Struct proto;
  EntryToJsonStructProto(std::move(entry), &proto);
  std::string output;
  ::google::protobuf::TextFormat::PrintToString(proto, &output);
  const char* pb_str =
      "fields {\n"
      "  key: \"authority\"\n"
      "  value {\n"
      "    string_value: \"authority\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"callId\"\n"
      "  value {\n"
      "    string_value: \"00000000-0000-4000-8000-0000000004d2\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"logger\"\n"
      "  value {\n"
      "    string_value: \"CLIENT\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"methodName\"\n"
      "  value {\n"
      "    string_value: \"method_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"payload\"\n"
      "  value {\n"
      "    struct_value {\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"peer\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"address\"\n"
      "        value {\n"
      "          string_value: \"127.0.0.1\"\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"ipPort\"\n"
      "        value {\n"
      "          number_value: 1234\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"type\"\n"
      "        value {\n"
      "          string_value: \"TYPE_IPV4\"\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"sequenceId\"\n"
      "  value {\n"
      "    number_value: 5\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"serviceName\"\n"
      "  value {\n"
      "    string_value: \"service_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"type\"\n"
      "  value {\n"
      "    string_value: \"CLIENT_HALF_CLOSE\"\n"
      "  }\n"
      "}\n";
  EXPECT_EQ(output, pb_str);
}

TEST(EntryToJsonStructTest, ServerTrailer) {
  LoggingSink::Entry entry;
  entry.call_id = 1234;
  entry.sequence_id = 6;
  entry.type = LoggingSink::Entry::EventType::kServerTrailer;
  entry.logger = LoggingSink::Entry::Logger::kServer;
  entry.payload.metadata["key"] = "value";
  entry.peer.type = LoggingSink::Entry::Address::Type::kIpv4;
  entry.peer.address = "127.0.0.1";
  entry.peer.ip_port = 1234;
  entry.authority = "authority";
  entry.service_name = "service_name";
  entry.method_name = "method_name";

  google::protobuf::Struct proto;
  EntryToJsonStructProto(std::move(entry), &proto);
  std::string output;
  ::google::protobuf::TextFormat::PrintToString(proto, &output);
  const char* pb_str =
      "fields {\n"
      "  key: \"authority\"\n"
      "  value {\n"
      "    string_value: \"authority\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"callId\"\n"
      "  value {\n"
      "    string_value: \"00000000-0000-4000-8000-0000000004d2\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"logger\"\n"
      "  value {\n"
      "    string_value: \"SERVER\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"methodName\"\n"
      "  value {\n"
      "    string_value: \"method_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"payload\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"metadata\"\n"
      "        value {\n"
      "          struct_value {\n"
      "            fields {\n"
      "              key: \"key\"\n"
      "              value {\n"
      "                string_value: \"value\"\n"
      "              }\n"
      "            }\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"peer\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"address\"\n"
      "        value {\n"
      "          string_value: \"127.0.0.1\"\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"ipPort\"\n"
      "        value {\n"
      "          number_value: 1234\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"type\"\n"
      "        value {\n"
      "          string_value: \"TYPE_IPV4\"\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"sequenceId\"\n"
      "  value {\n"
      "    number_value: 6\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"serviceName\"\n"
      "  value {\n"
      "    string_value: \"service_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"type\"\n"
      "  value {\n"
      "    string_value: \"SERVER_TRAILER\"\n"
      "  }\n"
      "}\n";
  EXPECT_EQ(output, pb_str);
}

TEST(EntryToJsonStructTest, Cancel) {
  LoggingSink::Entry entry;
  entry.call_id = 1234;
  entry.sequence_id = 7;
  entry.type = LoggingSink::Entry::EventType::kCancel;
  entry.logger = LoggingSink::Entry::Logger::kClient;
  entry.peer.type = LoggingSink::Entry::Address::Type::kIpv4;
  entry.peer.address = "127.0.0.1";
  entry.peer.ip_port = 1234;
  entry.authority = "authority";
  entry.service_name = "service_name";
  entry.method_name = "method_name";

  google::protobuf::Struct proto;
  EntryToJsonStructProto(std::move(entry), &proto);
  std::string output;
  ::google::protobuf::TextFormat::PrintToString(proto, &output);
  const char* pb_str =
      "fields {\n"
      "  key: \"authority\"\n"
      "  value {\n"
      "    string_value: \"authority\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"callId\"\n"
      "  value {\n"
      "    string_value: \"00000000-0000-4000-8000-0000000004d2\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"logger\"\n"
      "  value {\n"
      "    string_value: \"CLIENT\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"methodName\"\n"
      "  value {\n"
      "    string_value: \"method_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"payload\"\n"
      "  value {\n"
      "    struct_value {\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"peer\"\n"
      "  value {\n"
      "    struct_value {\n"
      "      fields {\n"
      "        key: \"address\"\n"
      "        value {\n"
      "          string_value: \"127.0.0.1\"\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"ipPort\"\n"
      "        value {\n"
      "          number_value: 1234\n"
      "        }\n"
      "      }\n"
      "      fields {\n"
      "        key: \"type\"\n"
      "        value {\n"
      "          string_value: \"TYPE_IPV4\"\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"sequenceId\"\n"
      "  value {\n"
      "    number_value: 7\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"serviceName\"\n"
      "  value {\n"
      "    string_value: \"service_name\"\n"
      "  }\n"
      "}\n"
      "fields {\n"
      "  key: \"type\"\n"
      "  value {\n"
      "    string_value: \"CANCEL\"\n"
      "  }\n"
      "}\n";
  EXPECT_EQ(output, pb_str);
}

}  // namespace

}  // namespace internal
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
