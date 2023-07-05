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
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"
#include "src/core/ext/filters/client_channel/lb_policy/endpoint_list.h"
#include "src/core/ext/filters/client_channel/lb_policy/oob_backend_metric.h"
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
  class EndpointWeight : public RefCounted<EndpointWeight> {
   public:
    EndpointWeight(RefCountedPtr<WeightedRoundRobin> wrr, std::string key)
        : wrr_(std::move(wrr)), key_(std::move(key)) {}
    ~EndpointWeight() override;

    void MaybeUpdateWeight(double qps, double eps, double utilization,
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

  class WrrEndpointList : public EndpointList {
   public:
    class WrrEndpoint : public Endpoint {
     public:
      WrrEndpoint(RefCountedPtr<WrrEndpointList> endpoint_list,
                  const ServerAddress& address, const ChannelArgs& args,
                  std::shared_ptr<WorkSerializer> work_serializer)
          : Endpoint(std::move(endpoint_list)),
            weight_(policy<WeightedRoundRobin>()->GetOrCreateWeight(
                address.address())) {
        Init(address, args, std::move(work_serializer));
      }

      RefCountedPtr<EndpointWeight> weight() const { return weight_; }

     private:
      class OobWatcher : public OobBackendMetricWatcher {
       public:
        OobWatcher(RefCountedPtr<EndpointWeight> weight,
                   float error_utilization_penalty)
            : weight_(std::move(weight)),
              error_utilization_penalty_(error_utilization_penalty) {}

        void OnBackendMetricReport(
            const BackendMetricData& backend_metric_data) override;

       private:
        RefCountedPtr<EndpointWeight> weight_;
        const float error_utilization_penalty_;
      };

      RefCountedPtr<SubchannelInterface> CreateSubchannel(
          ServerAddress address, const ChannelArgs& args) override;

      // Called when the child policy reports a connectivity state update.
      void OnStateUpdate(absl::optional<grpc_connectivity_state> old_state,
                         grpc_connectivity_state new_state,
                         const absl::Status& status) override;

      RefCountedPtr<EndpointWeight> weight_;
    };

    WrrEndpointList(RefCountedPtr<WeightedRoundRobin> wrr,
                    const ServerAddressList& addresses, const ChannelArgs& args)
        : EndpointList(std::move(wrr),
                       GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)
                           ? "WrrEndpointList"
                           : nullptr) {
      Init(addresses, args,
           [&](RefCountedPtr<WrrEndpointList> endpoint_list,
               const ServerAddress& address, const ChannelArgs& args) {
             return MakeOrphanable<WrrEndpoint>(
                 std::move(endpoint_list), address, args,
                 policy<WeightedRoundRobin>()->work_serializer());
           });
    }

   private:
    LoadBalancingPolicy::ChannelControlHelper* channel_control_helper()
        const override {
      return policy<WeightedRoundRobin>()->channel_control_helper();
    }

    // Updates the counters of children in each state when a
    // child transitions from old_state to new_state.
    void UpdateStateCountersLocked(
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state);

    // Ensures that the right child list is used and then updates
    // the WRR policy's connectivity state based on the child list's
    // state counters.
    void MaybeUpdateAggregatedConnectivityStateLocked(
        absl::Status status_for_tf);

    std::string CountersString() const {
      return absl::StrCat("num_children=", size(), " num_ready=", num_ready_,
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
           WrrEndpointList* endpoint_list);

    ~Picker() override;

    PickResult Pick(PickArgs args) override;

    void Orphan() override;

   private:
    // A call tracker that collects per-call endpoint utilization reports.
    class SubchannelCallTracker : public SubchannelCallTrackerInterface {
     public:
      SubchannelCallTracker(
          RefCountedPtr<EndpointWeight> weight, float error_utilization_penalty,
          std::unique_ptr<SubchannelCallTrackerInterface> child_tracker)
          : weight_(std::move(weight)),
            error_utilization_penalty_(error_utilization_penalty),
            child_tracker_(std::move(child_tracker)) {}

      void Start() override;

      void Finish(FinishArgs args) override;

     private:
      RefCountedPtr<EndpointWeight> weight_;
      const float error_utilization_penalty_;
      std::unique_ptr<SubchannelCallTrackerInterface> child_tracker_;
    };

    // Info stored about each endpoint.
    struct EndpointInfo {
      EndpointInfo(RefCountedPtr<SubchannelPicker> picker,
                   RefCountedPtr<EndpointWeight> weight)
          : picker(std::move(picker)), weight(std::move(weight)) {}

      RefCountedPtr<SubchannelPicker> picker;
      RefCountedPtr<EndpointWeight> weight;
    };

    // Returns the index into endpoints_ to be picked.
    size_t PickIndex();

    // Builds a new scheduler and swaps it into place, then starts a
    // timer for the next update.
    void BuildSchedulerAndStartTimerLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&timer_mu_);

    RefCountedPtr<WeightedRoundRobin> wrr_;
    RefCountedPtr<WeightedRoundRobinConfig> config_;
    std::vector<EndpointInfo> endpoints_;

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

  RefCountedPtr<EndpointWeight> GetOrCreateWeight(
      const grpc_resolved_address& address);

  RefCountedPtr<WeightedRoundRobinConfig> config_;

  // List of endpoints.
  OrphanablePtr<WrrEndpointList> endpoint_list_;
  // Latest pending endpoint list.
  // When we get an updated address list, we create a new endpoint list
  // for it here, and we wait to swap it into endpoint_list_ until the new
  // list becomes READY.
  OrphanablePtr<WrrEndpointList> latest_pending_endpoint_list_;

  Mutex endpoint_weight_map_mu_;
  std::map<std::string, EndpointWeight*, std::less<>> endpoint_weight_map_
      ABSL_GUARDED_BY(&endpoint_weight_map_mu_);

  bool shutdown_ = false;

  absl::BitGen bit_gen_;

  // Accessed by picker.
  std::atomic<uint32_t> scheduler_state_{absl::Uniform<uint32_t>(bit_gen_)};
};

//
// WeightedRoundRobin::EndpointWeight
//

WeightedRoundRobin::EndpointWeight::~EndpointWeight() {
  MutexLock lock(&wrr_->endpoint_weight_map_mu_);
  auto it = wrr_->endpoint_weight_map_.find(key_);
  if (it != wrr_->endpoint_weight_map_.end() && it->second == this) {
    wrr_->endpoint_weight_map_.erase(it);
  }
}

void WeightedRoundRobin::EndpointWeight::MaybeUpdateWeight(
    double qps, double eps, double utilization,
    float error_utilization_penalty) {
  // Compute weight.
  float weight = 0;
  if (qps > 0 && utilization > 0) {
    double penalty = 0.0;
    if (eps > 0 && error_utilization_penalty > 0) {
      penalty = eps / qps * error_utilization_penalty;
    }
    weight = qps / (utilization + penalty);
  }
  if (weight == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO,
              "[WRR %p] subchannel %s: qps=%f, eps=%f, utilization=%f: "
              "error_util_penalty=%f, weight=%f (not updating)",
              wrr_.get(), key_.c_str(), qps, eps, utilization,
              error_utilization_penalty, weight);
    }
    return;
  }
  Timestamp now = Timestamp::Now();
  // Grab the lock and update the data.
  MutexLock lock(&mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO,
            "[WRR %p] subchannel %s: qps=%f, eps=%f, utilization=%f "
            "error_util_penalty=%f : setting weight=%f weight_=%f now=%s "
            "last_update_time_=%s non_empty_since_=%s",
            wrr_.get(), key_.c_str(), qps, eps, utilization,
            error_utilization_penalty, weight, weight_, now.ToString().c_str(),
            last_update_time_.ToString().c_str(),
            non_empty_since_.ToString().c_str());
  }
  if (non_empty_since_ == Timestamp::InfFuture()) non_empty_since_ = now;
  weight_ = weight;
  last_update_time_ = now;
}

float WeightedRoundRobin::EndpointWeight::GetWeight(
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

void WeightedRoundRobin::EndpointWeight::ResetNonEmptySince() {
  MutexLock lock(&mu_);
  non_empty_since_ = Timestamp::InfFuture();
}

//
// WeightedRoundRobin::Picker::SubchannelCallTracker
//

void WeightedRoundRobin::Picker::SubchannelCallTracker::Start() {
  if (child_tracker_ != nullptr) child_tracker_->Start();
}

void WeightedRoundRobin::Picker::SubchannelCallTracker::Finish(
    FinishArgs args) {
  if (child_tracker_ != nullptr) child_tracker_->Finish(args);
  auto* backend_metric_data =
      args.backend_metric_accessor->GetBackendMetricData();
  double qps = 0;
  double eps = 0;
  double utilization = 0;
  if (backend_metric_data != nullptr) {
    qps = backend_metric_data->qps;
    eps = backend_metric_data->eps;
    utilization = backend_metric_data->application_utilization;
    if (utilization <= 0) {
      utilization = backend_metric_data->cpu_utilization;
    }
  }
  weight_->MaybeUpdateWeight(qps, eps, utilization, error_utilization_penalty_);
}

//
// WeightedRoundRobin::Picker
//

WeightedRoundRobin::Picker::Picker(RefCountedPtr<WeightedRoundRobin> wrr,
                                   WrrEndpointList* endpoint_list)
    : wrr_(std::move(wrr)),
      config_(wrr_->config_),
      last_picked_index_(absl::Uniform<size_t>(wrr_->bit_gen_)) {
  for (auto& endpoint : endpoint_list->endpoints()) {
    auto* ep = static_cast<WrrEndpointList::WrrEndpoint*>(endpoint.get());
    if (ep->connectivity_state() == GRPC_CHANNEL_READY) {
      endpoints_.emplace_back(ep->picker(), ep->weight());
    }
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO,
            "[WRR %p picker %p] created picker from endpoint_list=%p "
            "with %" PRIuPTR " subchannels",
            wrr_.get(), this, endpoint_list, endpoints_.size());
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
  wrr_.reset();
}

WeightedRoundRobin::PickResult WeightedRoundRobin::Picker::Pick(PickArgs args) {
  size_t index = PickIndex();
  GPR_ASSERT(index < endpoints_.size());
  auto& endpoint_info = endpoints_[index];
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO,
            "[WRR %p picker %p] returning index %" PRIuPTR ", picker=%p",
            wrr_.get(), this, index, endpoint_info.picker.get());
  }
  auto result = endpoint_info.picker->Pick(args);
  // Collect per-call utilization data if needed.
  if (!config_->enable_oob_load_report()) {
    auto* complete = absl::get_if<PickResult::Complete>(&result.result);
    if (complete != nullptr) {
      complete->subchannel_call_tracker =
          std::make_unique<SubchannelCallTracker>(
              endpoint_info.weight, config_->error_utilization_penalty(),
              std::move(complete->subchannel_call_tracker));
    }
  }
  return result;
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
  return last_picked_index_.fetch_add(1) % endpoints_.size();
}

void WeightedRoundRobin::Picker::BuildSchedulerAndStartTimerLocked() {
  // Build scheduler.
  const Timestamp now = Timestamp::Now();
  std::vector<float> weights;
  weights.reserve(endpoints_.size());
  for (const auto& endpoint : endpoints_) {
    weights.push_back(endpoint.weight->GetWeight(
        now, config_->weight_expiration_period(), config_->blackout_period()));
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO, "[WRR %p picker %p] scheduling timer for %s", wrr_.get(),
            this, config_->weight_update_period().ToString().c_str());
  }
  WeakRefCountedPtr<Picker> self = WeakRef();
  timer_handle_ = wrr_->channel_control_helper()->GetEventEngine()->RunAfter(
      config_->weight_update_period(), [self = std::move(self)]() mutable {
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
  GPR_ASSERT(endpoint_list_ == nullptr);
  GPR_ASSERT(latest_pending_endpoint_list_ == nullptr);
}

void WeightedRoundRobin::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO, "[WRR %p] Shutting down", this);
  }
  shutdown_ = true;
  endpoint_list_.reset();
  latest_pending_endpoint_list_.reset();
}

void WeightedRoundRobin::ResetBackoffLocked() {
  endpoint_list_->ResetBackoffLocked();
  if (latest_pending_endpoint_list_ != nullptr) {
    latest_pending_endpoint_list_->ResetBackoffLocked();
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
    if (endpoint_list_ != nullptr) return args.addresses.status();
  }
  // Create new subchannel list, replacing the previous pending list, if any.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace) &&
      latest_pending_endpoint_list_ != nullptr) {
    gpr_log(GPR_INFO, "[WRR %p] replacing previous pending subchannel list %p",
            this, latest_pending_endpoint_list_.get());
  }
  latest_pending_endpoint_list_ =
      MakeOrphanable<WrrEndpointList>(Ref(), std::move(addresses), args.args);
  // If the new list is empty, immediately promote it to
  // endpoint_list_ and report TRANSIENT_FAILURE.
  // TODO(roth): As part of adding dualstack backend support, we need to
  // also handle the case where the list of addresses for a given
  // endpoint is empty.
  if (latest_pending_endpoint_list_->size() == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace) &&
        endpoint_list_ != nullptr) {
      gpr_log(GPR_INFO, "[WRR %p] replacing previous subchannel list %p", this,
              endpoint_list_.get());
    }
    endpoint_list_ = std::move(latest_pending_endpoint_list_);
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
  // endpoint_list_.
  if (endpoint_list_.get() == nullptr) {
    endpoint_list_ = std::move(latest_pending_endpoint_list_);
  }
  return absl::OkStatus();
}

RefCountedPtr<WeightedRoundRobin::EndpointWeight>
WeightedRoundRobin::GetOrCreateWeight(const grpc_resolved_address& address) {
  auto key = grpc_sockaddr_to_uri(&address);
  if (!key.ok()) return nullptr;
  MutexLock lock(&endpoint_weight_map_mu_);
  auto it = endpoint_weight_map_.find(*key);
  if (it != endpoint_weight_map_.end()) {
    auto weight = it->second->RefIfNonZero();
    if (weight != nullptr) return weight;
  }
  auto weight = MakeRefCounted<EndpointWeight>(
      Ref(DEBUG_LOCATION, "EndpointWeight"), *key);
  endpoint_weight_map_.emplace(*key, weight.get());
  return weight;
}

//
// WeightedRoundRobin::WrrEndpointList::WrrEndpoint::OobWatcher
//

void WeightedRoundRobin::WrrEndpointList::WrrEndpoint::OobWatcher::
    OnBackendMetricReport(const BackendMetricData& backend_metric_data) {
  double utilization = backend_metric_data.application_utilization;
  if (utilization <= 0) {
    utilization = backend_metric_data.cpu_utilization;
  }
  weight_->MaybeUpdateWeight(backend_metric_data.qps, backend_metric_data.eps,
                             utilization, error_utilization_penalty_);
}

//
// WeightedRoundRobin::WrrEndpointList::WrrEndpoint
//

RefCountedPtr<SubchannelInterface>
WeightedRoundRobin::WrrEndpointList::WrrEndpoint::CreateSubchannel(
    ServerAddress address, const ChannelArgs& args) {
  auto* wrr = policy<WeightedRoundRobin>();
  auto subchannel =
      wrr->channel_control_helper()->CreateSubchannel(std::move(address), args);
  // Start OOB watch if configured.
  if (wrr->config_->enable_oob_load_report()) {
    subchannel->AddDataWatcher(MakeOobBackendMetricWatcher(
        wrr->config_->oob_reporting_period(),
        std::make_unique<OobWatcher>(
            weight_, wrr->config_->error_utilization_penalty())));
  }
  return subchannel;
}

void WeightedRoundRobin::WrrEndpointList::WrrEndpoint::OnStateUpdate(
    absl::optional<grpc_connectivity_state> old_state,
    grpc_connectivity_state new_state, const absl::Status& status) {
  auto* wrr_endpoint_list = endpoint_list<WrrEndpointList>();
  auto* wrr = policy<WeightedRoundRobin>();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
    gpr_log(GPR_INFO,
            "[WRR %p] connectivity changed for child %p, endpoint_list %p "
            "(index %" PRIuPTR " of %" PRIuPTR
            "): prev_state=%s new_state=%s (%s)",
            wrr, this, wrr_endpoint_list, Index(), wrr_endpoint_list->size(),
            (old_state.has_value() ? ConnectivityStateName(*old_state) : "N/A"),
            ConnectivityStateName(new_state), status.ToString().c_str());
  }
  if (new_state == GRPC_CHANNEL_IDLE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO,
              "[WRR %p] child %p reported IDLE; requesting connection", wrr,
              this);
    }
    ExitIdleLocked();
  } else if (new_state == GRPC_CHANNEL_READY) {
    // If we transition back to READY state, restart the blackout period.
    // Note that we cannot guarantee that we will never receive
    // lingering callbacks for backend metric reports from the previous
    // connection after the new connection has been established, but they
    // should be masked by new backend metric reports from the new
    // connection by the time the blackout period ends.
    weight_->ResetNonEmptySince();
  }
  // If state changed, update state counters.
  if (!old_state.has_value() || *old_state != new_state) {
    wrr_endpoint_list->UpdateStateCountersLocked(old_state, new_state);
  }
  // Update the policy state.
  wrr_endpoint_list->MaybeUpdateAggregatedConnectivityStateLocked(status);
}

//
// WeightedRoundRobin::WrrEndpointList
//

void WeightedRoundRobin::WrrEndpointList::UpdateStateCountersLocked(
    absl::optional<grpc_connectivity_state> old_state,
    grpc_connectivity_state new_state) {
  // We treat IDLE the same as CONNECTING, since it will immediately
  // transition into that state anyway.
  if (old_state.has_value()) {
    GPR_ASSERT(*old_state != GRPC_CHANNEL_SHUTDOWN);
    if (*old_state == GRPC_CHANNEL_READY) {
      GPR_ASSERT(num_ready_ > 0);
      --num_ready_;
    } else if (*old_state == GRPC_CHANNEL_CONNECTING ||
               *old_state == GRPC_CHANNEL_IDLE) {
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
  } else if (new_state == GRPC_CHANNEL_CONNECTING ||
             new_state == GRPC_CHANNEL_IDLE) {
    ++num_connecting_;
  } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ++num_transient_failure_;
  }
}

void WeightedRoundRobin::WrrEndpointList::
    MaybeUpdateAggregatedConnectivityStateLocked(absl::Status status_for_tf) {
  auto* wrr = policy<WeightedRoundRobin>();
  // If this is latest_pending_endpoint_list_, then swap it into
  // endpoint_list_ in the following cases:
  // - endpoint_list_ has no READY children.
  // - This list has at least one READY child and we have seen the
  //   initial connectivity state notification for all children.
  // - All of the children in this list are in TRANSIENT_FAILURE.
  //   (This may cause the channel to go from READY to TRANSIENT_FAILURE,
  //   but we're doing what the control plane told us to do.)
  if (wrr->latest_pending_endpoint_list_.get() == this &&
      (wrr->endpoint_list_->num_ready_ == 0 ||
       (num_ready_ > 0 && AllEndpointsSeenInitialState()) ||
       num_transient_failure_ == size())) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      const std::string old_counters_string =
          wrr->endpoint_list_ != nullptr ? wrr->endpoint_list_->CountersString()
                                         : "";
      gpr_log(GPR_INFO,
              "[WRR %p] swapping out endpoint list %p (%s) in favor of %p (%s)",
              wrr, wrr->endpoint_list_.get(), old_counters_string.c_str(), this,
              CountersString().c_str());
    }
    wrr->endpoint_list_ = std::move(wrr->latest_pending_endpoint_list_);
  }
  // Only set connectivity state if this is the current endpoint list.
  if (wrr->endpoint_list_.get() != this) return;
  // First matching rule wins:
  // 1) ANY child is READY => policy is READY.
  // 2) ANY child is CONNECTING => policy is CONNECTING.
  // 3) ALL children are TRANSIENT_FAILURE => policy is TRANSIENT_FAILURE.
  if (num_ready_ > 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO, "[WRR %p] reporting READY with endpoint list %p", wrr,
              this);
    }
    wrr->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_READY, absl::Status(),
        MakeRefCounted<Picker>(wrr->Ref(), this));
  } else if (num_connecting_ > 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO, "[WRR %p] reporting CONNECTING with endpoint list %p",
              wrr, this);
    }
    wrr->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING, absl::Status(),
        MakeRefCounted<QueuePicker>(nullptr));
  } else if (num_transient_failure_ == size()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_wrr_trace)) {
      gpr_log(GPR_INFO,
              "[WRR %p] reporting TRANSIENT_FAILURE with endpoint list %p: %s",
              wrr, this, status_for_tf.ToString().c_str());
    }
    if (!status_for_tf.ok()) {
      last_failure_ = absl::UnavailableError(
          absl::StrCat("connections to all backends failing; last error: ",
                       status_for_tf.ToString()));
    }
    wrr->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, last_failure_,
        MakeRefCounted<TransientFailurePicker>(last_failure_));
  }
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
    return LoadFromJson<RefCountedPtr<WeightedRoundRobinConfig>>(
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
