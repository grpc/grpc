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
#include <grpc/support/port_platform.h>

#include <algorithm>
#include <array>
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
#include "src/core/channelz/channelz.h"
#include "src/core/telemetry/histogram.h"
#include "src/core/util/avl.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/match.h"
#include "src/core/util/per_cpu.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/single_set_ptr.h"
#include "src/core/util/sync.h"

namespace grpc_core {

class InstrumentTest;

namespace instrument_detail {
class QueryableDomain;
class StorageSet;
}  // namespace instrument_detail

class CollectionScope;

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

// OpenTelemetry has no facility to export histogram data in the API (though
// there is a facility in the SDK). To cover this gap, if we are accessed via
// the OpenTelemetry API without the SDK being known to gRPC, we register a hook
// to be called when histogram data is collected.
// This comes with a relatively sever performance penalty. We'd like to be able
// to remove this in the future.
using HistogramCollectionHook = absl::AnyInvocable<void(
    const InstrumentMetadata::Description* instrument,
    absl::Span<const std::string> labels, int64_t value)>;
void RegisterHistogramCollectionHook(HistogramCollectionHook hook);

// A CollectionScope ensures that all metric updates in its lifetime are visible
// to a MetricsQuery.
class CollectionScope {
 public:
  explicit CollectionScope(
      std::vector<std::unique_ptr<instrument_detail::StorageSet>> storage_sets);

  size_t TestOnlyCountStorageHeld() const;

 private:
  friend class MetricsQuery;
  std::vector<instrument_detail::StorageSet*> GetStorageSets();

  std::vector<std::unique_ptr<instrument_detail::StorageSet>> storage_sets_;
};

namespace instrument_detail {

void CallHistogramCollectionHooks(
    const InstrumentMetadata::Description* instrument,
    absl::Span<const std::string> labels, int64_t value);

class GaugeStorage {
 public:
  explicit GaugeStorage(QueryableDomain* domain);

  void SetDouble(uint64_t offset, double value) {
    DCHECK_LT(offset, double_gauges_.size());
    double_gauges_[offset] = value;
  }
  void SetInt(uint64_t offset, int64_t value) {
    DCHECK_LT(offset, int_gauges_.size());
    int_gauges_[offset] = value;
  }
  void SetUint(uint64_t offset, uint64_t value) {
    DCHECK_LT(offset, uint_gauges_.size());
    uint_gauges_[offset] = value;
  }

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

 private:
  std::vector<std::optional<double>> double_gauges_;
  std::vector<std::optional<int64_t>> int_gauges_;
  std::vector<std::optional<uint64_t>> uint_gauges_;
};

class DomainStorage : public DualRefCounted<DomainStorage>,
                      public channelz::DataSource {
 public:
  DomainStorage(QueryableDomain* domain, std::vector<std::string> label);

  void Orphaned() override;

  virtual uint64_t SumCounter(size_t index) = 0;

  absl::Span<const std::string> label() const { return label_; }
  QueryableDomain* domain() const { return domain_; }

  virtual void FillGaugeStorage(GaugeStorage& gauge_storage) = 0;

  void AddData(channelz::DataSink sink) override;

 private:
  QueryableDomain* domain_;
  const std::vector<std::string> label_;
};

// Interface for a set of storage objects for a domain.
// Each StorageSet is a collection of storage objects for a domain, one storage
// object per unique set of labels.
// The StorageSet subscribes to new label sets being created, so that all
// storage in a time period can be exported.
class StorageSet {
 public:
  StorageSet(QueryableDomain* domain, size_t map_shards_size);
  virtual ~StorageSet();
  void ExportMetrics(
      MetricsSink& sink,
      absl::Span<const InstrumentMetadata::Description* const> metrics);
  size_t TestOnlyCountStorageHeld() const;
  QueryableDomain* domain() const { return domain_; }

  void AddStorage(WeakRefCountedPtr<DomainStorage> storage);

 private:
  struct MapShard {
    mutable Mutex mu;
    AVL<absl::Span<const std::string>, WeakRefCountedPtr<DomainStorage>>
        storage_map ABSL_GUARDED_BY(mu);
  };

  QueryableDomain* domain_;
  std::unique_ptr<MapShard[]> map_shards_;
  const size_t map_shards_size_;
};

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
  // Iterate all metric descriptions in all domains.
  static void ForEachInstrument(
      absl::FunctionRef<void(const InstrumentMetadata::Description*)> fn);

  // Returns the names of the labels in the domain.
  absl::Span<const std::string> label_names() const { return label_names_; }

  // Reset the internal state of all domains. For test use only.
  static void TestOnlyResetAll();
  // Reset the internal state of this domain. For test use only.
  void TestOnlyReset();

  static std::unique_ptr<CollectionScope> CreateCollectionScope();
  size_t TestOnlyCountStorageHeld() const;

  absl::string_view name() const { return name_; }

  RefCountedPtr<channelz::BaseNode> channelz_node() {
    if (!channelz_.is_set()) {
      return channelz_.Set(new ChannelzState(this))->channelz_node();
    }
    return channelz_->channelz_node();
  }

 protected:
  QueryableDomain(std::string name, std::vector<std::string> label_names,
                  size_t map_shards_size)
      : label_names_(std::move(label_names)),
        map_shards_size_(label_names_.empty() ? 1 : map_shards_size),
        map_shards_(std::make_unique<MapShard[]>(map_shards_size_)),
        name_(std::move(name)) {}

  // QueryableDomain should never be destroyed.
  ~QueryableDomain() { LOG(FATAL) << "QueryableDomain destroyed."; }

  RefCountedPtr<DomainStorage> GetDomainStorage(std::vector<std::string> label);

  // Called by InstrumentDomain when construction is complete.
  void Constructed();

  // Allocates a counter with the given name, description, and unit.
  const InstrumentMetadata::Description* AllocateCounter(
      absl::string_view name, absl::string_view description,
      absl::string_view unit);
  const InstrumentMetadata::Description* AllocateHistogram(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, HistogramBuckets bounds);
  const InstrumentMetadata::Description* AllocateDoubleGauge(
      absl::string_view name, absl::string_view description,
      absl::string_view unit);
  const InstrumentMetadata::Description* AllocateIntGauge(
      absl::string_view name, absl::string_view description,
      absl::string_view unit);
  const InstrumentMetadata::Description* AllocateUintGauge(
      absl::string_view name, absl::string_view description,
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
  friend class StorageSet;
  friend class DomainStorage;
  friend class GaugeStorage;

  struct MapShard {
    mutable Mutex mu;
    AVL<absl::Span<const std::string>, WeakRefCountedPtr<DomainStorage>>
        storage_map ABSL_GUARDED_BY(mu);
  };

  struct ChannelzState final : public channelz::DataSource {
    explicit ChannelzState(QueryableDomain* domain)
        : DataSource(MakeRefCounted<channelz::MetricsDomainNode>(
              std::string(domain->name()))),
          domain(domain) {
      SourceConstructed();
    }
    ~ChannelzState() { SourceDestructing(); }
    QueryableDomain* const domain;
    void AddData(channelz::DataSink sink) override { domain->AddData(sink); }
    RefCountedPtr<channelz::BaseNode> channelz_node() {
      return DataSource::channelz_node();
    }
  };

  void RegisterStorageSet(StorageSet* storage_set);
  void UnregisterStorageSet(StorageSet* storage_set);

  std::unique_ptr<StorageSet> CreateStorageSet();
  virtual RefCountedPtr<DomainStorage> CreateDomainStorage(
      std::vector<std::string> label) = 0;
  void DomainStorageOrphaned(DomainStorage* storage);
  MapShard& GetMapShard(absl::Span<const std::string> label);

  void AddData(channelz::DataSink sink);

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

  const size_t map_shards_size_;
  std::unique_ptr<MapShard[]> map_shards_;

  mutable Mutex active_storage_sets_mu_;
  std::vector<StorageSet*> active_storage_sets_
      ABSL_GUARDED_BY(active_storage_sets_mu_);

  std::string name_;
  SingleSetPtr<ChannelzState> channelz_;
};

// An InstrumentDomain is a collection of metrics with a common set of labels.
// The metrics can be of any type (counter, gauge, histogram, etc) and are
// all managed by a single instance of the InstrumentDomain.
// InstrumentDomains should be created at static initialization time.
// The InstrumentDomainImpl has a Backend, which defines how metrics are
// accumulated.
template <typename Backend, size_t N, typename Tag>
class InstrumentDomainImpl;

struct Counter {
  static constexpr size_t buckets() { return 1; }
  Counter operator->() const { return *this; }
  constexpr size_t BucketFor(int64_t /*value*/) const { return 0; }
};

// An InstrumentHandle is a handle to a single metric in an
// InstrumentDomainImpl. kType is used in using statements to disambiguate
// between different InstrumentHandle specializations. Backed, Label... are
// per InstrumentDomainImpl.
template <typename Shape, typename Domain>
class InstrumentHandle {
 private:
  friend Domain;

  InstrumentHandle(Domain* instrument_domain,
                   const InstrumentMetadata::Description* description,
                   Shape shape)
      : instrument_domain_(instrument_domain),
        offset_(description->offset),
        shape_(std::move(shape)),
        description_(description) {}

  Domain* instrument_domain_;
  uint64_t offset_;
  GPR_NO_UNIQUE_ADDRESS Shape shape_;
  const InstrumentMetadata::Description* description_ = nullptr;
};

template <typename T>
using StdString = std::string;

template <typename T>
using ConstCharPtr = const char*;

}  // namespace instrument_detail

// A domain backend for low contention domains.
// We use a simple array of atomics to back the collection - each increment
// is a relaxed add.
class LowContentionBackend final {
 public:
  explicit LowContentionBackend(size_t size);

  void Increment(size_t index) {
    counters_[index].fetch_add(1, std::memory_order_relaxed);
  }

  uint64_t Sum(size_t index);

 private:
  std::unique_ptr<std::atomic<uint64_t>[]> counters_;
};

// A domain backend for high contention domains.
// We shard the counters to reduce contention: increments happen on a shard
// selected by the current CPU, and reads need to accumulate across all the
// shards.
class HighContentionBackend final {
 public:
  explicit HighContentionBackend(size_t size);

  void Increment(size_t index) {
    counters_.this_cpu()[index].fetch_add(1, std::memory_order_relaxed);
  }

  uint64_t Sum(size_t index);

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
  void Apply(absl::Span<const std::string> label_names,
             absl::FunctionRef<void(MetricsSink&)> fn, MetricsSink& sink) const;

  // Runs the query, outputting the results to `sink`.
  void Run(std::unique_ptr<CollectionScope> scope, MetricsSink& sink) const;

 private:
  void ApplyLabelChecks(absl::Span<const std::string> label_names,
                        absl::FunctionRef<void(MetricsSink&)> fn,
                        MetricsSink& sink) const;

  absl::flat_hash_map<absl::string_view, std::string> label_eqs_;
  std::optional<std::vector<std::string>> only_metrics_;
  absl::flat_hash_set<std::string> collapsed_labels_;
};

namespace instrument_detail {

template <typename Shape, typename... Args>
Shape* GetMemoizedShape(Args&&... args) {
  // Many histograms are created with the same shape, so we try to deduplicate
  // them.
  using ShapeCache = absl::node_hash_map<std::tuple<Args...>, Shape*>;
  static ShapeCache* shape_cache = new ShapeCache();
  auto it =
      shape_cache->find(std::forward_as_tuple(std::forward<Args>(args)...));
  Shape* shape;
  if (it != shape_cache->end()) {
    shape = it->second;
  } else {
    shape = new Shape(std::forward<Args>(args)...);
    shape_cache->emplace(std::forward_as_tuple(std::forward<Args>(args)...),
                         shape);
  }
  return shape;
}

// An InstrumentDomainImpl is a collection of instruments with a common set of
// labels.
template <typename Backend, size_t N, typename Tag>
class InstrumentDomainImpl final : public QueryableDomain {
 public:
  using Self = InstrumentDomainImpl<Backend, N, Tag>;
  using CounterHandle = InstrumentHandle<Counter, Self>;
  using DoubleGaugeHandle =
      InstrumentHandle<InstrumentMetadata::DoubleGaugeShape, Self>;
  using IntGaugeHandle =
      InstrumentHandle<InstrumentMetadata::IntGaugeShape, Self>;
  using UintGaugeHandle =
      InstrumentHandle<InstrumentMetadata::UintGaugeShape, Self>;
  template <typename Shape>
  using HistogramHandle = InstrumentHandle<const Shape*, Self>;

  class GaugeSink {
   public:
    explicit GaugeSink(GaugeStorage& storage) : storage_(storage) {}

    void Set(InstrumentHandle<InstrumentMetadata::DoubleGaugeShape, Self> g,
             double x) {
      storage_.SetDouble(g.offset_, x);
    }
    void Set(InstrumentHandle<InstrumentMetadata::IntGaugeShape, Self> g,
             int64_t x) {
      storage_.SetInt(g.offset_, x);
    }
    void Set(InstrumentHandle<InstrumentMetadata::UintGaugeShape, Self> g,
             uint64_t x) {
      storage_.SetUint(g.offset_, x);
    }

   private:
    GaugeStorage& storage_;
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
  class Storage final : public DomainStorage {
   public:
    ~Storage() override = default;

    // Increments the counter specified by `handle` by 1 for this storages
    // labels.
    void Increment(CounterHandle handle) {
      DCHECK_EQ(handle.instrument_domain_, domain());
      backend_.Increment(handle.offset_);
    }

    template <typename Shape>
    void Increment(const HistogramHandle<Shape>& handle, int64_t value) {
      DCHECK_EQ(handle.instrument_domain_, domain());
      CallHistogramCollectionHooks(handle.description_, label(), value);
      backend_.Increment(handle.offset_ + handle.shape_->BucketFor(value));
    }

   private:
    friend class InstrumentDomainImpl<Backend, N, Tag>;
    friend class GaugeProvider;

    explicit Storage(InstrumentDomainImpl* instrument_domain,
                     std::vector<std::string> labels)
        : DomainStorage(instrument_domain, std::move(labels)),
          backend_(instrument_domain->allocated_counter_slots()) {}

    uint64_t SumCounter(size_t offset) override { return backend_.Sum(offset); }

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

    void FillGaugeStorage(GaugeStorage& storage) override {
      GaugeSink sink(storage);
      MutexLock lock(&gauge_providers_mu_);
      for (auto* provider : gauge_providers_) {
        provider->PopulateGaugeData(sink);
      }
    }

    Backend backend_;
    Mutex gauge_providers_mu_;
    std::vector<GaugeProvider*> gauge_providers_
        ABSL_GUARDED_BY(gauge_providers_mu_);
  };

  GPR_ATTRIBUTE_NOINLINE explicit InstrumentDomainImpl(
      std::string name, std::vector<std::string> label_names,
      size_t map_shards = std::min(16u, gpr_cpu_num_cores()))
      : QueryableDomain(std::move(name), std::move(label_names), map_shards) {
    CHECK_EQ(this->label_names().size(), N);
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
    auto* shape = GetMemoizedShape<Shape>(std::forward<Args>(args)...);
    const auto* desc =
        AllocateHistogram(name, description, unit, shape->bounds());
    return HistogramHandle<Shape>{this, desc, shape};
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
  template <typename... Args>
  RefCountedPtr<Storage> GetStorage(Args&&... labels) {
    static_assert(sizeof...(Args) == N, "Incorrect number of labels provided");
    std::vector<std::string> label_names;
    label_names.reserve(N);
    (label_names.emplace_back(absl::StrCat(labels)), ...);
    return DownCastRefCountedPtr<Storage>(
        GetDomainStorage(std::move(label_names)));
  }

  RefCountedPtr<DomainStorage> CreateDomainStorage(
      std::vector<std::string> labels) override {
    return RefCountedPtr<Storage>(new Storage(this, std::move(labels)));
  }

 private:
  ~InstrumentDomainImpl() = delete;
};

class MakeLabel {
 public:
  template <typename... LabelNames>
  auto operator()(LabelNames... t) {
    return std::vector<std::string>{absl::StrCat(t)...};
  }
};

template <typename... LabelNames>
GPR_ATTRIBUTE_NOINLINE auto MakeLabelFromTuple(
    std::tuple<LabelNames...> t) noexcept {
  return std::apply(MakeLabel(), t);
}
}  // namespace instrument_detail

template <class Derived>
class InstrumentDomain {
 public:
  static auto* Domain() {
    static auto* domain = new instrument_detail::InstrumentDomainImpl<
        typename Derived::Backend,
        std::tuple_size_v<decltype(Derived::kLabels)>, Derived>(
        absl::StrCat(Derived::kName),
        instrument_detail::MakeLabelFromTuple(Derived::kLabels));
    return domain;
  }

  // Returns an InstrumentStorageRefPtr<Derived>.
  template <typename... Args>
  static auto GetStorage(Args&&... labels) {
    return Domain()->GetStorage(std::forward<Args>(labels)...);
  }

 protected:
  template <typename... Label>
  static constexpr auto Labels(Label... labels) {
    return std::tuple<instrument_detail::ConstCharPtr<Label>...>{labels...};
  }

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

inline std::unique_ptr<CollectionScope> CreateCollectionScope() {
  return instrument_detail::QueryableDomain::CreateCollectionScope();
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TELEMETRY_INSTRUMENT_H
