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

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <grpc/grpc.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel_internal.h"
#include "src/core/ext/filters/client_channel/lb_policy/endpoint_list.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/resolver/server_address.h"
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
  class RingHashEndpointList : public EndpointList {
   public:
    class Ring : public RefCounted<Ring> {
     public:
      struct RingEntry {
        uint64_t hash;
        size_t endpoint_index;
      };

      Ring(RingHashLbConfig* config, const ServerAddressList& addresses,
           const ChannelArgs& args);

      const std::vector<RingEntry>& ring() const { return ring_; }

     private:
      std::vector<RingEntry> ring_;
    };

    class RingHashEndpoint : public Endpoint {
     public:
      // Info about an endpoint to be stored in the picker.
      struct EndpointInfo {
        RefCountedPtr<RingHashEndpoint> endpoint;
        RefCountedPtr<SubchannelPicker> picker;
        grpc_connectivity_state state;
        absl::Status status;
      };

      RingHashEndpoint(RefCountedPtr<RingHashEndpointList> endpoint_list,
                         const ServerAddress& address, const ChannelArgs& args,
                         std::shared_ptr<WorkSerializer> work_serializer)
          : Endpoint(std::move(endpoint_list)) {
        // FIXME: need to lazily create PF child!
        Init(address, args, std::move(work_serializer));
      }

      EndpointInfo GetInfoForPicker() {
        return {Ref(), picker(),
                connectivity_state().value_or(GRPC_CHANNEL_IDLE), status_};
      }

     private:
      // Called when the child policy reports a connectivity state update.
      void OnStateUpdate(absl::optional<grpc_connectivity_state> old_state,
                         grpc_connectivity_state new_state,
                         const absl::Status& status) override;

      // Status from last connectivity state update.
      absl::Status status_;
    };

    RingHashEndpointList(RefCountedPtr<RingHash> ring_hash,
                         const ServerAddressList& addresses,
                         const ChannelArgs& args)
        : EndpointList(std::move(ring_hash),
                       GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)
                           ? "RingHashEndpointList"
                           : nullptr),
          num_idle_(addresses.size()),
          ring_(MakeRefCounted<Ring>(policy<RingHash>()->config_.get(),
                                     addresses, args)) {
      Init(addresses, args,
           [&](RefCountedPtr<RingHashEndpointList> endpoint_list,
               const ServerAddress& address, const ChannelArgs& args) {
             return MakeOrphanable<RingHashEndpoint>(
                 std::move(endpoint_list), address, args,
                 policy<RingHash>()->work_serializer());
           });
    }

    RefCountedPtr<Ring> ring() { return ring_; }

    // Updates the aggregate policy's connectivity state based on the
    // endpoint list's state counters, creating a new picker.
    // The index parameter indicates the index into the list of the endpoint
    // whose status report triggered the call to
    // MaybeUpdateAggregatedConnectivityStateLocked().
    // connection_attempt_complete is true if the endpoint just
    // finished a connection attempt.
    void MaybeUpdateAggregatedConnectivityStateLocked(
        size_t index, bool connection_attempt_complete, absl::Status status);

   private:
    LoadBalancingPolicy::ChannelControlHelper* channel_control_helper()
        const override {
      return policy<RingHash>()->channel_control_helper();
    }

    // Updates the counters of children in each state when a
    // child transitions from old_state to new_state.
    void UpdateStateCountersLocked(grpc_connectivity_state old_state,
                                   grpc_connectivity_state new_state);

    std::string CountersString() const {
      return absl::StrCat("num_children=", size(), " num_idle=", num_idle_,
                          " num_ready=", num_ready_,
                          " num_connecting=", num_connecting_,
                          " num_transient_failure=", num_transient_failure_);
    }

    size_t num_idle_;
    size_t num_ready_ = 0;
    size_t num_connecting_ = 0;
    size_t num_transient_failure_ = 0;

    // TODO(roth): If we ever change the helper UpdateState() API to not
    // need the status reported for TRANSIENT_FAILURE state (because
    // it's not currently actually used for anything outside of the picker),
    // then we will no longer need this data member.
    absl::Status last_failure_;

    RefCountedPtr<Ring> ring_;

    // The index of the endpoint currently doing an internally
    // triggered connection attempt, if any.
    absl::optional<size_t> internally_triggered_connection_index_;
  };

  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<RingHash> ring_hash_lb,
           RingHashEndpointList* endpoint_list)
        : ring_hash_lb_(std::move(ring_hash_lb)),
          ring_(endpoint_list->ring()) {
      endpoints_.reserve(endpoint_list->size());
      for (const auto& endpoint : endpoint_list->endpoints()) {
        auto* ep = static_cast<RingHashEndpointList::RingHashEndpoint*>(
            endpoint.get());
        endpoints_.emplace_back(ep->GetInfoForPicker());
      }
    }

    PickResult Pick(PickArgs args) override;

   private:
    // A fire-and-forget class that schedules endpoint connection attempts
    // on the control plane WorkSerializer.
    class EndpointConnectionAttempter : public Orphanable {
     public:
      explicit EndpointConnectionAttempter(
          RefCountedPtr<RingHash> ring_hash_lb)
          : ring_hash_lb_(std::move(ring_hash_lb)) {
        GRPC_CLOSURE_INIT(&closure_, RunInExecCtx, this, nullptr);
      }

      void Orphan() override {
        // Hop into ExecCtx, so that we're not holding the data plane mutex
        // while we run control-plane code.
        ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
      }

      void AddEndpoint(
          RefCountedPtr<RingHashEndpointList::RingHashEndpoint> endpoint) {
        endpoints_.push_back(std::move(endpoint));
      }

     private:
      static void RunInExecCtx(void* arg, grpc_error_handle /*error*/) {
        auto* self = static_cast<EndpointConnectionAttempter*>(arg);
        self->ring_hash_lb_->work_serializer()->Run(
            [self]() {
              if (!self->ring_hash_lb_->shutdown_) {
                for (auto& endpoint : self->endpoints_) {
                  endpoint->ExitIdleLocked();
                }
              }
              delete self;
            },
            DEBUG_LOCATION);
      }

      RefCountedPtr<RingHash> ring_hash_lb_;
      grpc_closure closure_;
      std::vector<RefCountedPtr<RingHashEndpointList::RingHashEndpoint>>
          endpoints_;
    };

    RefCountedPtr<RingHash> ring_hash_lb_;
    RefCountedPtr<RingHashEndpointList::Ring> ring_;
    std::vector<RingHashEndpointList::RingHashEndpoint::EndpointInfo>
        endpoints_;
  };

  ~RingHash() override;

  void ShutdownLocked() override;

  // Current config from resolver.
  RefCountedPtr<RingHashLbConfig> config_;

  // List of endpoints.
  OrphanablePtr<RingHashEndpointList> endpoint_list_;
  OrphanablePtr<RingHashEndpointList> latest_pending_endpoint_list_;
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
  // Ported from https://github.com/RJ/ketama/blob/master/libketama/ketama.c
  // (ketama_get_server) NOTE: The algorithm depends on using signed integers
  // for lowp, highp, and first_index. Do not change them!
  size_t lowp = 0;
  size_t highp = ring.size();
  size_t first_index = 0;
  while (true) {
    first_index = (lowp + highp) / 2;
    if (first_index == ring.size()) {
      first_index = 0;
      break;
    }
    uint64_t midval = ring[first_index].hash;
    uint64_t midval1 = first_index == 0 ? 0 : ring[first_index - 1].hash;
    if (h <= midval && h > midval1) {
      break;
    }
    if (midval < h) {
      lowp = first_index + 1;
    } else {
      highp = first_index - 1;
    }
    if (lowp > highp) {
      first_index = 0;
      break;
    }
  }
  OrphanablePtr<EndpointConnectionAttempter> endpoint_connection_attempter;
  auto ScheduleEndpointConnectionAttempt =
      [&](RefCountedPtr<RingHashEndpointList::RingHashEndpoint> endpoint) {
        if (endpoint_connection_attempter == nullptr) {
          endpoint_connection_attempter =
              MakeOrphanable<EndpointConnectionAttempter>(ring_hash_lb_->Ref(
                  DEBUG_LOCATION, "EndpointConnectionAttempter"));
        }
        endpoint_connection_attempter->AddEndpoint(std::move(endpoint));
      };
  auto& first_endpoint = endpoints_[ring[first_index].endpoint_index];
  switch (first_endpoint.state) {
    case GRPC_CHANNEL_READY:
      return first_endpoint.picker->Pick(args);
    case GRPC_CHANNEL_IDLE:
      ScheduleEndpointConnectionAttempt(first_endpoint.endpoint);
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_CHANNEL_CONNECTING:
      return PickResult::Queue();
    default:  // GRPC_CHANNEL_TRANSIENT_FAILURE
      break;
  }
  ScheduleEndpointConnectionAttempt(first_endpoint.endpoint);
  // Loop through remaining endpoints to find one in READY.
  // On the way, we make sure the right set of connection attempts
  // will happen.
  bool found_second_endpoint = false;
  bool found_first_non_failed = false;
  for (size_t i = 1; i < ring.size(); ++i) {
    const auto& entry = ring[(first_index + i) % ring.size()];
    if (entry.endpoint_index == ring[first_index].endpoint_index) {
      continue;
    }
    auto& endpoint_info = endpoints_[entry.endpoint_index];
    if (endpoint_info.state == GRPC_CHANNEL_READY) {
      return endpoint_info.picker->Pick(args);
    }
    if (!found_second_endpoint) {
      switch (endpoint_info.state) {
        case GRPC_CHANNEL_IDLE:
          ScheduleEndpointConnectionAttempt(endpoint_info.endpoint);
          ABSL_FALLTHROUGH_INTENDED;
        case GRPC_CHANNEL_CONNECTING:
          return PickResult::Queue();
        default:
          break;
      }
      found_second_endpoint = true;
    }
    if (!found_first_non_failed) {
      if (endpoint_info.state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        ScheduleEndpointConnectionAttempt(endpoint_info.endpoint);
      } else {
        if (endpoint_info.state == GRPC_CHANNEL_IDLE) {
          ScheduleEndpointConnectionAttempt(endpoint_info.endpoint);
        }
        found_first_non_failed = true;
      }
    }
  }
  return PickResult::Fail(absl::UnavailableError(absl::StrCat(
      "ring hash cannot find a connected endpoint; first failure: ",
      first_endpoint.status.message())));
}

//
// RingHash::RingHashEndpointList::Ring
//

RingHash::RingHashEndpointList::Ring::Ring(
    RingHashLbConfig* config, const ServerAddressList& addresses,
    const ChannelArgs& args) {
  // Store the weights while finding the sum.
  struct AddressWeight {
    std::string address;
    // Default weight is 1 for the cases where a weight is not provided,
    // each occurrence of the address will be counted a weight value of 1.
    uint32_t weight = 1;
    double normalized_weight;
  };
  std::vector<AddressWeight> address_weights;
  size_t sum = 0;
  address_weights.reserve(addresses.size());
  for (const auto& address : addresses) {
    const auto* weight_attribute = static_cast<
        const ServerAddressWeightAttribute*>(address.GetAttribute(
        ServerAddressWeightAttribute::kServerAddressWeightAttributeKey));
    AddressWeight address_weight;
    address_weight.address =
        grpc_sockaddr_to_string(&address.address(), false).value();
    // Weight should never be zero, but ignore it just in case, since
    // that value would screw up the ring-building algorithm.
    if (weight_attribute != nullptr && weight_attribute->weight() > 0) {
      address_weight.weight = weight_attribute->weight();
    }
    sum += address_weight.weight;
    address_weights.push_back(std::move(address_weight));
  }
  // Calculating normalized weights and find min and max.
  double min_normalized_weight = 1.0;
  double max_normalized_weight = 0.0;
  for (auto& address : address_weights) {
    address.normalized_weight = static_cast<double>(address.weight) / sum;
    min_normalized_weight =
        std::min(address.normalized_weight, min_normalized_weight);
    max_normalized_weight =
        std::max(address.normalized_weight, max_normalized_weight);
  }
  // Scale up the number of hashes per host such that the least-weighted host
  // gets a whole number of hashes on the ring. Other hosts might not end up
  // with whole numbers, and that's fine (the ring-building algorithm below can
  // handle this). This preserves the original implementation's behavior: when
  // weights aren't provided, all hosts should get an equal number of hashes. In
  // the case where this number exceeds the max_ring_size, it's scaled back down
  // to fit.
  const size_t ring_size_cap = args.GetInt(GRPC_ARG_RING_HASH_LB_RING_SIZE_CAP)
                                   .value_or(kRingSizeCapDefault);
  const size_t min_ring_size = std::min(config->min_ring_size(), ring_size_cap);
  const size_t max_ring_size = std::min(config->max_ring_size(), ring_size_cap);
  const double scale = std::min(
      std::ceil(min_normalized_weight * min_ring_size) / min_normalized_weight,
      static_cast<double>(max_ring_size));
  // Reserve memory for the entire ring up front.
  const size_t ring_size = std::ceil(scale);
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
  for (size_t i = 0; i < addresses.size(); ++i) {
    const std::string& address_string = address_weights[i].address;
    hash_key_buffer.assign(address_string.begin(), address_string.end());
    hash_key_buffer.emplace_back('_');
    auto offset_start = hash_key_buffer.end();
    target_hashes += scale * address_weights[i].normalized_weight;
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
// RingHash::RingHashEndpointList
//

void RingHash::RingHashEndpointList::UpdateStateCountersLocked(
    grpc_connectivity_state old_state, grpc_connectivity_state new_state) {
  if (old_state == GRPC_CHANNEL_IDLE) {
    GPR_ASSERT(num_idle_ > 0);
    --num_idle_;
  } else if (old_state == GRPC_CHANNEL_READY) {
    GPR_ASSERT(num_ready_ > 0);
    --num_ready_;
  } else if (old_state == GRPC_CHANNEL_CONNECTING) {
    GPR_ASSERT(num_connecting_ > 0);
    --num_connecting_;
  } else if (old_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    GPR_ASSERT(num_transient_failure_ > 0);
    --num_transient_failure_;
  }
  GPR_ASSERT(new_state != GRPC_CHANNEL_SHUTDOWN);
  if (new_state == GRPC_CHANNEL_IDLE) {
    ++num_idle_;
  } else if (new_state == GRPC_CHANNEL_READY) {
    ++num_ready_;
  } else if (new_state == GRPC_CHANNEL_CONNECTING) {
    ++num_connecting_;
  } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ++num_transient_failure_;
  }
}

void
RingHash::RingHashEndpointList::MaybeUpdateAggregatedConnectivityStateLocked(
    size_t index, bool connection_attempt_complete, absl::Status status) {
  auto* ring_hash = policy<RingHash>();
  // If this is latest_pending_endpoint_list_, then swap it into
  // endpoint_list_ as soon as we get the initial connectivity state
  // report for every endpoint in the list.
  if (ring_hash->latest_pending_endpoint_list_.get() == this &&
      AllEndpointsSeenInitialState()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
      gpr_log(GPR_INFO, "[RH %p] replacing endpoint list %p with %p", ring_hash,
              ring_hash->endpoint_list_.get(), this);
    }
    ring_hash->endpoint_list_ =
        std::move(ring_hash->latest_pending_endpoint_list_);
  }
  // Only set connectivity state if this is the current endpoint list.
  if (ring_hash->endpoint_list_.get() != this) return;
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
  // We set start_connection_attempt to true if we match rules 2, 3, or 6.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    gpr_log(GPR_INFO,
            "[RH %p] setting connectivity state based on endpoint list %p: %s",
            ring_hash, this, CountersString().c_str());
  }
  grpc_connectivity_state state;
  bool start_connection_attempt = false;
  if (num_ready_ > 0) {
    state = GRPC_CHANNEL_READY;
  } else if (num_transient_failure_ >= 2) {
    state = GRPC_CHANNEL_TRANSIENT_FAILURE;
    start_connection_attempt = true;
  } else if (num_connecting_ > 0) {
    state = GRPC_CHANNEL_CONNECTING;
  } else if (num_transient_failure_ == 1 && size() > 1) {
    state = GRPC_CHANNEL_CONNECTING;
    start_connection_attempt = true;
  } else if (num_idle_ > 0) {
    state = GRPC_CHANNEL_IDLE;
  } else {
    state = GRPC_CHANNEL_TRANSIENT_FAILURE;
    start_connection_attempt = true;
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
  ring_hash->channel_control_helper()->UpdateState(
      state, status,
      MakeRefCounted<Picker>(
          ring_hash->Ref(DEBUG_LOCATION, "RingHashPicker"), this));
  // While the ring_hash policy is reporting TRANSIENT_FAILURE, it will
  // not be getting any pick requests from the priority policy.
  // However, because the ring_hash policy does not attempt to
  // reconnect to endpoints unless it is getting pick requests,
  // it will need special handling to ensure that it will eventually
  // recover from TRANSIENT_FAILURE state once the problem is resolved.
  // Specifically, it will make sure that it is attempting to connect to
  // at least one endpoint at any given time.  After a given endpoint
  // fails a connection attempt, it will move on to the next endpoint
  // in the ring.  It will keep doing this until one of the endpoints
  // successfully connects, at which point it will report READY and stop
  // proactively trying to connect.  The policy will remain in
  // TRANSIENT_FAILURE until at least one endpoint becomes connected,
  // even if endpoints are in state CONNECTING during that time.
  //
  // Note that we do the same thing when the policy is in state
  // CONNECTING, just to ensure that we don't remain in CONNECTING state
  // indefinitely if there are no new picks coming in.
  if (internally_triggered_connection_index_.has_value() &&
      *internally_triggered_connection_index_ == index &&
      connection_attempt_complete) {
    internally_triggered_connection_index_.reset();
  }
  if (start_connection_attempt &&
      !internally_triggered_connection_index_.has_value()) {
    size_t next_index = (index + 1) % size();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
      gpr_log(GPR_INFO,
              "[RH %p] triggering internal connection attempt for endpoint "
              "%p, endpoint_list %p (index %" PRIuPTR " of %" PRIuPTR ")",
              ring_hash, endpoints()[next_index].get(), this, next_index,
              size());
    }
    internally_triggered_connection_index_ = next_index;
    endpoints()[next_index]->ExitIdleLocked();
  }
}

//
// RingHash::RingHashEndpointList::RingHashEndpoint
//

void RingHash::RingHashEndpointList::RingHashEndpoint::OnStateUpdate(
    absl::optional<grpc_connectivity_state> old_state,
    grpc_connectivity_state new_state, const absl::Status& status) {
  auto* rh_endpoint_list = endpoint_list<RingHashEndpointList>();
  auto* ring_hash = policy<RingHash>();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    gpr_log(
        GPR_INFO,
        "[RH %p] connectivity changed for endpoint %p, endpoint_list %p "
        "(index %" PRIuPTR " of %" PRIuPTR "): prev_state=%s new_state=%s (%s)",
        ring_hash, this, rh_endpoint_list, Index(), rh_endpoint_list->size(),
        old_state.has_value() ? ConnectivityStateName(*old_state) : "N/A",
        ConnectivityStateName(new_state), status.ToString().c_str());
  }
  const bool connection_attempt_complete = new_state != GRPC_CHANNEL_CONNECTING;
  // Update status.
  status_ = status;
  // If state changed, update state counters.
  grpc_connectivity_state use_old_state = old_state.value_or(GRPC_CHANNEL_IDLE);
  if (use_old_state != new_state) {
    rh_endpoint_list->UpdateStateCountersLocked(use_old_state, new_state);
  }
  // Update the aggregated connectivity state.
  rh_endpoint_list->MaybeUpdateAggregatedConnectivityStateLocked(
      Index(), connection_attempt_complete, status);
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
  GPR_ASSERT(endpoint_list_ == nullptr);
  GPR_ASSERT(latest_pending_endpoint_list_ == nullptr);
}

void RingHash::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    gpr_log(GPR_INFO, "[RH %p] Shutting down", this);
  }
  shutdown_ = true;
  endpoint_list_.reset();
  latest_pending_endpoint_list_.reset();
}

void RingHash::ResetBackoffLocked() {
  endpoint_list_->ResetBackoffLocked();
  if (latest_pending_endpoint_list_ != nullptr) {
    latest_pending_endpoint_list_->ResetBackoffLocked();
  }
}

absl::Status RingHash::UpdateLocked(UpdateArgs args) {
  config_ = std::move(args.config);
  ServerAddressList addresses;
  if (args.addresses.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
      gpr_log(GPR_INFO, "[RH %p] received update with %" PRIuPTR " addresses",
              this, args.addresses->size());
    }
    addresses = *std::move(args.addresses);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
      gpr_log(GPR_INFO, "[RH %p] received update with addresses error: %s",
              this, args.addresses.status().ToString().c_str());
    }
    // If we already have an endpoint list, then keep using the existing
    // list, but still report back that the update was not accepted.
    if (endpoint_list_ != nullptr) return args.addresses.status();
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace) &&
      latest_pending_endpoint_list_ != nullptr) {
    gpr_log(GPR_INFO, "[RH %p] replacing latest pending endpoint list %p",
            this, latest_pending_endpoint_list_.get());
  }
  latest_pending_endpoint_list_ = MakeOrphanable<RingHashEndpointList>(
      Ref(), std::move(addresses), args.args);
  // If we have no existing list or the new list is empty, immediately
  // promote the new list.
  // Otherwise, do nothing; the new list will be promoted when the
  // initial connectivity states are reported.
  if (endpoint_list_ == nullptr ||
      latest_pending_endpoint_list_->size() == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace) &&
        endpoint_list_ != nullptr) {
      gpr_log(GPR_INFO,
              "[RH %p] empty address list, replacing endpoint list %p", this,
              endpoint_list_.get());
    }
    endpoint_list_ = std::move(latest_pending_endpoint_list_);
    // If the new list is empty, report TRANSIENT_FAILURE.
    if (endpoint_list_->size() == 0) {
      absl::Status status =
          args.addresses.ok()
              ? absl::UnavailableError(
                    absl::StrCat("empty address list: ", args.resolution_note))
              : args.addresses.status();
      channel_control_helper()->UpdateState(
          GRPC_CHANNEL_TRANSIENT_FAILURE, status,
          MakeRefCounted<TransientFailurePicker>(status));
      return status;
    }
    // Otherwise, report IDLE.
    endpoint_list_->MaybeUpdateAggregatedConnectivityStateLocked(
        /*index=*/0, /*connection_attempt_complete=*/false, absl::OkStatus());
  }
  return absl::OkStatus();
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
