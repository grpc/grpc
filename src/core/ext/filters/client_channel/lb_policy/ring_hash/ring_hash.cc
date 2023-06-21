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
#include "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h"
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
#include "src/core/lib/load_balancing/subchannel_interface.h"
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
  // Forward declaration.
  class RingHashSubchannelList;

  // Data for a particular subchannel in a subchannel list.
  // This subclass adds the following functionality:
  // - Tracks the previous connectivity state of the subchannel, so that
  //   we know how many subchannels are in each state.
  class RingHashSubchannelData
      : public SubchannelData<RingHashSubchannelList, RingHashSubchannelData> {
   public:
    RingHashSubchannelData(
        SubchannelList<RingHashSubchannelList, RingHashSubchannelData>*
            subchannel_list,
        const ServerAddress& address,
        RefCountedPtr<SubchannelInterface> subchannel)
        : SubchannelData(subchannel_list, address, std::move(subchannel)),
          address_(address) {}

    const ServerAddress& address() const { return address_; }

    grpc_connectivity_state logical_connectivity_state() const {
      return logical_connectivity_state_;
    }
    const absl::Status& logical_connectivity_status() const {
      return logical_connectivity_status_;
    }

   private:
    // Performs connectivity state updates that need to be done only
    // after we have started watching.
    void ProcessConnectivityChangeLocked(
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state) override;

    ServerAddress address_;

    // Last logical connectivity state seen.
    // Note that this may differ from the state actually reported by the
    // subchannel in some cases; for example, once this is set to
    // TRANSIENT_FAILURE, we do not change it again until we get READY,
    // so we skip any interim stops in CONNECTING.
    grpc_connectivity_state logical_connectivity_state_ = GRPC_CHANNEL_IDLE;
    absl::Status logical_connectivity_status_;
  };

  // A list of subchannels and the ring containing those subchannels.
  class RingHashSubchannelList
      : public SubchannelList<RingHashSubchannelList, RingHashSubchannelData> {
   public:
    class Ring : public RefCounted<Ring> {
     public:
      struct RingEntry {
        uint64_t hash;
        size_t subchannel_index;
      };

      Ring(RingHashLbConfig* config, RingHashSubchannelList* subchannel_list,
           const ChannelArgs& args);

      const std::vector<RingEntry>& ring() const { return ring_; }

     private:
      std::vector<RingEntry> ring_;
    };

    RingHashSubchannelList(RingHash* policy, ServerAddressList addresses,
                           const ChannelArgs& args);

    ~RingHashSubchannelList() override {
      RingHash* p = static_cast<RingHash*>(policy());
      p->Unref(DEBUG_LOCATION, "subchannel_list");
    }

    RefCountedPtr<Ring> ring() { return ring_; }

    // Updates the counters of subchannels in each state when a
    // subchannel transitions from old_state to new_state.
    void UpdateStateCountersLocked(grpc_connectivity_state old_state,
                                   grpc_connectivity_state new_state);

    // Updates the RH policy's connectivity state based on the
    // subchannel list's state counters, creating new picker and new ring.
    // The index parameter indicates the index into the list of the subchannel
    // whose status report triggered the call to
    // UpdateRingHashConnectivityStateLocked().
    // connection_attempt_complete is true if the subchannel just
    // finished a connection attempt.
    void UpdateRingHashConnectivityStateLocked(size_t index,
                                               bool connection_attempt_complete,
                                               absl::Status status);

   private:
    std::shared_ptr<WorkSerializer> work_serializer() const override {
      return static_cast<RingHash*>(policy())->work_serializer();
    }

    size_t num_idle_;
    size_t num_ready_ = 0;
    size_t num_connecting_ = 0;
    size_t num_transient_failure_ = 0;

    RefCountedPtr<Ring> ring_;

    // The index of the subchannel currently doing an internally
    // triggered connection attempt, if any.
    absl::optional<size_t> internally_triggered_connection_index_;

    // TODO(roth): If we ever change the helper UpdateState() API to not
    // need the status reported for TRANSIENT_FAILURE state (because
    // it's not currently actually used for anything outside of the picker),
    // then we will no longer need this data member.
    absl::Status last_failure_;
  };

  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<RingHash> ring_hash_lb,
           RingHashSubchannelList* subchannel_list)
        : ring_hash_lb_(std::move(ring_hash_lb)),
          ring_(subchannel_list->ring()) {
      subchannels_.reserve(subchannel_list->num_subchannels());
      for (size_t i = 0; i < subchannel_list->num_subchannels(); ++i) {
        RingHashSubchannelData* subchannel_data =
            subchannel_list->subchannel(i);
        subchannels_.emplace_back(
            SubchannelInfo{subchannel_data->subchannel()->Ref(),
                           subchannel_data->logical_connectivity_state(),
                           subchannel_data->logical_connectivity_status()});
      }
    }

    PickResult Pick(PickArgs args) override;

   private:
    // A fire-and-forget class that schedules subchannel connection attempts
    // on the control plane WorkSerializer.
    class SubchannelConnectionAttempter : public Orphanable {
     public:
      explicit SubchannelConnectionAttempter(
          RefCountedPtr<RingHash> ring_hash_lb)
          : ring_hash_lb_(std::move(ring_hash_lb)) {
        GRPC_CLOSURE_INIT(&closure_, RunInExecCtx, this, nullptr);
      }

      void Orphan() override {
        // Hop into ExecCtx, so that we're not holding the data plane mutex
        // while we run control-plane code.
        ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
      }

      void AddSubchannel(RefCountedPtr<SubchannelInterface> subchannel) {
        subchannels_.push_back(std::move(subchannel));
      }

     private:
      static void RunInExecCtx(void* arg, grpc_error_handle /*error*/) {
        auto* self = static_cast<SubchannelConnectionAttempter*>(arg);
        self->ring_hash_lb_->work_serializer()->Run(
            [self]() {
              if (!self->ring_hash_lb_->shutdown_) {
                for (auto& subchannel : self->subchannels_) {
                  subchannel->RequestConnection();
                }
              }
              delete self;
            },
            DEBUG_LOCATION);
      }

      RefCountedPtr<RingHash> ring_hash_lb_;
      grpc_closure closure_;
      std::vector<RefCountedPtr<SubchannelInterface>> subchannels_;
    };

    struct SubchannelInfo {
      RefCountedPtr<SubchannelInterface> subchannel;
      grpc_connectivity_state state;
      absl::Status status;
    };

    RefCountedPtr<RingHash> ring_hash_lb_;
    RefCountedPtr<RingHashSubchannelList::Ring> ring_;
    std::vector<SubchannelInfo> subchannels_;
  };

  ~RingHash() override;

  void ShutdownLocked() override;

  // Current config from resolver.
  RefCountedPtr<RingHashLbConfig> config_;

  // list of subchannels.
  RefCountedPtr<RingHashSubchannelList> subchannel_list_;
  RefCountedPtr<RingHashSubchannelList> latest_pending_subchannel_list_;
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
  int64_t lowp = 0;
  int64_t highp = ring.size();
  int64_t first_index = 0;
  while (true) {
    first_index = (lowp + highp) / 2;
    if (first_index == static_cast<int64_t>(ring.size())) {
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
  OrphanablePtr<SubchannelConnectionAttempter> subchannel_connection_attempter;
  auto ScheduleSubchannelConnectionAttempt =
      [&](RefCountedPtr<SubchannelInterface> subchannel) {
        if (subchannel_connection_attempter == nullptr) {
          subchannel_connection_attempter =
              MakeOrphanable<SubchannelConnectionAttempter>(ring_hash_lb_->Ref(
                  DEBUG_LOCATION, "SubchannelConnectionAttempter"));
        }
        subchannel_connection_attempter->AddSubchannel(std::move(subchannel));
      };
  SubchannelInfo& first_subchannel =
      subchannels_[ring[first_index].subchannel_index];
  switch (first_subchannel.state) {
    case GRPC_CHANNEL_READY:
      return PickResult::Complete(first_subchannel.subchannel);
    case GRPC_CHANNEL_IDLE:
      ScheduleSubchannelConnectionAttempt(first_subchannel.subchannel);
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_CHANNEL_CONNECTING:
      return PickResult::Queue();
    default:  // GRPC_CHANNEL_TRANSIENT_FAILURE
      break;
  }
  ScheduleSubchannelConnectionAttempt(first_subchannel.subchannel);
  // Loop through remaining subchannels to find one in READY.
  // On the way, we make sure the right set of connection attempts
  // will happen.
  bool found_second_subchannel = false;
  bool found_first_non_failed = false;
  for (size_t i = 1; i < ring.size(); ++i) {
    const auto& entry = ring[(first_index + i) % ring.size()];
    if (entry.subchannel_index == ring[first_index].subchannel_index) {
      continue;
    }
    SubchannelInfo& subchannel_info = subchannels_[entry.subchannel_index];
    if (subchannel_info.state == GRPC_CHANNEL_READY) {
      return PickResult::Complete(subchannel_info.subchannel);
    }
    if (!found_second_subchannel) {
      switch (subchannel_info.state) {
        case GRPC_CHANNEL_IDLE:
          ScheduleSubchannelConnectionAttempt(subchannel_info.subchannel);
          ABSL_FALLTHROUGH_INTENDED;
        case GRPC_CHANNEL_CONNECTING:
          return PickResult::Queue();
        default:
          break;
      }
      found_second_subchannel = true;
    }
    if (!found_first_non_failed) {
      if (subchannel_info.state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        ScheduleSubchannelConnectionAttempt(subchannel_info.subchannel);
      } else {
        if (subchannel_info.state == GRPC_CHANNEL_IDLE) {
          ScheduleSubchannelConnectionAttempt(subchannel_info.subchannel);
        }
        found_first_non_failed = true;
      }
    }
  }
  return PickResult::Fail(absl::UnavailableError(absl::StrCat(
      "ring hash cannot find a connected subchannel; first failure: ",
      first_subchannel.status.ToString())));
}

//
// RingHash::RingHashSubchannelList::Ring
//

RingHash::RingHashSubchannelList::Ring::Ring(
    RingHashLbConfig* config, RingHashSubchannelList* subchannel_list,
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
  address_weights.reserve(subchannel_list->num_subchannels());
  for (size_t i = 0; i < subchannel_list->num_subchannels(); ++i) {
    RingHashSubchannelData* sd = subchannel_list->subchannel(i);
    auto weight_arg = sd->address().args().GetInt(GRPC_ARG_ADDRESS_WEIGHT);
    AddressWeight address_weight;
    address_weight.address =
        grpc_sockaddr_to_string(&sd->address().address(), false).value();
    // Weight should never be zero, but ignore it just in case, since
    // that value would screw up the ring-building algorithm.
    if (weight_arg.has_value() && *weight_arg > 0) {
      address_weight.weight = *weight_arg;
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
  for (size_t i = 0; i < subchannel_list->num_subchannels(); ++i) {
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
// RingHash::RingHashSubchannelList
//

RingHash::RingHashSubchannelList::RingHashSubchannelList(
    RingHash* policy, ServerAddressList addresses, const ChannelArgs& args)
    : SubchannelList(policy,
                     (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)
                          ? "RingHashSubchannelList"
                          : nullptr),
                     std::move(addresses), policy->channel_control_helper(),
                     args),
      num_idle_(num_subchannels()) {
  // Need to maintain a ref to the LB policy as long as we maintain
  // any references to subchannels, since the subchannels'
  // pollset_sets will include the LB policy's pollset_set.
  policy->Ref(DEBUG_LOCATION, "subchannel_list").release();
  // Construct the ring.
  ring_ = MakeRefCounted<Ring>(policy->config_.get(), this, args);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    gpr_log(GPR_INFO,
            "[RH %p] created subchannel list %p with %" PRIuPTR " ring entries",
            policy, this, ring_->ring().size());
  }
}

void RingHash::RingHashSubchannelList::UpdateStateCountersLocked(
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

void RingHash::RingHashSubchannelList::UpdateRingHashConnectivityStateLocked(
    size_t index, bool connection_attempt_complete, absl::Status status) {
  RingHash* p = static_cast<RingHash*>(policy());
  // If this is latest_pending_subchannel_list_, then swap it into
  // subchannel_list_ as soon as we get the initial connectivity state
  // report for every subchannel in the list.
  if (p->latest_pending_subchannel_list_.get() == this &&
      AllSubchannelsSeenInitialState()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
      gpr_log(GPR_INFO, "[RH %p] replacing subchannel list %p with %p", p,
              p->subchannel_list_.get(), this);
    }
    p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
  }
  // Only set connectivity state if this is the current subchannel list.
  if (p->subchannel_list_.get() != this) return;
  // The overall aggregation rules here are:
  // 1. If there is at least one subchannel in READY state, report READY.
  // 2. If there are 2 or more subchannels in TRANSIENT_FAILURE state, report
  //    TRANSIENT_FAILURE.
  // 3. If there is at least one subchannel in CONNECTING state, report
  //    CONNECTING.
  // 4. If there is one subchannel in TRANSIENT_FAILURE state and there is
  //    more than one subchannel, report CONNECTING.
  // 5. If there is at least one subchannel in IDLE state, report IDLE.
  // 6. Otherwise, report TRANSIENT_FAILURE.
  //
  // We set start_connection_attempt to true if we match rules 2, 3, or 6.
  grpc_connectivity_state state;
  bool start_connection_attempt = false;
  if (num_ready_ > 0) {
    state = GRPC_CHANNEL_READY;
  } else if (num_transient_failure_ >= 2) {
    state = GRPC_CHANNEL_TRANSIENT_FAILURE;
    start_connection_attempt = true;
  } else if (num_connecting_ > 0) {
    state = GRPC_CHANNEL_CONNECTING;
  } else if (num_transient_failure_ == 1 && num_subchannels() > 1) {
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
          "no reachable subchannels; last error: ", status.ToString()));
    }
    status = last_failure_;
  } else {
    status = absl::OkStatus();
  }
  // Generate new picker and return it to the channel.
  // Note that we use our own picker regardless of connectivity state.
  p->channel_control_helper()->UpdateState(
      state, status,
      MakeRefCounted<Picker>(p->Ref(DEBUG_LOCATION, "RingHashPicker"), this));
  // While the ring_hash policy is reporting TRANSIENT_FAILURE, it will
  // not be getting any pick requests from the priority policy.
  // However, because the ring_hash policy does not attempt to
  // reconnect to subchannels unless it is getting pick requests,
  // it will need special handling to ensure that it will eventually
  // recover from TRANSIENT_FAILURE state once the problem is resolved.
  // Specifically, it will make sure that it is attempting to connect to
  // at least one subchannel at any given time.  After a given subchannel
  // fails a connection attempt, it will move on to the next subchannel
  // in the ring.  It will keep doing this until one of the subchannels
  // successfully connects, at which point it will report READY and stop
  // proactively trying to connect.  The policy will remain in
  // TRANSIENT_FAILURE until at least one subchannel becomes connected,
  // even if subchannels are in state CONNECTING during that time.
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
    size_t next_index = (index + 1) % num_subchannels();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
      gpr_log(GPR_INFO,
              "[RH %p] triggering internal connection attempt for subchannel "
              "%p, subchannel_list %p (index %" PRIuPTR " of %" PRIuPTR ")",
              p, subchannel(next_index)->subchannel(), this, next_index,
              num_subchannels());
    }
    internally_triggered_connection_index_ = next_index;
    subchannel(next_index)->subchannel()->RequestConnection();
  }
}

//
// RingHash::RingHashSubchannelData
//

void RingHash::RingHashSubchannelData::ProcessConnectivityChangeLocked(
    absl::optional<grpc_connectivity_state> old_state,
    grpc_connectivity_state new_state) {
  RingHash* p = static_cast<RingHash*>(subchannel_list()->policy());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    gpr_log(
        GPR_INFO,
        "[RH %p] connectivity changed for subchannel %p, subchannel_list %p "
        "(index %" PRIuPTR " of %" PRIuPTR "): prev_state=%s new_state=%s",
        p, subchannel(), subchannel_list(), Index(),
        subchannel_list()->num_subchannels(),
        ConnectivityStateName(logical_connectivity_state_),
        ConnectivityStateName(new_state));
  }
  GPR_ASSERT(subchannel() != nullptr);
  // If this is not the initial state notification and the new state is
  // TRANSIENT_FAILURE or IDLE, re-resolve.
  // Note that we don't want to do this on the initial state notification,
  // because that would result in an endless loop of re-resolution.
  if (old_state.has_value() && (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
                                new_state == GRPC_CHANNEL_IDLE)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
      gpr_log(GPR_INFO,
              "[RH %p] Subchannel %p reported %s; requesting re-resolution", p,
              subchannel(), ConnectivityStateName(new_state));
    }
    p->channel_control_helper()->RequestReresolution();
  }
  const bool connection_attempt_complete = new_state != GRPC_CHANNEL_CONNECTING;
  // Decide what state to report for the purposes of aggregation and
  // picker behavior.
  // If the last recorded state was TRANSIENT_FAILURE, ignore the change
  // unless the new state is READY (or TF again, in which case we need
  // to update the status).
  if (logical_connectivity_state_ != GRPC_CHANNEL_TRANSIENT_FAILURE ||
      new_state == GRPC_CHANNEL_READY ||
      new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    // Update state counters used for aggregation.
    subchannel_list()->UpdateStateCountersLocked(logical_connectivity_state_,
                                                 new_state);
    // Update logical state.
    logical_connectivity_state_ = new_state;
    logical_connectivity_status_ = connectivity_status();
  }
  // Update the RH policy's connectivity state, creating new picker and new
  // ring.
  subchannel_list()->UpdateRingHashConnectivityStateLocked(
      Index(), connection_attempt_complete, logical_connectivity_status_);
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
  GPR_ASSERT(subchannel_list_ == nullptr);
  GPR_ASSERT(latest_pending_subchannel_list_ == nullptr);
}

void RingHash::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace)) {
    gpr_log(GPR_INFO, "[RH %p] Shutting down", this);
  }
  shutdown_ = true;
  subchannel_list_.reset();
  latest_pending_subchannel_list_.reset();
}

void RingHash::ResetBackoffLocked() {
  subchannel_list_->ResetBackoffLocked();
  if (latest_pending_subchannel_list_ != nullptr) {
    latest_pending_subchannel_list_->ResetBackoffLocked();
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
    // If we already have a subchannel list, then keep using the existing
    // list, but still report back that the update was not accepted.
    if (subchannel_list_ != nullptr) return args.addresses.status();
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace) &&
      latest_pending_subchannel_list_ != nullptr) {
    gpr_log(GPR_INFO, "[RH %p] replacing latest pending subchannel list %p",
            this, latest_pending_subchannel_list_.get());
  }
  latest_pending_subchannel_list_ = MakeRefCounted<RingHashSubchannelList>(
      this, std::move(addresses), args.args);
  latest_pending_subchannel_list_->StartWatchingLocked();
  // If we have no existing list or the new list is empty, immediately
  // promote the new list.
  // Otherwise, do nothing; the new list will be promoted when the
  // initial subchannel states are reported.
  if (subchannel_list_ == nullptr ||
      latest_pending_subchannel_list_->num_subchannels() == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_ring_hash_trace) &&
        subchannel_list_ != nullptr) {
      gpr_log(GPR_INFO,
              "[RH %p] empty address list, replacing subchannel list %p", this,
              subchannel_list_.get());
    }
    subchannel_list_ = std::move(latest_pending_subchannel_list_);
    // If the new list is empty, report TRANSIENT_FAILURE.
    if (subchannel_list_->num_subchannels() == 0) {
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
    subchannel_list_->UpdateRingHashConnectivityStateLocked(
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
