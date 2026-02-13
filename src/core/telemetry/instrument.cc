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

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/channelz/channelz.h"
#include "src/core/channelz/property_list.h"
#include "src/core/telemetry/histogram.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/match.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/single_set_ptr.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {

namespace {
struct Hook {
  HistogramCollectionHook hook;
  Hook* next;
};

std::atomic<Hook*> hooks = nullptr;
}  // namespace

void RegisterHistogramCollectionHook(HistogramCollectionHook hook) {
  Hook* new_hook =
      new Hook{std::move(hook), hooks.load(std::memory_order_acquire)};
  while (!hooks.compare_exchange_weak(new_hook->next, new_hook,
                                      std::memory_order_acq_rel)) {
  }
}

namespace instrument_detail {

void CallHistogramCollectionHooks(
    const InstrumentMetadata::Description* instrument,
    absl::Span<const std::string> labels, int64_t value) {
  Hook* hook = hooks.load(std::memory_order_acquire);
  while (GPR_UNLIKELY(hook != nullptr)) {
    hook->hook(instrument, labels, value);
    hook = hook->next;
  }
}

}  // namespace instrument_detail

namespace {
std::vector<std::string> FilterLabels(
    absl::Span<const std::string> domain_label_names,
    const absl::flat_hash_set<std::string>& scope_labels_of_interest,
    absl::Span<const std::string> full_label_values) {
  std::vector<std::string> result;
  result.reserve(domain_label_names.size());
  for (size_t i = 0; i < domain_label_names.size(); ++i) {
    if (scope_labels_of_interest.contains(domain_label_names[i])) {
      result.push_back(full_label_values[i]);
    } else {
      result.push_back(std::string(kOmittedLabel));
    }
  }
  return result;
}
}  // namespace

CollectionScope::CollectionScope(
    std::vector<RefCountedPtr<CollectionScope>> parents,
    absl::Span<const std::string> labels, size_t child_shards_count,
    size_t storage_shards_count)
    : parents_(std::move(parents)),
      labels_of_interest_(labels.begin(), labels.end()),
      child_shards_(child_shards_count),
      storage_shards_(storage_shards_count) {
  // Sort parents (by address) and then remove any duplicates.
  std::sort(parents_.begin(), parents_.end());
  parents_.erase(std::unique(parents_.begin(), parents_.end()), parents_.end());
  for (const auto& parent : parents_) {
    if (parent != nullptr) {
      labels_of_interest_.insert(parent->labels_of_interest_.begin(),
                                 parent->labels_of_interest_.end());
      auto& shard = parent->child_shard(this);
      MutexLock lock(&shard.mu);
      shard.children.insert(this);
    }
  }
}

CollectionScope::~CollectionScope() {
  for (const auto& parent : parents_) {
    if (parent != nullptr) {
      auto& shard = parent->child_shard(this);
      MutexLock lock(&shard.mu);
      shard.children.erase(this);
    }
  }
  for (auto& shard : storage_shards_) {
    // TODO(ctiller): Consider a different entry point than GetDomainStorage
    // for this post-aggregation. We ought to be able to do this step without
    // accessing full_labels.
    MutexLock lock(&shard.mu);
    for (auto& storage_pair : shard.storage) {
      for (const auto& parent : parents_) {
        if (parent != nullptr) {
          storage_pair.second->domain()
              ->GetDomainStorage(parent, storage_pair.second->label())
              ->Add(storage_pair.second.get());
        }
      }
    }
  }
}

size_t CollectionScope::TestOnlyCountStorageHeld() const {
  size_t count = 0;
  for (const auto& shard : storage_shards_) {
    MutexLock lock(&shard.mu);
    count += shard.storage.size();
  }
  return count;
}

void CollectionScope::ForEachUniqueStorage(
    absl::FunctionRef<void(instrument_detail::DomainStorage*)> cb) {
  absl::flat_hash_set<instrument_detail::DomainStorage*> visited;
  ForEachUniqueStorage(cb, visited);
}

void CollectionScope::ForEachUniqueStorage(
    absl::FunctionRef<void(instrument_detail::DomainStorage*)> cb,
    absl::flat_hash_set<instrument_detail::DomainStorage*>& visited) {
  for (auto& shard : storage_shards_) {
    MutexLock lock(&shard.mu);
    for (const auto& s : shard.storage) {
      if (visited.insert(s.second.get()).second) {
        cb(s.second.get());
      }
    }
  }
  for (auto& shard : child_shards_) {
    MutexLock lock(&shard.mu);
    for (auto* child : shard.children) {
      child->ForEachUniqueStorage(cb, visited);
    }
  }
}

void CollectionScope::TestOnlyReset() {
  for (auto& shard : storage_shards_) {
    MutexLock lock(&shard.mu);
    shard.storage.clear();
  }
  for (auto& shard : child_shards_) {
    MutexLock lock(&shard.mu);
    shard.children.clear();
  }
}

RefCountedPtr<CollectionScope> CreateCollectionScope(
    std::vector<RefCountedPtr<CollectionScope>> parents,
    absl::Span<const std::string> labels, size_t child_shards_count,
    size_t storage_shards_count) {
  return MakeRefCounted<CollectionScope>(
      std::move(parents), labels, child_shards_count, storage_shards_count);
}

////////////////////////////////////////////////////////////////////////////////
// InstrumentMetadata

void InstrumentMetadata::ForEachInstrument(
    absl::FunctionRef<void(const InstrumentMetadata::Description*)> fn) {
  instrument_detail::QueryableDomain::ForEachInstrument(fn);
}

/////////////////////////////////////////////////////////////////////////////////
// GaugeStorage

namespace instrument_detail {

GaugeStorage::GaugeStorage(QueryableDomain* domain)
    : double_gauges_(domain->allocated_double_gauge_slots()),
      int_gauges_(domain->allocated_int_gauge_slots()),
      uint_gauges_(domain->allocated_uint_gauge_slots()) {}

}  // namespace instrument_detail

////////////////////////////////////////////////////////////////////////////////
// MetricsQuery

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

MetricsQuery& MetricsQuery::OnlyMetrics(std::vector<std::string> metrics) {
  only_metrics_.emplace(std::move(metrics));
  return *this;
}

void MetricsQuery::Run(RefCountedPtr<CollectionScope> scope,
                       MetricsSink& sink) const {
  GRPC_CHECK_NE(scope.get(), nullptr);
  struct DomainInfo {
    std::vector<const InstrumentMetadata::Description*> metrics;
    std::vector<RefCountedPtr<instrument_detail::DomainStorage>> storage;
  };
  absl::flat_hash_map<instrument_detail::QueryableDomain*, DomainInfo>
      domain_info_map;
  auto selected_metrics = this->selected_metrics();
  // Calculate list of desired metrics, per domain.
  if (selected_metrics.has_value()) {
    for (const auto& metric : *selected_metrics) {
      const auto* desc = instrument_detail::InstrumentIndex::Get().Find(metric);
      GRPC_CHECK_NE(desc, nullptr) << "Metric not found: " << metric;
      domain_info_map[desc->domain].metrics.push_back(desc);
    }
  } else {
    instrument_detail::QueryableDomain::ForEachInstrument(
        [&](const InstrumentMetadata::Description* desc) {
          domain_info_map[desc->domain].metrics.push_back(desc);
        });
  }
  // Calculate list of storage objects, per domain, that have at least one
  // desired metric.
  scope->ForEachUniqueStorage([&](instrument_detail::DomainStorage* storage) {
    auto it = domain_info_map.find(storage->domain());
    if (it == domain_info_map.end()) return;
    it->second.storage.push_back(storage->Ref());
  });
  for (const auto& pair : domain_info_map) {
    instrument_detail::QueryableDomain* domain = pair.first;
    const auto& metrics = pair.second.metrics;
    const auto& storages = pair.second.storage;
    GRPC_CHECK(!metrics.empty());
    if (storages.empty()) continue;
    this->Apply(
        domain->label_names(),
        [&](MetricsSink& sink) {
          for (auto& storage : storages) {
            const auto label_values = storage->label();
            const auto label_keys = domain->label_names();
            instrument_detail::GaugeStorage gauge_storage(storage->domain());
            storage->FillGaugeStorage(gauge_storage);
            for (const auto* metric : metrics) {
              Match(
                  metric->shape,
                  [metric, &sink, storage, &label_values,
                   &label_keys](InstrumentMetadata::CounterShape) {
                    sink.Counter(label_keys, label_values, metric->name,
                                 storage->SumCounter(metric->offset));
                  },
                  [metric, &sink, storage, &label_values,
                   &label_keys](InstrumentMetadata::UpDownCounterShape) {
                    sink.UpDownCounter(label_keys, label_values, metric->name,
                                       storage->SumCounter(metric->offset));
                  },
                  [metric, &sink, storage, &label_values,
                   &label_keys](InstrumentMetadata::HistogramShape bounds) {
                    std::vector<uint64_t> counts(bounds.size());
                    for (size_t i = 0; i < bounds.size(); ++i) {
                      counts[i] = storage->SumCounter(metric->offset + i);
                    }
                    sink.Histogram(label_keys, label_values, metric->name,
                                   bounds, counts);
                  },
                  [metric, &sink, &gauge_storage, &label_values,
                   &label_keys](InstrumentMetadata::DoubleGaugeShape) {
                    if (auto value = gauge_storage.GetDouble(metric->offset);
                        value.has_value()) {
                      sink.DoubleGauge(label_keys, label_values, metric->name,
                                       *value);
                    }
                  },
                  [metric, &sink, &gauge_storage, &label_values,
                   &label_keys](InstrumentMetadata::IntGaugeShape) {
                    if (auto value = gauge_storage.GetInt(metric->offset);
                        value.has_value()) {
                      sink.IntGauge(label_keys, label_values, metric->name,
                                    *value);
                    }
                  },
                  [metric, &sink, &gauge_storage, &label_values,
                   &label_keys](InstrumentMetadata::UintGaugeShape) {
                    if (auto value = gauge_storage.GetUint(metric->offset);
                        value.has_value()) {
                      sink.UintGauge(label_keys, label_values, metric->name,
                                     *value);
                    }
                  });
            }
          }
        },
        sink);
  }
}

void MetricsQuery::Apply(absl::Span<const std::string> label_names,
                         absl::FunctionRef<void(MetricsSink&)> fn,
                         MetricsSink& sink) const {
  if (collapsed_labels_.empty()) {
    ApplyLabelChecks(label_names, fn, sink);
    return;
  }
  std::vector<size_t> include_labels;
  std::vector<std::string> label_keys;
  for (size_t i = 0; i < label_names.size(); ++i) {
    if (!collapsed_labels_.contains(label_names[i])) {
      include_labels.push_back(i);
      label_keys.push_back(label_names[i]);
    }
  }
  if (include_labels.size() == label_names.size()) {
    ApplyLabelChecks(label_names, fn, sink);
    return;
  }
  class Filter final : public MetricsSink {
   public:
    explicit Filter(absl::Span<const size_t> include_labels,
                    absl::Span<const std::string> label_keys)
        : include_labels_(include_labels), label_keys_(label_keys) {}

    void Counter(absl::Span<const std::string> /* label_keys */,
                 absl::Span<const std::string> label_values,
                 absl::string_view name, uint64_t value) override {
      uint64_counters_[ConstructKey(label_values, name)] += value;
    }

    void UpDownCounter(absl::Span<const std::string> /* label_keys */,
                       absl::Span<const std::string> label_values,
                       absl::string_view name, uint64_t value) override {
      uint64_up_down_counters_[ConstructKey(label_values, name)] += value;
    }

    void Histogram(absl::Span<const std::string> /* label_keys */,
                   absl::Span<const std::string> label_values,
                   absl::string_view name, HistogramBuckets bounds,
                   absl::Span<const uint64_t> counts) override {
      GRPC_CHECK_EQ(counts.size(), bounds.size());
      auto it = histograms_.find(ConstructKey(label_values, name));
      if (it == histograms_.end()) {
        histograms_.emplace(std::piecewise_construct,
                            std::tuple(ConstructKey(label_values, name)),
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

    void DoubleGauge(absl::Span<const std::string> /* label_keys */,
                     absl::Span<const std::string>, absl::string_view,
                     double) override {
      // Not aggregatable
    }
    void IntGauge(absl::Span<const std::string> /* label_keys */,
                  absl::Span<const std::string>, absl::string_view,
                  int64_t) override {
      // Not aggregatable
    }
    void UintGauge(absl::Span<const std::string> /* label_keys */,
                   absl::Span<const std::string>, absl::string_view,
                   uint64_t) override {
      // Not aggregatable
    }

    void Publish(MetricsSink& sink) const {
      for (const auto& [key, value] : uint64_counters_) {
        sink.Counter(label_keys_, std::get<0>(key), std::get<1>(key), value);
      }
      for (const auto& [key, value] : uint64_up_down_counters_) {
        sink.UpDownCounter(label_keys_, std::get<0>(key), std::get<1>(key),
                           value);
      }
      for (const auto& [key, value] : histograms_) {
        sink.Histogram(label_keys_, std::get<0>(key), std::get<1>(key),
                       value.bounds, value.counts);
      }
    }

   private:
    std::tuple<std::vector<std::string>, absl::string_view> ConstructKey(
        absl::Span<const std::string> label_values,
        absl::string_view name) const {
      std::vector<std::string> key;
      key.reserve(include_labels_.size());
      for (auto i : include_labels_) {
        key.push_back(label_values[i]);
      }
      return std::tuple(std::move(key), name);
    }

    absl::Span<const size_t> include_labels_;
    absl::Span<const std::string> label_keys_;
    absl::flat_hash_map<std::tuple<std::vector<std::string>, absl::string_view>,
                        uint64_t>
        uint64_counters_;
    absl::flat_hash_map<std::tuple<std::vector<std::string>, absl::string_view>,
                        uint64_t>
        uint64_up_down_counters_;
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
  Filter filter(include_labels, label_keys);
  ApplyLabelChecks(label_names, fn, filter);
  filter.Publish(sink);
}

void MetricsQuery::ApplyLabelChecks(absl::Span<const std::string> label_names,
                                    absl::FunctionRef<void(MetricsSink&)> fn,
                                    MetricsSink& sink) const {
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
    explicit Filter(MetricsSink& sink,
                    absl::Span<const LabelEq> inclusion_checks)
        : inclusion_checks_(inclusion_checks), sink_(sink) {}

    void Counter(absl::Span<const std::string> label_keys,
                 absl::Span<const std::string> label_values,
                 absl::string_view name, uint64_t value) override {
      if (!Matches(label_values)) return;
      sink_.Counter(label_keys, label_values, name, value);
    }

    void UpDownCounter(absl::Span<const std::string> label_keys,
                       absl::Span<const std::string> label_values,
                       absl::string_view name, uint64_t value) override {
      if (!Matches(label_values)) return;
      sink_.UpDownCounter(label_keys, label_values, name, value);
    }

    void Histogram(absl::Span<const std::string> label_keys,
                   absl::Span<const std::string> label_values,
                   absl::string_view name, HistogramBuckets bounds,
                   absl::Span<const uint64_t> counts) override {
      if (!Matches(label_values)) return;
      sink_.Histogram(label_keys, label_values, name, bounds, counts);
    }

    void DoubleGauge(absl::Span<const std::string> label_keys,
                     absl::Span<const std::string> label_values,
                     absl::string_view name, double value) override {
      if (!Matches(label_values)) return;
      sink_.DoubleGauge(label_keys, label_values, name, value);
    }
    void IntGauge(absl::Span<const std::string> label_keys,
                  absl::Span<const std::string> label_values,
                  absl::string_view name, int64_t value) override {
      if (!Matches(label_values)) return;
      sink_.IntGauge(label_keys, label_values, name, value);
    }
    void UintGauge(absl::Span<const std::string> label_keys,
                   absl::Span<const std::string> label_values,
                   absl::string_view name, uint64_t value) override {
      if (!Matches(label_values)) return;
      sink_.UintGauge(label_keys, label_values, name, value);
    }

   private:
    bool Matches(absl::Span<const std::string> label_values) const {
      for (const auto& check : inclusion_checks_) {
        if (label_values[check.offset] != check.value) return false;
      }
      return true;
    }

    absl::Span<const LabelEq> inclusion_checks_;
    MetricsSink& sink_;
  };
  Filter filter(sink, label_eqs);
  fn(filter);
}

namespace instrument_detail {

////////////////////////////////////////////////////////////////////////////////
// InstrumentIndex

const InstrumentMetadata::Description* InstrumentIndex::Register(
    QueryableDomain* domain, uint64_t offset, absl::string_view name,
    absl::string_view description, absl::string_view unit,
    InstrumentMetadata::Shape shape) {
  auto it = metrics_.emplace(
      name, InstrumentMetadata::Description{domain, offset, name, description,
                                            unit, shape});
  if (!it.second) {
    // If this is firing one of two things is true:
    // 1. We have code in gRPC that's registering two different metrics with the
    //    same name. gRPC should fix this.
    // 2. gRPC static initialization is executing twice. This is an unsupported
    //    use of the gRPC library and the application owner should fix it.
    LOG(ERROR) << "Metric with name '" << name
               << "' registered more than once. Ignoring later registration.";
  }
  return &it.first->second;
}

const InstrumentMetadata::Description* InstrumentIndex::Find(
    absl::string_view name) const {
  auto it = metrics_.find(name);
  if (it == metrics_.end()) {
    return nullptr;
  }
  return &it->second;
}

////////////////////////////////////////////////////////////////////////////////
// DomainStorage

DomainStorage::DomainStorage(QueryableDomain* domain,
                             std::vector<std::string> label)
    : DataSource(MakeRefCounted<channelz::MetricsDomainStorageNode>(
          absl::StrCat(domain->name(), ":", absl::StrJoin(label, ",")))),
      domain_(domain),
      label_(std::move(label)) {
  channelz_node()->AddParent(domain->channelz_node().get());
  SourceConstructed();
}

void DomainStorage::Orphaned() { SourceDestructing(); }

void DomainStorage::AddData(channelz::DataSink sink) {
  sink.AddData(
      "domain_storage",
      channelz::PropertyList()
          .Set("label",
               [this]() {
                 channelz::PropertyGrid grid;
                 for (size_t i = 0; i < label_.size(); ++i) {
                   grid.SetRow(
                       domain_->label_names()[i],
                       channelz::PropertyList().Set("value", label_[i]));
                 }
                 return grid;
               }())
          .Set("metrics", [this]() {
            GaugeStorage storage(domain_);
            FillGaugeStorage(storage);
            channelz::PropertyGrid grid;
            for (const auto* metric : domain_->metrics_) {
              Match(
                  metric->shape,
                  [&, this](InstrumentMetadata::CounterShape) {
                    grid.SetRow(metric->name,
                                channelz::PropertyList().Set(
                                    "value", SumCounter(metric->offset)));
                  },
                  [&, this](InstrumentMetadata::UpDownCounterShape) {
                    grid.SetRow(metric->name,
                                channelz::PropertyList().Set(
                                    "value", SumCounter(metric->offset)));
                  },
                  [&](InstrumentMetadata::DoubleGaugeShape) {
                    grid.SetRow(
                        metric->name,
                        channelz::PropertyList().Set(
                            "value", storage.GetDouble(metric->offset)));
                  },
                  [&](InstrumentMetadata::IntGaugeShape) {
                    grid.SetRow(metric->name,
                                channelz::PropertyList().Set(
                                    "value", storage.GetInt(metric->offset)));
                  },
                  [&](InstrumentMetadata::UintGaugeShape) {
                    grid.SetRow(metric->name,
                                channelz::PropertyList().Set(
                                    "value", storage.GetUint(metric->offset)));
                  },
                  [&](const InstrumentMetadata::HistogramShape& h) {
                    channelz::PropertyTable table;
                    for (size_t i = 0; i < h.size(); ++i) {
                      table.AppendRow(
                          channelz::PropertyList()
                              .Set("bucket_max", h[i])
                              .Set("count", SumCounter(metric->offset + i)));
                    }
                    grid.SetRow(metric->name, channelz::PropertyList().Set(
                                                  "value", std::move(table)));
                  });
            }
            return grid;
          }()));
}

////////////////////////////////////////////////////////////////////////////////
// QueryableDomain

void QueryableDomain::AddData(channelz::DataSink sink) {
  sink.AddData(
      "domain",
      channelz::PropertyList()
          .Set("allocated_counter_slots", allocated_counter_slots_)
          .Set("allocated_double_gauge_slots", allocated_double_gauge_slots_)
          .Set("allocated_int_gauge_slots", allocated_int_gauge_slots_)
          .Set("allocated_uint_gauge_slots", allocated_uint_gauge_slots_)
          .Set("map_shards", map_shards_size_)
          .Set("metrics",
               [this]() {
                 channelz::PropertyGrid grid;
                 for (auto* metric : metrics_) {
                   grid.SetRow(
                       metric->name,
                       channelz::PropertyList()
                           .Set("description", metric->description)
                           .Set("unit", metric->unit)
                           .Set("offset", metric->offset)
                           .Set("shape",
                                Match(
                                    metric->shape,
                                    [](InstrumentMetadata::CounterShape)
                                        -> std::string { return "counter"; },
                                    [](InstrumentMetadata::UpDownCounterShape)
                                        -> std::string {
                                      return "up_down_counter";
                                    },
                                    [](InstrumentMetadata::DoubleGaugeShape)
                                        -> std::string {
                                      return "double_gauge";
                                    },
                                    [](InstrumentMetadata::IntGaugeShape)
                                        -> std::string { return "int_gauge"; },
                                    [](InstrumentMetadata::UintGaugeShape)
                                        -> std::string { return "uint_gauge"; },
                                    [](const InstrumentMetadata::HistogramShape&
                                           h) -> std::string {
                                      return absl::StrCat(
                                          "histogram:", absl::StrJoin(h, ","));
                                    })));
                 }
                 return grid;
               }())
          .Set("labels", absl::StrJoin(label_names_, ",")));
}

void QueryableDomain::Constructed() {
  GRPC_CHECK_EQ(prev_, nullptr);
  prev_ = last_;
  last_ = this;
}

void QueryableDomain::ForEachInstrument(
    absl::FunctionRef<void(const InstrumentMetadata::Description*)> fn) {
  for (auto* domain = last_; domain != nullptr; domain = domain->prev_) {
    for (const auto* metric : domain->metrics_) {
      fn(metric);
    }
  }
}

size_t QueryableDomain::TestOnlyCountStorageHeld() const {
  size_t count = 0;
  for (size_t i = 0; i < map_shards_size_; ++i) {
    MutexLock lock(&map_shards_[i].mu);
    map_shards_[i].storage_map.ForEach(
        [&count](const auto&, const auto&) { count++; });
  }
  return count;
}

void QueryableDomain::DomainStorageOrphaned(DomainStorage* /*storage*/) {}

QueryableDomain::MapShard& QueryableDomain::GetMapShard(
    absl::Span<const std::string> label) {
  size_t shard;
  if (map_shards_size_ == 1) {
    shard = 0;
  } else {
    GRPC_CHECK(!label.empty());
    // Use the first label to shard, all labels to index.
    shard = absl::HashOf(label[0], this) % map_shards_size_;
  }
  return map_shards_[shard];
}

void QueryableDomain::TestOnlyReset() {
  channelz_.Reset();
  map_shards_ = std::make_unique<MapShard[]>(map_shards_size_);
}

const InstrumentMetadata::Description* QueryableDomain::AllocateCounter(
    absl::string_view name, absl::string_view description,
    absl::string_view unit) {
  const size_t offset = allocated_counter_slots_++;
  auto* desc =
      InstrumentIndex::Get().Register(this, offset, name, description, unit,
                                      InstrumentMetadata::CounterShape{});
  metrics_.push_back(desc);
  return desc;
}

const InstrumentMetadata::Description* QueryableDomain::AllocateUpDownCounter(
    absl::string_view name, absl::string_view description,
    absl::string_view unit) {
  const size_t offset = allocated_counter_slots_++;
  auto* desc =
      InstrumentIndex::Get().Register(this, offset, name, description, unit,
                                      InstrumentMetadata::UpDownCounterShape{});
  metrics_.push_back(desc);
  return desc;
}

const InstrumentMetadata::Description* QueryableDomain::AllocateHistogram(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, HistogramBuckets bounds) {
  const size_t offset = AllocateCounterSlots(bounds.size());
  auto* desc = InstrumentIndex::Get().Register(this, offset, name, description,
                                               unit, bounds);
  metrics_.push_back(desc);
  return desc;
}

const InstrumentMetadata::Description* QueryableDomain::AllocateDoubleGauge(
    absl::string_view name, absl::string_view description,
    absl::string_view unit) {
  const size_t offset = allocated_double_gauge_slots_++;
  auto* desc =
      InstrumentIndex::Get().Register(this, offset, name, description, unit,
                                      InstrumentMetadata::DoubleGaugeShape{});
  metrics_.push_back(desc);
  return desc;
}

const InstrumentMetadata::Description* QueryableDomain::AllocateIntGauge(
    absl::string_view name, absl::string_view description,
    absl::string_view unit) {
  const size_t offset = allocated_int_gauge_slots_++;
  auto* desc =
      InstrumentIndex::Get().Register(this, offset, name, description, unit,
                                      InstrumentMetadata::IntGaugeShape{});
  metrics_.push_back(desc);
  return desc;
}

const InstrumentMetadata::Description* QueryableDomain::AllocateUintGauge(
    absl::string_view name, absl::string_view description,
    absl::string_view unit) {
  const size_t offset = allocated_uint_gauge_slots_++;
  auto* desc =
      InstrumentIndex::Get().Register(this, offset, name, description, unit,
                                      InstrumentMetadata::UintGaugeShape{});
  metrics_.push_back(desc);
  return desc;
}

void QueryableDomain::TestOnlyResetAll() {
  for (auto* domain = last_; domain != nullptr; domain = domain->prev_) {
    domain->TestOnlyReset();
  }
}

RefCountedPtr<DomainStorage> QueryableDomain::GetDomainStorage(
    RefCountedPtr<CollectionScope> scope,
    absl::Span<const std::string> label_values) {
  // auto key_labels =
  //     FilterLabels(label_names_, scope->labels_of_interest_, label_values);
  // temporary unfilterd:
  auto key_labels =
      std::vector<std::string>(label_values.begin(), label_values.end());
  if (scope->parents_.size() == 1 && scope->parents_[0] != nullptr) {
    auto parent_key_labels = FilterLabels(
        label_names_, scope->parents_[0]->labels_of_interest_, label_values);
    if (key_labels == parent_key_labels) {
      return GetDomainStorage(scope->parents_[0], label_values);
    }
  }
  size_t shard_idx = absl::HashOf(key_labels) % scope->storage_shards_.size();
  auto& shard = scope->storage_shards_[shard_idx];
  MutexLock lock(&shard.mu);
  auto it = shard.storage.find({this, key_labels});
  if (it != shard.storage.end()) {
    return it->second;
  }
  auto storage = CreateDomainStorage(key_labels);
  shard.storage.emplace(std::pair(this, key_labels), storage);
  return storage;
}

}  // namespace instrument_detail

LowContentionBackend::LowContentionBackend(size_t size)
    : counters_(new std::atomic<uint64_t>[size]) {
  for (size_t i = 0; i < size; ++i) {
    counters_[i].store(0, std::memory_order_relaxed);
  }
}

uint64_t LowContentionBackend::Sum(size_t index) {
  return counters_[index].load(std::memory_order_relaxed);
}

HighContentionBackend::HighContentionBackend(size_t size) {
  for (auto& shard : counters_) {
    shard = std::make_unique<std::atomic<int64_t>[]>(size);
    for (size_t i = 0; i < size; ++i) {
      shard[i].store(0, std::memory_order_relaxed);
    }
  }
}

uint64_t HighContentionBackend::Sum(size_t index) {
  uint64_t positive_sum = 0;
  uint64_t negative_sum = 0;
  for (auto& shard : counters_) {
    int64_t value = shard[index].load(std::memory_order_relaxed);
    if (value > 0) {
      positive_sum += value;
    } else if (value < 0) {
      negative_sum += -value;
    }
  }
  // Every decrement should have a corresponding increment.
  GRPC_CHECK(positive_sum >= negative_sum);
  return positive_sum - negative_sum;
}

class GlobalCollectionScopeManager {
 public:
  GlobalCollectionScopeManager(const GlobalCollectionScopeManager&) = delete;
  GlobalCollectionScopeManager& operator=(const GlobalCollectionScopeManager&) =
      delete;

  static GlobalCollectionScopeManager& Get() {
    static GlobalCollectionScopeManager* manager =
        new GlobalCollectionScopeManager();
    return *manager;
  }

  RefCountedPtr<CollectionScope> CreateRootScope(
      absl::Span<const std::string> labels, size_t child_shards_count,
      size_t storage_shards_count) {
    MutexLock lock(&mu_);
    if (auto* building = std::get_if<Building>(&state_); building != nullptr) {
      auto scope = CreateCollectionScope({}, labels, child_shards_count,
                                         storage_shards_count);
      building->root_scopes.push_back(scope);
      return scope;
    } else {
      // Global scope is already created, we can no longer subset labels.
      auto& published = std::get<Published>(state_);
      std::vector<std::string> missing_labels;
      for (const auto& label : labels) {
        if (!published.global_scope->ObservesLabel(label)) {
          missing_labels.push_back(label);
        }
      }
      if (missing_labels.empty()) {
        LOG(ERROR) << "Attempt to create a root scope with labels ["
                   << absl::StrJoin(labels, ", ")
                   << "] after the global scope was already created.  "
                      "All requested labels are collected by the global scope, "
                      "so this scope will be returned instead.  "
                      "To eliminate this message, ensure the root scope "
                      "creation that triggered it occurs before the first call "
                      "to GlobalCollectionScope().";
      } else {
        LOG(ERROR) << "Attempt to create a root scope with labels ["
                   << absl::StrJoin(labels, ", ")
                   << "] after the global scope was already created.  "
                      "The following labels are not collected by the global "
                      "scope, and so will not be available: ["
                   << absl::StrJoin(missing_labels, ", ")
                   << "]."
                      "To eliminate this message, ensure the root scope "
                      "creation that triggered it occurs before the first call "
                      "to GlobalCollectionScope().";
      }
      return published.global_scope;
    }
  }

  RefCountedPtr<CollectionScope> GetGlobalScope() {
    MutexLock lock(&mu_);
    if (auto* building = std::get_if<Building>(&state_); building != nullptr) {
      auto global_scope =
          CreateCollectionScope(building->root_scopes, {}, 32, 32);
      state_ = Published{global_scope};
      return global_scope;
    } else {
      return std::get<Published>(state_).global_scope;
    }
  }

  void TestOnlyReset() {
    std::variant<Building, Published> state;
    MutexLock lock(&mu_);
    state = std::exchange(state_, Building{});
    if (auto* published = std::get_if<Published>(&state);
        published != nullptr) {
      published->global_scope->TestOnlyReset();
    }
  }

 private:
  GlobalCollectionScopeManager() = default;

  struct Building {
    std::vector<RefCountedPtr<CollectionScope>> root_scopes;
  };

  struct Published {
    RefCountedPtr<CollectionScope> global_scope;
  };

  Mutex mu_;
  std::variant<Building, Published> state_ ABSL_GUARDED_BY(mu_);
};

RefCountedPtr<CollectionScope> CreateRootCollectionScope(
    absl::Span<const std::string> labels, size_t child_shards_count,
    size_t storage_shards_count) {
  return GlobalCollectionScopeManager::Get().CreateRootScope(
      labels, child_shards_count, storage_shards_count);
}

RefCountedPtr<CollectionScope> GlobalCollectionScope() {
  return GlobalCollectionScopeManager::Get().GetGlobalScope();
}

void TestOnlyResetInstruments() {
  Hook* hook = hooks.load(std::memory_order_acquire);
  while (hook != nullptr) {
    Hook* next = hook->next;
    delete hook;
    hook = next;
  }
  hooks.store(nullptr, std::memory_order_release);
  instrument_detail::QueryableDomain::TestOnlyResetAll();
  GlobalCollectionScopeManager::Get().TestOnlyReset();
}

}  // namespace grpc_core
