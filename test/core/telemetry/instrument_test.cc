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

namespace grpc_core {
namespace {

class MockMetricsSink : public MetricsSink {
 public:
  virtual ~MockMetricsSink() = default;
  MOCK_METHOD(void, Counter,
              (absl::Span<const std::string> label, absl::string_view name,
               uint64_t value),
              (override));
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

TEST(InstrumentIndexTest, RegisterAndFind) {
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

TEST(InstrumentIndexDeathTest, RegisterDuplicate) {
  InstrumentIndex& index = InstrumentIndex::Get();
  index.Register(nullptr, 1, "duplicate_metric", "Desc 1", "units", {});
  EXPECT_DEATH(
      index.Register(nullptr, 2, "duplicate_metric", "Desc 2", "units", {}),
      "Metric with name 'duplicate_metric' already registered.");
}

TEST(MetricsQueryTest, HighContention) {
  uint64_t value_before;
  auto storage = kHighContentionDomain->GetStorage();
  testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(), "high_contention",
                            testing::_))
      .WillOnce(testing::SaveArg<2>(&value_before));
  MetricsQuery().OnlyMetrics({"high_contention"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Increment(kHighContentionCounter);
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(), "high_contention",
                            value_before + 1));
  MetricsQuery().OnlyMetrics({"high_contention"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage.reset();
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(), "high_contention",
                            value_before + 1));
  MetricsQuery().OnlyMetrics({"high_contention"}).Run(sink);
}

TEST(MetricsQueryTest, LowContention) {
  uint64_t value_before;
  auto storage = kLowContentionDomain->GetStorage("example.com");
  std::vector<std::string> label = {"example.com"};
  testing::StrictMock<MockMetricsSink> sink;
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label),
                            "low_contention", testing::_))
      .WillOnce(testing::SaveArg<2>(&value_before));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage->Increment(kLowContentionCounter);
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label),
                            "low_contention", value_before + 1));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage.reset();
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label),
                            "low_contention", value_before + 1));
  MetricsQuery().OnlyMetrics({"low_contention"}).Run(sink);
}

TEST(MetricsQueryTest, FanOut) {
  auto storage_foo = kFanOutDomain->GetStorage("example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = kFanOutDomain->GetStorage("example.com", "bar");
  std::vector<std::string> label_bar = {"example.com", "bar"};
  testing::StrictMock<MockMetricsSink> sink;
  uint64_t foo_before;
  uint64_t bar_before;
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label_foo), "fan_out",
                            testing::_))
      .WillOnce(testing::SaveArg<2>(&foo_before));
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label_bar), "fan_out",
                            testing::_))
      .WillOnce(testing::SaveArg<2>(&bar_before));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo->Increment(kFanOutCounter);
  storage_bar->Increment(kFanOutCounter);
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label_foo), "fan_out",
                            foo_before + 1));
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label_bar), "fan_out",
                            bar_before + 1));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo.reset();
  storage_bar.reset();
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label_foo), "fan_out",
                            foo_before + 1));
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label_bar), "fan_out",
                            bar_before + 1));
  MetricsQuery().OnlyMetrics({"fan_out"}).Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  std::vector<std::string> label_all = {"example.com"};
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label_all), "fan_out",
                            foo_before + bar_before + 2));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .CollapseLabels({"grpc.method"})
      .Run(sink);
}

TEST(MetricsQueryTest, LabelEq) {
  auto storage_foo = kFanOutDomain->GetStorage("example.com", "foo");
  std::vector<std::string> label_foo = {"example.com", "foo"};
  auto storage_bar = kFanOutDomain->GetStorage("example.com", "bar");
  auto storage_baz = kFanOutDomain->GetStorage("example.org", "baz");
  testing::StrictMock<MockMetricsSink> sink;
  uint64_t foo_before;
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label_foo), "fan_out",
                            testing::_))
      .WillOnce(testing::SaveArg<2>(&foo_before));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .WithLabelEq("grpc.target", "example.com")
      .WithLabelEq("grpc.method", "foo")
      .Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
  storage_foo->Increment(kFanOutCounter);
  storage_bar->Increment(kFanOutCounter);
  storage_baz->Increment(kFanOutCounter);
  EXPECT_CALL(sink, Counter(absl::Span<const std::string>(label_foo), "fan_out",
                            foo_before + 1));
  MetricsQuery()
      .OnlyMetrics({"fan_out"})
      .WithLabelEq("grpc.target", "example.com")
      .WithLabelEq("grpc.method", "foo")
      .Run(sink);
  testing::Mock::VerifyAndClearExpectations(&sink);
}

TEST(MetricsQueryTest, ThreadStressTest) {
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
      class NoopSink final : public MetricsSink {
       public:
        void Counter(absl::Span<const std::string> label,
                     absl::string_view name, uint64_t value) override {}
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
