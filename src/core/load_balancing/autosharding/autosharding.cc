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

#include <grpc/channel_factory.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/client_channel/channel_factory.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/autosharding/autosharding_assignment.h"
#include "src/core/load_balancing/autosharding/autosharding_client.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/pick_first/pick_first.h"
#include "src/core/load_balancing/weighted_target/weighted_target.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/crash.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/uuid_v4.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/xds_client/xds_transport.h"
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

std::string GenerateUuid() {
  SharedBitGen bit_gen;
  return GenerateUUIDv4(absl::Uniform<uint64_t>(bit_gen),
                        absl::Uniform<uint64_t>(bit_gen));
}

// Returns target with the first "%s" token replaced by locality (gRFC A119).
std::string EffectiveTarget(absl::string_view target,
                            absl::string_view locality) {
  const size_t pos = target.find("%s");
  if (pos == absl::string_view::npos) return std::string(target);
  return absl::StrCat(target.substr(0, pos), locality, target.substr(pos + 2));
}

// Returns an UNAVAILABLE status with message, followed by the resolution
// note in parentheses if it is not empty.
absl::Status UnavailableErrorWithNote(absl::string_view message,
                                      absl::string_view resolution_note) {
  if (resolution_note.empty()) return absl::UnavailableError(message);
  return absl::UnavailableError(
      absl::StrCat(message, " (", resolution_note, ")"));
}

RefCountedPtr<XdsTransport> CreateTransportFromChannelFactory(
    experimental::ChannelFactory& channel_factory,
    absl::string_view channel_factory_key) {
  std::unique_ptr<experimental::ChannelFactory::ChannelHandle> handle =
      channel_factory.CreateChannel(channel_factory_key);
  // The channel factory returns a lame channel on failure, never null.
  GRPC_CHECK(handle != nullptr);
  // The ChannelFactory contract requires implementations to return a handle
  // obtained from a gRPC-provided factory, so it can be downcast to
  // ChannelHandleImpl.
  RefCountedPtr<XdsTransport> transport =
      DownCast<const ChannelHandleImpl&>(*handle).transport();
  GRPC_CHECK(transport != nullptr);
  return transport;
}

//
// autosharding LB policy
//

class AutoshardingLbPolicy final : public LoadBalancingPolicy {
 public:
  explicit AutoshardingLbPolicy(Args args);

  absl::string_view name() const override { return kAutoSharding; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  class AutoShardingEndpoint;

  //
  // SliceMap
  //
  // Immutable, lookup-optimized mapping of slices to endpoint indices.
  //

  class SliceMap final : public RefCounted<SliceMap> {
   public:
    struct Entry {
      // Exclusive end key (backed by assignment_).  Empty for +infinity.
      absl::string_view end_key;
      // Indices into Picker::endpoints_.
      std::vector<size_t> endpoints;
      bool fallback_disabled = false;
    };

    // Populates slices from assignment and endpoint_map.
    SliceMap(RefCountedPtr<const AutoshardingAssignment> assignment,
             const std::map<std::string, OrphanablePtr<AutoShardingEndpoint>>&
                 endpoint_map);

    const std::vector<Entry>& slices() const { return slices_; }

    // Returns the slice covering key. Lookup is guaranteed to succeed
    // because slices partition the entire keyspace.
    const Entry& Lookup(absl::string_view key) const {
      auto it = std::upper_bound(slices_.begin(), slices_.end(), key,
                                 [](absl::string_view key, const Entry& entry) {
                                   // An empty end_key means +infinity.
                                   return entry.end_key.empty() ||
                                          key < entry.end_key;
                                 });
      GRPC_CHECK(it != slices_.end());
      return *it;
    }

   private:
    RefCountedPtr<const AutoshardingAssignment> assignment_;
    std::vector<Entry> slices_;
  };

  // State for a particular endpoint.  Delegates to a pick_first child policy.
  class AutoShardingEndpoint final
      : public InternallyRefCounted<AutoShardingEndpoint> {
   public:
    // index is the index of this endpoint in AutoshardingLbPolicy::endpoints_.
    AutoShardingEndpoint(RefCountedPtr<AutoshardingLbPolicy> autosharding_lb,
                         size_t index)
        : autosharding_lb_(std::move(autosharding_lb)), index_(index) {}

    void Orphan() override;

    size_t index() const { return index_; }

    absl::Status UpdateLocked(size_t index);

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }

    // Info about the endpoint, stored in the picker.
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
    // asks the child to exit IDLE.  Does nothing if the endpoint has been
    // removed.
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
    RefCountedPtr<AutoshardingLbPolicy> autosharding_lb_;
    size_t index_;

    // The pick_first child policy.  Created lazily, on first use.
    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
    absl::Status status_;
    RefCountedPtr<SubchannelPicker> picker_;

    // Set by Orphan() when a resolver update removes this endpoint.  An old
    // picker can still hold a ref to this endpoint, so this prevents it from
    // creating a child policy for a removed endpoint (gRFC A119).
    bool orphaned_ = false;
  };

  class Picker final : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<AutoshardingLbPolicy> autosharding_lb,
           RefCountedPtr<SliceMap> slice_map)
        : autosharding_lb_(std::move(autosharding_lb)),
          config_(autosharding_lb_->config_),
          slice_map_(std::move(slice_map)),
          endpoints_(autosharding_lb_->endpoints_.size()),
          resolution_note_(autosharding_lb_->resolution_note_) {
      for (const auto& [_, endpoint] : autosharding_lb_->endpoint_map_) {
        endpoints_[endpoint->index()] = endpoint->GetInfoForPicker();
      }
    }

    PickResult Pick(PickArgs args) override;

   private:
    // Iterator interface over endpoint indices.
    class EndpointIndexIterator {
     public:
      virtual ~EndpointIndexIterator() = default;

      virtual size_t size() const = 0;
      virtual size_t operator[](size_t index) const = 0;
    };
    class SliceEndpointIndexIterator;
    class FallbackEndpointIndexIterator;

    bool IsPoolInFallback(absl::Span<const size_t> indices) const;

    PickResult PickFromEndpointIndices(const EndpointIndexIterator& indices,
                                       PickArgs args);

    void RequestConnectionForEndpoint(
        const RefCountedPtr<AutoShardingEndpoint>& endpoint);

    RefCountedPtr<AutoshardingLbPolicy> autosharding_lb_;
    RefCountedPtr<AutoShardingLbConfig> config_;
    RefCountedPtr<SliceMap> slice_map_;
    std::vector<AutoShardingEndpoint::EndpointInfo> endpoints_;
    std::string resolution_note_;
  };

  ~AutoshardingLbPolicy() override;

  void ShutdownLocked() override;

  // Updates the aggregate policy's connectivity state based on the
  // number of endpoints in each state, creating a new picker.
  // If the call to this method is triggered by an endpoint entering
  // TRANSIENT_FAILURE, then status is the status reported by the endpoint.
  void UpdateAggregatedConnectivityStateLocked(absl::Status status);

  // Reports state and picker to the channel, and stores state in
  // current_state_.
  void UpdateStateLocked(grpc_connectivity_state state,
                         const absl::Status& status,
                         RefCountedPtr<SubchannelPicker> picker);

  // Triggers a connection attempt on an IDLE endpoint chosen at random,
  // unless an endpoint is already CONNECTING (gRFC A119).
  void ConnectToRandomIdleEndpointLocked();

  // Creates (or recreates) client_.
  void CreateAutoshardingClientLocked(
      experimental::ChannelFactory& channel_factory);

  void OnAssignmentReceivedLocked(
      RefCountedPtr<AutoshardingClient> client,
      absl::StatusOr<RefCountedPtr<const AutoshardingAssignment>> result);

  void UpdateSliceMapLocked();

  std::map<std::string, OrphanablePtr<AutoShardingEndpoint>> endpoint_map_;
  EndpointAddressesList endpoints_;
  ChannelArgs args_;
  // Reused across stream restarts and client recreations (gRFC A119).
  const std::string uuid_;
  std::string effective_target_;
  // Recreated when channel_factory_key() or effective_target_ changes.
  OrphanablePtr<AutoshardingClient> client_;
  // Latest result from client_, retained across client recreations until the
  // new client reports.  nullptr queues picks until the first result
  // (gRFC A119).
  absl::StatusOr<RefCountedPtr<const AutoshardingAssignment>> assignment_ =
      nullptr;
  RefCountedPtr<SliceMap> slice_map_;
  RefCountedPtr<AutoShardingLbConfig> config_;
  std::string resolution_note_;
  // Set when the latest resolver update is a bad resolver state (empty
  // address list or missing channel factory).  While set, the policy reports
  // TRANSIENT_FAILURE, but keeps assignment_ for the next good update
  // (gRFC A119).
  absl::Status resolver_error_;
  // The state most recently reported to the channel.
  grpc_connectivity_state current_state_ = GRPC_CHANNEL_IDLE;

  // TODO(roth): If we ever change the helper UpdateState() API to not
  // need the status reported for TRANSIENT_FAILURE state (because
  // it's not currently actually used for anything outside of the picker),
  // then we will no longer need this data member.
  absl::Status last_failure_;
  bool shutdown_ = false;
};

//
// AutoshardingLbPolicy::Picker::SliceEndpointIndexIterator
//

class AutoshardingLbPolicy::Picker::SliceEndpointIndexIterator final
    : public EndpointIndexIterator {
 public:
  explicit SliceEndpointIndexIterator(absl::Span<const size_t> indices)
      : indices_(indices) {}

  size_t size() const override { return indices_.size(); }
  size_t operator[](size_t index) const override { return indices_[index]; }

 private:
  absl::Span<const size_t> indices_;
};

//
// AutoshardingLbPolicy::Picker::FallbackEndpointIndexIterator
//

class AutoshardingLbPolicy::Picker::FallbackEndpointIndexIterator final
    : public EndpointIndexIterator {
 public:
  explicit FallbackEndpointIndexIterator(size_t num_endpoints)
      : num_endpoints_(num_endpoints) {}

  size_t size() const override { return num_endpoints_; }
  size_t operator[](size_t index) const override { return index; }

 private:
  size_t num_endpoints_;
};

//
// AutoshardingLbPolicy::Picker
//

bool AutoshardingLbPolicy::Picker::IsPoolInFallback(
    absl::Span<const size_t> indices) const {
  if (indices.empty()) return true;
  for (size_t idx : indices) {
    if (endpoints_[idx].state != GRPC_CHANNEL_TRANSIENT_FAILURE) {
      return false;
    }
  }
  return true;
}

AutoshardingLbPolicy::PickResult AutoshardingLbPolicy::Picker::Pick(
    PickArgs args) {
  // Extract the sharding key from the request metadata.
  std::string buffer;
  absl::string_view key_header_name = config_->key_header_name();
  auto key = args.initial_metadata->Lookup(key_header_name, &buffer);
  // The key header cannot change without changing the application, so a
  // retry cannot succeed.  Drop the pick instead of queueing a wait_for_ready
  // RPC (gRFC A119).
  if (!key.has_value()) {
    return PickResult::Drop(absl::InternalError(absl::StrCat(
        "slice key header \"", key_header_name, "\" not present")));
  }
  // If we have no assignment, use all endpoints.  We get here only if
  // fallback is enabled; otherwise, the LB policy returns a picker that
  // fails all picks.
  if (slice_map_ == nullptr) {
    FallbackEndpointIndexIterator iterator(endpoints_.size());
    return PickFromEndpointIndices(iterator, args);
  }
  const SliceMap::Entry& slice = slice_map_->Lookup(*key);
  // Per gRFC A119, picks in a slice for which the server explicitly assigned
  // no endpoints fail, even when fallback is enabled.  A later assignment can
  // add endpoints to the slice, so fail the pick instead of dropping it.
  if (slice.fallback_disabled) {
    return PickResult::Fail(absl::UnavailableError(
        "no endpoints assigned to this slice and fallback is disabled"));
  }
  if (config_->enable_fallback() && IsPoolInFallback(slice.endpoints)) {
    FallbackEndpointIndexIterator iterator(endpoints_.size());
    return PickFromEndpointIndices(iterator, args);
  }
  SliceEndpointIndexIterator iterator(slice.endpoints);
  return PickFromEndpointIndices(iterator, args);
}

AutoshardingLbPolicy::PickResult
AutoshardingLbPolicy::Picker::PickFromEndpointIndices(
    const EndpointIndexIterator& indices, PickArgs args) {
  if (indices.size() == 0) {
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
          RequestConnectionForEndpoint(endpoint_info.endpoint);
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
  // picker to yield a detailed error message (or queue if the child policy
  // has not yet been created).
  const auto& endpoint_info = endpoints_[indices[first_index]];
  if (endpoint_info.picker == nullptr) {
    return PickResult::Queue();
  }
  return endpoint_info.picker->Pick(args);
}

void AutoshardingLbPolicy::Picker::RequestConnectionForEndpoint(
    const RefCountedPtr<AutoShardingEndpoint>& endpoint) {
  autosharding_lb_->work_serializer()->Run(
      [autosharding_lb = autosharding_lb_, endpoint]() {
        if (!autosharding_lb->shutdown_) {
          endpoint->RequestConnectionLocked();
        }
      });
}

//
// AutoshardingLbPolicy::AutoShardingEndpoint::Helper
//

class AutoshardingLbPolicy::AutoShardingEndpoint::Helper final
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
    return endpoint_->autosharding_lb_->channel_control_helper();
  }

  RefCountedPtr<AutoShardingEndpoint> endpoint_;
};

//
// AutoshardingLbPolicy::AutoShardingEndpoint
//

void AutoshardingLbPolicy::AutoShardingEndpoint::Orphan() {
  orphaned_ = true;
  if (child_policy_ != nullptr) {
    // Remove pollset_set linkage.
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     autosharding_lb_->interested_parties());
    child_policy_.reset();
    picker_.reset();
  }
  Unref();
}

absl::Status AutoshardingLbPolicy::AutoShardingEndpoint::UpdateLocked(
    size_t index) {
  index_ = index;
  if (child_policy_ == nullptr) return absl::OkStatus();
  return UpdateChildPolicyLocked();
}

void AutoshardingLbPolicy::AutoShardingEndpoint::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void AutoshardingLbPolicy::AutoShardingEndpoint::RequestConnectionLocked() {
  if (orphaned_) return;
  if (child_policy_ == nullptr) {
    CreateChildPolicy();
  } else {
    child_policy_->ExitIdleLocked();
  }
}

void AutoshardingLbPolicy::AutoShardingEndpoint::CreateChildPolicy() {
  GRPC_CHECK(child_policy_ == nullptr);
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = autosharding_lb_->work_serializer();
  lb_policy_args.args =
      autosharding_lb_->args_
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_ENABLE_HEALTH_CHECKING, true)
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_OMIT_STATUS_MESSAGE_PREFIX, true);
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  child_policy_ =
      CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
          "pick_first", std::move(lb_policy_args));
  if (GRPC_TRACE_FLAG_ENABLED(autosharding_lb)) {
    const EndpointAddresses& endpoint = autosharding_lb_->endpoints_[index_];
    LOG(INFO) << "[AS " << autosharding_lb_.get() << "] endpoint " << this
              << " (index " << index_ << " of "
              << autosharding_lb_->endpoints_.size() << ", "
              << endpoint.ToString() << "): created child policy "
              << child_policy_.get();
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy.
  grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                   autosharding_lb_->interested_parties());
  // If the child policy returns a non-OK status, request re-resolution.
  absl::Status status = UpdateChildPolicyLocked();
  if (!status.ok()) {
    autosharding_lb_->channel_control_helper()->RequestReresolution();
  }
}

absl::Status
AutoshardingLbPolicy::AutoShardingEndpoint::UpdateChildPolicyLocked() {
  // Construct pick_first config.
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray(
              {Json::FromObject({{"pick_first", Json::FromObject({})}})}));
  GRPC_CHECK(config.ok());
  // Update child policy.
  LoadBalancingPolicy::UpdateArgs update_args;
  update_args.addresses = std::make_shared<SingleEndpointIterator>(
      autosharding_lb_->endpoints_[index_]);
  update_args.args = autosharding_lb_->args_;
  update_args.config = std::move(*config);
  return child_policy_->UpdateLocked(std::move(update_args));
}

void AutoshardingLbPolicy::AutoShardingEndpoint::OnStateUpdate(
    grpc_connectivity_state new_state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  // Ignore lingering updates from the child policy of a removed endpoint
  // (gRFC A119).
  if (orphaned_) return;
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << autosharding_lb_.get()
      << "] connectivity changed for endpoint " << this << " ("
      << autosharding_lb_->endpoints_[index_].ToString()
      << ", child_policy=" << child_policy_.get()
      << "): prev_state=" << ConnectivityStateName(connectivity_state_)
      << " new_state=" << ConnectivityStateName(new_state) << " (" << status
      << ")";
  // Update state.
  connectivity_state_ = new_state;
  status_ = status;
  picker_ = std::move(picker);
  // Update the aggregated connectivity state.
  autosharding_lb_->UpdateAggregatedConnectivityStateLocked(status);
}

//
// AutoshardingLbPolicy::SliceMap
//

AutoshardingLbPolicy::SliceMap::SliceMap(
    RefCountedPtr<const AutoshardingAssignment> assignment,
    const std::map<std::string, OrphanablePtr<AutoShardingEndpoint>>&
        endpoint_map)
    : assignment_(std::move(assignment)) {
  // Build entries for each slice in the assignment.
  slices_.reserve(assignment_->slices.size());
  for (const auto& slice : assignment_->slices) {
    Entry entry;
    entry.end_key = slice.end_key;
    entry.endpoints.reserve(slice.endpoints.size());
    for (size_t idx : slice.endpoints) {
      GRPC_CHECK_LT(idx, assignment_->endpoint_names.size());
      auto it = endpoint_map.find(assignment_->endpoint_names[idx]);
      if (it != endpoint_map.end()) {
        entry.endpoints.push_back(it->second->index());
      }
    }
    entry.fallback_disabled = slice.fallback_disabled;
    slices_.push_back(std::move(entry));
  }
}

//
// AutoshardingLbPolicy
//

AutoshardingLbPolicy::AutoshardingLbPolicy(Args args)
    : LoadBalancingPolicy(std::move(args)), uuid_(GenerateUuid()) {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] Created with client UUID " << uuid_;
}

AutoshardingLbPolicy::~AutoshardingLbPolicy() {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] Destroying AutoshardingLbPolicy";
}

void AutoshardingLbPolicy::ShutdownLocked() {
  GRPC_TRACE_LOG(autosharding_lb, INFO) << "[AS " << this << "] Shutting down";
  shutdown_ = true;
  client_.reset();
  endpoint_map_.clear();
  slice_map_.reset();
  assignment_ = nullptr;
}

void AutoshardingLbPolicy::ExitIdleLocked() {
  // Per gRFC A119 (same as ring_hash in gRFC A117), a request to exit IDLE
  // triggers a connection attempt only if the policy reports IDLE.
  if (current_state_ == GRPC_CHANNEL_IDLE) {
    ConnectToRandomIdleEndpointLocked();
  }
}

void AutoshardingLbPolicy::ResetBackoffLocked() {
  if (client_ != nullptr) client_->ResetBackoff();
  for (const auto& [_, endpoint] : endpoint_map_) {
    endpoint->ResetBackoffLocked();
  }
}

// Extracts hostname from endpoint attribute (gRFC A81) or first address.
std::string ComputeHostname(const EndpointAddresses& endpoint) {
  auto hostname_arg = endpoint.args().GetString(GRPC_ARG_ADDRESS_NAME);
  if (hostname_arg.has_value()) return std::string(*hostname_arg);
  return grpc_sockaddr_to_string(&endpoint.addresses().front(), false)
      .value_or("<invalid address>");
}

absl::Status AutoshardingLbPolicy::UpdateLocked(UpdateArgs args) {
  // Save channel args.
  args_ = std::move(args.args);
  auto new_config = args.config.TakeAsSubclass<AutoShardingLbConfig>();
  // Update resolution note.
  resolution_note_ = std::move(args.resolution_note);
  resolver_error_ = absl::OkStatus();
  // Update endpoint list.
  absl::Status status;
  if (!args.addresses.ok()) {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this << "] received update with addresses error: "
        << args.addresses.status();
    status = args.addresses.status();
    // If we already have an endpoint list, then we keep using it.
  } else {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this << "] received update";
    endpoints_.clear();
    std::map<std::string, OrphanablePtr<AutoShardingEndpoint>> endpoint_map;
    std::vector<std::string> errors;
    (*args.addresses)->ForEach([&](const EndpointAddresses& endpoint) {
      const std::string hostname = ComputeHostname(endpoint);
      auto& autosharding_endpoint = endpoint_map[hostname];
      // If we've already seen this hostname, skip the dup.
      if (autosharding_endpoint != nullptr) {
        GRPC_TRACE_LOG(autosharding_lb, INFO)
            << "[AS " << this << "] ignoring duplicate endpoint for \""
            << hostname << "\"";
        return;
      }
      // Have not yet seen this hostname, so add a new endpoint.
      const size_t index = endpoints_.size();
      endpoints_.push_back(endpoint);
      // If present in old map, retain it; otherwise, create a new one.
      auto it = endpoint_map_.find(hostname);
      if (it != endpoint_map_.end()) {
        absl::Status status = it->second->UpdateLocked(index);
        if (!status.ok()) {
          errors.emplace_back(
              absl::StrCat("endpoint ", hostname, ": ", status.ToString()));
        }
        autosharding_endpoint = std::move(it->second);
      } else {
        autosharding_endpoint = MakeOrphanable<AutoShardingEndpoint>(
            RefAsSubclass<AutoshardingLbPolicy>(), index);
      }
    });
    endpoint_map_ = std::move(endpoint_map);
    if (!errors.empty()) {
      status = absl::UnavailableError(absl::StrCat(
          "errors from children: [", absl::StrJoin(errors, "; "), "]"));
    }
  }
  // Rebuild the slice map from the retained assignment.
  UpdateSliceMapLocked();
  // If the address list is empty, report TRANSIENT_FAILURE.  Keep client_ and
  // assignment_, so that the next good update uses them at once (gRFC A119).
  if (endpoints_.empty()) {
    if (status.ok()) {
      status = UnavailableErrorWithNote("empty address list", resolution_note_);
    }
    resolver_error_ = status;
    UpdateAggregatedConnectivityStateLocked(absl::OkStatus());
    return status;
  }
  // Locality attribute changes can alter effective_target_ without changing
  // config_, which also requires recreating client_ (gRFC A119).
  std::string new_effective_target = EffectiveTarget(
      new_config->autosharding_target(),
      args_.GetString(GRPC_ARG_LB_WEIGHTED_TARGET_CHILD).value_or(""));
  const bool create_client =
      client_ == nullptr ||
      new_config->channel_factory_key() != config_->channel_factory_key() ||
      new_effective_target != effective_target_;
  // If we need a new client but there is no channel factory, close client_
  // and fail with a bad resolver state, regardless of enable_fallback.  Keep
  // assignment_ for the next good update (gRFC A119).
  experimental::ChannelFactory* channel_factory = nullptr;
  if (create_client) {
    channel_factory = args_.GetObject<experimental::ChannelFactory>();
    if (channel_factory == nullptr) {
      client_.reset();
      resolver_error_ = UnavailableErrorWithNote(
          "no channel factory in channel args", resolution_note_);
      UpdateAggregatedConnectivityStateLocked(absl::OkStatus());
      return resolver_error_;
    }
  }
  config_ = std::move(new_config);
  if (create_client) {
    effective_target_ = std::move(new_effective_target);
    CreateAutoshardingClientLocked(*channel_factory);
  }
  // Return a new picker.
  UpdateAggregatedConnectivityStateLocked(absl::OkStatus());
  return status;
}

void AutoshardingLbPolicy::CreateAutoshardingClientLocked(
    experimental::ChannelFactory& channel_factory) {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] creating autosharding client for target \""
      << effective_target_ << "\" using channel factory key \""
      << config_->channel_factory_key() << "\"";
  // Create the new client before orphaning the old one, so that the channel
  // factory can return a new ref to the same channel.  The client holds refs
  // to EventEngine (for timers outliving ShutdownLocked()) and to this policy
  // (broken when ShutdownLocked() resets client_).
  client_ = MakeOrphanable<AutoshardingClient>(
      CreateTransportFromChannelFactory(channel_factory,
                                        config_->channel_factory_key()),
      effective_target_, uuid_, config_->initial_assignment_timeout(),
      channel_control_helper()->GetEventEngine()->shared_from_this(),
      [self = RefAsSubclass<AutoshardingLbPolicy>(DEBUG_LOCATION,
                                                  "AutoshardingClient")](
          RefCountedPtr<AutoshardingClient> client,
          absl::StatusOr<RefCountedPtr<const AutoshardingAssignment>> result) {
        // The client invokes this while holding its lock, so hop into the
        // WorkSerializer.
        self->work_serializer()->Run(
            [self, client = std::move(client),
             result = std::move(result)]() mutable {
              self->OnAssignmentReceivedLocked(std::move(client),
                                               std::move(result));
            },
            DEBUG_LOCATION);
      });
}

void AutoshardingLbPolicy::OnAssignmentReceivedLocked(
    RefCountedPtr<AutoshardingClient> client,
    absl::StatusOr<RefCountedPtr<const AutoshardingAssignment>> result) {
  if (shutdown_ || client.get() != client_.get()) return;
  assignment_ = std::move(result);
  UpdateSliceMapLocked();
  UpdateAggregatedConnectivityStateLocked(absl::OkStatus());
}

void AutoshardingLbPolicy::UpdateSliceMapLocked() {
  slice_map_ = assignment_.ok() && *assignment_ != nullptr
                   ? MakeRefCounted<SliceMap>(*assignment_, endpoint_map_)
                   : nullptr;
}

void AutoshardingLbPolicy::UpdateAggregatedConnectivityStateLocked(
    absl::Status status) {
  // A bad resolver state fails all picks (gRFC A119).
  if (!resolver_error_.ok()) {
    UpdateStateLocked(GRPC_CHANNEL_TRANSIENT_FAILURE, resolver_error_,
                      MakeRefCounted<TransientFailurePicker>(resolver_error_));
    return;
  }
  // Nothing to report until UpdateLocked() has set endpoints_ and config_.
  if (endpoints_.empty() || config_ == nullptr) return;
  // Until the client reports its first result, report CONNECTING and queue
  // all picks.  CONNECTING (not IDLE) lets a priority parent fail over if
  // the result does not arrive in time (gRFC A119).
  // TODO(bpawan): Set delay_type to "autosharding_assignment_pending" per
  // gRFC A121 once it is supported in C++.
  if (assignment_.ok() && *assignment_ == nullptr) {
    UpdateStateLocked(GRPC_CHANNEL_CONNECTING, absl::OkStatus(),
                      MakeRefCounted<QueuePicker>(nullptr));
    return;
  }
  // If there is no assignment and fallback is disabled, we cannot route any
  // pick, so report TRANSIENT_FAILURE.
  if (!assignment_.ok() && !config_->enable_fallback()) {
    absl::Status pick_status = UnavailableErrorWithNote(
        assignment_.status().message(), resolution_note_);
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this << "] no assignment and fallback disabled, failing "
        << "picks: " << pick_status;
    UpdateStateLocked(GRPC_CHANNEL_TRANSIENT_FAILURE, pick_status,
                      MakeRefCounted<TransientFailurePicker>(pick_status));
    return;
  }
  // Count the number of endpoints in each state.
  size_t num_idle = 0;
  size_t num_connecting = 0;
  size_t num_ready = 0;
  size_t num_transient_failure = 0;
  for (const auto& [_, endpoint] : endpoint_map_) {
    switch (endpoint->connectivity_state()) {
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
  UpdateStateLocked(
      state, status,
      MakeRefCounted<Picker>(RefAsSubclass<AutoshardingLbPolicy>(
                                 DEBUG_LOCATION, "AutoShardingPicker"),
                             slice_map_));
  // If in TRANSIENT_FAILURE or CONNECTING, trigger a connection attempt on
  // an IDLE endpoint to prevent premature failover when used as a child of
  // the priority policy (same as ring_hash).
  if (state == GRPC_CHANNEL_CONNECTING ||
      state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ConnectToRandomIdleEndpointLocked();
  }
}

void AutoshardingLbPolicy::UpdateStateLocked(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  current_state_ = state;
  channel_control_helper()->UpdateState(state, status, std::move(picker));
}

void AutoshardingLbPolicy::ConnectToRandomIdleEndpointLocked() {
  std::vector<AutoShardingEndpoint*> idle_endpoints;
  for (const auto& [_, endpoint] : endpoint_map_) {
    switch (endpoint->connectivity_state()) {
      case GRPC_CHANNEL_CONNECTING:
        return;
      case GRPC_CHANNEL_IDLE:
        idle_endpoints.push_back(endpoint.get());
        break;
      default:
        break;
    }
  }
  if (idle_endpoints.empty()) return;
  // Choose the endpoint at random, so that many clients that connect at the
  // same time spread their load across the endpoints (gRFC A117).
  AutoShardingEndpoint* endpoint = idle_endpoints[absl::Uniform<size_t>(
      SharedBitGen(), 0, idle_endpoints.size())];
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] triggering internal connection attempt for "
      << "endpoint " << endpoint << " ("
      << endpoints_[endpoint->index()].ToString() << ") (index "
      << endpoint->index() << " of " << endpoints_.size() << ")";
  endpoint->RequestConnectionLocked();
}

//
// factory
//

class AutoShardingFactory final : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<AutoshardingLbPolicy>(std::move(args));
  }

  absl::string_view name() const override { return kAutoSharding; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadFromJson<RefCountedPtr<AutoShardingLbConfig>>(
        json, JsonArgs(), "errors validating autosharding LB policy config");
  }
};

}  // namespace

//
// AutoShardingLbConfig
//

absl::string_view AutoShardingLbConfig::name() const { return kAutoSharding; }

const JsonLoaderInterface* AutoShardingLbConfig::JsonLoader(const JsonArgs&) {
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

void AutoShardingLbConfig::JsonPostLoad(const Json&, const JsonArgs&,
                                        ValidationErrors* errors) {
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
    if (!errors->FieldHasErrors()) {
      if (key_header_name_.empty()) {
        errors->AddError("must be non-empty");
      } else {
        ValidateMetadataResult result =
            ValidateHeaderKeyIsLegal(key_header_name_);
        if (result != ValidateMetadataResult::kOk) {
          errors->AddError(ValidateMetadataResultToString(result));
        }
      }
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

void RegisterAutoShardingLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<AutoShardingFactory>());
}

}  // namespace grpc_core
