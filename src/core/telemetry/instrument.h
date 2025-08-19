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

// This file defines the core interfaces for gRPC telemetry instrumentation.
//
// ## Concepts
//
// *   **Instrument:** An individual metric being tracked. This can be a
//     counter, gauge, histogram, etc. Each instrument has a unique name,
//     description, and unit.
//
// *   **Instrument Domain:** A collection of instruments that share a common
//     set of labels. For example, all metrics related to a specific resource
//     quota might belong to the same domain. Domains are defined by inheriting
//     from `InstrumentDomain` and specifying the labels and backend type.
//
// *   **Labels:** Key-value pairs that provide dimensions to metrics. Each
//     unique  combination of label values within a domain creates a separate
//     instance of the instrumentation storage.
//
// *   **Storage:** An object holding the current values for all instruments
//     within a domain, for a *specific combination* of label values. Storage
//     objects are ref-counted and managed by the domain. You obtain a
//     `RefCountedPtr<Storage>` using `Domain::GetStorage(...)`, passing the
//     current label values.
//
// *   **Backend:** Determines how the metric data is stored and aggregated
//     within a Storage object. Examples include `LowContentionBackend` and
//     `HighContentionBackend` for counters and histograms.
//
// ## Instrument Types
//
// *   **Counter:** A metric that only increases. Uses `RegisterCounter` and
//     `Storage::Increment`.
// *   **Histogram:** Tracks the distribution of values. Uses
//     `RegisterHistogram` and `Storage::Increment`.
// *   **Gauges:** Metrics that can go up or down, representing a current value.
//     *   `DoubleGauge`: for double values.
//     *   `IntGauge`: for int64_t values.
//     *   `UintGauge`: for uint64_t values.
//     Gauges are registered using `RegisterDoubleGauge`, `RegisterIntGauge`, or
//     `RegisterUintGauge`.
//
// ## Gauge Providers
//
// Unlike counters and histograms, gauge values are not stored directly in the
// `Backend`. Instead, they are derived from the current state of an object.
// Objects that need to expose gauge metrics should implement the
// `GaugeProvider<Domain>` interface. They register/unregister via methods on
// the base class.
//
// Example:
//   // In your class that has the gauge data:
//   class MyProvider : public GaugeProvider<MyDomain> {
//    public:
//     MyProvider(InstrumentStorageRefPtr<MyDomain> storage)
//         : GaugeProvider<MyDomain>(std::move(storage)) {
//       ProviderConstructed();
//     }
//     ~MyProvider() override { ProviderDestructing(); }
//
//     void PopulateGaugeData(GaugeSink<MyDomain>& sink) override {
//       sink.Set(MyDomain::kMyGauge, GetCurrentValue());
//     }
//   };
//
// ## Declaring an Instrument Domain
//
// To define a new set of metrics, you create a class that inherits from
// `InstrumentDomain<YourDomainName>`. This class must define:
//
// 1.  `using Backend = ...;`: Specifies the backend type (e.g.,
//      `LowContentionBackend`, `HighContentionBackend`).
// 2.  `static constexpr auto kLabels = std::tuple(...);`: Defines the names
//      of the labels for this domain. The types of the labels are inferred
//      from the arguments passed to `GetStorage()`.
//
// Instruments are registered as static members within the domain class using
// the `Register*` methods.
//
// Example:
//   class MyDomain : public InstrumentDomain<MyDomain> {
//    public:
//     using Backend = LowContentionBackend;
//     static constexpr auto kLabels = std::tuple("my_label", "another_label");
//
//     // Register a counter:
//     static inline const auto kMyCounter = RegisterCounter(
//         "grpc.my_domain.my_counter", "Description of my counter", "units");
//
//     // Register a gauge:
//     static inline const auto kMyGauge = RegisterIntGauge(
//         "grpc.my_domain.my_gauge", "Description of my gauge", "units");
//   };
//
// To increment the counter:
//   auto storage = MyDomain::GetStorage("label_val1", "label_val2");
//   storage->Increment(MyDomain::kMyCounter);
//
// To set the gauge (inside a callback):
//   sink.Set(MyDomain::kMyGauge, current_gauge_value);
//
// ## Querying Metrics
//
// The `MetricsQuery` class is used to fetch metric data. You can filter by
// label values, select specific metrics, and collapse labels (aggregate over
// them). The results are emitted to a `MetricsSink` interface.
//
// ## Aggregability
//
// *   **Counters & Histograms:** Are aggregatable. When labels are collapsed
//     using `MetricsQuery::CollapseLabels`, values from different label
//     combinations are summed up.
// *   **Gauges:** Are NOT aggregatable. Collapsing labels on a query that
//     includes gauges is not meaningful, as summing up current values from
//     different sources makes no sense. The `MetricsSink` will receive
//     individual gauge readings for each label set matching the filter.

#ifndef GRPC_SRC_CORE_TELEMETRY_INSTRUMENT_H
#define GRPC_SRC_CORE_TELEMETRY_INSTRUMENT_H

#include <grpc/support/cpu.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/core/telemetry/histogram.h"
#include "src/core/util/avl.h"
#include "src/core/util/match.h"
#include "src/core/util/per_cpu.h"
#include "src/core/util/sync.h"

namespace grpc_core {

namespace instrument_detail {
class QueryableDomain;
}  // namespace instrument_detail

class InstrumentMetadata {
 public:
  struct CounterShape {};
  struct DoubleGaugeShape {};
  struct IntGaugeShape {};
  struct UintGaugeShape {};
  using HistogramShape = HistogramBuckets;

  using Shape = std::variant<CounterShape, HistogramShape, DoubleGaugeShape,
                             IntGaugeShape, UintGaugeShape>;

  // A description of a metric.
  struct Description {
    // The domain that owns the metric.
    instrument_detail::QueryableDomain* domain;
    // The offset of the metric within the domain's allocated metrics.
    uint64_t offset;
    // The name of the metric.
    absl::string_view name;
    // A description of the metric.
    absl::string_view description;
    // The unit of the metric.
    absl::string_view unit;
    // The shape of the metric - for counters this is empty.
    // For histograms, it defines the buckets.
    Shape shape;
  };

  // Iterate all metric descriptions in all domains.
  static void ForEachInstrument(absl::FunctionRef<void(const Description*)> fn);
};

class MetricsQuery;
class MetricsSink;

namespace instrument_detail {

class QueryableDomain;

// A registry of metrics.
// In this singleton we maintain metadata about all registered metrics.
class InstrumentIndex {
 public:
  // Returns the singleton instance of the InstrumentIndex.
  static InstrumentIndex& Get() {
    static InstrumentIndex* index = new InstrumentIndex();
    return *index;
  }

  // Registers a metric with the given name, description, unit, and shape.
  // Returns a pointer to the Description struct, which contains metadata about
  // the metric.
  const InstrumentMetadata::Description* Register(
      QueryableDomain* domain, uint64_t offset, absl::string_view name,
      absl::string_view description, absl::string_view unit,
      InstrumentMetadata::Shape shape);

  // Finds a metric with the given name, or nullptr if not found.
  const InstrumentMetadata::Description* Find(absl::string_view name) const;

 private:
  InstrumentIndex() = default;

  // A map of metric name to Description. We use node_hash_map because we need
  // pointer stability for the values.
  absl::node_hash_map<absl::string_view, InstrumentMetadata::Description>
      metrics_;
};

// A QueryableDomain is a collection of metrics with a common set of labels.
// The metrics can be of any type (counter, gauge, histogram, etc) and are
// all managed by a single instance of the QueryableDomain.
// QueryableDomain is the base class for InstrumentDomainImpl, and contains
// common functionality that doesn't need to know about exact types.
class QueryableDomain {
 public:
  // Exports all metrics selected by `query` to `sink`.
  // This is the backing function for MetricsQuery::Run, which should be the
  // preferred way to export metrics.
  static void ExportMetrics(const MetricsQuery& query, MetricsSink& sink);

  // Iterate all metric descriptions in all domains.
  static void ForEachInstrument(
      absl::FunctionRef<void(const InstrumentMetadata::Description*)> fn);

  // Returns the names of the labels in the domain.
  absl::Span<const std::string> label_names() const { return label_names_; }

  // Reset the internal state of all domains. For test use only.
  static void TestOnlyResetAll();
  // Reset the internal state of this domain. For test use only.
  virtual void TestOnlyReset() = 0;

 protected:
  explicit QueryableDomain(std::vector<std::string> label_names)
      : label_names_(std::move(label_names)) {}

  // QueryableDomain should never be destroyed.
  ~QueryableDomain() { LOG(FATAL) << "QueryableDomain destroyed."; }

  // Called by InstrumentDomain when construction is complete.
  void Constructed();

  // Allocates a counter with the given name, description, and unit.
  uint64_t AllocateCounter(absl::string_view name,
                           absl::string_view description,
                           absl::string_view unit);
  uint64_t AllocateHistogram(absl::string_view name,
                             absl::string_view description,
                             absl::string_view unit, HistogramBuckets bounds);
  uint64_t AllocateDoubleGauge(absl::string_view name,
                               absl::string_view description,
                               absl::string_view unit);
  uint64_t AllocateIntGauge(absl::string_view name,
                            absl::string_view description,
                            absl::string_view unit);
  uint64_t AllocateUintGauge(absl::string_view name,
                             absl::string_view description,
                             absl::string_view unit);

  // Returns the number of slots allocated for each metric type.
  uint64_t allocated_counter_slots() const { return allocated_counter_slots_; }
  uint64_t allocated_double_gauge_slots() const {
    return allocated_double_gauge_slots_;
  }
  uint64_t allocated_int_gauge_slots() const {
    return allocated_int_gauge_slots_;
  }
  uint64_t allocated_uint_gauge_slots() const {
    return allocated_uint_gauge_slots_;
  }

 private:
  // Exports all metrics in `metrics` to `sink`.
  virtual void ExportMetrics(
      MetricsSink& sink,
      absl::Span<const InstrumentMetadata::Description* const> metrics) = 0;

  // Allocate `size` elements in the domain.
  // Counters will allocate one element. Histograms will allocate one per
  // bucket.
  uint64_t AllocateCounterSlots(size_t size) {
    const uint64_t offset = allocated_counter_slots_;
    allocated_counter_slots_ += size;
    return offset;
  }

  // We keep a linked list of all QueryableDomains, so that we can walk
  // them in order to export metrics.
  static inline QueryableDomain* last_ = nullptr;
  QueryableDomain* prev_ = nullptr;

  const std::vector<std::string> label_names_;
  std::vector<const InstrumentMetadata::Description*> metrics_;
  uint64_t allocated_counter_slots_ = 0;
  uint64_t allocated_double_gauge_slots_ = 0;
  uint64_t allocated_int_gauge_slots_ = 0;
  uint64_t allocated_uint_gauge_slots_ = 0;
};

// An InstrumentDomain is a collection of metrics with a common set of labels.
// The metrics can be of any type (counter, gauge, histogram, etc) and are
// all managed by a single instance of the InstrumentDomain.
// InstrumentDomains should be created at static initialization time.
// The InstrumentDomainImpl has a Backend, which defines how metrics are
// accumulated.
template <typename Backend, typename... Label>
class InstrumentDomainImpl;

struct Counter {
  static constexpr size_t buckets() { return 1; }
  Counter operator->() const { return *this; }
  constexpr size_t BucketFor(int64_t /*value*/) const { return 0; }
};

// An InstrumentHandle is a handle to a single metric in an
// InstrumentDomainImpl. kType is used in using statements to disambiguate
// between different InstrumentHandle specializations. Backed, Label... are per
// InstrumentDomainImpl.
template <typename Shape, typename Backend, typename... Label>
class InstrumentHandle {
 private:
  friend class InstrumentDomainImpl<Backend, Label...>;

  InstrumentHandle(InstrumentDomainImpl<Backend, Label...>* instrument_domain,
                   uint64_t offset, Shape shape)
      : instrument_domain_(instrument_domain),
        offset_(offset),
        shape_(std::move(shape)) {}

  InstrumentDomainImpl<Backend, Label...>* instrument_domain_;
  uint64_t offset_;
  GPR_NO_UNIQUE_ADDRESS Shape shape_;
};

template <typename T>
using StdString = std::string;

}  // namespace instrument_detail

// A domain backend for low contention domains.
// We use a simple array of atomics to back the collection - each increment
// is a relaxed add.
class LowContentionBackend {
 public:
  explicit LowContentionBackend(size_t size)
      : counters_(new std::atomic<uint64_t>[size]) {
    for (size_t i = 0; i < size; ++i) {
      counters_[i].store(0, std::memory_order_relaxed);
    }
  }

  void Increment(size_t index) {
    counters_[index].fetch_add(1, std::memory_order_relaxed);
  }

  uint64_t Sum(size_t index) {
    return counters_[index].load(std::memory_order_relaxed);
  }

 private:
  std::unique_ptr<std::atomic<uint64_t>[]> counters_;
};

// A domain backend for high contention domains.
// We shard the counters to reduce contention: increments happen on a shard
// selected by the current CPU, and reads need to accumulate across all the
// shards.
class HighContentionBackend {
 public:
  explicit HighContentionBackend(size_t size) {
    for (auto& shard : counters_) {
      shard = std::make_unique<std::atomic<uint64_t>[]>(size);
      for (size_t i = 0; i < size; ++i) {
        shard[i].store(0, std::memory_order_relaxed);
      }
    }
  }

  void Increment(size_t index) {
    counters_.this_cpu()[index].fetch_add(1, std::memory_order_relaxed);
  }

  uint64_t Sum(size_t index) {
    uint64_t sum = 0;
    for (auto& shard : counters_) {
      sum += shard[index].load(std::memory_order_relaxed);
    }
    return sum;
  }

 private:
  PerCpu<std::unique_ptr<std::atomic<uint64_t>[]>> counters_{
      PerCpuOptions().SetMaxShards(16)};
};

// MetricsSink is an interface for accumulating metrics.
// Importantly it's the output interface for MetricsQuery.
class MetricsSink {
 public:
  // Called once per label per metric, with the value of that metric for that
  // label.
  virtual void Counter(absl::Span<const std::string> label,
                       absl::string_view name, uint64_t value) = 0;
  virtual void Histogram(absl::Span<const std::string> label,
                         absl::string_view name, HistogramBuckets bounds,
                         absl::Span<const uint64_t> counts) = 0;
  virtual void DoubleGauge(absl::Span<const std::string> labels,
                           absl::string_view name, double value) = 0;
  virtual void IntGauge(absl::Span<const std::string> labels,
                        absl::string_view name, int64_t value) = 0;
  virtual void UintGauge(absl::Span<const std::string> labels,
                         absl::string_view name, uint64_t value) = 0;

 protected:
  ~MetricsSink() = default;
};

// A MetricsQuery allows querying across the global set of metrics and
// fetching their values.
// Allows a level of filtering so that we only get the values for metrics
// that match a set of criteria.
// Also allows collapsing labels (effectively omitting them) and aggregating
// over the remaining labels.
class MetricsQuery {
 public:
  // Only include metrics that include `label` and have that label equal to
  // `value`.
  MetricsQuery& WithLabelEq(absl::string_view label, std::string value);
  // Collapse labels, effectively omitting them. Counters are summed over the
  // remaining dimensions, etc.
  MetricsQuery& CollapseLabels(absl::Span<const std::string> labels);
  // Only include metrics that are in `metrics`.
  MetricsQuery& OnlyMetrics(absl::Span<const std::string> metrics);

  // Returns the metrics that are selected by this query.
  std::optional<absl::Span<const std::string>> selected_metrics() const {
    return only_metrics_;
  }

  // Adapts `sink` by including the filtering requested, and then calls `fn`
  // with the filtering sink. This is mainly an implementation detail.
  template <typename Fn>
  void Apply(absl::Span<const std::string> label_names, Fn fn,
             MetricsSink& sink) const;

  // Runs the query, outputting the results to `sink`.
  void Run(MetricsSink& sink) const;

 private:
  template <typename Fn, typename Sink>
  void ApplyLabelChecks(absl::Span<const std::string> label_names, Fn fn,
                        Sink& sink) const;

  absl::flat_hash_map<absl::string_view, std::string> label_eqs_;
  std::optional<std::vector<std::string>> only_metrics_;
  absl::flat_hash_set<std::string> collapsed_labels_;
};

namespace instrument_detail {

// An InstrumentDomainImpl is a collection of instruments with a common set of
// labels.
template <typename Backend, typename... Label>
class InstrumentDomainImpl final : public QueryableDomain {
 public:
  using LabelTuple = std::tuple<Label...>;

  using CounterHandle = InstrumentHandle<Counter, Backend, Label...>;
  using DoubleGaugeHandle =
      InstrumentHandle<InstrumentMetadata::DoubleGaugeShape, Backend, Label...>;
  using IntGaugeHandle =
      InstrumentHandle<InstrumentMetadata::IntGaugeShape, Backend, Label...>;
  using UintGaugeHandle =
      InstrumentHandle<InstrumentMetadata::UintGaugeShape, Backend, Label...>;
  template <typename Shape>
  using HistogramHandle = InstrumentHandle<const Shape*, Backend, Label...>;

  class GaugeSink {
   public:
    explicit GaugeSink(const InstrumentDomainImpl* domain)
        : double_gauges_(domain->allocated_double_gauge_slots()),
          int_gauges_(domain->allocated_int_gauge_slots()),
          uint_gauges_(domain->allocated_uint_gauge_slots()) {}

    void Set(InstrumentHandle<InstrumentMetadata::DoubleGaugeShape, Backend,
                              Label...>
                 g,
             double x) {
      DCHECK_LT(g.offset_, double_gauges_.size());
      double_gauges_[g.offset_] = x;
    }
    void Set(
        InstrumentHandle<InstrumentMetadata::IntGaugeShape, Backend, Label...>
            g,
        int64_t x) {
      DCHECK_LT(g.offset_, int_gauges_.size());
      int_gauges_[g.offset_] = x;
    }
    void Set(
        InstrumentHandle<InstrumentMetadata::UintGaugeShape, Backend, Label...>
            g,
        uint64_t x) {
      DCHECK_LT(g.offset_, uint_gauges_.size());
      uint_gauges_[g.offset_] = x;
    }

   private:
    friend class InstrumentDomainImpl<Backend, Label...>;

    std::optional<double> GetDouble(uint64_t offset) const {
      DCHECK_LT(offset, double_gauges_.size());
      return double_gauges_[offset];
    }
    std::optional<int64_t> GetInt(uint64_t offset) const {
      DCHECK_LT(offset, int_gauges_.size());
      return int_gauges_[offset];
    }
    std::optional<uint64_t> GetUint(uint64_t offset) const {
      DCHECK_LT(offset, uint_gauges_.size());
      return uint_gauges_[offset];
    }

    std::vector<std::optional<double>> double_gauges_;
    std::vector<std::optional<int64_t>> int_gauges_;
    std::vector<std::optional<uint64_t>> uint_gauges_;
  };

  class Storage;

  // Interface for objects that provide gauge values for this domain.
  class GaugeProvider {
   public:
    virtual void PopulateGaugeData(GaugeSink& sink) = 0;

   protected:
    explicit GaugeProvider(RefCountedPtr<Storage> storage)
        : storage_(std::move(storage)) {
      DCHECK(storage_ != nullptr);
    }
    ~GaugeProvider() { DCHECK(storage_ == nullptr); }

    void ProviderConstructed() {
      DCHECK(storage_ != nullptr);
      storage_->RegisterGaugeProvider(this);
    }
    void ProviderDestructing() {
      DCHECK(storage_ != nullptr);
      storage_->UnregisterGaugeProvider(this);
      storage_.reset();
    }

   private:
    RefCountedPtr<Storage> storage_;
  };

  // Storage is a ref-counted object that holds the backend for an
  // InstrumentDomain for a single set of labels.
  class Storage final : public RefCounted<Storage, NonPolymorphicRefCount> {
   public:
    ~Storage() = default;

    // Increments the counter specified by `handle` by 1 for this storages
    // labels.
    void Increment(CounterHandle handle) {
      DCHECK_EQ(handle.instrument_domain_, instrument_domain_);
      backend_.Increment(handle.offset_);
    }

    template <typename Shape>
    void Increment(const HistogramHandle<Shape>& handle, int64_t value) {
      DCHECK_EQ(handle.instrument_domain_, instrument_domain_);
      backend_.Increment(handle.offset_ + handle.shape_->BucketFor(value));
    }

   protected:
    explicit Storage(InstrumentDomainImpl* instrument_domain)
        : instrument_domain_(instrument_domain),
          backend_(instrument_domain->allocated_counter_slots()) {}

   private:
    friend class InstrumentDomainImpl<Backend, Label...>;
    friend class GaugeProvider;

    void RegisterGaugeProvider(GaugeProvider* provider) {
      MutexLock lock(&gauge_providers_mu_);
      gauge_providers_.push_back(provider);
    }

    void UnregisterGaugeProvider(GaugeProvider* provider) {
      MutexLock lock(&gauge_providers_mu_);
      gauge_providers_.erase(std::remove(gauge_providers_.begin(),
                                         gauge_providers_.end(), provider),
                             gauge_providers_.end());
    }

    InstrumentDomainImpl* instrument_domain_;
    Backend backend_;
    Mutex gauge_providers_mu_;
    std::vector<GaugeProvider*> gauge_providers_
        ABSL_GUARDED_BY(gauge_providers_mu_);
  };

  explicit InstrumentDomainImpl(
      std::vector<std::string> label_names,
      size_t map_shards = std::min(16u, gpr_cpu_num_cores()))
      : QueryableDomain(std::move(label_names)) {
    CHECK_EQ(this->label_names().size(), sizeof...(Label));
    if constexpr (sizeof...(Label) == 0) {
      map_shards_size_ = 1;
    } else {
      map_shards_size_ = std::max<size_t>(1, map_shards);
    }
    map_shards_ = std::make_unique<MapShard[]>(map_shards_size_);
    Constructed();
  }

  // Registration functions: must all complete before the first GetStorage call.
  // No locking is performed, so registrations must be performed with external
  // synchronization.
  // Effectively: Do this at static initialization time.

  CounterHandle RegisterCounter(absl::string_view name,
                                absl::string_view description,
                                absl::string_view unit) {
    return CounterHandle{this, AllocateCounter(name, description, unit),
                         Counter{}};
  }

  template <typename Shape, typename... Args>
  HistogramHandle<Shape> RegisterHistogram(absl::string_view name,
                                           absl::string_view description,
                                           absl::string_view unit,
                                           Args&&... args) {
    // Many histograms are created with the same shape, so we try to deduplicate
    // them.
    using ShapeCache = absl::node_hash_map<std::tuple<Args...>, const Shape*>;
    static ShapeCache* shape_cache = new ShapeCache();
    auto it =
        shape_cache->find(std::forward_as_tuple(std::forward<Args>(args)...));
    const Shape* shape;
    if (it != shape_cache->end()) {
      shape = it->second;
    } else {
      shape = new Shape(std::forward<Args>(args)...);
      shape_cache->emplace(std::forward_as_tuple(std::forward<Args>(args)...),
                           shape);
    }
    const size_t offset =
        AllocateHistogram(name, description, unit, shape->bounds());
    return HistogramHandle<Shape>{this, offset, shape};
  }

  DoubleGaugeHandle RegisterDoubleGauge(absl::string_view name,
                                        absl::string_view description,
                                        absl::string_view unit) {
    return DoubleGaugeHandle{this, AllocateDoubleGauge(name, description, unit),
                             InstrumentMetadata::DoubleGaugeShape{}};
  }

  IntGaugeHandle RegisterIntGauge(absl::string_view name,
                                  absl::string_view description,
                                  absl::string_view unit) {
    return IntGaugeHandle{this, AllocateIntGauge(name, description, unit),
                          InstrumentMetadata::IntGaugeShape{}};
  }

  UintGaugeHandle RegisterUintGauge(absl::string_view name,
                                    absl::string_view description,
                                    absl::string_view unit) {
    return UintGaugeHandle{this, AllocateUintGauge(name, description, unit),
                           InstrumentMetadata::UintGaugeShape{}};
  }

  // GetStorage: returns a pointer to the storage for the given key, creating
  // it if necessary.
  RefCountedPtr<Storage> GetStorage(Label... labels) {
    auto label = LabelTuple(std::move(labels)...);
    size_t shard;
    if constexpr (sizeof...(Label) == 0) {
      shard = 0;
    } else if constexpr (sizeof...(Label) == 1) {
      // Use a salted label to shard, so that we remove minimal entropy from the
      // main table.
      shard = absl::HashOf(std::get<0>(label), this) % map_shards_size_;
    } else {
      // Use the first label to shard, all labels to index.
      shard = absl::HashOf(std::get<0>(label)) % map_shards_size_;
    }
    RefCountedPtr<Storage> new_storage;
    MapShard& map_shard = map_shards_[shard];
    // First try to get an existing storage.
    map_shard.mu.Lock();
    auto storage_map = map_shard.storage_map;
    map_shard.mu.Unlock();
    // With an AVL we can search outside the lock.
    auto* storage = storage_map.Lookup(label);
    if (storage == nullptr) {
      // No hit: now we lock and search again.
      MutexLock lock(&map_shard.mu);
      storage_map = map_shard.storage_map;
      // We must look up the storage map again, as it may have been created
      // by another thread while we were not holding the lock.
      storage = storage_map.Lookup(label);
      if (storage == nullptr) {
        // Still no hit: with the lock held we allocate a new storage and insert
        // it into the map.
        new_storage.reset(new Storage(this));
        map_shard.storage_map = storage_map.Add(label, new_storage);
        storage = &new_storage;
      }
    }
    return *storage;
  }

 private:
  struct MapShard {
    Mutex mu;
    AVL<LabelTuple, RefCountedPtr<Storage>> storage_map ABSL_GUARDED_BY(mu);
  };

  ~InstrumentDomainImpl() = delete;

  void TestOnlyReset() override {
    map_shards_.reset(new MapShard[map_shards_size_]);
  }

  // Exports all metrics in `metrics` to `sink`.
  void ExportMetrics(MetricsSink& sink,
                     absl::Span<const InstrumentMetadata::Description* const>
                         metrics) override {
    // Walk over the map shards.
    for (size_t i = 0; i < map_shards_size_; ++i) {
      // Fetch the storage map, then process it outside the lock.
      auto& map_shard = map_shards_[i];
      map_shard.mu.Lock();
      auto storage_map = map_shard.storage_map;
      map_shard.mu.Unlock();
      storage_map.ForEach([&](const auto& label, const auto& storage) {
        std::vector<std::string> label_names = std::apply(
            [&](auto&&... args) {
              return std::vector<std::string>{absl::StrCat(args)...};
            },
            label);
        GaugeSink gauge_sink(this);
        {
          MutexLock lock(&storage->gauge_providers_mu_);
          for (auto* provider : storage->gauge_providers_) {
            provider->PopulateGaugeData(gauge_sink);
          }
        }
        for (const auto* metric : metrics) {
          Match(
              metric->shape,
              [metric, &sink, storage = storage.get(),
               &label_names](InstrumentMetadata::CounterShape) {
                sink.Counter(label_names, metric->name,
                             storage->backend_.Sum(metric->offset));
              },
              [metric, &sink, storage = storage.get(),
               &label_names](InstrumentMetadata::HistogramShape bounds) {
                std::vector<uint64_t> counts(bounds.size());
                for (size_t i = 0; i < bounds.size(); ++i) {
                  counts[i] = storage->backend_.Sum(metric->offset + i);
                }
                sink.Histogram(label_names, metric->name, bounds, counts);
              },
              [metric, &sink, &gauge_sink,
               &label_names](InstrumentMetadata::DoubleGaugeShape) {
                if (auto value = gauge_sink.GetDouble(metric->offset);
                    value.has_value()) {
                  sink.DoubleGauge(label_names, metric->name, *value);
                }
              },
              [metric, &sink, &gauge_sink,
               &label_names](InstrumentMetadata::IntGaugeShape) {
                if (auto value = gauge_sink.GetInt(metric->offset);
                    value.has_value()) {
                  sink.IntGauge(label_names, metric->name, *value);
                }
              },
              [metric, &sink, &gauge_sink,
               &label_names](InstrumentMetadata::UintGaugeShape) {
                if (auto value = gauge_sink.GetUint(metric->offset);
                    value.has_value()) {
                  sink.UintGauge(label_names, metric->name, *value);
                }
              });
        }
      });
    }
  }

  std::unique_ptr<MapShard[]> map_shards_;
  size_t map_shards_size_;
};
}  // namespace instrument_detail

template <class Derived>
class InstrumentDomain {
 private:
  class MakeDomainWithLabels {
   public:
    template <typename... LabelNames>
    auto operator()(LabelNames... label_names) {
      return new instrument_detail::InstrumentDomainImpl<
          typename Derived::Backend,
          instrument_detail::StdString<LabelNames>...>(
          std::vector<std::string>{absl::StrCat(label_names)...});
    }
  };

 public:
  static auto* Domain() {
    static auto* domain = std::apply(MakeDomainWithLabels(), Derived::kLabels);
    return domain;
  }

  // Returns an InstrumentStorageRefPtr<Derived>.
  template <typename... Label>
  static auto GetStorage(Label... labels) {
    return Domain()->GetStorage(std::move(labels)...);
  }

 protected:
  static auto RegisterCounter(absl::string_view name,
                              absl::string_view description,
                              absl::string_view unit) {
    return Domain()->RegisterCounter(name, description, unit);
  }

  template <typename Shape, typename... Args>
  static auto RegisterHistogram(absl::string_view name,
                                absl::string_view description,
                                absl::string_view unit, Args&&... args) {
    return Domain()->template RegisterHistogram<Shape>(
        name, description, unit, std::forward<Args>(args)...);
  }

  static auto RegisterDoubleGauge(absl::string_view name,
                                  absl::string_view description,
                                  absl::string_view unit) {
    return Domain()->RegisterDoubleGauge(name, description, unit);
  }

  static auto RegisterIntGauge(absl::string_view name,
                               absl::string_view description,
                               absl::string_view unit) {
    return Domain()->RegisterIntGauge(name, description, unit);
  }

  static auto RegisterUintGauge(absl::string_view name,
                                absl::string_view description,
                                absl::string_view unit) {
    return Domain()->RegisterUintGauge(name, description, unit);
  }

 private:
  InstrumentDomain() = delete;
};

template <typename DomainType>
using InstrumentDomainImpl =
    std::remove_pointer_t<decltype(DomainType::Domain())>;

template <typename DomainType>
using InstrumentStorage = typename InstrumentDomainImpl<DomainType>::Storage;

template <typename DomainType>
using InstrumentStorageRefPtr = RefCountedPtr<InstrumentStorage<DomainType>>;

template <typename DomainType>
using GaugeSink = typename InstrumentDomainImpl<DomainType>::GaugeSink;

template <typename DomainType>
using GaugeProvider = typename InstrumentDomainImpl<DomainType>::GaugeProvider;

// Reset all registered instruments. For test use only.
void TestOnlyResetInstruments();

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TELEMETRY_INSTRUMENT_H
