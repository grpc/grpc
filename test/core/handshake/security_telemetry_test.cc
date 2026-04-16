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

TEST(SecurityTelemetryTest, RecordAndQuery) {
  auto scope = CreateCollectionScope({}, {"grpc.security.handshaker.status",
                                          "grpc.security.handshaker.error_details",
                                          "grpc.security.handshaker.protocol"});
  auto storage = HandshakeTelemetryDomain::GetStorage(scope, "OK", "NONE", "TLS");
  
  std::vector<std::string> label_keys = {"grpc.security.handshaker.status",
                                         "grpc.security.handshaker.error_details",
                                         "grpc.security.handshaker.protocol"};
  std::vector<std::string> label_values = {"OK", "NONE", "TLS"};

  ::testing::StrictMock<MockMetricsSink> sink;
  
  // Initially should be 0 or empty counts.
  EXPECT_CALL(sink,
              Histogram(::testing::_,
                        ::testing::ElementsAreArray(label_values),
                        "grpc.security.handshaker.duration", ::testing::_, ::testing::_))
      .Times(1);
  
  MetricsQuery().OnlyMetrics({"grpc.security.handshaker.duration"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // Increment.
  storage->Increment(HandshakeTelemetryDomain::kDuration, 100);

  // Now should have 1 count.
  EXPECT_CALL(sink,
              Histogram(::testing::_,
                        ::testing::ElementsAreArray(label_values),
                        "grpc.security.handshaker.duration", ::testing::_, ::testing::_))
      .Times(1);
  
  MetricsQuery().OnlyMetrics({"grpc.security.handshaker.duration"}).Run(scope, sink);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

