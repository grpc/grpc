//
//
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
//
//

#include <grpcpp/ext/otel_plugin.h>
#include <grpcpp/grpcpp.h>

#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/exporters/memory/in_memory_span_exporter_factory.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/util/host_port.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "test/core/test_util/fail_first_call_filter.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/port.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {
namespace {

using opentelemetry::sdk::trace::SpanData;
using opentelemetry::sdk::trace::SpanDataEvent;
using ::testing::ElementsAre;
using ::testing::FieldsAre;
using ::testing::Lt;
using ::testing::MatchesRegex;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;

class OTelTracingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_init();
    data_ =
        std::make_shared<opentelemetry::exporter::memory::InMemorySpanData>(10);
    // Register OTel plugin for tracing with an in memory exporter
    auto tracer_provider =
        std::make_shared<opentelemetry::sdk::trace::TracerProvider>(
            opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(
                opentelemetry::exporter::memory::InMemorySpanExporterFactory::
                    Create(data_)));
    tracer_ = tracer_provider->GetTracer("grpc-test");
    auto status =
        OpenTelemetryPluginBuilder()
            .SetTracerProvider(std::move(tracer_provider))
            .SetTextMapPropagator(
                OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator())
            .BuildAndRegisterGlobal();
    ASSERT_TRUE(status.ok()) << status;
    port_ = grpc_pick_unused_port_or_die();
    server_address_ = absl::StrCat("localhost:", port_);
    RestartServer();
    auto channel = grpc::CreateChannel(server_address_,
                                       grpc::InsecureChannelCredentials());
    stub_ = EchoTestService::NewStub(channel);
  }

  void RestartServer() {
    if (server_ != nullptr) {
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
    }
    grpc::ServerBuilder builder;
    // Use IPv4 here because it's less flaky than IPv6 ("[::]:0") on Travis.
    builder.AddListeningPort(grpc_core::JoinHostPort("0.0.0.0", port_),
                             grpc::InsecureServerCredentials(), nullptr);
    // Allow only one stream at a time.
    builder.AddChannelArgument(GRPC_ARG_MAX_CONCURRENT_STREAMS, 1);
    builder.AddChannelArgument(
        GRPC_ARG_MAX_CONCURRENT_STREAMS_OVERLOAD_PROTECTION, false);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() override {
    server_->Shutdown();
    grpc_shutdown_blocking();
    grpc_core::ServerCallTracerFactory::TestOnlyReset();
    grpc_core::GlobalStatsPluginRegistryTestPeer::
        ResetGlobalStatsPluginRegistry();
    grpc_core::CoreConfiguration::Reset();
  }

  void SendRPC(EchoTestService::Stub* stub) {
    EchoRequest request;
    request.set_message("foo");
    EchoResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub->Echo(&context, request, &response);
  }

  // Waits for \a timeout for \a expected_size number of spans and returns them.
  std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanData>> GetSpans(
      size_t expected_size, absl::Duration timeout = absl::Seconds(10)) {
    absl::Time start_time = absl::Now();
    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanData>> spans;
    do {
      auto current_spans = data_->GetSpans();
      spans.insert(spans.end(), std::make_move_iterator(current_spans.begin()),
                   std::make_move_iterator(current_spans.end()));
      if ((spans.size() >= expected_size) ||
          (absl::Now() - start_time > timeout)) {
        break;
      }
      std::this_thread::yield();
    } while (true);
    return spans;
  }

  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer_;
  std::shared_ptr<opentelemetry::exporter::memory::InMemorySpanData> data_;
  CallbackTestServiceImpl service_;
  int port_;
  std::string server_address_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<EchoTestService::Stub> stub_;
};

TEST_F(OTelTracingTest, Basic) {
  SendRPC(stub_.get());
  auto spans = GetSpans(3);
  SpanData* client_span;
  SpanData* attempt_span;
  SpanData* server_span;
  // Verify that we get 3 spans -
  // 1) Client RPC Span - Sent.grpc.testing.EchoTestService/Echo
  // 2) Attempt Span - Attempt.grpc.testing.EchoTestService/Echo
  // 3) Server RPC Span - Recv.grpc.testing.EchoTestService/Echo
  EXPECT_EQ(spans.size(), 3);
  for (const auto& span : spans) {
    EXPECT_TRUE(span->GetSpanContext().IsValid());
    if (span->GetName() == "Attempt.grpc.testing.EchoTestService/Echo") {
      attempt_span = span.get();
      EXPECT_THAT(span->GetAttributes(),
                  UnorderedElementsAre(
                      Pair("transparent-retry", VariantWith<bool>(false)),
                      Pair("previous-rpc-attempts", VariantWith<uint64_t>(0))));
      // Verify outbound message event
      const auto outbound_message_event =
          std::find_if(span->GetEvents().begin(), span->GetEvents().end(),
                       [](const SpanDataEvent& event) {
                         return event.GetName() == "Outbound message";
                       });
      ASSERT_NE(outbound_message_event, span->GetEvents().end());
      EXPECT_THAT(outbound_message_event->GetAttributes(),
                  UnorderedElementsAre(
                      Pair("sequence-number", VariantWith<uint64_t>(0)),
                      Pair("message-size", VariantWith<uint64_t>(5))));
      // Verify inbound message event
      const auto inbound_message_event =
          std::find_if(span->GetEvents().begin(), span->GetEvents().end(),
                       [](const SpanDataEvent& event) {
                         return event.GetName() == "Inbound message";
                       });
      ASSERT_NE(inbound_message_event, span->GetEvents().end());
      EXPECT_THAT(inbound_message_event->GetAttributes(),
                  UnorderedElementsAre(
                      Pair("sequence-number", VariantWith<uint64_t>(0)),
                      Pair("message-size", VariantWith<uint64_t>(5))));
      EXPECT_EQ(span->GetStatus(), opentelemetry::trace::StatusCode::kOk);
    } else if (span->GetName() == "Recv.grpc.testing.EchoTestService/Echo") {
      server_span = span.get();
      // Verify outbound message event
      const auto outbound_message_event =
          std::find_if(span->GetEvents().begin(), span->GetEvents().end(),
                       [](const SpanDataEvent& event) {
                         return event.GetName() == "Outbound message";
                       });
      ASSERT_NE(outbound_message_event, span->GetEvents().end());
      EXPECT_THAT(outbound_message_event->GetAttributes(),
                  UnorderedElementsAre(
                      Pair("sequence-number", VariantWith<uint64_t>(0)),
                      Pair("message-size", VariantWith<uint64_t>(5))));
      // Verify inbound message event
      const auto inbound_message_event =
          std::find_if(span->GetEvents().begin(), span->GetEvents().end(),
                       [](const SpanDataEvent& event) {
                         return event.GetName() == "Inbound message";
                       });
      ASSERT_NE(inbound_message_event, span->GetEvents().end());
      EXPECT_THAT(inbound_message_event->GetAttributes(),
                  UnorderedElementsAre(
                      Pair("sequence-number", VariantWith<uint64_t>(0)),
                      Pair("message-size", VariantWith<uint64_t>(5))));
      EXPECT_EQ(span->GetStatus(), opentelemetry::trace::StatusCode::kOk);
    } else {
      client_span = span.get();
      EXPECT_EQ(span->GetName(), "Sent.grpc.testing.EchoTestService/Echo");
    }
  }
  // Check parent-child relationship
  EXPECT_EQ(client_span->GetTraceId(), attempt_span->GetTraceId());
  EXPECT_EQ(attempt_span->GetParentSpanId(), client_span->GetSpanId());
  EXPECT_EQ(attempt_span->GetTraceId(), server_span->GetTraceId());
  EXPECT_EQ(server_span->GetParentSpanId(), attempt_span->GetSpanId());
}

TEST_F(OTelTracingTest, TestApplicationContextFlows) {
  {
    auto span = tracer_->StartSpan("TestSpan");
    auto scope = opentelemetry::sdk::trace::Tracer::WithActiveSpan(span);
    SendRPC(stub_.get());
  }
  auto spans = GetSpans(4);
  EXPECT_EQ(spans.size(), 4);
  const auto test_span = std::find_if(
      spans.begin(), spans.end(), [](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "TestSpan";
      });
  ASSERT_NE(test_span, spans.end());
  const auto client_span = std::find_if(
      spans.begin(), spans.end(), [](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Sent.grpc.testing.EchoTestService/Echo";
      });
  ASSERT_NE(test_span, spans.end());
  EXPECT_EQ((*test_span)->GetTraceId(), (*client_span)->GetTraceId());
  EXPECT_EQ((*client_span)->GetParentSpanId(), (*test_span)->GetSpanId());
}

TEST_F(OTelTracingTest, MessageEventsWithoutCompression) {
  {
    EchoRequest request;
    request.set_message("AAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    EchoResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->Echo(&context, request, &response);
  }
  auto spans = GetSpans(3);
  EXPECT_EQ(spans.size(), 3);
  const auto attempt_span = std::find_if(
      spans.begin(), spans.end(), [](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Attempt.grpc.testing.EchoTestService/Echo";
      });
  ASSERT_NE(attempt_span, spans.end());
  // Verify outbound message on the attempt
  auto outbound_message_event = std::find_if(
      (*attempt_span)->GetEvents().begin(), (*attempt_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Outbound message";
      });
  ASSERT_NE(outbound_message_event, (*attempt_span)->GetEvents().end());
  EXPECT_THAT(
      outbound_message_event->GetAttributes(),
      UnorderedElementsAre(Pair("sequence-number", VariantWith<uint64_t>(0)),
                           Pair("message-size", VariantWith<uint64_t>(31))));
  // Verify inbound message on the attempt
  auto inbound_message_event = std::find_if(
      (*attempt_span)->GetEvents().begin(), (*attempt_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Inbound message";
      });
  ASSERT_NE(inbound_message_event, (*attempt_span)->GetEvents().end());
  EXPECT_THAT(
      inbound_message_event->GetAttributes(),
      UnorderedElementsAre(Pair("sequence-number", VariantWith<uint64_t>(0)),
                           Pair("message-size", VariantWith<uint64_t>(31))));
  const auto server_span = std::find_if(
      spans.begin(), spans.end(), [](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Recv.grpc.testing.EchoTestService/Echo";
      });
  ASSERT_NE(server_span, spans.end());
  // Verify inbound messages on the server
  inbound_message_event = std::find_if(
      (*server_span)->GetEvents().begin(), (*server_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Inbound message";
      });
  ASSERT_NE(inbound_message_event, (*server_span)->GetEvents().end());
  EXPECT_THAT(
      inbound_message_event->GetAttributes(),
      UnorderedElementsAre(Pair("sequence-number", VariantWith<uint64_t>(0)),
                           Pair("message-size", VariantWith<uint64_t>(31))));
  // Verify outbound messages on the server
  outbound_message_event = std::find_if(
      (*server_span)->GetEvents().begin(), (*server_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Outbound message";
      });
  ASSERT_NE(outbound_message_event, (*server_span)->GetEvents().end());
  EXPECT_THAT(
      outbound_message_event->GetAttributes(),
      UnorderedElementsAre(Pair("sequence-number", VariantWith<uint64_t>(0)),
                           Pair("message-size", VariantWith<uint64_t>(31))));
}

TEST_F(OTelTracingTest, CompressionMessageEvents) {
  {
    EchoRequest request;
    request.set_message("AAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    request.mutable_param()->set_compression_algorithm(RequestParams::GZIP);
    EchoResponse response;
    grpc::ClientContext context;
    context.set_compression_algorithm(GRPC_COMPRESS_GZIP);
    grpc::Status status = stub_->Echo(&context, request, &response);
  }
  auto spans = GetSpans(3);
  EXPECT_EQ(spans.size(), 3);
  const auto attempt_span = std::find_if(
      spans.begin(), spans.end(), [](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Attempt.grpc.testing.EchoTestService/Echo";
      });
  ASSERT_NE(attempt_span, spans.end());
  // Verify outbound messages on the attempt
  auto outbound_message_event = std::find_if(
      (*attempt_span)->GetEvents().begin(), (*attempt_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Outbound message";
      });
  ASSERT_NE(outbound_message_event, (*attempt_span)->GetEvents().end());
  EXPECT_THAT(
      outbound_message_event->GetAttributes(),
      UnorderedElementsAre(Pair("sequence-number", VariantWith<uint64_t>(0)),
                           Pair("message-size", VariantWith<uint64_t>(36))));
  auto outbound_message_compressed_event = std::find_if(
      (*attempt_span)->GetEvents().begin(), (*attempt_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Outbound message compressed";
      });
  ASSERT_NE(outbound_message_compressed_event,
            (*attempt_span)->GetEvents().end());
  EXPECT_THAT(
      outbound_message_compressed_event->GetAttributes(),
      UnorderedElementsAre(
          Pair("sequence-number", VariantWith<uint64_t>(0)),
          Pair("message-size-compressed", VariantWith<uint64_t>(Lt(36)))));
  // Verify inbound messages on the attempt
  auto inbound_message_event = std::find_if(
      (*attempt_span)->GetEvents().begin(), (*attempt_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Inbound compressed message";
      });
  ASSERT_NE(inbound_message_event, (*attempt_span)->GetEvents().end());
  EXPECT_THAT(
      inbound_message_event->GetAttributes(),
      UnorderedElementsAre(
          Pair("sequence-number", VariantWith<uint64_t>(0)),
          Pair("message-size-compressed", VariantWith<uint64_t>(Lt(31)))));
  auto inbound_message_decompressed_event = std::find_if(
      (*attempt_span)->GetEvents().begin(), (*attempt_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Inbound message";
      });
  ASSERT_NE(inbound_message_decompressed_event,
            (*attempt_span)->GetEvents().end());
  EXPECT_THAT(
      inbound_message_decompressed_event->GetAttributes(),
      UnorderedElementsAre(Pair("sequence-number", VariantWith<uint64_t>(0)),
                           Pair("message-size", VariantWith<uint64_t>(31))));
  const auto server_span = std::find_if(
      spans.begin(), spans.end(), [](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Recv.grpc.testing.EchoTestService/Echo";
      });
  ASSERT_NE(server_span, spans.end());
  // Verify inbound messages on the server
  inbound_message_event = std::find_if(
      (*server_span)->GetEvents().begin(), (*server_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Inbound compressed message";
      });
  ASSERT_NE(inbound_message_event, (*server_span)->GetEvents().end());
  EXPECT_THAT(
      inbound_message_event->GetAttributes(),
      UnorderedElementsAre(
          Pair("sequence-number", VariantWith<uint64_t>(0)),
          Pair("message-size-compressed", VariantWith<uint64_t>(Lt(36)))));
  inbound_message_decompressed_event = std::find_if(
      (*server_span)->GetEvents().begin(), (*server_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Inbound message";
      });
  ASSERT_NE(inbound_message_decompressed_event,
            (*server_span)->GetEvents().end());
  EXPECT_THAT(
      inbound_message_decompressed_event->GetAttributes(),
      UnorderedElementsAre(Pair("sequence-number", VariantWith<uint64_t>(0)),
                           Pair("message-size", VariantWith<uint64_t>(36))));
  // Verify outbound messages on the server
  outbound_message_event = std::find_if(
      (*server_span)->GetEvents().begin(), (*server_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Outbound message";
      });
  ASSERT_NE(outbound_message_event, (*server_span)->GetEvents().end());
  EXPECT_THAT(
      outbound_message_event->GetAttributes(),
      UnorderedElementsAre(Pair("sequence-number", VariantWith<uint64_t>(0)),
                           Pair("message-size", VariantWith<uint64_t>(31))));
  outbound_message_compressed_event = std::find_if(
      (*server_span)->GetEvents().begin(), (*server_span)->GetEvents().end(),
      [](const SpanDataEvent& event) {
        return event.GetName() == "Outbound message compressed";
      });
  ASSERT_NE(outbound_message_compressed_event,
            (*server_span)->GetEvents().end());
  EXPECT_THAT(
      outbound_message_compressed_event->GetAttributes(),
      UnorderedElementsAre(
          Pair("sequence-number", VariantWith<uint64_t>(0)),
          Pair("message-size-compressed", VariantWith<uint64_t>(Lt(31)))));
}

TEST_F(OTelTracingTest, FailedStatus) {
  {
    EchoRequest request;
    request.set_message("foo");
    request.mutable_param()->mutable_expected_error()->set_code(
        grpc::StatusCode::UNAVAILABLE);
    request.mutable_param()->mutable_expected_error()->set_error_message(
        "test message");
    EchoResponse response;
    grpc::ClientContext context;
    context.set_compression_algorithm(GRPC_COMPRESS_GZIP);
    grpc::Status status = stub_->Echo(&context, request, &response);
  }
  auto spans = GetSpans(3);
  EXPECT_EQ(spans.size(), 3);
  const auto attempt_span = std::find_if(
      spans.begin(), spans.end(), [](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Attempt.grpc.testing.EchoTestService/Echo";
      });
  ASSERT_NE(attempt_span, spans.end());
  EXPECT_EQ((*attempt_span)->GetStatus(),
            opentelemetry::trace::StatusCode::kError);
  EXPECT_THAT((*attempt_span)->GetDescription(),
              MatchesRegex("UNAVAILABLE:.*test message.*"));
  const auto server_span = std::find_if(
      spans.begin(), spans.end(), [](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Recv.grpc.testing.EchoTestService/Echo";
      });
  ASSERT_NE(server_span, spans.end());
  EXPECT_EQ((*server_span)->GetStatus(),
            opentelemetry::trace::StatusCode::kError);
  EXPECT_THAT((*server_span)->GetDescription(),
              MatchesRegex("UNAVAILABLE:.*test message.*"));
}

TEST_F(OTelTracingTest, Streaming) {
  {
    EchoRequest request;
    request.set_message("foo");
    EchoResponse response;
    grpc::ClientContext context;
    auto stream = stub_->BidiStream(&context);
    for (int i = 0; i < 10; ++i) {
      EXPECT_TRUE(stream->Write(request));
      EXPECT_TRUE(stream->Read(&response));
    }
    stream->WritesDone();
    auto status = stream->Finish();
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message();
  }
  auto spans = GetSpans(3);
  EXPECT_EQ(spans.size(), 3);
  const auto attempt_span = std::find_if(
      spans.begin(), spans.end(), [](const std::unique_ptr<SpanData>& span) {
        return span->GetName() ==
               "Attempt.grpc.testing.EchoTestService/BidiStream";
      });
  ASSERT_NE(attempt_span, spans.end());
  // Verify messages on the attempt span
  std::vector<uint64_t> outbound_seq_nums;
  std::vector<uint64_t> inbound_seq_nums;
  for (const auto& event : (*attempt_span)->GetEvents()) {
    if (event.GetName() == "Outbound message") {
      outbound_seq_nums.push_back(
          std::get<uint64_t>(event.GetAttributes().at("sequence-number")));
    }
    if (event.GetName() == "Inbound message") {
      inbound_seq_nums.push_back(
          std::get<uint64_t>(event.GetAttributes().at("sequence-number")));
    }
  }
  EXPECT_THAT(outbound_seq_nums, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
  EXPECT_THAT(inbound_seq_nums, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
  const auto server_span = std::find_if(
      spans.begin(), spans.end(), [](const std::unique_ptr<SpanData>& span) {
        return span->GetName() ==
               "Recv.grpc.testing.EchoTestService/BidiStream";
      });
  outbound_seq_nums.clear();
  inbound_seq_nums.clear();
  // Verify messages on the server span
  for (const auto& event : (*server_span)->GetEvents()) {
    if (event.GetName() == "Outbound message") {
      outbound_seq_nums.push_back(
          std::get<uint64_t>(event.GetAttributes().at("sequence-number")));
    }
    if (event.GetName() == "Inbound message") {
      inbound_seq_nums.push_back(
          std::get<uint64_t>(event.GetAttributes().at("sequence-number")));
    }
  }
  EXPECT_THAT(outbound_seq_nums, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
  EXPECT_THAT(inbound_seq_nums, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
  ASSERT_NE(server_span, spans.end());
}

TEST_F(OTelTracingTest, Retries) {
  {
    ChannelArguments args;
    args.SetString(GRPC_ARG_SERVICE_CONFIG,
                   "{\n"
                   "  \"methodConfig\": [ {\n"
                   "    \"name\": [\n"
                   "      { \"service\": \"grpc.testing.EchoTestService\" }\n"
                   "    ],\n"
                   "    \"retryPolicy\": {\n"
                   "      \"maxAttempts\": 3,\n"
                   "      \"initialBackoff\": \"0.1s\",\n"
                   "      \"maxBackoff\": \"120s\",\n"
                   "      \"backoffMultiplier\": 1,\n"
                   "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
                   "    }\n"
                   "  } ]\n"
                   "}");
    auto channel = CreateCustomChannel(server_address_,
                                       InsecureChannelCredentials(), args);
    auto stub = EchoTestService::NewStub(channel);
    EchoRequest request;
    request.set_message("foo");
    request.mutable_param()->mutable_expected_error()->set_code(
        StatusCode::ABORTED);
    EchoResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub->Echo(&context, request, &response);
  }
  auto spans = GetSpans(7);
  EXPECT_EQ(spans.size(), 7);  // 1 client span, 3 attempt spans, 3 server spans
  std::vector<uint64_t> attempt_seq_nums;
  uint64_t server_span_count = 0;
  for (const auto& span : spans) {
    if (span->GetName() == "Attempt.grpc.testing.EchoTestService/Echo") {
      attempt_seq_nums.push_back(std::get<uint64_t>(
          span->GetAttributes().at("previous-rpc-attempts")));
      EXPECT_EQ(std::get<bool>(span->GetAttributes().at("transparent-retry")),
                false);
    } else if (span->GetName() == "Recv.grpc.testing.EchoTestService/Echo") {
      ++server_span_count;
    }
  }
  EXPECT_THAT(attempt_seq_nums, ElementsAre(0, 1, 2));
  EXPECT_EQ(server_span_count, 3);
}

// An Echo Service that propagates an Echo request to another server.
class PropagatingEchoTestServiceImpl : public EchoTestService::CallbackService {
 public:
  explicit PropagatingEchoTestServiceImpl(EchoTestService::Stub* stub)
      : stub_(stub) {}

  ServerUnaryReactor* Echo(CallbackServerContext* context,
                           const EchoRequest* request,
                           EchoResponse* response) override {
    auto* reactor = context->DefaultReactor();
    ClientContext* child_context =
        ClientContext::FromCallbackServerContext(*context).release();
    stub_->async()->Echo(child_context, request, response,
                         [child_context, reactor](Status s) mutable {
                           EXPECT_TRUE(s.ok())
                               << "code=" << s.error_code()
                               << " message=" << s.error_message();
                           reactor->Finish(s);
                           delete child_context;
                         });
    return reactor;
  }

 private:
  EchoTestService::Stub* const stub_;
};

// Tests that spans are propagated from parent call to child call.
TEST_F(OTelTracingTest, PropagationParentToChild) {
  {
    // Start a propagating echo service that propagates the echo request to the
    // actual server.
    grpc::ServerBuilder builder;
    int port = grpc_pick_unused_port_or_die();
    // Use IPv4 here because it's less flaky than IPv6 ("[::]:0") on Travis.
    builder.AddListeningPort(grpc_core::JoinHostPort("0.0.0.0", port),
                             grpc::InsecureServerCredentials(), nullptr);
    PropagatingEchoTestServiceImpl service(stub_.get());
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    auto channel = grpc::CreateChannel(absl::StrCat("localhost:", port),
                                       grpc::InsecureChannelCredentials());
    auto stub = EchoTestService::NewStub(channel);
    auto span = tracer_->StartSpan("TestSpan");
    auto scope = opentelemetry::sdk::trace::Tracer::WithActiveSpan(span);
    SendRPC(stub.get());
  }
  auto spans =
      GetSpans(7);  // test span, client span, attempt span, server span at
                    // propagating echo service, child client span at
                    // propagating echo service, attempt span at propagating
                    // echo service and server span at actual echo service.
  EXPECT_EQ(spans.size(), 7);
  const auto test_span = std::find_if(
      spans.begin(), spans.end(), [&](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "TestSpan";
      });
  ASSERT_NE(test_span, spans.end());
  const auto client_span = std::find_if(
      spans.begin(), spans.end(), [&](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Sent.grpc.testing.EchoTestService/Echo" &&
               span->GetParentSpanId() == (*test_span)->GetSpanId();
      });
  ASSERT_NE(client_span, spans.end());
  EXPECT_EQ((*client_span)->GetTraceId(), (*test_span)->GetTraceId());
  const auto attempt_span = std::find_if(
      spans.begin(), spans.end(), [&](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Attempt.grpc.testing.EchoTestService/Echo" &&
               span->GetParentSpanId() == (*client_span)->GetSpanId();
      });
  ASSERT_NE(attempt_span, spans.end());
  EXPECT_EQ((*attempt_span)->GetTraceId(), (*test_span)->GetTraceId());
  const auto propagating_server_span = std::find_if(
      spans.begin(), spans.end(), [&](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Recv.grpc.testing.EchoTestService/Echo" &&
               span->GetParentSpanId() == (*attempt_span)->GetSpanId();
      });
  ASSERT_NE(propagating_server_span, spans.end());
  EXPECT_EQ((*propagating_server_span)->GetTraceId(),
            (*test_span)->GetTraceId());
  const auto propagating_client_span = std::find_if(
      spans.begin(), spans.end(), [&](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Sent.grpc.testing.EchoTestService/Echo" &&
               span->GetParentSpanId() ==
                   (*propagating_server_span)->GetSpanId();
      });
  ASSERT_NE(propagating_client_span, spans.end());
  EXPECT_EQ((*propagating_client_span)->GetTraceId(),
            (*test_span)->GetTraceId());
  const auto propagating_attempt_span = std::find_if(
      spans.begin(), spans.end(), [&](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Attempt.grpc.testing.EchoTestService/Echo" &&
               span->GetParentSpanId() ==
                   (*propagating_client_span)->GetSpanId();
      });
  ASSERT_NE(propagating_attempt_span, spans.end());
  EXPECT_EQ((*propagating_attempt_span)->GetTraceId(),
            (*test_span)->GetTraceId());
  const auto server_span = std::find_if(
      spans.begin(), spans.end(), [&](const std::unique_ptr<SpanData>& span) {
        return span->GetName() == "Recv.grpc.testing.EchoTestService/Echo" &&
               span->GetParentSpanId() ==
                   (*propagating_attempt_span)->GetSpanId();
      });
  ASSERT_NE(server_span, spans.end());
  EXPECT_EQ((*server_span)->GetTraceId(), (*test_span)->GetTraceId());
}

class OTelTracingTestForTransparentRetries : public OTelTracingTest {
 protected:
  void SetUp() override {
    grpc_core::CoreConfiguration::RegisterBuilder(
        [](grpc_core::CoreConfiguration::Builder* builder) {
          // Register FailFirstCallFilter to simulate transparent retries.
          builder->channel_init()->RegisterFilter(
              GRPC_CLIENT_SUBCHANNEL,
              &grpc_core::testing::FailFirstCallFilter::kFilterVtable);
        });
    OTelTracingTest::SetUp();
  }
};

TEST_F(OTelTracingTestForTransparentRetries, TransparentRetries) {
  SendRPC(stub_.get());
  auto spans = GetSpans(4);
  // 1 client spans, 2 attempt spans, 1 server span.
  EXPECT_EQ(spans.size(), 4);
  struct AttemptAttributes {
    std::string PrettyPrint() {
      return absl::StrFormat(
          "previous-rpc-attempts: %lu, transparent-retry: %s",
          previous_rpc_attempts, transparent_retry ? "true" : "false");
    }
    uint64_t previous_rpc_attempts;
    bool transparent_retry;
  };
  std::vector<AttemptAttributes> attempt_attributes;
  uint64_t server_span_count = 0;
  for (const auto& span : spans) {
    if (span->GetName() == "Attempt.grpc.testing.EchoTestService/Echo") {
      attempt_attributes.push_back(
          {std::get<uint64_t>(
               span->GetAttributes().at("previous-rpc-attempts")),
           std::get<bool>(span->GetAttributes().at("transparent-retry"))});
    } else if (span->GetName() == "Recv.grpc.testing.EchoTestService/Echo") {
      ++server_span_count;
    }
  }
  EXPECT_EQ(attempt_attributes.size(), 2);
  EXPECT_THAT(attempt_attributes[0], FieldsAre(/*previous-rpc-attempts=*/0,
                                               /*transparent-retry=*/false))
      << attempt_attributes[0].PrettyPrint();
  for (int i = 1; i < attempt_attributes.size(); ++i) {
    EXPECT_THAT(attempt_attributes[i], FieldsAre(/*previous-rpc-attempts=*/0,
                                                 /*transparent-retry=*/true))
        << attempt_attributes[i].PrettyPrint();
  }
  EXPECT_EQ(server_span_count, 1);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
