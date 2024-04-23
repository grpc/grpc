// Copyright 2023 The gRPC Authors.
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

#ifndef GRPC_TEST_CORE_UTIL_FAKE_STATS_PLUGIN_H
#define GRPC_TEST_CORE_UTIL_FAKE_STATS_PLUGIN_H

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/metrics.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/channel/tcp_tracer.h"
#include "src/core/lib/gprpp/ref_counted.h"

namespace grpc_core {

// Registers a FakeStatsClientFilter as a client channel filter if there is a
// FakeClientCallTracerFactory in the channel args. This filter will use the
// FakeClientCallTracerFactory to create and inject a FakeClientCallTracer into
// the call context.
// Example usage:
//   RegisterFakeStatsPlugin();  // before grpc_init()
//
//   // Creates a FakeClientCallTracerFactory and adds it into the channel args.
//   FakeClientCallTracerFactory fake_client_call_tracer_factory;
//   ChannelArguments channel_args;
//   channel_args.SetPointer(GRPC_ARG_INJECT_FAKE_CLIENT_CALL_TRACER_FACTORY,
//                           &fake_client_call_tracer_factory);
//
//   // After the system under test has been executed (e.g. an RPC has been
//   // sent), use the FakeClientCallTracerFactory to verify certain
//   // expectations.
//   EXPECT_THAT(fake_client_call_tracer_factory.GetLastFakeClientCallTracer()
//                   ->GetLastCallAttemptTracer()
//                   ->GetOptionalLabels(),
//               VerifyCsmServiceLabels());
void RegisterFakeStatsPlugin();

class FakeClientCallTracer : public ClientCallTracer {
 public:
  class FakeClientCallAttemptTracer
      : public ClientCallTracer::CallAttemptTracer,
        public RefCounted<FakeClientCallAttemptTracer> {
   public:
    explicit FakeClientCallAttemptTracer(
        std::vector<std::string>* annotation_logger)
        : annotation_logger_(annotation_logger) {}
    void RecordSendInitialMetadata(
        grpc_metadata_batch* /*send_initial_metadata*/) override {}
    void RecordSendTrailingMetadata(
        grpc_metadata_batch* /*send_trailing_metadata*/) override {}
    void RecordSendMessage(const SliceBuffer& /*send_message*/) override {}
    void RecordSendCompressedMessage(
        const SliceBuffer& /*send_compressed_message*/) override {}
    void RecordReceivedInitialMetadata(
        grpc_metadata_batch* /*recv_initial_metadata*/) override {}
    void RecordReceivedMessage(const SliceBuffer& /*recv_message*/) override {}
    void RecordReceivedDecompressedMessage(
        const SliceBuffer& /*recv_decompressed_message*/) override {}
    void RecordCancel(grpc_error_handle /*cancel_error*/) override {}
    void RecordReceivedTrailingMetadata(
        absl::Status /*status*/,
        grpc_metadata_batch* /*recv_trailing_metadata*/,
        const grpc_transport_stream_stats* /*transport_stream_stats*/)
        override {}
    void RecordEnd(const gpr_timespec& /*latency*/) override { Unref(); }
    void RecordAnnotation(absl::string_view annotation) override {
      annotation_logger_->push_back(std::string(annotation));
    }
    void RecordAnnotation(const Annotation& /*annotation*/) override {}
    std::shared_ptr<TcpTracerInterface> StartNewTcpTrace() override {
      return nullptr;
    }
    void SetOptionalLabel(OptionalLabelKey key,
                          RefCountedStringValue value) override {
      optional_labels_.emplace(key, std::move(value));
    }
    std::string TraceId() override { return ""; }
    std::string SpanId() override { return ""; }
    bool IsSampled() override { return false; }

    const std::map<OptionalLabelKey, RefCountedStringValue>& GetOptionalLabels()
        const {
      return optional_labels_;
    }

   private:
    std::vector<std::string>* annotation_logger_;
    std::map<OptionalLabelKey, RefCountedStringValue> optional_labels_;
  };

  explicit FakeClientCallTracer(std::vector<std::string>* annotation_logger)
      : annotation_logger_(annotation_logger) {}
  ~FakeClientCallTracer() override {}
  CallAttemptTracer* StartNewAttempt(bool /*is_transparent_retry*/) override {
    auto call_attempt_tracer =
        MakeRefCounted<FakeClientCallAttemptTracer>(annotation_logger_);
    call_attempt_tracers_.emplace_back(call_attempt_tracer);
    return call_attempt_tracer.release();  // Released in RecordEnd().
  }

  void RecordAnnotation(absl::string_view annotation) override {
    annotation_logger_->push_back(std::string(annotation));
  }
  void RecordAnnotation(const Annotation& /*annotation*/) override {}
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

  FakeClientCallAttemptTracer* GetLastCallAttemptTracer() const {
    return call_attempt_tracers_.back().get();
  }

 private:
  std::vector<std::string>* annotation_logger_;
  std::vector<RefCountedPtr<FakeClientCallAttemptTracer>> call_attempt_tracers_;
};

#define GRPC_ARG_INJECT_FAKE_CLIENT_CALL_TRACER_FACTORY \
  "grpc.testing.inject_fake_client_call_tracer_factory"

class FakeClientCallTracerFactory {
 public:
  FakeClientCallTracer* CreateFakeClientCallTracer() {
    fake_client_call_tracers_.emplace_back(
        new FakeClientCallTracer(&annotation_logger_));
    return fake_client_call_tracers_.back().get();
  }

  FakeClientCallTracer* GetLastFakeClientCallTracer() {
    return fake_client_call_tracers_.back().get();
  }

 private:
  std::vector<std::string> annotation_logger_;
  std::vector<std::unique_ptr<FakeClientCallTracer>> fake_client_call_tracers_;
};

class FakeServerCallTracer : public ServerCallTracer {
 public:
  explicit FakeServerCallTracer(std::vector<std::string>* annotation_logger)
      : annotation_logger_(annotation_logger) {}
  ~FakeServerCallTracer() override {}
  void RecordSendInitialMetadata(
      grpc_metadata_batch* /*send_initial_metadata*/) override {}
  void RecordSendTrailingMetadata(
      grpc_metadata_batch* /*send_trailing_metadata*/) override {}
  void RecordSendMessage(const SliceBuffer& /*send_message*/) override {}
  void RecordSendCompressedMessage(
      const SliceBuffer& /*send_compressed_message*/) override {}
  void RecordReceivedInitialMetadata(
      grpc_metadata_batch* /*recv_initial_metadata*/) override {}
  void RecordReceivedMessage(const SliceBuffer& /*recv_message*/) override {}
  void RecordReceivedDecompressedMessage(
      const SliceBuffer& /*recv_decompressed_message*/) override {}
  void RecordCancel(grpc_error_handle /*cancel_error*/) override {}
  void RecordReceivedTrailingMetadata(
      grpc_metadata_batch* /*recv_trailing_metadata*/) override {}
  void RecordEnd(const grpc_call_final_info* /*final_info*/) override {}
  void RecordAnnotation(absl::string_view annotation) override {
    annotation_logger_->push_back(std::string(annotation));
  }
  void RecordAnnotation(const Annotation& /*annotation*/) override {}
  std::shared_ptr<TcpTracerInterface> StartNewTcpTrace() override {
    return nullptr;
  }
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

 private:
  std::vector<std::string>* annotation_logger_;
};

std::string MakeLabelString(
    absl::Span<const absl::string_view> label_keys,
    absl::Span<const absl::string_view> label_values,
    absl::Span<const absl::string_view> optional_label_keys,
    absl::Span<const absl::string_view> optional_values);

class FakeStatsPlugin : public StatsPlugin {
 public:
  class ScopeConfig : public StatsPlugin::ScopeConfig {};

  explicit FakeStatsPlugin(
      absl::AnyInvocable<
          bool(const experimental::StatsPluginChannelScope& /*scope*/) const>
          channel_filter = nullptr,
      bool use_disabled_by_default_metrics = false)
      : channel_filter_(std::move(channel_filter)) {
    GlobalInstrumentsRegistry::ForEach(
        [&](const GlobalInstrumentsRegistry::GlobalInstrumentDescriptor&
                descriptor) {
          if (!use_disabled_by_default_metrics &&
              !descriptor.enable_by_default) {
            gpr_log(GPR_INFO,
                    "FakeStatsPlugin[%p]: skipping disabled metric: %s", this,
                    std::string(descriptor.name).c_str());
            return;
          }
          switch (descriptor.instrument_type) {
            case GlobalInstrumentsRegistry::InstrumentType::kCounter: {
              MutexLock lock(&mu_);
              if (descriptor.value_type ==
                  GlobalInstrumentsRegistry::ValueType::kUInt64) {
                uint64_counters_.emplace(descriptor.index, descriptor);
              } else {
                double_counters_.emplace(descriptor.index, descriptor);
              }
              break;
            }
            case GlobalInstrumentsRegistry::InstrumentType::kHistogram: {
              MutexLock lock(&mu_);
              if (descriptor.value_type ==
                  GlobalInstrumentsRegistry::ValueType::kUInt64) {
                uint64_histograms_.emplace(descriptor.index, descriptor);
              } else {
                double_histograms_.emplace(descriptor.index, descriptor);
              }
              break;
            }
            case GlobalInstrumentsRegistry::InstrumentType::kCallbackGauge: {
              MutexLock lock(&callback_mu_);
              if (descriptor.value_type ==
                  GlobalInstrumentsRegistry::ValueType::kInt64) {
                int64_callback_gauges_.emplace(descriptor.index, descriptor);
              } else {
                double_callback_gauges_.emplace(descriptor.index, descriptor);
              }
              break;
            }
            default:
              Crash("unknown instrument type");
          }
        });
  }

  std::pair<bool, std::shared_ptr<StatsPlugin::ScopeConfig>>
  IsEnabledForChannel(
      const experimental::StatsPluginChannelScope& scope) const override {
    if (channel_filter_ == nullptr || channel_filter_(scope)) {
      return {true, nullptr};
    }
    return {false, nullptr};
  }
  std::pair<bool, std::shared_ptr<StatsPlugin::ScopeConfig>> IsEnabledForServer(
      const ChannelArgs& /*args*/) const override {
    return {true, nullptr};
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
    gpr_log(GPR_INFO,
            "FakeStatsPlugin[%p]::AddCounter(index=%u, value=(uint64)%lu, "
            "label_values={%s}, optional_label_values={%s}",
            this, handle.index, value,
            absl::StrJoin(label_values, ", ").c_str(),
            absl::StrJoin(optional_values, ", ").c_str());
    MutexLock lock(&mu_);
    auto iter = uint64_counters_.find(handle.index);
    if (iter == uint64_counters_.end()) return;
    iter->second.Add(value, label_values, optional_values);
  }
  void AddCounter(
      GlobalInstrumentsRegistry::GlobalDoubleCounterHandle handle, double value,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override {
    gpr_log(GPR_INFO,
            "FakeStatsPlugin[%p]::AddCounter(index=%u, value(double)=%f, "
            "label_values={%s}, optional_label_values={%s}",
            this, handle.index, value,
            absl::StrJoin(label_values, ", ").c_str(),
            absl::StrJoin(optional_values, ", ").c_str());
    MutexLock lock(&mu_);
    auto iter = double_counters_.find(handle.index);
    if (iter == double_counters_.end()) return;
    iter->second.Add(value, label_values, optional_values);
  }
  void RecordHistogram(
      GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle handle,
      uint64_t value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override {
    gpr_log(GPR_INFO,
            "FakeStatsPlugin[%p]::RecordHistogram(index=%u, value=(uint64)%lu, "
            "label_values={%s}, optional_label_values={%s}",
            this, handle.index, value,
            absl::StrJoin(label_values, ", ").c_str(),
            absl::StrJoin(optional_values, ", ").c_str());
    MutexLock lock(&mu_);
    auto iter = uint64_histograms_.find(handle.index);
    if (iter == uint64_histograms_.end()) return;
    iter->second.Record(value, label_values, optional_values);
  }
  void RecordHistogram(
      GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle handle,
      double value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override {
    gpr_log(GPR_INFO,
            "FakeStatsPlugin[%p]::RecordHistogram(index=%u, value=(double)%f, "
            "label_values={%s}, optional_label_values={%s}",
            this, handle.index, value,
            absl::StrJoin(label_values, ", ").c_str(),
            absl::StrJoin(optional_values, ", ").c_str());
    MutexLock lock(&mu_);
    auto iter = double_histograms_.find(handle.index);
    if (iter == double_histograms_.end()) return;
    iter->second.Record(value, label_values, optional_values);
  }
  void AddCallback(RegisteredMetricCallback* callback) override {
    gpr_log(GPR_INFO, "FakeStatsPlugin[%p]::AddCallback(%p)", this, callback);
    callbacks_.insert(callback);
  }
  void RemoveCallback(RegisteredMetricCallback* callback) override {
    gpr_log(GPR_INFO, "FakeStatsPlugin[%p]::RemoveCallback(%p)", this,
            callback);
    callbacks_.erase(callback);
  }

  ClientCallTracer* GetClientCallTracer(
      const Slice& /*path*/, bool /*registered_method*/,
      std::shared_ptr<StatsPlugin::ScopeConfig> /*scope_config*/) override {
    return nullptr;
  }
  ServerCallTracer* GetServerCallTracer(
      std::shared_ptr<StatsPlugin::ScopeConfig> /*scope_config*/) override {
    return nullptr;
  }

  absl::optional<uint64_t> GetCounterValue(
      GlobalInstrumentsRegistry::GlobalUInt64CounterHandle handle,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) {
    MutexLock lock(&mu_);
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
    MutexLock lock(&mu_);
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
    MutexLock lock(&mu_);
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
    MutexLock lock(&mu_);
    auto iter = double_histograms_.find(handle.index);
    if (iter == double_histograms_.end()) {
      return absl::nullopt;
    }
    return iter->second.GetValues(label_values, optional_values);
  }
  void TriggerCallbacks() {
    gpr_log(GPR_INFO, "FakeStatsPlugin[%p]::TriggerCallbacks(): START", this);
    Reporter reporter(*this);
    for (auto* callback : callbacks_) {
      callback->Run(reporter);
    }
    gpr_log(GPR_INFO, "FakeStatsPlugin[%p]::TriggerCallbacks(): END", this);
  }
  absl::optional<int64_t> GetCallbackGaugeValue(
      GlobalInstrumentsRegistry::GlobalCallbackInt64GaugeHandle handle,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) {
    MutexLock lock(&callback_mu_);
    auto iter = int64_callback_gauges_.find(handle.index);
    if (iter == int64_callback_gauges_.end()) {
      return absl::nullopt;
    }
    return iter->second.GetValue(label_values, optional_values);
  }
  absl::optional<double> GetCallbackGaugeValue(
      GlobalInstrumentsRegistry::GlobalCallbackDoubleGaugeHandle handle,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) {
    MutexLock lock(&callback_mu_);
    auto iter = double_callback_gauges_.find(handle.index);
    if (iter == double_callback_gauges_.end()) {
      return absl::nullopt;
    }
    return iter->second.GetValue(label_values, optional_values);
  }

 private:
  class Reporter : public CallbackMetricReporter {
   public:
    explicit Reporter(FakeStatsPlugin& plugin) : plugin_(plugin) {}

    void Report(
        GlobalInstrumentsRegistry::GlobalCallbackInt64GaugeHandle handle,
        int64_t value, absl::Span<const absl::string_view> label_values,
        absl::Span<const absl::string_view> optional_values) override {
      gpr_log(GPR_INFO,
              "FakeStatsPlugin[%p]::Reporter::Report(index=%u, "
              "value=(uint64)%ld, label_values={%s}, "
              "optional_label_values={%s}",
              this, handle.index, value,
              absl::StrJoin(label_values, ", ").c_str(),
              absl::StrJoin(optional_values, ", ").c_str());
      MutexLock lock(&plugin_.callback_mu_);
      auto iter = plugin_.int64_callback_gauges_.find(handle.index);
      if (iter == plugin_.int64_callback_gauges_.end()) return;
      iter->second.Set(value, label_values, optional_values);
    }

    void Report(
        GlobalInstrumentsRegistry::GlobalCallbackDoubleGaugeHandle handle,
        double value, absl::Span<const absl::string_view> label_values,
        absl::Span<const absl::string_view> optional_values) override {
      gpr_log(GPR_INFO,
              "FakeStatsPlugin[%p]::Reporter::Report(index=%u, "
              "value=(double)%f, label_values={%s}, "
              "optional_label_values={%s}",
              this, handle.index, value,
              absl::StrJoin(label_values, ", ").c_str(),
              absl::StrJoin(optional_values, ", ").c_str());
      MutexLock lock(&plugin_.callback_mu_);
      auto iter = plugin_.double_callback_gauges_.find(handle.index);
      if (iter == plugin_.double_callback_gauges_.end()) return;
      iter->second.Set(value, label_values, optional_values);
    }

   private:
    FakeStatsPlugin& plugin_;
  };

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

  template <class T>
  class Gauge {
   public:
    explicit Gauge(GlobalInstrumentsRegistry::GlobalInstrumentDescriptor u)
        : name_(u.name),
          description_(u.description),
          unit_(u.unit),
          label_keys_(std::move(u.label_keys)),
          optional_label_keys_(std::move(u.optional_label_keys)) {}

    void Set(T t, absl::Span<const absl::string_view> label_values,
             absl::Span<const absl::string_view> optional_values) {
      storage_[MakeLabelString(label_keys_, label_values, optional_label_keys_,
                               optional_values)] = t;
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
    absl::flat_hash_map<std::string, T> storage_;
  };

  absl::AnyInvocable<bool(
      const experimental::StatsPluginChannelScope& /*scope*/) const>
      channel_filter_;
  // Instruments.
  Mutex mu_;
  absl::flat_hash_map<uint32_t, Counter<uint64_t>> uint64_counters_
      ABSL_GUARDED_BY(&mu_);
  absl::flat_hash_map<uint32_t, Counter<double>> double_counters_
      ABSL_GUARDED_BY(&mu_);
  absl::flat_hash_map<uint32_t, Histogram<uint64_t>> uint64_histograms_
      ABSL_GUARDED_BY(&mu_);
  absl::flat_hash_map<uint32_t, Histogram<double>> double_histograms_
      ABSL_GUARDED_BY(&mu_);
  Mutex callback_mu_;
  absl::flat_hash_map<uint32_t, Gauge<int64_t>> int64_callback_gauges_
      ABSL_GUARDED_BY(&callback_mu_);
  absl::flat_hash_map<uint32_t, Gauge<double>> double_callback_gauges_
      ABSL_GUARDED_BY(&callback_mu_);
  std::set<RegisteredMetricCallback*> callbacks_;
};

class FakeStatsPluginBuilder {
 public:
  FakeStatsPluginBuilder& SetChannelFilter(
      absl::AnyInvocable<
          bool(const experimental::StatsPluginChannelScope& /*scope*/) const>
          channel_filter) {
    channel_filter_ = std::move(channel_filter);
    return *this;
  }

  FakeStatsPluginBuilder& UseDisabledByDefaultMetrics(bool value) {
    use_disabled_by_default_metrics_ = value;
    return *this;
  }

  std::shared_ptr<FakeStatsPlugin> BuildAndRegister() {
    auto f = std::make_shared<FakeStatsPlugin>(
        std::move(channel_filter_), use_disabled_by_default_metrics_);
    GlobalStatsPluginRegistry::RegisterStatsPlugin(f);
    return f;
  }

 private:
  absl::AnyInvocable<bool(
      const experimental::StatsPluginChannelScope& /*scope*/) const>
      channel_filter_;
  bool use_disabled_by_default_metrics_ = false;
};

std::shared_ptr<FakeStatsPlugin> MakeStatsPluginForTarget(
    absl::string_view target_suffix);

class GlobalInstrumentsRegistryTestPeer {
 public:
  static void ResetGlobalInstrumentsRegistry();

  static absl::optional<GlobalInstrumentsRegistry::GlobalUInt64CounterHandle>
  FindUInt64CounterHandleByName(absl::string_view name);
  static absl::optional<GlobalInstrumentsRegistry::GlobalDoubleCounterHandle>
  FindDoubleCounterHandleByName(absl::string_view name);
  static absl::optional<GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle>
  FindUInt64HistogramHandleByName(absl::string_view name);
  static absl::optional<GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle>
  FindDoubleHistogramHandleByName(absl::string_view name);
  static absl::optional<
      GlobalInstrumentsRegistry::GlobalCallbackInt64GaugeHandle>
  FindCallbackInt64GaugeHandleByName(absl::string_view name);
  static absl::optional<
      GlobalInstrumentsRegistry::GlobalCallbackDoubleGaugeHandle>
  FindCallbackDoubleGaugeHandleByName(absl::string_view name);

  static GlobalInstrumentsRegistry::GlobalInstrumentDescriptor*
  FindMetricDescriptorByName(absl::string_view name);
};

class GlobalStatsPluginRegistryTestPeer {
 public:
  static void ResetGlobalStatsPluginRegistry() {
    MutexLock lock(&*GlobalStatsPluginRegistry::mutex_);
    GlobalStatsPluginRegistry::plugins_->clear();
  }
};

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_UTIL_FAKE_STATS_PLUGIN_H
