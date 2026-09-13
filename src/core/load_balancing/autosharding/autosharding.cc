//
// Copyright 2026 gRPC authors.
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

#include "src/core/load_balancing/autosharding/autosharding.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/pick_first/pick_first.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/crash.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/ref_counted_string.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "src/core/util/work_serializer.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kAutoSharding = "autosharding_experimental";
constexpr Duration kDefaultInitialAssignmentTimeout = Duration::Seconds(60);

class AutoShardingLbConfig final : public LoadBalancingPolicy::Config {
 public:
  AutoShardingLbConfig() = default;

  AutoShardingLbConfig(const AutoShardingLbConfig&) = delete;
  AutoShardingLbConfig& operator=(const AutoShardingLbConfig&) = delete;

  AutoShardingLbConfig(AutoShardingLbConfig&& other) = delete;
  AutoShardingLbConfig& operator=(AutoShardingLbConfig&& other) = delete;

  absl::string_view name() const override { return kAutoSharding; }

  const std::string& channel_factory_key() const {
    return channel_factory_key_;
  }
  const std::string& autosharding_target() const {
    return autosharding_target_;
  }
  const std::string& key_header_name() const { return key_header_name_; }
  bool enable_fallback() const { return enable_fallback_; }
  Duration initial_assignment_timeout() const {
    return initial_assignment_timeout_;
  }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader =
        JsonObjectLoader<AutoShardingLbConfig>()
            .Field("channelFactoryKey",
                   &AutoShardingLbConfig::channel_factory_key_)
            .Field("autoshardingTarget",
                   &AutoShardingLbConfig::autosharding_target_)
            .Field("keyHeaderName", &AutoShardingLbConfig::key_header_name_)
            .OptionalField("enableFallback",
                           &AutoShardingLbConfig::enable_fallback_)
            .OptionalField("initialAssignmentTimeout",
                           &AutoShardingLbConfig::initial_assignment_timeout_)
            .Finish();
    return loader;
  }

  void JsonPostLoad(const Json&, const JsonArgs&, ValidationErrors* errors) {
    {
      ValidationErrors::ScopedField field(errors, ".channelFactoryKey");
      if (!errors->FieldHasErrors() && channel_factory_key_.empty()) {
        errors->AddError("must be non-empty");
      }
    }
    {
      ValidationErrors::ScopedField field(errors, ".autoshardingTarget");
      if (!errors->FieldHasErrors() && autosharding_target_.empty()) {
        errors->AddError("must be non-empty");
      }
    }
    {
      ValidationErrors::ScopedField field(errors, ".keyHeaderName");
      if (!errors->FieldHasErrors() && key_header_name_.empty()) {
        errors->AddError("must be non-empty");
      }
    }
    {
      ValidationErrors::ScopedField field(errors, ".initialAssignmentTimeout");
      if (!errors->FieldHasErrors() &&
          initial_assignment_timeout_.millis() <= 0) {
        errors->AddError("must be greater than zero");
      }
    }
  }

 private:
  std::string channel_factory_key_;
  std::string autosharding_target_;
  std::string key_header_name_;
  bool enable_fallback_ = false;
  Duration initial_assignment_timeout_ = kDefaultInitialAssignmentTimeout;
};

//
// autosharding LB policy
//

class AutoSharding final : public LoadBalancingPolicy {
 public:
  explicit AutoSharding(Args args);

  absl::string_view name() const override { return kAutoSharding; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  class AutoShardingEndpoint;

  // A complete assignment received from the sharding service.
  class Assignment {
   public:
    // A key range and its assigned endpoints.
    struct Slice {
      // Inclusive start key.  Empty for -infinity.
      std::string start_key;
      // Exclusive end key.  Empty for +infinity.
      std::string end_key;
      // Indices into Assignment::endpoint_names_.
      std::vector<size_t> endpoints;
    };

    Assignment(std::vector<Slice> slices,
               std::vector<std::string> endpoint_names, int64_t generation)
        : slices_(std::move(slices)),
          endpoint_names_(std::move(endpoint_names)),
          generation_(generation) {}

    // Validates key ranges, overlaps, and endpoint indices. On success, sorts
    // slices by end_key and fills gaps to cover [-infinity, +infinity).
    absl::Status Validate();

    // Slices covering the keyspace after Validate().
    const std::vector<Slice>& slices() const { return slices_; }
    // Complete list of endpoint names in the assignment.
    const std::vector<std::string>& endpoint_names() const {
      return endpoint_names_;
    }
    int64_t generation() const { return generation_; }

   private:
    struct EndKeyLessThan {
      bool operator()(absl::string_view a, absl::string_view b) const {
        if (a.empty() && b.empty()) return false;
        if (a.empty()) return false;  // a is +infinity, cannot be < b
        if (b.empty()) return true;   // b is +infinity, any finite a is < b
        return a < b;
      }
    };

    void FillGaps();

    std::vector<Slice> slices_;
    std::vector<std::string> endpoint_names_;
    int64_t generation_ = 0;
  };

  //
  // SliceMap
  //
  // Immutable, lookup-optimized mapping of slices to endpoint indices.
  //

  class SliceMap final : public RefCounted<SliceMap> {
   public:
    struct Entry {
      // Inclusive start key.  Empty for -infinity.
      std::string start_key;
      // Exclusive end key.  Empty for +infinity.
      std::string end_key;
      // Indices into Picker::endpoints_.
      std::vector<size_t> endpoints;

      bool Contains(absl::string_view key) const {
        if (!start_key.empty() && key < start_key) return false;
        if (!end_key.empty() && key >= end_key) return false;
        return true;
      }
    };

    // Comparator used by std::upper_bound to compare a request key against a
    // slice's exclusive end_key.
    struct KeyLessThanSliceEndKey {
      bool operator()(absl::string_view key, const Entry& entry) const {
        if (entry.end_key.empty()) return true;  // Empty end_key is +infinity.
        return key < entry.end_key;
      }
    };

    // Populates slices from assignment and endpoint_map, filling empty slices
    // (or creating a whole-keyspace slice if no assignment) with the fallback
    // pool if fallback_enabled is true.
    SliceMap(const std::optional<Assignment>& assignment,
             const std::map<std::string, OrphanablePtr<AutoShardingEndpoint>>&
                 endpoint_map,
             bool fallback_enabled);

    const std::vector<Entry>& slices() const { return slices_; }
    const std::vector<size_t>& fallback_pool() const { return fallback_pool_; }
    int64_t generation() const { return generation_; }

    // Returns endpoint indices covering key, or empty span if none available.
    absl::Span<const size_t> Lookup(absl::string_view key) const {
      if (slices_.empty()) return {};
      auto it = std::upper_bound(slices_.begin(), slices_.end(), key,
                                 KeyLessThanSliceEndKey());
      if (it != slices_.end() && it->Contains(key)) {
        return it->endpoints;
      }
      return {};
    }

   private:
    std::vector<Entry> slices_;
    std::vector<size_t> fallback_pool_;
    int64_t generation_ = 0;
  };

  // State for a particular endpoint.  Delegates to a pick_first child policy.
  class AutoShardingEndpoint final
      : public InternallyRefCounted<AutoShardingEndpoint> {
   public:
    // index is the index of this endpoint within the Name Resolver update.
    AutoShardingEndpoint(RefCountedPtr<AutoSharding> autosharding, size_t index)
        : autosharding_(std::move(autosharding)), index_(index) {}

    void Orphan() override;

    size_t index() const { return index_; }

    absl::Status UpdateLocked(size_t index);

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }

    // Returns info about the endpoint to be stored in the picker.
    struct EndpointInfo {
      RefCountedPtr<AutoShardingEndpoint> endpoint;
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
    absl::Status UpdateChildPolicyLocked();

    // Called when the child policy reports a connectivity state update.
    void OnStateUpdate(grpc_connectivity_state new_state,
                       const absl::Status& status,
                       RefCountedPtr<SubchannelPicker> picker);

    // Ref to our parent.
    RefCountedPtr<AutoSharding> autosharding_;
    size_t index_;  // Index into AutoSharding::endpoints_ of this endpoint.

    // The pick_first child policy.  Created lazily, on first use.
    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
    absl::Status status_;
    RefCountedPtr<SubchannelPicker> picker_;
  };

  // Timer for the initial assignment from the sharding service, during which
  // RPCs are queued.
  class InitialAssignmentTimer final
      : public InternallyRefCounted<InitialAssignmentTimer> {
   public:
    InitialAssignmentTimer(RefCountedPtr<AutoSharding> autosharding,
                           Duration timeout);

    void Orphan() override;

   private:
    void OnTimerLocked();

    RefCountedPtr<AutoSharding> autosharding_;
    std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
        timer_handle_;
  };

  class Picker final : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<AutoSharding> autosharding,
           RefCountedPtr<SliceMap> slice_map)
        : autosharding_(std::move(autosharding)),
          slice_map_(std::move(slice_map)),
          endpoints_(autosharding_->endpoints_.size()),
          key_header_name_(autosharding_->key_header_name_),
          resolution_note_(autosharding_->resolution_note_) {
      for (const auto& [_, endpoint] : autosharding_->endpoint_map_) {
        endpoints_[endpoint->index()] = endpoint->GetInfoForPicker();
      }
    }

    PickResult Pick(PickArgs args) override;

   private:
    // A fire-and-forget class that schedules endpoint connection attempts
    // on the control plane WorkSerializer.
    class EndpointConnectionAttempter final {
     public:
      EndpointConnectionAttempter(RefCountedPtr<AutoSharding> autosharding,
                                  RefCountedPtr<AutoShardingEndpoint> endpoint)
          : autosharding_(std::move(autosharding)),
            endpoint_(std::move(endpoint)) {
        // Hop into ExecCtx, so that we're not holding the data plane mutex
        // while we run control-plane code.
        GRPC_CLOSURE_INIT(&closure_, RunInExecCtx, this, nullptr);
        ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
      }

     private:
      static void RunInExecCtx(void* arg, grpc_error_handle /*error*/) {
        auto* self = static_cast<EndpointConnectionAttempter*>(arg);
        self->autosharding_->work_serializer()->Run([self]() {
          if (!self->autosharding_->shutdown_) {
            self->endpoint_->RequestConnectionLocked();
          }
          delete self;
        });
      }

      RefCountedPtr<AutoSharding> autosharding_;
      RefCountedPtr<AutoShardingEndpoint> endpoint_;
      grpc_closure closure_;
    };

    RefCountedPtr<AutoSharding> autosharding_;
    RefCountedPtr<SliceMap> slice_map_;
    std::vector<AutoShardingEndpoint::EndpointInfo> endpoints_;
    RefCountedStringValue key_header_name_;
    std::string resolution_note_;
  };

  ~AutoSharding() override;

  void ShutdownLocked() override;

  // Updates the aggregate policy's connectivity state based on the
  // number of endpoints in each state, creating a new picker.
  // If the call to this method is triggered by an endpoint entering
  // TRANSIENT_FAILURE, then status is the status reported by the endpoint.
  void UpdateAggregatedConnectivityStateLocked(absl::Status status);

  // Creates a channel to the sharding service and starts the initial
  // assignment timer.
  // TODO(bpawan): Channel creation and stream management will be added
  // with the OSS Autosharding gRPC protocol (gRFC A119).
  void CreateShardingServiceChannelLocked();

  void RestartInitialAssignmentTimerLocked();

  bool assignment_pending() const {
    return initial_assignment_timer_ != nullptr && !assignment_.has_value();
  }

  void OnInitialAssignmentTimeoutLocked();

  // Called when an assignment is received from the sharding service.
  // Validates the assignment, returning non-OK on failure (caller should NACK).
  // TODO(bpawan): Will be wired up when the protocol stream is implemented.
  absl::Status OnAssignmentReceivedLocked(Assignment assignment);

  std::map<std::string, OrphanablePtr<AutoShardingEndpoint>> endpoint_map_;
  EndpointAddressesList endpoints_;
  ChannelArgs args_;
  std::optional<Assignment> assignment_;
  RefCountedPtr<SliceMap> slice_map_;
  RefCountedPtr<AutoShardingLbConfig> config_;
  RefCountedStringValue key_header_name_;
  // True after creating a new channel until its first assignment is received,
  // so generation checks are bypassed for the new channel.
  bool awaiting_first_assignment_on_new_channel_ = false;
  OrphanablePtr<InitialAssignmentTimer> initial_assignment_timer_;
  std::string resolution_note_;

  // TODO(roth): If we ever change the helper UpdateState() API to not
  // need the status reported for TRANSIENT_FAILURE state (because
  // it's not currently actually used for anything outside of the picker),
  // then we will no longer need this data member.
  absl::Status last_failure_;

  bool shutdown_ = false;
};

//
// AutoSharding::Picker
//

AutoSharding::PickResult AutoSharding::Picker::Pick(PickArgs args) {
  // Extract the sharding key from the request metadata.
  std::string buffer;
  auto key =
      args.initial_metadata->Lookup(key_header_name_.as_string_view(), &buffer);
  if (!key.has_value()) {
    return PickResult::Fail(absl::InternalError(
        absl::StrCat("slice key header \"", key_header_name_.as_string_view(),
                     "\" not present")));
  }
  // Look up the endpoints covering the key.
  absl::Span<const size_t> indices = slice_map_->Lookup(*key);
  if (indices.empty()) {
    std::string message = "no endpoint available";
    if (!resolution_note_.empty()) {
      absl::StrAppend(&message, " (", resolution_note_, ")");
    }
    return PickResult::Fail(absl::UnavailableError(message));
  }
  // Scan the pool for a READY endpoint, starting from a random index and
  // triggering at most one connection attempt on an IDLE endpoint.
  size_t first_index = absl::Uniform<size_t>(SharedBitGen(), 0, indices.size());
  bool requested_connection = false;
  bool found_connecting = false;
  for (size_t i = 0; i < indices.size(); ++i) {
    const auto& endpoint_info =
        endpoints_[indices[(first_index + i) % indices.size()]];
    switch (endpoint_info.state) {
      case GRPC_CHANNEL_READY:
        return endpoint_info.picker->Pick(args);
      case GRPC_CHANNEL_CONNECTING:
        found_connecting = true;
        break;
      case GRPC_CHANNEL_IDLE:
        if (!requested_connection) {
          new EndpointConnectionAttempter(
              autosharding_.Ref(DEBUG_LOCATION, "EndpointConnectionAttempter"),
              endpoint_info.endpoint);
          requested_connection = true;
        }
        break;
      default:
        break;
    }
  }
  if (requested_connection || found_connecting) {
    return PickResult::Queue();
  }
  // All endpoints in TRANSIENT_FAILURE. Delegate to the first endpoint's
  // picker to yield a detailed error message.
  const auto& endpoint_info = endpoints_[indices[first_index]];
  if (endpoint_info.picker != nullptr) {
    return endpoint_info.picker->Pick(args);
  }
  return PickResult::Fail(
      endpoint_info.status.ok()
          ? absl::UnavailableError("all endpoints in transient failure")
          : endpoint_info.status);
}

//
// AutoSharding::InitialAssignmentTimer
//

AutoSharding::InitialAssignmentTimer::InitialAssignmentTimer(
    RefCountedPtr<AutoSharding> autosharding, Duration timeout)
    : autosharding_(std::move(autosharding)) {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << autosharding_.get()
      << "] starting initial assignment timer for " << timeout.millis() << "ms";
  timer_handle_ =
      autosharding_->channel_control_helper()->GetEventEngine()->RunAfter(
          timeout, [self = Ref(DEBUG_LOCATION, "Timer")]() mutable {
            ExecCtx exec_ctx;
            auto self_ptr = self.get();
            self_ptr->autosharding_->work_serializer()->Run(
                [self = std::move(self)]() { self->OnTimerLocked(); });
          });
}

void AutoSharding::InitialAssignmentTimer::Orphan() {
  if (timer_handle_.has_value()) {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << autosharding_.get()
        << "] cancelling initial assignment timer";
    autosharding_->channel_control_helper()->GetEventEngine()->Cancel(
        *timer_handle_);
    timer_handle_.reset();
  }
  Unref();
}

void AutoSharding::InitialAssignmentTimer::OnTimerLocked() {
  if (!timer_handle_.has_value()) return;  // Already fired or cancelled.
  timer_handle_.reset();
  autosharding_->OnInitialAssignmentTimeoutLocked();
}

//
// AutoSharding::AutoShardingEndpoint::Helper
//

class AutoSharding::AutoShardingEndpoint::Helper final
    : public LoadBalancingPolicy::DelegatingChannelControlHelper {
 public:
  explicit Helper(RefCountedPtr<AutoShardingEndpoint> endpoint)
      : endpoint_(std::move(endpoint)) {}

  ~Helper() override { endpoint_.reset(DEBUG_LOCATION, "Helper"); }

  void UpdateState(
      grpc_connectivity_state state, const absl::Status& status,
      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) override {
    endpoint_->OnStateUpdate(state, status, std::move(picker));
  }

 private:
  LoadBalancingPolicy::ChannelControlHelper* parent_helper() const override {
    return endpoint_->autosharding_->channel_control_helper();
  }

  RefCountedPtr<AutoShardingEndpoint> endpoint_;
};

//
// AutoSharding::AutoShardingEndpoint
//

void AutoSharding::AutoShardingEndpoint::Orphan() {
  if (child_policy_ != nullptr) {
    // Remove pollset_set linkage.
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     autosharding_->interested_parties());
    child_policy_.reset();
    picker_.reset();
  }
  Unref();
}

absl::Status AutoSharding::AutoShardingEndpoint::UpdateLocked(size_t index) {
  index_ = index;
  if (child_policy_ == nullptr) return absl::OkStatus();
  return UpdateChildPolicyLocked();
}

void AutoSharding::AutoShardingEndpoint::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void AutoSharding::AutoShardingEndpoint::RequestConnectionLocked() {
  if (child_policy_ == nullptr) {
    CreateChildPolicy();
  } else {
    child_policy_->ExitIdleLocked();
  }
}

void AutoSharding::AutoShardingEndpoint::CreateChildPolicy() {
  GRPC_CHECK(child_policy_ == nullptr);
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = autosharding_->work_serializer();
  lb_policy_args.args =
      autosharding_->args_
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_ENABLE_HEALTH_CHECKING, true)
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_OMIT_STATUS_MESSAGE_PREFIX, true);
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  child_policy_ =
      CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
          "pick_first", std::move(lb_policy_args));
  if (GRPC_TRACE_FLAG_ENABLED(autosharding_lb)) {
    const EndpointAddresses& endpoint = autosharding_->endpoints_[index_];
    LOG(INFO) << "[AS " << autosharding_.get() << "] endpoint " << this
              << " (index " << index_ << " of "
              << autosharding_->endpoints_.size() << ", " << endpoint.ToString()
              << "): created child policy " << child_policy_.get();
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy.
  grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                   autosharding_->interested_parties());
  // If the child policy returns a non-OK status, request re-resolution.
  absl::Status status = UpdateChildPolicyLocked();
  if (!status.ok()) {
    autosharding_->channel_control_helper()->RequestReresolution();
  }
}

absl::Status AutoSharding::AutoShardingEndpoint::UpdateChildPolicyLocked() {
  // Construct pick_first config.
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray(
              {Json::FromObject({{"pick_first", Json::FromObject({})}})}));
  GRPC_CHECK(config.ok());
  // Update child policy.
  LoadBalancingPolicy::UpdateArgs update_args;
  update_args.addresses = std::make_shared<SingleEndpointIterator>(
      autosharding_->endpoints_[index_]);
  update_args.args = autosharding_->args_;
  update_args.config = std::move(*config);
  return child_policy_->UpdateLocked(std::move(update_args));
}

void AutoSharding::AutoShardingEndpoint::OnStateUpdate(
    grpc_connectivity_state new_state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << autosharding_.get() << "] connectivity changed for endpoint "
      << this << " (" << autosharding_->endpoints_[index_].ToString()
      << ", child_policy=" << child_policy_.get()
      << "): prev_state=" << ConnectivityStateName(connectivity_state_)
      << " new_state=" << ConnectivityStateName(new_state) << " (" << status
      << ")";
  if (child_policy_ == nullptr) return;
  // Update state.
  connectivity_state_ = new_state;
  status_ = status;
  picker_ = std::move(picker);
  // Update the aggregated connectivity state.
  autosharding_->UpdateAggregatedConnectivityStateLocked(status);
}

//
// AutoSharding
//

AutoSharding::AutoSharding(Args args) : LoadBalancingPolicy(std::move(args)) {
  GRPC_TRACE_LOG(autosharding_lb, INFO) << "[AS " << this << "] Created";
}

AutoSharding::~AutoSharding() {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] Destroying AutoSharding policy";
}

void AutoSharding::ShutdownLocked() {
  GRPC_TRACE_LOG(autosharding_lb, INFO) << "[AS " << this << "] Shutting down";
  shutdown_ = true;
  initial_assignment_timer_.reset();
  endpoint_map_.clear();
  slice_map_.reset();
  assignment_.reset();
}

void AutoSharding::ResetBackoffLocked() {
  for (const auto& [_, endpoint] : endpoint_map_) {
    endpoint->ResetBackoffLocked();
  }
}

absl::Status AutoSharding::UpdateLocked(UpdateArgs args) {
  // Extracts hostname from endpoint attribute (gRFC A81) or first address.
  auto compute_hostname = [](const EndpointAddresses& endpoint) -> std::string {
    auto hostname_arg = endpoint.args().GetString(GRPC_ARG_ADDRESS_NAME);
    if (hostname_arg.has_value()) return std::string(*hostname_arg);
    if (endpoint.addresses().empty()) return "";
    auto addr_str =
        grpc_sockaddr_to_string(&endpoint.addresses().front(), false);
    return addr_str.ok() ? std::move(*addr_str) : "";
  };
  std::vector<std::string> hostnames;
  // Check address list.
  if (args.addresses.ok()) {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this << "] received update";
    // De-duplicate endpoints by hostname (last one wins).
    endpoints_.clear();
    std::map<std::string, size_t> endpoint_indices;
    (*args.addresses)->ForEach([&](const EndpointAddresses& endpoint) {
      const std::string hostname = compute_hostname(endpoint);
      auto [it, inserted] =
          endpoint_indices.emplace(hostname, endpoints_.size());
      if (!inserted) {
        GRPC_TRACE_LOG(autosharding_lb, INFO)
            << "[AS " << this << "] duplicate endpoint for \"" << hostname
            << "\", replacing previous entry";
        // Duplicate hostname; replace earlier entry with the latest one.
        endpoints_[it->second] = endpoint;
      } else {
        endpoints_.push_back(endpoint);
        hostnames.push_back(hostname);
      }
    });
  } else {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this << "] received update with addresses error: "
        << args.addresses.status();
    // If we already have an endpoint list, then keep using the existing
    // list, but still report back that the update was not accepted.
    if (!endpoints_.empty()) return args.addresses.status();
  }
  // Save channel args.
  args_ = std::move(args.args);
  // Save config.
  auto* new_config = DownCast<AutoShardingLbConfig*>(args.config.get());
  key_header_name_ = RefCountedStringValue(new_config->key_header_name());
  const bool channel_factory_key_changed =
      config_ == nullptr ||
      new_config->channel_factory_key() != config_->channel_factory_key();
  const bool autosharding_target_changed =
      config_ != nullptr &&
      new_config->autosharding_target() != config_->autosharding_target();
  config_ = args.config.TakeAsSubclass<AutoShardingLbConfig>();
  if (autosharding_target_changed) {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this << "] autosharding target changed to \""
        << config_->autosharding_target() << "\", resetting assignment";
    // Invalidate previous assignment when target changes.
    // TODO(bpawan): Start a new WatchShardingAssignment stream here when
    // protocol is implemented.
    assignment_.reset();
    slice_map_.reset();
  }
  if (channel_factory_key_changed) {
    CreateShardingServiceChannelLocked();
  } else if (autosharding_target_changed) {
    RestartInitialAssignmentTimerLocked();
  }
  // Update endpoint map.
  std::map<std::string, OrphanablePtr<AutoShardingEndpoint>> endpoint_map;
  std::vector<std::string> errors;
  for (size_t i = 0; i < endpoints_.size(); ++i) {
    const std::string& hostname = hostnames[i];
    // If present in old map, retain it; otherwise, create a new one.
    auto it = endpoint_map_.find(hostname);
    if (it != endpoint_map_.end()) {
      absl::Status status = it->second->UpdateLocked(i);
      if (!status.ok()) {
        errors.emplace_back(
            absl::StrCat("endpoint ", hostname, ": ", status.ToString()));
      }
      endpoint_map[hostname] = std::move(it->second);
    } else {
      endpoint_map[hostname] = MakeOrphanable<AutoShardingEndpoint>(
          RefAsSubclass<AutoSharding>(), i);
    }
  }
  endpoint_map_ = std::move(endpoint_map);
  // Update resolution note.
  resolution_note_ = std::move(args.resolution_note);
  // If the address list is empty, report TRANSIENT_FAILURE.
  if (endpoints_.empty()) {
    absl::Status status = args.addresses.ok()
                              ? absl::UnavailableError(absl::StrCat(
                                    "empty address list: ", resolution_note_))
                              : args.addresses.status();
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        MakeRefCounted<TransientFailurePicker>(status));
    return status;
  }
  // Build new SliceMap.
  slice_map_ = MakeRefCounted<SliceMap>(assignment_, endpoint_map_,
                                        config_->enable_fallback());
  // Return a new picker.
  UpdateAggregatedConnectivityStateLocked(absl::OkStatus());
  if (!errors.empty()) {
    return absl::UnavailableError(absl::StrCat(
        "errors from children: [", absl::StrJoin(errors, "; "), "]"));
  }
  return absl::OkStatus();
}

void AutoSharding::CreateShardingServiceChannelLocked() {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] creating channel to sharding service for key \""
      << config_->channel_factory_key() << "\"";
  // TODO(bpawan): Use Channel Factory to create channel and stream (gRFC A119).
  // Fresh channels bypass previous generation filtering until first assignment.
  awaiting_first_assignment_on_new_channel_ = true;
  RestartInitialAssignmentTimerLocked();
}

void AutoSharding::RestartInitialAssignmentTimerLocked() {
  initial_assignment_timer_.reset();
  initial_assignment_timer_ = MakeOrphanable<InitialAssignmentTimer>(
      RefAsSubclass<AutoSharding>(DEBUG_LOCATION, "InitialAssignmentTimer"),
      config_->initial_assignment_timeout());
}

//
// AutoSharding::SliceMap
//

AutoSharding::SliceMap::SliceMap(
    const std::optional<Assignment>& assignment,
    const std::map<std::string, OrphanablePtr<AutoShardingEndpoint>>&
        endpoint_map,
    bool fallback_enabled) {
  // Populate the fallback pool, deterministically sorted by endpoint index.
  fallback_pool_.reserve(endpoint_map.size());
  for (const auto& [_, endpoint] : endpoint_map) {
    fallback_pool_.push_back(endpoint->index());
  }
  std::sort(fallback_pool_.begin(), fallback_pool_.end());
  // With no assignment, create a single slice covering the entire keyspace.
  if (!assignment.has_value()) {
    Entry entry;
    entry.start_key = "";
    entry.end_key = "";
    if (fallback_enabled) {
      entry.endpoints = fallback_pool_;
    }
    slices_.push_back(std::move(entry));
    return;
  }
  generation_ = assignment->generation();
  // Map assignment endpoint index to picker endpoint index.
  std::vector<std::optional<size_t>> assignment_endpoint_to_picker_index(
      assignment->endpoint_names().size(), std::nullopt);
  for (size_t i = 0; i < assignment->endpoint_names().size(); ++i) {
    auto it = endpoint_map.find(assignment->endpoint_names()[i]);
    if (it != endpoint_map.end()) {
      assignment_endpoint_to_picker_index[i] = it->second->index();
    }
  }
  // Build entries for each slice in the assignment.
  slices_.reserve(assignment->slices().size());
  for (const auto& slice : assignment->slices()) {
    Entry entry;
    entry.start_key = slice.start_key;
    entry.end_key = slice.end_key;
    entry.endpoints.reserve(slice.endpoints.size());
    for (size_t idx : slice.endpoints) {
      if (idx < assignment_endpoint_to_picker_index.size() &&
          assignment_endpoint_to_picker_index[idx].has_value()) {
        entry.endpoints.push_back(*assignment_endpoint_to_picker_index[idx]);
      }
    }
    if (entry.endpoints.empty() && fallback_enabled) {
      entry.endpoints = fallback_pool_;
    }
    slices_.push_back(std::move(entry));
  }
}

void AutoSharding::OnInitialAssignmentTimeoutLocked() {
  if (shutdown_) return;
  initial_assignment_timer_.reset();
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] initial assignment timer expired";
  // Initial assignment timeout expired; stop using any existing assignment.
  assignment_.reset();
  if (endpoints_.empty()) return;
  slice_map_ = MakeRefCounted<SliceMap>(assignment_, endpoint_map_,
                                        config_->enable_fallback());
  UpdateAggregatedConnectivityStateLocked(absl::OkStatus());
}

//
// AutoSharding::Assignment
//

absl::Status AutoSharding::Assignment::Validate() {
  // Sort slices by exclusive end_key to check for overlaps.
  std::sort(slices_.begin(), slices_.end(),
            [](const Slice& lhs, const Slice& rhs) {
              return EndKeyLessThan()(lhs.end_key, rhs.end_key);
            });
  bool contains_overlap = false;
  absl::string_view prev_end;
  bool first = true;
  for (const auto& slice : slices_) {
    // Reject empty or reversed key ranges.
    if (!slice.start_key.empty() && !slice.end_key.empty() &&
        slice.start_key >= slice.end_key) {
      return absl::InvalidArgumentError("slice key range is empty or reversed");
    }
    // Check for overlap with preceding slice.
    if (!first && (slice.start_key.empty() || prev_end.empty() ||
                   slice.start_key < prev_end)) {
      contains_overlap = true;
    }
    prev_end = slice.end_key;
    first = false;
  }
  if (contains_overlap) {
    return absl::InvalidArgumentError(
        "assignment contains overlapping key ranges");
  }
  // Ensure all endpoint indices are within range.
  for (const auto& slice : slices_) {
    for (size_t idx : slice.endpoints) {
      if (idx >= endpoint_names_.size()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "slice contains invalid endpoint index ", idx, " (assignment has ",
            endpoint_names_.size(), " endpoint names)"));
      }
    }
  }
  // Fill any gaps so slices cover [-infinity, +infinity).
  FillGaps();
  return absl::OkStatus();
}

void AutoSharding::Assignment::FillGaps() {
  std::vector<Slice> filled;
  if (slices_.empty()) {
    filled.emplace_back();
    slices_ = std::move(filled);
    return;
  }
  filled.reserve(2 * slices_.size() + 1);
  std::string prev_end;  // Empty means -infinity.
  for (auto& slice : slices_) {
    if (slice.start_key != prev_end) {
      Slice gap;
      gap.start_key = prev_end;
      gap.end_key = slice.start_key;
      filled.push_back(std::move(gap));
    }
    prev_end = slice.end_key;
    filled.push_back(std::move(slice));
  }
  if (!prev_end.empty()) {
    Slice gap;
    gap.start_key = std::move(prev_end);
    filled.push_back(std::move(gap));
  }
  slices_ = std::move(filled);
}

absl::Status AutoSharding::OnAssignmentReceivedLocked(Assignment assignment) {
  if (shutdown_) return absl::OkStatus();
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] received assignment with generation "
      << assignment.generation();
  // Ignore stale assignments (bypassed on first assignment of a new channel).
  if (!awaiting_first_assignment_on_new_channel_ && assignment_.has_value() &&
      assignment.generation() <= assignment_->generation()) {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this << "] ignoring stale assignment with generation "
        << assignment.generation() << " (current generation is "
        << assignment_->generation() << ")";
    return absl::OkStatus();
  }
  // Validate the assignment (caller NACKs on error).
  absl::Status status = assignment.Validate();
  if (!status.ok()) {
    LOG(ERROR) << "[AS " << this
               << "] rejecting invalid assignment: " << status;
    return status;
  }
  initial_assignment_timer_.reset();
  assignment_ = std::move(assignment);
  awaiting_first_assignment_on_new_channel_ = false;
  if (endpoints_.empty()) return absl::OkStatus();
  slice_map_ = MakeRefCounted<SliceMap>(assignment_, endpoint_map_,
                                        config_->enable_fallback());
  UpdateAggregatedConnectivityStateLocked(absl::OkStatus());
  return absl::OkStatus();
}

void AutoSharding::UpdateAggregatedConnectivityStateLocked(
    absl::Status status) {
  // Count the number of endpoints in each state.
  size_t num_idle = 0;
  size_t num_connecting = 0;
  size_t num_ready = 0;
  size_t num_transient_failure = 0;
  AutoShardingEndpoint* idle_endpoint = nullptr;
  for (const auto& [_, endpoint] : endpoint_map_) {
    switch (endpoint->connectivity_state()) {
      case GRPC_CHANNEL_READY:
        ++num_ready;
        break;
      case GRPC_CHANNEL_IDLE:
        ++num_idle;
        if (idle_endpoint == nullptr) idle_endpoint = endpoint.get();
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
  // The overall aggregation rules here are the same as those used by the
  // ring_hash LB policy (gRFC A42):
  // 1. If there is at least one endpoint in READY state, report READY.
  // 2. If there are 2 or more endpoints in TRANSIENT_FAILURE state, report
  //    TRANSIENT_FAILURE.
  // 3. If there is at least one endpoint in CONNECTING state, report
  //    CONNECTING.
  // 4. If there is one endpoint in TRANSIENT_FAILURE state and there is
  //    more than one endpoint, report CONNECTING.
  // 5. If there is at least one endpoint in IDLE state, report IDLE.
  // 6. Otherwise, report TRANSIENT_FAILURE.
  grpc_connectivity_state state;
  if (num_ready > 0) {
    state = GRPC_CHANNEL_READY;
  } else if (num_transient_failure >= 2) {
    state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  } else if (num_connecting > 0) {
    state = GRPC_CHANNEL_CONNECTING;
  } else if (num_transient_failure == 1 && endpoint_map_.size() > 1) {
    state = GRPC_CHANNEL_CONNECTING;
  } else if (num_idle > 0) {
    state = GRPC_CHANNEL_IDLE;
  } else {
    state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  }
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] setting connectivity state to "
      << ConnectivityStateName(state) << " (num_idle=" << num_idle
      << ", num_connecting=" << num_connecting << ", num_ready=" << num_ready
      << ", num_transient_failure=" << num_transient_failure
      << ", size=" << endpoint_map_.size() << ")";
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
  // Queue picks while waiting for the initial assignment.
  // TODO(bpawan): Set delay_type per gRFC A121 once supported.
  RefCountedPtr<SubchannelPicker> picker;
  if (assignment_pending()) {
    picker = MakeRefCounted<QueuePicker>(
        RefAsSubclass<AutoSharding>(DEBUG_LOCATION, "AutoShardingQueuePicker"));
  } else {
    picker = MakeRefCounted<Picker>(
        RefAsSubclass<AutoSharding>(DEBUG_LOCATION, "AutoShardingPicker"),
        slice_map_);
  }
  channel_control_helper()->UpdateState(state, status, std::move(picker));
  // If in TRANSIENT_FAILURE or CONNECTING without a CONNECTING endpoint,
  // trigger a connection attempt on an IDLE endpoint to prevent premature
  // failover when used as a child of the priority policy (same as ring_hash).
  if ((state == GRPC_CHANNEL_CONNECTING ||
       state == GRPC_CHANNEL_TRANSIENT_FAILURE) &&
      num_connecting == 0 && idle_endpoint != nullptr) {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this
        << "] triggering internal connection attempt for endpoint "
        << idle_endpoint << " ("
        << endpoints_[idle_endpoint->index()].ToString() << ") (index "
        << idle_endpoint->index() << " of " << endpoints_.size() << ")";
    idle_endpoint->RequestConnectionLocked();
  }
}

//
// factory
//

class AutoShardingFactory final : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<AutoSharding>(std::move(args));
  }

  absl::string_view name() const override { return kAutoSharding; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadFromJson<RefCountedPtr<AutoShardingLbConfig>>(
        json, JsonArgs(), "errors validating autosharding LB policy config");
  }
};

}  // namespace

void RegisterAutoShardingLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<AutoShardingFactory>());
}

}  // namespace grpc_core
