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

#include "src/core/telemetry/instrument.h"

#include <thread>

#include "absl/random/random.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

class MockMetricsSink : public MetricsSink {
 public:
  virtual ~MockMetricsSink() = default;
  MOCK_METHOD(void, Counter,
              (absl::Span<const std::string> label, absl::string_view name,
               uint64_t value),
              (override));
  MOCK_METHOD(void, Histogram,
              (absl::Span<const std::string> label, absl::string_view name,
               HistogramBuckets bounds, absl::Span<const uint64_t> counts),
              (override));
};

class InstrumentTest : public ::testing::Test {
 protected:
  void SetUp() override { TestOnlyResetInstruments(); }
  void TearDown() override { TestOnlyResetInstruments(); }
};

auto* kHighContentionDomain = MakeInstrumentDomain<HighContentionBackend>();
auto* kLowContentionDomain =
    MakeInstrumentDomain<LowContentionBackend>("grpc.target");
auto* kFanOutDomain =
    MakeInstrumentDomain<LowContentionBackend>("grpc.target", "grpc.method");
auto kHighContentionCounter =
    kHighContentionDomain->RegisterCounter("high_contention", "Desc", "unit");
auto kLowContentionCounter =
    kLowContentionDomain->RegisterCounter("low_contention", "Desc", "unit");
auto kFanOutCounter = kFanOutDomain->RegisterCounter("fan_out", "Desc", "unit");
auto kLowContentionExponentialHistogram =
    kLowContentionDomain->RegisterHistogram<ExponentialHistogramShape>(
        "exponential_histogram", "Desc", "unit", 1024, 20);

using InstrumentIndexTest = InstrumentTest;

TEST_F(InstrumentIndexTest, RegisterAndFind) {
  InstrumentIndex& index = InstrumentIndex::Get();
  const InstrumentIndex::Description* description = index.Register(
      nullptr, 0, "test_metric", "Test description", "units", {});
  ASSERT_NE(description, nullptr);
  EXPECT_EQ(description->name, "test_metric");
  EXPECT_EQ(description->description, "Test description");
  EXPECT_EQ(description->unit, "units");

  const InstrumentIndex::Description* found = index.Find("test_metric");
  EXPECT_EQ(found, description);

  const InstrumentIndex::Description* not_found = index.Find("nonexistent");
  EXPECT_EQ(not_found, nullptr);
}

using InstrumentIndexDeathTest = InstrumentTest;

TEST_F(InstrumentIndexDeathTest, RegisterDuplicate) {
  InstrumentIndex& index = InstrumentIndex::Get();
  index.Register(nullptr, 1, "duplicate_metric", "Desc 1", "units", {});
  EXPECT_DEATH(
      index.Register(nullptr, 2, "duplicate_metric", "Desc 2", "units", {}),
      "Metric with name 'duplicate_metric' already registered.");
}

using MetricsQueryTest = InstrumentTest;

TEST_F(MetricsQueryTest, HighContention) {
  auto storage = kHighContentionDomain->GetStorage();
  testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(), "high_contention", 0));
  MetricsQuery().OnlyMetrics({"high_contention"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Increment(kHighContentionCounter);
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(), "high_contention", 1));
  MetricsQuery().OnlyMetrics({"high_contention"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage.reset();
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(), "high_contention", 1));
  MetricsQuery().OnlyMetrics({"high_contention"}).Run(sink);
}

TEST_F(MetricsQueryTest, LowContention) {
  auto storage = kLowContentionDomain->GetStorage("example.com");
  std::vector<std::string> label = {"example.com"};
  testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(
      sink, Counter(absl::Span<const std::string>(label), "low_contention", 0));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Increment(kLowContentionCounter);
  EXPECT_CALL(
      sink, Counter(absl::Span<const std::string>(label), "low_contention", 1));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage.reset();
  EXPECT_CALL(
      sink, Counter(absl::Span<const std::string>(label), "low_contention", 1));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(sink);
}

TEST_F(MetricsQueryTest, LowContentionHistogram) {
  std::vector<uint64_t> value_before;
  auto storage = kLowContentionDomain->GetStorage("example.com");
  testing::StrictMock<MockMetricsSink> sink;
  std::vector<std::string> label = {"example.com"};
  EXPECT_CALL(sink, Histogram(absl::Span<const std::string>(label),
                              "exponential_histogram", testing::_, testing::_))
      .WillOnce([&value_before](auto, auto, auto, auto counts) {
        value_before.assign(counts.begin(), counts.end());
      });
  MetricsQuery()
      .OnlyMetrics({"exponential_histogram"})
      .WithLabelEq("grpc.target", "example.com")
      .Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  std::vector<uint64_t> expect_value = value_before;
  expect_value[0] += 1;
  storage->Increment(kLowContentionExponentialHistogram, 0);
  EXPECT_CALL(sink, Histogram(absl::Span<const std::string>(label),
                              "exponential_histogram", testing::_,
                              absl::MakeConstSpan(expect_value)))
      .Times(1);
  MetricsQuery()
      .OnlyMetrics({"exponential_histogram"})
      .WithLabelEq("grpc.target", "example.com")
      .Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
}

TEST_F(MetricsQueryTest, FanOut) {
  auto storage_foo = kFanOutDomain->GetStorage("example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = kFanOutDomain->GetStorage("example.com", "bar");
  std::vector<std::string> label_bar = {"example.com", "bar"};
  testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 0));
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_bar), "fan_out", 0));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo->Increment(kFanOutCounter);
  storage_bar->Increment(kFanOutCounter);
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 1));
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_bar), "fan_out", 1));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo.reset();
  storage_bar.reset();
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 1));
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_bar), "fan_out", 1));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  std::vector<std::string> label_all = {"example.com"};
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_all), "fan_out", 2));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .CollapseLabels({"grpc.method"})
      .Run(sink);
}

TEST_F(MetricsQueryTest, LabelEq) {
  auto storage_foo = kFanOutDomain->GetStorage("example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = kFanOutDomain->GetStorage("example.com", "bar");
  auto storage_baz = kFanOutDomain->GetStorage("example.org", "baz");
  testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 0));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .WithLabelEq("grpc.target", "example.com")
      .WithLabelEq("grpc.method", "foo")
      .Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo->Increment(kFanOutCounter);
  storage_bar->Increment(kFanOutCounter);
  storage_baz->Increment(kFanOutCounter);
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 1));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .WithLabelEq("grpc.target", "example.com")
      .WithLabelEq("grpc.method", "foo")
      .Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
}

TEST_F(MetricsQueryTest, ThreadStressTest) {
  std::vector<std::thread> threads;
  std::atomic<bool> done = false;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&done]() {
      auto storage = kHighContentionDomain->GetStorage();
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(kHighContentionCounter);
      }
    });
    threads.emplace_back([&done]() {
      auto storage = kLowContentionDomain->GetStorage("example.com");
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(kLowContentionCounter);
      }
    });
    threads.emplace_back([&done]() {
      auto storage = kLowContentionDomain->GetStorage("bar.com");
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(kLowContentionCounter);
      }
    });
    threads.emplace_back([&done]() {
      auto storage = kLowContentionDomain->GetStorage("example.com");
      absl::BitGen gen;
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(kLowContentionExponentialHistogram,
                           absl::Uniform(gen, 0, 1024));
      }
    });
    threads.emplace_back([&done]() {
      class NoopSink final : public MetricsSink {
       public:
        void Counter(absl::Span<const std::string> label,
                     absl::string_view name, uint64_t value) override {}
        void Histogram(absl::Span<const std::string> label,
                       absl::string_view name, HistogramBuckets bounds,
                       absl::Span<const uint64_t> counts) override {}
      };
      NoopSink sink;
      while (!done.load(std::memory_order_relaxed)) {
        MetricsQuery().Run(sink);
      }
    });
  }
  absl::SleepFor(absl::Seconds(1));
  done.store(true, std::memory_order_relaxed);
  for (auto& thread : threads) {
    thread.join();
  }
}

}  // namespace
}  // namespace grpc_core
