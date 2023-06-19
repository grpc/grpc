//
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
//

#include <chrono>
#include <thread>  // NOLINT

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

#include <grpc++/grpc++.h>
#include <grpcpp/support/status.h>

#include "src/core/ext/filters/logging/logging_filter.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/cpp/ext/gcp/observability_logging_sink.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/ext/filters/logging/library.h"

namespace grpc {
namespace testing {

namespace {

using grpc_core::LoggingSink;

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

TEST_F(LoggingTest, SimpleRpc) {
  g_test_logging_sink->SetConfig(LoggingSink::Config(4096, 4096));
  EchoRequest request;
  request.set_message("foo");
  request.mutable_param()->set_echo_metadata_initially(true);
  request.mutable_param()->set_echo_metadata(true);
  EchoResponse response;
  grpc::ClientContext context;
  context.AddMetadata("key", "value");
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  g_test_logging_sink->WaitForNumEntries(12, absl::Seconds(5));
  EXPECT_THAT(
      g_test_logging_sink->entries(),
      ::testing::UnorderedElementsAre(
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(12)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\x0a\x03\x66\x6f\x6f\x12\x05\x20\x01\x88"
                                     "\x01\x01"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(12)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\x0a\x03\x66\x6f\x6f\x12\x05\x20\x01\x88"
                                     "\x01\x01"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value")))))));
}

TEST_F(LoggingTest, SimpleRpcNoMetadata) {
  g_test_logging_sink->SetConfig(LoggingSink::Config(4096, 4096));
  EchoRequest request;
  request.set_message("foo");
  request.mutable_param()->set_echo_metadata_initially(true);
  request.mutable_param()->set_echo_metadata(true);
  EchoResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  g_test_logging_sink->WaitForNumEntries(12, absl::Seconds(5));
  EXPECT_THAT(
      g_test_logging_sink->entries(),
      ::testing::UnorderedElementsAre(
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre()))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(12)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\x0a\x03\x66\x6f\x6f\x12\x05\x20\x01\x88"
                                     "\x01\x01"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre()))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre()))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre()))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(12)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\x0a\x03\x66\x6f\x6f\x12\x05\x20\x01\x88"
                                     "\x01\x01"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre()))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre())))));
}

TEST_F(LoggingTest, LoggingDisabled) {
  g_test_logging_sink->SetConfig(LoggingSink::Config());
  EchoRequest request;
  request.set_message("foo");
  request.mutable_param()->set_echo_metadata_initially(true);
  request.mutable_param()->set_echo_metadata(true);
  EchoResponse response;
  grpc::ClientContext context;
  context.AddMetadata("key", "value");
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(g_test_logging_sink->entries().empty());
}

TEST_F(LoggingTest, MetadataTruncated) {
  g_test_logging_sink->SetConfig(
      LoggingSink::Config(10 /* expect truncated metadata*/, 4096));
  EchoRequest request;
  request.set_message("foo");
  request.mutable_param()->set_echo_metadata_initially(true);
  request.mutable_param()->set_echo_metadata(true);
  EchoResponse response;
  grpc::ClientContext context;
  context.AddMetadata("key", "value");
  context.AddMetadata("key-2", "value-2");
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  g_test_logging_sink->WaitForNumEntries(12, absl::Seconds(5));
  EXPECT_THAT(
      g_test_logging_sink->entries(),
      ::testing::UnorderedElementsAre(
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(12)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\x0a\x03\x66\x6f\x6f\x12\x05\x20\x01\x88"
                                     "\x01\x01"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(12)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\x0a\x03\x66\x6f\x6f\x12\x05\x20\x01\x88"
                                     "\x01\x01"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair("key", "value")))))));
}

TEST_F(LoggingTest, PayloadTruncated) {
  g_test_logging_sink->SetConfig(LoggingSink::Config(4096, 10));
  EchoRequest request;
  // The following message should get truncated
  request.set_message("Hello World");
  EchoResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  g_test_logging_sink->WaitForNumEntries(12, absl::Seconds(5));
  EXPECT_THAT(
      g_test_logging_sink->entries(),
      ::testing::UnorderedElementsAre(
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre()))),
          AllOf(
              Field(&LoggingSink::Entry::type,
                    Eq(LoggingSink::Entry::EventType::kClientMessage)),
              Field(&LoggingSink::Entry::logger,
                    Eq(LoggingSink::Entry::Logger::kClient)),
              Field(&LoggingSink::Entry::authority, Eq(server_address_)),
              Field(&LoggingSink::Entry::service_name,
                    Eq("grpc.testing.EchoTestService")),
              Field(&LoggingSink::Entry::method_name, Eq("Echo")),
              Field(&LoggingSink::Entry::payload,
                    AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                Eq(13)),
                          Field(&LoggingSink::Entry::Payload::message,
                                Eq("\n\013Hello Wo") /* truncated message */))),
              Field(&LoggingSink::Entry::payload_truncated, true)),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre()))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(13)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\013Hello Wo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre()))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre()))),
          AllOf(
              Field(&LoggingSink::Entry::type,
                    Eq(LoggingSink::Entry::EventType::kClientMessage)),
              Field(&LoggingSink::Entry::logger,
                    Eq(LoggingSink::Entry::Logger::kServer)),
              Field(&LoggingSink::Entry::authority, Eq(server_address_)),
              Field(&LoggingSink::Entry::service_name,
                    Eq("grpc.testing.EchoTestService")),
              Field(&LoggingSink::Entry::method_name, Eq("Echo")),
              Field(&LoggingSink::Entry::payload,
                    AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                Eq(13)),
                          Field(&LoggingSink::Entry::Payload::message,
                                Eq("\n\013Hello Wo") /* truncated message */))),
              Field(&LoggingSink::Entry::payload_truncated, true)),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre()))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(13)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\013Hello Wo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre())))));
}

TEST_F(LoggingTest, CancelledRpc) {
  g_test_logging_sink->SetConfig(LoggingSink::Config(4096, 4096));
  EchoRequest request;
  request.set_message("foo");
  const int kCancelDelayUs = 10 * 1000;
  request.mutable_param()->set_client_cancel_after_us(kCancelDelayUs);
  EchoResponse response;
  grpc::ClientContext context;
  context.AddMetadata("key", "value");
  auto cancel_thread = std::thread(
      [&context, this](int delay) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay));
        while (!service_.signal_client()) {
        }
        context.TryCancel();
      },
      kCancelDelayUs);
  grpc::Status status = stub_->Echo(&context, request, &response);
  cancel_thread.join();
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);
  auto initial_time = absl::Now();
  while (true) {
    bool found_cancel_on_client = false;
    bool found_cancel_on_server = false;
    for (const auto& entry : g_test_logging_sink->entries()) {
      if (entry.type == LoggingSink::Entry::EventType::kCancel) {
        if (entry.logger == LoggingSink::Entry::Logger::kClient) {
          found_cancel_on_client = true;
        } else {
          found_cancel_on_server = true;
        }
      }
    }
    if (found_cancel_on_client && found_cancel_on_server) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_LT(absl::Now() - initial_time, absl::Seconds(10));
  }
}

TEST_F(LoggingTest, ServerCancelsRpc) {
  g_test_logging_sink->SetConfig(LoggingSink::Config(4096, 4096));
  EchoRequest request;
  request.set_message("foo");
  auto* error = request.mutable_param()->mutable_expected_error();
  error->set_code(25);
  error->set_error_message("error message");
  error->set_binary_error_details("binary error details");
  EchoResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_EQ(status.error_code(), 25);
  EXPECT_EQ(status.error_message(), "error message");
  EXPECT_EQ(status.error_details(), "binary error details");
  g_test_logging_sink->WaitForNumEntries(9, absl::Seconds(5));
  EXPECT_THAT(
      g_test_logging_sink->entries(),
      ::testing::UnorderedElementsAre(
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::status_code,
                                  Eq(25)),
                            Field(&LoggingSink::Entry::Payload::status_message,
                                  Eq("error message"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::status_code,
                                  Eq(25)),
                            Field(&LoggingSink::Entry::Payload::status_message,
                                  Eq("error message")))))));
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
