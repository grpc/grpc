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

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {

MetricsQuery& MetricsQuery::WithLabelEq(absl::string_view label,
                                        std::string value) {
  label_eqs_.emplace(label, std::move(value));
  return *this;
}

MetricsQuery& MetricsQuery::CollapseLabels(
    absl::Span<const std::string> labels) {
  for (const auto& label : labels) {
    collapsed_labels_.insert(label);
  }
  return *this;
}

MetricsQuery& MetricsQuery::OnlyMetrics(absl::Span<const std::string> metrics) {
  only_metrics_.emplace(metrics.begin(), metrics.end());
  return *this;
}

void MetricsQuery::Run(MetricsSink& sink) const {
  QueryableDomain::ExportMetrics(*this, sink);
}

const InstrumentIndex::Description* InstrumentIndex::Register(
    QueryableDomain* domain, uint64_t offset, absl::string_view name,
    absl::string_view description, absl::string_view unit, Shape shape) {
  auto it = metrics_.emplace(
      name, Description{domain, offset, name, description, unit, shape});
  if (!it.second) {
    LOG(FATAL) << "Metric with name '" << name << "' already registered.";
  }
  return &it.first->second;
}

const InstrumentIndex::Description* InstrumentIndex::Find(
    absl::string_view name) const {
  auto it = metrics_.find(name);
  if (it == metrics_.end()) {
    return nullptr;
  }
  return &it->second;
}

template <typename Fn>
void MetricsQuery::Apply(absl::Span<const std::string> label_names, Fn fn,
                         MetricsSink& sink) const {
  if (collapsed_labels_.empty()) {
    ApplyLabelChecks(label_names, std::move(fn), sink);
    return;
  }
  std::vector<size_t> include_labels;
  for (size_t i = 0; i < label_names.size(); ++i) {
    if (!collapsed_labels_.contains(label_names[i])) {
      include_labels.push_back(i);
    }
  }
  if (include_labels.size() == label_names.size()) {
    ApplyLabelChecks(label_names, std::move(fn), sink);
    return;
  }
  class Filter final : public MetricsSink {
   public:
    explicit Filter(absl::Span<const size_t> include_labels)
        : include_labels_(include_labels) {}

    void Counter(absl::Span<const std::string> label, absl::string_view name,
                 uint64_t value) override {
      uint64_counters_[ConstructKey(label, name)] += value;
    }

    void Histogram(absl::Span<const std::string> label, absl::string_view name,
                   HistogramBuckets bounds,
                   absl::Span<const uint64_t> counts) override {
      CHECK_EQ(counts.size(), bounds.size());
      auto it = histograms_.find(ConstructKey(label, name));
      if (it == histograms_.end()) {
        histograms_.emplace(std::piecewise_construct,
                            std::tuple(ConstructKey(label, name)),
                            std::tuple(bounds, counts));
      } else {
        if (it->second.bounds != bounds) {
          LOG(FATAL) << "Histogram bounds mismatch for metric '" << name
                     << "': {" << absl::StrJoin(it->second.bounds, ",")
                     << "} vs {" << absl::StrJoin(bounds, ",") << "}";
        }
        for (size_t i = 0; i < counts.size(); ++i) {
          it->second.counts[i] += counts[i];
        }
      }
    }

    void Publish(MetricsSink& sink) const {
      for (const auto& [key, value] : uint64_counters_) {
        sink.Counter(std::get<0>(key), std::get<1>(key), value);
      }
      for (const auto& [key, value] : histograms_) {
        sink.Histogram(std::get<0>(key), std::get<1>(key), value.bounds,
                       value.counts);
      }
    }

   private:
    std::tuple<std::vector<std::string>, absl::string_view> ConstructKey(
        absl::Span<const std::string> label, absl::string_view name) const {
      std::vector<std::string> key;
      key.reserve(include_labels_.size());
      for (auto i : include_labels_) {
        key.push_back(label[i]);
      }
      return std::tuple(std::move(key), name);
    }

    absl::Span<const size_t> include_labels_;
    absl::flat_hash_map<std::tuple<std::vector<std::string>, absl::string_view>,
                        uint64_t>
        uint64_counters_;
    struct HistogramValue {
      HistogramValue(HistogramBuckets bounds, absl::Span<const uint64_t> counts)
          : bounds(bounds), counts(counts.begin(), counts.end()) {}
      HistogramBuckets bounds;
      std::vector<uint64_t> counts;
    };
    absl::flat_hash_map<std::tuple<std::vector<std::string>, absl::string_view>,
                        HistogramValue>
        histograms_;
  };
  Filter filter(include_labels);
  ApplyLabelChecks(label_names, std::move(fn), filter);
  filter.Publish(sink);
}

template <typename Fn, typename Sink>
void MetricsQuery::ApplyLabelChecks(absl::Span<const std::string> label_names,
                                    Fn fn, Sink& sink) const {
  if (label_eqs_.empty()) {
    fn(sink);
    return;
  }
  struct LabelEq {
    size_t offset;
    absl::string_view value;
  };
  std::vector<LabelEq> label_eqs;
  for (size_t i = 0; i < label_names.size(); ++i) {
    const auto& label = label_names[i];
    auto it = label_eqs_.find(label);
    if (it != label_eqs_.end()) label_eqs.push_back({i, it->second});
  }
  // If there are labels to match, but this domain doesn't have all the labels
  // requested, skip it - it can never match all!
  if (label_eqs.size() < label_eqs_.size()) return;
  class Filter final : public MetricsSink {
   public:
    explicit Filter(Sink& sink, absl::Span<const LabelEq> inclusion_checks)
        : inclusion_checks_(inclusion_checks), sink_(sink) {}

    void Counter(absl::Span<const std::string> label, absl::string_view name,
                 uint64_t value) override {
      if (!Matches(label)) return;
      sink_.Counter(label, name, value);
    }

    void Histogram(absl::Span<const std::string> label, absl::string_view name,
                   HistogramBuckets bounds,
                   absl::Span<const uint64_t> counts) override {
      if (!Matches(label)) return;
      sink_.Histogram(label, name, bounds, counts);
    }

   private:
    bool Matches(absl::Span<const std::string> label) const {
      for (const auto& check : inclusion_checks_) {
        if (label[check.offset] != check.value) return false;
      }
      return true;
    }

    absl::Span<const LabelEq> inclusion_checks_;
    Sink& sink_;
  };
  Filter filter(sink, label_eqs);
  fn(filter);
}

void QueryableDomain::Constructed() {
  CHECK_EQ(prev_, nullptr);
  prev_ = last_;
  last_ = this;
}

void QueryableDomain::ExportMetrics(const MetricsQuery& query,
                                    MetricsSink& sink) {
  auto selected_metrics = query.selected_metrics();
  if (selected_metrics.has_value()) {
    absl::flat_hash_map<QueryableDomain*,
                        std::vector<const InstrumentIndex::Description*>>
        what;
    for (const auto& metric : *selected_metrics) {
      const auto* desc = InstrumentIndex::Get().Find(metric);
      if (desc == nullptr) continue;
      what[desc->domain].push_back(desc);
    }
    for (auto it = what.begin(); it != what.end(); ++it) {
      // TODO(ctiller): switch to structured bindings when we have C++20.
      // Avoids "error: captured structured bindings are a C++20 extension
      // [-Werror,-Wc++20-extensions]"
      auto* domain = it->first;
      auto& metrics = it->second;
      query.Apply(
          domain->label_names(),
          [&](MetricsSink& sink) { domain->ExportMetrics(sink, metrics); },
          sink);
    }
  } else {
    for (auto* domain = last_; domain != nullptr; domain = domain->prev_) {
      query.Apply(
          domain->label_names(),
          [&](MetricsSink& sink) {
            domain->ExportMetrics(sink, domain->all_metrics());
          },
          sink);
    }
  }
}

uint64_t QueryableDomain::AllocateCounter(absl::string_view name,
                                          absl::string_view description,
                                          absl::string_view unit) {
  const size_t offset = Allocate(1);
  metrics_.push_back(InstrumentIndex::Get().Register(
      this, offset, name, description, unit, InstrumentIndex::Counter{}));
  return offset;
}

uint64_t QueryableDomain::AllocateHistogram(absl::string_view name,
                                            absl::string_view description,
                                            absl::string_view unit,
                                            HistogramBuckets bounds) {
  const size_t offset = Allocate(bounds.size());
  metrics_.push_back(InstrumentIndex::Get().Register(
      this, offset, name, description, unit, bounds));
  return offset;
}

void QueryableDomain::TestOnlyResetAll() {
  for (auto* domain = last_; domain != nullptr; domain = domain->prev_) {
    domain->TestOnlyReset();
  }
}

void TestOnlyResetInstruments() { QueryableDomain::TestOnlyResetAll(); }

}  // namespace grpc_core
