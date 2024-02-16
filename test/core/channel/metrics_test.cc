// Copyright 2024 The gRPC Authors.
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

#include "src/core/lib/channel/metrics.h"

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

void AddKeyValuePairs(absl::Span<const absl::string_view> keys,
                      absl::Span<const absl::string_view> values,
                      std::vector<std::string>* key_value_pairs) {
  GPR_ASSERT(keys.size() == values.size());
  for (size_t i = 0; i < keys.size(); ++i) {
    key_value_pairs->push_back(absl::StrCat(keys[i], "=", values[i]));
  }
}

std::string MakeLabelString(
    absl::Span<const absl::string_view> label_keys,
    absl::Span<const absl::string_view> label_values,
    absl::Span<const absl::string_view> optional_label_keys,
    absl::Span<const absl::string_view> optional_values) {
  std::vector<std::string> key_value_pairs;
  AddKeyValuePairs(label_keys, label_values, &key_value_pairs);
  AddKeyValuePairs(optional_label_keys, optional_values, &key_value_pairs);
  return absl::StrJoin(key_value_pairs, ",");
}

class ChannelScope {
 public:
  ChannelScope(absl::string_view target, absl::string_view authority) {
    scope_.target = target;
    scope_.authority = authority;
  }

  absl::string_view target() const { return scope_.target; }
  absl::string_view authority() const { return scope_.authority; }

 private:
  StatsPlugin::Scope scope_;
};

// TODO(yijiem): Move this to test/core/util/fake_stats_plugin.h
class FakeStatsPlugin : public StatsPlugin {
 public:
  bool IsEnabledForChannel(const Scope& scope) const override {
    return channel_filter_(ChannelScope(scope.target, scope.authority));
  }

  bool IsEnabledForServer(const ChannelArgs& args) const override {
    return false;
  }

  void AddCounter(
      GlobalInstrumentsRegistry::GlobalUInt64CounterHandle handle,
      uint64_t value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override {
    // The problem with this approach is that we initialize uint64_counters_ in
    // BuildAndRegister by querying the GlobalInstrumentsRegistry at the time.
    // If the GlobalInstrumentsRegistry has changed since then (which we
    // currently don't allow), we might not have seen that descriptor nor have
    // we created an instrument for it. We probably could copy the existing
    // instruments at build time and for the handle that we haven't seen we will
    // just ignore it here. This would also prevent us from having to lock the
    // GlobalInstrumentsRegistry everytime a metric is recorded. But this is not
    // a concern for now.
    auto iter = uint64_counters_.find(handle.index);
    ASSERT_TRUE(iter != uint64_counters_.end());
    iter->second.Add(value, label_values, optional_values);
  }
  void AddCounter(
      GlobalInstrumentsRegistry::GlobalDoubleCounterHandle handle, double value,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override {
    auto iter = double_counters_.find(handle.index);
    ASSERT_TRUE(iter != double_counters_.end());
    iter->second.Add(value, label_values, optional_values);
  }
  void RecordHistogram(
      GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle handle,
      uint64_t value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override {
    auto iter = uint64_histograms_.find(handle.index);
    ASSERT_TRUE(iter != uint64_histograms_.end());
    iter->second.Record(value, label_values, optional_values);
  }
  void RecordHistogram(
      GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle handle,
      double value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override {
    auto iter = double_histograms_.find(handle.index);
    ASSERT_TRUE(iter != double_histograms_.end());
    iter->second.Record(value, label_values, optional_values);
  }

  absl::optional<uint64_t> GetCounterValue(
      GlobalInstrumentsRegistry::GlobalUInt64CounterHandle handle,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) {
    auto iter = uint64_counters_.find(handle.index);
    if (iter == uint64_counters_.end()) {
      return absl::nullopt;
    }
    return iter->second.GetValue(label_values, optional_values);
  }
  absl::optional<double> GetCounterValue(
      GlobalInstrumentsRegistry::GlobalDoubleCounterHandle handle,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) {
    auto iter = double_counters_.find(handle.index);
    if (iter == double_counters_.end()) {
      return absl::nullopt;
    }
    return iter->second.GetValue(label_values, optional_values);
  }
  absl::optional<std::vector<uint64_t>> GetHistogramValue(
      GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle handle,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) {
    auto iter = uint64_histograms_.find(handle.index);
    if (iter == uint64_histograms_.end()) {
      return absl::nullopt;
    }
    return iter->second.GetValues(label_values, optional_values);
  }
  absl::optional<std::vector<double>> GetHistogramValue(
      GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle handle,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) {
    auto iter = double_histograms_.find(handle.index);
    if (iter == double_histograms_.end()) {
      return absl::nullopt;
    }
    return iter->second.GetValues(label_values, optional_values);
  }

 private:
  friend class FakeStatsPluginBuilder;

  explicit FakeStatsPlugin(
      absl::AnyInvocable<bool(const ChannelScope& /*scope*/) const>
          channel_filter)
      : channel_filter_(std::move(channel_filter)) {
    GlobalInstrumentsRegistry::ForEach(
        [this](const GlobalInstrumentsRegistry::GlobalInstrumentDescriptor&
                   descriptor) {
          if (descriptor.instrument_type ==
              GlobalInstrumentsRegistry::InstrumentType::kCounter) {
            if (descriptor.value_type ==
                GlobalInstrumentsRegistry::ValueType::kUInt64) {
              uint64_counters_.emplace(descriptor.index, descriptor);
            } else {
              double_counters_.emplace(descriptor.index, descriptor);
            }
          } else {
            EXPECT_EQ(descriptor.instrument_type,
                      GlobalInstrumentsRegistry::InstrumentType::kHistogram);
            if (descriptor.value_type ==
                GlobalInstrumentsRegistry::ValueType::kUInt64) {
              uint64_histograms_.emplace(descriptor.index, descriptor);
            } else {
              double_histograms_.emplace(descriptor.index, descriptor);
            }
          }
        });
  }

  template <class T>
  class Counter {
   public:
    explicit Counter(GlobalInstrumentsRegistry::GlobalInstrumentDescriptor u)
        : name_(u.name),
          description_(u.description),
          unit_(u.unit),
          label_keys_(std::move(u.label_keys)),
          optional_label_keys_(std::move(u.optional_label_keys)) {}

    void Add(T t, absl::Span<const absl::string_view> label_values,
             absl::Span<const absl::string_view> optional_values) {
      auto iter = storage_.find(MakeLabelString(
          label_keys_, label_values, optional_label_keys_, optional_values));
      if (iter != storage_.end()) {
        iter->second += t;
      } else {
        storage_[MakeLabelString(label_keys_, label_values,
                                 optional_label_keys_, optional_values)] = t;
      }
    }

    absl::optional<T> GetValue(
        absl::Span<const absl::string_view> label_values,
        absl::Span<const absl::string_view> optional_values) {
      auto iter = storage_.find(MakeLabelString(
          label_keys_, label_values, optional_label_keys_, optional_values));
      if (iter == storage_.end()) {
        return absl::nullopt;
      }
      return iter->second;
    }

   private:
    absl::string_view name_;
    absl::string_view description_;
    absl::string_view unit_;
    std::vector<absl::string_view> label_keys_;
    std::vector<absl::string_view> optional_label_keys_;
    // Aggregation of the same key attributes.
    absl::flat_hash_map<std::string, T> storage_;
  };

  template <class T>
  class Histogram {
   public:
    explicit Histogram(GlobalInstrumentsRegistry::GlobalInstrumentDescriptor u)
        : name_(u.name),
          description_(u.description),
          unit_(u.unit),
          label_keys_(std::move(u.label_keys)),
          optional_label_keys_(std::move(u.optional_label_keys)) {}

    void Record(T t, absl::Span<const absl::string_view> label_values,
                absl::Span<const absl::string_view> optional_values) {
      std::string key = MakeLabelString(label_keys_, label_values,
                                        optional_label_keys_, optional_values);
      auto iter = storage_.find(key);
      if (iter == storage_.end()) {
        storage_.emplace(key, std::initializer_list<T>{t});
      } else {
        iter->second.push_back(t);
      }
    }

    absl::optional<std::vector<T>> GetValues(
        absl::Span<const absl::string_view> label_values,
        absl::Span<const absl::string_view> optional_values) {
      auto iter = storage_.find(MakeLabelString(
          label_keys_, label_values, optional_label_keys_, optional_values));
      if (iter == storage_.end()) {
        return absl::nullopt;
      }
      return iter->second;
    }

   private:
    absl::string_view name_;
    absl::string_view description_;
    absl::string_view unit_;
    std::vector<absl::string_view> label_keys_;
    std::vector<absl::string_view> optional_label_keys_;
    absl::flat_hash_map<std::string, std::vector<T>> storage_;
  };

  absl::AnyInvocable<bool(const ChannelScope& /*scope*/) const> channel_filter_;
  // Instruments.
  absl::flat_hash_map<uint32_t, Counter<uint64_t>> uint64_counters_;
  absl::flat_hash_map<uint32_t, Counter<double>> double_counters_;
  absl::flat_hash_map<uint32_t, Histogram<uint64_t>> uint64_histograms_;
  absl::flat_hash_map<uint32_t, Histogram<double>> double_histograms_;
};

// TODO(yijiem): Move this to test/core/util/fake_stats_plugin.h
class FakeStatsPluginBuilder {
 public:
  FakeStatsPluginBuilder& SetChannelFilter(
      absl::AnyInvocable<bool(const ChannelScope& /*scope*/) const>
          channel_filter) {
    channel_filter_ = std::move(channel_filter);
    return *this;
  }

  std::shared_ptr<FakeStatsPlugin> BuildAndRegister() {
    auto f = std::shared_ptr<FakeStatsPlugin>(
        new FakeStatsPlugin(std::move(channel_filter_)));
    GlobalStatsPluginRegistry::RegisterStatsPlugin(f);
    return f;
  }

 private:
  absl::AnyInvocable<bool(const ChannelScope& /*scope*/) const> channel_filter_;
};

class MetricsTest : public testing::Test {
 public:
  void SetUp() override {
    plugin1_ = FakeStatsPluginBuilder()
                   .SetChannelFilter([](const ChannelScope& scope) {
                     return absl::EndsWith(scope.target(),
                                           "domain1.domain2.domain3.domain4");
                   })
                   .BuildAndRegister();
    plugin2_ =
        FakeStatsPluginBuilder()
            .SetChannelFilter([](const ChannelScope& scope) {
              return absl::EndsWith(scope.target(), "domain2.domain3.domain4");
            })
            .BuildAndRegister();
    plugin3_ = FakeStatsPluginBuilder()
                   .SetChannelFilter([](const ChannelScope& scope) {
                     return absl::EndsWith(scope.target(), "domain3.domain4");
                   })
                   .BuildAndRegister();
  }

  void TearDown() override {
    GlobalStatsPluginRegistry::TestOnlyResetStatsPlugins();
  }

  static void SetUpTestSuite() {
    // Initializer list.
    uint64_counter_handle_ = GlobalInstrumentsRegistry::RegisterUInt64Counter(
        "uint64_counter", "A simple uint64 counter.", "unit",
        {"label_key_1", "label_key_2"},
        {"optional_label_key_1", "optional_label_key_2"});
    // C-style array.
    constexpr absl::string_view kLabelKeys[] = {"label_key_1", "label_key_2"};
    constexpr absl::string_view kOptionalLabelKeys[] = {"optional_label_key_1",
                                                        "optional_label_key_2"};
    double_counter_handle_ = GlobalInstrumentsRegistry::RegisterDoubleCounter(
        "double_counter", "A simple double counter.", "unit", kLabelKeys,
        kOptionalLabelKeys);
    // Explicit container.
    const std::vector<absl::string_view> kLabelKeysContainer = {"label_key_1",
                                                                "label_key_2"};
    const std::vector<absl::string_view> kOptionalLabelKeysContainer = {
        "optional_label_key_1", "optional_label_key_2"};
    uint64_histogram_handle_ =
        GlobalInstrumentsRegistry::RegisterUInt64Histogram(
            "uint64_histogram", "A simple uint64 histogram.", "unit",
            kLabelKeysContainer, kOptionalLabelKeysContainer);
    double_histogram_handle_ =
        GlobalInstrumentsRegistry::RegisterDoubleHistogram(
            "double_histogram", "A simple double histogram.", "unit",
            kLabelKeysContainer, kOptionalLabelKeysContainer);
  }

 protected:
  std::shared_ptr<FakeStatsPlugin> plugin1_;
  std::shared_ptr<FakeStatsPlugin> plugin2_;
  std::shared_ptr<FakeStatsPlugin> plugin3_;

  static GlobalInstrumentsRegistry::GlobalUInt64CounterHandle
      uint64_counter_handle_;
  static GlobalInstrumentsRegistry::GlobalDoubleCounterHandle
      double_counter_handle_;
  static GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle
      uint64_histogram_handle_;
  static GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle
      double_histogram_handle_;
};

GlobalInstrumentsRegistry::GlobalUInt64CounterHandle
    MetricsTest::uint64_counter_handle_;
GlobalInstrumentsRegistry::GlobalDoubleCounterHandle
    MetricsTest::double_counter_handle_;
GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle
    MetricsTest::uint64_histogram_handle_;
GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle
    MetricsTest::double_histogram_handle_;

TEST_F(MetricsTest, UInt64Counter) {
  constexpr absl::string_view kLabelValues[] = {"label_value_1",
                                                "label_value_2"};
  constexpr absl::string_view kOptionalLabelValues[] = {
      "optional_label_value_1", "optional_label_value_2"};
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain1.domain2.domain3.domain4"})
      .AddCounter(uint64_counter_handle_, 1, kLabelValues,
                  kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain2.domain3.domain4"})
      .AddCounter(uint64_counter_handle_, 2, kLabelValues,
                  kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain3.domain4"})
      .AddCounter(uint64_counter_handle_, 3, kLabelValues,
                  kOptionalLabelValues);
  EXPECT_THAT(plugin1_->GetCounterValue(uint64_counter_handle_, kLabelValues,
                                        kOptionalLabelValues),
              ::testing::Optional(1));
  EXPECT_THAT(plugin2_->GetCounterValue(uint64_counter_handle_, kLabelValues,
                                        kOptionalLabelValues),
              ::testing::Optional(3));
  EXPECT_THAT(plugin3_->GetCounterValue(uint64_counter_handle_, kLabelValues,
                                        kOptionalLabelValues),
              ::testing::Optional(6));
}

TEST_F(MetricsTest, DoubleCounter) {
  constexpr absl::string_view kLabelValues[] = {"label_value_1",
                                                "label_value_2"};
  constexpr absl::string_view kOptionalLabelValues[] = {
      "optional_label_value_1", "optional_label_value_2"};
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain1.domain2.domain3.domain4"})
      .AddCounter(double_counter_handle_, 1.23, kLabelValues,
                  kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain2.domain3.domain4"})
      .AddCounter(double_counter_handle_, 2.34, kLabelValues,
                  kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain3.domain4"})
      .AddCounter(double_counter_handle_, 3.45, kLabelValues,
                  kOptionalLabelValues);
  EXPECT_THAT(plugin1_->GetCounterValue(double_counter_handle_, kLabelValues,
                                        kOptionalLabelValues),
              ::testing::Optional(1.23));
  EXPECT_THAT(plugin2_->GetCounterValue(double_counter_handle_, kLabelValues,
                                        kOptionalLabelValues),
              ::testing::Optional(3.57));
  EXPECT_THAT(plugin3_->GetCounterValue(double_counter_handle_, kLabelValues,
                                        kOptionalLabelValues),
              ::testing::Optional(7.02));
}

TEST_F(MetricsTest, UInt64Histogram) {
  constexpr absl::string_view kLabelValues[] = {"label_value_1",
                                                "label_value_2"};
  constexpr absl::string_view kOptionalLabelValues[] = {
      "optional_label_value_1", "optional_label_value_2"};
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain1.domain2.domain3.domain4"})
      .RecordHistogram(uint64_histogram_handle_, 1, kLabelValues,
                       kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain2.domain3.domain4"})
      .RecordHistogram(uint64_histogram_handle_, 2, kLabelValues,
                       kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain3.domain4"})
      .RecordHistogram(uint64_histogram_handle_, 3, kLabelValues,
                       kOptionalLabelValues);
  EXPECT_THAT(plugin1_->GetHistogramValue(uint64_histogram_handle_,
                                          kLabelValues, kOptionalLabelValues),
              ::testing::Optional(::testing::UnorderedElementsAre(1)));
  EXPECT_THAT(plugin2_->GetHistogramValue(uint64_histogram_handle_,
                                          kLabelValues, kOptionalLabelValues),
              ::testing::Optional(::testing::UnorderedElementsAre(1, 2)));
  EXPECT_THAT(plugin3_->GetHistogramValue(uint64_histogram_handle_,
                                          kLabelValues, kOptionalLabelValues),
              ::testing::Optional(::testing::UnorderedElementsAre(1, 2, 3)));
}

TEST_F(MetricsTest, DoubleHistogram) {
  constexpr absl::string_view kLabelValues[] = {"label_value_1",
                                                "label_value_2"};
  constexpr absl::string_view kOptionalLabelValues[] = {
      "optional_label_value_1", "optional_label_value_2"};
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain1.domain2.domain3.domain4"})
      .RecordHistogram(double_histogram_handle_, 1.23, kLabelValues,
                       kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain2.domain3.domain4"})
      .RecordHistogram(double_histogram_handle_, 2.34, kLabelValues,
                       kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      {.target = "domain3.domain4"})
      .RecordHistogram(double_histogram_handle_, 3.45, kLabelValues,
                       kOptionalLabelValues);
  EXPECT_THAT(plugin1_->GetHistogramValue(double_histogram_handle_,
                                          kLabelValues, kOptionalLabelValues),
              ::testing::Optional(::testing::UnorderedElementsAre(1.23)));
  EXPECT_THAT(plugin2_->GetHistogramValue(double_histogram_handle_,
                                          kLabelValues, kOptionalLabelValues),
              ::testing::Optional(::testing::UnorderedElementsAre(1.23, 2.34)));
  EXPECT_THAT(
      plugin3_->GetHistogramValue(double_histogram_handle_, kLabelValues,
                                  kOptionalLabelValues),
      ::testing::Optional(::testing::UnorderedElementsAre(1.23, 2.34, 3.45)));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
