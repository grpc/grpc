//
//
// Copyright 2023 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#include "test/cpp/interop/backend_metrics_lb_policy.h"

#include "google/protobuf/util/message_differencer.h"

#include "src/core/lib/iomgr/pollset_set.h"

namespace grpc {
namespace testing {

namespace {

using namespace grpc_core;

constexpr absl::string_view kBackendMetricsLbPolicyName =
    "test_backend_metrics_load_balancer";

const std::string kMetricsTrackerAttribute = "orca_metrics_tracker";

absl::optional<xds::data::orca::v3::OrcaLoadReport>
BackendMetricDataToOrcaLoadReport(
    const grpc_core::BackendMetricData* backend_metric_data) {
  if (backend_metric_data == nullptr) {
    return absl::nullopt;
  }
  xds::data::orca::v3::OrcaLoadReport load_report;
  load_report.set_cpu_utilization(backend_metric_data->cpu_utilization);
  load_report.set_mem_utilization(backend_metric_data->mem_utilization);
  load_report.set_rps_fractional(backend_metric_data->qps);
  for (const auto& p : backend_metric_data->request_cost) {
    std::string name(p.first);
    (*load_report.mutable_request_cost())[name] = p.second;
  }
  for (const auto& p : backend_metric_data->utilization) {
    std::string name(p.first);
    (*load_report.mutable_utilization())[name] = p.second;
  }
  return load_report;
}

class BackendMetricsLbPolicy : public LoadBalancingPolicy {
 public:
  BackendMetricsLbPolicy(Args args)
      : LoadBalancingPolicy(std::move(args), /*initial_refcount=*/2) {
    Args delegate_args;
    delegate_args.work_serializer = work_serializer();
    delegate_args.args = channel_args();
    delegate_args.channel_control_helper =
        std::make_unique<Helper>(RefCountedPtr<BackendMetricsLbPolicy>(this));
    load_report_tracker_ = delegate_args.args.GetPointer<LoadReportTracker>(
        kMetricsTrackerAttribute);
    delegate_ =
        CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
            "pick_first", std::move(delegate_args));
    grpc_pollset_set_add_pollset_set(delegate_->interested_parties(),
                                     interested_parties());
  }

  ~BackendMetricsLbPolicy() override = default;

  absl::string_view name() const override {
    return kBackendMetricsLbPolicyName;
  }

  absl::Status UpdateLocked(UpdateArgs args) override {
    return delegate_->UpdateLocked(std::move(args));
  }

  void ExitIdleLocked() override { delegate_->ExitIdleLocked(); }

  void ResetBackoffLocked() override { delegate_->ResetBackoffLocked(); }

 private:
  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<SubchannelPicker> delegate_picker,
           LoadReportTracker* load_report_tracker)
        : delegate_picker_(std::move(delegate_picker)),
          load_report_tracker_(load_report_tracker) {}

    PickResult Pick(PickArgs args) override {
      // Do pick.
      PickResult result = delegate_picker_->Pick(args);
      // Intercept trailing metadata.
      auto* complete_pick = absl::get_if<PickResult::Complete>(&result.result);
      if (complete_pick != nullptr) {
        complete_pick->subchannel_call_tracker =
            std::make_unique<SubchannelCallTracker>(load_report_tracker_);
      }
      return result;
    }

   private:
    RefCountedPtr<SubchannelPicker> delegate_picker_;
    LoadReportTracker* load_report_tracker_;
  };

  class Helper : public ChannelControlHelper {
   public:
    Helper(RefCountedPtr<BackendMetricsLbPolicy> parent)
        : parent_(std::move(parent)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const ChannelArgs& args) override {
      return parent_->channel_control_helper()->CreateSubchannel(
          std::move(address), args);
    }

    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override {
      parent_->channel_control_helper()->UpdateState(
          state, status,
          MakeRefCounted<Picker>(std::move(picker),
                                 parent_->load_report_tracker_));
    }

    void RequestReresolution() override {
      parent_->channel_control_helper()->RequestReresolution();
    }

    absl::string_view GetAuthority() override {
      return parent_->channel_control_helper()->GetAuthority();
    }

    grpc_event_engine::experimental::EventEngine* GetEventEngine() override {
      return parent_->channel_control_helper()->GetEventEngine();
    }

    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override {
      parent_->channel_control_helper()->AddTraceEvent(severity, message);
    }

   private:
    RefCountedPtr<BackendMetricsLbPolicy> parent_;
  };

  class SubchannelCallTracker : public SubchannelCallTrackerInterface {
   public:
    explicit SubchannelCallTracker(LoadReportTracker* load_report_tracker)
        : load_report_tracker_(load_report_tracker) {}

    void Start() override {}

    void Finish(FinishArgs args) override {
      load_report_tracker_->RecordPerRpcLoadReport(
          args.backend_metric_accessor->GetBackendMetricData());
    }

   private:
    LoadReportTracker* load_report_tracker_;
  };

  void ShutdownLocked() override { delegate_.reset(); }

  OrphanablePtr<LoadBalancingPolicy> delegate_;
  LoadReportTracker* load_report_tracker_;
};

class BackendMetricsLbPolicyFactory : public LoadBalancingPolicyFactory {
 private:
  class BackendMetricsLbPolicyFactoryConfig
      : public LoadBalancingPolicy::Config {
   private:
    absl::string_view name() const override {
      return kBackendMetricsLbPolicyName;
    }
  };

  absl::string_view name() const override {
    return kBackendMetricsLbPolicyName;
  }

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<BackendMetricsLbPolicy>(std::move(args));
  }

  virtual absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return MakeRefCounted<BackendMetricsLbPolicyFactoryConfig>();
  }
};
}  // namespace

void RegisterBackendMetricsLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<BackendMetricsLbPolicyFactory>());
}

void LoadReportTracker::SetupOnChannel(ChannelArguments* arguments) {
  arguments->SetPointer(kMetricsTrackerAttribute, this);
}

void LoadReportTracker::RecordPerRpcLoadReport(
    const grpc_core::BackendMetricData* metrics_data) {
  if (metrics_data == nullptr) {
    per_rpc_load_reports_.emplace_back(absl::nullopt);
  } else {
    per_rpc_load_reports_.emplace_back(
        BackendMetricDataToOrcaLoadReport(metrics_data));
  }
}

void LoadReportTracker::AssertHasSinglePerRpcLoadReport(
    const xds::data::orca::v3::OrcaLoadReport& expected) {
  GPR_ASSERT(per_rpc_load_reports_.size() == 1);
  const auto& actual = per_rpc_load_reports_.front();
  GPR_ASSERT(actual.has_value());
  GPR_ASSERT(
      google::protobuf::util::MessageDifferencer::Equals(*actual, expected));
}

}  // namespace testing
}  // namespace grpc
