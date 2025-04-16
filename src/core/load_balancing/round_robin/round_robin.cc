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

#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>
#include <stdlib.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/meta/type_traits.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/endpoint_list.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/json/json.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/work_serializer.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kRoundRobin = "round_robin";

class RoundRobin final : public LoadBalancingPolicy {
 public:
  explicit RoundRobin(Args args);

  absl::string_view name() const override { return kRoundRobin; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  class RoundRobinEndpointList final : public EndpointList {
   public:
    RoundRobinEndpointList(RefCountedPtr<RoundRobin> round_robin,
                           EndpointAddressesIterator* endpoints,
                           const ChannelArgs& args, std::string resolution_note,
                           std::vector<std::string>* errors)
        : EndpointList(std::move(round_robin), std::move(resolution_note),
                       GRPC_TRACE_FLAG_ENABLED(round_robin)
                           ? "RoundRobinEndpointList"
                           : nullptr) {
      Init(endpoints, args,
           [&](RefCountedPtr<EndpointList> endpoint_list,
               const EndpointAddresses& addresses, const ChannelArgs& args) {
             return MakeOrphanable<RoundRobinEndpoint>(
                 std::move(endpoint_list), addresses, args,
                 policy<RoundRobin>()->work_serializer(), errors);
           });
    }

   private:
    class RoundRobinEndpoint final : public Endpoint {
     public:
      RoundRobinEndpoint(RefCountedPtr<EndpointList> endpoint_list,
                         const EndpointAddresses& addresses,
                         const ChannelArgs& args,
                         std::shared_ptr<WorkSerializer> work_serializer,
                         std::vector<std::string>* errors)
          : Endpoint(std::move(endpoint_list)) {
        absl::Status status = Init(addresses, args, std::move(work_serializer));
        if (!status.ok()) {
          errors->emplace_back(absl::StrCat("endpoint ", addresses.ToString(),
                                            ": ", status.ToString()));
        }
      }

     private:
      // Called when the child policy reports a connectivity state update.
      void OnStateUpdate(std::optional<grpc_connectivity_state> old_state,
                         grpc_connectivity_state new_state,
                         const absl::Status& status) override;
    };

    LoadBalancingPolicy::ChannelControlHelper* channel_control_helper()
        const override {
      return policy<RoundRobin>()->channel_control_helper();
    }

    // Updates the counters of children in each state when a
    // child transitions from old_state to new_state.
    void UpdateStateCountersLocked(
        std::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state);

    // Ensures that the right child list is used and then updates
    // the RR policy's connectivity state based on the child list's
    // state counters.
    void MaybeUpdateRoundRobinConnectivityStateLocked(
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

  class Picker final : public SubchannelPicker {
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
  OrphanablePtr<RoundRobinEndpointList> endpoint_list_;
  // Latest pending child list.
  // When we get an updated address list, we create a new child list
  // for it here, and we wait to swap it into endpoint_list_ until the new
  // list becomes READY.
  OrphanablePtr<RoundRobinEndpointList> latest_pending_endpoint_list_;

  bool shutdown_ = false;
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
  size_t index = absl::Uniform<size_t>(SharedBitGen(), 0, pickers_.size());
  last_picked_index_.store(index, std::memory_order_relaxed);
  GRPC_TRACE_LOG(round_robin, INFO)
      << "[RR " << parent_ << " picker " << this
      << "] created picker from endpoint_list=" << parent_->endpoint_list_.get()
      << " with " << pickers_.size()
      << " READY children; last_picked_index_=" << index;
}

RoundRobin::PickResult RoundRobin::Picker::Pick(PickArgs args) {
  size_t index = last_picked_index_.fetch_add(1, std::memory_order_relaxed) %
                 pickers_.size();
  GRPC_TRACE_LOG(round_robin, INFO)
      << "[RR " << parent_ << " picker " << this << "] using picker index "
      << index << ", picker=" << pickers_[index].get();
  return pickers_[index]->Pick(args);
}

//
// RoundRobin
//

RoundRobin::RoundRobin(Args args) : LoadBalancingPolicy(std::move(args)) {
  GRPC_TRACE_LOG(round_robin, INFO) << "[RR " << this << "] Created";
}

RoundRobin::~RoundRobin() {
  GRPC_TRACE_LOG(round_robin, INFO)
      << "[RR " << this << "] Destroying Round Robin policy";
  CHECK(endpoint_list_ == nullptr);
  CHECK(latest_pending_endpoint_list_ == nullptr);
}

void RoundRobin::ShutdownLocked() {
  GRPC_TRACE_LOG(round_robin, INFO) << "[RR " << this << "] Shutting down";
  shutdown_ = true;
  endpoint_list_.reset();
  latest_pending_endpoint_list_.reset();
}

void RoundRobin::ResetBackoffLocked() {
  endpoint_list_->ResetBackoffLocked();
  if (latest_pending_endpoint_list_ != nullptr) {
    latest_pending_endpoint_list_->ResetBackoffLocked();
  }
}

absl::Status RoundRobin::UpdateLocked(UpdateArgs args) {
  EndpointAddressesIterator* addresses = nullptr;
  if (args.addresses.ok()) {
    GRPC_TRACE_LOG(round_robin, INFO) << "[RR " << this << "] received update";
    addresses = args.addresses->get();
  } else {
    GRPC_TRACE_LOG(round_robin, INFO)
        << "[RR " << this
        << "] received update with address error: " << args.addresses.status();
    // If we already have a child list, then keep using the existing
    // list, but still report back that the update was not accepted.
    if (endpoint_list_ != nullptr) return args.addresses.status();
  }
  // Create new child list, replacing the previous pending list, if any.
  if (GRPC_TRACE_FLAG_ENABLED(round_robin) &&
      latest_pending_endpoint_list_ != nullptr) {
    LOG(INFO) << "[RR " << this << "] replacing previous pending child list "
              << latest_pending_endpoint_list_.get();
  }
  std::vector<std::string> errors;
  latest_pending_endpoint_list_ = MakeOrphanable<RoundRobinEndpointList>(
      RefAsSubclass<RoundRobin>(DEBUG_LOCATION, "RoundRobinEndpointList"),
      addresses, args.args, std::move(args.resolution_note), &errors);
  // If the new list is empty, immediately promote it to
  // endpoint_list_ and report TRANSIENT_FAILURE.
  if (latest_pending_endpoint_list_->size() == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(round_robin) && endpoint_list_ != nullptr) {
      LOG(INFO) << "[RR " << this << "] replacing previous child list "
                << endpoint_list_.get();
    }
    endpoint_list_ = std::move(latest_pending_endpoint_list_);
    absl::Status status = args.addresses.ok()
                              ? absl::UnavailableError("empty address list")
                              : args.addresses.status();
    endpoint_list_->ReportTransientFailure(status);
    return status;
  }
  // Otherwise, if this is the initial update, immediately promote it to
  // endpoint_list_.
  if (endpoint_list_ == nullptr) {
    endpoint_list_ = std::move(latest_pending_endpoint_list_);
  }
  if (!errors.empty()) {
    return absl::UnavailableError(absl::StrCat(
        "errors from children: [", absl::StrJoin(errors, "; "), "]"));
  }
  return absl::OkStatus();
}

//
// RoundRobin::RoundRobinEndpointList::RoundRobinEndpoint
//

void RoundRobin::RoundRobinEndpointList::RoundRobinEndpoint::OnStateUpdate(
    std::optional<grpc_connectivity_state> old_state,
    grpc_connectivity_state new_state, const absl::Status& status) {
  auto* rr_endpoint_list = endpoint_list<RoundRobinEndpointList>();
  auto* round_robin = policy<RoundRobin>();
  GRPC_TRACE_LOG(round_robin, INFO)
      << "[RR " << round_robin << "] connectivity changed for child " << this
      << ", endpoint_list " << rr_endpoint_list << " (index " << Index()
      << " of " << rr_endpoint_list->size() << "): prev_state="
      << (old_state.has_value() ? ConnectivityStateName(*old_state) : "N/A")
      << " new_state=" << ConnectivityStateName(new_state) << " (" << status
      << ")";
  if (new_state == GRPC_CHANNEL_IDLE) {
    GRPC_TRACE_LOG(round_robin, INFO)
        << "[RR " << round_robin << "] child " << this
        << " reported IDLE; requesting connection";
    ExitIdleLocked();
  }
  // If state changed, update state counters.
  if (!old_state.has_value() || *old_state != new_state) {
    rr_endpoint_list->UpdateStateCountersLocked(old_state, new_state);
  }
  // Update the policy state.
  rr_endpoint_list->MaybeUpdateRoundRobinConnectivityStateLocked(status);
}

//
// RoundRobin::RoundRobinEndpointList
//

void RoundRobin::RoundRobinEndpointList::UpdateStateCountersLocked(
    std::optional<grpc_connectivity_state> old_state,
    grpc_connectivity_state new_state) {
  // We treat IDLE the same as CONNECTING, since it will immediately
  // transition into that state anyway.
  if (old_state.has_value()) {
    CHECK(*old_state != GRPC_CHANNEL_SHUTDOWN);
    if (*old_state == GRPC_CHANNEL_READY) {
      CHECK_GT(num_ready_, 0u);
      --num_ready_;
    } else if (*old_state == GRPC_CHANNEL_CONNECTING ||
               *old_state == GRPC_CHANNEL_IDLE) {
      CHECK_GT(num_connecting_, 0u);
      --num_connecting_;
    } else if (*old_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      CHECK_GT(num_transient_failure_, 0u);
      --num_transient_failure_;
    }
  }
  CHECK(new_state != GRPC_CHANNEL_SHUTDOWN);
  if (new_state == GRPC_CHANNEL_READY) {
    ++num_ready_;
  } else if (new_state == GRPC_CHANNEL_CONNECTING ||
             new_state == GRPC_CHANNEL_IDLE) {
    ++num_connecting_;
  } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ++num_transient_failure_;
  }
}

void RoundRobin::RoundRobinEndpointList::
    MaybeUpdateRoundRobinConnectivityStateLocked(absl::Status status_for_tf) {
  auto* round_robin = policy<RoundRobin>();
  // If this is latest_pending_endpoint_list_, then swap it into
  // endpoint_list_ in the following cases:
  // - endpoint_list_ has no READY children.
  // - This list has at least one READY child and we have seen the
  //   initial connectivity state notification for all children.
  // - All of the children in this list are in TRANSIENT_FAILURE.
  //   (This may cause the channel to go from READY to TRANSIENT_FAILURE,
  //   but we're doing what the control plane told us to do.)
  if (round_robin->latest_pending_endpoint_list_.get() == this &&
      (round_robin->endpoint_list_->num_ready_ == 0 ||
       (num_ready_ > 0 && AllEndpointsSeenInitialState()) ||
       num_transient_failure_ == size())) {
    if (GRPC_TRACE_FLAG_ENABLED(round_robin)) {
      LOG(INFO) << "[RR " << round_robin << "] swapping out child list "
                << round_robin->endpoint_list_.get() << " ("
                << round_robin->endpoint_list_->CountersString()
                << ") in favor of " << this << " (" << CountersString() << ")";
    }
    round_robin->endpoint_list_ =
        std::move(round_robin->latest_pending_endpoint_list_);
  }
  // Only set connectivity state if this is the current child list.
  if (round_robin->endpoint_list_.get() != this) return;
  // First matching rule wins:
  // 1) ANY child is READY => policy is READY.
  // 2) ANY child is CONNECTING => policy is CONNECTING.
  // 3) ALL children are TRANSIENT_FAILURE => policy is TRANSIENT_FAILURE.
  if (num_ready_ > 0) {
    GRPC_TRACE_LOG(round_robin, INFO)
        << "[RR " << round_robin << "] reporting READY with child list "
        << this;
    std::vector<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>> pickers;
    for (const auto& endpoint : endpoints()) {
      auto state = endpoint->connectivity_state();
      if (state.has_value() && *state == GRPC_CHANNEL_READY) {
        pickers.push_back(endpoint->picker());
      }
    }
    CHECK(!pickers.empty());
    round_robin->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_READY, absl::OkStatus(),
        MakeRefCounted<Picker>(round_robin, std::move(pickers)));
  } else if (num_connecting_ > 0) {
    GRPC_TRACE_LOG(round_robin, INFO)
        << "[RR " << round_robin << "] reporting CONNECTING with child list "
        << this;
    round_robin->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING, absl::OkStatus(),
        MakeRefCounted<QueuePicker>(nullptr));
  } else if (num_transient_failure_ == size()) {
    GRPC_TRACE_LOG(round_robin, INFO)
        << "[RR " << round_robin
        << "] reporting TRANSIENT_FAILURE with child list " << this << ": "
        << status_for_tf;
    if (!status_for_tf.ok()) {
      last_failure_ = absl::UnavailableError(
          absl::StrCat("connections to all backends failing; last error: ",
                       status_for_tf.message()));
    }
    ReportTransientFailure(last_failure_);
  }
}

//
// factory
//

class RoundRobinConfig final : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override { return kRoundRobin; }
};

class RoundRobinFactory final : public LoadBalancingPolicyFactory {
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
