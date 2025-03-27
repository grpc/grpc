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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/trace/context.h"
#include "src/core/call/metadata_batch.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "test/core/promise/test_context.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace testing {
namespace {

using ::testing::MockFunction;
using ::testing::Return;
using ::testing::StrictMock;

class TestTextMapCarrier
    : public opentelemetry::context::propagation::TextMapCarrier {
 public:
  opentelemetry::nostd::string_view Get(
      opentelemetry::nostd::string_view key) const noexcept override {
    if (key == "grpc-trace-bin") {
      return value_;
    } else {
      return "";
    }
  }

  void Set(opentelemetry::nostd::string_view key,
           opentelemetry::nostd::string_view value) noexcept override {
    if (key == "grpc-trace-bin") {
      value_ = std::string(value);
    }
  }

 private:
  std::string value_;
};

TEST(GrpcTraceBinTextMapPropagatorTest, Inject) {
  auto propagator =
      OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator();
  TestTextMapCarrier carrier;
  opentelemetry::context::Context context;
  char trace_id[] = "0123456789ABCDEF";
  char span_id[] = "01234567";
  context = opentelemetry::trace::SetSpan(
      context,
      std::shared_ptr<opentelemetry::trace::Span>(
          new (std::nothrow) opentelemetry::trace::DefaultSpan(
              opentelemetry::trace::SpanContext(
                  opentelemetry::trace::TraceId(
                      opentelemetry::nostd::span<const uint8_t, 16>(
                          reinterpret_cast<const uint8_t*>(trace_id), 16)),
                  opentelemetry::trace::SpanId(
                      opentelemetry::nostd::span<const uint8_t, 8>(
                          reinterpret_cast<const uint8_t*>(span_id), 8)),
                  opentelemetry::trace::TraceFlags(1), /*is_remote=*/true))));
  propagator->Inject(carrier, context);
  std::string unescaped_val;
  absl::Base64Unescape(
      internal::NoStdStringViewToAbslStringView(carrier.Get("grpc-trace-bin")),
      &unescaped_val);
  EXPECT_EQ(unescaped_val[0], 0);
  EXPECT_EQ(unescaped_val[0], 0);
  EXPECT_EQ(absl::string_view(unescaped_val).substr(2, 16), trace_id);
  EXPECT_EQ(unescaped_val[18], 1);
  EXPECT_EQ(absl::string_view(unescaped_val).substr(19, 8), span_id);
  EXPECT_EQ(unescaped_val[27], 2);
  EXPECT_EQ(unescaped_val[28], 1);
}

TEST(GrpcTraceBinTextMapPropagatorTest, Extract) {
  TestTextMapCarrier carrier;
  constexpr char kTraceBinValue[] =
      "\x00"              // version
      "\x00"              // field 0
      "0123456789ABCDEF"  // trace
      "\x01"              // field 1
      "01234567"          // span
      "\x02"              // field 2
      "\x01";             // flag
  carrier.Set("grpc-trace-bin",
              absl::Base64Escape(absl::string_view(
                  kTraceBinValue, sizeof(kTraceBinValue) - 1)));
  auto propagator =
      OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator();
  opentelemetry::context::Context context;
  context = propagator->Extract(carrier, context);
  auto span_context = opentelemetry::trace::GetSpan(context)->GetContext();
  EXPECT_EQ(span_context.trace_id(),
            opentelemetry::trace::TraceId(
                opentelemetry::nostd::span<const uint8_t, 16>(
                    reinterpret_cast<const uint8_t*>("0123456789ABCDEF"), 16)));
  EXPECT_EQ(
      span_context.span_id(),
      opentelemetry::trace::SpanId(opentelemetry::nostd::span<const uint8_t, 8>(
          reinterpret_cast<const uint8_t*>("01234567"), 8)));
  EXPECT_EQ(span_context.trace_flags().flags(), 1);
}

TEST(GrpcTraceBinTextMapPropagatorTest, Fields) {
  auto propagator =
      OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator();
  StrictMock<MockFunction<bool(opentelemetry::nostd::string_view)>>(
      mock_callback);
  EXPECT_CALL(mock_callback,
              Call(opentelemetry::nostd::string_view("grpc-trace-bin")))
      .WillOnce(Return(true));
  propagator->Fields(mock_callback.AsStdFunction());
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
