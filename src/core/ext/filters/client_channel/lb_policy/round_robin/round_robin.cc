//
// Copyright 2015 gRPC authors.
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
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_lb_round_robin_trace(false, "round_robin");

namespace {

//
// round_robin LB policy
//

constexpr absl::string_view kRoundRobin = "round_robin";

class RoundRobin : public LoadBalancingPolicy {
 public:
  explicit RoundRobin(Args args);

  absl::string_view name() const override { return kRoundRobin; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  class ChildList : public InternallyRefCounted<ChildList> {
   public:
    ChildList(RefCountedPtr<RoundRobin> round_robin,
              const ServerAddressList& addresses, const ChannelArgs& args);

    ~ChildList() override { round_robin_.reset(DEBUG_LOCATION, "ChildList"); }

    void Orphan() override {
      children_.clear();
      Unref();
    }

    size_t num_children() const { return children_.size(); }

    void ResetBackoffLocked();

   private:
    class ChildPolicy : public InternallyRefCounted<ChildPolicy> {
     public:
      ChildPolicy(RefCountedPtr<ChildList> child_list,
                  const ServerAddress& address, const ChannelArgs& args);

      ~ChildPolicy() override {
        child_list_.reset(DEBUG_LOCATION, "ChildPolicy");
      }

      void Orphan() override;

      size_t Index() const;

      void ResetBackoffLocked();

      absl::optional<grpc_connectivity_state> connectivity_state() const {
        return connectivity_state_;
      }
      RefCountedPtr<SubchannelPicker> picker() const { return picker_; }

     private:
      class Helper : public LoadBalancingPolicy::ChannelControlHelper {
       public:
        explicit Helper(RefCountedPtr<ChildPolicy> child)
            : child_(std::move(child)) {}

        ~Helper() override { child_.reset(DEBUG_LOCATION, "Helper"); }

        RefCountedPtr<SubchannelInterface> CreateSubchannel(
            ServerAddress address, const ChannelArgs& args) override;
        void UpdateState(grpc_connectivity_state state,
                         const absl::Status& status,
                         RefCountedPtr<SubchannelPicker> picker) override;
        void RequestReresolution() override;
        absl::string_view GetAuthority() override;
        grpc_event_engine::experimental::EventEngine* GetEventEngine() override;
        void AddTraceEvent(TraceSeverity severity,
                           absl::string_view message) override;

       private:
        LoadBalancingPolicy::ChannelControlHelper* parent_helper() const {
          return child_->child_list_->round_robin_->channel_control_helper();
        }

        RefCountedPtr<ChildPolicy> child_;
      };

      // Called when the child policy reports a connectivity state update.
      void OnStateUpdate(grpc_connectivity_state state,
                         const absl::Status& status,
                         RefCountedPtr<SubchannelPicker> picker);

      // Updates the logical connectivity state.
      void UpdateLogicalConnectivityStateLocked(
          grpc_connectivity_state connectivity_state);

      RefCountedPtr<ChildList> child_list_;

      OrphanablePtr<LoadBalancingPolicy> policy_;

      // The logical connectivity state of the child.
      // Note that the logical connectivity state may differ from the
      // actual reported state in some cases (e.g., after we see
      // TRANSIENT_FAILURE, we ignore any subsequent state changes until
      // we see READY).
      absl::optional<grpc_connectivity_state> connectivity_state_;

      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker_;
    };

    // Returns true if all children have seen their initial connectivity
    // state notification.
    bool AllChildrenSeenInitialState() const;

    // Updates the counters of children in each state when a
    // child transitions from old_state to new_state.
    void UpdateStateCountersLocked(
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state);

    // Ensures that the right child list is used and then updates
    // the RR policy's connectivity state based on the child list's
    // state counters.
    void MaybeUpdateRoundRobinConnectivityStateLocked(
        absl::Status status_for_tf);

    std::string CountersString() const {
      return absl::StrCat("num_children=", children_.size(),
                          " num_ready=", num_ready_,
                          " num_connecting=", num_connecting_,
                          " num_transient_failure=", num_transient_failure_);
    }

    RefCountedPtr<RoundRobin> round_robin_;

    std::vector<OrphanablePtr<ChildPolicy>> children_;

    size_t num_ready_ = 0;
    size_t num_connecting_ = 0;
    size_t num_transient_failure_ = 0;

    absl::Status last_failure_;
  };

  class Picker : public SubchannelPicker {
   public:
    Picker(RoundRobin* parent,
           std::vector<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>>
               pickers);

    PickResult Pick(PickArgs args) override;

   private:
    // Using pointer value only, no ref held -- do not dereference!
    RoundRobin* parent_;

    std::atomic<size_t> last_picked_index_;
    std::vector<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>> pickers_;
  };

  ~RoundRobin() override;

  void ShutdownLocked() override;

  // Current child list.
  OrphanablePtr<ChildList> child_list_;
  // Latest pending child list.
  // When we get an updated address list, we create a new child list
  // for it here, and we wait to swap it into child_list_ until the new
  // list becomes READY.
  OrphanablePtr<ChildList> latest_pending_child_list_;

  bool shutdown_ = false;

  absl::BitGen bit_gen_;
};

//
// RoundRobin::Picker
//

RoundRobin::Picker::Picker(
    RoundRobin* parent,
    std::vector<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>> pickers)
    : parent_(parent), pickers_(std::move(pickers)) {
  // For discussion on why we generate a random starting index for
  // the picker, see https://github.com/grpc/grpc-go/issues/2580.
  size_t index = absl::Uniform<size_t>(parent->bit_gen_, 0, pickers_.size());
  last_picked_index_.store(index, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO,
            "[RR %p picker %p] created picker from child_list=%p "
            "with %" PRIuPTR " READY children; last_picked_index_=%" PRIuPTR,
            parent_, this, parent_->child_list_.get(), pickers_.size(), index);
  }
}

RoundRobin::PickResult RoundRobin::Picker::Pick(PickArgs args) {
  size_t index = last_picked_index_.fetch_add(1, std::memory_order_relaxed) %
                 pickers_.size();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO,
            "[RR %p picker %p] using picker index %" PRIuPTR ", picker=%p",
            parent_, this, index, pickers_[index].get());
  }
  return pickers_[index]->Pick(std::move(args));
}

//
// RoundRobin
//

RoundRobin::RoundRobin(Args args) : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "[RR %p] Created", this);
  }
}

RoundRobin::~RoundRobin() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "[RR %p] Destroying Round Robin policy", this);
  }
  GPR_ASSERT(child_list_ == nullptr);
  GPR_ASSERT(latest_pending_child_list_ == nullptr);
}

void RoundRobin::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "[RR %p] Shutting down", this);
  }
  shutdown_ = true;
  child_list_.reset();
  latest_pending_child_list_.reset();
}

void RoundRobin::ResetBackoffLocked() {
  child_list_->ResetBackoffLocked();
  if (latest_pending_child_list_ != nullptr) {
    latest_pending_child_list_->ResetBackoffLocked();
  }
}

absl::Status RoundRobin::UpdateLocked(UpdateArgs args) {
  ServerAddressList addresses;
  if (args.addresses.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] received update with %" PRIuPTR " addresses",
              this, args.addresses->size());
    }
    addresses = std::move(*args.addresses);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] received update with address error: %s", this,
              args.addresses.status().ToString().c_str());
    }
    // If we already have a child list, then keep using the existing
    // list, but still report back that the update was not accepted.
    if (child_list_ != nullptr) return args.addresses.status();
  }
  // Create new child list, replacing the previous pending list, if any.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace) &&
      latest_pending_child_list_ != nullptr) {
    gpr_log(GPR_INFO, "[RR %p] replacing previous pending child list %p", this,
            latest_pending_child_list_.get());
  }
  latest_pending_child_list_ = MakeOrphanable<ChildList>(
      Ref(DEBUG_LOCATION, "ChildList"), std::move(addresses), args.args);
  // If the new list is empty, immediately promote it to
  // child_list_ and report TRANSIENT_FAILURE.
  if (latest_pending_child_list_->num_children() == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace) &&
        child_list_ != nullptr) {
      gpr_log(GPR_INFO, "[RR %p] replacing previous child list %p", this,
              child_list_.get());
    }
    child_list_ = std::move(latest_pending_child_list_);
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
  // child_list_ and report CONNECTING.
  if (child_list_.get() == nullptr) {
    child_list_ = std::move(latest_pending_child_list_);
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING, absl::Status(),
        MakeRefCounted<QueuePicker>(Ref(DEBUG_LOCATION, "QueuePicker")));
  }
  return absl::OkStatus();
}

//
// RoundRobin::ChildList::ChildPolicy::Helper
//

RefCountedPtr<SubchannelInterface>
RoundRobin::ChildList::ChildPolicy::Helper::CreateSubchannel(
    ServerAddress address, const ChannelArgs& args) {
  return parent_helper()->CreateSubchannel(std::move(address), args);
}

void RoundRobin::ChildList::ChildPolicy::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  child_->OnStateUpdate(state, status, std::move(picker));
}

void RoundRobin::ChildList::ChildPolicy::Helper::RequestReresolution() {
  parent_helper()->RequestReresolution();
}

absl::string_view RoundRobin::ChildList::ChildPolicy::Helper::GetAuthority() {
  return parent_helper()->GetAuthority();
}

grpc_event_engine::experimental::EventEngine*
RoundRobin::ChildList::ChildPolicy::Helper::GetEventEngine() {
  return parent_helper()->GetEventEngine();
}

void RoundRobin::ChildList::ChildPolicy::Helper::AddTraceEvent(
    TraceSeverity severity, absl::string_view message) {
  parent_helper()->AddTraceEvent(severity, message);
}

//
// RoundRobin::ChildList::ChildPolicy
//

RoundRobin::ChildList::ChildPolicy::ChildPolicy(
    RefCountedPtr<ChildList> child_list, const ServerAddress& address,
    const ChannelArgs& args)
    : child_list_(std::move(child_list)) {
  ChannelArgs child_args =
      args.Set(GRPC_ARG_INTERNAL_PICK_FIRST_ENABLE_HEALTH_CHECKING, true)
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_OMIT_STATUS_MESSAGE_PREFIX, true);
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = child_list_->round_robin_->work_serializer();
  lb_policy_args.args = child_args;
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  policy_ =
      CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
          "pick_first", std::move(lb_policy_args));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "[RR %p] child %p: created child policy %p",
            child_list_->round_robin_.get(), this, policy_.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(
      policy_->interested_parties(),
      child_list_->round_robin_->interested_parties());
  // Update child policy.
  UpdateArgs update_args;
  update_args.addresses.emplace().emplace_back(address);
  update_args.args = child_args;
  // TODO(roth): If the child reports a non-OK status with the update,
  // we need to propagate that back to the resolver somehow.
  (void)policy_->UpdateLocked(std::move(update_args));
}

void RoundRobin::ChildList::ChildPolicy::Orphan() {
  // Remove pollset_set linkage.
  grpc_pollset_set_del_pollset_set(
      policy_->interested_parties(),
      child_list_->round_robin_->interested_parties());
  policy_.reset();
  picker_.reset();
  Unref();
}

void RoundRobin::ChildList::ChildPolicy::ResetBackoffLocked() {
  if (policy_ != nullptr) policy_->ResetBackoffLocked();
}

size_t RoundRobin::ChildList::ChildPolicy::Index() const {
  for (size_t i = 0; i < child_list_->children_.size(); ++i) {
    if (child_list_->children_[i].get() == this) return i;
  }
  return -1;
}

void RoundRobin::ChildList::ChildPolicy::OnStateUpdate(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  RoundRobin* round_robin = child_list_->round_robin_.get();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO,
            "[RR %p] connectivity changed for child %p, child_list %p "
            "(index %" PRIuPTR " of %" PRIuPTR "): prev_state=%s new_state=%s",
            round_robin, this, child_list_.get(), Index(),
            child_list_->num_children(),
            (connectivity_state_.has_value()
                 ? ConnectivityStateName(*connectivity_state_)
                 : "N/A"),
            ConnectivityStateName(state));
  }
// FIXME: is this still right now that the child is pick_first?
  // If this is not the initial state notification and the new state is
  // TRANSIENT_FAILURE or IDLE, re-resolve.
  // Note that we don't want to do this on the initial state notification,
  // because that would result in an endless loop of re-resolution.
  if (connectivity_state_.has_value() &&
      (state == GRPC_CHANNEL_TRANSIENT_FAILURE || state == GRPC_CHANNEL_IDLE)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO,
              "[RR %p] child %p reported %s; requesting re-resolution",
              round_robin, this, ConnectivityStateName(state));
    }
    round_robin->channel_control_helper()->RequestReresolution();
  }
  if (state == GRPC_CHANNEL_IDLE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] child %p reported IDLE; requesting connection",
              round_robin, this);
    }
    policy_->ExitIdleLocked();
  }
  // Store picker.
  picker_ = std::move(picker);
  // Update logical connectivity state.
  UpdateLogicalConnectivityStateLocked(state);
  // Update the policy state.
  child_list_->MaybeUpdateRoundRobinConnectivityStateLocked(status);
}

void RoundRobin::ChildList::ChildPolicy::UpdateLogicalConnectivityStateLocked(
    grpc_connectivity_state connectivity_state) {
  RoundRobin* round_robin = child_list_->round_robin_.get();
  // Decide what state to report for aggregation purposes.
  // If the last logical state was TRANSIENT_FAILURE, then ignore the
  // state change unless the new state is READY.
  if (connectivity_state_.has_value() &&
      *connectivity_state_ == GRPC_CHANNEL_TRANSIENT_FAILURE &&
      connectivity_state != GRPC_CHANNEL_READY) {
    return;
  }
  // If the new state is IDLE, treat it as CONNECTING, since it will
  // immediately transition into CONNECTING anyway.
  if (connectivity_state == GRPC_CHANNEL_IDLE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO,
              "[RR %p] child %p, child_list %p (index %" PRIuPTR " of %" PRIuPTR
              "): treating IDLE as CONNECTING",
              round_robin, this, child_list_.get(), Index(),
              child_list_->num_children());
    }
    connectivity_state = GRPC_CHANNEL_CONNECTING;
  }
  // If no change, do nothing.
  if (connectivity_state_.has_value() &&
      *connectivity_state_ == connectivity_state) {
    return;
  }
  // Otherwise, update counters and logical state.
  child_list_->UpdateStateCountersLocked(connectivity_state_,
                                         connectivity_state);
  connectivity_state_ = connectivity_state;
}

//
// RoundRobin::ChildList
//

RoundRobin::ChildList::ChildList(RefCountedPtr<RoundRobin> round_robin,
                                 const ServerAddressList& addresses,
                                 const ChannelArgs& args)
    : round_robin_(std::move(round_robin)) {
  for (const ServerAddress& address : addresses) {
    children_.push_back(MakeOrphanable<ChildPolicy>(
        Ref(DEBUG_LOCATION, "ChildPolicy"), address, args));
  }
}

void RoundRobin::ChildList::ResetBackoffLocked() {
  for (const auto& child : children_) {
    child->ResetBackoffLocked();
  }
}

bool RoundRobin::ChildList::AllChildrenSeenInitialState() const {
  for (const auto& child : children_) {
    if (!child->connectivity_state().has_value()) return false;
  }
  return true;
}

void RoundRobin::ChildList::UpdateStateCountersLocked(
    absl::optional<grpc_connectivity_state> old_state,
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

void RoundRobin::ChildList::MaybeUpdateRoundRobinConnectivityStateLocked(
    absl::Status status_for_tf) {
  // If this is latest_pending_child_list_, then swap it into
  // child_list_ in the following cases:
  // - child_list_ has no READY children.
  // - This list has at least one READY child and we have seen the
  //   initial connectivity state notification for all children.
  // - All of the children in this list are in TRANSIENT_FAILURE.
  //   (This may cause the channel to go from READY to TRANSIENT_FAILURE,
  //   but we're doing what the control plane told us to do.)
  if (round_robin_->latest_pending_child_list_.get() == this &&
      (round_robin_->child_list_->num_ready_ == 0 ||
       (num_ready_ > 0 && AllChildrenSeenInitialState()) ||
       num_transient_failure_ == children_.size())) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      const std::string old_counters_string =
          round_robin_->child_list_ != nullptr
              ? round_robin_->child_list_->CountersString()
              : "";
      gpr_log(GPR_INFO,
              "[RR %p] swapping out child list %p (%s) in favor of %p (%s)",
              round_robin_.get(), round_robin_->child_list_.get(),
              old_counters_string.c_str(), this, CountersString().c_str());
    }
    round_robin_->child_list_ =
        std::move(round_robin_->latest_pending_child_list_);
  }
  // Only set connectivity state if this is the current child list.
  if (round_robin_->child_list_.get() != this) return;
// FIXME: scan children each time instead of keeping counters?
  // First matching rule wins:
  // 1) ANY child is READY => policy is READY.
  // 2) ANY child is CONNECTING => policy is CONNECTING.
  // 3) ALL children are TRANSIENT_FAILURE => policy is TRANSIENT_FAILURE.
  if (num_ready_ > 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] reporting READY with child list %p",
              round_robin_.get(), this);
    }
    std::vector<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>> pickers;
    for (const auto& child : children_) {
      auto state = child->connectivity_state();
      if (state.has_value() && *state == GRPC_CHANNEL_READY) {
        pickers.push_back(child->picker());
      }
    }
    GPR_ASSERT(!pickers.empty());
    round_robin_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_READY, absl::Status(),
        MakeRefCounted<Picker>(round_robin_.get(), std::move(pickers)));
  } else if (num_connecting_ > 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] reporting CONNECTING with child list %p",
              round_robin_.get(), this);
    }
    round_robin_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING, absl::Status(),
        MakeRefCounted<QueuePicker>(
            round_robin_->Ref(DEBUG_LOCATION, "QueuePicker")));
  } else if (num_transient_failure_ == children_.size()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO,
              "[RR %p] reporting TRANSIENT_FAILURE with child list %p: %s",
              round_robin_.get(), this, status_for_tf.ToString().c_str());
    }
    if (!status_for_tf.ok()) {
      last_failure_ = absl::UnavailableError(
          absl::StrCat("connections to all backends failing; last error: ",
                       status_for_tf.message()));
    }
    round_robin_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, last_failure_,
        MakeRefCounted<TransientFailurePicker>(last_failure_));
  }
}

//
// factory
//

class RoundRobinConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override { return kRoundRobin; }
};

class RoundRobinFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<RoundRobin>(std::move(args));
  }

  absl::string_view name() const override { return kRoundRobin; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<RoundRobinConfig>();
  }
};

}  // namespace

void RegisterRoundRobinLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<RoundRobinFactory>());
}

}  // namespace grpc_core
