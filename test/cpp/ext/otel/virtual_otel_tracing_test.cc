// Copyright 2026 gRPC authors.
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

#include <grpcpp/ext/otel_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/generic_stub_session.h>
#include <grpcpp/virtual_channel.h>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "opentelemetry/exporters/memory/in_memory_span_exporter_factory.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "src/core/config/core_configuration.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/util/host_port.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/port.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/ext/otel/otel_test_library.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

namespace grpc {
namespace testing {
namespace {

using opentelemetry::sdk::trace::SpanData;
using opentelemetry::sdk::trace::SpanDataEvent;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;

const absl::Duration kTestTimeout = absl::Seconds(20);

class SimpleSessionReactor : public grpc::experimental::ServerSessionReactor {
 public:
  SimpleSessionReactor() { StartVirtualRPCs(); }
  void OnSendInitialMetadataDone(bool /*ok*/) override {}
  void OnCancel() override {
    bool do_finish = false;
    {
      grpc::internal::MutexLock l(&mu_);
      if (!finished_) {
        finished_ = true;
        do_finish = true;
      }
    }
    if (do_finish) {
      Finish(grpc::Status::CANCELLED);
    }
  }
  void OnDone() override { delete this; }

  void Close() {
    bool do_finish = false;
    {
      grpc::internal::MutexLock l(&mu_);
      if (!finished_) {
        finished_ = true;
        do_finish = true;
      }
    }
    if (do_finish) {
      Finish(grpc::Status::OK);
    }
  }

  grpc::internal::Mutex mu_;
  bool finished_ ABSL_GUARDED_BY(mu_) = false;
};

class OuterEchoService : public grpc::Service {
 public:
  explicit OuterEchoService(grpc::Service* inner_service)
      : inner_service_(inner_service) {
    auto* method = new grpc::internal::RpcServiceMethod(
        "/grpc.testing.EchoTestService/SessionRequest",
        grpc::internal::RpcMethod::SESSION_RPC,
        new grpc::experimental::internal::CallbackSessionHandler<
            grpc::testing::EchoRequest>(
            [this](grpc::CallbackServerContext* context,
                   const grpc::testing::EchoRequest* request)
                -> grpc::experimental::ServerSessionReactor* {
              if (on_session_reactor_) {
                return on_session_reactor_(request);
              }
              return new SimpleSessionReactor();
            },
            inner_service_));
    method->SetServerApiType(
        grpc::internal::RpcServiceMethod::ApiType::CALL_BACK);
    AddMethod(method);
  }

  void SetSessionReactorFactory(
      std::function<grpc::experimental::ServerSessionReactor*(
          const grpc::testing::EchoRequest*)>
          factory) {
    on_session_reactor_ = std::move(factory);
  }

 private:
  grpc::Service* inner_service_;
  std::function<grpc::experimental::ServerSessionReactor*(
      const grpc::testing::EchoRequest*)>
      on_session_reactor_;
};

class TestSessionReactor : public grpc::experimental::ClientSessionReactor {
 public:
  TestSessionReactor(std::function<void(grpc::internal::Call)> on_ready,
                     std::function<void(const grpc::Status&)> on_done)
      : on_ready_(std::move(on_ready)), on_done_(std::move(on_done)) {}

  void OnSessionReady(grpc::internal::Call call) override {
    if (on_ready_) on_ready_(call);
  }

  void OnDone(const grpc::Status& s) override {
    if (on_done_) on_done_(s);
    delete this;
  }

 private:
  std::function<void(grpc::internal::Call)> on_ready_;
  std::function<void(const grpc::Status&)> on_done_;
};

class VirtualOTelTracingTest : public ::testing::Test {
 protected:
  absl::Status BuildAndRegisterOpenTelemetryPlugin(
      std::shared_ptr<opentelemetry::sdk::trace::TracerProvider>
          tracer_provider,
      std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider>
          meter_provider) {
    return OpenTelemetryPluginBuilder()
        .SetTracerProvider(std::move(tracer_provider))
        .SetMeterProvider(std::move(meter_provider))
        .SetTextMapPropagator(
            OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator())
        .BuildAndRegisterGlobal();
  }

  void SetUp() override {
    grpc_init();
    data_ = std::make_shared<opentelemetry::exporter::memory::InMemorySpanData>(
        100);
    auto tracer_provider =
        std::make_shared<opentelemetry::sdk::trace::TracerProvider>(
            opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(
                opentelemetry::exporter::memory::InMemorySpanExporterFactory::
                    Create(data_)));
    tracer_ = tracer_provider->GetTracer("grpc-test");

    auto meter_provider =
        std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
    reader_ = std::make_shared<grpc::testing::MockMetricReader>();
    meter_provider->AddMetricReader(reader_);

    ASSERT_TRUE(BuildAndRegisterOpenTelemetryPlugin(std::move(tracer_provider),
                                                    std::move(meter_provider))
                    .ok());

    // Start inner server
    ServerBuilder inner_builder;
    inner_builder.RegisterService(&inner_service_);
    inner_server_ = inner_builder.BuildAndStart();

    // Start outer server
    int port = grpc_pick_unused_port_or_die();
    server_address_ = absl::StrCat("localhost:", port);
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, InsecureServerCredentials());
    outer_service_ = std::make_unique<OuterEchoService>(&inner_service_);
    builder.RegisterService(outer_service_.get());
    server_ = builder.BuildAndStart();
  }

  void TearDown() override {
    server_->Shutdown();
    inner_server_->Shutdown();
    grpc_shutdown_blocking();
    grpc_core::ServerCallTracerFactory::TestOnlyReset();
    grpc_core::GlobalStatsPluginRegistryTestPeer::
        ResetGlobalStatsPluginRegistry();
    grpc_core::CoreConfiguration::Reset();
  }

  std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanData>>
  GetEchoSpans(size_t expected_size,
               absl::Duration timeout = absl::Seconds(10)) {
    absl::Time start_time = absl::Now();
    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanData>>
        echo_spans;
    do {
      std::vector<std::unique_ptr<SpanData>> all_spans = data_->GetSpans();
      for (auto& span : all_spans) {
        if (absl::EndsWith(span->GetName(), "/Echo")) {
          echo_spans.push_back(std::move(span));
        }
      }
      if ((echo_spans.size() >= expected_size) ||
          (absl::Now() - start_time > timeout)) {
        break;
      }
      std::this_thread::yield();
    } while (true);
    return echo_spans;
  }

  absl::flat_hash_map<
      std::string,
      std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
  ReadCurrentMetricsData(
      absl::AnyInvocable<
          bool(const absl::flat_hash_map<
               std::string,
               std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&)>
          continue_predicate) {
    absl::flat_hash_map<
        std::string,
        std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
        data;
    auto deadline = absl::Now() + absl::Seconds(5);
    do {
      std::this_thread::yield();
      reader_->Collect([&](opentelemetry::sdk::metrics::ResourceMetrics& rm) {
        for (const opentelemetry::sdk::metrics::ScopeMetrics& smd :
             rm.scope_metric_data_) {
          for (const opentelemetry::sdk::metrics::MetricData& md :
               smd.metric_data_) {
            for (const opentelemetry::sdk::metrics::PointDataAttributes& dp :
                 md.point_data_attr_) {
              data[md.instrument_descriptor.name_].push_back(dp);
            }
          }
        }
        return true;
      });
    } while (continue_predicate(data) && deadline > absl::Now());
    return data;
  }

  std::shared_ptr<grpc::Channel> CreateAndWaitForVirtualChannel() {
    absl::Notification server_reactor_created;
    outer_service_->SetSessionReactorFactory(
        [&](const grpc::testing::EchoRequest* /*request*/) {
          server_reactor_ = new SimpleSessionReactor();
          server_reactor_created.Notify();
          return server_reactor_;
        });

    std::shared_ptr<grpc::Channel> channel =
        grpc::CreateChannel(server_address_, InsecureChannelCredentials());
    session_stub_ = std::make_unique<
        grpc::experimental::GenericStubSession<EchoRequest, EchoResponse>>(
        channel);

    absl::Notification session_ready;
    std::shared_ptr<grpc::Channel> session_channel;
    EchoRequest session_request;
    session_request.set_message("Session request");

    auto* session_reactor = new TestSessionReactor(
        [&](grpc::internal::Call call) {
          session_channel = grpc::experimental::CreateVirtualChannel(call);
          session_ready.Notify();
        },
        [&](const grpc::Status& s) {
          EXPECT_TRUE(s.ok()) << s.error_message();
          done_.Notify();
        });

    session_stub_->PrepareSessionCall(
        &context_, "/grpc.testing.EchoTestService/SessionRequest", {},
        &session_request, session_reactor);
    session_reactor->StartCall();

    session_ready.WaitForNotification();
    server_reactor_created.WaitForNotification();

    return session_channel;
  }

  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader_;
  std::shared_ptr<opentelemetry::exporter::memory::InMemorySpanData> data_;
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer_;
  CallbackTestServiceImpl inner_service_;
  std::unique_ptr<OuterEchoService> outer_service_;
  std::unique_ptr<Server> inner_server_;
  std::unique_ptr<Server> server_;
  std::string server_address_;

  SimpleSessionReactor* server_reactor_ = nullptr;
  std::unique_ptr<
      grpc::experimental::GenericStubSession<EchoRequest, EchoResponse>>
      session_stub_;
  ClientContext context_;
  absl::Notification done_;
};

TEST_F(VirtualOTelTracingTest, Basic) {
  // 1. Establish session
  std::shared_ptr<grpc::Channel> session_channel =
      CreateAndWaitForVirtualChannel();

  // 2. Send RPC over virtual channel
  std::unique_ptr<EchoTestService::Stub> stub =
      EchoTestService::NewStub(session_channel);
  {
    EchoRequest request;
    request.set_message("foo");
    EchoResponse response;
    ClientContext rpc_context;
    Status status = stub->Echo(&rpc_context, request, &response);
    EXPECT_TRUE(status.ok());
  }

  // 3. Clean up session
  server_reactor_->Close();
  EXPECT_TRUE(done_.WaitForNotificationWithTimeout(kTestTimeout));

  // 4. Verify Spans
  std::vector<std::unique_ptr<SpanData>> echo_spans = GetEchoSpans(3);

  // We expect 3 spans for Echo: Sent, Attempt, Recv
  ASSERT_EQ(echo_spans.size(), 3);

  SpanData* client_span = nullptr;
  SpanData* attempt_span = nullptr;
  SpanData* server_span = nullptr;
  for (const auto& span : echo_spans) {
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
      EXPECT_EQ(span->GetStatus(), opentelemetry::trace::StatusCode::kOk);
    } else if (span->GetName() == "Sent.grpc.testing.EchoTestService/Echo") {
      client_span = span.get();
    }
  }

  ASSERT_NE(client_span, nullptr);
  ASSERT_NE(attempt_span, nullptr);
  ASSERT_NE(server_span, nullptr);

  EXPECT_EQ(client_span->GetTraceId(), attempt_span->GetTraceId());
  EXPECT_EQ(attempt_span->GetParentSpanId(), client_span->GetSpanId());
  EXPECT_EQ(attempt_span->GetTraceId(), server_span->GetTraceId());
  EXPECT_EQ(server_span->GetParentSpanId(), attempt_span->GetSpanId());

  // 5. Verify Metrics
  const char* kSentMetricName =
      "grpc.client.attempt.sent_total_compressed_message_size";
  const char* kRcvdMetricName =
      "grpc.client.attempt.rcvd_total_compressed_message_size";
  absl::flat_hash_map<
      std::string,
      std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
      metrics_data = ReadCurrentMetricsData(
          [&](const absl::flat_hash_map<
              std::string,
              std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                  data) {
            if (!data.contains(kSentMetricName) ||
                !data.contains(kRcvdMetricName)) {
              return true;
            }
            bool found_sent = false;
            for (const auto& pd : data.at(kSentMetricName)) {
              const auto& attrs = pd.attributes.GetAttributes();
              auto it = attrs.find("grpc.method");
              if (it != attrs.end()) {
                auto* val = std::get_if<std::string>(&it->second);
                if (val != nullptr &&
                    *val == "grpc.testing.EchoTestService/Echo") {
                  found_sent = true;
                  break;
                }
              }
            }
            if (!found_sent) return true;

            bool found_rcvd = false;
            for (const auto& pd : data.at(kRcvdMetricName)) {
              const auto& attrs = pd.attributes.GetAttributes();
              auto it = attrs.find("grpc.method");
              if (it != attrs.end()) {
                auto* val = std::get_if<std::string>(&it->second);
                if (val != nullptr &&
                    *val == "grpc.testing.EchoTestService/Echo") {
                  found_rcvd = true;
                  break;
                }
              }
            }
            return !found_rcvd;
          });

  // Verify Sent Metric
  auto echo_sent_point = std::find_if(
      metrics_data[kSentMetricName].begin(),
      metrics_data[kSentMetricName].end(),
      [](const opentelemetry::sdk::metrics::PointDataAttributes& pd) {
        const auto& attrs = pd.attributes.GetAttributes();
        auto it = attrs.find("grpc.method");
        if (it == attrs.end()) return false;
        auto* val = std::get_if<std::string>(&it->second);
        return val != nullptr && *val == "grpc.testing.EchoTestService/Echo";
      });
  ASSERT_NE(echo_sent_point, metrics_data[kSentMetricName].end());
  auto sent_point_data =
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &echo_sent_point->point_data);
  ASSERT_NE(sent_point_data, nullptr);
  ASSERT_EQ(sent_point_data->count_, 1);
  EXPECT_EQ(std::get<int64_t>(sent_point_data->max_), 5);

  // Verify Received Metric
  auto echo_rcvd_point = std::find_if(
      metrics_data[kRcvdMetricName].begin(),
      metrics_data[kRcvdMetricName].end(),
      [](const opentelemetry::sdk::metrics::PointDataAttributes& pd) {
        const auto& attrs = pd.attributes.GetAttributes();
        auto it = attrs.find("grpc.method");
        if (it == attrs.end()) return false;
        auto* val = std::get_if<std::string>(&it->second);
        return val != nullptr && *val == "grpc.testing.EchoTestService/Echo";
      });
  ASSERT_NE(echo_rcvd_point, metrics_data[kRcvdMetricName].end());
  auto rcvd_point_data =
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &echo_rcvd_point->point_data);
  ASSERT_NE(rcvd_point_data, nullptr);
  ASSERT_EQ(rcvd_point_data->count_, 1);
  EXPECT_EQ(std::get<int64_t>(rcvd_point_data->max_), 5);
}

TEST_F(VirtualOTelTracingTest, Cancellation) {
  // 1. Establish session
  std::shared_ptr<grpc::Channel> session_channel =
      CreateAndWaitForVirtualChannel();

  // 2. Send RPC over virtual channel and cancel it (via deadline)
  std::unique_ptr<EchoTestService::Stub> stub =
      EchoTestService::NewStub(session_channel);
  {
    EchoRequest request;
    request.set_message("foo");
    request.mutable_param()->set_server_cancel_after_us(1000000);  // 1s
    EchoResponse response;
    ClientContext rpc_context;
    // Set short deadline
    rpc_context.set_deadline(std::chrono::system_clock::now() +
                             std::chrono::milliseconds(200));

    Status status = stub->Echo(&rpc_context, request, &response);
    EXPECT_EQ(status.error_code(), StatusCode::DEADLINE_EXCEEDED);
  }

  // 3. Clean up session
  server_reactor_->Close();
  EXPECT_TRUE(done_.WaitForNotificationWithTimeout(kTestTimeout));

  // 4. Verify Spans
  std::vector<std::unique_ptr<SpanData>> echo_spans = GetEchoSpans(3);

  // We expect 3 spans for Echo: Sent, Attempt, Recv
  ASSERT_EQ(echo_spans.size(), 3);

  SpanData* client_span = nullptr;
  SpanData* attempt_span = nullptr;
  SpanData* server_span = nullptr;
  for (const auto& span : echo_spans) {
    EXPECT_TRUE(span->GetSpanContext().IsValid());
    if (span->GetName() == "Attempt.grpc.testing.EchoTestService/Echo") {
      attempt_span = span.get();
      EXPECT_EQ(span->GetStatus(), opentelemetry::trace::StatusCode::kError);
    } else if (span->GetName() == "Recv.grpc.testing.EchoTestService/Echo") {
      server_span = span.get();
      EXPECT_EQ(span->GetStatus(), opentelemetry::trace::StatusCode::kError);
    } else if (span->GetName() == "Sent.grpc.testing.EchoTestService/Echo") {
      client_span = span.get();
    }
  }

  ASSERT_NE(client_span, nullptr);
  ASSERT_NE(attempt_span, nullptr);
  ASSERT_NE(server_span, nullptr);

  EXPECT_EQ(client_span->GetTraceId(), attempt_span->GetTraceId());
  EXPECT_EQ(attempt_span->GetParentSpanId(), client_span->GetSpanId());
  EXPECT_EQ(attempt_span->GetTraceId(), server_span->GetTraceId());
  EXPECT_EQ(server_span->GetParentSpanId(), attempt_span->GetSpanId());
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
