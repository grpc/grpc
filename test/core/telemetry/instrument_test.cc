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

using instrument_detail::InstrumentIndex;
using instrument_detail::QueryableDomain;

using GetStorageTest = InstrumentTest;
using MetricsQueryTest = InstrumentTest;
using InstrumentIndexDeathTest = InstrumentTest;
using StorageReapingTest = InstrumentTest;

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
  static constexpr absl::string_view kName = "high_contention";
  static constexpr auto kLabels = Labels();

  static inline const auto kCounter =
      RegisterCounter("high_contention", "Desc", "unit");
};

class LowContentionDomain final : public InstrumentDomain<LowContentionDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "low_contention";
  static constexpr auto kLabels = Labels("grpc.target");

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

class InstanceCounterDomain final
    : public InstrumentDomain<InstanceCounterDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "instance_counter";
  static constexpr auto kLabels = Labels("grpc.target");

  static inline const auto kInstanceCounter =
      RegisterCounter("instance_counter", "Desc", "unit");
};

class TestDomain1 : public InstrumentDomain<TestDomain1> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "test_domain1";
  static constexpr auto kLabels = Labels("label1");
  static inline const auto kCounter1 = RegisterCounter("test.counter1", "", "");
};

class TestDomain2 : public InstrumentDomain<TestDomain2> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "test_domain2";
  static constexpr auto kLabels = Labels("label2", "label3");
  static inline const auto kCounter2 = RegisterCounter("test.counter2", "", "");
};

class GarbageCollectionTestDomain
    : public InstrumentDomain<GarbageCollectionTestDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "gc_test";
  static constexpr auto kLabels = Labels("label");
  static inline const auto kTestCounter =
      RegisterCounter("gc-test.counter", "", "");
};

using InstrumentIndexTest = InstrumentTest;

class FanOutDomain final : public InstrumentDomain<FanOutDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "fan_out";
  static constexpr auto kLabels = Labels("grpc.target", "grpc.method");

  static inline const auto kCounter =
      RegisterCounter("fan_out", "Desc", "unit");
  static inline const auto kDoubleGauge =
      RegisterDoubleGauge("fan_out_double", "Desc", "unit");
};

using InstrumentIndexDeathTest = InstrumentTest;
using InstrumentIndexTest = InstrumentTest;
using MetricsQueryTest = InstrumentTest;

// Verifies that instrument metadata can be registered and found via the
// InstrumentIndex.
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

// Verifies that registering a metric with a duplicate name returns the same
// description pointer.
TEST_F(InstrumentIndexTest, RegisterDuplicateReturnsSame) {
  InstrumentIndex& index = InstrumentIndex::Get();
  const auto* desc1 =
      index.Register(nullptr, 1, "duplicate_metric", "Desc 1", "units", {});
  const auto* desc2 =
      index.Register(nullptr, 2, "duplicate_metric", "Desc 2", "units", {});
  EXPECT_EQ(desc1, desc2);
  EXPECT_EQ(desc1->description, "Desc 1");
}

// Tests basic counter functionality in a high-contention domain (no labels).
// Verifies that increments are recorded and that storage is reset after being
// released.
TEST_F(MetricsQueryTest, HighContention) {
  auto storage = HighContentionDomain::GetStorage();
  ::testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(), "high_contention", 0));
  MetricsQuery()
      .OnlyMetrics({"high_contention"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Increment(HighContentionDomain::kCounter);
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(), "high_contention", 1));
  MetricsQuery()
      .OnlyMetrics({"high_contention"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage.reset();
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage = HighContentionDomain::GetStorage();
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(), "high_contention", 0));
  MetricsQuery()
      .OnlyMetrics({"high_contention"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
}

// Tests basic counter functionality in a low-contention domain (one label).
// Verifies that increments are recorded for the correct label and that storage
// is reset after being released.
TEST_F(MetricsQueryTest, LowContention) {
  auto storage = LowContentionDomain::GetStorage("example.com");
  std::vector<std::string> label = {"example.com"};
  ::testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(
      sink, Counter(absl::Span<const std::string>(label), "low_contention", 0));
  MetricsQuery()
      .OnlyMetrics({"low_contention"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Increment(LowContentionDomain::kCounter);
  EXPECT_CALL(
      sink, Counter(absl::Span<const std::string>(label), "low_contention", 1));
  MetricsQuery()
      .OnlyMetrics({"low_contention"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage.reset();
  MetricsQuery()
      .OnlyMetrics({"low_contention"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  storage = LowContentionDomain::GetStorage("example.com");
  EXPECT_CALL(
      sink, Counter(absl::Span<const std::string>(label), "low_contention", 0));
  MetricsQuery()
      .OnlyMetrics({"low_contention"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
}

// Tests histogram functionality in a low-contention domain.
// Verifies that increments are recorded in the correct histogram bucket.
TEST_F(MetricsQueryTest, LowContentionHistogram) {
  std::vector<uint64_t> value_before;
  auto storage = LowContentionDomain::GetStorage("example.com");
  ::testing::StrictMock<MockMetricsSink> sink;
  std::vector<std::string> label = {"example.com"};
  EXPECT_CALL(sink,
              Histogram(absl::Span<const std::string>(label),
                        "exponential_histogram", ::testing::_, ::testing::_))
      .WillOnce([&value_before](auto, auto, auto, auto counts) {
        value_before.assign(counts.begin(), counts.end());
      });
  MetricsQuery()
      .OnlyMetrics({"exponential_histogram"})
      .WithLabelEq("grpc.target", "example.com")
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  std::vector<uint64_t> expect_value = value_before;
  expect_value[0] += 1;
  storage->Increment(LowContentionDomain::kExponentialHistogram, 0);
  EXPECT_CALL(sink, Histogram(absl::Span<const std::string>(label),
                              "exponential_histogram", ::testing::_,
                              absl::MakeConstSpan(expect_value)))
      .Times(1);
  MetricsQuery()
      .OnlyMetrics({"exponential_histogram"})
      .WithLabelEq("grpc.target", "example.com")
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
}

// Tests gauge functionality (double, int, uint) in a low-contention domain.
// Verifies that a GaugeProvider can register itself and provide correct values
// during a query.
TEST_F(MetricsQueryTest, LowContentionGauge) {
  auto storage = LowContentionDomain::GetStorage("example.com");
  std::vector<std::string> label = {"example.com"};
  ::testing::StrictMock<MockMetricsSink> sink;

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
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
}

// Tests metric collection across multiple label sets ("fan-out").
// Verifies that metrics for different label combinations are reported correctly
// and that collapsing labels aggregates the results as expected.
TEST_F(MetricsQueryTest, FanOut) {
  auto storage_foo = FanOutDomain::GetStorage("example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = FanOutDomain::GetStorage("example.com", "bar");
  std::vector<std::string> label_bar = {"example.com", "bar"};
  ::testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 0));
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_bar), "fan_out", 0));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo->Increment(FanOutDomain::kCounter);
  storage_bar->Increment(FanOutDomain::kCounter);
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 1));
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_bar), "fan_out", 1));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  const std::vector<std::string> label_all = {"example.com"};
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_all), "fan_out", 2));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .CollapseLabels({"grpc.method"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo.reset();
  storage_bar.reset();
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo = FanOutDomain::GetStorage("example.com", "foo");
  storage_bar = FanOutDomain::GetStorage("example.com", "bar");
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 0));
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_bar), "fan_out", 0));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_all), "fan_out", 0));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .CollapseLabels({"grpc.method"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
}

// Tests gauge functionality with multiple label sets.
// Verifies that gauges for different label combinations are reported correctly
// and that label filtering works. It also confirms that gauges are not
// aggregated when labels are collapsed.
TEST_F(MetricsQueryTest, FanOutGauge) {
  auto storage_foo = FanOutDomain::GetStorage("example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = FanOutDomain::GetStorage("example.com", "bar");
  std::vector<std::string> label_bar = {"example.com", "bar"};
  ::testing::StrictMock<MockMetricsSink> sink;

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
  MetricsQuery()
      .OnlyMetrics({"fan_out_double"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // Test label equality filter
  EXPECT_CALL(sink, DoubleGauge(absl::Span<const std::string>(label_foo),
                                "fan_out_double", 1.1));
  MetricsQuery()
      .OnlyMetrics({"fan_out_double"})
      .WithLabelEq("grpc.method", "foo")
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // Test collapsing - Gauges are not aggregated.
  EXPECT_CALL(sink, DoubleGauge(::testing::_, ::testing::_, ::testing::_))
      .Times(0);
  MetricsQuery()
      .OnlyMetrics({"fan_out_double"})
      .CollapseLabels({"grpc.method"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
}

// Tests the `WithLabelEq` filter in MetricsQuery.
// Verifies that only metrics matching the specified label values are returned.
TEST_F(MetricsQueryTest, LabelEq) {
  auto storage_foo = FanOutDomain::GetStorage("example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = FanOutDomain::GetStorage("example.com", "bar");
  auto storage_baz = FanOutDomain::GetStorage("example.org", "baz");
  ::testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 0));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .WithLabelEq("grpc.target", "example.com")
      .WithLabelEq("grpc.method", "foo")
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo->Increment(FanOutDomain::kCounter);
  storage_bar->Increment(FanOutDomain::kCounter);
  storage_baz->Increment(FanOutDomain::kCounter);
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label_foo), "fan_out", 1));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .WithLabelEq("grpc.target", "example.com")
      .WithLabelEq("grpc.method", "foo")
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
}

// A stress test that runs multiple threads concurrently, performing metric
// increments, gauge provider registrations, and metric queries.
// This is a "does it crash" test to check for race conditions.
TEST_F(MetricsQueryTest, ThreadStress) {
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
        MetricsQuery().Run(QueryableDomain::CreateCollectionScope(), sink);
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

// Tests that a registered histogram collection hook is called when a histogram
// is incremented.
TEST_F(InstrumentTest, HistogramHook) {
  ::testing::MockFunction<void(
      const InstrumentMetadata::Description* instrument,
      absl::Span<const std::string> labels, int64_t value)>
      hook;
  RegisterHistogramCollectionHook(hook.AsStdFunction());
  auto storage = LowContentionDomain::GetStorage("example.com");
  std::vector<std::string> label = {"example.com"};
  EXPECT_CALL(hook,
              Call(::testing::_, absl::Span<const std::string>(label), 10));
  storage->Increment(LowContentionDomain::kExponentialHistogram, 10);
  ::testing::Mock::VerifyAndClearExpectations(&hook);
}

// Tests that multiple registered histogram collection hooks are all called when
// a histogram is incremented.
TEST_F(InstrumentTest, MultipleHistogramHooks) {
  ::testing::MockFunction<void(
      const InstrumentMetadata::Description* instrument,
      absl::Span<const std::string> labels, int64_t value)>
      hook1;
  ::testing::MockFunction<void(
      const InstrumentMetadata::Description* instrument,
      absl::Span<const std::string> labels, int64_t value)>
      hook2;
  RegisterHistogramCollectionHook(hook1.AsStdFunction());
  RegisterHistogramCollectionHook(hook2.AsStdFunction());
  auto storage = LowContentionDomain::GetStorage("example.com");
  std::vector<std::string> label = {"example.com"};
  EXPECT_CALL(hook1,
              Call(::testing::_, absl::Span<const std::string>(label), 10));
  EXPECT_CALL(hook2,
              Call(::testing::_, absl::Span<const std::string>(label), 10));
  storage->Increment(LowContentionDomain::kExponentialHistogram, 10);
  ::testing::Mock::VerifyAndClearExpectations(&hook1);
  ::testing::Mock::VerifyAndClearExpectations(&hook2);
}

// Verifies that calling GetStorage with the same labels multiple times returns
// a pointer to the same Storage instance, as long as a strong reference is
// held.
TEST_F(GetStorageTest, SameInstanceForRepeatedCalls) {
  auto storage1 = LowContentionDomain::GetStorage("test.com");
  auto storage2 = LowContentionDomain::GetStorage("test.com");
  EXPECT_EQ(storage1.get(), storage2.get());
}

// Verifies that after a Storage instance is released (strong ref count goes to
// zero), a subsequent call to GetStorage with the same labels creates a new
// Storage instance. This is verified by checking that a test-only instance
// counter is reset.
TEST_F(GetStorageTest, NewInstanceAfterRelease) {
  const std::string target = "test.com";
  std::vector<std::string> label = {target};
  ::testing::StrictMock<MockMetricsSink> sink;

  auto storage1 = InstanceCounterDomain::GetStorage(target);
  storage1->Increment(InstanceCounterDomain::kInstanceCounter);
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label),
                            "instance_counter", 1));
  MetricsQuery()
      .OnlyMetrics({"instance_counter"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // Release the strong ref. Orphaned() should run and remove it from the map.
  storage1.reset();

  // GetStorage again, should create a new instance.
  auto storage2 = InstanceCounterDomain::GetStorage(target);
  // The counter on the new instance should be 0.
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label),
                            "instance_counter", 0));
  MetricsQuery()
      .OnlyMetrics({"instance_counter"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  storage2->Increment(InstanceCounterDomain::kInstanceCounter);
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label),
                            "instance_counter", 1));
  MetricsQuery()
      .OnlyMetrics({"instance_counter"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
}

// Tests that a Storage instance created *after* a CollectionScope has been
// created is still visible and included in the metric query results for that
// scope.
TEST_F(MetricsQueryTest, NewStorageVisibleInQuery) {
  ::testing::StrictMock<MockMetricsSink> sink;
  std::vector<std::string> label = {"new_metric.com"};

  // Initial query, storage doesn't exist yet.
  MetricsQuery()
      .OnlyMetrics({"low_contention"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // Create scope.
  auto scope = QueryableDomain::CreateCollectionScope();
  // Storage created *after* scope.
  auto storage = LowContentionDomain::GetStorage("new_metric.com");
  storage->Increment(LowContentionDomain::kCounter);

  // Query again with the same scope, new storage should be visible.
  EXPECT_CALL(
      sink, Counter(absl::Span<const std::string>(label), "low_contention", 1));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(std::move(scope), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // Query with a new scope, should also be visible.
  EXPECT_CALL(
      sink, Counter(absl::Span<const std::string>(label), "low_contention", 1));
  MetricsQuery()
      .OnlyMetrics({"low_contention"})
      .Run(QueryableDomain::CreateCollectionScope(), sink);
}

// Verifies that when a Storage instance's strong reference is released, its
// Orphaned() method is called, which removes it from the domain's central map.
TEST_F(StorageReapingTest, MapRemoval) {
  EXPECT_EQ(LowContentionDomain::Domain()->TestOnlyCountStorageHeld(), 0);
  auto storage1 = LowContentionDomain::GetStorage("test.com");
  EXPECT_EQ(LowContentionDomain::Domain()->TestOnlyCountStorageHeld(), 1);
  auto storage2 = LowContentionDomain::GetStorage("test2.com");
  EXPECT_EQ(LowContentionDomain::Domain()->TestOnlyCountStorageHeld(), 2);
  storage1.reset();
  EXPECT_EQ(LowContentionDomain::Domain()->TestOnlyCountStorageHeld(), 1);
  storage2.reset();
  EXPECT_EQ(LowContentionDomain::Domain()->TestOnlyCountStorageHeld(), 0);
}

// Verifies that repeatedly creating and destroying Storage instances for unique
// labels does not lead to unbounded growth of the domain's storage map.
TEST_F(StorageReapingTest, LongTermMapSize) {
  for (int i = 0; i < 1000; ++i) {
    auto storage = LowContentionDomain::GetStorage(absl::StrCat("test.com", i));
    storage.reset();
    if (i % 100 == 0) {
      EXPECT_LT(LowContentionDomain::Domain()->TestOnlyCountStorageHeld(), 5);
    }
  }
  EXPECT_EQ(LowContentionDomain::Domain()->TestOnlyCountStorageHeld(), 0);
}

// A stress test for the storage reaping mechanism. Multiple threads
// concurrently create, use, and release Storage instances for a shared set of
// labels. The test verifies that eventually all storage is reaped.
TEST_F(StorageReapingTest, Concurrency) {
  std::vector<std::thread> threads;
  std::atomic<bool> done = false;
  static constexpr int kNumThreads = 10;
  static constexpr int kLabelsPerThread = 10;

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&done, i]() {
      absl::BitGen gen;
      while (!done.load(std::memory_order_relaxed)) {
        // Access a mix of shared and unique labels
        for (int j = 0; j < kLabelsPerThread; ++j) {
          std::string label = absl::StrCat(
              "label_", (i + j) % (kNumThreads * kLabelsPerThread / 2));
          auto storage = LowContentionDomain::GetStorage(label);
          storage->Increment(LowContentionDomain::kCounter);
          // Hold the ref for a short time
          absl::SleepFor(absl::Milliseconds(absl::Uniform(gen, 0, 10)));
          storage.reset();
        }
      }
    });
  }

  // Briefly run the concurrent operations
  absl::SleepFor(absl::Seconds(2));
  done.store(true, std::memory_order_relaxed);

  for (auto& thread : threads) {
    thread.join();
  }

  // Eventually, all storage should be reaped.
  EXPECT_EQ(LowContentionDomain::Domain()->TestOnlyCountStorageHeld(), 0);
}

// Verifies that a CollectionScope created via CreateCollectionScope takes a
// snapshot of the existing metrics, which are then readable via
// MetricsQuery::Run.
TEST_F(InstrumentTest, CollectionScopeSnapshotsExistingMetrics) {
  // Create some metrics *before* the scope is created.
  auto storage1 = LowContentionDomain::GetStorage("test1.com");
  storage1->Increment(LowContentionDomain::kCounter);
  auto storage2 = FanOutDomain::GetStorage("target1", "method1");
  for (int i = 0; i < 5; ++i) {
    storage2->Increment(FanOutDomain::kCounter);
  }

  // Create the scope.
  auto scope = QueryableDomain::CreateCollectionScope();

  // Query the data.
  ::testing::StrictMock<MockMetricsSink> sink;
  std::vector<std::string> label1 = {"test1.com"};
  std::vector<std::string> label2 = {"target1", "method1"};
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label1),
                            "low_contention", 1));
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label2), "fan_out", 5));
  MetricsQuery()
      .OnlyMetrics({"low_contention", "fan_out"})
      .Run(std::move(scope), sink);
}

// Verifies that metrics created *after* a CollectionScope is created are still
// visible to that scope, verifying the live-update mechanism.
TEST_F(InstrumentTest, CollectionScopeSeesNewMetrics) {
  // Create the scope first.
  auto scope = QueryableDomain::CreateCollectionScope();

  // Create metrics *after* the scope exists.
  auto storage1 = LowContentionDomain::GetStorage("test1.com");
  storage1->Increment(LowContentionDomain::kCounter);
  auto storage2 = FanOutDomain::GetStorage("target1", "method1");
  for (int i = 0; i < 5; ++i) {
    storage2->Increment(FanOutDomain::kCounter);
  }

  // Query the data using the original scope.
  ::testing::StrictMock<MockMetricsSink> sink;
  std::vector<std::string> label1 = {"test1.com"};
  std::vector<std::string> label2 = {"target1", "method1"};
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label1),
                            "low_contention", 1));
  EXPECT_CALL(sink,
              Counter(absl::Span<const std::string>(label2), "fan_out", 5));
  MetricsQuery()
      .OnlyMetrics({"low_contention", "fan_out"})
      .Run(std::move(scope), sink);
}

// Verifies that CreateCollectionScope creates a valid scope.
TEST_F(InstrumentTest, CreateCollectionScope) {
  auto scope = QueryableDomain::CreateCollectionScope();
  ASSERT_NE(scope, nullptr);
}

// Verifies that a Storage instance created *after* a CollectionScope exists is
// correctly added to that scope's internal sets and is visible for metric
// collection.
TEST_F(InstrumentTest, NewStorageVisibleInScope) {
  auto scope = QueryableDomain::CreateCollectionScope();
  ASSERT_NE(scope, nullptr);
  size_t initial_count = scope->TestOnlyCountStorageHeld();
  auto storage = LowContentionDomain::GetStorage("new_storage.com");
  EXPECT_EQ(scope->TestOnlyCountStorageHeld(), initial_count + 1);
}

// End-to-end test for garbage collection.
// 1. Creates a Storage instance.
// 2. Creates a CollectionScope (which holds a weak ref).
// 3. Releases the strong ref to the Storage instance.
// 4. Verifies the Storage is still alive and its data is readable via the
//    scope.
// 5. Destroys the scope.
// 6. Verifies that the Storage has now been destroyed by creating a new
//    instance and checking that its counter is reset.
TEST_F(InstrumentTest, GarbageCollection) {
  auto storage = GarbageCollectionTestDomain::GetStorage("a");
  storage->Increment(GarbageCollectionTestDomain::kTestCounter);
  // Create the scope whilst the storage is still strongly referenced.
  auto scope = QueryableDomain::CreateCollectionScope();
  // Now release the strong reference.
  storage.reset();

  // The storage should still be alive in the scope.
  ::testing::StrictMock<MockMetricsSink> sink;
  std::vector<std::string> label = {"a"};
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label),
                            "gc-test.counter", 1));
  MetricsQuery().Run(std::move(scope), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // Now the scope is gone, the storage should be gone too.
  storage = GarbageCollectionTestDomain::GetStorage("a");
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label),
                            "gc-test.counter", 0));
  MetricsQuery().Run(QueryableDomain::CreateCollectionScope(), sink);
}

// Tests garbage collection with multiple active CollectionScopes.
// Verifies that a Storage object is kept alive as long as at least one
// CollectionScope holds a weak reference to it.
TEST_F(InstrumentTest, GarbageCollectionMultipleScopes) {
  auto storage = GarbageCollectionTestDomain::GetStorage("b");
  storage->Increment(GarbageCollectionTestDomain::kTestCounter);

  // Create two scopes while the storage is strongly referenced.
  auto scope1 = QueryableDomain::CreateCollectionScope();
  auto scope2 = QueryableDomain::CreateCollectionScope();

  // Release the strong reference.
  storage.reset();

  // The storage should still be alive in the first scope.
  ::testing::StrictMock<MockMetricsSink> sink;
  std::vector<std::string> label = {"b"};
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label),
                            "gc-test.counter", 1));
  MetricsQuery().Run(std::move(scope1), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // After the first scope is destroyed, the storage should still be alive in
  // the second scope.
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label),
                            "gc-test.counter", 1));
  MetricsQuery().Run(std::move(scope2), sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // Now that both scopes are gone, the storage should be gone too.
  storage = GarbageCollectionTestDomain::GetStorage("b");
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label),
                            "gc-test.counter", 0));
  MetricsQuery().Run(QueryableDomain::CreateCollectionScope(), sink);
}

}  // namespace grpc_core
