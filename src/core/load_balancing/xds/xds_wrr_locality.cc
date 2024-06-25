//
// Copyright 2022 gRPC authors.
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

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/connectivity_state.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/ref_counted_string.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/xds/xds_channel_args.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/xds/xds_client/xds_client_stats.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kXdsWrrLocality = "xds_wrr_locality_experimental";

// Config for xds_wrr_locality LB policy.
class XdsWrrLocalityLbConfig final : public LoadBalancingPolicy::Config {
 public:
  XdsWrrLocalityLbConfig() = default;

  XdsWrrLocalityLbConfig(const XdsWrrLocalityLbConfig&) = delete;
  XdsWrrLocalityLbConfig& operator=(const XdsWrrLocalityLbConfig&) = delete;

  XdsWrrLocalityLbConfig(XdsWrrLocalityLbConfig&& other) = delete;
  XdsWrrLocalityLbConfig& operator=(XdsWrrLocalityLbConfig&& other) = delete;

  absl::string_view name() const override { return kXdsWrrLocality; }

  const Json& child_config() const { return child_config_; }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    // Note: The "childPolicy" field requires custom processing, so
    // it's handled in JsonPostLoad() instead.
    static const auto* loader =
        JsonObjectLoader<XdsWrrLocalityLbConfig>().Finish();
    return loader;
  }

  void JsonPostLoad(const Json& json, const JsonArgs&,
                    ValidationErrors* errors) {
    ValidationErrors::ScopedField field(errors, ".childPolicy");
    auto it = json.object().find("childPolicy");
    if (it == json.object().end()) {
      errors->AddError("field not present");
      return;
    }
    auto lb_config =
        CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
            it->second);
    if (!lb_config.ok()) {
      errors->AddError(lb_config.status().message());
      return;
    }
    child_config_ = it->second;
  }

 private:
  Json child_config_;
};

// xds_wrr_locality LB policy.
class XdsWrrLocalityLb final : public LoadBalancingPolicy {
 public:
  explicit XdsWrrLocalityLb(Args args);

  absl::string_view name() const override { return kXdsWrrLocality; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  using Helper = ParentOwningDelegatingChannelControlHelper<XdsWrrLocalityLb>;

  ~XdsWrrLocalityLb() override;

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const ChannelArgs& args);

  OrphanablePtr<LoadBalancingPolicy> child_policy_;
};

//
// XdsWrrLocalityLb
//

XdsWrrLocalityLb::XdsWrrLocalityLb(Args args)
    : LoadBalancingPolicy(std::move(args)) {}

XdsWrrLocalityLb::~XdsWrrLocalityLb() {
  if (GRPC_TRACE_FLAG_ENABLED(xds_wrr_locality_lb)) {
    LOG(INFO) << "[xds_wrr_locality_lb " << this << "] destroying";
  }
}

void XdsWrrLocalityLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(xds_wrr_locality_lb)) {
    LOG(INFO) << "[xds_wrr_locality_lb " << this << "] shutting down";
  }
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
}

void XdsWrrLocalityLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

void XdsWrrLocalityLb::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

absl::Status XdsWrrLocalityLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_wrr_locality_lb)) {
    LOG(INFO) << "[xds_wrr_locality_lb " << this << "] Received update";
  }
  auto config = args.config.TakeAsSubclass<XdsWrrLocalityLbConfig>();
  // Scan the addresses to find the weight for each locality.
  std::map<RefCountedStringValue, uint32_t> locality_weights;
  if (args.addresses.ok()) {
    (*args.addresses)->ForEach([&](const EndpointAddresses& endpoint) {
      auto* locality_name = endpoint.args().GetObject<XdsLocalityName>();
      uint32_t weight =
          endpoint.args().GetInt(GRPC_ARG_XDS_LOCALITY_WEIGHT).value_or(0);
      if (locality_name != nullptr && weight > 0) {
        auto p = locality_weights.emplace(
            locality_name->human_readable_string(), weight);
        if (!p.second && p.first->second != weight) {
          LOG(ERROR) << "INTERNAL ERROR: xds_wrr_locality found different "
                        "weights for locality "
                     << p.first->first.c_str() << " (" << p.first->second
                     << " vs " << weight << "); using first value";
        }
      }
    });
  }
  // Construct the config for the weighted_target policy.
  Json::Object weighted_targets;
  for (const auto& p : locality_weights) {
    absl::string_view locality_name = p.first.as_string_view();
    uint32_t weight = p.second;
    // Add weighted target entry.
    weighted_targets[std::string(locality_name)] = Json::FromObject({
        {"weight", Json::FromNumber(weight)},
        {"childPolicy", config->child_config()},
    });
  }
  Json child_config_json = Json::FromArray({
      Json::FromObject({
          {"weighted_target_experimental",
           Json::FromObject({
               {"targets", Json::FromObject(std::move(weighted_targets))},
           })},
      }),
  });
  if (GRPC_TRACE_FLAG_ENABLED(xds_wrr_locality_lb)) {
    LOG(INFO) << "[xds_wrr_locality_lb " << this
              << "] generated child policy config: "
              << JsonDump(child_config_json, /*indent=*/1);
  }
  // Parse config.
  auto child_config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          child_config_json);
  if (!child_config.ok()) {
    // This should never happen, but if it does, we basically have no
    // way to fix it, so we put the channel in TRANSIENT_FAILURE.
    LOG(ERROR) << "[xds_wrr_locality " << this
               << "] error parsing generated child policy config -- putting "
                  "channel in TRANSIENT_FAILURE: "
               << child_config.status();
    absl::Status status = absl::InternalError(absl::StrCat(
        "xds_wrr_locality LB policy: error parsing generated child policy "
        "config: ",
        child_config.status().ToString()));
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        MakeRefCounted<TransientFailurePicker>(status));
    return status;
  }
  // Create child policy if needed (i.e., on first update).
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args.args);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = std::move(args.addresses);
  update_args.config = std::move(*child_config);
  update_args.resolution_note = std::move(args.resolution_note);
  update_args.args = std::move(args.args);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(xds_wrr_locality_lb)) {
    LOG(INFO) << "[xds_wrr_locality_lb " << this << "] updating child policy "
              << child_policy_.get();
  }
  return child_policy_->UpdateLocked(std::move(update_args));
}

OrphanablePtr<LoadBalancingPolicy> XdsWrrLocalityLb::CreateChildPolicyLocked(
    const ChannelArgs& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper = std::make_unique<Helper>(
      RefAsSubclass<XdsWrrLocalityLb>(DEBUG_LOCATION, "Helper"));
  auto lb_policy =
      CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
          "weighted_target_experimental", std::move(lb_policy_args));
  if (GRPC_TRACE_FLAG_ENABLED(xds_wrr_locality_lb)) {
    LOG(INFO) << "[xds_wrr_locality_lb " << this
              << "] created new child policy " << lb_policy.get();
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this LB policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

//
// factory
//

class XdsWrrLocalityLbFactory final : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<XdsWrrLocalityLb>(std::move(args));
  }

  absl::string_view name() const override { return kXdsWrrLocality; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadFromJson<RefCountedPtr<XdsWrrLocalityLbConfig>>(
        json, JsonArgs(),
        "errors validating xds_wrr_locality LB policy config");
  }
};

}  // namespace

void RegisterXdsWrrLocalityLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<XdsWrrLocalityLbFactory>());
}

}  // namespace grpc_core
