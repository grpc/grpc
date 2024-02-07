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

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/client_channel/client_channel_internal.h"
#include "src/core/load_balancing/backend_metric_data.h"
#include "src/core/load_balancing/child_policy_handler.h"
#include "src/core/load_balancing/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_client_grpc.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/ext/xds/xds_endpoint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/security/credentials/xds/xds_credentials.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/subchannel_interface.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/xds/xds_dependency_manager.h"

namespace grpc_core {

TraceFlag grpc_xds_cluster_impl_lb_trace(false, "xds_cluster_impl_lb");

namespace {

using OptionalLabelComponent =
    ClientCallTracer::CallAttemptTracer::OptionalLabelComponent;
using XdsConfig = XdsDependencyManager::XdsConfig;

//
// global circuit breaker atomic map
//

class CircuitBreakerCallCounterMap {
 public:
  using Key =
      std::pair<std::string /*cluster*/, std::string /*eds_service_name*/>;

  class CallCounter : public RefCounted<CallCounter> {
   public:
    explicit CallCounter(Key key) : key_(std::move(key)) {}
    ~CallCounter() override;

    uint32_t Load() {
      return concurrent_requests_.load(std::memory_order_seq_cst);
    }
    uint32_t Increment() { return concurrent_requests_.fetch_add(1); }
    void Decrement() { concurrent_requests_.fetch_sub(1); }

   private:
    Key key_;
    std::atomic<uint32_t> concurrent_requests_{0};
  };

  RefCountedPtr<CallCounter> GetOrCreate(const std::string& cluster,
                                         const std::string& eds_service_name);

 private:
  Mutex mu_;
  std::map<Key, CallCounter*> map_ ABSL_GUARDED_BY(mu_);
};

CircuitBreakerCallCounterMap* const g_call_counter_map =
    new CircuitBreakerCallCounterMap;

RefCountedPtr<CircuitBreakerCallCounterMap::CallCounter>
CircuitBreakerCallCounterMap::GetOrCreate(const std::string& cluster,
                                          const std::string& eds_service_name) {
  Key key(cluster, eds_service_name);
  RefCountedPtr<CallCounter> result;
  MutexLock lock(&mu_);
  auto it = map_.find(key);
  if (it == map_.end()) {
    it = map_.insert({key, nullptr}).first;
  } else {
    result = it->second->RefIfNonZero();
  }
  if (result == nullptr) {
    result = MakeRefCounted<CallCounter>(std::move(key));
    it->second = result.get();
  }
  return result;
}

CircuitBreakerCallCounterMap::CallCounter::~CallCounter() {
  MutexLock lock(&g_call_counter_map->mu_);
  auto it = g_call_counter_map->map_.find(key_);
  if (it != g_call_counter_map->map_.end() && it->second == this) {
    g_call_counter_map->map_.erase(it);
  }
}

//
// LB policy
//

constexpr absl::string_view kXdsClusterImpl = "xds_cluster_impl_experimental";

// Config for xDS Cluster Impl LB policy.
class XdsClusterImplLbConfig : public LoadBalancingPolicy::Config {
 public:
  XdsClusterImplLbConfig() = default;

  XdsClusterImplLbConfig(const XdsClusterImplLbConfig&) = delete;
  XdsClusterImplLbConfig& operator=(const XdsClusterImplLbConfig&) = delete;

  XdsClusterImplLbConfig(XdsClusterImplLbConfig&& other) = delete;
  XdsClusterImplLbConfig& operator=(XdsClusterImplLbConfig&& other) = delete;

  absl::string_view name() const override { return kXdsClusterImpl; }

  const std::string& cluster_name() const { return cluster_name_; }
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
  void JsonPostLoad(const Json& json, const JsonArgs& args,
                    ValidationErrors* errors);

 private:
  std::string cluster_name_;
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
};

// xDS Cluster Impl LB policy.
class XdsClusterImplLb : public LoadBalancingPolicy {
 public:
  XdsClusterImplLb(RefCountedPtr<GrpcXdsClient> xds_client, Args args);

  absl::string_view name() const override { return kXdsClusterImpl; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  class StatsSubchannelWrapper : public DelegatingSubchannel {
   public:
    StatsSubchannelWrapper(
        RefCountedPtr<SubchannelInterface> wrapped_subchannel,
        RefCountedPtr<XdsClusterLocalityStats> locality_stats)
        : DelegatingSubchannel(std::move(wrapped_subchannel)),
          locality_stats_(std::move(locality_stats)) {}

    XdsClusterLocalityStats* locality_stats() const {
      return locality_stats_.get();
    }

   private:
    RefCountedPtr<XdsClusterLocalityStats> locality_stats_;
  };

  // A picker that wraps the picker from the child to perform drops.
  class Picker : public SubchannelPicker {
   public:
    Picker(XdsClusterImplLb* xds_cluster_impl_lb,
           RefCountedPtr<SubchannelPicker> picker);

    PickResult Pick(PickArgs args) override;

   private:
    class SubchannelCallTracker;

    RefCountedPtr<CircuitBreakerCallCounterMap::CallCounter> call_counter_;
    uint32_t max_concurrent_requests_;
    std::shared_ptr<std::map<std::string, std::string>> service_labels_;
    RefCountedPtr<XdsEndpointResource::DropConfig> drop_config_;
    RefCountedPtr<XdsClusterDropStats> drop_stats_;
    RefCountedPtr<SubchannelPicker> picker_;
  };

  class Helper
      : public ParentOwningDelegatingChannelControlHelper<XdsClusterImplLb> {
   public:
    explicit Helper(RefCountedPtr<XdsClusterImplLb> xds_cluster_impl_policy)
        : ParentOwningDelegatingChannelControlHelper(
              std::move(xds_cluster_impl_policy)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_resolved_address& address,
        const ChannelArgs& per_address_args, const ChannelArgs& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override;
  };

  ~XdsClusterImplLb() override;

  void ShutdownLocked() override;

  void ResetState();
  void ReportTransientFailure(absl::Status status);

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const ChannelArgs& args);
  absl::Status UpdateChildPolicyLocked(
      absl::StatusOr<std::shared_ptr<EndpointAddressesIterator>> addresses,
      std::string resolution_note, const ChannelArgs& args);

  absl::StatusOr<RefCountedPtr<XdsCertificateProvider>>
  MaybeCreateCertificateProviderLocked(
      const XdsClusterResource& cluster_resource) const;

  void MaybeUpdatePickerLocked();

  // Current config from the resolver.
  RefCountedPtr<XdsClusterImplLbConfig> config_;
  std::shared_ptr<const XdsClusterResource> cluster_resource_;
  RefCountedPtr<XdsEndpointResource::DropConfig> drop_config_;

  // Current concurrent number of requests.
  RefCountedPtr<CircuitBreakerCallCounterMap::CallCounter> call_counter_;

  // Internal state.
  bool shutting_down_ = false;

  // The xds client.
  RefCountedPtr<GrpcXdsClient> xds_client_;

  // The stats for client-side load reporting.
  RefCountedPtr<XdsClusterDropStats> drop_stats_;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Latest state and picker reported by the child policy.
  grpc_connectivity_state state_ = GRPC_CHANNEL_IDLE;
  absl::Status status_;
  RefCountedPtr<SubchannelPicker> picker_;
};

//
// XdsClusterImplLb::Picker::SubchannelCallTracker
//

class XdsClusterImplLb::Picker::SubchannelCallTracker
    : public LoadBalancingPolicy::SubchannelCallTrackerInterface {
 public:
  SubchannelCallTracker(
      std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>
          original_subchannel_call_tracker,
      RefCountedPtr<XdsClusterLocalityStats> locality_stats,
      RefCountedPtr<CircuitBreakerCallCounterMap::CallCounter> call_counter)
      : original_subchannel_call_tracker_(
            std::move(original_subchannel_call_tracker)),
        locality_stats_(std::move(locality_stats)),
        call_counter_(std::move(call_counter)) {}

  ~SubchannelCallTracker() override {
    locality_stats_.reset(DEBUG_LOCATION, "SubchannelCallTracker");
    call_counter_.reset(DEBUG_LOCATION, "SubchannelCallTracker");
    GPR_DEBUG_ASSERT(!started_);
  }

  void Start() override {
    // Increment number of calls in flight.
    call_counter_->Increment();
    // Record a call started.
    if (locality_stats_ != nullptr) {
      locality_stats_->AddCallStarted();
    }
    // Delegate if needed.
    if (original_subchannel_call_tracker_ != nullptr) {
      original_subchannel_call_tracker_->Start();
    }
#ifndef NDEBUG
    started_ = true;
#endif
  }

  void Finish(FinishArgs args) override {
    // Delegate if needed.
    if (original_subchannel_call_tracker_ != nullptr) {
      original_subchannel_call_tracker_->Finish(args);
    }
    // Record call completion for load reporting.
    if (locality_stats_ != nullptr) {
      auto* backend_metric_data =
          args.backend_metric_accessor->GetBackendMetricData();
      const std::map<absl::string_view, double>* named_metrics = nullptr;
      if (backend_metric_data != nullptr) {
        named_metrics = &backend_metric_data->named_metrics;
      }
      locality_stats_->AddCallFinished(named_metrics, !args.status.ok());
    }
    // Decrement number of calls in flight.
    call_counter_->Decrement();
#ifndef NDEBUG
    started_ = false;
#endif
  }

 private:
  std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>
      original_subchannel_call_tracker_;
  RefCountedPtr<XdsClusterLocalityStats> locality_stats_;
  RefCountedPtr<CircuitBreakerCallCounterMap::CallCounter> call_counter_;
#ifndef NDEBUG
  bool started_ = false;
#endif
};

//
// XdsClusterImplLb::Picker
//

XdsClusterImplLb::Picker::Picker(XdsClusterImplLb* xds_cluster_impl_lb,
                                 RefCountedPtr<SubchannelPicker> picker)
    : call_counter_(xds_cluster_impl_lb->call_counter_),
      max_concurrent_requests_(
          xds_cluster_impl_lb->cluster_resource_->max_concurrent_requests),
      service_labels_(xds_cluster_impl_lb->cluster_resource_->telemetry_labels),
      drop_config_(xds_cluster_impl_lb->drop_config_),
      drop_stats_(xds_cluster_impl_lb->drop_stats_),
      picker_(std::move(picker)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_impl_lb %p] constructed new picker %p",
            xds_cluster_impl_lb, this);
  }
}

LoadBalancingPolicy::PickResult XdsClusterImplLb::Picker::Pick(
    LoadBalancingPolicy::PickArgs args) {
  auto* call_state = static_cast<ClientChannelLbCallState*>(args.call_state);
  if (call_state->GetCallAttemptTracer() != nullptr) {
    call_state->GetCallAttemptTracer()->AddOptionalLabels(
        OptionalLabelComponent::kXdsServiceLabels, service_labels_);
  }
  // Handle EDS drops.
  const std::string* drop_category;
  if (drop_config_ != nullptr && drop_config_->ShouldDrop(&drop_category)) {
    if (drop_stats_ != nullptr) drop_stats_->AddCallDropped(*drop_category);
    return PickResult::Drop(absl::UnavailableError(
        absl::StrCat("EDS-configured drop: ", *drop_category)));
  }
  // Check if we exceeded the max concurrent requests circuit breaking limit.
  // Note: We check the value here, but we don't actually increment the
  // counter for the current request until the channel calls the subchannel
  // call tracker's Start() method.  This means that we may wind up
  // allowing more concurrent requests than the configured limit.
  if (call_counter_->Load() >= max_concurrent_requests_) {
    if (drop_stats_ != nullptr) drop_stats_->AddUncategorizedDrops();
    return PickResult::Drop(absl::UnavailableError("circuit breaker drop"));
  }
  // If we're not dropping the call, we should always have a child picker.
  if (picker_ == nullptr) {  // Should never happen.
    return PickResult::Fail(absl::InternalError(
        "xds_cluster_impl picker not given any child picker"));
  }
  // Not dropping, so delegate to child picker.
  PickResult result = picker_->Pick(args);
  auto* complete_pick = absl::get_if<PickResult::Complete>(&result.result);
  if (complete_pick != nullptr) {
    RefCountedPtr<XdsClusterLocalityStats> locality_stats;
    if (drop_stats_ != nullptr) {  // If load reporting is enabled.
      auto* subchannel_wrapper =
          static_cast<StatsSubchannelWrapper*>(complete_pick->subchannel.get());
      // Handle load reporting.
      locality_stats = subchannel_wrapper->locality_stats()->Ref(
          DEBUG_LOCATION, "SubchannelCallTracker");
      // Unwrap subchannel to pass back up the stack.
      complete_pick->subchannel = subchannel_wrapper->wrapped_subchannel();
    }
    // Inject subchannel call tracker to record call completion.
    complete_pick->subchannel_call_tracker =
        std::make_unique<SubchannelCallTracker>(
            std::move(complete_pick->subchannel_call_tracker),
            std::move(locality_stats),
            call_counter_->Ref(DEBUG_LOCATION, "SubchannelCallTracker"));
  } else {
    // TODO(roth): We should ideally also record call failures here in the case
    // where a pick fails.  This is challenging, because we don't know which
    // picks are for wait_for_ready RPCs or how many times we'll return a
    // failure for the same wait_for_ready RPC.
  }
  return result;
}

//
// XdsClusterImplLb
//

XdsClusterImplLb::XdsClusterImplLb(RefCountedPtr<GrpcXdsClient> xds_client,
                                   Args args)
    : LoadBalancingPolicy(std::move(args)), xds_client_(std::move(xds_client)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_impl_lb %p] created -- using xds client %p",
            this, xds_client_.get());
  }
}

XdsClusterImplLb::~XdsClusterImplLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_impl_lb %p] destroying xds_cluster_impl LB policy",
            this);
  }
}

void XdsClusterImplLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_impl_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  ResetState();
  xds_client_.reset(DEBUG_LOCATION, "XdsClusterImpl");
}

void XdsClusterImplLb::ResetState() {
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_.reset();
  drop_stats_.reset();
}

void XdsClusterImplLb::ReportTransientFailure(absl::Status status) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_impl_lb %p] reporting TRANSIENT_FAILURE: %s", this,
            status.ToString().c_str());
  }
  ResetState();
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_TRANSIENT_FAILURE, status,
      MakeRefCounted<TransientFailurePicker>(status));
}

void XdsClusterImplLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

void XdsClusterImplLb::ResetBackoffLocked() {
  // The XdsClient will have its backoff reset by the xds resolver, so we
  // don't need to do it here.
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

std::string GetEdsResourceName(const XdsClusterResource& cluster_resource) {
  auto* eds = absl::get_if<XdsClusterResource::Eds>(&cluster_resource.type);
  if (eds == nullptr) return "";
  return eds->eds_service_name;
}

absl::Status XdsClusterImplLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_impl_lb %p] Received update", this);
  }
  // Grab new LB policy config.
  auto new_config = args.config.TakeAsSubclass<XdsClusterImplLbConfig>();
  // Cluster name should never change, because the cds policy will assign a
  // different priority child name if that happens, which means that this
  // policy instance will get replaced instead of being updated.
  if (config_ != nullptr) {
    GPR_ASSERT(config_->cluster_name() == new_config->cluster_name());
  }
  // Get xDS config.
  auto new_xds_config = args.args.GetObjectRef<XdsConfig>();
  if (new_xds_config == nullptr) {
    // Should never happen.
    absl::Status status = absl::InternalError(
        "xDS config not passed to xds_cluster_impl LB policy");
    ReportTransientFailure(status);
    return status;
  }
  auto it = new_xds_config->clusters.find(new_config->cluster_name());
  if (it == new_xds_config->clusters.end() || !it->second.ok() ||
      it->second->cluster == nullptr) {
    // Should never happen.
    absl::Status status = absl::InternalError(absl::StrCat(
        "xDS config has no entry for cluster ", new_config->cluster_name()));
    ReportTransientFailure(status);
    return status;
  }
  auto& new_cluster_config = *it->second;
  auto* endpoint_config =
      absl::get_if<XdsConfig::ClusterConfig::EndpointConfig>(
          &new_cluster_config.children);
  if (endpoint_config == nullptr) {
    // Should never happen.
    absl::Status status = absl::InternalError(
        absl::StrCat("cluster config for ", new_config->cluster_name(),
                     " has no endpoint config"));
    ReportTransientFailure(status);
    return status;
  }
  auto xds_cert_provider =
      MaybeCreateCertificateProviderLocked(*new_cluster_config.cluster);
  if (!xds_cert_provider.ok()) {
    // Should never happen.
    ReportTransientFailure(xds_cert_provider.status());
    return xds_cert_provider.status();
  }
  if (*xds_cert_provider != nullptr) {
    args.args = args.args.SetObject(std::move(*xds_cert_provider));
  }
  // Now we've verified the new config is good.
  // Get new and old (if any) EDS service name.
  std::string new_eds_service_name =
      GetEdsResourceName(*new_cluster_config.cluster);
  std::string old_eds_service_name =
      cluster_resource_ == nullptr ? ""
                                   : GetEdsResourceName(*cluster_resource_);
  // Update drop stats if needed.
  // Note: We need a drop stats object whenever load reporting is enabled,
  // even if we have no EDS drop config, because we also use it when
  // reporting circuit breaker drops.
  if (!new_cluster_config.cluster->lrs_load_reporting_server.has_value()) {
    drop_stats_.reset();
  } else if (cluster_resource_ == nullptr ||
             old_eds_service_name != new_eds_service_name ||
             cluster_resource_->lrs_load_reporting_server !=
                 new_cluster_config.cluster->lrs_load_reporting_server) {
    drop_stats_ = xds_client_->AddClusterDropStats(
        *new_cluster_config.cluster->lrs_load_reporting_server,
        new_config->cluster_name(), new_eds_service_name);
    if (drop_stats_ == nullptr) {
      gpr_log(
          GPR_ERROR,
          "[xds_cluster_impl_lb %p] Failed to get cluster drop stats for "
          "LRS server %s, cluster %s, EDS service name %s, load "
          "reporting for drops will not be done.",
          this,
          new_cluster_config.cluster->lrs_load_reporting_server->server_uri()
              .c_str(),
          new_config->cluster_name().c_str(), new_eds_service_name.c_str());
    }
  }
  // Update call counter if needed.
  if (cluster_resource_ == nullptr ||
      old_eds_service_name != new_eds_service_name) {
    call_counter_ = g_call_counter_map->GetOrCreate(new_config->cluster_name(),
                                                    new_eds_service_name);
  }
  // Update config state, now that we're done comparing old and new fields.
  config_ = std::move(new_config);
  cluster_resource_ = new_cluster_config.cluster;
  drop_config_ = endpoint_config->endpoints != nullptr
                     ? endpoint_config->endpoints->drop_config
                     : nullptr;
  // Update picker in case some dependent config field changed.
  MaybeUpdatePickerLocked();
  // Update child policy.
  return UpdateChildPolicyLocked(std::move(args.addresses),
                                 std::move(args.resolution_note), args.args);
}

absl::StatusOr<RefCountedPtr<XdsCertificateProvider>>
XdsClusterImplLb::MaybeCreateCertificateProviderLocked(
    const XdsClusterResource& cluster_resource) const {
  // If the channel is not using XdsCreds, do nothing.
  auto channel_credentials = channel_control_helper()->GetChannelCredentials();
  if (channel_credentials == nullptr ||
      channel_credentials->type() != XdsCredentials::Type()) {
    return nullptr;
  }
  // Configure root cert.
  absl::string_view root_provider_instance_name =
      cluster_resource.common_tls_context.certificate_validation_context
          .ca_certificate_provider_instance.instance_name;
  absl::string_view root_cert_name =
      cluster_resource.common_tls_context.certificate_validation_context
          .ca_certificate_provider_instance.certificate_name;
  RefCountedPtr<grpc_tls_certificate_provider> root_cert_provider;
  if (!root_provider_instance_name.empty()) {
    root_cert_provider =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(root_provider_instance_name);
    if (root_cert_provider == nullptr) {
      return absl::InternalError(
          absl::StrCat("Certificate provider instance name: \"",
                       root_provider_instance_name, "\" not recognized."));
    }
  }
  // Configure identity cert.
  absl::string_view identity_provider_instance_name =
      cluster_resource.common_tls_context.tls_certificate_provider_instance
          .instance_name;
  absl::string_view identity_cert_name =
      cluster_resource.common_tls_context.tls_certificate_provider_instance
          .certificate_name;
  RefCountedPtr<grpc_tls_certificate_provider> identity_cert_provider;
  if (!identity_provider_instance_name.empty()) {
    identity_cert_provider =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(identity_provider_instance_name);
    if (identity_cert_provider == nullptr) {
      return absl::InternalError(
          absl::StrCat("Certificate provider instance name: \"",
                       identity_provider_instance_name, "\" not recognized."));
    }
  }
  // Configure SAN matchers.
  const std::vector<StringMatcher>& san_matchers =
      cluster_resource.common_tls_context.certificate_validation_context
          .match_subject_alt_names;
  // Create xds cert provider.
  return MakeRefCounted<XdsCertificateProvider>(
      root_cert_provider, root_cert_name, identity_cert_provider,
      identity_cert_name, san_matchers);
}

void XdsClusterImplLb::MaybeUpdatePickerLocked() {
  // If we're dropping all calls, report READY, regardless of what (or
  // whether) the child has reported.
  if (drop_config_ != nullptr && drop_config_->drop_all()) {
    auto drop_picker = MakeRefCounted<Picker>(this, picker_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
      gpr_log(GPR_INFO,
              "[xds_cluster_impl_lb %p] updating connectivity (drop all): "
              "state=READY picker=%p",
              this, drop_picker.get());
    }
    channel_control_helper()->UpdateState(GRPC_CHANNEL_READY, absl::Status(),
                                          std::move(drop_picker));
    return;
  }
  // Otherwise, update only if we have a child picker.
  if (picker_ != nullptr) {
    auto drop_picker = MakeRefCounted<Picker>(this, picker_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
      gpr_log(GPR_INFO,
              "[xds_cluster_impl_lb %p] updating connectivity: state=%s "
              "status=(%s) picker=%p",
              this, ConnectivityStateName(state_), status_.ToString().c_str(),
              drop_picker.get());
    }
    channel_control_helper()->UpdateState(state_, status_,
                                          std::move(drop_picker));
  }
}

OrphanablePtr<LoadBalancingPolicy> XdsClusterImplLb::CreateChildPolicyLocked(
    const ChannelArgs& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper = std::make_unique<Helper>(
      RefAsSubclass<XdsClusterImplLb>(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_xds_cluster_impl_lb_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_impl_lb %p] Created new child policy handler %p",
            this, lb_policy.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

absl::Status XdsClusterImplLb::UpdateChildPolicyLocked(
    absl::StatusOr<std::shared_ptr<EndpointAddressesIterator>> addresses,
    std::string resolution_note, const ChannelArgs& args) {
  // Create policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = std::move(addresses);
  update_args.resolution_note = std::move(resolution_note);
  update_args.config = config_->child_policy();
  update_args.args =
      args.Set(GRPC_ARG_XDS_CLUSTER_NAME, config_->cluster_name());
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_impl_lb %p] Updating child policy handler %p", this,
            child_policy_.get());
  }
  return child_policy_->UpdateLocked(std::move(update_args));
}

//
// XdsClusterImplLb::Helper
//

RefCountedPtr<SubchannelInterface> XdsClusterImplLb::Helper::CreateSubchannel(
    const grpc_resolved_address& address, const ChannelArgs& per_address_args,
    const ChannelArgs& args) {
  if (parent()->shutting_down_) return nullptr;
  // If load reporting is enabled, wrap the subchannel such that it
  // includes the locality stats object, which will be used by the Picker.
  if (parent()->cluster_resource_->lrs_load_reporting_server.has_value()) {
    auto locality_name = per_address_args.GetObjectRef<XdsLocalityName>();
    RefCountedPtr<XdsClusterLocalityStats> locality_stats =
        parent()->xds_client_->AddClusterLocalityStats(
            parent()->cluster_resource_->lrs_load_reporting_server.value(),
            parent()->config_->cluster_name(),
            GetEdsResourceName(*parent()->cluster_resource_),
            std::move(locality_name));
    if (locality_stats != nullptr) {
      return MakeRefCounted<StatsSubchannelWrapper>(
          parent()->channel_control_helper()->CreateSubchannel(
              address, per_address_args, args),
          std::move(locality_stats));
    }
    gpr_log(GPR_ERROR,
            "[xds_cluster_impl_lb %p] Failed to get locality stats object for "
            "LRS server %s, cluster %s, EDS service name %s; load reports will "
            "not be generated (not wrapping subchannel)",
            parent(),
            parent()
                ->cluster_resource_->lrs_load_reporting_server->server_uri()
                .c_str(),
            parent()->config_->cluster_name().c_str(),
            GetEdsResourceName(*parent()->cluster_resource_).c_str());
  }
  // Load reporting not enabled, so don't wrap the subchannel.
  return parent()->channel_control_helper()->CreateSubchannel(
      address, per_address_args, args);
}

void XdsClusterImplLb::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  if (parent()->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_impl_lb %p] child connectivity state update: "
            "state=%s (%s) picker=%p",
            parent(), ConnectivityStateName(state), status.ToString().c_str(),
            picker.get());
  }
  // Save the state and picker.
  parent()->state_ = state;
  parent()->status_ = status;
  parent()->picker_ = std::move(picker);
  // Wrap the picker and return it to the channel.
  parent()->MaybeUpdatePickerLocked();
}

//
// factory
//

const JsonLoaderInterface* XdsClusterImplLbConfig::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<XdsClusterImplLbConfig>()
          // Note: Some fields require custom processing, so they are
          // handled in JsonPostLoad() instead.
          .Field("clusterName", &XdsClusterImplLbConfig::cluster_name_)
          .Finish();
  return loader;
}

void XdsClusterImplLbConfig::JsonPostLoad(const Json& json, const JsonArgs&,
                                          ValidationErrors* errors) {
  // Parse "childPolicy" field.
  ValidationErrors::ScopedField field(errors, ".childPolicy");
  auto it = json.object().find("childPolicy");
  if (it == json.object().end()) {
    errors->AddError("field not present");
  } else {
    auto lb_config =
        CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
            it->second);
    if (!lb_config.ok()) {
      errors->AddError(lb_config.status().message());
    } else {
      child_policy_ = std::move(*lb_config);
    }
  }
}

class XdsClusterImplLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    auto xds_client = args.args.GetObjectRef<GrpcXdsClient>(DEBUG_LOCATION,
                                                            "XdsClusterImplLb");
    if (xds_client == nullptr) {
      gpr_log(GPR_ERROR,
              "XdsClient not present in channel args -- cannot instantiate "
              "xds_cluster_impl LB policy");
      return nullptr;
    }
    return MakeOrphanable<XdsClusterImplLb>(std::move(xds_client),
                                            std::move(args));
  }

  absl::string_view name() const override { return kXdsClusterImpl; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadFromJson<RefCountedPtr<XdsClusterImplLbConfig>>(
        json, JsonArgs(),
        "errors validating xds_cluster_impl LB policy config");
  }
};

}  // namespace

void RegisterXdsClusterImplLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<XdsClusterImplLbFactory>());
}

}  // namespace grpc_core
