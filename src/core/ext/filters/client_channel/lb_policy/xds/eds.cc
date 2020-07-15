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
#include "src/core/ext/filters/client_channel/xds/xds_channel_args.h"
#include "src/core/ext/filters/client_channel/xds/xds_client.h"
#include "src/core/ext/filters/client_channel/xds/xds_client_stats.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/uri/uri_parser.h"

#define GRPC_EDS_DEFAULT_FALLBACK_TIMEOUT 10000

namespace grpc_core {

TraceFlag grpc_lb_eds_trace(false, "eds_lb");

namespace {

constexpr char kEds[] = "eds_experimental";

// Config for EDS LB policy.
class EdsLbConfig : public LoadBalancingPolicy::Config {
 public:
  EdsLbConfig(std::string cluster_name, std::string eds_service_name,
              absl::optional<std::string> lrs_load_reporting_server_name,
              Json locality_picking_policy, Json endpoint_picking_policy)
      : cluster_name_(std::move(cluster_name)),
        eds_service_name_(std::move(eds_service_name)),
        lrs_load_reporting_server_name_(
            std::move(lrs_load_reporting_server_name)),
        locality_picking_policy_(std::move(locality_picking_policy)),
        endpoint_picking_policy_(std::move(endpoint_picking_policy)) {}

  const char* name() const override { return kEds; }

  const std::string& cluster_name() const { return cluster_name_; }
  const std::string& eds_service_name() const { return eds_service_name_; }
  const absl::optional<std::string>& lrs_load_reporting_server_name() const {
    return lrs_load_reporting_server_name_;
  };
  const Json& locality_picking_policy() const {
    return locality_picking_policy_;
  }
  const Json& endpoint_picking_policy() const {
    return endpoint_picking_policy_;
  }

 private:
  std::string cluster_name_;
  std::string eds_service_name_;
  absl::optional<std::string> lrs_load_reporting_server_name_;
  Json locality_picking_policy_;
  Json endpoint_picking_policy_;
};

// EDS LB policy.
class EdsLb : public LoadBalancingPolicy {
 public:
  explicit EdsLb(Args args);

  const char* name() const override { return kEds; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  class EndpointWatcher;

  // A simple wrapper for ref-counting a picker from the child policy.
  class ChildPickerWrapper : public RefCounted<ChildPickerWrapper> {
   public:
    explicit ChildPickerWrapper(std::unique_ptr<SubchannelPicker> picker)
        : picker_(std::move(picker)) {}
    PickResult Pick(PickArgs args) { return picker_->Pick(args); }

   private:
    std::unique_ptr<SubchannelPicker> picker_;
  };

  // A picker that handles drops.
  class DropPicker : public SubchannelPicker {
   public:
    explicit DropPicker(EdsLb* eds_policy);

    PickResult Pick(PickArgs args) override;

   private:
    RefCountedPtr<XdsApi::DropConfig> drop_config_;
    RefCountedPtr<XdsClusterDropStats> drop_stats_;
    RefCountedPtr<ChildPickerWrapper> child_picker_;
  };

  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<EdsLb> eds_policy)
        : eds_policy_(std::move(eds_policy)) {}

    ~Helper() { eds_policy_.reset(DEBUG_LOCATION, "Helper"); }

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state,
                     std::unique_ptr<SubchannelPicker> picker) override;
    // This is a no-op, because we get the addresses from the xds
    // client, which is a watch-based API.
    void RequestReresolution() override {}
    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override;

   private:
    RefCountedPtr<EdsLb> eds_policy_;
  };

  ~EdsLb();

  void ShutdownLocked() override;

  void MaybeDestroyChildPolicyLocked();

  void UpdatePriorityList(XdsApi::PriorityListUpdate priority_list_update);
  void UpdateChildPolicyLocked();
  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const grpc_channel_args* args);
  ServerAddressList CreateChildPolicyAddressesLocked();
  RefCountedPtr<Config> CreateChildPolicyConfigLocked();
  grpc_channel_args* CreateChildPolicyArgsLocked(
      const grpc_channel_args* args_in);
  void MaybeUpdateDropPickerLocked();

  // Caller must ensure that config_ is set before calling.
  const absl::string_view GetEdsResourceName() const {
    if (xds_client_from_channel_ == nullptr) return server_name_;
    if (!config_->eds_service_name().empty()) {
      return config_->eds_service_name();
    }
    return config_->cluster_name();
  }

  // Returns a pair containing the cluster and eds_service_name to use
  // for LRS load reporting.
  // Caller must ensure that config_ is set before calling.
  std::pair<absl::string_view, absl::string_view> GetLrsClusterKey() const {
    if (xds_client_from_channel_ == nullptr) return {server_name_, nullptr};
    return {config_->cluster_name(), config_->eds_service_name()};
  }

  XdsClient* xds_client() const {
    return xds_client_from_channel_ != nullptr ? xds_client_from_channel_.get()
                                               : xds_client_.get();
  }

  // Server name from target URI.
  std::string server_name_;

  // Current channel args and config from the resolver.
  const grpc_channel_args* args_ = nullptr;
  RefCountedPtr<EdsLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // The xds client and endpoint watcher.
  // If we get the XdsClient from the channel, we store it in
  // xds_client_from_channel_; if we create it ourselves, we store it in
  // xds_client_.
  RefCountedPtr<XdsClient> xds_client_from_channel_;
  OrphanablePtr<XdsClient> xds_client_;
  // A pointer to the endpoint watcher, to be used when cancelling the watch.
  // Note that this is not owned, so this pointer must never be derefernced.
  EndpointWatcher* endpoint_watcher_ = nullptr;
  // The latest data from the endpoint watcher.
  XdsApi::PriorityListUpdate priority_list_update_;
  // State used to retain child policy names for priority policy.
  std::vector<size_t /*child_number*/> priority_child_numbers_;

  RefCountedPtr<XdsApi::DropConfig> drop_config_;
  RefCountedPtr<XdsClusterDropStats> drop_stats_;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // The latest state and picker returned from the child policy.
  grpc_connectivity_state child_state_;
  RefCountedPtr<ChildPickerWrapper> child_picker_;
};

//
// EdsLb::DropPicker
//

EdsLb::DropPicker::DropPicker(EdsLb* eds_policy)
    : drop_config_(eds_policy->drop_config_),
      drop_stats_(eds_policy->drop_stats_),
      child_picker_(eds_policy->child_picker_) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] constructed new drop picker %p", eds_policy,
            this);
  }
}

EdsLb::PickResult EdsLb::DropPicker::Pick(PickArgs args) {
  // Handle drop.
  const std::string* drop_category;
  if (drop_config_->ShouldDrop(&drop_category)) {
    if (drop_stats_ != nullptr) drop_stats_->AddCallDropped(*drop_category);
    PickResult result;
    result.type = PickResult::PICK_COMPLETE;
    return result;
  }
  // If we're not dropping all calls, we should always have a child picker.
  if (child_picker_ == nullptr) {  // Should never happen.
    PickResult result;
    result.type = PickResult::PICK_FAILED;
    result.error =
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                               "eds drop picker not given any child picker"),
                           GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_INTERNAL);
    return result;
  }
  // Not dropping, so delegate to child's picker.
  return child_picker_->Pick(args);
}

//
// EdsLb::Helper
//

RefCountedPtr<SubchannelInterface> EdsLb::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (eds_policy_->shutting_down_) return nullptr;
  return eds_policy_->channel_control_helper()->CreateSubchannel(args);
}

void EdsLb::Helper::UpdateState(grpc_connectivity_state state,
                                std::unique_ptr<SubchannelPicker> picker) {
  if (eds_policy_->shutting_down_ || eds_policy_->child_policy_ == nullptr) {
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] child policy updated state=%s picker=%p",
            eds_policy_.get(), ConnectivityStateName(state), picker.get());
  }
  // Save the state and picker.
  eds_policy_->child_state_ = state;
  eds_policy_->child_picker_ =
      MakeRefCounted<ChildPickerWrapper>(std::move(picker));
  // Wrap the picker in a DropPicker and pass it up.
  eds_policy_->MaybeUpdateDropPickerLocked();
}

void EdsLb::Helper::AddTraceEvent(TraceSeverity severity,
                                  absl::string_view message) {
  if (eds_policy_->shutting_down_) return;
  eds_policy_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// EdsLb::EndpointWatcher
//

class EdsLb::EndpointWatcher : public XdsClient::EndpointWatcherInterface {
 public:
  explicit EndpointWatcher(RefCountedPtr<EdsLb> eds_policy)
      : eds_policy_(std::move(eds_policy)) {}

  ~EndpointWatcher() { eds_policy_.reset(DEBUG_LOCATION, "EndpointWatcher"); }

  void OnEndpointChanged(XdsApi::EdsUpdate update) override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
      gpr_log(GPR_INFO, "[edslb %p] Received EDS update from xds client",
              eds_policy_.get());
    }
    // Update the drop config.
    const bool drop_config_changed =
        eds_policy_->drop_config_ == nullptr ||
        *eds_policy_->drop_config_ != *update.drop_config;
    if (drop_config_changed) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
        gpr_log(GPR_INFO, "[edslb %p] Updating drop config", eds_policy_.get());
      }
      eds_policy_->drop_config_ = std::move(update.drop_config);
      eds_policy_->MaybeUpdateDropPickerLocked();
    } else if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
      gpr_log(GPR_INFO, "[edslb %p] Drop config unchanged, ignoring",
              eds_policy_.get());
    }
    // Update priority and locality info.
    if (eds_policy_->child_policy_ == nullptr ||
        eds_policy_->priority_list_update_ != update.priority_list_update) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
        gpr_log(GPR_INFO, "[edslb %p] Updating priority list",
                eds_policy_.get());
      }
      eds_policy_->UpdatePriorityList(std::move(update.priority_list_update));
    } else if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
      gpr_log(GPR_INFO, "[edslb %p] Priority list unchanged, ignoring",
              eds_policy_.get());
    }
  }

  void OnError(grpc_error* error) override {
    gpr_log(GPR_ERROR, "[edslb %p] xds watcher reported error: %s",
            eds_policy_.get(), grpc_error_string(error));
    // Go into TRANSIENT_FAILURE if we have not yet created the child
    // policy (i.e., we have not yet received data from xds).  Otherwise,
    // we keep running with the data we had previously.
    if (eds_policy_->child_policy_ == nullptr) {
      eds_policy_->channel_control_helper()->UpdateState(
          GRPC_CHANNEL_TRANSIENT_FAILURE,
          absl::make_unique<TransientFailurePicker>(error));
    } else {
      GRPC_ERROR_UNREF(error);
    }
  }

  void OnResourceDoesNotExist() override {
    gpr_log(
        GPR_ERROR,
        "[edslb %p] EDS resource does not exist -- reporting TRANSIENT_FAILURE",
        eds_policy_.get());
    eds_policy_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        absl::make_unique<TransientFailurePicker>(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "EDS resource does not exist")));
    eds_policy_->MaybeDestroyChildPolicyLocked();
  }

 private:
  RefCountedPtr<EdsLb> eds_policy_;
};

//
// EdsLb public methods
//

EdsLb::EdsLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      xds_client_from_channel_(XdsClient::GetFromChannelArgs(*args.args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] created -- xds client from channel: %p", this,
            xds_client_from_channel_.get());
  }
  // Record server name.
  const grpc_arg* arg = grpc_channel_args_find(args.args, GRPC_ARG_SERVER_URI);
  const char* server_uri = grpc_channel_arg_get_string(arg);
  GPR_ASSERT(server_uri != nullptr);
  grpc_uri* uri = grpc_uri_parse(server_uri, true);
  GPR_ASSERT(uri->path[0] != '\0');
  server_name_ = uri->path[0] == '/' ? uri->path + 1 : uri->path;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] server name from channel: %s", this,
            server_name_.c_str());
  }
  grpc_uri_destroy(uri);
}

EdsLb::~EdsLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] destroying xds LB policy", this);
  }
  grpc_channel_args_destroy(args_);
}

void EdsLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] shutting down", this);
  }
  shutting_down_ = true;
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  child_picker_.reset();
  MaybeDestroyChildPolicyLocked();
  drop_stats_.reset();
  // Cancel the endpoint watch here instead of in our dtor if we are using the
  // xds resolver, because the watcher holds a ref to us and we might not be
  // destroying the XdsClient, leading to a situation where this LB policy is
  // never destroyed.
  if (xds_client_from_channel_ != nullptr) {
    if (config_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
        gpr_log(GPR_INFO, "[edslb %p] cancelling xds watch for %s", this,
                std::string(GetEdsResourceName()).c_str());
      }
      xds_client()->CancelEndpointDataWatch(GetEdsResourceName(),
                                            endpoint_watcher_);
    }
    xds_client_from_channel_.reset();
  }
  xds_client_.reset();
}

void EdsLb::MaybeDestroyChildPolicyLocked() {
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
}

void EdsLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] Received update", this);
  }
  const bool is_initial_update = args_ == nullptr;
  // Update config.
  auto old_config = std::move(config_);
  config_ = std::move(args.config);
  // Update args.
  grpc_channel_args_destroy(args_);
  args_ = args.args;
  args.args = nullptr;
  if (is_initial_update) {
    // Initialize XdsClient.
    if (xds_client_from_channel_ == nullptr) {
      grpc_error* error = GRPC_ERROR_NONE;
      xds_client_ = MakeOrphanable<XdsClient>(
          work_serializer(), interested_parties(), GetEdsResourceName(),
          nullptr /* service config watcher */, *args_, &error);
      // TODO(roth): If we decide that we care about EDS-only mode, add
      // proper error handling here.
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
        gpr_log(GPR_INFO, "[edslb %p] Created xds client %p", this,
                xds_client_.get());
      }
    }
  }
  // Update drop stats for load reporting if needed.
  if (is_initial_update || config_->lrs_load_reporting_server_name() !=
                               old_config->lrs_load_reporting_server_name()) {
    drop_stats_.reset();
    if (config_->lrs_load_reporting_server_name().has_value()) {
      const auto key = GetLrsClusterKey();
      drop_stats_ = xds_client()->AddClusterDropStats(
          config_->lrs_load_reporting_server_name().value(),
          key.first /*cluster_name*/, key.second /*eds_service_name*/);
    }
    MaybeUpdateDropPickerLocked();
  }
  // Update child policy if needed.
  // Note that this comes after updating drop_stats_, since we want that
  // to be used by any new picker we create here.
  if (child_policy_ != nullptr) UpdateChildPolicyLocked();
  // Create endpoint watcher if needed.
  if (is_initial_update) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
      gpr_log(GPR_INFO, "[edslb %p] starting xds watch for %s", this,
              std::string(GetEdsResourceName()).c_str());
    }
    auto watcher = absl::make_unique<EndpointWatcher>(
        Ref(DEBUG_LOCATION, "EndpointWatcher"));
    endpoint_watcher_ = watcher.get();
    xds_client()->WatchEndpointData(GetEdsResourceName(), std::move(watcher));
  }
}

void EdsLb::ResetBackoffLocked() {
  // When the XdsClient is instantiated in the resolver instead of in this
  // LB policy, this is done via the resolver, so we don't need to do it
  // for xds_client_from_channel_ here.
  if (xds_client_ != nullptr) xds_client_->ResetBackoff();
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
  }
}

//
// child policy-related methods
//

void EdsLb::UpdatePriorityList(
    XdsApi::PriorityListUpdate priority_list_update) {
  // Build some maps from locality to child number and the reverse from
  // the old data in priority_list_update_ and priority_child_numbers_.
  std::map<XdsLocalityName*, size_t /*child_number*/, XdsLocalityName::Less>
      locality_child_map;
  std::map<size_t, std::set<XdsLocalityName*>> child_locality_map;
  for (uint32_t priority = 0; priority < priority_list_update_.size();
       ++priority) {
    auto* locality_map = priority_list_update_.Find(priority);
    GPR_ASSERT(locality_map != nullptr);
    size_t child_number = priority_child_numbers_[priority];
    for (const auto& p : locality_map->localities) {
      XdsLocalityName* locality_name = p.first.get();
      locality_child_map[locality_name] = child_number;
      child_locality_map[child_number].insert(locality_name);
    }
  }
  // Construct new list of children.
  std::vector<size_t> priority_child_numbers;
  for (uint32_t priority = 0; priority < priority_list_update.size();
       ++priority) {
    auto* locality_map = priority_list_update.Find(priority);
    GPR_ASSERT(locality_map != nullptr);
    absl::optional<size_t> child_number;
    // If one of the localities in this priority already existed, reuse its
    // child number.
    for (const auto& p : locality_map->localities) {
      XdsLocalityName* locality_name = p.first.get();
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
           ++(*child_number))
        ;
      // Add entry so we know that the child number is in use.
      // (Don't need to add the list of localities, since we won't use them.)
      child_locality_map[*child_number];
    }
    priority_child_numbers.push_back(*child_number);
  }
  // Save update.
  priority_list_update_ = std::move(priority_list_update);
  priority_child_numbers_ = std::move(priority_child_numbers);
  // Update child policy.
  UpdateChildPolicyLocked();
}

ServerAddressList EdsLb::CreateChildPolicyAddressesLocked() {
  ServerAddressList addresses;
  for (uint32_t priority = 0; priority < priority_list_update_.size();
       ++priority) {
    std::string priority_child_name =
        absl::StrCat("child", priority_child_numbers_[priority]);
    const auto* locality_map = priority_list_update_.Find(priority);
    GPR_ASSERT(locality_map != nullptr);
    for (const auto& p : locality_map->localities) {
      const auto& locality_name = p.first;
      const auto& locality = p.second;
      std::vector<std::string> hierarchical_path = {
          priority_child_name, locality_name->AsHumanReadableString()};
      for (size_t i = 0; i < locality.serverlist.size(); ++i) {
        const ServerAddress& address = locality.serverlist[i];
        grpc_arg new_arg = MakeHierarchicalPathArg(hierarchical_path);
        grpc_channel_args* args =
            grpc_channel_args_copy_and_add(address.args(), &new_arg, 1);
        addresses.emplace_back(address.address(), args);
      }
    }
  }
  return addresses;
}

RefCountedPtr<LoadBalancingPolicy::Config>
EdsLb::CreateChildPolicyConfigLocked() {
  Json::Object priority_children;
  Json::Array priority_priorities;
  for (uint32_t priority = 0; priority < priority_list_update_.size();
       ++priority) {
    const auto* locality_map = priority_list_update_.Find(priority);
    GPR_ASSERT(locality_map != nullptr);
    Json::Object weighted_targets;
    for (const auto& p : locality_map->localities) {
      XdsLocalityName* locality_name = p.first.get();
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
      // Construct endpoint-picking policy.
      // Wrap it in the LRS policy if load reporting is enabled.
      Json endpoint_picking_policy;
      if (config_->lrs_load_reporting_server_name().has_value()) {
        const auto key = GetLrsClusterKey();
        Json::Object lrs_config = {
            {"clusterName", std::string(key.first)},
            {"locality", std::move(locality_name_json)},
            {"lrsLoadReportingServerName",
             config_->lrs_load_reporting_server_name().value()},
            {"childPolicy", config_->endpoint_picking_policy()},
        };
        if (!key.second.empty()) {
          lrs_config["edsServiceName"] = std::string(key.second);
        }
        endpoint_picking_policy = Json::Array{Json::Object{
            {"lrs_experimental", std::move(lrs_config)},
        }};
      } else {
        endpoint_picking_policy = config_->endpoint_picking_policy();
      }
      // Add weighted target entry.
      weighted_targets[locality_name->AsHumanReadableString()] = Json::Object{
          {"weight", locality.lb_weight},
          {"childPolicy", std::move(endpoint_picking_policy)},
      };
    }
    // Add priority entry.
    const size_t child_number = priority_child_numbers_[priority];
    std::string child_name = absl::StrCat("child", child_number);
    priority_priorities.emplace_back(child_name);
    Json locality_picking_config = config_->locality_picking_policy();
    Json::Object& config =
        *(*locality_picking_config.mutable_array())[0].mutable_object();
    auto it = config.begin();
    GPR_ASSERT(it != config.end());
    (*it->second.mutable_object())["targets"] = std::move(weighted_targets);
    priority_children[child_name] = Json::Object{
        {"config", std::move(locality_picking_config)},
    };
  }
  Json json = Json::Array{Json::Object{
      {"priority_experimental",
       Json::Object{
           {"children", std::move(priority_children)},
           {"priorities", std::move(priority_priorities)},
       }},
  }};
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    std::string json_str = json.Dump(/*indent=*/1);
    gpr_log(GPR_INFO, "[edslb %p] generated config for child policy: %s", this,
            json_str.c_str());
  }
  grpc_error* error = GRPC_ERROR_NONE;
  RefCountedPtr<LoadBalancingPolicy::Config> config =
      LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(json, &error);
  if (error != GRPC_ERROR_NONE) {
    // This should never happen, but if it does, we basically have no
    // way to fix it, so we put the channel in TRANSIENT_FAILURE.
    gpr_log(GPR_ERROR,
            "[edslb %p] error parsing generated child policy config -- "
            "will put channel in TRANSIENT_FAILURE: %s",
            this, grpc_error_string(error));
    error = grpc_error_set_int(
        grpc_error_add_child(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "eds LB policy: error parsing generated child policy config"),
            error),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_INTERNAL);
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        absl::make_unique<TransientFailurePicker>(error));
    return nullptr;
  }
  return config;
}

void EdsLb::UpdateChildPolicyLocked() {
  if (shutting_down_) return;
  UpdateArgs update_args;
  update_args.config = CreateChildPolicyConfigLocked();
  if (update_args.config == nullptr) return;
  update_args.addresses = CreateChildPolicyAddressesLocked();
  update_args.args = CreateChildPolicyArgsLocked(args_);
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(update_args.args);
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] Updating child policy %p", this,
            child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

grpc_channel_args* EdsLb::CreateChildPolicyArgsLocked(
    const grpc_channel_args* args) {
  absl::InlinedVector<grpc_arg, 3> args_to_add = {
      // A channel arg indicating if the target is a backend inferred from an
      // xds load balancer.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_IS_BACKEND_FROM_XDS_LOAD_BALANCER),
          1),
      // Inhibit client-side health checking, since the balancer does
      // this for us.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_INHIBIT_HEALTH_CHECKING), 1),
  };
  absl::InlinedVector<const char*, 1> args_to_remove;
  if (xds_client_from_channel_ == nullptr) {
    args_to_add.emplace_back(xds_client_->MakeChannelArg());
  } else if (!config_->lrs_load_reporting_server_name().has_value()) {
    // Remove XdsClient from channel args, so that its presence doesn't
    // prevent us from sharing subchannels between channels.
    // If load reporting is enabled, this happens in the LRS policy instead.
    args_to_remove.push_back(GRPC_ARG_XDS_CLIENT);
  }
  return grpc_channel_args_copy_and_add_and_remove(
      args, args_to_remove.data(), args_to_remove.size(), args_to_add.data(),
      args_to_add.size());
}

OrphanablePtr<LoadBalancingPolicy> EdsLb::CreateChildPolicyLocked(
    const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          "priority_experimental", std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR, "[edslb %p] failure creating child policy", this);
    return nullptr;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p]: Created new child policy %p", this,
            lb_policy.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void EdsLb::MaybeUpdateDropPickerLocked() {
  // If we're dropping all calls, report READY, regardless of what (or
  // whether) the child has reported.
  if (drop_config_ != nullptr && drop_config_->drop_all()) {
    channel_control_helper()->UpdateState(GRPC_CHANNEL_READY,
                                          absl::make_unique<DropPicker>(this));
    return;
  }
  // Update only if we have a child picker.
  if (child_picker_ != nullptr) {
    channel_control_helper()->UpdateState(child_state_,
                                          absl::make_unique<DropPicker>(this));
  }
}

//
// factory
//

class EdsLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<EdsChildHandler>(std::move(args), &grpc_lb_eds_trace);
  }

  const char* name() const override { return kEds; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // eds was mentioned as a policy in the deprecated loadBalancingPolicy
      // field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:eds policy requires configuration. "
          "Please use loadBalancingConfig field of service config instead.");
      return nullptr;
    }
    std::vector<grpc_error*> error_list;
    // EDS service name.
    std::string eds_service_name;
    auto it = json.object_value().find("edsServiceName");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:edsServiceName error:type should be string"));
      } else {
        eds_service_name = it->second.string_value();
      }
    }
    // Cluster name.
    std::string cluster_name;
    it = json.object_value().find("clusterName");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:clusterName error:required field missing"));
    } else if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:clusterName error:type should be string"));
    } else {
      cluster_name = it->second.string_value();
    }
    // LRS load reporting server name.
    absl::optional<std::string> lrs_load_reporting_server_name;
    it = json.object_value().find("lrsLoadReportingServerName");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:lrsLoadReportingServerName error:type should be string"));
      } else {
        lrs_load_reporting_server_name.emplace(it->second.string_value());
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
    if (error_list.empty()) {
      return MakeRefCounted<EdsLbConfig>(
          std::move(cluster_name), std::move(eds_service_name),
          std::move(lrs_load_reporting_server_name),
          std::move(locality_picking_policy),
          std::move(endpoint_picking_policy));
    } else {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR(
          "eds_experimental LB policy config", &error_list);
      return nullptr;
    }
  }

 private:
  class EdsChildHandler : public ChildPolicyHandler {
   public:
    EdsChildHandler(Args args, TraceFlag* tracer)
        : ChildPolicyHandler(std::move(args), tracer) {}

    bool ConfigChangeRequiresNewPolicyInstance(
        LoadBalancingPolicy::Config* old_config,
        LoadBalancingPolicy::Config* new_config) const override {
      GPR_ASSERT(old_config->name() == kEds);
      GPR_ASSERT(new_config->name() == kEds);
      EdsLbConfig* old_eds_config = static_cast<EdsLbConfig*>(old_config);
      EdsLbConfig* new_eds_config = static_cast<EdsLbConfig*>(new_config);
      return old_eds_config->cluster_name() != new_eds_config->cluster_name() ||
             old_eds_config->eds_service_name() !=
                 new_eds_config->eds_service_name();
    }

    OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
        const char* name, LoadBalancingPolicy::Args args) const override {
      return MakeOrphanable<EdsLb>(std::move(args));
    }
  };
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_lb_policy_eds_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::EdsLbFactory>());
}

void grpc_lb_policy_eds_shutdown() {}
