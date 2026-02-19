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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/random/random.h"

namespace grpc_core {

using instrument_detail::InstrumentIndex;

MATCHER_P(InstrumentLabelListElementsAreArray, expected, "") {
  std::vector<std::string> actual;
  actual.reserve(arg.size());
  for (size_t i = 0; i < arg.size(); ++i) {
    actual.push_back(std::string(arg[i].label()));
  }
  return ::testing::ExplainMatchResult(::testing::ElementsAreArray(expected),
                                       actual, result_listener);
}

using GetStorageTest = InstrumentTest;
using MetricsQueryTest = InstrumentTest;
using InstrumentIndexDeathTest = InstrumentTest;
using StorageReapingTest = InstrumentTest;
using InstrumentHandleTest = InstrumentTest;
using InstrumentLabelTest = InstrumentTest;
using InstrumentLabelListTest = InstrumentTest;
using InstrumentIndexTest = InstrumentTest;
using DomainStorageTest = InstrumentTest;
using CollectionScopeTest = InstrumentTest;

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

template <typename... LabelNames>
std::vector<std::string> TupleToVector(std::tuple<LabelNames...> labels) {
  auto make_vector = [](auto&&... args) {
    return std::vector<std::string>{absl::StrCat(args)...};
  };
  return std::apply(make_vector, std::move(labels));
}

template <size_t N>
std::vector<std::string> TupleToVector(FixedInstrumentLabelList<N> labels) {
  auto list = labels.ToList();
  std::vector<std::string> result;
  result.reserve(list.size());
  for (size_t i = 0; i < list.size(); ++i) {
    result.push_back(std::string(list[i].label()));
  }
  return result;
}

std::vector<std::string> LabelListToVector(const InstrumentLabelList& list) {
  std::vector<std::string> result;
  result.reserve(list.size());
  for (size_t i = 0; i < list.size(); ++i) {
    result.push_back(std::string(list[i].label()));
  }
  return result;
}

template <typename Domain>
std::vector<std::string> DomainLabels() {
  return TupleToVector(Domain::Labels());
}

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
  GRPC_INSTRUMENT_DOMAIN_LABELS();

  static inline const auto kCounter =
      RegisterCounter("high_contention", "Desc", "unit");
};

class LowContentionDomain final : public InstrumentDomain<LowContentionDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "low_contention";
  GRPC_INSTRUMENT_DOMAIN_LABELS("grpc.target");

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
  static inline const auto kUpDownCounter =
      RegisterUpDownCounter("up_down_counter", "Desc", "unit");
};

class InstanceCounterDomain final
    : public InstrumentDomain<InstanceCounterDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "instance_counter";
  GRPC_INSTRUMENT_DOMAIN_LABELS("grpc.target");

  static inline const auto kInstanceCounter =
      RegisterCounter("instance_counter", "Desc", "unit");
};

class TestDomain1 : public InstrumentDomain<TestDomain1> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "test_domain1";
  GRPC_INSTRUMENT_DOMAIN_LABELS("label1");
  static inline const auto kCounter1 = RegisterCounter("test.counter1", "", "");
};

class TestDomain2 : public InstrumentDomain<TestDomain2> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "test_domain2";
  GRPC_INSTRUMENT_DOMAIN_LABELS("label2", "label3");
  static inline const auto kCounter2 = RegisterCounter("test.counter2", "", "");
};

class GarbageCollectionTestDomain
    : public InstrumentDomain<GarbageCollectionTestDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "gc_test";
  GRPC_INSTRUMENT_DOMAIN_LABELS("label");
  static inline const auto kTestCounter =
      RegisterCounter("gc-test.counter", "", "");
};

using InstrumentIndexTest = InstrumentTest;

class FanOutDomain final : public InstrumentDomain<FanOutDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "fan_out";
  GRPC_INSTRUMENT_DOMAIN_LABELS("grpc.target", "grpc.method");

  static inline const auto kCounter =
      RegisterCounter("fan_out", "Desc", "unit");
  static inline const auto kUpDownCounter =
      RegisterUpDownCounter("fan_out_up_down", "Desc", "unit");
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

// Verifies that the accessors on InstrumentHandle return the correct metadata.
TEST_F(InstrumentHandleTest, Accessors) {
  EXPECT_EQ(LowContentionDomain::kCounter.name(), "low_contention");
  EXPECT_EQ(LowContentionDomain::kCounter.description(), "Desc");
  EXPECT_EQ(LowContentionDomain::kCounter.unit(), "unit");

  EXPECT_EQ(LowContentionDomain::kExponentialHistogram.name(),
            "exponential_histogram");
  EXPECT_EQ(LowContentionDomain::kExponentialHistogram.description(), "Desc");
  EXPECT_EQ(LowContentionDomain::kExponentialHistogram.unit(), "unit");

  EXPECT_EQ(LowContentionDomain::kDoubleGauge.name(), "double_gauge");
  EXPECT_EQ(LowContentionDomain::kDoubleGauge.description(), "Desc");
  EXPECT_EQ(LowContentionDomain::kDoubleGauge.unit(), "unit");

  EXPECT_EQ(LowContentionDomain::kIntGauge.name(), "int_gauge");
  EXPECT_EQ(LowContentionDomain::kIntGauge.description(), "Desc");
  EXPECT_EQ(LowContentionDomain::kIntGauge.unit(), "unit");

  EXPECT_EQ(LowContentionDomain::kUintGauge.name(), "uint_gauge");
  EXPECT_EQ(LowContentionDomain::kUintGauge.description(), "Desc");
  EXPECT_EQ(LowContentionDomain::kUintGauge.unit(), "unit");
}

// Tests basic counter functionality in a high-contention domain (no labels).
// Verifies that increments are recorded and that storage is reset after being
// released.
TEST_F(MetricsQueryTest, HighContention) {
  auto scope = CreateCollectionScope({}, {});
  auto storage = HighContentionDomain::GetStorage(scope);
  ::testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(
      sink,
      Counter(InstrumentLabelListElementsAreArray(std::vector<std::string>{}),
              ::testing::ElementsAreArray(absl::Span<const std::string>()),
              "high_contention", 0));
  MetricsQuery().OnlyMetrics({"high_contention"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Increment(HighContentionDomain::kCounter);
  EXPECT_CALL(
      sink,
      Counter(InstrumentLabelListElementsAreArray(std::vector<std::string>{}),
              ::testing::ElementsAreArray(absl::Span<const std::string>()),
              "high_contention", 1));
  MetricsQuery().OnlyMetrics({"high_contention"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage.reset();
  scope = CreateCollectionScope({}, {});
  storage = HighContentionDomain::GetStorage(scope);
  EXPECT_CALL(
      sink,
      Counter(InstrumentLabelListElementsAreArray(std::vector<std::string>{}),
              ::testing::ElementsAreArray(absl::Span<const std::string>()),
              "high_contention", 0));
  MetricsQuery().OnlyMetrics({"high_contention"}).Run(scope, sink);
}

// Tests basic counter functionality in a low-contention domain (one label).
// Verifies that increments are recorded for the correct label and that storage
// is reset after being released.
TEST_F(MetricsQueryTest, LowContention) {
  auto scope = CreateCollectionScope({}, {"grpc.target"});
  auto storage = LowContentionDomain::GetStorage(scope, "example.com");
  std::vector<std::string> label_keys = {"grpc.target"};
  std::vector<std::string> label = {"example.com"};
  ::testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink,
              Counter(InstrumentLabelListElementsAreArray(label_keys),
                      ::testing::ElementsAreArray(label), "low_contention", 0));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Increment(LowContentionDomain::kCounter);
  EXPECT_CALL(sink,
              Counter(InstrumentLabelListElementsAreArray(label_keys),
                      ::testing::ElementsAreArray(label), "low_contention", 1));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage.reset();
  scope = CreateCollectionScope({}, {"grpc.target"});
  storage = LowContentionDomain::GetStorage(scope, "example.com");
  EXPECT_CALL(sink,
              Counter(InstrumentLabelListElementsAreArray(label_keys),
                      ::testing::ElementsAreArray(label), "low_contention", 0));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(scope, sink);
}

// Tests histogram functionality in a low-contention domain.
// Verifies that increments are recorded in the correct histogram bucket.
TEST_F(MetricsQueryTest, LowContentionHistogram) {
  auto scope = CreateCollectionScope({}, {"grpc.target"});
  std::vector<uint64_t> value_before;
  auto storage = LowContentionDomain::GetStorage(scope, "example.com");
  ::testing::StrictMock<MockMetricsSink> sink;
  std::vector<std::string> label_keys = {"grpc.target"};
  std::vector<std::string> label = {"example.com"};
  EXPECT_CALL(sink,
              Histogram(InstrumentLabelListElementsAreArray(label_keys),
                        ::testing::ElementsAreArray(label),
                        "exponential_histogram", ::testing::_, ::testing::_))
      .WillOnce([&value_before](auto, auto, auto, auto, auto counts) {
        value_before.assign(counts.begin(), counts.end());
      });
  MetricsQuery()
      .OnlyMetrics({"exponential_histogram"})
      .WithLabelEq("grpc.target", "example.com")
      .Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  ASSERT_FALSE(value_before.empty());
  std::vector<uint64_t> expect_value = value_before;
  expect_value[0] += 1;
  storage->Increment(LowContentionDomain::kExponentialHistogram, 0);
  EXPECT_CALL(sink,
              Histogram(InstrumentLabelListElementsAreArray(label_keys),
                        absl::MakeConstSpan(label), "exponential_histogram",
                        ::testing::_, absl::MakeConstSpan(expect_value)))
      .Times(1);
  MetricsQuery()
      .OnlyMetrics({"exponential_histogram"})
      .WithLabelEq("grpc.target", "example.com")
      .Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
}

// Tests gauge functionality (double, int, uint) in a low-contention domain.
// Verifies that a GaugeProvider can register itself and provide correct values
// during a query.
TEST_F(MetricsQueryTest, LowContentionGauge) {
  auto scope = CreateCollectionScope({}, {"grpc.target"});
  auto storage = LowContentionDomain::GetStorage(scope, "example.com");
  std::vector<std::string> label_keys = {"grpc.target"};
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

  EXPECT_CALL(sink, DoubleGauge(InstrumentLabelListElementsAreArray(
                                    DomainLabels<LowContentionDomain>()),
                                ::testing::ElementsAreArray(label),
                                "double_gauge", 1.23));
  EXPECT_CALL(sink,
              IntGauge(InstrumentLabelListElementsAreArray(
                           DomainLabels<LowContentionDomain>()),
                       ::testing::ElementsAreArray(label), "int_gauge", -456));
  EXPECT_CALL(sink,
              UintGauge(InstrumentLabelListElementsAreArray(
                            DomainLabels<LowContentionDomain>()),
                        ::testing::ElementsAreArray(label), "uint_gauge", 789));
  MetricsQuery()
      .OnlyMetrics({"double_gauge", "int_gauge", "uint_gauge"})
      .Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
}

// Tests metric collection across multiple label sets ("fan-out").
// Verifies that metrics for different label combinations are reported correctly
// and that collapsing labels aggregates the results as expected.
TEST_F(MetricsQueryTest, FanOut) {
  auto scope = CreateCollectionScope({}, {"grpc.target", "grpc.method"});
  std::vector<std::string> label_keys = {"grpc.target", "grpc.method"};
  auto storage_foo = FanOutDomain::GetStorage(scope, "example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = FanOutDomain::GetStorage(scope, "example.com", "bar");
  std::vector<std::string> label_bar = {"example.com", "bar"};
  {
    ::testing::StrictMock<MockMetricsSink> sink;
    EXPECT_CALL(sink,
                Counter(InstrumentLabelListElementsAreArray(label_keys),
                        ::testing::ElementsAreArray(label_foo), "fan_out", 0));
    EXPECT_CALL(sink,
                Counter(InstrumentLabelListElementsAreArray(label_keys),
                        ::testing::ElementsAreArray(label_bar), "fan_out", 0));
    MetricsQuery().OnlyMetrics({"fan_out"}).Run(scope, sink);
  }
  storage_foo->Increment(FanOutDomain::kCounter);
  storage_bar->Increment(FanOutDomain::kCounter);
  {
    ::testing::StrictMock<MockMetricsSink> sink;
    EXPECT_CALL(sink,
                Counter(InstrumentLabelListElementsAreArray(label_keys),
                        ::testing::ElementsAreArray(label_foo), "fan_out", 1));
    EXPECT_CALL(sink,
                Counter(InstrumentLabelListElementsAreArray(label_keys),
                        ::testing::ElementsAreArray(label_bar), "fan_out", 1));
    MetricsQuery().OnlyMetrics({"fan_out"}).Run(scope, sink);
  }
  {
    const std::vector<std::string> label_all = {"example.com"};
    ::testing::StrictMock<MockMetricsSink> sink;
    EXPECT_CALL(sink,
                Counter(::testing::ResultOf(
                            LabelListToVector,
                            ::testing::ElementsAreArray(
                                std::vector<std::string>({"grpc.target"}))),
                        ::testing::ElementsAreArray(label_all), "fan_out", 2));
    MetricsQuery()
        .OnlyMetrics({"fan_out"})
        .CollapseLabels({InstrumentLabel("grpc.method")})
        .Run(scope, sink);
  }
  storage_foo.reset();
  storage_bar.reset();
  {
    ::testing::StrictMock<MockMetricsSink> sink;
    EXPECT_CALL(sink,
                Counter(InstrumentLabelListElementsAreArray(label_keys),
                        ::testing::ElementsAreArray(label_foo), "fan_out", 1));
    EXPECT_CALL(sink,
                Counter(InstrumentLabelListElementsAreArray(label_keys),
                        ::testing::ElementsAreArray(label_bar), "fan_out", 1));
    MetricsQuery().OnlyMetrics({"fan_out"}).Run(scope, sink);
  }
  storage_foo = FanOutDomain::GetStorage(scope, "example.com", "foo");
  storage_bar = FanOutDomain::GetStorage(scope, "example.com", "bar");
  {
    ::testing::StrictMock<MockMetricsSink> sink;
    EXPECT_CALL(sink,
                Counter(InstrumentLabelListElementsAreArray(label_keys),
                        ::testing::ElementsAreArray(label_foo), "fan_out", 1));
    EXPECT_CALL(sink,
                Counter(InstrumentLabelListElementsAreArray(label_keys),
                        ::testing::ElementsAreArray(label_bar), "fan_out", 1));
    MetricsQuery().OnlyMetrics({"fan_out"}).Run(scope, sink);
  }
  {
    const std::vector<std::string> label_all = {"example.com"};
    ::testing::StrictMock<MockMetricsSink> sink;
    EXPECT_CALL(sink,
                Counter(InstrumentLabelListElementsAreArray(
                            std::vector<std::string>({"grpc.target"})),
                        ::testing::ElementsAreArray(label_all), "fan_out", 2));
    MetricsQuery()
        .OnlyMetrics({"fan_out"})
        .CollapseLabels({InstrumentLabel("grpc.method")})
        .Run(scope, sink);
  }
}

// Tests gauge functionality with multiple label sets.
// Verifies that gauges for different label combinations are reported correctly
// and that label filtering works. It also confirms that gauges are not
// aggregated when labels are collapsed.
TEST_F(MetricsQueryTest, FanOutGauge) {
  auto scope = CreateCollectionScope({}, {"grpc.target", "grpc.method"});
  std::vector<std::string> label_keys = {"grpc.target", "grpc.method"};
  auto storage_foo = FanOutDomain::GetStorage(scope, "example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = FanOutDomain::GetStorage(scope, "example.com", "bar");
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

  EXPECT_CALL(sink, DoubleGauge(InstrumentLabelListElementsAreArray(
                                    DomainLabels<FanOutDomain>()),
                                ::testing::ElementsAreArray(label_foo),
                                "fan_out_double", 1.1));
  EXPECT_CALL(sink, DoubleGauge(InstrumentLabelListElementsAreArray(
                                    DomainLabels<FanOutDomain>()),
                                ::testing::ElementsAreArray(label_bar),
                                "fan_out_double", 2.2));
  MetricsQuery().OnlyMetrics({"fan_out_double"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // Test label equality filter
  EXPECT_CALL(
      sink, DoubleGauge(
                ::testing::ResultOf(
                    LabelListToVector,
                    ::testing::ElementsAreArray(DomainLabels<FanOutDomain>())),
                ::testing::ElementsAreArray(label_foo), "fan_out_double", 1.1));
  MetricsQuery()
      .OnlyMetrics({"fan_out_double"})
      .WithLabelEq("grpc.method", "foo")
      .Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // Test collapsing - Gauges are not aggregated.
  EXPECT_CALL(
      sink, DoubleGauge(::testing::_, ::testing::_, ::testing::_, ::testing::_))

      .Times(0);
  MetricsQuery()
      .OnlyMetrics({"fan_out_double"})
      .CollapseLabels({InstrumentLabel("grpc.method")})
      .Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
}

// Tests the `WithLabelEq` filter in MetricsQuery.
// Verifies that only metrics matching the specified label values are returned.
TEST_F(MetricsQueryTest, LowContentionUpDownCounter) {
  const InstrumentLabelSet kLabels = {"grpc.target"};
  auto scope = CreateCollectionScope({}, kLabels);
  auto storage = LowContentionDomain::GetStorage(scope, "example.com");
  std::vector<std::string> label_keys = {"grpc.target"};
  std::vector<std::string> label = {"example.com"};
  ::testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(
      sink,
      UpDownCounter(InstrumentLabelListElementsAreArray(label_keys),
                    ::testing::ElementsAreArray(label), "up_down_counter", 0));
  MetricsQuery().OnlyMetrics({"up_down_counter"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Increment(LowContentionDomain::kUpDownCounter);
  EXPECT_CALL(
      sink,
      UpDownCounter(InstrumentLabelListElementsAreArray(label_keys),
                    ::testing::ElementsAreArray(label), "up_down_counter", 1));
  MetricsQuery().OnlyMetrics({"up_down_counter"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Decrement(LowContentionDomain::kUpDownCounter);
  EXPECT_CALL(
      sink,
      UpDownCounter(InstrumentLabelListElementsAreArray(label_keys),
                    ::testing::ElementsAreArray(label), "up_down_counter", 0));
  MetricsQuery().OnlyMetrics({"up_down_counter"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage.reset();
  scope = CreateCollectionScope({}, kLabels);
  storage = LowContentionDomain::GetStorage(scope, "example.com");
  EXPECT_CALL(
      sink,
      UpDownCounter(InstrumentLabelListElementsAreArray(label_keys),
                    ::testing::ElementsAreArray(label), "up_down_counter", 0));
  MetricsQuery().OnlyMetrics({"up_down_counter"}).Run(scope, sink);
}

TEST_F(MetricsQueryTest, FanOutUpDownCounter) {
  const InstrumentLabelSet kLabels = {"grpc.target", "grpc.method"};
  auto scope = CreateCollectionScope({}, kLabels);
  std::vector<std::string> label_keys = {"grpc.target", "grpc.method"};
  auto storage_foo = FanOutDomain::GetStorage(scope, "example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = FanOutDomain::GetStorage(scope, "example.com", "bar");
  std::vector<std::string> label_bar = {"example.com", "bar"};
  storage_foo->Increment(FanOutDomain::kUpDownCounter);
  storage_bar->Increment(FanOutDomain::kUpDownCounter);
  storage_bar->Increment(FanOutDomain::kUpDownCounter);
  {
    ::testing::StrictMock<MockMetricsSink> sink;
    EXPECT_CALL(sink,
                UpDownCounter(InstrumentLabelListElementsAreArray(label_keys),
                              ::testing::ElementsAreArray(label_foo),
                              "fan_out_up_down", 1));
    EXPECT_CALL(sink,
                UpDownCounter(InstrumentLabelListElementsAreArray(label_keys),
                              ::testing::ElementsAreArray(label_bar),
                              "fan_out_up_down", 2));
    MetricsQuery().OnlyMetrics({"fan_out_up_down"}).Run(scope, sink);
  }
  {
    const std::vector<std::string> label_all = {"example.com"};
    ::testing::StrictMock<MockMetricsSink> sink;
    EXPECT_CALL(
        sink,
        UpDownCounter(
            ::testing::ResultOf(LabelListToVector,
                                ::testing::ElementsAreArray(
                                    std::vector<std::string>({"grpc.target"}))),
            ::testing::ElementsAreArray(label_all), "fan_out_up_down", 3));
    MetricsQuery()
        .OnlyMetrics({"fan_out_up_down"})
        .CollapseLabels({InstrumentLabel("grpc.method")})
        .Run(scope, sink);
  }
}

TEST_F(MetricsQueryTest, LabelEq) {
  auto scope = CreateCollectionScope({}, {"grpc.target", "grpc.method"});
  std::vector<std::string> label_keys = {"grpc.target", "grpc.method"};
  auto storage_foo = FanOutDomain::GetStorage(scope, "example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = FanOutDomain::GetStorage(scope, "example.com", "bar");
  auto storage_baz = FanOutDomain::GetStorage(scope, "example.org", "baz");
  ::testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(
      sink,
      Counter(InstrumentLabelListElementsAreArray(DomainLabels<FanOutDomain>()),
              ::testing::ElementsAreArray(label_foo), "fan_out", 0));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .WithLabelEq("grpc.target", "example.com")
      .WithLabelEq("grpc.method", "foo")
      .Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo->Increment(FanOutDomain::kCounter);
  storage_bar->Increment(FanOutDomain::kCounter);
  storage_baz->Increment(FanOutDomain::kCounter);
  EXPECT_CALL(
      sink,
      Counter(InstrumentLabelListElementsAreArray(DomainLabels<FanOutDomain>()),
              ::testing::ElementsAreArray(label_foo), "fan_out", 1));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .WithLabelEq("grpc.target", "example.com")
      .WithLabelEq("grpc.method", "foo")
      .Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
}

// A stress test that runs multiple threads concurrently, performing metric
// increments, gauge provider registrations, and metric queries.
// This is a "does it crash" test to check for race conditions.
TEST_F(MetricsQueryTest, ThreadStress) {
  auto scope = CreateCollectionScope({}, {});
  std::vector<std::thread> threads;
  std::atomic<bool> done = false;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&]() {
      auto storage = HighContentionDomain::GetStorage(scope);
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(HighContentionDomain::kCounter);
      }
    });
    threads.emplace_back([&]() {
      auto storage = LowContentionDomain::GetStorage(scope, "example.com");
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(LowContentionDomain::kCounter);
      }
    });
    threads.emplace_back([&]() {
      auto storage = LowContentionDomain::GetStorage(scope, "bar.com");
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(LowContentionDomain::kCounter);
      }
    });
    threads.emplace_back([&]() {
      auto storage = LowContentionDomain::GetStorage(scope, "example.com");
      absl::BitGen gen;
      while (!done.load(std::memory_order_relaxed)) {
        storage->Increment(LowContentionDomain::kExponentialHistogram,
                           absl::Uniform(gen, 0, 1024));
      }
    });
    threads.emplace_back([&]() {
      class NoopSink final : public MetricsSink {
       public:
        void Counter(InstrumentLabelList label_keys,
                     absl::Span<const std::string> label,
                     absl::string_view name, uint64_t value) override {}
        void UpDownCounter(InstrumentLabelList label_keys,
                           absl::Span<const std::string> label,
                           absl::string_view name, uint64_t value) override {}
        void Histogram(InstrumentLabelList label_keys,
                       absl::Span<const std::string> label,
                       absl::string_view name, HistogramBuckets bounds,
                       absl::Span<const uint64_t> counts) override {}
        void DoubleGauge(InstrumentLabelList label_keys,
                         absl::Span<const std::string> labels,
                         absl::string_view name, double value) override {}
        void IntGauge(InstrumentLabelList label_keys,
                      absl::Span<const std::string> labels,
                      absl::string_view name, int64_t value) override {}
        void UintGauge(InstrumentLabelList label_keys,
                       absl::Span<const std::string> labels,
                       absl::string_view name, uint64_t value) override {}
      };
      NoopSink sink;
      while (!done.load(std::memory_order_relaxed)) {
        MetricsQuery().Run(scope, sink);
      }
    });
    threads.emplace_back([&]() {
      auto storage = LowContentionDomain::GetStorage(scope, "gauge_stress.com");
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
  auto scope = CreateCollectionScope({}, {});
  ::testing::MockFunction<void(
      const InstrumentMetadata::Description* instrument,
      absl::Span<const std::string> labels, int64_t value)>
      hook;
  RegisterHistogramCollectionHook(hook.AsStdFunction());
  auto storage = LowContentionDomain::GetStorage(scope, "example.com");
  std::vector<std::string> label = {std::string(kOmittedLabel)};
  EXPECT_CALL(hook, Call(::testing::_, ::testing::ElementsAreArray(label), 10));
  storage->Increment(LowContentionDomain::kExponentialHistogram, 10);
  ::testing::Mock::VerifyAndClearExpectations(&hook);
}

// Tests that multiple registered histogram collection hooks are all called when
// a histogram is incremented.
TEST_F(InstrumentTest, MultipleHistogramHooks) {
  auto scope = CreateCollectionScope({}, {});
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
  auto storage = LowContentionDomain::GetStorage(scope, "example.com");
  std::vector<std::string> label = {std::string(kOmittedLabel)};
  EXPECT_CALL(hook1,
              Call(::testing::_, ::testing::ElementsAreArray(label), 10));
  EXPECT_CALL(hook2,
              Call(::testing::_, ::testing::ElementsAreArray(label), 10));
  storage->Increment(LowContentionDomain::kExponentialHistogram, 10);
  ::testing::Mock::VerifyAndClearExpectations(&hook1);
  ::testing::Mock::VerifyAndClearExpectations(&hook2);
}

TEST_F(InstrumentLabelListTest, FixedToList) {
  FixedInstrumentLabelList<2> fixed("label1", "label2");
  InstrumentLabelList list = fixed.ToList();
  EXPECT_EQ(list.size(), 2);
  EXPECT_EQ(list[0].label(), "label1");
  EXPECT_EQ(list[1].label(), "label2");
}

// Verifies that calling GetStorage with the same labels multiple times returns
// a pointer to the same Storage instance, as long as a strong reference is
// held.
TEST_F(GetStorageTest, SameInstanceForRepeatedCalls) {
  auto scope = CreateCollectionScope({}, {});
  auto storage1 = LowContentionDomain::GetStorage(scope, "test.com");
  auto storage2 = LowContentionDomain::GetStorage(scope, "test.com");
  EXPECT_EQ(storage1.get(), storage2.get());
}

// Tests that a Storage instance created *after* a CollectionScope has been
// created is still visible and included in the metric query results for that
// scope.
TEST_F(MetricsQueryTest, NewStorageVisibleInQuery) {
  ::testing::StrictMock<MockMetricsSink> sink;
  std::vector<std::string> label = {std::string(kOmittedLabel)};
  auto scope = CreateCollectionScope({}, {});

  // Initial query, storage doesn't exist yet.
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);

  // Storage created *after* scope.
  auto storage = LowContentionDomain::GetStorage(scope, "new_metric.com");
  storage->Increment(LowContentionDomain::kCounter);

  // Query again with the same scope, new storage should be visible.
  EXPECT_CALL(sink,
              Counter(InstrumentLabelListElementsAreArray(
                          DomainLabels<LowContentionDomain>()),
                      ::testing::ElementsAreArray(label), "low_contention", 1));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(scope, sink);
  ::testing::Mock::VerifyAndClearExpectations(&sink);
}

// Verifies that a CollectionScope created via CreateCollectionScope takes a
// snapshot of the existing metrics, which are then readable via
// MetricsQuery::Run.
TEST_F(InstrumentTest, CollectionScopeSnapshotsExistingMetrics) {
  auto scope = CreateCollectionScope({}, {});
  // Create some metrics *before* the scope is created.
  auto storage1 = LowContentionDomain::GetStorage(scope, "test1.com");
  storage1->Increment(LowContentionDomain::kCounter);
  auto storage2 = FanOutDomain::GetStorage(scope, "target1", "method1");
  for (int i = 0; i < 5; ++i) {
    storage2->Increment(FanOutDomain::kCounter);
  }

  // Query the data.
  ::testing::StrictMock<MockMetricsSink> sink;
  std::vector<std::string> low_contention_label = {std::string(kOmittedLabel)};
  std::vector<std::string> fan_out_label = {std::string(kOmittedLabel),
                                            std::string(kOmittedLabel)};
  EXPECT_CALL(sink, Counter(::testing::ResultOf(
                                LabelListToVector,
                                ::testing::ElementsAreArray(
                                    DomainLabels<LowContentionDomain>())),
                            ::testing::ElementsAreArray(low_contention_label),
                            "low_contention", 1));
  EXPECT_CALL(
      sink, Counter(::testing::ResultOf(LabelListToVector,
                                        ::testing::ElementsAreArray(
                                            DomainLabels<FanOutDomain>())),
                    ::testing::ElementsAreArray(fan_out_label), "fan_out", 5));
  MetricsQuery().OnlyMetrics({"low_contention", "fan_out"}).Run(scope, sink);
}

// Verifies that metrics created *after* a CollectionScope is created are still
// visible to that scope, verifying the live-update mechanism.
TEST_F(InstrumentTest, CollectionScopeSeesNewMetrics) {
  // Create the scope first.
  auto scope = CreateCollectionScope({}, {});

  // Create metrics *after* the scope exists.
  auto storage1 = LowContentionDomain::GetStorage(scope, "test1.com");
  storage1->Increment(LowContentionDomain::kCounter);
  auto storage2 = FanOutDomain::GetStorage(scope, "target1", "method1");
  for (int i = 0; i < 5; ++i) {
    storage2->Increment(FanOutDomain::kCounter);
  }

  // Query the data using the original scope.
  ::testing::StrictMock<MockMetricsSink> sink;
  std::vector<std::string> low_contention_label = {std::string(kOmittedLabel)};
  std::vector<std::string> fan_out_label = {std::string(kOmittedLabel),
                                            std::string(kOmittedLabel)};
  EXPECT_CALL(sink, Counter(InstrumentLabelListElementsAreArray(
                                DomainLabels<LowContentionDomain>()),
                            ::testing::ElementsAreArray(low_contention_label),
                            "low_contention", 1));
  EXPECT_CALL(
      sink,
      Counter(InstrumentLabelListElementsAreArray(DomainLabels<FanOutDomain>()),
              ::testing::ElementsAreArray(fan_out_label), "fan_out", 5));
  MetricsQuery().OnlyMetrics({"low_contention", "fan_out"}).Run(scope, sink);
}

TEST_F(MetricsQueryTest, ScopedLabels) {
  auto scope = CreateCollectionScope({}, {"grpc.target"});
  auto s1 = FanOutDomain::GetStorage(scope, "t1", "m1");
  auto s2 = FanOutDomain::GetStorage(scope, "t1", "m2");
  s1->Increment(FanOutDomain::kCounter);
  s2->Increment(FanOutDomain::kCounter);
  std::vector<std::string> label = {"t1", std::string(kOmittedLabel)};
  ::testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(
      sink,
      Counter(InstrumentLabelListElementsAreArray(DomainLabels<FanOutDomain>()),
              ::testing::ElementsAreArray(label), "fan_out", 2));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(scope, sink);
}

TEST_F(MetricsQueryTest, StorageIsSharedWhenChildLabelsAreSameAsParent) {
  auto parent_scope = CreateCollectionScope({}, {"grpc.target"});
  auto child_scope = CreateCollectionScope({parent_scope}, {});
  auto s1 = FanOutDomain::GetStorage(parent_scope, "t1", "m1");
  auto s2 = FanOutDomain::GetStorage(child_scope, "t1", "m1");
  EXPECT_EQ(s1.get(), s2.get());
}

TEST_F(MetricsQueryTest, StorageIsNotSharedWhenChildLabelsAreDifferent) {
  auto parent_scope = CreateCollectionScope({}, {"grpc.target"});
  auto child_scope = CreateCollectionScope({parent_scope}, {"grpc.method"});
  auto s1 = FanOutDomain::GetStorage(parent_scope, "t1", "m1");
  auto s2 = FanOutDomain::GetStorage(child_scope, "t1", "m1");
  EXPECT_NE(s1.get(), s2.get());
  EXPECT_THAT(s1->label(),
              ::testing::ElementsAre("t1", std::string(kOmittedLabel)));
  EXPECT_THAT(s2->label(), ::testing::ElementsAre("t1", "m1"));
}

TEST_F(MetricsQueryTest, HierarchicalQuery) {
  auto parent_scope = CreateCollectionScope({}, {"grpc.target"});
  auto child_scope = CreateCollectionScope({parent_scope}, {"grpc.method"});
  auto s1 = FanOutDomain::GetStorage(parent_scope, "t1", "m1");
  auto s2 = FanOutDomain::GetStorage(child_scope, "t2", "m2");
  s1->Increment(FanOutDomain::kCounter);
  s2->Increment(FanOutDomain::kCounter);
  std::vector<std::string> label1 = {"t1", std::string(kOmittedLabel)};
  std::vector<std::string> label2 = {"t2", "m2"};
  ::testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(
      sink,
      Counter(InstrumentLabelListElementsAreArray(DomainLabels<FanOutDomain>()),
              ::testing::ElementsAreArray(label1), "fan_out", 1));
  EXPECT_CALL(
      sink,
      Counter(InstrumentLabelListElementsAreArray(DomainLabels<FanOutDomain>()),
              ::testing::ElementsAreArray(label2), "fan_out", 1));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(parent_scope, sink);
}

TEST_F(MetricsQueryTest, AggregationOnChildDestruction) {
  auto parent_scope = CreateCollectionScope({}, {"grpc.target"});
  auto child_scope = CreateCollectionScope({parent_scope}, {"grpc.method"});
  auto s_p = FanOutDomain::GetStorage(parent_scope, "t1", "m1");
  auto s_c = FanOutDomain::GetStorage(child_scope, "t1", "m1");
  s_p->Increment(FanOutDomain::kCounter);
  s_c->Increment(FanOutDomain::kCounter);
  child_scope.reset();
  std::vector<std::string> label = {"t1", std::string(kOmittedLabel)};
  ::testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(
      sink,
      Counter(InstrumentLabelListElementsAreArray(DomainLabels<FanOutDomain>()),
              ::testing::ElementsAreArray(label), "fan_out", 2));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(parent_scope, sink);
}

// Verifies that CreateCollectionScope creates a valid scope.
TEST_F(InstrumentTest, CreateCollectionScope) {
  auto scope = CreateCollectionScope({}, {});
  ASSERT_NE(scope, nullptr);
}

TEST_F(MetricsQueryTest, AggregationToMultipleParents) {
  auto p1 = CreateCollectionScope({}, {"grpc.target"});
  auto p2 = CreateCollectionScope({}, {"grpc.method"});
  auto s_p1 = FanOutDomain::GetStorage(p1, "t", "m");
  auto s_p2 = FanOutDomain::GetStorage(p2, "t", "m");
  s_p1->Increment(FanOutDomain::kCounter);
  s_p2->Increment(FanOutDomain::kCounter);
  {
    auto child = CreateCollectionScope({p1, p2}, {});
    auto s_c = FanOutDomain::GetStorage(child, "t", "m");
    s_c->Increment(FanOutDomain::kCounter);
  }
  // child scope destroyed, s_c should be aggregated to p1 and p2.
  ::testing::StrictMock<MockMetricsSink> sink1;
  ::testing::StrictMock<MockMetricsSink> sink2;
  std::vector<std::string> label1 = {"t", std::string(kOmittedLabel)};
  std::vector<std::string> label2 = {std::string(kOmittedLabel), "m"};
  EXPECT_CALL(
      sink1,
      Counter(InstrumentLabelListElementsAreArray(DomainLabels<FanOutDomain>()),
              ::testing::ElementsAreArray(label1), "fan_out", 2));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(p1, sink1);
  EXPECT_CALL(
      sink2,
      Counter(InstrumentLabelListElementsAreArray(DomainLabels<FanOutDomain>()),
              ::testing::ElementsAreArray(label2), "fan_out", 2));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(p2, sink2);
}

TEST_F(MetricsQueryTest, StorageNotSharedWithMultipleParents) {
  auto p1 = CreateCollectionScope({}, {"grpc.target"});
  auto p2 = CreateCollectionScope({}, {"grpc.target"});
  auto child = CreateCollectionScope({p1, p2}, {});
  auto s1 = FanOutDomain::GetStorage(p1, "t", "m");
  auto sc = FanOutDomain::GetStorage(child, "t", "m");
  EXPECT_NE(s1.get(), sc.get());
}

TEST_F(InstrumentLabelTest, RegistrationConsistency) {
  InstrumentLabel label1("foo");
  InstrumentLabel label2("foo");
  EXPECT_EQ(label1.index(), label2.index());
  EXPECT_EQ(label1.label(), "foo");
  EXPECT_EQ(label2.label(), "foo");
  EXPECT_EQ(label1, label2);
}

TEST_F(InstrumentLabelTest, RegistrationUniqueness) {
  InstrumentLabel label1("foo");
  InstrumentLabel label2("bar");
  EXPECT_NE(label1.index(), label2.index());
  EXPECT_EQ(label1.label(), "foo");
  EXPECT_EQ(label2.label(), "bar");
  EXPECT_NE(label1, label2);
}

TEST_F(InstrumentLabelTest, Comparison) {
  InstrumentLabel label1("foo");
  InstrumentLabel label2("bar");
  InstrumentLabel label3("foo");
  EXPECT_EQ(label1, label3);
  EXPECT_NE(label1, label2);
  EXPECT_TRUE(label1 == label3);
  EXPECT_FALSE(label1 == label2);
  EXPECT_TRUE(label1 != label2);
  EXPECT_FALSE(label1 != label3);
}

TEST_F(InstrumentLabelTest, CopyAndMove) {
  InstrumentLabel label1("foo");
  InstrumentLabel label2(label1);
  EXPECT_EQ(label1, label2);
  EXPECT_EQ(label1.index(), label2.index());

  // Disable linter -- the move *is* unnecessary, but we want to test it anyway.
  // NOLINTNEXTLINE(performance-move-const-arg)
  InstrumentLabel label3(std::move(label1));
  EXPECT_EQ(label2, label3);
  EXPECT_EQ(label2.index(), label3.index());

  InstrumentLabel label4("bar");
  label4 = label2;
  EXPECT_EQ(label2, label4);
  EXPECT_EQ(label2.index(), label4.index());

  InstrumentLabel label5("baz");
  // Disable linter -- the move *is* unnecessary, but we want to test it anyway.
  // NOLINTNEXTLINE(performance-move-const-arg)
  label5 = std::move(label2);
  EXPECT_EQ(label3, label5);
  EXPECT_EQ(label3.index(), label5.index());
}

TEST_F(InstrumentLabelListTest, AppendAndIterate) {
  InstrumentLabelList list;
  InstrumentLabel label1("foo");
  InstrumentLabel label2("bar");
  list.Append(label1);
  list.Append(label2);
  EXPECT_EQ(list.size(), 2);
  EXPECT_EQ(list[0], label1);
  EXPECT_EQ(list[1], label2);
}

TEST_F(InstrumentLabelListTest, SetToList) {
  InstrumentLabelSet set;
  InstrumentLabel label1("foo");
  InstrumentLabel label2("bar");
  set.Set(label1);
  set.Set(label2);
  InstrumentLabelList list = set.ToList();
  EXPECT_EQ(list.size(), 2);
  // Order is not guaranteed by Set, but we can check for presence.
  bool found_foo = false;
  bool found_bar = false;
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i] == label1) found_foo = true;
    if (list[i] == label2) found_bar = true;
  }
  EXPECT_TRUE(found_foo);
  EXPECT_TRUE(found_bar);
}

TEST_F(InstrumentLabelListTest, CopyAndMove) {
  InstrumentLabelList list1;
  InstrumentLabel label1("foo");
  InstrumentLabel label2("bar");
  list1.Append(label1);
  list1.Append(label2);

  InstrumentLabelList list2(list1);
  EXPECT_EQ(list2.size(), 2);
  EXPECT_EQ(list2[0].label(), "foo");
  EXPECT_EQ(list2[1].label(), "bar");

  // Disable linter -- the move *is* unnecessary, but we want to test it anyway.
  // NOLINTNEXTLINE(performance-move-const-arg)
  InstrumentLabelList list3(std::move(list1));
  EXPECT_EQ(list3.size(), 2);
  EXPECT_EQ(list3[0].label(), "foo");
  EXPECT_EQ(list3[1].label(), "bar");

  InstrumentLabelList list4;
  list4 = list2;
  EXPECT_EQ(list4.size(), 2);
  EXPECT_EQ(list4[0].label(), "foo");
  EXPECT_EQ(list4[1].label(), "bar");

  InstrumentLabelList list5;
  // Disable linter -- the move *is* unnecessary, but we want to test it anyway.
  // NOLINTNEXTLINE(performance-move-const-arg)
  list5 = std::move(list2);
  EXPECT_EQ(list5.size(), 2);
  EXPECT_EQ(list5[0].label(), "foo");
  EXPECT_EQ(list5[1].label(), "bar");
}

TEST_F(InstrumentIndexTest, RegistrationAndLookup) {
  // The instruments are already registered by the static initializers in
  // other tests, so we can just check if they exist.
  const auto* desc = InstrumentIndex::Get().Find("low_contention");
  ASSERT_NE(desc, nullptr);
  EXPECT_EQ(desc->name, "low_contention");
  EXPECT_EQ(desc->unit, "unit");

  desc = InstrumentIndex::Get().Find("non_existent_metric");
  EXPECT_EQ(desc, nullptr);
}

TEST_F(DomainStorageTest, CounterIncrement) {
  auto scope = CreateCollectionScope({}, {"grpc.target"});
  auto storage = LowContentionDomain::GetStorage(scope, "example.com");
  storage->Increment(LowContentionDomain::kCounter);
  EXPECT_EQ(storage->SumCounter(LowContentionDomain::kCounter.offset()), 1);
}

TEST_F(DomainStorageTest, GaugeProvider) {
  auto scope = CreateCollectionScope({}, {"grpc.target"});
  auto storage = LowContentionDomain::GetStorage(scope, "example.com");
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
    }
  };
  MyGaugeProvider provider(storage);
  instrument_detail::GaugeStorage gauge_storage(storage->domain());
  storage->FillGaugeStorage(gauge_storage);
  EXPECT_EQ(gauge_storage.GetDouble(LowContentionDomain::kDoubleGauge.offset()),
            1.23);
}

TEST_F(CollectionScopeTest, StorageCollection) {
  auto scope = CreateCollectionScope({}, {"grpc.target"});
  auto storage = LowContentionDomain::GetStorage(scope, "example.com");
  std::vector<instrument_detail::DomainStorage*> storages;
  scope->ForEachUniqueStorage(
      [&](instrument_detail::DomainStorage* s) { storages.push_back(s); });
  EXPECT_EQ(storages.size(), 1);
  EXPECT_EQ(storages[0], storage.get());
}

TEST_F(CollectionScopeTest, HierarchicalStorageCollection) {
  LOG(INFO) << "Labels array: " << InstrumentLabel::RegistrationDebugString();
  GRPC_CHECK_EQ(FanOutDomain::Domain()->label_names()[0].label(),
                "grpc.target");
  GRPC_CHECK_EQ(FanOutDomain::Domain()->label_names()[1].label(),
                "grpc.method");
  auto parent_scope = CreateCollectionScope({}, {"grpc.target"});
  auto child_scope = CreateCollectionScope({parent_scope}, {"grpc.method"});
  auto s1 = FanOutDomain::GetStorage(parent_scope, "t1", "m1");
  auto s2 = FanOutDomain::GetStorage(child_scope, "t2", "m2");
  std::vector<instrument_detail::DomainStorage*> storages;
  parent_scope->ForEachUniqueStorage(
      [&](instrument_detail::DomainStorage* s) { storages.push_back(s); });
  EXPECT_EQ(storages.size(), 2);
  bool found_s1 = false;
  bool found_s2 = false;
  for (auto* s : storages) {
    if (s == s1.get()) found_s1 = true;
    if (s == s2.get()) found_s2 = true;
  }
  EXPECT_TRUE(found_s1);
  EXPECT_TRUE(found_s2);
}

}  // namespace grpc_core
