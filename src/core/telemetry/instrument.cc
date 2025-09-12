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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
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
#include "src/core/channelz/property_list.h"

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

CollectionScope::CollectionScope(
    std::vector<std::unique_ptr<instrument_detail::StorageSet>> storage_sets)
    : storage_sets_(std::move(storage_sets)) {}

size_t CollectionScope::TestOnlyCountStorageHeld() const {
  size_t count = 0;
  for (const auto& set : storage_sets_) {
    count += set->TestOnlyCountStorageHeld();
  }
  return count;
}

std::vector<instrument_detail::StorageSet*> CollectionScope::GetStorageSets() {
  std::vector<instrument_detail::StorageSet*> result;
  result.reserve(storage_sets_.size());
  for (const auto& storage_set : storage_sets_) {
    result.push_back(storage_set.get());
  }
  return result;
}

std::unique_ptr<CollectionScope>
instrument_detail::QueryableDomain::CreateCollectionScope() {
  std::vector<std::unique_ptr<instrument_detail::StorageSet>> storage_sets;
  for (auto* domain = last_; domain != nullptr; domain = domain->prev_) {
    storage_sets.push_back(domain->CreateStorageSet());
  }
  return std::make_unique<CollectionScope>(std::move(storage_sets));
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
// StorageSet

namespace instrument_detail {

StorageSet::StorageSet(QueryableDomain* domain, size_t map_shards_size)
    : domain_(domain),
      map_shards_(std::make_unique<MapShard[]>(map_shards_size)),
      map_shards_size_(map_shards_size) {
  domain_->RegisterStorageSet(this);
}

StorageSet::~StorageSet() { domain_->UnregisterStorageSet(this); }

void StorageSet::ExportMetrics(
    MetricsSink& sink,
    absl::Span<const InstrumentMetadata::Description* const> metrics) {
  for (size_t i = 0; i < map_shards_size_; ++i) {
    MutexLock lock(&map_shards_[i].mu);
    map_shards_[i].storage_map.ForEach([&](const auto& label,
                                           const auto& weak_storage) {
      // It's safe to use `get()` here because the `StorageSet` itself
      // holds a weak reference, guaranteeing the object's memory is
      // alive. We can't get a strong ref if the object is orphaned, but
      // we still need to read its final metric values.
      DomainStorage* storage = weak_storage.get();
      GaugeStorage gauge_storage(storage->domain());
      storage->FillGaugeStorage(gauge_storage);
      for (const auto* metric : metrics) {
        Match(
            metric->shape,
            [metric, &sink, storage, &label](InstrumentMetadata::CounterShape) {
              sink.Counter(label, metric->name,
                           storage->SumCounter(metric->offset));
            },
            [metric, &sink, storage,
             &label](InstrumentMetadata::HistogramShape bounds) {
              std::vector<uint64_t> counts(bounds.size());
              for (size_t i = 0; i < bounds.size(); ++i) {
                counts[i] = storage->SumCounter(metric->offset + i);
              }
              sink.Histogram(label, metric->name, bounds, counts);
            },
            [metric, &sink, &gauge_storage,
             &label](InstrumentMetadata::DoubleGaugeShape) {
              if (auto value = gauge_storage.GetDouble(metric->offset);
                  value.has_value()) {
                sink.DoubleGauge(label, metric->name, *value);
              }
            },
            [metric, &sink, &gauge_storage,
             &label](InstrumentMetadata::IntGaugeShape) {
              if (auto value = gauge_storage.GetInt(metric->offset);
                  value.has_value()) {
                sink.IntGauge(label, metric->name, *value);
              }
            },
            [metric, &sink, &gauge_storage,
             &label](InstrumentMetadata::UintGaugeShape) {
              if (auto value = gauge_storage.GetUint(metric->offset);
                  value.has_value()) {
                sink.UintGauge(label, metric->name, *value);
              }
            });
      }
    });
  }
}

size_t StorageSet::TestOnlyCountStorageHeld() const {
  size_t count = 0;
  for (size_t i = 0; i < map_shards_size_; ++i) {
    MutexLock lock(&map_shards_[i].mu);
    map_shards_[i].storage_map.ForEach([&](const auto&, const auto& weak) {
      count += weak->RefIfNonZero() != nullptr;
    });
  }
  return count;
}

void StorageSet::AddStorage(WeakRefCountedPtr<DomainStorage> storage) {
  size_t shard;
  auto label = storage->label();
  if (map_shards_size_ == 1) {
    shard = 0;
  } else {
    CHECK(!label.empty());
    shard = absl::HashOf(label[0], this) % map_shards_size_;
  }
  MapShard& map_shard = map_shards_[shard];
  MutexLock lock(&map_shard.mu);
  map_shard.storage_map = map_shard.storage_map.Add(label, std::move(storage));
}

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

MetricsQuery& MetricsQuery::OnlyMetrics(absl::Span<const std::string> metrics) {
  only_metrics_.emplace(metrics.begin(), metrics.end());
  return *this;
}

void MetricsQuery::Run(std::unique_ptr<CollectionScope> scope,
                       MetricsSink& sink) const {
  CHECK_NE(scope.get(), nullptr);
  auto selected_metrics = this->selected_metrics();
  absl::flat_hash_map<instrument_detail::QueryableDomain*,
                      std::vector<const InstrumentMetadata::Description*>>
      metrics_by_domain;
  if (selected_metrics.has_value()) {
    for (const auto& metric : *selected_metrics) {
      const auto* desc = instrument_detail::InstrumentIndex::Get().Find(metric);
      if (desc == nullptr) continue;
      metrics_by_domain[desc->domain].push_back(desc);
    }
  } else {
    instrument_detail::QueryableDomain::ForEachInstrument(
        [&](const InstrumentMetadata::Description* desc) {
          metrics_by_domain[desc->domain].push_back(desc);
        });
  }
  for (auto* storage_set : scope->GetStorageSets()) {
    instrument_detail::QueryableDomain* domain = storage_set->domain();
    if (domain == nullptr) continue;
    auto it = metrics_by_domain.find(domain);
    if (it == metrics_by_domain.end()) continue;
    this->Apply(
        domain->label_names(),
        [&](MetricsSink& sink) {
          storage_set->ExportMetrics(sink, it->second);
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
  for (size_t i = 0; i < label_names.size(); ++i) {
    if (!collapsed_labels_.contains(label_names[i])) {
      include_labels.push_back(i);
    }
  }
  if (include_labels.size() == label_names.size()) {
    ApplyLabelChecks(label_names, fn, sink);
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

    void DoubleGauge(absl::Span<const std::string>, absl::string_view,
                     double) override {
      // Not aggregatable
    }
    void IntGauge(absl::Span<const std::string>, absl::string_view,
                  int64_t) override {
      // Not aggregatable
    }
    void UintGauge(absl::Span<const std::string>, absl::string_view,
                   uint64_t) override {
      // Not aggregatable
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

    void DoubleGauge(absl::Span<const std::string> label,
                     absl::string_view name, double value) override {
      if (!Matches(label)) return;
      sink_.DoubleGauge(label, name, value);
    }
    void IntGauge(absl::Span<const std::string> label, absl::string_view name,
                  int64_t value) override {
      if (!Matches(label)) return;
      sink_.IntGauge(label, name, value);
    }
    void UintGauge(absl::Span<const std::string> label, absl::string_view name,
                   uint64_t value) override {
      if (!Matches(label)) return;
      sink_.UintGauge(label, name, value);
    }

   private:
    bool Matches(absl::Span<const std::string> label) const {
      for (const auto& check : inclusion_checks_) {
        if (label[check.offset] != check.value) return false;
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

void DomainStorage::Orphaned() {
  SourceDestructing();
  domain_->DomainStorageOrphaned(this);
}

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
  CHECK_EQ(prev_, nullptr);
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

void QueryableDomain::DomainStorageOrphaned(DomainStorage* storage) {
  auto label = storage->label();
  auto& map_shard = GetMapShard(label);
  MutexLock lock(&map_shard.mu);
  auto* found_storage = map_shard.storage_map.Lookup(label);
  if (found_storage == nullptr) return;
  if (found_storage->get() != storage) return;
  map_shard.storage_map = map_shard.storage_map.Remove(label);
}

QueryableDomain::MapShard& QueryableDomain::GetMapShard(
    absl::Span<const std::string> label) {
  size_t shard;
  if (map_shards_size_ == 1) {
    shard = 0;
  } else {
    CHECK(!label.empty());
    // Use the first label to shard, all labels to index.
    shard = absl::HashOf(label[0], this) % map_shards_size_;
  }
  return map_shards_[shard];
}

std::unique_ptr<StorageSet> QueryableDomain::CreateStorageSet() {
  return std::make_unique<StorageSet>(this, map_shards_size_);
}

void QueryableDomain::RegisterStorageSet(StorageSet* storage_set) {
  // Now register the new set.
  std::vector<RefCountedPtr<DomainStorage>> added_storage;
  MutexLock lock(&active_storage_sets_mu_);
  active_storage_sets_.push_back(storage_set);
  for (size_t i = 0; i < map_shards_size_; ++i) {
    MutexLock lock(&map_shards_[i].mu);
    map_shards_[i].storage_map.ForEach(
        [&](const auto&, const auto& weak_storage) {
          // Only add storage that is not already orphaned.
          if (auto storage = weak_storage->RefIfNonZero(); storage != nullptr) {
            added_storage.emplace_back(storage);
            storage_set->AddStorage(weak_storage);
          }
        });
  }
}

void QueryableDomain::UnregisterStorageSet(StorageSet* storage_set) {
  MutexLock lock(&active_storage_sets_mu_);
  active_storage_sets_.erase(
      std::remove(active_storage_sets_.begin(), active_storage_sets_.end(),
                  storage_set),
      active_storage_sets_.end());
}

void QueryableDomain::TestOnlyReset() {
  channelz_.Reset();
  map_shards_ = std::make_unique<MapShard[]>(map_shards_size_);
  MutexLock lock(&active_storage_sets_mu_);
  active_storage_sets_.clear();
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
    std::vector<std::string> label) {
  MapShard& map_shard = GetMapShard(label);
  // First try to get an existing storage.
  map_shard.mu.Lock();
  auto storage_map = map_shard.storage_map;
  map_shard.mu.Unlock();
  // With an AVL we can search outside the lock.
  auto* weak_storage = storage_map.Lookup(label);
  if (weak_storage != nullptr) {
    // Found a weak pointer, try to upgrade it to a strong pointer.
    RefCountedPtr<DomainStorage> strong_storage =
        (*weak_storage)->RefIfNonZero();
    if (strong_storage != nullptr) {
      // Upgrade successful, return the strong pointer.
      return strong_storage;
    }
  }
  // No hit, or the storage is orphaned: now we lock and search again.
  RefCountedPtr<DomainStorage> new_storage;
  {
    MutexLock lock(&map_shard.mu);
    storage_map = map_shard.storage_map;
    // We must look up the storage map again, as it may have been created
    // by another thread while we were not holding the lock.
    weak_storage = storage_map.Lookup(label);
    if (weak_storage != nullptr) {
      // Found a weak pointer, try to upgrade it to a strong pointer.
      RefCountedPtr<DomainStorage> strong_storage =
          (*weak_storage)->RefIfNonZero();
      if (strong_storage != nullptr) {
        // Upgrade successful, return the strong pointer.
        return strong_storage;
      }
    }
    // Still no hit or orphaned: with the lock held we allocate a new storage
    // and insert it into the map.
    new_storage = CreateDomainStorage(std::move(label));
    auto new_weak_storage = new_storage->WeakRef();
    map_shard.storage_map =
        map_shard.storage_map.Add(new_weak_storage->label(), new_weak_storage);
  }
  // Add to active storage sets
  MutexLock lock(&active_storage_sets_mu_);
  for (auto* storage_set : active_storage_sets_) {
    storage_set->AddStorage(new_storage->WeakRef());
  }
  return new_storage;
}

}  // namespace instrument_detail

void TestOnlyResetInstruments() {
  Hook* hook = hooks.load(std::memory_order_acquire);
  while (hook != nullptr) {
    Hook* next = hook->next;
    delete hook;
    hook = next;
  }
  hooks.store(nullptr, std::memory_order_release);
  instrument_detail::QueryableDomain::TestOnlyResetAll();
}

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
    shard = std::make_unique<std::atomic<uint64_t>[]>(size);
    for (size_t i = 0; i < size; ++i) {
      shard[i].store(0, std::memory_order_relaxed);
    }
  }
}

uint64_t HighContentionBackend::Sum(size_t index) {
  uint64_t sum = 0;
  for (auto& shard : counters_) {
    sum += shard[index].load(std::memory_order_relaxed);
  }
  return sum;
}
}  // namespace grpc_core
