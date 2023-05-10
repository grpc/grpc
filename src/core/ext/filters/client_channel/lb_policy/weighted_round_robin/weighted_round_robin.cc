//
// Copyright 2022 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"
#include "src/core/ext/filters/client_channel/lb_policy/oob_backend_metric.h"
#include "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h"
#include "src/core/ext/filters/client_channel/lb_policy/weighted_round_robin/static_stride_scheduler.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_lb_wrr_trace(false, "weighted_round_robin_lb");

namespace {

constexpr absl::string_view kWeightedRoundRobin = "weighted_round_robin";

// Config for WRR policy.
class WeightedRoundRobinConfig : public LoadBalancingPolicy::Config {
 public:
  WeightedRoundRobinConfig() = default;

  WeightedRoundRobinConfig(const WeightedRoundRobinConfig&) = delete;
  WeightedRoundRobinConfig& operator=(const WeightedRoundRobinConfig&) = delete;

  WeightedRoundRobinConfig(WeightedRoundRobinConfig&&) = delete;
  WeightedRoundRobinConfig& operator=(WeightedRoundRobinConfig&&) = delete;

  absl::string_view name() const override { return kWeightedRoundRobin; }

  bool enable_oob_load_report() const { return enable_oob_load_report_; }
  Duration oob_reporting_period() const { return oob_reporting_period_; }
  Duration blackout_period() const { return blackout_period_; }
  Duration weight_update_period() const { return weight_update_period_; }
  Duration weight_expiration_period() const {
    return weight_expiration_period_;
  }
  float error_utilization_penalty() const { return error_utilization_penalty_; }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader =
        JsonObjectLoader<WeightedRoundRobinConfig>()
            .OptionalField("enableOobLoadReport",
                           &WeightedRoundRobinConfig::enable_oob_load_report_)
            .OptionalField("oobReportingPeriod",
                           &WeightedRoundRobinConfig::oob_reporting_period_)
            .OptionalField("blackoutPeriod",
                           &WeightedRoundRobinConfig::blackout_period_)
            .OptionalField("weightUpdatePeriod",
                           &WeightedRoundRobinConfig::weight_update_period_)
            .OptionalField("weightExpirationPeriod",
                           &WeightedRoundRobinConfig::weight_expiration_period_)
            .OptionalField(
                "errorUtilizationPenalty",
                &WeightedRoundRobinConfig::error_utilization_penalty_)
            .Finish();
    return loader;
  }

  void JsonPostLoad(const Json&, const JsonArgs&, ValidationErrors* errors) {
    // Impose lower bound of 100ms on weightUpdatePeriod.
    weight_update_period_ =
        std::max(weight_update_period_, Duration::Milliseconds(100));
    if (error_utilization_penalty_ < 0) {
      ValidationErrors::ScopedField field(errors, ".errorUtilizationPenalty");
      errors->AddError("must be non-negative");
    }
  }

 private:
  bool enable_oob_load_report_ = false;
  Duration oob_reporting_period_ = Duration::Seconds(10);
  Duration blackout_period_ = Duration::Seconds(10);
  Duration weight_update_period_ = Duration::Seconds(1);
  Duration weight_expiration_period_ = Duration::Minutes(3);
  float error_utilization_penalty_ = 1.0;
};

// WRR LB policy.
class WeightedRoundRobin : public LoadBalancingPolicy {
 public:
  explicit WeightedRoundRobin(Args args);

  absl::string_view name() const override { return kWeightedRoundRobin; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  // Represents the weight for a given address.
  class AddressWeight : public RefCounted<AddressWeight> {
   public:
    AddressWeight(RefCountedPtr<WeightedRoundRobin> wrr, std::string key)
        : wrr_(std::move(wrr)), key_(std::move(key)) {}
    ~AddressWeight() override;

    void MaybeUpdateWeight(double qps, double eps, double cpu_utilization,
                           float error_utilization_penalty);

    float GetWeight(Timestamp now, Duration weight_expiration_period,
                    Duration blackout_period);

    void ResetNonEmptySince();

   private:
    RefCountedPtr<WeightedRoundRobin> wrr_;
    const std::string key_;

    Mutex mu_;
    float weight_ ABSL_GUARDED_BY(&mu_) = 0;
    Timestamp non_empty_since_ ABSL_GUARDED_BY(&mu_) = Timestamp::InfFuture();
    Timestamp last_update_time_ ABSL_GUARDED_BY(&mu_) = Timestamp::InfPast();
  };

  // Forward declaration.
  class WeightedRoundRobinSubchannelList;

  // Data for a particular subchannel in a subchannel list.
  // This subclass adds the following functionality:
  // - Tracks the previous connectivity state of the subchannel, so that
  //   we know how many subchannels are in each state.
  class WeightedRoundRobinSubchannelData
      : public SubchannelData<WeightedRoundRobinSubchannelList,
                              WeightedRoundRobinSubchannelData> {
   public:
    WeightedRoundRobinSubchannelData(
        SubchannelList<WeightedRoundRobinSubchannelList,
                       WeightedRoundRobinSubchannelData>* subchannel_list,
        const ServerAddress& address, RefCountedPtr<SubchannelInterface> sc);

    absl::optional<grpc_connectivity_state> connectivity_state() const {
      return logical_connectivity_state_;
    }

    RefCountedPtr<AddressWeight> weight() const { return weight_; }

   private:
    class OobWatcher : public OobBackendMetricWatcher {
     public:
      OobWatcher(RefCountedPtr<AddressWeight> weight,
                 float error_utilization_penalty)
          : weight_(std::move(weight)),
            error_utilization_penalty_(error_utilization_penalty) {}

      void OnBackendMetricReport(
          const BackendMetricData& backend_metric_data) override;

     private:
      RefCountedPtr<AddressWeight> weight_;
      const float error_utilization_penalty_;
    };

    // Performs connectivity state updates that need to be done only
    // after we have started watching.
    void ProcessConnectivityChangeLocked(
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state) override;

    // Updates the logical connectivity state.
    void UpdateLogicalConnectivityStateLocked(
        grpc_connectivity_state connectivity_state);

    // The logical connectivity state of the subchannel.
    // Note that the logical connectivity state may differ from the
    // actual reported state in some cases (e.g., after we see
    // TRANSIENT_FAILURE, we ignore any subsequent state changes until
    // we see READY).
    absl::optional<grpc_connectivity_state> logical_connectivity_state_;

    RefCountedPtr<AddressWeight> weight_;
  };

  // A list of subchannels.
  class WeightedRoundRobinSubchannelList
      : public SubchannelList<WeightedRoundRobinSubchannelList,
                              WeightedRoundRobinSubchannelData> {
   public:
    WeightedRoundRobinSubchannelList(WeightedRoundRobin* policy,
                                     ServerAddressList addresses,
                                     const ChannelArgs& args)
        : SubchannelList(policy,
                         (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)
                              ? "WeightedRoundRobinSubchannelList"
                              : nullptr),
                         std::move(addresses), policy->channel_control_helper(),
                         args) {
      // Need to maintain a ref to the LB policy as long as we maintain
      // any references to subchannels, since the subchannels'
      // pollset_sets will include the LB policy's pollset_set.
      policy->Ref(DEBUG_LOCATION, "subchannel_list").release();
    }

    ~WeightedRoundRobinSubchannelList() override {
      WeightedRoundRobin* p = static_cast<WeightedRoundRobin*>(policy());
      p->Unref(DEBUG_LOCATION, "subchannel_list");
    }

    // Updates the counters of subchannels in each state when a
    // subchannel transitions from old_state to new_state.
    void UpdateStateCountersLocked(
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state);

    // Ensures that the right subchannel list is used and then updates
    // the aggregated connectivity state based on the subchannel list's
    // state counters.
    void MaybeUpdateAggregatedConnectivityStateLocked(
        absl::Status status_for_tf);

   private:
    std::shared_ptr<WorkSerializer> work_serializer() const override {
      return static_cast<WeightedRoundRobin*>(policy())->work_serializer();
    }

    std::string CountersString() const {
      return absl::StrCat("num_subchannels=", num_subchannels(),
                          " num_ready=", num_ready_,
                          " num_connecting=", num_connecting_,
                          " num_transient_failure=", num_transient_failure_);
    }

    size_t num_ready_ = 0;
    size_t num_connecting_ = 0;
    size_t num_transient_failure_ = 0;

    absl::Status last_failure_;
  };

  // A picker that performs WRR picks with weights based on
  // endpoint-reported utilization and QPS.
  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<WeightedRoundRobin> wrr,
           WeightedRoundRobinSubchannelList* subchannel_list);

    ~Picker() override;

    PickResult Pick(PickArgs args) override;

    void Orphan() override;

   private:
    // A call tracker that collects per-call endpoint utilization reports.
    class SubchannelCallTracker : public SubchannelCallTrackerInterface {
     public:
      SubchannelCallTracker(RefCountedPtr<AddressWeight> weight,
                            float error_utilization_penalty)
          : weight_(std::move(weight)),
            error_utilization_penalty_(error_utilization_penalty) {}

      void Start() override {}

      void Finish(FinishArgs args) override;

     private:
      RefCountedPtr<AddressWeight> weight_;
      const float error_utilization_penalty_;
    };

    // Info stored about each subchannel.
    struct SubchannelInfo {
      SubchannelInfo(RefCountedPtr<SubchannelInterface> subchannel,
                     RefCountedPtr<AddressWeight> weight)
          : subchannel(std::move(subchannel)), weight(std::move(weight)) {}

      RefCountedPtr<SubchannelInterface> subchannel;
      RefCountedPtr<AddressWeight> weight;
    };

    // Returns the index into subchannels_ to be picked.
    size_t PickIndex();

    // Builds a new scheduler and swaps it into place, then starts a
    // timer for the next update.
    void BuildSchedulerAndStartTimerLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&timer_mu_);

    RefCountedPtr<WeightedRoundRobin> wrr_;
    const bool use_per_rpc_utilization_;
    const Duration weight_update_period_;
    const Duration weight_expiration_period_;
    const Duration blackout_period_;
    const float error_utilization_penalty_;
    std::vector<SubchannelInfo> subchannels_;

    Mutex scheduler_mu_;
    std::shared_ptr<StaticStrideScheduler> scheduler_
        ABSL_GUARDED_BY(&scheduler_mu_);

    Mutex timer_mu_ ABSL_ACQUIRED_BEFORE(&scheduler_mu_);
    absl::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
        timer_handle_ ABSL_GUARDED_BY(&timer_mu_);

    // Used when falling back to RR.
    std::atomic<size_t> last_picked_index_;
  };

  ~WeightedRoundRobin() override;

  void ShutdownLocked() override;

  RefCountedPtr<AddressWeight> GetOrCreateWeight(
      const grpc_resolved_address& address);

  RefCountedPtr<WeightedRoundRobinConfig> config_;

  // List of subchannels.
  RefCountedPtr<WeightedRoundRobinSubchannelList> subchannel_list_;
  // Latest pending subchannel list.
  // When we get an updated address list, we create a new subchannel list
  // for it here, and we wait to swap it into subchannel_list_ until the new
  // list becomes READY.
  RefCountedPtr<WeightedRoundRobinSubchannelList>
      latest_pending_subchannel_list_;

  Mutex address_weight_map_mu_;
  std::map<std::string, AddressWeight*, std::less<>> address_weight_map_
      ABSL_GUARDED_BY(&address_weight_map_mu_);

  bool shutdown_ = false;

  absl::BitGen bit_gen_;

  // Accessed by picker.
  std::atomic<uint32_t> scheduler_state_{absl::Uniform<uint32_t>(bit_gen_)};
};

//
// WeightedRoundRobin::AddressWeight
//

WeightedRoundRobin::AddressWeight::~AddressWeight() {
  MutexLock lock(&wrr_->address_weight_map_mu_);
  auto it = wrr_->address_weight_map_.find(key_);
  if (it != wrr_->address_weight_map_.end() && it->second == this) {
    wrr_->address_weight_map_.erase(it);
  }
}

void WeightedRoundRobin::AddressWeight::MaybeUpdateWeight(
    double qps, double eps, double cpu_utilization,
    float error_utilization_penalty) {
  // Compute weight.
  float weight = 0;
  if (qps > 0 && cpu_utilization > 0) {
    double penalty = 0.0;
    if (eps > 0 && error_utilization_penalty > 0) {
      penalty = eps / qps * error_utilization_penalty;
    }
    weight = qps / (cpu_utilization + penalty);
  }
  if (weight == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO,
              "[WRR %p] subchannel %s: qps=%f, eps=%f, cpu_utilization=%f: "
              "error_util_penalty=%f, weight=%f (not updating)",
              wrr_.get(), key_.c_str(), qps, eps, cpu_utilization,
              error_utilization_penalty, weight);
    }
    return;
  }
  Timestamp now = Timestamp::Now();
  // Grab the lock and update the data.
  MutexLock lock(&mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO,
            "[WRR %p] subchannel %s: qps=%f, eps=%f, cpu_utilization=%f "
            "error_util_penalty=%f : setting weight=%f weight_=%f now=%s "
            "last_update_time_=%s non_empty_since_=%s",
            wrr_.get(), key_.c_str(), qps, eps, cpu_utilization,
            error_utilization_penalty, weight, weight_, now.ToString().c_str(),
            last_update_time_.ToString().c_str(),
            non_empty_since_.ToString().c_str());
  }
  if (non_empty_since_ == Timestamp::InfFuture()) non_empty_since_ = now;
  weight_ = weight;
  last_update_time_ = now;
}

float WeightedRoundRobin::AddressWeight::GetWeight(
    Timestamp now, Duration weight_expiration_period,
    Duration blackout_period) {
  MutexLock lock(&mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO,
            "[WRR %p] subchannel %s: getting weight: now=%s "
            "weight_expiration_period=%s blackout_period=%s "
            "last_update_time_=%s non_empty_since_=%s weight_=%f",
            wrr_.get(), key_.c_str(), now.ToString().c_str(),
            weight_expiration_period.ToString().c_str(),
            blackout_period.ToString().c_str(),
            last_update_time_.ToString().c_str(),
            non_empty_since_.ToString().c_str(), weight_);
  }
  // If the most recent update was longer ago than the expiration
  // period, reset non_empty_since_ so that we apply the blackout period
  // again if we start getting data again in the future, and return 0.
  if (now - last_update_time_ >= weight_expiration_period) {
    non_empty_since_ = Timestamp::InfFuture();
    return 0;
  }
  // If we don't have at least blackout_period worth of data, return 0.
  if (blackout_period > Duration::Zero() &&
      now - non_empty_since_ < blackout_period) {
    return 0;
  }
  // Otherwise, return the weight.
  return weight_;
}

void WeightedRoundRobin::AddressWeight::ResetNonEmptySince() {
  MutexLock lock(&mu_);
  non_empty_since_ = Timestamp::InfFuture();
}

//
// WeightedRoundRobin::Picker::SubchannelCallTracker
//

void WeightedRoundRobin::Picker::SubchannelCallTracker::Finish(
    FinishArgs args) {
  auto* backend_metric_data =
      args.backend_metric_accessor->GetBackendMetricData();
  double qps = 0;
  double eps = 0;
  double cpu_utilization = 0;
  if (backend_metric_data != nullptr) {
    qps = backend_metric_data->qps;
    eps = backend_metric_data->eps;
    cpu_utilization = backend_metric_data->cpu_utilization;
  }
  weight_->MaybeUpdateWeight(qps, eps, cpu_utilization,
                             error_utilization_penalty_);
}

//
// WeightedRoundRobin::Picker
//

WeightedRoundRobin::Picker::Picker(
    RefCountedPtr<WeightedRoundRobin> wrr,
    WeightedRoundRobinSubchannelList* subchannel_list)
    : wrr_(std::move(wrr)),
      use_per_rpc_utilization_(!wrr_->config_->enable_oob_load_report()),
      weight_update_period_(wrr_->config_->weight_update_period()),
      weight_expiration_period_(wrr_->config_->weight_expiration_period()),
      blackout_period_(wrr_->config_->blackout_period()),
      error_utilization_penalty_(wrr_->config_->error_utilization_penalty()),
      last_picked_index_(absl::Uniform<size_t>(wrr_->bit_gen_)) {
  for (size_t i = 0; i < subchannel_list->num_subchannels(); ++i) {
    WeightedRoundRobinSubchannelData* sd = subchannel_list->subchannel(i);
    if (sd->connectivity_state() == GRPC_CHANNEL_READY) {
      subchannels_.emplace_back(sd->subchannel()->Ref(), sd->weight());
    }
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO,
            "[WRR %p picker %p] created picker from subchannel_list=%p "
            "with %" PRIuPTR " subchannels",
            wrr_.get(), this, subchannel_list, subchannels_.size());
  }
  BuildSchedulerAndStartTimerLocked();
}

WeightedRoundRobin::Picker::~Picker() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO, "[WRR %p picker %p] destroying picker", wrr_.get(), this);
  }
}

void WeightedRoundRobin::Picker::Orphan() {
  MutexLock lock(&timer_mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO, "[WRR %p picker %p] cancelling timer", wrr_.get(), this);
  }
  wrr_->channel_control_helper()->GetEventEngine()->Cancel(*timer_handle_);
  timer_handle_.reset();
}

WeightedRoundRobin::PickResult WeightedRoundRobin::Picker::Pick(
    PickArgs /*args*/) {
  size_t index = PickIndex();
  GPR_ASSERT(index < subchannels_.size());
  auto& subchannel_info = subchannels_[index];
  // Collect per-call utilization data if needed.
  std::unique_ptr<SubchannelCallTrackerInterface> subchannel_call_tracker;
  if (use_per_rpc_utilization_) {
    subchannel_call_tracker = std::make_unique<SubchannelCallTracker>(
        subchannel_info.weight, error_utilization_penalty_);
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO,
            "[WRR %p picker %p] returning index %" PRIuPTR ", subchannel=%p",
            wrr_.get(), this, index, subchannel_info.subchannel.get());
  }
  return PickResult::Complete(subchannel_info.subchannel,
                              std::move(subchannel_call_tracker));
}

size_t WeightedRoundRobin::Picker::PickIndex() {
  // Grab a ref to the scheduler.
  std::shared_ptr<StaticStrideScheduler> scheduler;
  {
    MutexLock lock(&scheduler_mu_);
    scheduler = scheduler_;
  }
  // If we have a scheduler, use it to do a WRR pick.
  if (scheduler != nullptr) return scheduler->Pick();
  // We don't have a scheduler (i.e., either all of the weights are 0 or
  // there is only one subchannel), so fall back to RR.
  return last_picked_index_.fetch_add(1) % subchannels_.size();
}

void WeightedRoundRobin::Picker::BuildSchedulerAndStartTimerLocked() {
  // Build scheduler.
  const Timestamp now = Timestamp::Now();
  std::vector<float> weights;
  weights.reserve(subchannels_.size());
  for (const auto& subchannel : subchannels_) {
    weights.push_back(subchannel.weight->GetWeight(
        now, weight_expiration_period_, blackout_period_));
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO, "[WRR %p picker %p] new weights: %s", wrr_.get(), this,
            absl::StrJoin(weights, " ").c_str());
  }
  auto scheduler_or = StaticStrideScheduler::Make(
      weights, [this]() { return wrr_->scheduler_state_.fetch_add(1); });
  std::shared_ptr<StaticStrideScheduler> scheduler;
  if (scheduler_or.has_value()) {
    scheduler =
        std::make_shared<StaticStrideScheduler>(std::move(*scheduler_or));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO, "[WRR %p picker %p] new scheduler: %p", wrr_.get(),
              this, scheduler.get());
    }
  } else if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO, "[WRR %p picker %p] no scheduler, falling back to RR",
            wrr_.get(), this);
  }
  {
    MutexLock lock(&scheduler_mu_);
    scheduler_ = std::move(scheduler);
  }
  // Start timer.
  WeakRefCountedPtr<Picker> self = WeakRef();
  timer_handle_ = wrr_->channel_control_helper()->GetEventEngine()->RunAfter(
      weight_update_period_, [self = std::move(self)]() mutable {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        {
          MutexLock lock(&self->timer_mu_);
          if (self->timer_handle_.has_value()) {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
              gpr_log(GPR_INFO, "[WRR %p picker %p] timer fired",
                      self->wrr_.get(), self.get());
            }
            self->BuildSchedulerAndStartTimerLocked();
          }
        }
        // Release ref before ExecCtx goes out of scope.
        self.reset();
      });
}

//
// WeightedRoundRobin
//

WeightedRoundRobin::WeightedRoundRobin(Args args)
    : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO, "[WRR %p] Created", this);
  }
}

WeightedRoundRobin::~WeightedRoundRobin() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO, "[WRR %p] Destroying Round Robin policy", this);
  }
  GPR_ASSERT(subchannel_list_ == nullptr);
  GPR_ASSERT(latest_pending_subchannel_list_ == nullptr);
}

void WeightedRoundRobin::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO, "[WRR %p] Shutting down", this);
  }
  shutdown_ = true;
  subchannel_list_.reset();
  latest_pending_subchannel_list_.reset();
}

void WeightedRoundRobin::ResetBackoffLocked() {
  subchannel_list_->ResetBackoffLocked();
  if (latest_pending_subchannel_list_ != nullptr) {
    latest_pending_subchannel_list_->ResetBackoffLocked();
  }
}

absl::Status WeightedRoundRobin::UpdateLocked(UpdateArgs args) {
  config_ = std::move(args.config);
  ServerAddressList addresses;
  if (args.addresses.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO, "[WRR %p] received update with %" PRIuPTR " addresses",
              this, args.addresses->size());
    }
    // Weed out duplicate addresses.  Also sort the addresses so that if
    // the set of the addresses don't change, their indexes in the
    // subchannel list don't change, since this avoids unnecessary churn
    // in the picker.  Note that this does not ensure that if a given
    // address remains present that it will have the same index; if,
    // for example, an address at the end of the list is replaced with one
    // that sorts much earlier in the list, then all of the addresses in
    // between those two positions will have changed indexes.
    struct AddressLessThan {
      bool operator()(const ServerAddress& address1,
                      const ServerAddress& address2) const {
        const grpc_resolved_address& addr1 = address1.address();
        const grpc_resolved_address& addr2 = address2.address();
        if (addr1.len != addr2.len) return addr1.len < addr2.len;
        return memcmp(addr1.addr, addr2.addr, addr1.len) < 0;
      }
    };
    std::set<ServerAddress, AddressLessThan> ordered_addresses(
        args.addresses->begin(), args.addresses->end());
    addresses =
        ServerAddressList(ordered_addresses.begin(), ordered_addresses.end());
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO, "[WRR %p] received update with address error: %s", this,
              args.addresses.status().ToString().c_str());
    }
    // If we already have a subchannel list, then keep using the existing
    // list, but still report back that the update was not accepted.
    if (subchannel_list_ != nullptr) return args.addresses.status();
  }
  // Create new subchannel list, replacing the previous pending list, if any.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace) &&
      latest_pending_subchannel_list_ != nullptr) {
    gpr_log(GPR_INFO, "[WRR %p] replacing previous pending subchannel list %p",
            this, latest_pending_subchannel_list_.get());
  }
  latest_pending_subchannel_list_ =
      MakeRefCounted<WeightedRoundRobinSubchannelList>(
          this, std::move(addresses), args.args);
  latest_pending_subchannel_list_->StartWatchingLocked();
  // If the new list is empty, immediately promote it to
  // subchannel_list_ and report TRANSIENT_FAILURE.
  if (latest_pending_subchannel_list_->num_subchannels() == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace) &&
        subchannel_list_ != nullptr) {
      gpr_log(GPR_INFO, "[WRR %p] replacing previous subchannel list %p", this,
              subchannel_list_.get());
    }
    subchannel_list_ = std::move(latest_pending_subchannel_list_);
    absl::Status status =
        args.addresses.ok() ? absl::UnavailableError(absl::StrCat(
                                  "empty address list: ", args.resolution_note))
                            : args.addresses.status();
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        MakeRefCounted<TransientFailurePicker>(status));
    return status;
  }
  // Otherwise, if this is the initial update, immediately promote it to
  // subchannel_list_.
  if (subchannel_list_.get() == nullptr) {
    subchannel_list_ = std::move(latest_pending_subchannel_list_);
  }
  return absl::OkStatus();
}

RefCountedPtr<WeightedRoundRobin::AddressWeight>
WeightedRoundRobin::GetOrCreateWeight(const grpc_resolved_address& address) {
  auto key = grpc_sockaddr_to_uri(&address);
  if (!key.ok()) return nullptr;
  MutexLock lock(&address_weight_map_mu_);
  auto it = address_weight_map_.find(*key);
  if (it != address_weight_map_.end()) {
    auto weight = it->second->RefIfNonZero();
    if (weight != nullptr) return weight;
  }
  auto weight =
      MakeRefCounted<AddressWeight>(Ref(DEBUG_LOCATION, "AddressWeight"), *key);
  address_weight_map_.emplace(*key, weight.get());
  return weight;
}

//
// WeightedRoundRobin::WeightedRoundRobinSubchannelList
//

void WeightedRoundRobin::WeightedRoundRobinSubchannelList::
    UpdateStateCountersLocked(absl::optional<grpc_connectivity_state> old_state,
                              grpc_connectivity_state new_state) {
  if (old_state.has_value()) {
    GPR_ASSERT(*old_state != GRPC_CHANNEL_SHUTDOWN);
    if (*old_state == GRPC_CHANNEL_READY) {
      GPR_ASSERT(num_ready_ > 0);
      --num_ready_;
    } else if (*old_state == GRPC_CHANNEL_CONNECTING) {
      GPR_ASSERT(num_connecting_ > 0);
      --num_connecting_;
    } else if (*old_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      GPR_ASSERT(num_transient_failure_ > 0);
      --num_transient_failure_;
    }
  }
  GPR_ASSERT(new_state != GRPC_CHANNEL_SHUTDOWN);
  if (new_state == GRPC_CHANNEL_READY) {
    ++num_ready_;
  } else if (new_state == GRPC_CHANNEL_CONNECTING) {
    ++num_connecting_;
  } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ++num_transient_failure_;
  }
}

void WeightedRoundRobin::WeightedRoundRobinSubchannelList::
    MaybeUpdateAggregatedConnectivityStateLocked(absl::Status status_for_tf) {
  WeightedRoundRobin* p = static_cast<WeightedRoundRobin*>(policy());
  // If this is latest_pending_subchannel_list_, then swap it into
  // subchannel_list_ in the following cases:
  // - subchannel_list_ has no READY subchannels.
  // - This list has at least one READY subchannel and we have seen the
  //   initial connectivity state notification for all subchannels.
  // - All of the subchannels in this list are in TRANSIENT_FAILURE.
  //   (This may cause the channel to go from READY to TRANSIENT_FAILURE,
  //   but we're doing what the control plane told us to do.)
  if (p->latest_pending_subchannel_list_.get() == this &&
      (p->subchannel_list_->num_ready_ == 0 ||
       (num_ready_ > 0 && AllSubchannelsSeenInitialState()) ||
       num_transient_failure_ == num_subchannels())) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      const std::string old_counters_string =
          p->subchannel_list_ != nullptr ? p->subchannel_list_->CountersString()
                                         : "";
      gpr_log(
          GPR_INFO,
          "[WRR %p] swapping out subchannel list %p (%s) in favor of %p (%s)",
          p, p->subchannel_list_.get(), old_counters_string.c_str(), this,
          CountersString().c_str());
    }
    p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
  }
  // Only set connectivity state if this is the current subchannel list.
  if (p->subchannel_list_.get() != this) return;
  // First matching rule wins:
  // 1) ANY subchannel is READY => policy is READY.
  // 2) ANY subchannel is CONNECTING => policy is CONNECTING.
  // 3) ALL subchannels are TRANSIENT_FAILURE => policy is TRANSIENT_FAILURE.
  if (num_ready_ > 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO, "[WRR %p] reporting READY with subchannel list %p", p,
              this);
    }
    p->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_READY, absl::Status(),
        MakeRefCounted<Picker>(p->Ref(), this));
  } else if (num_connecting_ > 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO, "[WRR %p] reporting CONNECTING with subchannel list %p",
              p, this);
    }
    p->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING, absl::Status(),
        MakeRefCounted<QueuePicker>(p->Ref(DEBUG_LOCATION, "QueuePicker")));
  } else if (num_transient_failure_ == num_subchannels()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(
          GPR_INFO,
          "[WRR %p] reporting TRANSIENT_FAILURE with subchannel list %p: %s", p,
          this, status_for_tf.ToString().c_str());
    }
    if (!status_for_tf.ok()) {
      last_failure_ = absl::UnavailableError(
          absl::StrCat("connections to all backends failing; last error: ",
                       status_for_tf.ToString()));
    }
    p->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, last_failure_,
        MakeRefCounted<TransientFailurePicker>(last_failure_));
  }
}

//
// WeightedRoundRobin::WeightedRoundRobinSubchannelData::OobWatcher
//

void WeightedRoundRobin::WeightedRoundRobinSubchannelData::OobWatcher::
    OnBackendMetricReport(const BackendMetricData& backend_metric_data) {
  weight_->MaybeUpdateWeight(backend_metric_data.qps, backend_metric_data.eps,
                             backend_metric_data.cpu_utilization,
                             error_utilization_penalty_);
}

//
// WeightedRoundRobin::WeightedRoundRobinSubchannelData
//

WeightedRoundRobin::WeightedRoundRobinSubchannelData::
    WeightedRoundRobinSubchannelData(
        SubchannelList<WeightedRoundRobinSubchannelList,
                       WeightedRoundRobinSubchannelData>* subchannel_list,
        const ServerAddress& address, RefCountedPtr<SubchannelInterface> sc)
    : SubchannelData(subchannel_list, address, std::move(sc)),
      weight_(static_cast<WeightedRoundRobin*>(subchannel_list->policy())
                  ->GetOrCreateWeight(address.address())) {
  // Start OOB watch if configured.
  WeightedRoundRobin* p =
      static_cast<WeightedRoundRobin*>(subchannel_list->policy());
  if (p->config_->enable_oob_load_report()) {
    subchannel()->AddDataWatcher(MakeOobBackendMetricWatcher(
        p->config_->oob_reporting_period(),
        std::make_unique<OobWatcher>(weight_,
                                     p->config_->error_utilization_penalty())));
  }
}

void WeightedRoundRobin::WeightedRoundRobinSubchannelData::
    ProcessConnectivityChangeLocked(
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state) {
  WeightedRoundRobin* p =
      static_cast<WeightedRoundRobin*>(subchannel_list()->policy());
  GPR_ASSERT(subchannel() != nullptr);
  // If this is not the initial state notification and the new state is
  // TRANSIENT_FAILURE or IDLE, re-resolve.
  // Note that we don't want to do this on the initial state notification,
  // because that would result in an endless loop of re-resolution.
  if (old_state.has_value() && (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
                                new_state == GRPC_CHANNEL_IDLE)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO,
              "[WRR %p] Subchannel %p reported %s; requesting re-resolution", p,
              subchannel(), ConnectivityStateName(new_state));
    }
    p->channel_control_helper()->RequestReresolution();
  }
  if (new_state == GRPC_CHANNEL_IDLE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO,
              "[WRR %p] Subchannel %p reported IDLE; requesting connection", p,
              subchannel());
    }
    subchannel()->RequestConnection();
  } else if (new_state == GRPC_CHANNEL_READY) {
    // If we transition back to READY state, restart the blackout period.
    // Note that we cannot guarantee that we will never receive
    // lingering callbacks for backend metric reports from the previous
    // connection after the new connection has been established, but they
    // should be masked by new backend metric reports from the new
    // connection by the time the blackout period ends.
    weight_->ResetNonEmptySince();
  }
  // Update logical connectivity state.
  UpdateLogicalConnectivityStateLocked(new_state);
  // Update the policy state.
  subchannel_list()->MaybeUpdateAggregatedConnectivityStateLocked(
      connectivity_status());
}

void WeightedRoundRobin::WeightedRoundRobinSubchannelData::
    UpdateLogicalConnectivityStateLocked(
        grpc_connectivity_state connectivity_state) {
  WeightedRoundRobin* p =
      static_cast<WeightedRoundRobin*>(subchannel_list()->policy());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(
        GPR_INFO,
        "[WRR %p] connectivity changed for subchannel %p, subchannel_list %p "
        "(index %" PRIuPTR " of %" PRIuPTR "): prev_state=%s new_state=%s",
        p, subchannel(), subchannel_list(), Index(),
        subchannel_list()->num_subchannels(),
        (logical_connectivity_state_.has_value()
             ? ConnectivityStateName(*logical_connectivity_state_)
             : "N/A"),
        ConnectivityStateName(connectivity_state));
  }
  // Decide what state to report for aggregation purposes.
  // If the last logical state was TRANSIENT_FAILURE, then ignore the
  // state change unless the new state is READY.
  if (logical_connectivity_state_.has_value() &&
      *logical_connectivity_state_ == GRPC_CHANNEL_TRANSIENT_FAILURE &&
      connectivity_state != GRPC_CHANNEL_READY) {
    return;
  }
  // If the new state is IDLE, treat it as CONNECTING, since it will
  // immediately transition into CONNECTING anyway.
  if (connectivity_state == GRPC_CHANNEL_IDLE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO,
              "[WRR %p] subchannel %p, subchannel_list %p (index %" PRIuPTR
              " of %" PRIuPTR "): treating IDLE as CONNECTING",
              p, subchannel(), subchannel_list(), Index(),
              subchannel_list()->num_subchannels());
    }
    connectivity_state = GRPC_CHANNEL_CONNECTING;
  }
  // If no change, return false.
  if (logical_connectivity_state_.has_value() &&
      *logical_connectivity_state_ == connectivity_state) {
    return;
  }
  // Otherwise, update counters and logical state.
  subchannel_list()->UpdateStateCountersLocked(logical_connectivity_state_,
                                               connectivity_state);
  logical_connectivity_state_ = connectivity_state;
}

//
// factory
//

class WeightedRoundRobinFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<WeightedRoundRobin>(std::move(args));
  }

  absl::string_view name() const override { return kWeightedRoundRobin; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadRefCountedFromJson<WeightedRoundRobinConfig>(
        json, JsonArgs(),
        "errors validating weighted_round_robin LB policy config");
  }
};

}  // namespace

void RegisterWeightedRoundRobinLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<WeightedRoundRobinFactory>());
}

}  // namespace grpc_core
