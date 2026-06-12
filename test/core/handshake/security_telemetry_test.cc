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

#include "src/core/handshaker/security/security_telemetry.h"

#include "src/core/telemetry/instrument.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

class MockMetricsSink : public MetricsSink {
 public:
  virtual ~MockMetricsSink() = default;
  MOCK_METHOD(void, Counter,
              (InstrumentLabelList label_keys,
               absl::Span<const std::string> label, absl::string_view name,
               uint64_t value),
              (override));
  MOCK_METHOD(void, UpDownCounter,
              (InstrumentLabelList label_keys,
               absl::Span<const std::string> label, absl::string_view name,
               uint64_t value),
              (override));
  MOCK_METHOD(void, Histogram,
              (InstrumentLabelList label_keys,
               absl::Span<const std::string> label, absl::string_view name,
               HistogramBuckets bounds, absl::Span<const uint64_t> counts),
              (override));
  MOCK_METHOD(void, DoubleGauge,
              (InstrumentLabelList label_keys,
               absl::Span<const std::string> labels, absl::string_view name,
               double value),
              (override));
  MOCK_METHOD(void, IntGauge,
              (InstrumentLabelList label_keys,
               absl::Span<const std::string> labels, absl::string_view name,
               int64_t value),
              (override));
  MOCK_METHOD(void, UintGauge,
              (InstrumentLabelList label_keys,
               absl::Span<const std::string> labels, absl::string_view name,
               uint64_t value),
              (override));
};

TEST(ClientSecurityTelemetryTest, RecordAndQuery) {
  auto scope = CreateCollectionScope(
      {}, {"grpc.security.handshaker.status", "grpc.target",
           "grpc.security.handshaker.resumed", "grpc.lb.locality",
           "grpc.lb.backend_service"});
  auto storage = ClientHandshakeTelemetryDomain::GetStorage(
      scope, "OK", "dns:///localhost:50051", "false", "", "");

  std::vector<std::string> label_values = {"OK", "dns:///localhost:50051",
                                           "false", "", ""};

  ::testing::StrictMock<MockMetricsSink> sink;

  EXPECT_CALL(sink,
              Counter(::testing::_, ::testing::ElementsAreArray(label_values),
                      "grpc.client.tls.handshakes", 0))
      .Times(1);

  MetricsQuery().OnlyMetrics({"grpc.client.tls.handshakes"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  storage->Increment(ClientHandshakeTelemetryDomain::kHandshakes);

  EXPECT_CALL(sink,
              Counter(::testing::_, ::testing::ElementsAreArray(label_values),
                      "grpc.client.tls.handshakes", 1))
      .Times(1);

  MetricsQuery().OnlyMetrics({"grpc.client.tls.handshakes"}).Run(scope, sink);
}

TEST(ServerSecurityTelemetryTest, RecordAndQuery) {
  auto scope = CreateCollectionScope({}, {"grpc.security.handshaker.status",
                                          "grpc.security.handshaker.resumed"});
  auto storage =
      ServerHandshakeTelemetryDomain::GetStorage(scope, "OK", "false");

  std::vector<std::string> label_values = {"OK", "false"};

  ::testing::StrictMock<MockMetricsSink> sink;

  EXPECT_CALL(sink,
              Counter(::testing::_, ::testing::ElementsAreArray(label_values),
                      "grpc.server.tls.handshakes", 0))
      .Times(1);

  MetricsQuery().OnlyMetrics({"grpc.server.tls.handshakes"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  storage->Increment(ServerHandshakeTelemetryDomain::kHandshakes);

  EXPECT_CALL(sink,
              Counter(::testing::_, ::testing::ElementsAreArray(label_values),
                      "grpc.server.tls.handshakes", 1))
      .Times(1);

  MetricsQuery().OnlyMetrics({"grpc.server.tls.handshakes"}).Run(scope, sink);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
