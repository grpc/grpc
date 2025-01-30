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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/exporters/memory/in_memory_span_exporter_factory.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "src/core/config/core_configuration.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {
namespace {

using opentelemetry::sdk::trace::SpanData;
using opentelemetry::sdk::trace::SpanDataEvent;
using ::testing::Lt;
using ::testing::MatchesRegex;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;

class OTelTracingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_core::CoreConfiguration::Reset();
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
    ASSERT_TRUE(OpenTelemetryPluginBuilder()
                    .SetTracerProvider(std::move(tracer_provider))
                    .SetTextMapPropagator(
                        grpc::experimental::MakeGrpcTraceBinTextMapPropagator())
                    .BuildAndRegisterGlobal()
                    .ok());
    grpc::ServerBuilder builder;
    int port;
    // Use IPv4 here because it's less flaky than IPv6 ("[::]:0") on Travis.
    builder.AddListeningPort("0.0.0.0:0", grpc::InsecureServerCredentials(),
                             &port);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    server_address_ = absl::StrCat("localhost:", port);
    auto channel = grpc::CreateChannel(server_address_,
                                       grpc::InsecureChannelCredentials());
    stub_ = EchoTestService::NewStub(channel);
  }

  void TearDown() override {
    server_->Shutdown();
    grpc_shutdown_blocking();
    grpc_core::ServerCallTracerFactory::TestOnlyReset();
    grpc_core::GlobalStatsPluginRegistryTestPeer::
        ResetGlobalStatsPluginRegistry();
  }

  void SendRPC() {
    EchoRequest request;
    request.set_message("foo");
    EchoResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->Echo(&context, request, &response);
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
      if (absl::Now() - start_time > timeout) {
        break;
      }
      std::this_thread::yield();
    } while (spans.size() != expected_size);
    return spans;
  }

  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer_;
  std::shared_ptr<opentelemetry::exporter::memory::InMemorySpanData> data_;
  CallbackTestServiceImpl service_;
  std::string server_address_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<EchoTestService::Stub> stub_;
};

TEST_F(OTelTracingTest, Basic) {
  SendRPC();
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
    SendRPC();
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
    EXPECT_TRUE(stream->Finish().ok());
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
  EXPECT_THAT(outbound_seq_nums,
              UnorderedElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
  EXPECT_THAT(inbound_seq_nums,
              UnorderedElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
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
  EXPECT_THAT(outbound_seq_nums,
              UnorderedElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
  EXPECT_THAT(inbound_seq_nums,
              UnorderedElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
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
  auto spans = GetSpans(3);
  EXPECT_EQ(spans.size(), 7);  // 1 client span, 3 attempt spans, 3 server spans
  std::vector<uint64_t> attempt_seq_nums;
  uint64_t server_span_count = 0;
  for (const auto& span : spans) {
    if (span->GetName() == "Attempt.grpc.testing.EchoTestService/Echo") {
      attempt_seq_nums.push_back(std::get<uint64_t>(
          span->GetAttributes().at("previous-rpc-attempts")));
    } else if (span->GetName() == "Recv.grpc.testing.EchoTestService/Echo") {
      ++server_span_count;
    }
  }
  EXPECT_THAT(attempt_seq_nums, UnorderedElementsAre(0, 1, 2));
  EXPECT_EQ(server_span_count, 3);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
