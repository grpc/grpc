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
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/work_serializer.h"
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

    bool operator==(const DiscoveryMechanism& other) const {
      return (cluster_name == other.cluster_name &&
              lrs_load_reporting_server_name ==
                  other.lrs_load_reporting_server_name &&
              max_concurrent_requests == other.max_concurrent_requests &&
              type == other.type && eds_service_name == other.eds_service_name);
    }
  };

  XdsClusterResolverLbConfig(
      std::vector<DiscoveryMechanism> discovery_mechanisms,
      Json locality_picking_policy, Json endpoint_picking_policy)
      : discovery_mechanisms_(std::move(discovery_mechanisms)),
        locality_picking_policy_(std::move(locality_picking_policy)),
        endpoint_picking_policy_(std::move(endpoint_picking_policy)) {}

  const char* name() const override { return kXdsClusterResolver; }

  const Json& locality_picking_policy() const {
    return locality_picking_policy_;
  }
  const Json& endpoint_picking_policy() const {
    return endpoint_picking_policy_;
  }
  const std::vector<DiscoveryMechanism>& discovery_mechanisms() const {
    return discovery_mechanisms_;
  }

 private:
  std::vector<DiscoveryMechanism> discovery_mechanisms_;
  Json locality_picking_policy_;
  Json endpoint_picking_policy_;
};

// EDS LB policy.
class XdsClusterResolverLb : public LoadBalancingPolicy {
 public:
  XdsClusterResolverLb(RefCountedPtr<XdsClient> xds_client, Args args);

  const char* name() const override { return kXdsClusterResolver; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  // Discovery Mechanism Interface
  //
  // Implemented by EDS and LOGICAL_DNS.
  //
  // Contains a watcher object which will get notified for events like
  // OnEndpointChanged, OnResourceDoesNotExist, and OnError.
  //
  // Must implement Shutdown() method to cancel the watches.
  class DiscoveryMechanismInterface
      : public InternallyRefCounted<DiscoveryMechanismInterface> {
   public:
    explicit DiscoveryMechanismInterface() : num_priorities_(0){};
    virtual ~DiscoveryMechanismInterface() = default;
    uint32_t num_priorities() const { return num_priorities_; };
    void SetNumPriorities(uint32_t num) { num_priorities_ = num; }
    virtual void Orphan() override = 0;

   private:
    // Number of priorities this mechanism contributes.
    uint32_t num_priorities_;
  };

  class EdsDiscoveryMechanism : public DiscoveryMechanismInterface {
   public:
    EdsDiscoveryMechanism(uint32_t index,
                          RefCountedPtr<XdsClusterResolverLb> parent);
    void Orphan() override;

   private:
    class EndpointWatcher : public XdsClient::EndpointWatcherInterface {
     public:
      explicit EndpointWatcher(
          RefCountedPtr<EdsDiscoveryMechanism> discovery_mechanism)
          : discovery_mechanism_(std::move(discovery_mechanism)) {}
      ~EndpointWatcher() override {
        discovery_mechanism_.reset(DEBUG_LOCATION, "EndpointWatcher");
      }
      void OnEndpointChanged(XdsApi::EdsUpdate update) override {
        new Notifier(discovery_mechanism_, std::move(update));
      }
      void OnError(grpc_error* error) override {
        new Notifier(discovery_mechanism_, error);
      }
      void OnResourceDoesNotExist() override {
        new Notifier(discovery_mechanism_);
      }

     private:
      class Notifier {
       public:
        Notifier(RefCountedPtr<EdsDiscoveryMechanism> discovery_mechanism,
                 XdsApi::EdsUpdate update);
        Notifier(RefCountedPtr<EdsDiscoveryMechanism> discovery_mechanism,
                 grpc_error* error);
        explicit Notifier(
            RefCountedPtr<EdsDiscoveryMechanism> discovery_mechanism);
        ~Notifier() { discovery_mechanism_.reset(DEBUG_LOCATION, "Notifier"); }

       private:
        enum Type { kUpdate, kError, kDoesNotExist };

        static void RunInExecCtx(void* arg, grpc_error* error);
        void RunInWorkSerializer(grpc_error* error);

        RefCountedPtr<EdsDiscoveryMechanism> discovery_mechanism_;
        grpc_closure closure_;
        XdsApi::EdsUpdate update_;
        Type type_;
      };

      RefCountedPtr<EdsDiscoveryMechanism> discovery_mechanism_;
    };

    RefCountedPtr<XdsClusterResolverLb> parent_;
    // Stores its own index in the vector of DiscoveryMechanismInterface.
    uint32_t index_;
    EndpointWatcher* watcher_ = nullptr;
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
    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override;

   private:
    RefCountedPtr<XdsClusterResolverLb> xds_cluster_resolver_policy_;
  };

  ~XdsClusterResolverLb() override;

  void ShutdownLocked() override;

  void OnEndpointChanged(uint32_t index, XdsApi::EdsUpdate update);
  void OnError(uint32_t index, grpc_error* error);
  void OnResourceDoesNotExist(uint32_t index);

  void MaybeDestroyChildPolicyLocked();

  void UpdatePriorityList(XdsApi::EdsUpdate::PriorityList priority_list);
  void UpdateChildPolicyLocked();
  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const grpc_channel_args* args);
  ServerAddressList CreateChildPolicyAddressesLocked();
  RefCountedPtr<Config> CreateChildPolicyConfigLocked();
  grpc_channel_args* CreateChildPolicyArgsLocked(
      const grpc_channel_args* args_in);

  // Caller must ensure that config_ is set before calling.
  const absl::string_view GetXdsClusterResolverResourceName(
      uint32_t index) const {
    if (!is_xds_uri_) return server_name_;
    if (!config_->discovery_mechanisms()[index].eds_service_name.empty()) {
      return config_->discovery_mechanisms()[index].eds_service_name;
    }
    return config_->discovery_mechanisms()[index].cluster_name;
  }

  // Returns a pair containing the cluster and eds_service_name
  // to use for LRS load reporting. Caller must ensure that config_ is set
  // before calling.
  // TODO@donnadionne remove default indx 0
  std::pair<absl::string_view, absl::string_view> GetLrsClusterKey(
      uint32_t index) const {
    if (!is_xds_uri_) return {server_name_, nullptr};
    return {config_->discovery_mechanisms()[index].cluster_name,
            config_->discovery_mechanisms()[index].eds_service_name};
  }

  // Server name from target URI.
  std::string server_name_;
  bool is_xds_uri_;

  // Current channel args and config from the resolver.
  const grpc_channel_args* args_ = nullptr;
  RefCountedPtr<XdsClusterResolverLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // The xds client and endpoint watcher.
  RefCountedPtr<XdsClient> xds_client_;

  // Vector of discovery mechansism entries in priority order.
  struct DiscoveryMechanismEntry {
    // Note that this is not owned, so this pointer must never be dereferenced.
    OrphanablePtr<DiscoveryMechanismInterface> discovery_mechanism;
    // Number of priorities this mechanism has contributed to priority_list_.
    // (The sum of this across all discovery mechanisms should always equal
    // the number of priorities in priority_list_.)
    uint32_t num_priorities = 0;
    RefCountedPtr<XdsApi::EdsUpdate::DropConfig> drop_config;
  };
  std::vector<DiscoveryMechanismEntry> discovery_mechanisms_;

  // The latest data from the endpoint watcher.
  XdsApi::EdsUpdate::PriorityList priority_list_;
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
            "[xds_cluster_resolverlb %p] child policy updated state=%s (%s) "
            "picker=%p",
            xds_cluster_resolver_policy_.get(), ConnectivityStateName(state),
            status.ToString().c_str(), picker.get());
  }
  xds_cluster_resolver_policy_->channel_control_helper()->UpdateState(
      state, status, std::move(picker));
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
XdsClusterResolverLb::EdsDiscoveryMechanism::EdsDiscoveryMechanism(
    uint32_t index, RefCountedPtr<XdsClusterResolverLb> parent)
    : parent_(std::move(parent)), index_(index) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO, "[EdsDiscoveryMechanism %p] starting xds watch for %s",
            this,
            std::string(parent_->GetXdsClusterResolverResourceName(index_))
                .c_str());
  }
  auto watcher = absl::make_unique<EndpointWatcher>(
      Ref(DEBUG_LOCATION, "EdsDiscoveryMechanism"));
  watcher_ = watcher.get();
  parent_->xds_client_->WatchEndpointData(
      parent_->GetXdsClusterResolverResourceName(index_), std::move(watcher));
}

void XdsClusterResolverLb::EdsDiscoveryMechanism::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO, "[EdsDiscoveryMechanism %p] cancelling xds watch for %s",
            this,
            std::string(parent_->GetXdsClusterResolverResourceName(index_))
                .c_str());
  }
  parent_->xds_client_->CancelEndpointDataWatch(
      parent_->GetXdsClusterResolverResourceName(index_), watcher_);
  Unref();
}

//
// XdsClusterResolverLb::EndpointWatcher::Notifier
//

XdsClusterResolverLb::EdsDiscoveryMechanism::EndpointWatcher::Notifier::
    Notifier(RefCountedPtr<XdsClusterResolverLb::EdsDiscoveryMechanism>
                 discovery_mechanism,
             XdsApi::EdsUpdate update)
    : discovery_mechanism_(std::move(discovery_mechanism)),
      update_(std::move(update)),
      type_(kUpdate) {
  GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &closure_, GRPC_ERROR_NONE);
}

XdsClusterResolverLb::EdsDiscoveryMechanism::EndpointWatcher::Notifier::
    Notifier(RefCountedPtr<XdsClusterResolverLb::EdsDiscoveryMechanism>
                 discovery_mechanism,
             grpc_error* error)
    : discovery_mechanism_(std::move(discovery_mechanism)), type_(kError) {
  GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &closure_, error);
}

XdsClusterResolverLb::EdsDiscoveryMechanism::EndpointWatcher::Notifier::
    Notifier(RefCountedPtr<XdsClusterResolverLb::EdsDiscoveryMechanism>
                 discovery_mechanism)
    : discovery_mechanism_(std::move(discovery_mechanism)),
      type_(kDoesNotExist) {
  GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &closure_, GRPC_ERROR_NONE);
}

void XdsClusterResolverLb::EdsDiscoveryMechanism::EndpointWatcher::Notifier::
    RunInExecCtx(void* arg, grpc_error* error) {
  Notifier* self = static_cast<Notifier*>(arg);
  GRPC_ERROR_REF(error);
  self->discovery_mechanism_->parent_->work_serializer()->Run(
      [self, error]() { self->RunInWorkSerializer(error); }, DEBUG_LOCATION);
}

void XdsClusterResolverLb::EdsDiscoveryMechanism::EndpointWatcher::Notifier::
    RunInWorkSerializer(grpc_error* error) {
  switch (type_) {
    case kUpdate:
      discovery_mechanism_->parent_->OnEndpointChanged(
          discovery_mechanism_->index_, std::move(update_));
      break;
    case kError:
      discovery_mechanism_->parent_->OnError(discovery_mechanism_->index_,
                                             error);
      break;
    case kDoesNotExist:
      discovery_mechanism_->parent_->OnResourceDoesNotExist(
          discovery_mechanism_->index_);
      break;
  };
  delete this;
}

//
// XdsClusterResolverLb public methods
//

XdsClusterResolverLb::XdsClusterResolverLb(RefCountedPtr<XdsClient> xds_client,
                                           Args args)
    : LoadBalancingPolicy(std::move(args)), xds_client_(std::move(xds_client)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_resolverlb %p] created -- using xds client %p", this,
            xds_client_.get());
  }
  // Record server name.
  const char* server_uri =
      grpc_channel_args_find_string(args.args, GRPC_ARG_SERVER_URI);
  GPR_ASSERT(server_uri != nullptr);
  absl::StatusOr<URI> uri = URI::Parse(server_uri);
  GPR_ASSERT(uri.ok() && !uri->path().empty());
  server_name_ = std::string(absl::StripPrefix(uri->path(), "/"));
  is_xds_uri_ = uri->scheme() == "xds";
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] server name from channel (is_xds_uri=%d): %s",
            this, is_xds_uri_, server_name_.c_str());
  }
  // EDS-only flow.
  if (!is_xds_uri_) {
    // Setup channelz linkage.
    channelz::ChannelNode* parent_channelz_node =
        grpc_channel_args_find_pointer<channelz::ChannelNode>(
            args.args, GRPC_ARG_CHANNELZ_CHANNEL_NODE);
    if (parent_channelz_node != nullptr) {
      xds_client_->AddChannelzLinkage(parent_channelz_node);
    }
    // Couple polling.
    grpc_pollset_set_add_pollset_set(xds_client_->interested_parties(),
                                     interested_parties());
  }
}

XdsClusterResolverLb::~XdsClusterResolverLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(
        GPR_INFO,
        "[xds_cluster_resolverlb %p] destroying xds_cluster_resolver LB policy",
        this);
  }
}

void XdsClusterResolverLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_resolverlb %p] shutting down", this);
  }
  shutting_down_ = true;
  MaybeDestroyChildPolicyLocked();
  discovery_mechanisms_.clear();
  if (!is_xds_uri_) {
    // Remove channelz linkage.
    channelz::ChannelNode* parent_channelz_node =
        grpc_channel_args_find_pointer<channelz::ChannelNode>(
            args_, GRPC_ARG_CHANNELZ_CHANNEL_NODE);
    if (parent_channelz_node != nullptr) {
      xds_client_->RemoveChannelzLinkage(parent_channelz_node);
    }
    // Decouple polling.
    grpc_pollset_set_del_pollset_set(xds_client_->interested_parties(),
                                     interested_parties());
  }
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
    gpr_log(GPR_INFO, "[xds_cluster_resolverlb %p] Received update", this);
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
    for (auto config : config_->discovery_mechanisms()) {
      // TODO@donnadionne: need to add new types of
      // watchers.
      DiscoveryMechanismEntry entry;
      entry.discovery_mechanism =
          grpc_core::MakeOrphanable<EdsDiscoveryMechanism>(
              discovery_mechanisms_.size(),
              Ref(DEBUG_LOCATION, "EdsDiscoveryMechanism"));
      discovery_mechanisms_.push_back(std::move(entry));
    }
  }
}

void XdsClusterResolverLb::ResetBackoffLocked() {
  // When the XdsClient is instantiated in the resolver instead of in this
  // LB policy, this is done via the resolver, so we don't need to do it here.
  if (!is_xds_uri_ && xds_client_ != nullptr) xds_client_->ResetBackoff();
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
  }
}

void XdsClusterResolverLb::OnEndpointChanged(uint32_t index,
                                             XdsApi::EdsUpdate update) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_resolverlb %p] Received EDS update from xds client",
            this);
  }
  // Find the range of priorities to be replaced by this update.
  auto start = 0;
  for (int i = 0; i < index; ++i) {
    start += discovery_mechanisms_[i].discovery_mechanism->num_priorities();
  }
  auto end = start +
             discovery_mechanisms_[index].discovery_mechanism->num_priorities();
  // Rebuild a priority list where the range the priorities is replaced with the
  // priority list from the new update.
  XdsApi::EdsUpdate::PriorityList priority_list(priority_list_.begin(),
                                                priority_list_.begin() + start);
  priority_list.insert(priority_list.end(), update.priorities.begin(),
                       update.priorities.end());
  priority_list.insert(priority_list.end(),
                       priority_list_.begin() + start + end,
                       priority_list_.end());
  // Update the number of priorities in the new update.
  discovery_mechanisms_[index].discovery_mechanism->SetNumPriorities(
      update.priorities.size());
  // Update the drop config.
  discovery_mechanisms_[index].drop_config = std::move(update.drop_config);
  // If priority list is empty, add a single priority, just so that we
  // have a child in which to create the xds_cluster_impl policy.
  if (priority_list.empty()) {
    gpr_log(GPR_INFO, "DONNA this happened???");
    priority_list.emplace_back();
    discovery_mechanisms_[index].discovery_mechanism->SetNumPriorities(1);
  }
  // Update child policy.
  UpdatePriorityList(std::move(priority_list));
}

void XdsClusterResolverLb::OnError(uint32_t index, grpc_error* error) {
  gpr_log(GPR_ERROR,
          "[xds_cluster_resolverlb %p] xds watcher reported error: %s", this,
          grpc_error_string(error));
  // Go into TRANSIENT_FAILURE if we have not yet created the child
  // policy (i.e., we have not yet received data from xds).  Otherwise,
  // we keep running with the data we had previously.
  if (child_policy_ == nullptr) {
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, grpc_error_to_absl_status(error),
        absl::make_unique<TransientFailurePicker>(error));
  } else {
    GRPC_ERROR_UNREF(error);
  }
}

void XdsClusterResolverLb::OnResourceDoesNotExist(uint32_t index) {
  gpr_log(GPR_ERROR,
          "[xds_cluster_resolverlb %p] EDS resource does not exist -- "
          "reporting TRANSIENT_FAILURE",
          this);
  grpc_error* error = grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("EDS resource does not exist"),
      GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_TRANSIENT_FAILURE, grpc_error_to_absl_status(error),
      absl::make_unique<TransientFailurePicker>(error));
  MaybeDestroyChildPolicyLocked();
}

//
// child policy-related methods
//

void XdsClusterResolverLb::UpdatePriorityList(
    XdsApi::EdsUpdate::PriorityList priority_list) {
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
        addresses.emplace_back(
            endpoint
                .WithAttribute(kHierarchicalPathAttributeKey,
                               MakeHierarchicalPathAttribute(hierarchical_path))
                .WithAttribute(kXdsLocalityNameAttributeKey,
                               absl::make_unique<XdsLocalityAttribute>(
                                   locality_name->Ref())));
      }
    }
  }
  return addresses;
}

RefCountedPtr<LoadBalancingPolicy::Config>
XdsClusterResolverLb::CreateChildPolicyConfigLocked() {
  Json::Object priority_children;
  Json::Array priority_priorities;
  uint32_t index = 0;
  uint32_t num_priorities_remaining = 0;
  if (!discovery_mechanisms_.empty()) {
    num_priorities_remaining =
        discovery_mechanisms_[index].discovery_mechanism->num_priorities();
  } else {
    // If there are no discovery mechanisms there should be no priorities
    GPR_ASSERT(priority_list_.empty());
  }
  gpr_log(GPR_INFO, "DONNA size 1 %d size 2 %d", discovery_mechanisms_.size(),
          priority_list_.size());
  for (size_t priority = 0; priority < priority_list_.size(); ++priority) {
    gpr_log(GPR_INFO, "DONNA index %d and num %d", index,
            num_priorities_remaining);
    if (num_priorities_remaining == 0) {
      ++index;
      num_priorities_remaining =
          discovery_mechanisms_[index].discovery_mechanism->num_priorities();
    } else {
      --num_priorities_remaining;
    }
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
        locality_name_json["subzone"] = locality_name->sub_zone();
      }
      // Add weighted target entry.
      weighted_targets[locality_name->AsHumanReadableString()] = Json::Object{
          {"weight", locality.lb_weight},
          {"childPolicy", config_->endpoint_picking_policy()},
      };
    }
    // Construct locality-picking policy.
    // Start with field from our config and add the "targets" field.
    Json locality_picking_config = config_->locality_picking_policy();
    Json::Object& config =
        *(*locality_picking_config.mutable_array())[0].mutable_object();
    auto it = config.begin();
    GPR_ASSERT(it != config.end());
    (*it->second.mutable_object())["targets"] = std::move(weighted_targets);
    // Wrap it in the drop policy.
    Json::Array drop_categories;
    for (const auto& category :
         discovery_mechanisms_[index].drop_config->drop_category_list()) {
      drop_categories.push_back(Json::Object{
          {"category", category.name},
          {"requests_per_million", category.parts_per_million},
      });
    }
    const auto lrs_key = GetLrsClusterKey(index);
    Json::Object xds_cluster_impl_config = {
        {"clusterName", std::string(lrs_key.first)},
        {"childPolicy", std::move(locality_picking_config)},
        {"dropCategories", std::move(drop_categories)},
        {"maxConcurrentRequests",
         config_->discovery_mechanisms()[index].max_concurrent_requests},
    };
    if (!lrs_key.second.empty()) {
      xds_cluster_impl_config["edsServiceName"] = std::string(lrs_key.second);
    }
    if (config_->discovery_mechanisms()[index]
            .lrs_load_reporting_server_name.has_value()) {
      xds_cluster_impl_config["lrsLoadReportingServerName"] =
          config_->discovery_mechanisms()[index]
              .lrs_load_reporting_server_name.value();
    }
    Json locality_picking_policy = Json::Array{Json::Object{
        {"xds_cluster_impl_experimental", std::move(xds_cluster_impl_config)},
    }};
    // Add priority entry.
    const size_t child_number = priority_child_numbers_[priority];
    std::string child_name = absl::StrCat("child", child_number);
    priority_priorities.emplace_back(child_name);
    priority_children[child_name] = Json::Object{
        {"config", std::move(locality_picking_policy)},
        {"ignore_reresolution_requests", true},
    };
  }
  // There should be matching number of priorities in discovery_mechanisms_ and
  // in priority_list_.
  GPR_ASSERT(num_priorities_remaining == 0);
  GPR_ASSERT(discovery_mechanisms_.empty() ||
             index == discovery_mechanisms_.size() - 1);
  Json json = Json::Array{Json::Object{
      {"priority_experimental",
       Json::Object{
           {"children", std::move(priority_children)},
           {"priorities", std::move(priority_priorities)},
       }},
  }};
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    std::string json_str = json.Dump(/*indent=*/1);
    gpr_log(GPR_INFO,
            "[xds_cluster_resolverlb %p] generated config for child policy: %s",
            this, json_str.c_str());
  }
  grpc_error* error = GRPC_ERROR_NONE;
  RefCountedPtr<LoadBalancingPolicy::Config> config =
      LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(json, &error);
  if (error != GRPC_ERROR_NONE) {
    // This should never happen, but if it does, we basically have no
    // way to fix it, so we put the channel in TRANSIENT_FAILURE.
    gpr_log(GPR_ERROR,
            "[xds_cluster_resolverlb %p] error parsing generated child policy "
            "config -- "
            "will put channel in TRANSIENT_FAILURE: %s",
            this, grpc_error_string(error));
    error = grpc_error_set_int(
        grpc_error_add_child(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                 "xds_cluster_resolver LB policy: error "
                                 "parsing generated child policy config"),
                             error),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_INTERNAL);
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, grpc_error_to_absl_status(error),
        absl::make_unique<TransientFailurePicker>(error));
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
    gpr_log(GPR_INFO, "[xds_cluster_resolverlb %p] Updating child policy %p",
            this, child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

grpc_channel_args* XdsClusterResolverLb::CreateChildPolicyArgsLocked(
    const grpc_channel_args* args) {
  grpc_arg args_to_add[] = {
      // A channel arg indicating if the target is a backend inferred from an
      // xds load balancer.
      // TODO(roth): This isn't needed with the new fallback design.
      // Remove as part of implementing the new fallback functionality.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_IS_BACKEND_FROM_XDS_LOAD_BALANCER),
          1),
      // Inhibit client-side health checking, since the balancer does
      // this for us.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_INHIBIT_HEALTH_CHECKING), 1),
  };
  return grpc_channel_args_copy_and_add(args, args_to_add,
                                        GPR_ARRAY_SIZE(args_to_add));
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
            "[xds_cluster_resolverlb %p] failure creating child policy", this);
    return nullptr;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_cluster_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_resolverlb %p]: Created new child policy %p", this,
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
    grpc_error* error = GRPC_ERROR_NONE;
    RefCountedPtr<XdsClient> xds_client = XdsClient::GetOrCreate(&error);
    if (error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR,
              "cannot get XdsClient to instantiate xds_cluster_resolver LB "
              "policy: %s",
              grpc_error_string(error));
      GRPC_ERROR_UNREF(error);
      return nullptr;
    }
    return MakeOrphanable<XdsClusterResolverChildHandler>(std::move(xds_client),
                                                          std::move(args));
  }

  const char* name() const override { return kXdsClusterResolver; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
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
    std::vector<grpc_error*> error_list;
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
        std::vector<grpc_error*> discovery_mechanism_errors =
            ParseDiscoveryMechanism(array[i], &discovery_mechanism);
        if (!discovery_mechanism_errors.empty()) {
          grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("field:discovery_mechanism element: ", i, " error")
                  .c_str());
          for (grpc_error* discovery_mechanism_error :
               discovery_mechanism_errors) {
            error = grpc_error_add_child(error, discovery_mechanism_error);
          }
          error_list.push_back(error);
        }
        discovery_mechanisms.emplace_back(std::move(discovery_mechanism));
      }
    }
    // Locality-picking policy.
    Json locality_picking_policy;
    it = json.object_value().find("localityPickingPolicy");
    if (it == json.object_value().end()) {
      locality_picking_policy = Json::Array{
          Json::Object{
              {"weighted_target_experimental",
               Json::Object{
                   {"targets", Json::Object()},
               }},
          },
      };
    } else {
      locality_picking_policy = it->second;
    }
    grpc_error* parse_error = GRPC_ERROR_NONE;
    if (LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
            locality_picking_policy, &parse_error) == nullptr) {
      GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
      error_list.push_back(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "localityPickingPolicy", &parse_error, 1));
      GRPC_ERROR_UNREF(parse_error);
    }
    // Endpoint-picking policy.  Called "childPolicy" for xds policy.
    Json endpoint_picking_policy;
    it = json.object_value().find("endpointPickingPolicy");
    if (it == json.object_value().end()) {
      endpoint_picking_policy = Json::Array{
          Json::Object{
              {"round_robin", Json::Object()},
          },
      };
    } else {
      endpoint_picking_policy = it->second;
    }
    parse_error = GRPC_ERROR_NONE;
    if (LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
            endpoint_picking_policy, &parse_error) == nullptr) {
      GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
      error_list.push_back(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "endpointPickingPolicy", &parse_error, 1));
      GRPC_ERROR_UNREF(parse_error);
    }
    // Construct config.
    if (error_list.empty() && !discovery_mechanisms.empty()) {
      return MakeRefCounted<XdsClusterResolverLbConfig>(
          std::move(discovery_mechanisms), std::move(locality_picking_policy),
          std::move(endpoint_picking_policy));
    } else {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR(
          "xds_cluster_resolver_experimental LB policy config", &error_list);
      return nullptr;
    }
  }

 private:
  static std::vector<grpc_error*> ParseDiscoveryMechanism(
      const Json& json,
      XdsClusterResolverLbConfig::DiscoveryMechanism* discovery_mechanism) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "value should be of type object"));
      return error_list;
    }
    // EDS service name.
    auto it = json.object_value().find("edsServiceName");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:xds_cluster_resolverServiceName error:type should be "
            "string"));
      } else {
        discovery_mechanism->eds_service_name = it->second.string_value();
      }
    }
    // Cluster name.
    it = json.object_value().find("clusterName");
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
        const char* name, LoadBalancingPolicy::Args args) const override {
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
