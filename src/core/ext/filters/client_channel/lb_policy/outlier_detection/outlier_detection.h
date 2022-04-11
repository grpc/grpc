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
#include <limits.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/address_filtering.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

// Config for outlier detection LB policy.
class OutlierDetectionLbConfig : public LoadBalancingPolicy::Config {
 public:
  OutlierDetectionLbConfig(RefCountedPtr<LoadBalancingPolicy::Config> child_policy)
      : child_policy_(std::move(child_policy)) {}
  const char* name() const override;

  const RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const { return child_policy_; }

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
};

// outlier detection LB policy.
class OutlierDetectionLb : public LoadBalancingPolicy {
 public:
  explicit OutlierDetectionLb(Args args);

  const char* name() const override;

  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  // Each ChildOutlierDetection holds a ref to the OutlierDetectionLb.
  class ChildOutlierDetection
      : public InternallyRefCounted<ChildOutlierDetection> {
   public:
    ChildOutlierDetection(
        RefCountedPtr<OutlierDetectionLb> outlier_detection_policy,
        std::string name);

    ~ChildOutlierDetection() override {
      outlier_detection_policy_.reset(DEBUG_LOCATION, "ChildOutlierDetection");
    }

    const std::string& name() const { return name_; }

    void UpdateLocked(absl::StatusOr<ServerAddressList> addresses,
                      RefCountedPtr<LoadBalancingPolicy::Config> config,
                      bool ignore_reresolution_requests);
    void ExitIdleLocked();
    void ResetBackoffLocked();

    void Orphan() override;

    std::unique_ptr<SubchannelPicker> GetPicker() {
      return absl::make_unique<RefCountedPickerWrapper>(picker_wrapper_);
    }

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }

    const absl::Status& connectivity_status() const {
      return connectivity_status_;
    }

   private:
    // A simple wrapper for ref-counting a picker from the child policy.
    class RefCountedPicker : public RefCounted<RefCountedPicker> {
     public:
      explicit RefCountedPicker(std::unique_ptr<SubchannelPicker> picker)
          : picker_(std::move(picker)) {}
      PickResult Pick(PickArgs args) { return picker_->Pick(args); }

     private:
      std::unique_ptr<SubchannelPicker> picker_;
    };

    // A non-ref-counted wrapper for RefCountedPicker.
    class RefCountedPickerWrapper : public SubchannelPicker {
     public:
      explicit RefCountedPickerWrapper(RefCountedPtr<RefCountedPicker> picker)
          : picker_(std::move(picker)) {}
      PickResult Pick(PickArgs args) override { return picker_->Pick(args); }

     private:
      RefCountedPtr<RefCountedPicker> picker_;
    };

    class Helper : public ChannelControlHelper {
     public:
      explicit Helper(RefCountedPtr<ChildOutlierDetection> outlier_detection)
          : outlier_detection_(std::move(outlier_detection)) {}

      ~Helper() override { outlier_detection_.reset(DEBUG_LOCATION, "Helper"); }

      RefCountedPtr<SubchannelInterface> CreateSubchannel(
          ServerAddress address, const grpc_channel_args& args) override;
      void UpdateState(grpc_connectivity_state state,
                       const absl::Status& status,
                       std::unique_ptr<SubchannelPicker> picker) override;
      void RequestReresolution() override;
      absl::string_view GetAuthority() override;
      void AddTraceEvent(TraceSeverity severity,
                         absl::string_view message) override;

     private:
      RefCountedPtr<ChildOutlierDetection> outlier_detection_;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const grpc_channel_args* args);

    void OnConnectivityStateUpdateLocked(
        grpc_connectivity_state state, const absl::Status& status,
        std::unique_ptr<SubchannelPicker> picker);

    RefCountedPtr<OutlierDetectionLb> outlier_detection_policy_;
    const std::string name_;
    bool ignore_reresolution_requests_ = false;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_CONNECTING;
    absl::Status connectivity_status_;
    RefCountedPtr<RefCountedPicker> picker_wrapper_;
  };

  ~OutlierDetectionLb() override;

  void ShutdownLocked() override;

  void HandleChildConnectivityStateChangeLocked(ChildOutlierDetection* child);

  // Current channel args and config from the resolver.
  const grpc_channel_args* args_ = nullptr;
  RefCountedPtr<OutlierDetectionLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  bool update_in_progress_ = false;

  OrphanablePtr<ChildOutlierDetection> child_;
};
}  // namespace grpc_core
