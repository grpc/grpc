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

#include <inttypes.h>
#include <limits.h>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/address_filtering.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy/ring_hash/ring_hash.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/ext/xds/xds_endpoint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/uri/uri_parser.h"

#define GRPC_EDS_DEFAULT_FALLBACK_TIMEOUT 10000

namespace grpc_core {

TraceFlag grpc_lb_xds_cluster_resolver_trace(false, "xds_cluster_resolver_lb");

const char* kXdsLocalityNameAttributeKey = "xds_locality_name";

namespace {

constexpr char kXdsClusterResolver[] = "xds_cluster_resolver_experimental";

// Config for EDS LB policy.
class XdsClusterResolverLbConfig : public LoadBalancingPolicy::Config {
 public:
  struct DiscoveryMechanism {
    std::string cluster_name;
    absl::optional<std::string> lrs_load_reporting_server_name;
    uint32_t max_concurrent_requests;
    enum DiscoveryMechanismType {
      EDS,
      LOGICAL_DNS,
    };
    DiscoveryMechanismType type;
    std::string eds_service_name;
    std::string dns_hostname;

    bool operator==(const DiscoveryMechanism& other) const {
      return (cluster_name == other.cluster_name &&
              lrs_load_reporting_server_name ==
                  other.lrs_load_reporting_server_name &&
              max_concurrent_requests == other.max_concurrent_requests &&
              type == other.type &&
              eds_service_name == other.eds_service_name &&
              dns_hostname == other.dns_hostname);
    }
  };

  XdsClusterResolverLbConfig(
      std::vector<DiscoveryMechanism> discovery_mechanisms, Json xds_lb_policy)
      : discovery_mechanisms_(std::move(discovery_mechanisms)),
        xds_lb_policy_(std::move(xds_lb_policy)) {}

  const char* name() const override { return kXdsClusterResolver; }
  const std::vector<DiscoveryMechanism>& discovery_mechanisms() const {
    return discovery_mechanisms_;
  }

  const Json& xds_lb_policy() const { return xds_lb_policy_; }

 private:
  std::vector<DiscoveryMechanism> discovery_mechanisms_;
  Json xds_lb_policy_;
};

// Xds Cluster Resolver LB policy.
class XdsClusterResolverLb : public LoadBalancingPolicy {
 public:
  XdsClusterResolverLb(RefCountedPtr<XdsClient> xds_client, Args args);

  const char* name() const override { return kXdsClusterResolver; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;
  void ExitIdleLocked() override;

 private:
  // Discovery Mechanism Base class
  //
  // Implemented by EDS and LOGICAL_DNS.
  //
  // Implementations are responsible for calling the LB policy's
  // OnEndpointChanged(), OnError(), and OnResourceDoesNotExist()
  // methods when the corresponding events occur.
  //
  // Must implement Orphan() method to cancel the watchers.
  class DiscoveryMechanism : public InternallyRefCounted<DiscoveryMechanism> {
   public:
    DiscoveryMechanism(
        RefCountedPtr<XdsClusterResolverLb> xds_cluster_resolver_lb,
        size_t index)
        : parent_(std::move(xds_cluster_resolver_lb)), index_(index) {}
    virtual void Start() = 0;
    void Orphan() override = 0;
    virtual Json::Array override_child_policy() = 0;
    virtual bool disable_reresolution() = 0;

    // Returns a pair containing the cluster and eds_service_name
    // to use for LRS load reporting. Caller must ensure that config_ is set
    // before calling.
    std::pair<absl::string_view, absl::string_view> GetLrsClusterKey() const {
      return {
          parent_->config_->discovery_mechanisms()[index_].cluster_name,
          parent_->config_->discovery_mechanisms()[index_].eds_service_name};
    }

   protected:
    XdsClusterResolverLb* parent() const { return parent_.get(); }
    size_t index() const { return index_; }

   private:
    RefCountedPtr<XdsClusterResolverLb> parent_;
    // Stores its own index in the vector of DiscoveryMechanism.
    size_t index_;
  };

  class EdsDiscoveryMechanism : public DiscoveryMechanism {
   public:
    EdsDiscoveryMechanism(
        RefCountedPtr<XdsClusterResolverLb> xds_cluster_resolver_lb,
        size_t index)
        : DiscoveryMechanism(std::move(xds_cluster_resolver_lb), index) {}
    void Start() override;
    void Orphan() override;
    Json::Array override_child_policy() override { return Json::Array{}; }
    bool disable_reresolution() override { return true; }

   private:
    class EndpointWatcher : public XdsEndpointResourceType::WatcherInterface {
     public:
      explicit EndpointWatcher(
          RefCountedPtr<EdsDiscoveryMechanism> discovery_mechanism)
          : discovery_mechanism_(std::move(discovery_mechanism)) {}
      ~EndpointWatcher() override {
        discovery_mechanism_.reset(DEBUG_LOCATION, "EndpointWatcher");
      }
      void OnResourceChanged(XdsEndpointResource update) override {
        Ref().release();  // ref held by callback
        discovery_mechanism_->parent()->work_serializer()->Run(
            // TODO(yashykt): When we move to C++14, capture update with
            // std::move
            [this, update]() mutable {
              OnResourceChangedHelper(std::move(update));
              Unref();
            },
            DEBUG_LOCATION);
      }
      void OnError(grpc_error_handle error) override {
        Ref().release();  // ref held by callback
        discovery_mechanism_->parent()->work_serializer()->Run(
            [this, error]() {
              OnErrorHelper(error);
              Unref();
            },
            DEBUG_LOCATION);
      }
      void OnResourceDoesNotExist() override {
        Ref().release();  // ref held by callback
        discovery_mechanism_->parent()->work_serializer()->Run(
            [this]() {
              OnResourceDoesNotExistHelper();
              Unref();
            },
            DEBUG_LOCATION);
      }

     private:
      // Code accessing protected methods of `DiscoveryMechanism` need to be
      // in methods of this class rather than in lambdas to work around an MSVC
      // bug.
      void OnResourceChangedHelper(XdsEndpointResource update) {
        discovery_mechanism_->parent()->OnEndpointChanged(
            discovery_mechanism_->index(), std::move(update));
      }
      void OnErrorHelper(grpc_error_handle error) {
        discovery_mechanism_->parent()->OnError(discovery_mechanism_->index(),
                                                error);
      }
      void OnResourceDoesNotExistHelper() {
        discovery_mechanism_->parent()->OnResourceDoesNotExist(
            discovery_mechanism_->index());
      }
      RefCountedPtr<EdsDiscoveryMechanism> discovery_mechanism_;
    };

    // This is necessary only because of a bug in msvc where nested class
    // cannot access protected member in base class.
    friend class EndpointWatcher;

    absl::string_view GetEdsResourceName() const {
      if (!parent()
               ->config_->discovery_mechanisms()[index()]
               .eds_service_name.empty()) {
        return parent()
            ->config_->discovery_mechanisms()[index()]
            .eds_service_name;
      }
      return parent()->config_->discovery_mechanisms()[index()].cluster_name;
    }

    // Note that this is not owned, so this pointer must never be dereferenced.
    EndpointWatcher* watcher_ = nullptr;
  };

  class LogicalDNSDiscoveryMechanism : public DiscoveryMechanism {
   public:
    LogicalDNSDiscoveryMechanism(
        RefCountedPtr<XdsClusterResolverLb> xds_cluster_resolver_lb,
        size_t index)
        : DiscoveryMechanism(std::move(xds_cluster_resolver_lb), index) {}
    void Start() override;
    void Orphan() override;
    Json::Array override_child_policy() override {
      return Json::Array{
          Json::Object{
              {"pick_first", Json::Object()},
          },
      };
    }
    bool disable_reresolution() override { return false; };

   private:
    class ResolverResultHandler : public Resolver::ResultHandler {
     public:
      explicit ResolverResultHandler(
          RefCountedPtr<LogicalDNSDiscoveryMechanism> discovery_mechanism)
          : discovery_mechanism_(std::move(discovery_mechanism)) {}

      ~ResolverResultHandler() override {}

      void ReportResult(Resolver::Result result) override;

     private:
      RefCountedPtr<LogicalDNSDiscoveryMechanism> discovery_mechanism_;
    };

    // This is necessary only because of a bug in msvc where nested class cannot
    // access protected member in base class.
    friend class ResolverResultHandler;

    OrphanablePtr<Resolver> resolver_;
  };

  struct DiscoveryMechanismEntry {
    OrphanablePtr<DiscoveryMechanism> discovery_mechanism;
    bool first_update_received = false;
    // Number of priorities this mechanism has contributed to priority_list_.
    // (The sum of this across all discovery mechanisms should always equal
    // the number of priorities in priority_list_.)
    uint32_t num_priorities = 0;
    RefCountedPtr<XdsEndpointResource::DropConfig> drop_config;
    // Populated only when an update has been delivered by the mechanism
    // but has not yet been applied to the LB policy's combined priority_list_.
    absl::optional<XdsEndpointResource::PriorityList> pending_priority_list;
  };

  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(
        RefCountedPtr<XdsClusterResolverLb> xds_cluster_resolver_policy)
        : xds_cluster_resolver_policy_(std::move(xds_cluster_resolver_policy)) {
    }

    ~Helper() override {
      xds_cluster_resolver_policy_.reset(DEBUG_LOCATION, "Helper");
    }

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     std::unique_ptr<SubchannelPicker> picker) override;
    // This is a no-op, because we get the addresses from the xds
    // client, which is a watch-based API.
    void RequestReresolution() override {}
    absl::string_view GetAuthority() override;
    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override;

   private:
    RefCountedPtr<XdsClusterResolverLb> xds_cluster_resolver_policy_;
  };

  ~XdsClusterResolverLb() override;

  void ShutdownLocked() override;

  void OnEndpointChanged(size_t index, XdsEndpointResource update);
  void OnError(size_t index, grpc_error_handle error);
  void OnResourceDoesNotExist(size_t index);

  void MaybeDestroyChildPolicyLocked();

  void UpdatePriorityList(XdsEndpointResource::PriorityList priority_list);
  void UpdateChildPolicyLocked();
  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const grpc_channel_args* args);
  ServerAddressList CreateChildPolicyAddressesLocked();
  RefCountedPtr<Config> CreateChildPolicyConfigLocked();
  grpc_channel_args* CreateChildPolicyArgsLocked(
      const grpc_channel_args* args_in);

  // The xds client and endpoint watcher.
  RefCountedPtr<XdsClient> xds_client_;

  // Current channel args and config from the resolver.
  const grpc_channel_args* args_ = nullptr;
  RefCountedPtr<XdsClusterResolverLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // Vector of discovery mechansism entries in priority order.
  std::vector<DiscoveryMechanismEntry> discovery_mechanisms_;

  // The latest data from the endpoint watcher.
  XdsEndpointResource::PriorityList priority_list_;
  // State used to retain child policy names for priority policy.
  std::vector<size_t /*child_number*/> priority_child_numbers_;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;
};

//
// XdsClusterResolverLb::Helper
//

RefCountedPtr<SubchannelInterface>
XdsClusterResolverLb::Helper::CreateSubchannel(ServerAddress address,
                                               const grpc_channel_args& args) {
  if (xds_cluster_resolver_policy_->shutting_down_) return nullptr;
  return xds_cluster_resolver_policy_->channel_control_helper()
      ->CreateSubchannel(std::move(address), args);
}

void XdsClusterResolverLb::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    std::unique_ptr<SubchannelPicker> picker) {
  if (xds_cluster_resolver_policy_->shutting_down_ ||
      xds_cluster_resolver_policy_->child_policy_ == nullptr) {
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_resolver_lb %p] child policy updated state=%s (%s) "
            "picker=%p",
            xds_cluster_resolver_policy_.get(), ConnectivityStateName(state),
            status.ToString().c_str(), picker.get());
  }
  xds_cluster_resolver_policy_->channel_control_helper()->UpdateState(
      state, status, std::move(picker));
}

absl::string_view XdsClusterResolverLb::Helper::GetAuthority() {
  return xds_cluster_resolver_policy_->channel_control_helper()->GetAuthority();
}

void XdsClusterResolverLb::Helper::AddTraceEvent(TraceSeverity severity,
                                                 absl::string_view message) {
  if (xds_cluster_resolver_policy_->shutting_down_) return;
  xds_cluster_resolver_policy_->channel_control_helper()->AddTraceEvent(
      severity, message);
}

//
// XdsClusterResolverLb::EdsDiscoveryMechanism
//

void XdsClusterResolverLb::EdsDiscoveryMechanism::Start() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_resolver_lb %p] eds discovery mechanism %" PRIuPTR
            ":%p starting xds watch for %s",
            parent(), index(), this, std::string(GetEdsResourceName()).c_str());
  }
  auto watcher = MakeRefCounted<EndpointWatcher>(
      Ref(DEBUG_LOCATION, "EdsDiscoveryMechanism"));
  watcher_ = watcher.get();
  XdsEndpointResourceType::StartWatch(parent()->xds_client_.get(),
                                      GetEdsResourceName(), std::move(watcher));
}

void XdsClusterResolverLb::EdsDiscoveryMechanism::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_resolver_lb %p] eds discovery mechanism %" PRIuPTR
            ":%p cancelling xds watch for %s",
            parent(), index(), this, std::string(GetEdsResourceName()).c_str());
  }
  XdsEndpointResourceType::CancelWatch(parent()->xds_client_.get(),
                                       GetEdsResourceName(), watcher_);
  Unref();
}

//
// XdsClusterResolverLb::LogicalDNSDiscoveryMechanism
//

void XdsClusterResolverLb::LogicalDNSDiscoveryMechanism::Start() {
  std::string target =
      parent()->config_->discovery_mechanisms()[index()].dns_hostname;
  grpc_channel_args* args = nullptr;
  FakeResolverResponseGenerator* fake_resolver_response_generator =
      grpc_channel_args_find_pointer<FakeResolverResponseGenerator>(
          parent()->args_,
          GRPC_ARG_XDS_LOGICAL_DNS_CLUSTER_FAKE_RESOLVER_RESPONSE_GENERATOR);
  if (fake_resolver_response_generator != nullptr) {
    target = absl::StrCat("fake:", target);
    grpc_arg new_arg = FakeResolverResponseGenerator::MakeChannelArg(
        fake_resolver_response_generator);
    args = grpc_channel_args_copy_and_add(parent()->args_, &new_arg, 1);
  } else {
    target = absl::StrCat("dns:", target);
    args = grpc_channel_args_copy(parent()->args_);
  }
  resolver_ = ResolverRegistry::CreateResolver(
      target.c_str(), args, parent()->interested_parties(),
      parent()->work_serializer(),
      absl::make_unique<ResolverResultHandler>(
          Ref(DEBUG_LOCATION, "LogicalDNSDiscoveryMechanism")));
  grpc_channel_args_destroy(args);
  if (resolver_ == nullptr) {
    parent()->OnResourceDoesNotExist(index());
    return;
  }
  resolver_->StartLocked();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_resolver_lb %p] logical DNS discovery mechanism "
            "%" PRIuPTR ":%p starting dns resolver %p",
            parent(), index(), this, resolver_.get());
  }
}

void XdsClusterResolverLb::LogicalDNSDiscoveryMechanism::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(
        GPR_INFO,
        "[xds_cluster_resolver_lb %p] logical DNS discovery mechanism %" PRIuPTR
        ":%p shutting down dns resolver %p",
        parent(), index(), this, resolver_.get());
  }
  resolver_.reset();
  Unref();
}

//
// XdsClusterResolverLb::LogicalDNSDiscoveryMechanism::ResolverResultHandler
//

void XdsClusterResolverLb::LogicalDNSDiscoveryMechanism::ResolverResultHandler::
    ReportResult(Resolver::Result result) {
  if (!result.addresses.ok()) {
    discovery_mechanism_->parent()->OnError(
        discovery_mechanism_->index(),
        absl_status_to_grpc_error(result.addresses.status()));
    return;
  }
  // Convert resolver result to EDS update.
  // TODO(roth): Figure out a way to pass resolution_note through to the
  // child policy.
  XdsEndpointResource update;
  XdsEndpointResource::Priority::Locality locality;
  locality.name = MakeRefCounted<XdsLocalityName>("", "", "");
  locality.lb_weight = 1;
  locality.endpoints = std::move(*result.addresses);
  XdsEndpointResource::Priority priority;
  priority.localities.emplace(locality.name.get(), std::move(locality));
  update.priorities.emplace_back(std::move(priority));
  discovery_mechanism_->parent()->OnEndpointChanged(
      discovery_mechanism_->index(), std::move(update));
}

//
// XdsClusterResolverLb public methods
//

XdsClusterResolverLb::XdsClusterResolverLb(RefCountedPtr<XdsClient> xds_client,
                                           Args args)
    : LoadBalancingPolicy(std::move(args)), xds_client_(std::move(xds_client)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_resolver_lb %p] created -- xds_client=%p",
            this, xds_client_.get());
  }
}

XdsClusterResolverLb::~XdsClusterResolverLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_resolver_lb %p] destroying xds_cluster_resolver LB "
            "policy",
            this);
  }
}

void XdsClusterResolverLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_resolver_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  MaybeDestroyChildPolicyLocked();
  discovery_mechanisms_.clear();
  xds_client_.reset(DEBUG_LOCATION, "XdsClusterResolverLb");
  // Destroy channel args.
  grpc_channel_args_destroy(args_);
  args_ = nullptr;
}

void XdsClusterResolverLb::MaybeDestroyChildPolicyLocked() {
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
}

void XdsClusterResolverLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_resolver_lb %p] Received update", this);
  }
  const bool is_initial_update = args_ == nullptr;
  // Update config.
  auto old_config = std::move(config_);
  config_ = std::move(args.config);
  // Update args.
  grpc_channel_args_destroy(args_);
  args_ = args.args;
  args.args = nullptr;
  // Update child policy if needed.
  if (child_policy_ != nullptr) UpdateChildPolicyLocked();
  // Create endpoint watcher if needed.
  if (is_initial_update) {
    for (const auto& config : config_->discovery_mechanisms()) {
      DiscoveryMechanismEntry entry;
      if (config.type == XdsClusterResolverLbConfig::DiscoveryMechanism::
                             DiscoveryMechanismType::EDS) {
        entry.discovery_mechanism = MakeOrphanable<EdsDiscoveryMechanism>(
            Ref(DEBUG_LOCATION, "EdsDiscoveryMechanism"),
            discovery_mechanisms_.size());
      } else if (config.type == XdsClusterResolverLbConfig::DiscoveryMechanism::
                                    DiscoveryMechanismType::LOGICAL_DNS) {
        entry.discovery_mechanism =
            MakeOrphanable<LogicalDNSDiscoveryMechanism>(
                Ref(DEBUG_LOCATION, "LogicalDNSDiscoveryMechanism"),
                discovery_mechanisms_.size());
      } else {
        GPR_ASSERT(0);
      }
      discovery_mechanisms_.push_back(std::move(entry));
    }
    // Call start() on all discovery mechanisms after creation.
    for (const auto& discovery_mechanism : discovery_mechanisms_) {
      discovery_mechanism.discovery_mechanism->Start();
    }
  }
}

void XdsClusterResolverLb::ResetBackoffLocked() {
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
  }
}

void XdsClusterResolverLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

void XdsClusterResolverLb::OnEndpointChanged(size_t index,
                                             XdsEndpointResource update) {
  if (shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_resolver_lb %p] Received update from xds client"
            " for discovery mechanism %" PRIuPTR "",
            this, index);
  }
  // We need at least one priority for each discovery mechanism, just so that we
  // have a child in which to create the xds_cluster_impl policy.  This ensures
  // that we properly handle the case of a discovery mechanism dropping 100% of
  // calls, the OnError() case, and the OnResourceDoesNotExist() case.
  if (update.priorities.empty()) update.priorities.emplace_back();
  discovery_mechanisms_[index].drop_config = std::move(update.drop_config);
  discovery_mechanisms_[index].pending_priority_list =
      std::move(update.priorities);
  discovery_mechanisms_[index].first_update_received = true;
  // If any discovery mechanism has not received its first update,
  // wait until that happens before creating the child policy.
  // TODO(roth): If this becomes problematic in the future (e.g., a
  // secondary discovery mechanism delaying us from starting up at all),
  // we can consider some sort of optimization whereby we can create the
  // priority policy with only a subset of its children.  But we need to
  // make sure not to get into a situation where the priority policy
  // will put the channel into TRANSIENT_FAILURE instead of CONNECTING
  // while we're still waiting for the other discovery mechanism(s).
  for (DiscoveryMechanismEntry& mechanism : discovery_mechanisms_) {
    if (!mechanism.first_update_received) return;
  }
  // Construct new priority list.
  XdsEndpointResource::PriorityList priority_list;
  size_t priority_index = 0;
  for (DiscoveryMechanismEntry& mechanism : discovery_mechanisms_) {
    // If the mechanism has a pending update, use that.
    // Otherwise, use the priorities that it previously contributed to the
    // combined list.
    if (mechanism.pending_priority_list.has_value()) {
      priority_list.insert(priority_list.end(),
                           mechanism.pending_priority_list->begin(),
                           mechanism.pending_priority_list->end());
      priority_index += mechanism.num_priorities;
      mechanism.num_priorities = mechanism.pending_priority_list->size();
      mechanism.pending_priority_list.reset();
    } else {
      priority_list.insert(
          priority_list.end(), priority_list_.begin() + priority_index,
          priority_list_.begin() + priority_index + mechanism.num_priorities);
      priority_index += mechanism.num_priorities;
    }
  }
  // Update child policy.
  UpdatePriorityList(std::move(priority_list));
}

void XdsClusterResolverLb::OnError(size_t index, grpc_error_handle error) {
  gpr_log(GPR_ERROR,
          "[xds_cluster_resolver_lb %p] discovery mechanism %" PRIuPTR
          " xds watcher reported error: %s",
          this, index, grpc_error_std_string(error).c_str());
  GRPC_ERROR_UNREF(error);
  if (shutting_down_) return;
  if (!discovery_mechanisms_[index].first_update_received) {
    // Call OnEndpointChanged with an empty update just like
    // OnResourceDoesNotExist.
    OnEndpointChanged(index, XdsEndpointResource());
  }
}

void XdsClusterResolverLb::OnResourceDoesNotExist(size_t index) {
  gpr_log(GPR_ERROR,
          "[xds_cluster_resolver_lb %p] discovery mechanism %" PRIuPTR
          " resource does not exist",
          this, index);
  if (shutting_down_) return;
  // Call OnEndpointChanged with an empty update.
  OnEndpointChanged(index, XdsEndpointResource());
}

//
// child policy-related methods
//

void XdsClusterResolverLb::UpdatePriorityList(
    XdsEndpointResource::PriorityList priority_list) {
  // Build some maps from locality to child number and the reverse from
  // the old data in priority_list_ and priority_child_numbers_.
  std::map<XdsLocalityName*, size_t /*child_number*/, XdsLocalityName::Less>
      locality_child_map;
  std::map<size_t, std::set<XdsLocalityName*>> child_locality_map;
  for (size_t priority = 0; priority < priority_list_.size(); ++priority) {
    size_t child_number = priority_child_numbers_[priority];
    const auto& localities = priority_list_[priority].localities;
    for (const auto& p : localities) {
      XdsLocalityName* locality_name = p.first;
      locality_child_map[locality_name] = child_number;
      child_locality_map[child_number].insert(locality_name);
    }
  }
  // Construct new list of children.
  std::vector<size_t> priority_child_numbers;
  for (size_t priority = 0; priority < priority_list.size(); ++priority) {
    const auto& localities = priority_list[priority].localities;
    absl::optional<size_t> child_number;
    // If one of the localities in this priority already existed, reuse its
    // child number.
    for (const auto& p : localities) {
      XdsLocalityName* locality_name = p.first;
      if (!child_number.has_value()) {
        auto it = locality_child_map.find(locality_name);
        if (it != locality_child_map.end()) {
          child_number = it->second;
          locality_child_map.erase(it);
          // Remove localities that *used* to be in this child number, so
          // that we don't incorrectly reuse this child number for a
          // subsequent priority.
          for (XdsLocalityName* old_locality :
               child_locality_map[*child_number]) {
            locality_child_map.erase(old_locality);
          }
        }
      } else {
        // Remove all localities that are now in this child number, so
        // that we don't accidentally reuse this child number for a
        // subsequent priority.
        locality_child_map.erase(locality_name);
      }
    }
    // If we didn't find an existing child number, assign a new one.
    if (!child_number.has_value()) {
      for (child_number = 0;
           child_locality_map.find(*child_number) != child_locality_map.end();
           ++(*child_number)) {
      }
      // Add entry so we know that the child number is in use.
      // (Don't need to add the list of localities, since we won't use them.)
      child_locality_map[*child_number];
    }
    priority_child_numbers.push_back(*child_number);
  }
  // Save update.
  priority_list_ = std::move(priority_list);
  priority_child_numbers_ = std::move(priority_child_numbers);
  // Update child policy.
  UpdateChildPolicyLocked();
}

ServerAddressList XdsClusterResolverLb::CreateChildPolicyAddressesLocked() {
  ServerAddressList addresses;
  for (size_t priority = 0; priority < priority_list_.size(); ++priority) {
    const auto& localities = priority_list_[priority].localities;
    std::string priority_child_name =
        absl::StrCat("child", priority_child_numbers_[priority]);
    for (const auto& p : localities) {
      const auto& locality_name = p.first;
      const auto& locality = p.second;
      std::vector<std::string> hierarchical_path = {
          priority_child_name, locality_name->AsHumanReadableString()};
      for (const auto& endpoint : locality.endpoints) {
        const ServerAddressWeightAttribute* weight_attribute = static_cast<
            const ServerAddressWeightAttribute*>(endpoint.GetAttribute(
            ServerAddressWeightAttribute::kServerAddressWeightAttributeKey));
        uint32_t weight = locality.lb_weight;
        if (weight_attribute != nullptr) {
          weight = locality.lb_weight * weight_attribute->weight();
        }
        addresses.emplace_back(
            endpoint
                .WithAttribute(kHierarchicalPathAttributeKey,
                               MakeHierarchicalPathAttribute(hierarchical_path))
                .WithAttribute(kXdsLocalityNameAttributeKey,
                               absl::make_unique<XdsLocalityAttribute>(
                                   locality_name->Ref()))
                .WithAttribute(
                    ServerAddressWeightAttribute::
                        kServerAddressWeightAttributeKey,
                    absl::make_unique<ServerAddressWeightAttribute>(weight)));
      }
    }
  }
  return addresses;
}

RefCountedPtr<LoadBalancingPolicy::Config>
XdsClusterResolverLb::CreateChildPolicyConfigLocked() {
  Json::Object priority_children;
  Json::Array priority_priorities;
  // Setting up index to iterate through the discovery mechanisms and keeping
  // track the discovery_mechanism each priority belongs to.
  size_t discovery_index = 0;
  // Setting up num_priorities_remaining to track the priorities in each
  // discovery_mechanism.
  size_t num_priorities_remaining_in_discovery =
      discovery_mechanisms_[discovery_index].num_priorities;
  for (size_t priority = 0; priority < priority_list_.size(); ++priority) {
    Json child_policy;
    if (!discovery_mechanisms_[discovery_index]
             .discovery_mechanism->override_child_policy()
             .empty()) {
      child_policy = discovery_mechanisms_[discovery_index]
                         .discovery_mechanism->override_child_policy();
    } else {
      const auto& xds_lb_policy = config_->xds_lb_policy().object_value();
      if (xds_lb_policy.find("ROUND_ROBIN") != xds_lb_policy.end()) {
        const auto& localities = priority_list_[priority].localities;
        Json::Object weighted_targets;
        for (const auto& p : localities) {
          XdsLocalityName* locality_name = p.first;
          const auto& locality = p.second;
          // Construct JSON object containing locality name.
          Json::Object locality_name_json;
          if (!locality_name->region().empty()) {
            locality_name_json["region"] = locality_name->region();
          }
          if (!locality_name->zone().empty()) {
            locality_name_json["zone"] = locality_name->zone();
          }
          if (!locality_name->sub_zone().empty()) {
            locality_name_json["sub_zone"] = locality_name->sub_zone();
          }
          // Add weighted target entry.
          weighted_targets[locality_name->AsHumanReadableString()] =
              Json::Object{
                  {"weight", locality.lb_weight},
                  {"childPolicy",
                   Json::Array{
                       Json::Object{
                           {"round_robin", Json::Object()},
                       },
                   }},
              };
        }
        // Construct locality-picking policy.
        // Start with field from our config and add the "targets" field.
        child_policy = Json::Array{
            Json::Object{
                {"weighted_target_experimental",
                 Json::Object{
                     {"targets", Json::Object()},
                 }},
            },
        };
        Json::Object& config =
            *(*child_policy.mutable_array())[0].mutable_object();
        auto it = config.begin();
        GPR_ASSERT(it != config.end());
        (*it->second.mutable_object())["targets"] = std::move(weighted_targets);
      } else {
        auto it = xds_lb_policy.find("RING_HASH");
        GPR_ASSERT(it != xds_lb_policy.end());
        Json::Object ring_hash_experimental_policy = it->second.object_value();
        child_policy = Json::Array{
            Json::Object{
                {"ring_hash_experimental", ring_hash_experimental_policy},
            },
        };
      }
    }
    // Wrap it in the drop policy.
    Json::Array drop_categories;
    if (discovery_mechanisms_[discovery_index].drop_config != nullptr) {
      for (const auto& category : discovery_mechanisms_[discovery_index]
                                      .drop_config->drop_category_list()) {
        drop_categories.push_back(Json::Object{
            {"category", category.name},
            {"requests_per_million", category.parts_per_million},
        });
      }
    }
    const auto lrs_key = discovery_mechanisms_[discovery_index]
                             .discovery_mechanism->GetLrsClusterKey();
    Json::Object xds_cluster_impl_config = {
        {"clusterName", std::string(lrs_key.first)},
        {"childPolicy", std::move(child_policy)},
        {"dropCategories", std::move(drop_categories)},
        {"maxConcurrentRequests",
         config_->discovery_mechanisms()[discovery_index]
             .max_concurrent_requests},
    };
    if (!lrs_key.second.empty()) {
      xds_cluster_impl_config["edsServiceName"] = std::string(lrs_key.second);
    }
    if (config_->discovery_mechanisms()[discovery_index]
            .lrs_load_reporting_server_name.has_value()) {
      xds_cluster_impl_config["lrsLoadReportingServerName"] =
          config_->discovery_mechanisms()[discovery_index]
              .lrs_load_reporting_server_name.value();
    }
    Json locality_picking_policy = Json::Array{Json::Object{
        {"xds_cluster_impl_experimental", std::move(xds_cluster_impl_config)},
    }};
    // Add priority entry.
    const size_t child_number = priority_child_numbers_[priority];
    std::string child_name = absl::StrCat("child", child_number);
    priority_priorities.emplace_back(child_name);
    Json::Object child_config = {
        {"config", std::move(locality_picking_policy)},
    };
    if (discovery_mechanisms_[discovery_index]
            .discovery_mechanism->disable_reresolution()) {
      child_config["ignore_reresolution_requests"] = true;
    }
    priority_children[child_name] = std::move(child_config);
    // Each priority in the priority_list_ should correspond to a priority in a
    // discovery mechanism in discovery_mechanisms_ (both in the same order).
    // Keeping track of the discovery_mechanism each priority belongs to.
    --num_priorities_remaining_in_discovery;
    while (num_priorities_remaining_in_discovery == 0 &&
           discovery_index < discovery_mechanisms_.size() - 1) {
      ++discovery_index;
      num_priorities_remaining_in_discovery =
          discovery_mechanisms_[discovery_index].num_priorities;
    }
  }
  // There should be matching number of priorities in discovery_mechanisms_ and
  // in priority_list_; therefore at the end of looping through all the
  // priorities, num_priorities_remaining should be down to 0, and index should
  // be the last index in discovery_mechanisms_.
  GPR_ASSERT(num_priorities_remaining_in_discovery == 0);
  GPR_ASSERT(discovery_index == discovery_mechanisms_.size() - 1);
  Json json = Json::Array{Json::Object{
      {"priority_experimental",
       Json::Object{
           {"children", std::move(priority_children)},
           {"priorities", std::move(priority_priorities)},
       }},
  }};
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    std::string json_str = json.Dump(/*indent=*/1);
    gpr_log(
        GPR_INFO,
        "[xds_cluster_resolver_lb %p] generated config for child policy: %s",
        this, json_str.c_str());
  }
  grpc_error_handle error = GRPC_ERROR_NONE;
  RefCountedPtr<LoadBalancingPolicy::Config> config =
      LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(json, &error);
  if (error != GRPC_ERROR_NONE) {
    // This should never happen, but if it does, we basically have no
    // way to fix it, so we put the channel in TRANSIENT_FAILURE.
    gpr_log(GPR_ERROR,
            "[xds_cluster_resolver_lb %p] error parsing generated child policy "
            "config -- "
            "will put channel in TRANSIENT_FAILURE: %s",
            this, grpc_error_std_string(error).c_str());
    absl::Status status = absl::InternalError(
        "xds_cluster_resolver LB policy: error parsing generated child policy "
        "config");
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        absl::make_unique<TransientFailurePicker>(status));
    return nullptr;
  }
  return config;
}

void XdsClusterResolverLb::UpdateChildPolicyLocked() {
  if (shutting_down_) return;
  UpdateArgs update_args;
  update_args.config = CreateChildPolicyConfigLocked();
  if (update_args.config == nullptr) return;
  update_args.addresses = CreateChildPolicyAddressesLocked();
  update_args.args = CreateChildPolicyArgsLocked(args_);
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(update_args.args);
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_resolver_lb %p] Updating child policy %p",
            this, child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

grpc_channel_args* XdsClusterResolverLb::CreateChildPolicyArgsLocked(
    const grpc_channel_args* args) {
  absl::InlinedVector<grpc_arg, 2> new_args = {
      // Inhibit client-side health checking, since the balancer does this
      // for us.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_INHIBIT_HEALTH_CHECKING), 1),
  };
  return grpc_channel_args_copy_and_add(args, new_args.data(), new_args.size());
}

OrphanablePtr<LoadBalancingPolicy>
XdsClusterResolverLb::CreateChildPolicyLocked(const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          "priority_experimental", std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR,
            "[xds_cluster_resolver_lb %p] failure creating child policy", this);
    return nullptr;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_resolver_lb %p]: Created new child policy %p", this,
            lb_policy.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

//
// factory
//

class XdsClusterResolverLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    RefCountedPtr<XdsClient> xds_client =
        XdsClient::GetFromChannelArgs(*args.args);
    if (xds_client == nullptr) {
      gpr_log(GPR_ERROR,
              "XdsClient not present in channel args -- cannot instantiate "
              "xds_cluster_resolver LB policy");
      return nullptr;
    }
    return MakeOrphanable<XdsClusterResolverChildHandler>(std::move(xds_client),
                                                          std::move(args));
  }

  const char* name() const override { return kXdsClusterResolver; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error_handle* error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // xds_cluster_resolver was mentioned as a policy in the deprecated
      // loadBalancingPolicy field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:xds_cluster_resolver policy "
          "requires configuration. "
          "Please use loadBalancingConfig field of service config instead.");
      return nullptr;
    }
    std::vector<grpc_error_handle> error_list;
    std::vector<XdsClusterResolverLbConfig::DiscoveryMechanism>
        discovery_mechanisms;
    auto it = json.object_value().find("discoveryMechanisms");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:discoveryMechanisms error:required field missing"));
    } else if (it->second.type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:discoveryMechanisms error:type should be array"));
    } else {
      const Json::Array& array = it->second.array_value();
      for (size_t i = 0; i < array.size(); ++i) {
        XdsClusterResolverLbConfig::DiscoveryMechanism discovery_mechanism;
        std::vector<grpc_error_handle> discovery_mechanism_errors =
            ParseDiscoveryMechanism(array[i], &discovery_mechanism);
        if (!discovery_mechanism_errors.empty()) {
          grpc_error_handle error = GRPC_ERROR_CREATE_FROM_CPP_STRING(
              absl::StrCat("field:discovery_mechanism element: ", i, " error"));
          for (const grpc_error_handle& discovery_mechanism_error :
               discovery_mechanism_errors) {
            error = grpc_error_add_child(error, discovery_mechanism_error);
          }
          error_list.push_back(error);
        }
        discovery_mechanisms.emplace_back(std::move(discovery_mechanism));
      }
    }
    if (discovery_mechanisms.empty()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:discovery_mechanism error:list is missing or empty"));
    }
    Json xds_lb_policy = Json::Object{
        {"ROUND_ROBIN", Json::Object()},
    };
    it = json.object_value().find("xdsLbPolicy");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::ARRAY) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:xdsLbPolicy error:type should be array"));
      } else {
        const Json::Array& array = it->second.array_value();
        for (size_t i = 0; i < array.size(); ++i) {
          if (array[i].type() != Json::Type::OBJECT) {
            error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "field:xdsLbPolicy error:element should be of type object"));
            continue;
          }
          const Json::Object& policy = array[i].object_value();
          auto policy_it = policy.find("ROUND_ROBIN");
          if (policy_it != policy.end()) {
            if (policy_it->second.type() != Json::Type::OBJECT) {
              error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                  "field:ROUND_ROBIN error:type should be object"));
            }
            break;
          }
          policy_it = policy.find("RING_HASH");
          if (policy_it != policy.end()) {
            xds_lb_policy = array[i];
            size_t min_ring_size;
            size_t max_ring_size;
            ParseRingHashLbConfig(policy_it->second, &min_ring_size,
                                  &max_ring_size, &error_list);
          }
        }
      }
    }
    // Construct config.
    if (error_list.empty()) {
      return MakeRefCounted<XdsClusterResolverLbConfig>(
          std::move(discovery_mechanisms), std::move(xds_lb_policy));
    } else {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR(
          "xds_cluster_resolver_experimental LB policy config", &error_list);
      return nullptr;
    }
  }

 private:
  static std::vector<grpc_error_handle> ParseDiscoveryMechanism(
      const Json& json,
      XdsClusterResolverLbConfig::DiscoveryMechanism* discovery_mechanism) {
    std::vector<grpc_error_handle> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "value should be of type object"));
      return error_list;
    }
    // Cluster name.
    auto it = json.object_value().find("clusterName");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:clusterName error:required field missing"));
    } else if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:clusterName error:type should be string"));
    } else {
      discovery_mechanism->cluster_name = it->second.string_value();
    }
    // LRS load reporting server name.
    it = json.object_value().find("lrsLoadReportingServerName");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:lrsLoadReportingServerName error:type should be string"));
      } else {
        discovery_mechanism->lrs_load_reporting_server_name.emplace(
            it->second.string_value());
      }
    }
    // Max concurrent requests.
    discovery_mechanism->max_concurrent_requests = 1024;
    it = json.object_value().find("max_concurrent_requests");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::NUMBER) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:max_concurrent_requests error:must be of type number"));
      } else {
        discovery_mechanism->max_concurrent_requests =
            gpr_parse_nonnegative_int(it->second.string_value().c_str());
      }
    }
    // Discovery Mechanism type
    it = json.object_value().find("type");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:type error:required field missing"));
    } else if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:type error:type should be string"));
    } else {
      if (it->second.string_value() == "EDS") {
        discovery_mechanism->type = XdsClusterResolverLbConfig::
            DiscoveryMechanism::DiscoveryMechanismType::EDS;
        it = json.object_value().find("edsServiceName");
        if (it != json.object_value().end()) {
          if (it->second.type() != Json::Type::STRING) {
            error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "field:edsServiceName error:type should be string"));
          } else {
            discovery_mechanism->eds_service_name = it->second.string_value();
          }
        }
      } else if (it->second.string_value() == "LOGICAL_DNS") {
        discovery_mechanism->type = XdsClusterResolverLbConfig::
            DiscoveryMechanism::DiscoveryMechanismType::LOGICAL_DNS;
        it = json.object_value().find("dnsHostname");
        if (it == json.object_value().end()) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:dnsHostname error:required field missing"));
        } else if (it->second.type() != Json::Type::STRING) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:dnsHostname error:type should be string"));
        } else {
          discovery_mechanism->dns_hostname = it->second.string_value();
        }
      } else {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:type error:invalid type"));
      }
    }
    return error_list;
  }

  class XdsClusterResolverChildHandler : public ChildPolicyHandler {
   public:
    XdsClusterResolverChildHandler(RefCountedPtr<XdsClient> xds_client,
                                   Args args)
        : ChildPolicyHandler(std::move(args),
                             &grpc_lb_xds_cluster_resolver_trace),
          xds_client_(std::move(xds_client)) {}

    bool ConfigChangeRequiresNewPolicyInstance(
        LoadBalancingPolicy::Config* old_config,
        LoadBalancingPolicy::Config* new_config) const override {
      GPR_ASSERT(old_config->name() == kXdsClusterResolver);
      GPR_ASSERT(new_config->name() == kXdsClusterResolver);
      XdsClusterResolverLbConfig* old_xds_cluster_resolver_config =
          static_cast<XdsClusterResolverLbConfig*>(old_config);
      XdsClusterResolverLbConfig* new_xds_cluster_resolver_config =
          static_cast<XdsClusterResolverLbConfig*>(new_config);
      return old_xds_cluster_resolver_config->discovery_mechanisms() !=
             new_xds_cluster_resolver_config->discovery_mechanisms();
    }

    OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
        const char* /*name*/, LoadBalancingPolicy::Args args) const override {
      return MakeOrphanable<XdsClusterResolverLb>(xds_client_, std::move(args));
    }

   private:
    RefCountedPtr<XdsClient> xds_client_;
  };
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_lb_policy_xds_cluster_resolver_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::XdsClusterResolverLbFactory>());
}

void grpc_lb_policy_xds_cluster_resolver_shutdown() {}
