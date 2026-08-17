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

#include "opentelemetry/trace/context.h"
#include "src/core/call/metadata_batch.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "test/core/promise/test_context.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

TEST(GrpcTraceBinTextMapPropagatorTest, StressTest_InvalidVersions) {
  auto propagator =
      OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator();
  uint8_t canonical[29] = {0};
  canonical[0] = 0;
  canonical[1] = 0;
  for (int i = 0; i < 16; i++) canonical[2 + i] = i + 1;
  canonical[18] = 1;
  for (int i = 0; i < 8; i++) canonical[19 + i] = i + 1;
  canonical[27] = 2;
  canonical[28] = 1;

  for (uint8_t v : {1, 2, 3, 127, 128, 255}) {
    canonical[0] = v;
    TestTextMapCarrier carrier;
    carrier.Set("grpc-trace-bin",
                absl::Base64Escape(absl::string_view(
                    reinterpret_cast<char*>(canonical), 29)));
    opentelemetry::context::Context context;
    context = propagator->Extract(carrier, context);
    auto span_context = opentelemetry::trace::GetSpan(context)->GetContext();
    EXPECT_FALSE(span_context.IsValid()) << "Version " << static_cast<int>(v) << " should be invalid";
  }
}

TEST(GrpcTraceBinTextMapPropagatorTest, StressTest_InvalidFieldIDs) {
  auto propagator =
      OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator();
  uint8_t canonical[29] = {0};
  canonical[0] = 0;
  canonical[1] = 0;
  for (int i = 0; i < 16; i++) canonical[2 + i] = i + 1;
  canonical[18] = 1;
  for (int i = 0; i < 8; i++) canonical[19 + i] = i + 1;
  canonical[27] = 2;
  canonical[28] = 1;

  // Invalid trace field id
  for (uint8_t fid : {1, 2, 3, 255}) {
    canonical[1] = fid;
    TestTextMapCarrier carrier;
    carrier.Set("grpc-trace-bin",
                absl::Base64Escape(absl::string_view(
                    reinterpret_cast<char*>(canonical), 29)));
    opentelemetry::context::Context context;
    context = propagator->Extract(carrier, context);
    auto span_context = opentelemetry::trace::GetSpan(context)->GetContext();
    EXPECT_FALSE(span_context.IsValid());
  }
  canonical[1] = 0;

  // Invalid span field id
  for (uint8_t fid : {0, 2, 3, 255}) {
    canonical[18] = fid;
    TestTextMapCarrier carrier;
    carrier.Set("grpc-trace-bin",
                absl::Base64Escape(absl::string_view(
                    reinterpret_cast<char*>(canonical), 29)));
    opentelemetry::context::Context context;
    context = propagator->Extract(carrier, context);
    auto span_context = opentelemetry::trace::GetSpan(context)->GetContext();
    EXPECT_FALSE(span_context.IsValid());
  }
  canonical[18] = 1;

  // Invalid flag field id
  for (uint8_t fid : {0, 1, 3, 255}) {
    canonical[27] = fid;
    TestTextMapCarrier carrier;
    carrier.Set("grpc-trace-bin",
                absl::Base64Escape(absl::string_view(
                    reinterpret_cast<char*>(canonical), 29)));
    opentelemetry::context::Context context;
    context = propagator->Extract(carrier, context);
    auto span_context = opentelemetry::trace::GetSpan(context)->GetContext();
    EXPECT_FALSE(span_context.IsValid());
  }
}

TEST(GrpcTraceBinTextMapPropagatorTest, StressTest_BufferBoundaries) {
  auto propagator =
      OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator();
  uint8_t canonical[100] = {0};
  canonical[0] = 0;
  canonical[1] = 0;
  for (int i = 0; i < 16; i++) canonical[2 + i] = i + 1;
  canonical[18] = 1;
  for (int i = 0; i < 8; i++) canonical[19 + i] = i + 1;
  canonical[27] = 2;
  canonical[28] = 1;

  // Truncated buffers (0 to 28 bytes)
  for (int len = 0; len < 29; len++) {
    TestTextMapCarrier carrier;
    carrier.Set("grpc-trace-bin",
                absl::Base64Escape(absl::string_view(
                    reinterpret_cast<char*>(canonical), len)));
    opentelemetry::context::Context context;
    context = propagator->Extract(carrier, context);
    auto span_context = opentelemetry::trace::GetSpan(context)->GetContext();
    EXPECT_FALSE(span_context.IsValid()) << "Length " << len << " should be invalid";
  }

  // Trailing extra bytes (30 to 100 bytes)
  for (int len = 30; len <= 100; len++) {
    TestTextMapCarrier carrier;
    carrier.Set("grpc-trace-bin",
                absl::Base64Escape(absl::string_view(
                    reinterpret_cast<char*>(canonical), len)));
    opentelemetry::context::Context context;
    context = propagator->Extract(carrier, context);
    auto span_context = opentelemetry::trace::GetSpan(context)->GetContext();
    EXPECT_FALSE(span_context.IsValid()) << "Length " << len << " should be invalid for exact 29-byte match";
  }
}

TEST(GrpcTraceBinTextMapPropagatorTest, StressTest_ExtremeIDs) {
  auto propagator =
      OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator();
  // Max IDs (0xFF..FF)
  uint8_t max_buf[29];
  max_buf[0] = 0;
  max_buf[1] = 0;
  std::memset(&max_buf[2], 0xFF, 16);
  max_buf[18] = 1;
  std::memset(&max_buf[19], 0xFF, 8);
  max_buf[27] = 2;
  max_buf[28] = 1;

  TestTextMapCarrier max_carrier;
  max_carrier.Set("grpc-trace-bin",
                  absl::Base64Escape(absl::string_view(
                      reinterpret_cast<char*>(max_buf), 29)));
  opentelemetry::context::Context context;
  context = propagator->Extract(max_carrier, context);
  auto max_sc = opentelemetry::trace::GetSpan(context)->GetContext();
  EXPECT_TRUE(max_sc.IsValid());

  // All zeros IDs
  uint8_t zero_buf[29] = {0};
  zero_buf[0] = 0;
  zero_buf[1] = 0;
  zero_buf[18] = 1;
  zero_buf[27] = 2;
  zero_buf[28] = 1;

  TestTextMapCarrier zero_carrier;
  zero_carrier.Set("grpc-trace-bin",
                   absl::Base64Escape(absl::string_view(
                       reinterpret_cast<char*>(zero_buf), 29)));
  opentelemetry::context::Context zero_context;
  zero_context = propagator->Extract(zero_carrier, zero_context);
  auto zero_sc = opentelemetry::trace::GetSpan(zero_context)->GetContext();
  EXPECT_FALSE(zero_sc.IsValid());
}

TEST(GrpcTraceBinTextMapPropagatorTest, StressTest_TraceFlags) {
  auto propagator =
      OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator();
  uint8_t buf[29] = {0};
  buf[0] = 0;
  buf[1] = 0;
  for (int i = 0; i < 16; i++) buf[2 + i] = i + 1;
  buf[18] = 1;
  for (int i = 0; i < 8; i++) buf[19 + i] = i + 1;
  buf[27] = 2;

  struct FlagCase {
    uint8_t flag;
    bool sampled;
  };
  std::vector<FlagCase> cases = {
      {0x00, false},
      {0x01, true},
      {0x02, false},
      {0x03, true},
      {0x80, false},
      {0x81, true},
      {0xFF, true},
  };

  for (const auto& tc : cases) {
    buf[28] = tc.flag;
    TestTextMapCarrier carrier;
    carrier.Set("grpc-trace-bin",
                absl::Base64Escape(absl::string_view(
                    reinterpret_cast<char*>(buf), 29)));
    opentelemetry::context::Context context;
    context = propagator->Extract(carrier, context);
    auto sc = opentelemetry::trace::GetSpan(context)->GetContext();
    EXPECT_TRUE(sc.IsValid());
    EXPECT_EQ(sc.IsSampled(), tc.sampled);
    EXPECT_EQ(sc.trace_flags().flags(), tc.flag);
  }
}

TEST(GrpcTraceBinTextMapPropagatorTest, StressTest_Base64Variations) {
  auto propagator =
      OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator();
  uint8_t canonical[29] = {0};
  canonical[0] = 0;
  canonical[1] = 0;
  for (int i = 0; i < 16; i++) canonical[2 + i] = i + 1;
  canonical[18] = 1;
  for (int i = 0; i < 8; i++) canonical[19 + i] = i + 1;
  canonical[27] = 2;
  canonical[28] = 1;

  std::string standard_b64 = absl::Base64Escape(
      absl::string_view(reinterpret_cast<char*>(canonical), 29));

  // Padded vs unpadded Base64
  std::string unpadded_b64 = standard_b64;
  while (!unpadded_b64.empty() && unpadded_b64.back() == '=') {
    unpadded_b64.pop_back();
  }

  TestTextMapCarrier unpadded_carrier;
  unpadded_carrier.Set("grpc-trace-bin", unpadded_b64);
  opentelemetry::context::Context context;
  context = propagator->Extract(unpadded_carrier, context);
  auto sc_unpadded = opentelemetry::trace::GetSpan(context)->GetContext();
  EXPECT_TRUE(sc_unpadded.IsValid());

  // Corrupted base64 inputs
  std::vector<std::string> invalid_b64 = {
      "not-valid-b64!", "====", "???", "", "abc", "!!!@@@###$$$"
  };
  for (const auto& inv : invalid_b64) {
    TestTextMapCarrier carrier;
    carrier.Set("grpc-trace-bin", inv);
    opentelemetry::context::Context ctx;
    ctx = propagator->Extract(carrier, ctx);
    auto sc = opentelemetry::trace::GetSpan(ctx)->GetContext();
    EXPECT_FALSE(sc.IsValid());
  }
}

TEST(GrpcTraceBinTextMapPropagatorTest, StressTest_FuzzRandomInputs) {
  auto propagator =
      OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator();
  // 10,000 randomized iterations
  for (int i = 0; i < 10000; i++) {
    int len = i % 100;
    std::string random_bytes;
    random_bytes.resize(len);
    for (int j = 0; j < len; j++) {
      random_bytes[j] = static_cast<char>((i * 31 + j * 17) % 256);
    }
    TestTextMapCarrier carrier;
    carrier.Set("grpc-trace-bin",
                absl::Base64Escape(random_bytes));
    opentelemetry::context::Context context;
    // Must never crash or panic
    context = propagator->Extract(carrier, context);
    auto sc = opentelemetry::trace::GetSpan(context)->GetContext();
    if (sc.IsValid()) {
      EXPECT_EQ(len, 29);
      EXPECT_EQ(static_cast<uint8_t>(random_bytes[0]), 0);
      EXPECT_EQ(static_cast<uint8_t>(random_bytes[1]), 0);
      EXPECT_EQ(static_cast<uint8_t>(random_bytes[18]), 1);
      EXPECT_EQ(static_cast<uint8_t>(random_bytes[27]), 2);
    }
  }
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
