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

using instrument_detail::InstrumentIndex;
using instrument_detail::QueryableDomain;

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
  MOCK_METHOD(void, DoubleGauge,
              (absl::Span<const std::string> labels, absl::string_view name,
               double value),
              (override));
  MOCK_METHOD(void, IntGauge,
              (absl::Span<const std::string> labels, absl::string_view name,
               int64_t value),
              (override));
  MOCK_METHOD(void, UintGauge,
              (absl::Span<const std::string> labels, absl::string_view name,
               uint64_t value),
              (override));
};

class InstrumentTest : public ::testing::Test {
 protected:
  void SetUp() override { TestOnlyResetInstruments(); }
  void TearDown() override { TestOnlyResetInstruments(); }
};

class HighContentionDomain final
    : public InstrumentDomain<HighContentionDomain> {
 public:
  using Backend = HighContentionBackend;
  static constexpr auto kLabels = std::tuple();

  static inline const auto kCounter =
      RegisterCounter("high_contention", "Desc", "unit");
};

class LowContentionDomain final : public InstrumentDomain<LowContentionDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr auto kLabels = std::tuple("grpc.target");

  static inline const auto kCounter =
      RegisterCounter("low_contention", "Desc", "unit");
  static inline const auto kExponentialHistogram =
      RegisterHistogram<ExponentialHistogramShape>("exponential_histogram",
                                                   "Desc", "unit", 1024, 20);
  static inline const auto kDoubleGauge =
      RegisterDoubleGauge("double_gauge", "Desc", "unit");
  static inline const auto kIntGauge =
      RegisterIntGauge("int_gauge", "Desc", "unit");
  static inline const auto kUintGauge =
      RegisterUintGauge("uint_gauge", "Desc", "unit");
};

class FanOutDomain final : public InstrumentDomain<FanOutDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr auto kLabels = std::tuple("grpc.target", "grpc.method");

  static inline const auto kCounter =
      RegisterCounter("fan_out", "Desc", "unit");
  static inline const auto kDoubleGauge =
      RegisterDoubleGauge("fan_out_double", "Desc", "unit");
};

using InstrumentIndexDeathTest = InstrumentTest;
using InstrumentIndexTest = InstrumentTest;
using MetricsQueryTest = InstrumentTest;

TEST_F(InstrumentIndexTest, RegisterAndFind) {
  InstrumentIndex& index = InstrumentIndex::Get();
  const InstrumentMetadata::Description* description = index.Register(
      nullptr, 0, "test_metric", "Test description", "units", {});
  ASSERT_NE(description, nullptr);
  EXPECT_EQ(description->name, "test_metric");
  EXPECT_EQ(description->description, "Test description");
  EXPECT_EQ(description->unit, "units");

  const auto* found = index.Find("test_metric");
  EXPECT_EQ(found, description);

  const auto* not_found = index.Find("nonexistent");
  EXPECT_EQ(not_found, nullptr);
}

TEST_F(InstrumentIndexDeathTest, RegisterDuplicate) {
  InstrumentIndex& index = InstrumentIndex::Get();
  index.Register(nullptr, 1, "duplicate_metric", "Desc 1", "units", {});
  EXPECT_DEATH(
      index.Register(nullptr, 2, "duplicate_metric", "Desc 2", "units", {}),
      "Metric with name 'duplicate_metric' already registered.");
}

TEST_F(MetricsQueryTest, HighContention) {
  auto storage = HighContentionDomain::GetStorage();
  testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(), "high_contention", 0));
  MetricsQuery().OnlyMetrics({"high_contention"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Increment(HighContentionDomain::kCounter);
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
  auto storage = LowContentionDomain::GetStorage("example.com");
  std::vector<std::string> label = {"example.com"};
  testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(
      sink, Counter(absl::Span<const std::string>(label), "low_contention", 0));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Increment(LowContentionDomain::kCounter);
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
  auto storage = LowContentionDomain::GetStorage("example.com");
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
  storage->Increment(LowContentionDomain::kExponentialHistogram, 0);
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

TEST_F(MetricsQueryTest, LowContentionGauge) {
  auto storage = LowContentionDomain::GetStorage("example.com");
  std::vector<std::string> label = {"example.com"};
  testing::StrictMock<MockMetricsSink> sink;

  class MyGaugeProvider final : public GaugeProvider<LowContentionDomain> {
   public:
    explicit MyGaugeProvider(
        InstrumentStorageRefPtr<LowContentionDomain> storage)
        : GaugeProvider(std::move(storage)) {
      ProviderConstructed();
    }
    ~MyGaugeProvider() { ProviderDestructing(); }

    void PopulateGaugeData(
        GaugeSink<LowContentionDomain>& gauge_sink) override {
      gauge_sink.Set(LowContentionDomain::kDoubleGauge, 1.23);
      gauge_sink.Set(LowContentionDomain::kIntGauge, -456);
      gauge_sink.Set(LowContentionDomain::kUintGauge, 789);
    }
  };
  MyGaugeProvider provider(storage);

  EXPECT_CALL(sink, DoubleGauge(absl::Span<const std::string>(label),
                                "double_gauge", 1.23));
  EXPECT_CALL(
      sink, IntGauge(absl::Span<const std::string>(label), "int_gauge", -456));
  EXPECT_CALL(
      sink, UintGauge(absl::Span<const std::string>(label), "uint_gauge", 789));
  MetricsQuery()
      .OnlyMetrics({"double_gauge", "int_gauge", "uint_gauge"})
      .Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
}

TEST_F(MetricsQueryTest, FanOut) {
  auto storage_foo = FanOutDomain::GetStorage("example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = FanOutDomain::GetStorage("example.com", "bar");
  std::vector<std::string> label_bar = {"example.com", "bar"};
  testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 0));
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_bar), "fan_out", 0));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo->Increment(FanOutDomain::kCounter);
  storage_bar->Increment(FanOutDomain::kCounter);
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

TEST_F(MetricsQueryTest, FanOutGauge) {
  auto storage_foo = FanOutDomain::GetStorage("example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = FanOutDomain::GetStorage("example.com", "bar");
  std::vector<std::string> label_bar = {"example.com", "bar"};
  testing::StrictMock<MockMetricsSink> sink;

  class MyGaugeProvider final : public GaugeProvider<FanOutDomain> {
   public:
    explicit MyGaugeProvider(InstrumentStorageRefPtr<FanOutDomain> storage,
                             double value)
        : GaugeProvider(std::move(storage)), value_(value) {
      ProviderConstructed();
    }
    ~MyGaugeProvider() { ProviderDestructing(); }

    void PopulateGaugeData(GaugeSink<FanOutDomain>& gauge_sink) override {
      gauge_sink.Set(FanOutDomain::kDoubleGauge, value_);
    }

   private:
    double value_;
  };
  MyGaugeProvider provider_foo(storage_foo, 1.1);
  MyGaugeProvider provider_bar(storage_bar, 2.2);

  EXPECT_CALL(sink, DoubleGauge(absl::Span<const std::string>(label_foo),
                                "fan_out_double", 1.1));
  EXPECT_CALL(sink, DoubleGauge(absl::Span<const std::string>(label_bar),
                                "fan_out_double", 2.2));
  MetricsQuery().OnlyMetrics({"fan_out_double"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);

  // Test label equality filter
  EXPECT_CALL(sink, DoubleGauge(absl::Span<const std::string>(label_foo),
                                "fan_out_double", 1.1));
  MetricsQuery()
      .OnlyMetrics({"fan_out_double"})
      .WithLabelEq("grpc.method", "foo")
      .Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);

  // Test collapsing - Gauges are not aggregated.
  EXPECT_CALL(sink, DoubleGauge(testing::_, testing::_, testing::_)).Times(0);
  MetricsQuery()
      .OnlyMetrics({"fan_out_double"})
      .CollapseLabels({"grpc.method"})
      .Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
}

TEST_F(MetricsQueryTest, LabelEq) {
  auto storage_foo = FanOutDomain::GetStorage("example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = FanOutDomain::GetStorage("example.com", "bar");
  auto storage_baz = FanOutDomain::GetStorage("example.org", "baz");
  testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 0));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .WithLabelEq("grpc.target", "example.com")
      .WithLabelEq("grpc.method", "foo")
      .Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo->Increment(FanOutDomain::kCounter);
  storage_bar->Increment(FanOutDomain::kCounter);
  storage_baz->Increment(FanOutDomain::kCounter);
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
      auto storage = HighContentionDomain::GetStorage();
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(HighContentionDomain::kCounter);
      }
    });
    threads.emplace_back([&done]() {
      auto storage = LowContentionDomain::GetStorage("example.com");
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(LowContentionDomain::kCounter);
      }
    });
    threads.emplace_back([&done]() {
      auto storage = LowContentionDomain::GetStorage("bar.com");
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(LowContentionDomain::kCounter);
      }
    });
    threads.emplace_back([&done]() {
      auto storage = LowContentionDomain::GetStorage("example.com");
      absl::BitGen gen;
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(LowContentionDomain::kExponentialHistogram,
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
        void DoubleGauge(absl::Span<const std::string> labels,
                         absl::string_view name, double value) override {}
        void IntGauge(absl::Span<const std::string> labels,
                      absl::string_view name, int64_t value) override {}
        void UintGauge(absl::Span<const std::string> labels,
                       absl::string_view name, uint64_t value) override {}
      };
      NoopSink sink;
      while (!done.load(std::memory_order_relaxed)) {
        MetricsQuery().Run(sink);
      }
    });
    threads.emplace_back([&done]() {
      auto storage = LowContentionDomain::GetStorage("gauge_stress.com");
      class MyProvider final : public GaugeProvider<LowContentionDomain> {
       public:
        explicit MyProvider(
            InstrumentStorageRefPtr<LowContentionDomain> storage)
            : GaugeProvider(std::move(storage)) {
          ProviderConstructed();
        }
        ~MyProvider() { ProviderDestructing(); }
        void PopulateGaugeData(GaugeSink<LowContentionDomain>& sink) override {
          sink.Set(LowContentionDomain::kDoubleGauge, 1.0);
        }
      };
      while (!done.load(std::memory_order_relaxed)) {
        MyProvider provider(storage);
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
