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
// *   **Collection Scope:** Defines a scope for collecting metrics, identified
//     by a set of labels of interest. Metric collection via
//     `GetStorage`+`Increment` will be filtered according to these labels.
//     Scopes can be hierarchical.
//     On destruction, metrics collected in this scope are aggregated into the
//     parent scopes.
//
// *   **Storage:** An object holding the current values for all instruments
//     within a domain, for a *specific combination* of filtered label values.
//     Its lifetime is managed by one or more `CollectionScope`s. You obtain a
//     `RefCountedPtr<Storage>` using `Domain::GetStorage(scope, ...)`, passing
//     the current label values. If a child scope's filtered labels match its
//     parent's filtered labels for a given metric, the parent's `Storage`
//     instance is reused (shared).
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
// 2.  `GRPC_INSTRUMENT_DOMAIN_LABELS("label1", "label2", ...);`: Defines the
//      names of the labels for this domain via a macro that generates a
//      static `Labels()` method. The types of the labels are inferred from
//      the arguments passed to `GetStorage()`.
//
// Instruments are registered as static members within the domain class using
// the `Register*` methods.
//
// Example:
//   class MyDomain : public InstrumentDomain<MyDomain> {
//    public:
//     using Backend = LowContentionBackend;
//     GRPC_INSTRUMENT_DOMAIN_LABELS("my_label", "another_label");
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
//   auto scope = CreateCollectionScope({}, {}); // Or some other scope
//   auto storage = MyDomain::GetStorage(scope, "label_val1", "label_val2");
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
// `MetricsQuery::Run(scope, sink)` operates on a given `CollectionScope`,
// querying all unique storage instances reachable from that scope and its
// children.
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
//
// ## Collection Scope Hierarchy
//
// Collection scopes form a DAG. The typical layout is to have a collection of
// root scopes, a trunk scope ("the global scope"), and a set of leaf scopes:
//
// ┌────────┐     ┌────────┐
// │ Root 1 │     │ Root 2 │     ...
// └───┬────┘     └───┬────┘
//     │              │
//     └──────────────┤
//                    │
//             ┌──────▼───────┐
//             │ Global Scope │
//             └──────┬───────┘
//                    │
//     ┌──────────────┤
//     │              │
// ┌───▼────┐     ┌───▼────┐
// │ Leaf 1 │     │ Leaf 2 │     ...
// └────────┘     └────────┘
//
// The root scopes correspond to global stats plugins in the higher level
// system. The leaf scopes correspond to per-channel stats plugins. The global
// (trunk) scope is not associated with any stats plugin, but allows
// non-channel-related metrics to be aggregated into the global stats plugins.
//
// When creating a storage instance systems should use the most specific scope
// (lowest in the tree) that matches the current context.

#ifndef GRPC_SRC_CORE_TELEMETRY_INSTRUMENT_H
#define GRPC_SRC_CORE_TELEMETRY_INSTRUMENT_H

#include <grpc/support/cpu.h>
#include <grpc/support/port_platform.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/channelz/channelz.h"
#include "src/core/telemetry/histogram.h"
#include "src/core/util/avl.h"
#include "src/core/util/bitset.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/per_cpu.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/single_set_ptr.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {

class InstrumentTest;
class GlobalCollectionScopeManager;

static constexpr absl::string_view kOmittedLabel = "<omitted>";

namespace instrument_detail {
class QueryableDomain;
class DomainStorage;
}  // namespace instrument_detail

class InstrumentLabel {
 public:
  static constexpr size_t kMaxLabelsPerProcess = 63;
  static constexpr size_t kMaxLabelsPerDomain = 15;

  InstrumentLabel() : index_(kSentinelIndex) {}
  explicit InstrumentLabel(absl::string_view label);
  explicit InstrumentLabel(const char* label)
      : InstrumentLabel(absl::string_view(label)) {}

  static InstrumentLabel FromIndex(uint8_t index) {
    InstrumentLabel label;
    label.index_ = index;
    return label;
  }

  uint8_t index() const { return index_; }
  absl::string_view label() const {
    CHECK_NE(index_, kSentinelIndex);
    std::atomic<const std::string*>* labels = GetLabels();
    const std::string* label = labels[index_].load(std::memory_order_acquire);
    CHECK_NE(label, nullptr)
        << "Label index " << static_cast<int>(index_) << " is out of range";
    return *label;
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, InstrumentLabel label) {
    sink.Append(label.label());
  }

  template <typename H>
  friend H AbslHashValue(H h, InstrumentLabel label) {
    return H::combine(std::move(h), label.index_);
  }

  friend bool operator==(InstrumentLabel a, InstrumentLabel b) {
    return a.index_ == b.index_;
  }

  friend bool operator!=(InstrumentLabel a, InstrumentLabel b) {
    return a.index_ != b.index_;
  }

  friend bool operator<(InstrumentLabel a, InstrumentLabel b) {
    return a.index_ < b.index_;
  }

  friend bool operator>(InstrumentLabel a, InstrumentLabel b) {
    return a.index_ > b.index_;
  }

  static std::string RegistrationDebugString();
  static std::atomic<const std::string*>* GetLabels();

 private:
  static constexpr uint8_t kSentinelIndex = 255;
  uint8_t index_ = kSentinelIndex;
};

class InstrumentLabelList;

class InstrumentLabelSet {
 public:
  InstrumentLabelSet() = default;
  InstrumentLabelSet(std::initializer_list<absl::string_view> labels) {
    for (const auto& label : labels) {
      set_.set(InstrumentLabel(label).index());
    }
  }

  void Set(InstrumentLabel label) { set_.set(label.index()); }
  bool empty() const { return set_.none(); }
  bool contains(InstrumentLabel label) const {
    return set_.is_set(label.index());
  }
  void Merge(InstrumentLabelSet other) { set_.Merge(other.set_); }
  InstrumentLabelList ToList() const;

 private:
  BitSet<InstrumentLabel::kMaxLabelsPerProcess> set_;
};

class InstrumentLabelList {
 public:
  InstrumentLabelList() = default;
  InstrumentLabelList(std::initializer_list<absl::string_view> labels) {
    for (const auto& label : labels) {
      Append(InstrumentLabel(label));
    }
  }

  void Append(InstrumentLabel label) {
    GRPC_DCHECK_LT(count_, InstrumentLabel::kMaxLabelsPerProcess);
    labels_[count_++] = label;
  }

  bool empty() const { return count_ == 0; }
  size_t size() const { return count_; }

  InstrumentLabel operator[](size_t i) const {
    DCHECK_LT(i, count_);
    return labels_[i];
  }

  InstrumentLabelList Remove(InstrumentLabelSet labels);

  const InstrumentLabel* begin() const { return labels_; }
  const InstrumentLabel* end() const { return labels_ + count_; }

  std::string DebugString() const;

 private:
  uint8_t count_ = 0;
  InstrumentLabel labels_[InstrumentLabel::kMaxLabelsPerProcess];
};

template <size_t kNumLabels>
class FixedInstrumentLabelList {
 public:
  template <typename... Args>
  explicit FixedInstrumentLabelList(Args&&... args)
      : labels_{InstrumentLabel(std::forward<Args>(args))...} {
    static_assert(kNumLabels == sizeof...(args));
  }

  InstrumentLabel operator[](size_t i) const {
    CHECK_LT(i, kNumLabels);
    return labels_[i];
  }

  static constexpr size_t count() { return kNumLabels; }

  InstrumentLabelList ToList() const {
    InstrumentLabelList list;
    for (size_t i = 0; i < kNumLabels; ++i) {
      list.Append(labels_[i]);
    }
    return list;
  }

 private:
  InstrumentLabel labels_[kNumLabels];
};

template <>
class FixedInstrumentLabelList<0> {
 public:
  explicit FixedInstrumentLabelList() {}

  InstrumentLabel operator[](size_t i) const {
    LOG(FATAL) << "Index out of bounds: " << i << " for label list of size 0";
  }

  static constexpr size_t count() { return 0; }

  InstrumentLabelList ToList() const { return InstrumentLabelList(); }
};

class CollectionScope;

class InstrumentMetadata {
 public:
  struct CounterShape {};
  struct UpDownCounterShape {};
  struct DoubleGaugeShape {};
  struct IntGaugeShape {};
  struct UintGaugeShape {};
  using HistogramShape = HistogramBuckets;

  using Shape = std::variant<CounterShape, UpDownCounterShape, HistogramShape,
                             DoubleGaugeShape, IntGaugeShape, UintGaugeShape>;

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

// Defines a scope for collecting metrics, identified by a set of labels of
// interest. Metric collection via GetStorage+Increment will be filtered
// according to these labels. Scopes can be hierarchical. On destruction,
// metrics collected in this scope are aggregated into the parent scope.
class CollectionScope : public RefCounted<CollectionScope> {
 public:
  CollectionScope(std::vector<RefCountedPtr<CollectionScope>> parents,
                  InstrumentLabelSet labels_of_interest,
                  size_t child_shards_count, size_t storage_shards_count);
  ~CollectionScope() override;

  size_t TestOnlyCountStorageHeld() const;

  void ForEachUniqueStorage(
      absl::FunctionRef<void(instrument_detail::DomainStorage*)> cb);

  bool ObservesLabel(InstrumentLabel label) const {
    return labels_of_interest_.contains(label);
  }

  bool IsRoot() const { return parents_.empty(); }

 private:
  friend class GlobalCollectionScopeManager;
  friend class MetricsQuery;
  friend class instrument_detail::QueryableDomain;

  struct StorageShard {
    mutable Mutex mu;
    absl::flat_hash_map<std::pair<instrument_detail::QueryableDomain*,
                                  std::vector<std::string>>,
                        RefCountedPtr<instrument_detail::DomainStorage>>
        storage ABSL_GUARDED_BY(mu);
  };

  struct ChildShard {
    Mutex mu;
    absl::flat_hash_set<CollectionScope*> children ABSL_GUARDED_BY(mu);
  };

  ChildShard& child_shard(CollectionScope* child) {
    return child_shards_[absl::HashOf(child) % child_shards_.size()];
  }

  std::vector<RefCountedPtr<CollectionScope>> parents_;
  InstrumentLabelSet labels_of_interest_;
  std::vector<ChildShard> child_shards_;
  std::vector<StorageShard> storage_shards_;

  void ForEachUniqueStorage(
      absl::FunctionRef<void(instrument_detail::DomainStorage*)> cb,
      absl::flat_hash_set<instrument_detail::DomainStorage*>& visited);

  void TestOnlyReset();
};

namespace instrument_detail {

void CallHistogramCollectionHooks(
    const InstrumentMetadata::Description* instrument,
    absl::Span<const std::string> labels, int64_t value);

class GaugeStorage {
 public:
  explicit GaugeStorage(QueryableDomain* domain);

  void SetDouble(uint64_t offset, double value) {
    GRPC_DCHECK_LT(offset, double_gauges_.size());
    double_gauges_[offset] = value;
  }
  void SetInt(uint64_t offset, int64_t value) {
    GRPC_DCHECK_LT(offset, int_gauges_.size());
    int_gauges_[offset] = value;
  }
  void SetUint(uint64_t offset, uint64_t value) {
    GRPC_DCHECK_LT(offset, uint_gauges_.size());
    uint_gauges_[offset] = value;
  }

  std::optional<double> GetDouble(uint64_t offset) const {
    GRPC_DCHECK_LT(offset, double_gauges_.size());
    return double_gauges_[offset];
  }
  std::optional<int64_t> GetInt(uint64_t offset) const {
    GRPC_DCHECK_LT(offset, int_gauges_.size());
    return int_gauges_[offset];
  }
  std::optional<uint64_t> GetUint(uint64_t offset) const {
    GRPC_DCHECK_LT(offset, uint_gauges_.size());
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
  virtual void Add(DomainStorage* other) = 0;
  virtual void FillGaugeStorage(GaugeStorage& gauge_storage) = 0;

  // Returns the label values of the CollectionScope that owns this storage.
  // This is the full set of labels published by the domain, with unused labels
  // in the scope set to kOmittedLabel.
  absl::Span<const std::string> label() const { return label_; }
  QueryableDomain* domain() const { return domain_; }

  void AddData(channelz::DataSink sink) override;

 private:
  QueryableDomain* domain_;
  const std::vector<std::string> label_;
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
  InstrumentLabelList label_names() const { return label_names_; }

  // Reset the internal state of all domains. For test use only.
  static void TestOnlyResetAll();
  // Reset the internal state of this domain. For test use only.
  void TestOnlyReset();

  size_t TestOnlyCountStorageHeld() const;

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

  RefCountedPtr<DomainStorage> GetDomainStorage(
      RefCountedPtr<CollectionScope> scope,
      absl::Span<const std::string> label);

  absl::string_view name() const { return name_; }

  RefCountedPtr<channelz::BaseNode> channelz_node() {
    if (!channelz_.is_set()) {
      return channelz_.Set(new ChannelzState(this))->channelz_node();
    }
    return channelz_->channelz_node();
  }

 protected:
  QueryableDomain(std::string name, InstrumentLabelList label_names,
                  size_t map_shards_size)
      : label_names_(label_names),
        map_shards_size_(label_names_.empty() ? 1 : map_shards_size),
        map_shards_(std::make_unique<MapShard[]>(map_shards_size_)),
        name_(std::move(name)) {}

  // QueryableDomain should never be destroyed.
  ~QueryableDomain() { LOG(FATAL) << "QueryableDomain destroyed."; }

  // Called by InstrumentDomain when construction is complete.
  void Constructed();

  // Allocates a counter with the given name, description, and unit.
  const InstrumentMetadata::Description* AllocateCounter(
      absl::string_view name, absl::string_view description,
      absl::string_view unit);
  const InstrumentMetadata::Description* AllocateUpDownCounter(
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

 private:
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

  const InstrumentLabelList label_names_;
  std::vector<const InstrumentMetadata::Description*> metrics_;
  uint64_t allocated_counter_slots_ = 0;
  uint64_t allocated_double_gauge_slots_ = 0;
  uint64_t allocated_int_gauge_slots_ = 0;
  uint64_t allocated_uint_gauge_slots_ = 0;

  const size_t map_shards_size_;
  std::unique_ptr<MapShard[]> map_shards_;

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
// instrument domain. It has a Shape (how the metric behaves).
template <typename Shape, typename Domain>
class InstrumentHandle {
 public:
  absl::string_view name() const { return description_->name; }
  absl::string_view description() const { return description_->description; }
  absl::string_view unit() const { return description_->unit; }
  uint64_t offset() const { return offset_; }

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

}  // namespace instrument_detail

// A domain backend for low contention domains.
// We use a simple array of atomics to back the collection - each increment
// is a relaxed add.
class LowContentionBackend final {
 public:
  explicit LowContentionBackend(size_t size);

  void Add(size_t index, uint64_t amount) {
    counters_[index].fetch_add(amount, std::memory_order_relaxed);
  }
  void Subtract(size_t index, uint64_t amount) {
    uint64_t old_value =
        counters_[index].fetch_sub(amount, std::memory_order_relaxed);
    // Every decrement should have a corresponding increment.
    GRPC_DCHECK(old_value >= amount);
  }
  void Increment(size_t index) { Add(index, 1); }
  void Decrement(size_t index) { Subtract(index, 1); }

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

  void Add(size_t index, uint64_t amount) {
    counters_.this_cpu()[index].fetch_add(amount, std::memory_order_relaxed);
  }
  void Subtract(size_t index, uint64_t amount) {
    counters_.this_cpu()[index].fetch_sub(amount, std::memory_order_relaxed);
  }
  void Increment(size_t index) { Add(index, 1); }
  void Decrement(size_t index) { Subtract(index, 1); }

  uint64_t Sum(size_t index);

 private:
  // Since Increments and Decrements can happen on different CPUs, we need to
  // use a int64_t counter. The sum should still be a uint64_t.
  PerCpu<std::unique_ptr<std::atomic<int64_t>[]>> counters_{
      PerCpuOptions().SetMaxShards(16)};
};

// MetricsSink is an interface for accumulating metrics.
// Importantly it's the output interface for MetricsQuery.
class MetricsSink {
 public:
  // Called once per label per metric, with the value of that metric for that
  // label.
  virtual void Counter(InstrumentLabelList label_keys,
                       absl::Span<const std::string> label_values,
                       absl::string_view name, uint64_t value) = 0;
  virtual void UpDownCounter(InstrumentLabelList label_keys,
                             absl::Span<const std::string> label_values,
                             absl::string_view name, uint64_t value) = 0;
  virtual void Histogram(InstrumentLabelList label_keys,
                         absl::Span<const std::string> label_values,
                         absl::string_view name, HistogramBuckets bounds,
                         absl::Span<const uint64_t> counts) = 0;
  virtual void DoubleGauge(InstrumentLabelList label_keys,
                           absl::Span<const std::string> label_values,
                           absl::string_view name, double value) = 0;
  virtual void IntGauge(InstrumentLabelList label_keys,
                        absl::Span<const std::string> label_values,
                        absl::string_view name, int64_t value) = 0;
  virtual void UintGauge(InstrumentLabelList label_keys,
                         absl::Span<const std::string> label_values,
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
  MetricsQuery& CollapseLabels(absl::Span<const InstrumentLabel> labels);
  // Only include metrics that are in `metrics`.
  MetricsQuery& OnlyMetrics(std::vector<std::string> metrics);

  // Returns the metrics that are selected by this query.
  std::optional<absl::Span<const std::string>> selected_metrics() const {
    return only_metrics_;
  }

  // Runs the query, outputting the results to `sink`.
  void Run(RefCountedPtr<CollectionScope> scope, MetricsSink& sink) const;

 private:
  // Adapts `sink` by including the filtering requested, and then calls `fn`
  // with the filtering sink. This is mainly an implementation detail.
  void Apply(InstrumentLabelList label_names,
             absl::FunctionRef<void(MetricsSink&)> fn, MetricsSink& sink) const;

  void ApplyLabelChecks(InstrumentLabelList label_names,
                        absl::FunctionRef<void(MetricsSink&)> fn,
                        MetricsSink& sink) const;

  absl::flat_hash_map<InstrumentLabel, std::string> label_eqs_;
  std::optional<std::vector<std::string>> only_metrics_;
  InstrumentLabelSet collapsed_labels_;
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
  using UpDownCounterHandle =
      InstrumentHandle<InstrumentMetadata::UpDownCounterShape, Self>;
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
      GRPC_DCHECK(storage_ != nullptr);
    }
    ~GaugeProvider() { GRPC_DCHECK(storage_ == nullptr); }

    void ProviderConstructed() {
      GRPC_DCHECK(storage_ != nullptr);
      storage_->RegisterGaugeProvider(this);
    }
    void ProviderDestructing() {
      GRPC_DCHECK(storage_ != nullptr);
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
    void Increment(CounterHandle handle, uint64_t amount = 1) {
      GRPC_DCHECK_EQ(handle.instrument_domain_, domain());
      backend_.Add(handle.offset_, amount);
    }

    void Increment(UpDownCounterHandle handle, uint64_t amount = 1) {
      GRPC_DCHECK_EQ(handle.instrument_domain_, domain());
      backend_.Add(handle.offset_, amount);
    }

    void Decrement(UpDownCounterHandle handle, uint64_t amount = 1) {
      GRPC_DCHECK_EQ(handle.instrument_domain_, domain());
      backend_.Subtract(handle.offset_, amount);
    }

    void Add(DomainStorage* other) override {
      GRPC_DCHECK_EQ(domain(), other->domain());
      for (size_t i = 0; i < domain()->allocated_counter_slots(); ++i) {
        uint64_t amount = other->SumCounter(i);
        if (amount == 0) continue;
        backend_.Add(i, amount);
      }
    }

    template <typename Shape>
    void Increment(const HistogramHandle<Shape>& handle, int64_t value) {
      GRPC_DCHECK_EQ(handle.instrument_domain_, domain());
      CallHistogramCollectionHooks(handle.description_, label(), value);
      backend_.Add(handle.offset_ + handle.shape_->BucketFor(value), 1);
    }

    uint64_t SumCounter(size_t offset) override { return backend_.Sum(offset); }
    void FillGaugeStorage(GaugeStorage& storage) override {
      GaugeSink sink(storage);
      MutexLock lock(&gauge_providers_mu_);
      for (auto* provider : gauge_providers_) {
        provider->PopulateGaugeData(sink);
      }
    }

   private:
    friend class InstrumentDomainImpl<Backend, N, Tag>;
    friend class GaugeProvider;

    explicit Storage(InstrumentDomainImpl* instrument_domain,
                     std::vector<std::string> labels)
        : DomainStorage(instrument_domain, std::move(labels)),
          backend_(instrument_domain->allocated_counter_slots()) {}

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

    Backend backend_;
    Mutex gauge_providers_mu_;
    std::vector<GaugeProvider*> gauge_providers_
        ABSL_GUARDED_BY(gauge_providers_mu_);
  };

  GPR_ATTRIBUTE_NOINLINE explicit InstrumentDomainImpl(
      std::string name, FixedInstrumentLabelList<N> labels,
      size_t map_shards = std::min(16u, gpr_cpu_num_cores()))
      : QueryableDomain(std::move(name), labels.ToList(), map_shards) {
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

  UpDownCounterHandle RegisterUpDownCounter(absl::string_view name,
                                            absl::string_view description,
                                            absl::string_view unit) {
    return UpDownCounterHandle{this,
                               AllocateUpDownCounter(name, description, unit),
                               InstrumentMetadata::UpDownCounterShape{}};
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
  RefCountedPtr<Storage> GetStorage(RefCountedPtr<CollectionScope> scope,
                                    Args&&... labels) {
    static_assert(sizeof...(Args) == N, "Incorrect number of labels provided");
    std::vector<std::string> label_values;
    label_values.reserve(N);
    (label_values.emplace_back(absl::StrCat(labels)), ...);
    return DownCastRefCountedPtr<Storage>(
        GetDomainStorage(std::move(scope), label_values));
  }

  RefCountedPtr<DomainStorage> CreateDomainStorage(
      std::vector<std::string> labels) override {
    return RefCountedPtr<Storage>(new Storage(this, std::move(labels)));
  }

 private:
  ~InstrumentDomainImpl() = delete;
};

}  // namespace instrument_detail

template <class Derived>
class InstrumentDomain {
 public:
  static auto* Domain() {
    static const auto labels = Derived::Labels();
    static auto* domain =
        new instrument_detail::InstrumentDomainImpl<typename Derived::Backend,
                                                    labels.count(), Derived>(
            absl::StrCat(Derived::kName), labels);
    for (size_t i = 0; i < labels.count(); ++i) {
      GRPC_DCHECK_EQ(domain->label_names()[i], labels[i]);
    }
    return domain;
  }

  // Returns an InstrumentStorageRefPtr<Derived>.
  template <typename... Args>
  static auto GetStorage(RefCountedPtr<CollectionScope> scope,
                         Args&&... labels) {
    return Domain()->GetStorage(std::move(scope),
                                std::forward<Args>(labels)...);
  }

 protected:
  template <typename... Label>
  static FixedInstrumentLabelList<sizeof...(Label)> MakeLabels(
      Label... labels) {
    InstrumentLabel l[] = {InstrumentLabel(labels)...};
    for (size_t i = 0; i < sizeof...(Label); ++i) {
      for (size_t j = i + 1; j < sizeof...(Label); ++j) {
        GRPC_CHECK_NE(l[i], l[j]);
      }
    }
    auto list = FixedInstrumentLabelList<sizeof...(Label)>(
        std::forward<Label>(labels)...);
    const std::vector<std::string> label_names{std::string(labels)...};
    for (size_t i = 0; i < sizeof...(Label); ++i) {
      CHECK_EQ(label_names[i], list[i].label());
      for (size_t j = i + 1; j < sizeof...(Label); ++j) {
        GRPC_CHECK_NE(list[i], list[j]);
      }
    }
    return list;
  }

  static auto RegisterCounter(absl::string_view name,
                              absl::string_view description,
                              absl::string_view unit) {
    return Domain()->RegisterCounter(name, description, unit);
  }

  static auto RegisterUpDownCounter(absl::string_view name,
                                    absl::string_view description,
                                    absl::string_view unit) {
    return Domain()->RegisterUpDownCounter(name, description, unit);
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

// Create a new collection scope.
// `parent` is the parent scope, or nullptr for a root scope.
// `labels` is a list of labels that this scope is interested in. The scope's
// labels of interest will be the union of its own labels and its parent's
// labels.
// `child_shards_count` and `storage_shards_count` are performance tuning
// parameters for sharding internal data structures.
RefCountedPtr<CollectionScope> CreateCollectionScope(
    std::vector<RefCountedPtr<CollectionScope>> parents,
    InstrumentLabelSet labels, size_t child_shards_count = 1,
    size_t storage_shards_count = 1);

RefCountedPtr<CollectionScope> CreateRootCollectionScope(
    InstrumentLabelSet labels, size_t child_shards_count = 1,
    size_t storage_shards_count = 1);

RefCountedPtr<CollectionScope> GlobalCollectionScope();

}  // namespace grpc_core

#define GRPC_INSTRUMENT_DOMAIN_LABELS_NUM_LABELS(...) \
  (std::tuple_size<decltype(std::tuple(__VA_ARGS__))>::value)

#define GRPC_INSTRUMENT_DOMAIN_LABELS(...)                   \
  static grpc_core::FixedInstrumentLabelList<                \
      GRPC_INSTRUMENT_DOMAIN_LABELS_NUM_LABELS(__VA_ARGS__)> \
  Labels() {                                                 \
    return MakeLabels(__VA_ARGS__);                          \
  }

#endif  // GRPC_SRC_CORE_TELEMETRY_INSTRUMENT_H
