//
// Copyright 2019 gRPC authors.
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

#ifndef GRPC_CORE_EXT_XDS_XDS_CLIENT_H
#define GRPC_CORE_EXT_XDS_XDS_CLIENT_H

#include <grpc/support/port_platform.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "upb/def.hpp"

#include <grpc/event_engine/event_engine.h>

#include "src/core/ext/xds/xds_api.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/ext/xds/xds_transport.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

extern TraceFlag grpc_xds_client_trace;
extern TraceFlag grpc_xds_client_refcount_trace;

class XdsClient : public DualRefCounted<XdsClient> {
 public:
  // Resource watcher interface.  Implemented by callers.
  // Note: Most callers will not use this API directly but rather via a
  // resource-type-specific wrapper API provided by the relevant
  // XdsResourceType implementation.
  class ResourceWatcherInterface : public RefCounted<ResourceWatcherInterface> {
   public:
    virtual void OnGenericResourceChanged(
        const XdsResourceType::ResourceData* resource)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) = 0;
    virtual void OnError(absl::Status status)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) = 0;
    virtual void OnResourceDoesNotExist()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) = 0;
  };

  XdsClient(std::unique_ptr<XdsBootstrap> bootstrap,
            OrphanablePtr<XdsTransportFactory> transport_factory,
            Duration resource_request_timeout = Duration::Seconds(15));
  ~XdsClient() override;

  const XdsBootstrap& bootstrap() const {
    return *bootstrap_;  // ctor asserts that it is non-null
  }

  XdsTransportFactory* transport_factory() const {
    return transport_factory_.get();
  }

  void Orphan() override;

  // Start and cancel watch for a resource.
  //
  // The XdsClient takes ownership of the watcher, but the caller may
  // keep a raw pointer to the watcher, which may be used only for
  // cancellation.  (Because the caller does not own the watcher, the
  // pointer must not be used for any other purpose.)
  // If the caller is going to start a new watch after cancelling the
  // old one, it should set delay_unsubscription to true.
  //
  // The resource type object must be a global singleton, since the first
  // time the XdsClient sees a particular resource type object, it will
  // store the pointer to that object as the authoritative implementation for
  // its type URLs.  The resource type object must outlive the XdsClient object,
  // and it is illegal to start a subsequent watch for the same type URLs using
  // a different resource type object.
  //
  // Note: Most callers will not use this API directly but rather via a
  // resource-type-specific wrapper API provided by the relevant
  // XdsResourceType implementation.
  void WatchResource(const XdsResourceType* type, absl::string_view name,
                     RefCountedPtr<ResourceWatcherInterface> watcher);
  void CancelResourceWatch(const XdsResourceType* type,
                           absl::string_view listener_name,
                           ResourceWatcherInterface* watcher,
                           bool delay_unsubscription = false);

  // Adds and removes drop stats for cluster_name and eds_service_name.
  RefCountedPtr<XdsClusterDropStats> AddClusterDropStats(
      const XdsBootstrap::XdsServer& xds_server, absl::string_view cluster_name,
      absl::string_view eds_service_name);
  void RemoveClusterDropStats(const XdsBootstrap::XdsServer& xds_server,
                              absl::string_view cluster_name,
                              absl::string_view eds_service_name,
                              XdsClusterDropStats* cluster_drop_stats);

  // Adds and removes locality stats for cluster_name and eds_service_name
  // for the specified locality.
  RefCountedPtr<XdsClusterLocalityStats> AddClusterLocalityStats(
      const XdsBootstrap::XdsServer& xds_server, absl::string_view cluster_name,
      absl::string_view eds_service_name,
      RefCountedPtr<XdsLocalityName> locality);
  void RemoveClusterLocalityStats(
      const XdsBootstrap::XdsServer& xds_server, absl::string_view cluster_name,
      absl::string_view eds_service_name,
      const RefCountedPtr<XdsLocalityName>& locality,
      XdsClusterLocalityStats* cluster_locality_stats);

  // Resets connection backoff state.
  void ResetBackoff();

  // Dumps the active xDS config in JSON format.
  // Individual xDS resource is encoded as envoy.admin.v3.*ConfigDump. Returns
  // envoy.service.status.v3.ClientConfig which also includes the config
  // status (e.g., CLIENT_REQUESTED, CLIENT_ACKED, CLIENT_NACKED).
  //
  // Expected to be invoked by wrapper languages in their CSDS service
  // implementation.
  std::string DumpClientConfigBinary();

  grpc_event_engine::experimental::EventEngine* engine() {
    return engine_.get();
  }

 private:
  struct XdsResourceKey {
    std::string id;
    std::vector<URI::QueryParam> query_params;

    bool operator<(const XdsResourceKey& other) const {
      int c = id.compare(other.id);
      if (c != 0) return c < 0;
      return query_params < other.query_params;
    }
  };

  struct XdsResourceName {
    std::string authority;
    XdsResourceKey key;
  };

  // Contains a channel to the xds server and all the data related to the
  // channel.  Holds a ref to the xds client object.
  class ChannelState : public DualRefCounted<ChannelState> {
   public:
    template <typename T>
    class RetryableCall;

    class AdsCallState;
    class LrsCallState;

    ChannelState(WeakRefCountedPtr<XdsClient> xds_client,
                 const XdsBootstrap::XdsServer& server);
    ~ChannelState() override;

    void Orphan() override;

    XdsClient* xds_client() const { return xds_client_.get(); }
    AdsCallState* ads_calld() const;
    LrsCallState* lrs_calld() const;

    void ResetBackoff();

    void MaybeStartLrsCall();
    void StopLrsCallLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

    // Returns non-OK if there has been an error since the last time the
    // ADS stream saw a response.
    const absl::Status& status() const { return status_; }

    void SubscribeLocked(const XdsResourceType* type,
                         const XdsResourceName& name)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
    void UnsubscribeLocked(const XdsResourceType* type,
                           const XdsResourceName& name,
                           bool delay_unsubscription)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

   private:
    void OnConnectivityFailure(absl::Status status);

    // Enqueues error notifications to watchers.  Caller must drain
    // XdsClient::work_serializer_ after releasing the lock.
    void SetChannelStatusLocked(absl::Status status)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

    // The owning xds client.
    WeakRefCountedPtr<XdsClient> xds_client_;

    const XdsBootstrap::XdsServer& server_;  // Owned by bootstrap.

    OrphanablePtr<XdsTransportFactory::XdsTransport> transport_;

    bool shutting_down_ = false;

    // The retryable XDS calls.
    OrphanablePtr<RetryableCall<AdsCallState>> ads_calld_;
    OrphanablePtr<RetryableCall<LrsCallState>> lrs_calld_;

    // Stores the most recent accepted resource version for each resource type.
    std::map<const XdsResourceType*, std::string /*version*/>
        resource_type_version_map_;

    absl::Status status_;
  };

  struct ResourceState {
    std::map<ResourceWatcherInterface*, RefCountedPtr<ResourceWatcherInterface>>
        watchers;
    // The latest data seen for the resource.
    std::unique_ptr<XdsResourceType::ResourceData> resource;
    XdsApi::ResourceMetadata meta;
    bool ignored_deletion = false;
  };

  struct AuthorityState {
    RefCountedPtr<ChannelState> channel_state;
    std::map<const XdsResourceType*, std::map<XdsResourceKey, ResourceState>>
        resource_map;
  };

  struct LoadReportState {
    struct LocalityState {
      XdsClusterLocalityStats* locality_stats = nullptr;
      XdsClusterLocalityStats::Snapshot deleted_locality_stats;
    };

    XdsClusterDropStats* drop_stats = nullptr;
    XdsClusterDropStats::Snapshot deleted_drop_stats;
    std::map<RefCountedPtr<XdsLocalityName>, LocalityState,
             XdsLocalityName::Less>
        locality_stats;
    Timestamp last_report_time = Timestamp::Now();
  };

  // Load report data.
  using LoadReportMap = std::map<
      std::pair<std::string /*cluster_name*/, std::string /*eds_service_name*/>,
      LoadReportState>;

  struct LoadReportServer {
    RefCountedPtr<ChannelState> channel_state;
    LoadReportMap load_report_map;
  };

  // Sends an error notification to a specific set of watchers.
  void NotifyWatchersOnErrorLocked(
      const std::map<ResourceWatcherInterface*,
                     RefCountedPtr<ResourceWatcherInterface>>& watchers,
      absl::Status status);
  // Sends a resource-does-not-exist notification to a specific set of watchers.
  void NotifyWatchersOnResourceDoesNotExist(
      const std::map<ResourceWatcherInterface*,
                     RefCountedPtr<ResourceWatcherInterface>>& watchers);

  void MaybeRegisterResourceTypeLocked(const XdsResourceType* resource_type)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Gets the type for resource_type, or null if the type is unknown.
  const XdsResourceType* GetResourceTypeLocked(absl::string_view resource_type)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::StatusOr<XdsResourceName> ParseXdsResourceName(
      absl::string_view name, const XdsResourceType* type);
  static std::string ConstructFullXdsResourceName(
      absl::string_view authority, absl::string_view resource_type,
      const XdsResourceKey& key);

  XdsApi::ClusterLoadReportMap BuildLoadReportSnapshotLocked(
      const XdsBootstrap::XdsServer& xds_server, bool send_all_clusters,
      const std::set<std::string>& clusters) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  RefCountedPtr<ChannelState> GetOrCreateChannelStateLocked(
      const XdsBootstrap::XdsServer& server, const char* reason)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  std::unique_ptr<XdsBootstrap> bootstrap_;
  OrphanablePtr<XdsTransportFactory> transport_factory_;
  const Duration request_timeout_;
  const bool xds_federation_enabled_;
  XdsApi api_;
  WorkSerializer work_serializer_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine_;

  Mutex mu_;

  // Stores resource type objects seen by type URL.
  std::map<absl::string_view /*resource_type*/, const XdsResourceType*>
      resource_types_ ABSL_GUARDED_BY(mu_);
  upb::SymbolTable symtab_ ABSL_GUARDED_BY(mu_);

  // Map of existing xDS server channels.
  // Key is owned by the bootstrap config.
  std::map<const XdsBootstrap::XdsServer*, ChannelState*>
      xds_server_channel_map_ ABSL_GUARDED_BY(mu_);

  std::map<std::string /*authority*/, AuthorityState> authority_state_map_
      ABSL_GUARDED_BY(mu_);

  // Key is owned by the bootstrap config.
  std::map<const XdsBootstrap::XdsServer*, LoadReportServer>
      xds_load_report_server_map_ ABSL_GUARDED_BY(mu_);

  // Stores started watchers whose resource name was not parsed successfully,
  // waiting to be cancelled or reset in Orphan().
  std::map<ResourceWatcherInterface*, RefCountedPtr<ResourceWatcherInterface>>
      invalid_watchers_ ABSL_GUARDED_BY(mu_);

  bool shutting_down_ ABSL_GUARDED_BY(mu_) = false;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_CLIENT_H
