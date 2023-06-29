//
// Copyright 2018 gRPC authors.
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

#include "src/core/ext/filters/client_channel/lb_policy/ring_hash/ring_hash.h"

#include <inttypes.h>
#include <stdlib.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/support/json.h>

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <grpc/grpc.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel_internal.h"
#include "src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/delegating_helper.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_lb_ring_hash_trace(false, "ring_hash_lb");

UniqueTypeName RequestHashAttribute::TypeName() {
  static UniqueTypeName::Factory kFactory("request_hash");
  return kFactory.Create();
}

// Helper Parser method

const JsonLoaderInterface* RingHashConfig::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<RingHashConfig>()
          .OptionalField("minRingSize", &RingHashConfig::min_ring_size)
          .OptionalField("maxRingSize", &RingHashConfig::max_ring_size)
          .Finish();
  return loader;
}

void RingHashConfig::JsonPostLoad(const Json&, const JsonArgs&,
                                  ValidationErrors* errors) {
  {
    ValidationErrors::ScopedField field(errors, ".minRingSize");
    if (!errors->FieldHasErrors() &&
        (min_ring_size == 0 || min_ring_size > 8388608)) {
      errors->AddError("must be in the range [1, 8388608]");
    }
  }
  {
    ValidationErrors::ScopedField field(errors, ".maxRingSize");
    if (!errors->FieldHasErrors() &&
        (max_ring_size == 0 || max_ring_size > 8388608)) {
      errors->AddError("must be in the range [1, 8388608]");
    }
  }
  if (min_ring_size > max_ring_size) {
    errors->AddError("max_ring_size cannot be smaller than min_ring_size");
  }
}

namespace {

constexpr absl::string_view kRingHash = "ring_hash_experimental";

class RingHashLbConfig : public LoadBalancingPolicy::Config {
 public:
  RingHashLbConfig(size_t min_ring_size, size_t max_ring_size)
      : min_ring_size_(min_ring_size), max_ring_size_(max_ring_size) {}
  absl::string_view name() const override { return kRingHash; }
  size_t min_ring_size() const { return min_ring_size_; }
  size_t max_ring_size() const { return max_ring_size_; }

 private:
  size_t min_ring_size_;
  size_t max_ring_size_;
};

//
// ring_hash LB policy
//

constexpr size_t kRingSizeCapDefault = 4096;

class RingHash : public LoadBalancingPolicy {
 public:
  explicit RingHash(Args args);

  absl::string_view name() const override { return kRingHash; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  // A ring computed based on a config and address list.
  class Ring : public RefCounted<Ring> {
   public:
    struct RingEntry {
      uint64_t hash;
      size_t endpoint_index;  // Index into RingHash::endpoints_.
    };

    Ring(RingHash* ring_hash, RingHashLbConfig* config);

    const std::vector<RingEntry>& ring() const { return ring_; }

   private:
    std::vector<RingEntry> ring_;
  };

  // State for a particular endpoint.  Delegates to a pick_first child policy.
  class RingHashEndpoint : public InternallyRefCounted<RingHashEndpoint> {
   public:
    // index is the index into RingHash::endpoints_ of this endpoint.
    RingHashEndpoint(RefCountedPtr<RingHash> ring_hash, size_t index)
        : ring_hash_(std::move(ring_hash)), index_(index) {}

    void Orphan() override;

    size_t index() const { return index_; }

    void UpdateLocked(size_t index);

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }

    // Returns info about the endpoint to be stored in the picker.
    struct EndpointInfo {
      RefCountedPtr<RingHashEndpoint> endpoint;
      RefCountedPtr<SubchannelPicker> picker;
      grpc_connectivity_state state;
      absl::Status status;
    };
    EndpointInfo GetInfoForPicker() {
      return {Ref(), picker_, connectivity_state_, status_};
    }

    void ResetBackoffLocked();

    // If the child policy does not yet exist, creates it; otherwise,
    // asks the child to exit IDLE.
    void RequestConnectionLocked();

   private:
    class Helper;

    void CreateChildPolicy();
    void UpdateChildPolicyLocked();

    // Called when the child policy reports a connectivity state update.
    void OnStateUpdate(grpc_connectivity_state new_state,
                       const absl::Status& status,
                       RefCountedPtr<SubchannelPicker> picker);

    // Ref to our parent.
    RefCountedPtr<RingHash> ring_hash_;
    size_t index_;  // Index into RingHash::endpoints_ of this endpoint.

    // The pick_first child policy.
    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
    absl::Status status_;
    RefCountedPtr<SubchannelPicker> picker_;
  };

  class Picker : public SubchannelPicker {
   public:
    explicit Picker(RefCountedPtr<RingHash> ring_hash)
        : ring_hash_(std::move(ring_hash)),
          ring_(ring_hash_->ring_),
          endpoints_(ring_hash_->endpoints_.size()) {
      for (const auto& p : ring_hash_->endpoint_map_) {
        endpoints_[p.second->index()] = p.second->GetInfoForPicker();
      }
    }

    PickResult Pick(PickArgs args) override;

   private:
    // A fire-and-forget class that schedules endpoint connection attempts
    // on the control plane WorkSerializer.
    class EndpointConnectionAttempter {
     public:
      EndpointConnectionAttempter(RefCountedPtr<RingHash> ring_hash,
                                  RefCountedPtr<RingHashEndpoint> endpoint)
          : ring_hash_(std::move(ring_hash)), endpoint_(std::move(endpoint)) {
        // Hop into ExecCtx, so that we're not holding the data plane mutex
        // while we run control-plane code.
        GRPC_CLOSURE_INIT(&closure_, RunInExecCtx, this, nullptr);
        ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
      }

     private:
      static void RunInExecCtx(void* arg, grpc_error_handle /*error*/) {
        auto* self = static_cast<EndpointConnectionAttempter*>(arg);
        self->ring_hash_->work_serializer()->Run(
            [self]() {
              if (!self->ring_hash_->shutdown_) {
                self->endpoint_->RequestConnectionLocked();
              }
              delete self;
            },
            DEBUG_LOCATION);
      }

      RefCountedPtr<RingHash> ring_hash_;
      RefCountedPtr<RingHashEndpoint> endpoint_;
      grpc_closure closure_;
    };

    RefCountedPtr<RingHash> ring_hash_;
    RefCountedPtr<Ring> ring_;
    std::vector<RingHashEndpoint::EndpointInfo> endpoints_;
  };

  ~RingHash() override;

  void ShutdownLocked() override;

  // Updates the aggregate policy's connectivity state based on the
  // endpoint list's state counters, creating a new picker.
  // entered_transient_failure is true if the endpoint has just
  // entered TRANSIENT_FAILURE state.
  // If the call to this method is triggered by an endpoint entering
  // TRANSIENT_FAILURE, then status is the status reported by the endpoint.
  void UpdateAggregatedConnectivityStateLocked(bool entered_transient_failure,
                                               absl::Status status);

  // Current endpoint list, channel args, and ring.
  EndpointAddressesList endpoints_;
  ChannelArgs args_;
  RefCountedPtr<Ring> ring_;

  std::map<EndpointAddressSet, OrphanablePtr<RingHashEndpoint>> endpoint_map_;

  // TODO(roth): If we ever change the helper UpdateState() API to not
  // need the status reported for TRANSIENT_FAILURE state (because
  // it's not currently actually used for anything outside of the picker),
  // then we will no longer need this data member.
  absl::Status last_failure_;

  // indicating if we are shutting down.
  bool shutdown_ = false;
};

//
// RingHash::Picker
//

RingHash::PickResult RingHash::Picker::Pick(PickArgs args) {
  auto* call_state = static_cast<ClientChannelLbCallState*>(args.call_state);
  auto* hash_attribute = static_cast<RequestHashAttribute*>(
      call_state->GetCallAttribute(RequestHashAttribute::TypeName()));
  absl::string_view hash;
  if (hash_attribute != nullptr) {
    hash = hash_attribute->request_hash();
  }
  uint64_t h;
  if (!absl::SimpleAtoi(hash, &h)) {
    return PickResult::Fail(
        absl::InternalError("ring hash value is not a number"));
  }
  const auto& ring = ring_->ring();
  // Find the index in the ring to use for this RPC.
  // Ported from https://github.com/RJ/ketama/blob/master/libketama/ketama.c
  // (ketama_get_server) NOTE: The algorithm depends on using signed integers
  // for lowp, highp, and index. Do not change them!
  int64_t lowp = 0;
  int64_t highp = ring.size();
  int64_t index = 0;
  while (true) {
    index = (lowp + highp) / 2;
    if (index == static_cast<int64_t>(ring.size())) {
      index = 0;
      break;
    }
    uint64_t midval = ring[index].hash;
    uint64_t midval1 = index == 0 ? 0 : ring[index - 1].hash;
    if (h <= midval && h > midval1) {
      break;
    }
    if (midval < h) {
      lowp = index + 1;
    } else {
      highp = index - 1;
    }
    if (lowp > highp) {
      index = 0;
      break;
    }
  }
  // Find the first endpoint we can use from the selected index.
  for (size_t i = 0; i < ring.size(); ++i) {
    const auto& entry = ring[(index + i) % ring.size()];
    const auto& endpoint_info = endpoints_[entry.endpoint_index];
    switch (endpoint_info.state) {
      case GRPC_CHANNEL_READY:
        return endpoint_info.picker->Pick(args);
      case GRPC_CHANNEL_IDLE:
        new EndpointConnectionAttempter(
            ring_hash_->Ref(DEBUG_LOCATION, "EndpointConnectionAttempter"),
            endpoint_info.endpoint);
        ABSL_FALLTHROUGH_INTENDED;
      case GRPC_CHANNEL_CONNECTING:
        return PickResult::Queue();
      default:
        break;
    }
  }
  return PickResult::Fail(absl::UnavailableError(absl::StrCat(
      "ring hash cannot find a connected endpoint; first failure: ",
      endpoints_[ring[index].endpoint_index].status.message())));
}

//
// RingHash::Ring
//

RingHash::Ring::Ring(RingHash* ring_hash, RingHashLbConfig* config) {
  // Store the weights while finding the sum.
  struct EndpointWeight {
    std::string address;  // Key by endpoint's first address.
    // Default weight is 1 for the cases where a weight is not provided,
    // each occurrence of the address will be counted a weight value of 1.
    uint32_t weight = 1;
    double normalized_weight;
  };
  std::vector<EndpointWeight> endpoint_weights;
  size_t sum = 0;
  const EndpointAddressesList& endpoints = ring_hash->endpoints_;
  endpoint_weights.reserve(endpoints.size());
  for (const auto& endpoint : endpoints) {
    EndpointWeight endpoint_weight;
    endpoint_weight.address =
        grpc_sockaddr_to_string(&endpoint.addresses().front(), false).value();
    // Weight should never be zero, but ignore it just in case, since
    // that value would screw up the ring-building algorithm.
    auto weight_arg = endpoint.args().GetInt(GRPC_ARG_ADDRESS_WEIGHT);
    if (weight_arg.value_or(0) > 0) {
      endpoint_weight.weight = *weight_arg;
    }
    sum += endpoint_weight.weight;
    endpoint_weights.push_back(std::move(endpoint_weight));
  }
  // Calculating normalized weights and find min and max.
  double min_normalized_weight = 1.0;
  double max_normalized_weight = 0.0;
  for (auto& endpoint_weight : endpoint_weights) {
    endpoint_weight.normalized_weight =
        static_cast<double>(endpoint_weight.weight) / sum;
    min_normalized_weight =
        std::min(endpoint_weight.normalized_weight, min_normalized_weight);
    max_normalized_weight =
        std::max(endpoint_weight.normalized_weight, max_normalized_weight);
  }
  // Scale up the number of hashes per host such that the least-weighted host
  // gets a whole number of hashes on the ring. Other hosts might not end up
  // with whole numbers, and that's fine (the ring-building algorithm below can
  // handle this). This preserves the original implementation's behavior: when
  // weights aren't provided, all hosts should get an equal number of hashes. In
  // the case where this number exceeds the max_ring_size, it's scaled back down
  // to fit.
  const size_t ring_size_cap =
      ring_hash->args_.GetInt(GRPC_ARG_RING_HASH_LB_RING_SIZE_CAP)
          .value_or(kRingSizeCapDefault);
  const size_t min_ring_size = std::min(config->min_ring_size(), ring_size_cap);
  const size_t max_ring_size = std::min(config->max_ring_size(), ring_size_cap);
  const double scale = std::min(
      std::ceil(min_normalized_weight * min_ring_size) / min_normalized_weight,
      static_cast<double>(max_ring_size));
  // Reserve memory for the entire ring up front.
  const uint64_t ring_size = std::ceil(scale);
  ring_.reserve(ring_size);
  // Populate the hash ring by walking through the (host, weight) pairs in
  // normalized_host_weights, and generating (scale * weight) hashes for each
  // host. Since these aren't necessarily whole numbers, we maintain running
  // sums -- current_hashes and target_hashes -- which allows us to populate the
  // ring in a mostly stable way.
  absl::InlinedVector<char, 196> hash_key_buffer;
  double current_hashes = 0.0;
  double target_hashes = 0.0;
  uint64_t min_hashes_per_host = ring_size;
  uint64_t max_hashes_per_host = 0;
  for (size_t i = 0; i < endpoints.size(); ++i) {
    const std::string& address_string = endpoint_weights[i].address;
    hash_key_buffer.assign(address_string.begin(), address_string.end());
    hash_key_buffer.emplace_back('_');
    auto offset_start = hash_key_buffer.end();
    target_hashes += scale * endpoint_weights[i].normalized_weight;
    size_t count = 0;
    while (current_hashes < target_hashes) {
      const std::string count_str = absl::StrCat(count);
      hash_key_buffer.insert(offset_start, count_str.begin(), count_str.end());
      absl::string_view hash_key(hash_key_buffer.data(),
                                 hash_key_buffer.size());
      const uint64_t hash = XXH64(hash_key.data(), hash_key.size(), 0);
      ring_.push_back({hash, i});
      ++count;
      ++current_hashes;
      hash_key_buffer.erase(offset_start, hash_key_buffer.end());
    }
    min_hashes_per_host =
        std::min(static_cast<uint64_t>(i), min_hashes_per_host);
    max_hashes_per_host =
        std::max(static_cast<uint64_t>(i), max_hashes_per_host);
  }
  std::sort(ring_.begin(), ring_.end(),
            [](const RingEntry& lhs, const RingEntry& rhs) -> bool {
              return lhs.hash < rhs.hash;
            });
}

//
// RingHash::RingHashEndpoint::Helper
//

class RingHash::RingHashEndpoint::Helper
    : public LoadBalancingPolicy::DelegatingChannelControlHelper {
 public:
  explicit Helper(RefCountedPtr<RingHashEndpoint> endpoint)
      : endpoint_(std::move(endpoint)) {}

  ~Helper() override { endpoint_.reset(DEBUG_LOCATION, "Helper"); }

  void UpdateState(
      grpc_connectivity_state state, const absl::Status& status,
      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) override {
    endpoint_->OnStateUpdate(state, status, std::move(picker));
  }

 private:
  LoadBalancingPolicy::ChannelControlHelper* parent_helper() const override {
    return endpoint_->ring_hash_->channel_control_helper();
  }

  RefCountedPtr<RingHashEndpoint> endpoint_;
};

//
// RingHash::RingHashEndpoint
//

void RingHash::RingHashEndpoint::Orphan() {
  if (child_policy_ != nullptr) {
    // Remove pollset_set linkage.
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     ring_hash_->interested_parties());
    child_policy_.reset();
    picker_.reset();
  }
  Unref();
}

void RingHash::RingHashEndpoint::UpdateLocked(size_t index) {
  index_ = index;
  if (child_policy_ != nullptr) UpdateChildPolicyLocked();
}

void RingHash::RingHashEndpoint::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void RingHash::RingHashEndpoint::RequestConnectionLocked() {
  if (child_policy_ == nullptr) {
    CreateChildPolicy();
  } else {
    child_policy_->ExitIdleLocked();
  }
}

void RingHash::RingHashEndpoint::CreateChildPolicy() {
  GPR_ASSERT(child_policy_ == nullptr);
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = ring_hash_->work_serializer();
  lb_policy_args.args =
      ring_hash_->args_
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_ENABLE_HEALTH_CHECKING, true)
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_OMIT_STATUS_MESSAGE_PREFIX, true);
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  child_policy_ =
      CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
          "pick_first", std::move(lb_policy_args));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    const EndpointAddresses& endpoint = ring_hash_->endpoints_[index_];
    gpr_log(GPR_INFO,
            "[RH %p] endpoint %p (index %" PRIuPTR " of %" PRIuPTR
            ", %s): created child policy %p",
            ring_hash_.get(), this, index_, ring_hash_->endpoints_.size(),
            endpoint.ToString().c_str(), child_policy_.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                   ring_hash_->interested_parties());
  UpdateChildPolicyLocked();
}

void RingHash::RingHashEndpoint::UpdateChildPolicyLocked() {
  // Construct pick_first config.
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray(
              {Json::FromObject({{"pick_first", Json::FromObject({})}})}));
  GPR_ASSERT(config.ok());
  // Update child policy.
  LoadBalancingPolicy::UpdateArgs update_args;
  update_args.addresses.emplace().emplace_back(ring_hash_->endpoints_[index_]);
  update_args.args = ring_hash_->args_;
  update_args.config = std::move(*config);
  // TODO(roth): If the child reports a non-OK status with the update,
  // we need to propagate that back to the resolver somehow.
  (void)child_policy_->UpdateLocked(std::move(update_args));
}

void RingHash::RingHashEndpoint::OnStateUpdate(
    grpc_connectivity_state new_state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    gpr_log(
        GPR_INFO,
        "[RH %p] connectivity changed for endpoint %p (%s, child_policy=%p): "
        "prev_state=%s new_state=%s (%s)",
        ring_hash_.get(), this,
        ring_hash_->endpoints_[index_].ToString().c_str(), child_policy_.get(),
        ConnectivityStateName(connectivity_state_),
        ConnectivityStateName(new_state), status.ToString().c_str());
  }
  if (child_policy_ == nullptr) return;  // Already orphaned.
  // Update state.
  const bool entered_transient_failure =
      connectivity_state_ != GRPC_CHANNEL_TRANSIENT_FAILURE &&
      new_state == GRPC_CHANNEL_TRANSIENT_FAILURE;
  connectivity_state_ = new_state;
  status_ = status;
  picker_ = std::move(picker);
  // Update the aggregated connectivity state.
  ring_hash_->UpdateAggregatedConnectivityStateLocked(entered_transient_failure,
                                                      status);
}

//
// RingHash
//

RingHash::RingHash(Args args) : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    gpr_log(GPR_INFO, "[RH %p] Created", this);
  }
}

RingHash::~RingHash() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    gpr_log(GPR_INFO, "[RH %p] Destroying Ring Hash policy", this);
  }
}

void RingHash::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    gpr_log(GPR_INFO, "[RH %p] Shutting down", this);
  }
  shutdown_ = true;
  endpoint_map_.clear();
}

void RingHash::ResetBackoffLocked() {
  for (const auto& p : endpoint_map_) {
    p.second->ResetBackoffLocked();
  }
}

absl::Status RingHash::UpdateLocked(UpdateArgs args) {
  // Check address list.
  if (args.addresses.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
      gpr_log(GPR_INFO, "[RH %p] received update with %" PRIuPTR " addresses",
              this, args.addresses->size());
    }
    endpoints_ = *std::move(args.addresses);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
      gpr_log(GPR_INFO, "[RH %p] received update with addresses error: %s",
              this, args.addresses.status().ToString().c_str());
    }
    // If we already have an endpoint list, then keep using the existing
    // list, but still report back that the update was not accepted.
    if (!endpoints_.empty()) return args.addresses.status();
  }
  // Save channel args.
  args_ = std::move(args.args);
  // Build new ring.
  ring_ = MakeRefCounted<Ring>(
      this, static_cast<RingHashLbConfig*>(args.config.get()));
  // Update endpoint map.
  std::map<EndpointAddressSet, OrphanablePtr<RingHashEndpoint>> endpoint_map;
  for (size_t i = 0; i < endpoints_.size(); ++i) {
    const EndpointAddresses& addresses = endpoints_[i];
    const EndpointAddressSet address_set(addresses.addresses());
    // If present in old map, retain it; otherwise, create a new one.
    auto it = endpoint_map_.find(address_set);
    if (it != endpoint_map_.end()) {
      it->second->UpdateLocked(i);
      endpoint_map.emplace(address_set, std::move(it->second));
    } else {
      endpoint_map.emplace(address_set,
                           MakeOrphanable<RingHashEndpoint>(Ref(), i));
    }
  }
  endpoint_map_ = std::move(endpoint_map);
  // If the address list is empty, report TRANSIENT_FAILURE.
  if (endpoints_.empty()) {
    absl::Status status =
        args.addresses.ok() ? absl::UnavailableError(absl::StrCat(
                                  "empty address list: ", args.resolution_note))
                            : args.addresses.status();
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        MakeRefCounted<TransientFailurePicker>(status));
    return status;
  }
  // Return a new picker.
  UpdateAggregatedConnectivityStateLocked(/*entered_transient_failure=*/false,
                                          absl::OkStatus());
  return absl::OkStatus();
}

void RingHash::UpdateAggregatedConnectivityStateLocked(
    bool entered_transient_failure, absl::Status status) {
  // Count the number of endpoints in each state.
  size_t num_idle = 0;
  size_t num_connecting = 0;
  size_t num_ready = 0;
  size_t num_transient_failure = 0;
  for (const auto& p : endpoint_map_) {
    switch (p.second->connectivity_state()) {
      case GRPC_CHANNEL_READY:
        ++num_ready;
        break;
      case GRPC_CHANNEL_IDLE:
        ++num_idle;
        break;
      case GRPC_CHANNEL_CONNECTING:
        ++num_connecting;
        break;
      case GRPC_CHANNEL_TRANSIENT_FAILURE:
        ++num_transient_failure;
        break;
      default:
        Crash("child policy should never report SHUTDOWN");
    }
  }
  // The overall aggregation rules here are:
  // 1. If there is at least one endpoint in READY state, report READY.
  // 2. If there are 2 or more endpoints in TRANSIENT_FAILURE state, report
  //    TRANSIENT_FAILURE.
  // 3. If there is at least one endpoint in CONNECTING state, report
  //    CONNECTING.
  // 4. If there is one endpoint in TRANSIENT_FAILURE state and there is
  //    more than one endpoint, report CONNECTING.
  // 5. If there is at least one endpoint in IDLE state, report IDLE.
  // 6. Otherwise, report TRANSIENT_FAILURE.
  //
  // We set start_connection_attempt to true if we match rules 2, 4, or 6.
  grpc_connectivity_state state;
  bool start_connection_attempt = false;
  if (num_ready > 0) {
    state = GRPC_CHANNEL_READY;
  } else if (num_transient_failure >= 2) {
    state = GRPC_CHANNEL_TRANSIENT_FAILURE;
    start_connection_attempt = true;
  } else if (num_connecting > 0) {
    state = GRPC_CHANNEL_CONNECTING;
  } else if (num_transient_failure == 1 && endpoints_.size() > 1) {
    state = GRPC_CHANNEL_CONNECTING;
    start_connection_attempt = true;
  } else if (num_idle > 0) {
    state = GRPC_CHANNEL_IDLE;
  } else {
    state = GRPC_CHANNEL_TRANSIENT_FAILURE;
    start_connection_attempt = true;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    gpr_log(GPR_INFO,
            "[RH %p] setting connectivity state to %s (num_idle=%" PRIuPTR
            ", num_connecting=%" PRIuPTR ", num_ready=%" PRIuPTR
            ", num_transient_failure=%" PRIuPTR ", size=%" PRIuPTR
            ") -- start_connection_attempt=%d",
            this, ConnectivityStateName(state), num_idle, num_connecting,
            num_ready, num_transient_failure, endpoints_.size(),
            start_connection_attempt);
  }
  // In TRANSIENT_FAILURE, report the last reported failure.
  // Otherwise, report OK.
  if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    if (!status.ok()) {
      last_failure_ = absl::UnavailableError(absl::StrCat(
          "no reachable endpoints; last error: ", status.message()));
    }
    status = last_failure_;
  } else {
    status = absl::OkStatus();
  }
  // Generate new picker and return it to the channel.
  // Note that we use our own picker regardless of connectivity state.
  channel_control_helper()->UpdateState(
      state, status,
      MakeRefCounted<Picker>(Ref(DEBUG_LOCATION, "RingHashPicker")));
  // While the ring_hash policy is reporting TRANSIENT_FAILURE, it will
  // not be getting any pick requests from the priority policy.
  // However, because the ring_hash policy does not attempt to
  // reconnect to endpoints unless it is getting pick requests,
  // it will need special handling to ensure that it will eventually
  // recover from TRANSIENT_FAILURE state once the problem is resolved.
  // Specifically, it will make sure that it is attempting to connect to
  // at least one endpoint at any given time.  But we don't want to just
  // try to connect to only one endpoint, because if that particular
  // endpoint happens to be down but the rest are reachable, we would
  // incorrectly fail to recover.
  //
  // So, to handle this, whenever an endpoint initially enters
  // TRANSIENT_FAILURE state (i.e., its initial connection attempt has
  // failed), if there are no endpoints currently in CONNECTING state
  // (i.e., they are still trying their initial connection attempt),
  // then we will trigger a connection attempt for the first endpoint
  // that is currently in state IDLE, if any.
  //
  // Note that once an endpoint enters TRANSIENT_FAILURE state, it will
  // stay in that state and automatically retry after appropriate backoff,
  // never stopping until it establishes a connection.  This means that
  // if we stay in TRANSIENT_FAILURE for a long period of time, we will
  // eventually be trying *all* endpoints, which probably isn't ideal.
  // But it's no different than what can happen if ring_hash is the root
  // LB policy and we keep getting picks, so it's not really a new
  // problem.  If/when it becomes an issue, we can figure out how to
  // address it.
  //
  // Note that we do the same thing when the policy is in state
  // CONNECTING, just to ensure that we don't remain in CONNECTING state
  // indefinitely if there are no new picks coming in.
  if (start_connection_attempt && entered_transient_failure) {
    size_t first_idle_index = endpoints_.size();
    for (size_t i = 0; i < endpoints_.size(); ++i) {
      auto it =
          endpoint_map_.find(EndpointAddressSet(endpoints_[i].addresses()));
      GPR_ASSERT(it != endpoint_map_.end());
      if (it->second->connectivity_state() == GRPC_CHANNEL_CONNECTING) {
        first_idle_index = endpoints_.size();
        break;
      }
      if (first_idle_index == endpoints_.size() &&
          it->second->connectivity_state() == GRPC_CHANNEL_IDLE) {
        first_idle_index = i;
      }
    }
    if (first_idle_index != endpoints_.size()) {
      auto it = endpoint_map_.find(
          EndpointAddressSet(endpoints_[first_idle_index].addresses()));
      GPR_ASSERT(it != endpoint_map_.end());
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
        gpr_log(GPR_INFO,
                "[RH %p] triggering internal connection attempt for endpoint "
                "%p (%s) (index %" PRIuPTR " of %" PRIuPTR ")",
                this, it->second.get(),
                endpoints_[first_idle_index].ToString().c_str(),
                first_idle_index, endpoints_.size());
      }
      it->second->RequestConnectionLocked();
    }
  }
}

//
// factory
//

class RingHashFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<RingHash>(std::move(args));
  }

  absl::string_view name() const override { return kRingHash; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    auto config = LoadFromJson<RingHashConfig>(
        json, JsonArgs(), "errors validating ring_hash LB policy config");
    if (!config.ok()) return config.status();
    return MakeRefCounted<RingHashLbConfig>(config->min_ring_size,
                                            config->max_ring_size);
  }
};

}  // namespace

void RegisterRingHashLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<RingHashFactory>());
}

}  // namespace grpc_core
