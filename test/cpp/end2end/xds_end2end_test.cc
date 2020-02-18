/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <deque>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/xds/xds_api.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/xds/ads_for_test.grpc.pb.h"
#include "src/proto/grpc/testing/xds/cds_for_test.grpc.pb.h"
#include "src/proto/grpc/testing/xds/eds_for_test.grpc.pb.h"
#include "src/proto/grpc/testing/xds/lds_rds_for_test.grpc.pb.h"
#include "src/proto/grpc/testing/xds/lrs_for_test.grpc.pb.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

// TODO(dgq): Other scenarios in need of testing:
// - Send a serverlist with faulty ip:port addresses (port > 2^16, etc).
// - Test reception of invalid serverlist
// - Test against a non-LB server.
// - Random LB server closing the stream unexpectedly.
//
// Findings from end to end testing to be covered here:
// - Handling of LB servers restart, including reconnection after backing-off
//   retries.
// - Destruction of load balanced channel (and therefore of xds instance)
//   while:
//   1) the internal LB call is still active. This should work by virtue
//   of the weak reference the LB call holds. The call should be terminated as
//   part of the xds shutdown process.
//   2) the retry timer is active. Again, the weak reference it holds should
//   prevent a premature call to \a glb_destroy.

namespace grpc {
namespace testing {
namespace {

using std::chrono::system_clock;

using ::envoy::api::v2::Cluster;
using ::envoy::api::v2::ClusterLoadAssignment;
using ::envoy::api::v2::DiscoveryRequest;
using ::envoy::api::v2::DiscoveryResponse;
using ::envoy::api::v2::FractionalPercent;
using ::envoy::api::v2::HttpConnectionManager;
using ::envoy::api::v2::Listener;
using ::envoy::api::v2::RouteConfiguration;
using ::envoy::service::discovery::v2::AggregatedDiscoveryService;
using ::envoy::service::load_stats::v2::ClusterStats;
using ::envoy::service::load_stats::v2::LoadReportingService;
using ::envoy::service::load_stats::v2::LoadStatsRequest;
using ::envoy::service::load_stats::v2::LoadStatsResponse;
using ::envoy::service::load_stats::v2::UpstreamLocalityStats;

constexpr char kLdsTypeUrl[] = "type.googleapis.com/envoy.api.v2.Listener";
constexpr char kRdsTypeUrl[] =
    "type.googleapis.com/envoy.api.v2.RouteConfiguration";
constexpr char kCdsTypeUrl[] = "type.googleapis.com/envoy.api.v2.Cluster";
constexpr char kEdsTypeUrl[] =
    "type.googleapis.com/envoy.api.v2.ClusterLoadAssignment";
constexpr char kDefaultLocalityRegion[] = "xds_default_locality_region";
constexpr char kDefaultLocalityZone[] = "xds_default_locality_zone";
constexpr char kLbDropType[] = "lb";
constexpr char kThrottleDropType[] = "throttle";
constexpr char kDefaultResourceName[] = "application_target_name";
constexpr char kDefaultVersionString[] = "version_1";
constexpr char kDefaultNonceString[] = "nonce_1";
constexpr int kDefaultLocalityWeight = 3;
constexpr int kDefaultLocalityPriority = 0;

constexpr char kBootstrapFile[] =
    "{\n"
    "  \"xds_servers\": [\n"
    "    {\n"
    "      \"server_uri\": \"fake:///lb\",\n"
    "      \"channel_creds\": [\n"
    "        {\n"
    "          \"type\": \"fake\"\n"
    "        }\n"
    "      ]\n"
    "    }\n"
    "  ],\n"
    "  \"node\": {\n"
    "    \"id\": \"xds_end2end_test\",\n"
    "    \"cluster\": \"test\",\n"
    "    \"metadata\": {\n"
    "      \"foo\": \"bar\"\n"
    "    },\n"
    "    \"locality\": {\n"
    "      \"region\": \"corp\",\n"
    "      \"zone\": \"svl\",\n"
    "      \"subzone\": \"mp3\"\n"
    "    }\n"
    "  }\n"
    "}\n";

constexpr char kBootstrapFileBad[] =
    "{\n"
    "  \"xds_servers\": [\n"
    "    {\n"
    "      \"server_uri\": \"fake:///wrong_lb\",\n"
    "      \"channel_creds\": [\n"
    "        {\n"
    "          \"type\": \"fake\"\n"
    "        }\n"
    "      ]\n"
    "    }\n"
    "  ],\n"
    "  \"node\": {\n"
    "  }\n"
    "}\n";

char* g_bootstrap_file;
char* g_bootstrap_file_bad;

void WriteBootstrapFiles() {
  char* bootstrap_file;
  FILE* out = gpr_tmpfile("xds_bootstrap", &bootstrap_file);
  fputs(kBootstrapFile, out);
  fclose(out);
  g_bootstrap_file = bootstrap_file;
  out = gpr_tmpfile("xds_bootstrap_bad", &bootstrap_file);
  fputs(kBootstrapFileBad, out);
  fclose(out);
  g_bootstrap_file_bad = bootstrap_file;
}

// Helper class to minimize the number of unique ports we use for this test.
class PortSaver {
 public:
  int GetPort() {
    if (idx_ >= ports_.size()) {
      ports_.push_back(grpc_pick_unused_port_or_die());
    }
    return ports_[idx_++];
  }

  void Reset() { idx_ = 0; }

 private:
  std::vector<int> ports_;
  size_t idx_ = 0;
};

PortSaver* g_port_saver = nullptr;

template <typename ServiceType>
class CountedService : public ServiceType {
 public:
  size_t request_count() {
    grpc_core::MutexLock lock(&mu_);
    return request_count_;
  }

  size_t response_count() {
    grpc_core::MutexLock lock(&mu_);
    return response_count_;
  }

  void IncreaseResponseCount() {
    grpc_core::MutexLock lock(&mu_);
    ++response_count_;
  }
  void IncreaseRequestCount() {
    grpc_core::MutexLock lock(&mu_);
    ++request_count_;
  }

  void ResetCounters() {
    grpc_core::MutexLock lock(&mu_);
    request_count_ = 0;
    response_count_ = 0;
  }

 protected:
  grpc_core::Mutex mu_;

 private:
  size_t request_count_ = 0;
  size_t response_count_ = 0;
};

using BackendService = CountedService<TestServiceImpl>;
using AdsService = CountedService<AggregatedDiscoveryService::Service>;
using LrsService = CountedService<LoadReportingService::Service>;

const char g_kCallCredsMdKey[] = "Balancer should not ...";
const char g_kCallCredsMdValue[] = "... receive me";

class BackendServiceImpl : public BackendService {
 public:
  BackendServiceImpl() {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    // Backend should receive the call credentials metadata.
    auto call_credentials_entry =
        context->client_metadata().find(g_kCallCredsMdKey);
    EXPECT_NE(call_credentials_entry, context->client_metadata().end());
    if (call_credentials_entry != context->client_metadata().end()) {
      EXPECT_EQ(call_credentials_entry->second, g_kCallCredsMdValue);
    }
    IncreaseRequestCount();
    const auto status = TestServiceImpl::Echo(context, request, response);
    IncreaseResponseCount();
    AddClient(context->peer());
    return status;
  }

  void Start() {}
  void Shutdown() {}

  std::set<grpc::string> clients() {
    grpc_core::MutexLock lock(&clients_mu_);
    return clients_;
  }

 private:
  void AddClient(const grpc::string& client) {
    grpc_core::MutexLock lock(&clients_mu_);
    clients_.insert(client);
  }

  grpc_core::Mutex mu_;
  grpc_core::Mutex clients_mu_;
  std::set<grpc::string> clients_;
};

class ClientStats {
 public:
  struct LocalityStats {
    // Converts from proto message class.
    LocalityStats(const UpstreamLocalityStats& upstream_locality_stats)
        : total_successful_requests(
              upstream_locality_stats.total_successful_requests()),
          total_requests_in_progress(
              upstream_locality_stats.total_requests_in_progress()),
          total_error_requests(upstream_locality_stats.total_error_requests()),
          total_issued_requests(
              upstream_locality_stats.total_issued_requests()) {}

    uint64_t total_successful_requests;
    uint64_t total_requests_in_progress;
    uint64_t total_error_requests;
    uint64_t total_issued_requests;
  };

  // Converts from proto message class.
  ClientStats(const ClusterStats& cluster_stats)
      : total_dropped_requests_(cluster_stats.total_dropped_requests()) {
    for (const auto& input_locality_stats :
         cluster_stats.upstream_locality_stats()) {
      locality_stats_.emplace(input_locality_stats.locality().sub_zone(),
                              LocalityStats(input_locality_stats));
    }
    for (const auto& input_dropped_requests :
         cluster_stats.dropped_requests()) {
      dropped_requests_.emplace(input_dropped_requests.category(),
                                input_dropped_requests.dropped_count());
    }
  }

  uint64_t total_successful_requests() const {
    uint64_t sum = 0;
    for (auto& p : locality_stats_) {
      sum += p.second.total_successful_requests;
    }
    return sum;
  }
  uint64_t total_requests_in_progress() const {
    uint64_t sum = 0;
    for (auto& p : locality_stats_) {
      sum += p.second.total_requests_in_progress;
    }
    return sum;
  }
  uint64_t total_error_requests() const {
    uint64_t sum = 0;
    for (auto& p : locality_stats_) {
      sum += p.second.total_error_requests;
    }
    return sum;
  }
  uint64_t total_issued_requests() const {
    uint64_t sum = 0;
    for (auto& p : locality_stats_) {
      sum += p.second.total_issued_requests;
    }
    return sum;
  }
  uint64_t total_dropped_requests() const { return total_dropped_requests_; }
  uint64_t dropped_requests(const grpc::string& category) const {
    auto iter = dropped_requests_.find(category);
    GPR_ASSERT(iter != dropped_requests_.end());
    return iter->second;
  }

 private:
  std::map<grpc::string, LocalityStats> locality_stats_;
  uint64_t total_dropped_requests_;
  std::map<grpc::string, uint64_t> dropped_requests_;
};

// TODO(roth): Change this service to a real fake.
class AdsServiceImpl : public AdsService {
 public:
  enum ResponseState {
    NOT_SENT,
    SENT,
    ACKED,
    NACKED,
  };

  struct ResponseArgs {
    struct Locality {
      Locality(const grpc::string& sub_zone, std::vector<int> ports,
               int lb_weight = kDefaultLocalityWeight,
               int priority = kDefaultLocalityPriority,
               std::vector<envoy::api::v2::HealthStatus> health_statuses = {})
          : sub_zone(std::move(sub_zone)),
            ports(std::move(ports)),
            lb_weight(lb_weight),
            priority(priority),
            health_statuses(std::move(health_statuses)) {}

      const grpc::string sub_zone;
      std::vector<int> ports;
      int lb_weight;
      int priority;
      std::vector<envoy::api::v2::HealthStatus> health_statuses;
    };

    ResponseArgs() = default;
    explicit ResponseArgs(std::vector<Locality> locality_list)
        : locality_list(std::move(locality_list)) {}

    std::vector<Locality> locality_list;
    std::map<grpc::string, uint32_t> drop_categories;
    FractionalPercent::DenominatorType drop_denominator =
        FractionalPercent::MILLION;
  };

  using Stream = ServerReaderWriter<DiscoveryResponse, DiscoveryRequest>;
  using ResponseDelayPair = std::pair<DiscoveryResponse, int>;

  // A queue of resource type/name pair that have changed since the client
  // subscribed to them.
  using UpdateQueue = std::deque<
      std::pair<std::string /* type url */, std::string /* resource name */>>;

  // A struct representing a client's subscription to a particular resource.
  struct SubscriberState {
    // Version that the client currently knows about.
    int current_version = 0;
    // The queue upon which to place updates when the resource is updated.
    // (Will point to the local update_queue variable.)
    UpdateQueue* update_queue;
  };

  using SubscriptionMap =
      std::map<std::string /* type_url */,
               std::map<std::string /* resource_name */, SubscriberState>>;

  // A struct representing the current state for a LDS resource;
  // keeping track of list of subscribers and the version they are subscribed
  // to.
  struct LdsResourceState {
    int version = 0;
    Listener resource;
    std::set<SubscriberState*> subscribers;
  };

  // A struct representing the current state for a CDS resource;
  // keeping track of list of subscribers and the version they are subscribed
  // to.
  struct CdsResourceState {
    int version = 0;
    Cluster resource;
    std::set<SubscriberState*> subscribers;
  };

  // A struct representing the current state for a EDS resource;
  // keeping track of list of subscribers and the version they are subscribed
  // to.
  struct EdsResourceState {
    int version = 0;
    DiscoveryResponse resource;
    std::set<SubscriberState*> subscribers;
  };

  // A struct representing the current state for a RDS resource;
  // keeping track of list of subscribers and the version they are subscribed
  // to.
  struct RdsResourceState {
    int version = 0;
    RouteConfiguration resource;
    std::set<SubscriberState*> subscribers;
  };

  // A struct representing the current state for all resources:
  // LDS, CDS, EDS, and RDS for the class as a whole.
  struct ResourcesMap {
    std::map<std::string, LdsResourceState> lds_resources_state;
    std::map<std::string, CdsResourceState> cds_resources_state;
    std::map<std::string, EdsResourceState> eds_resources_state;
    std::map<std::string, RdsResourceState> rds_resources_state;
  };

  AdsServiceImpl(bool enable_load_reporting) {
    // Construct RDS response data.
    default_route_config_.set_name(kDefaultResourceName);
    auto* virtual_host = default_route_config_.add_virtual_hosts();
    virtual_host->add_domains("*");
    auto* route = virtual_host->add_routes();
    route->mutable_match()->set_prefix("");
    route->mutable_route()->set_cluster(kDefaultResourceName);
    SetRdsResponse(default_route_config_);
    // Construct LDS response data (with inlined RDS result).
    default_listener_ = BuildListener(default_route_config_);
    SetLdsResponse(default_listener_);
    // Construct CDS response data.
    default_cluster_.set_name(kDefaultResourceName);
    default_cluster_.set_type(envoy::api::v2::Cluster::EDS);
    default_cluster_.mutable_eds_cluster_config()
        ->mutable_eds_config()
        ->mutable_ads();
    default_cluster_.set_lb_policy(envoy::api::v2::Cluster::ROUND_ROBIN);
    if (enable_load_reporting) {
      default_cluster_.mutable_lrs_server()->mutable_self();
    }
    SetCdsResponse(default_cluster_);
  }

  void HandleLdsRequest(Stream* stream, const std::string& name) {
    gpr_log(GPR_INFO, "ADS[%p]: Handle LDS update name %s", this, name.c_str());
    DiscoveryResponse response;
    response.set_type_url(kLdsTypeUrl);
    response.set_version_info(kDefaultVersionString);
    response.set_nonce(kDefaultNonceString);
    {
      grpc_core::MutexLock lock(&ads_mu_);
      if (lds_ignore_) return;
      auto lds_resource = resources_map_.lds_resources_state.find(name);
      GPR_ASSERT(lds_resource != resources_map_.lds_resources_state.end());
      response.add_resources()->PackFrom(lds_resource->second.resource);
      lds_response_state_ = SENT;
    }
    gpr_log(GPR_INFO, "ADS[%p]: LDS request: sending response  %s", this,
            response.DebugString().c_str());
    stream->Write(response);
  }

  void HandleRdsRequest(Stream* stream, const std::string& name) {
    gpr_log(GPR_INFO, "ADS[%p]: Handle RDS update name %s", this, name.c_str());
    DiscoveryResponse response;
    response.set_type_url(kRdsTypeUrl);
    response.set_version_info(kDefaultVersionString);
    response.set_nonce(kDefaultNonceString);
    {
      grpc_core::MutexLock lock(&ads_mu_);
      if (rds_ignore_) return;
      auto rds_resource = resources_map_.rds_resources_state.find(name);
      GPR_ASSERT(rds_resource != resources_map_.rds_resources_state.end());
      response.add_resources()->PackFrom(rds_resource->second.resource);
      rds_response_state_ = SENT;
    }
    gpr_log(GPR_INFO, "ADS[%p]: RDS request: sending response  %s", this,
            response.DebugString().c_str());
    stream->Write(response);
  }

  void HandleCdsRequest(Stream* stream, const std::string& name) {
    gpr_log(GPR_INFO, "ADS[%p]: Handle CDS update name %s", this, name.c_str());
    DiscoveryResponse response;
    response.set_type_url(kCdsTypeUrl);
    response.set_version_info(kDefaultVersionString);
    response.set_nonce(kDefaultNonceString);
    {
      grpc_core::MutexLock lock(&ads_mu_);
      if (cds_ignore_) return;
      auto cds_resource = resources_map_.cds_resources_state.find(name);
      GPR_ASSERT(cds_resource != resources_map_.cds_resources_state.end());
      response.add_resources()->PackFrom(cds_resource->second.resource);
      cds_response_state_ = SENT;
    }
    gpr_log(GPR_INFO, "ADS[%p]: CDS request: sending response  %s", this,
            response.DebugString().c_str());
    stream->Write(response);
  }

  void HandleEdsRequest(Stream* stream, const std::string& name) {
    gpr_log(GPR_INFO, "ADS[%p]: Handle EDS update name %s", this, name.c_str());
    DiscoveryResponse response;
    {
      grpc_core::MutexLock lock(&ads_mu_);
      if (eds_ignore_) return;
      auto eds_resource = resources_map_.eds_resources_state.find(name);
      GPR_ASSERT(eds_resource != resources_map_.eds_resources_state.end());
      response = eds_resource->second.resource;
    }
    gpr_log(GPR_INFO, "ADS[%p]: EDS request: sending response '%s'", this,
            response.DebugString().c_str());
    IncreaseResponseCount();
    stream->Write(response);
  }

  void BlockingRead(Stream* stream, std::deque<DiscoveryRequest>* requests,
                    bool* stream_closed) {
    DiscoveryRequest request;
    bool seen_first_request = false;
    while (stream->Read(&request)) {
      if (!seen_first_request) {
        EXPECT_TRUE(request.has_node());
        seen_first_request = true;
      }
      {
        grpc_core::MutexLock lock(&ads_mu_);
        requests->emplace_back(request);
      }
    }
    gpr_log(GPR_INFO, "ADS[%p]: Null read, stream closed", this);
    grpc_core::MutexLock lock(&ads_mu_);
    *stream_closed = true;
  }

  // This is a helper function to check versions of the resources and update it
  // if necessary. A lock to ads_mu is required before calling this method.
  bool ResourceUpdated(SubscriptionMap* subscription_map,
                       const string& resource_type, const string& name) {
    if (resource_type == kLdsTypeUrl) {
      auto sub_iter = subscription_map->find(kLdsTypeUrl)->second.find(name);
      auto resource_iter = resources_map_.lds_resources_state.find(name);
      if (sub_iter->second.current_version < resource_iter->second.version) {
        sub_iter->second.current_version = resource_iter->second.version;
        gpr_log(GPR_INFO,
                "ADS[%p]: Need to process new LDS update, bring current to %d",
                this, sub_iter->second.current_version);
        return true;
      } else {
        gpr_log(GPR_INFO,
                "ADS[%p]: Skipping an old LDS update, current is at %d", this,
                sub_iter->second.current_version);
        return false;
      }
    } else if (resource_type == kCdsTypeUrl) {
      auto sub_iter = subscription_map->find(kCdsTypeUrl)->second.find(name);
      auto resource_iter = resources_map_.cds_resources_state.find(name);
      if (sub_iter->second.current_version < resource_iter->second.version) {
        sub_iter->second.current_version = resource_iter->second.version;
        gpr_log(GPR_INFO,
                "ADS[%p]: Need to process new CDS update, bring current to %d",
                this, sub_iter->second.current_version);
        return true;
      } else {
        gpr_log(GPR_INFO,
                "ADS[%p]: Skipping an old CDS update, current is at %d", this,
                sub_iter->second.current_version);
        return false;
      }
    } else if (resource_type == kEdsTypeUrl) {
      auto sub_iter = subscription_map->find(kEdsTypeUrl)->second.find(name);
      auto resource_iter = resources_map_.eds_resources_state.find(name);
      if (sub_iter->second.current_version < resource_iter->second.version) {
        sub_iter->second.current_version = resource_iter->second.version;
        gpr_log(GPR_INFO,
                "ADS[%p]: Need to process new EDS update, bring current to %d",
                this, sub_iter->second.current_version);
        return true;
      } else {
        gpr_log(GPR_INFO,
                "ADS[%p]: Skipping an old EDS update, current is at %d", this,
                sub_iter->second.current_version);
        return false;
      }
    } else if (resource_type == kRdsTypeUrl) {
      auto sub_iter = subscription_map->find(kRdsTypeUrl)->second.find(name);
      auto resource_iter = resources_map_.rds_resources_state.find(name);
      if (sub_iter->second.current_version < resource_iter->second.version) {
        sub_iter->second.current_version = resource_iter->second.version;
        gpr_log(GPR_INFO,
                "ADS[%p]: Need to process new RDS update, bring current to %d",
                this, sub_iter->second.current_version);
        return true;
      } else {
        gpr_log(GPR_INFO,
                "ADS[%p]: Skipping an old RDS update, current is at %d", this,
                sub_iter->second.current_version);
        return false;
      }
    }
    return false;
  }

  void ResourceSubscribe(SubscriptionMap* subscription_map,
                         UpdateQueue* update_queue,
                         const std::string& resource_type,
                         const std::string& name) {
    // Subscribe to all resources: LCS, CDS, EDS, and RDS.
    grpc_core::MutexLock lock(&ads_mu_);
    if (resource_type == kLdsTypeUrl) {
      auto lds_entry = resources_map_.lds_resources_state.find(name);
      if (lds_entry == resources_map_.lds_resources_state.end()) {
        gpr_log(
            GPR_INFO,
            "ADS[%p]: Adding new LDS entry %s to resources map and subscribe.",
            this, name.c_str());
        LdsResourceState state;
        state.subscribers.emplace(
            &(subscription_map->find(kLdsTypeUrl)->second.find(name)->second));
        resources_map_.lds_resources_state.emplace(name, std::move(state));
      } else {
        gpr_log(GPR_INFO,
                "ADS[%p]: Resources map has the LDS entry %s, subscribe, and "
                "queue the update.",
                this, name.c_str());
        lds_entry->second.subscribers.emplace(
            &(subscription_map->find(kLdsTypeUrl)->second.find(name)->second));
        update_queue->emplace_back(kLdsTypeUrl, name);
      }
    } else if (resource_type == kCdsTypeUrl) {
      auto cds_entry = resources_map_.cds_resources_state.find(name);
      if (cds_entry == resources_map_.cds_resources_state.end()) {
        gpr_log(
            GPR_INFO,
            "ADS[%p]: Adding new CDS entry %s to resources map and subsribe.",
            this, name.c_str());
        CdsResourceState state;
        state.subscribers.emplace(
            &(subscription_map->find(kCdsTypeUrl)->second.find(name)->second));
        resources_map_.cds_resources_state.emplace(name, std::move(state));
      } else {
        gpr_log(GPR_INFO,
                "ADS[%p]: Resources map has the CDS entry %s, subscribe, and "
                "queue the update.",
                this, name.c_str());
        cds_entry->second.subscribers.emplace(
            &(subscription_map->find(kCdsTypeUrl)->second.find(name)->second));
        update_queue->emplace_back(kCdsTypeUrl, name);
      }
    } else if (resource_type == kEdsTypeUrl) {
      auto eds_entry = resources_map_.eds_resources_state.find(name);
      if (eds_entry == resources_map_.eds_resources_state.end()) {
        gpr_log(
            GPR_INFO,
            "ADS[%p]: Adding new EDS entry %s to resources map and subsribe.",
            this, name.c_str());
        EdsResourceState state;
        state.subscribers.emplace(
            &(subscription_map->find(kEdsTypeUrl)->second.find(name)->second));
        resources_map_.eds_resources_state.emplace(name, std::move(state));
      } else {
        gpr_log(GPR_INFO,
                "ADS[%p]: Resources map has the EDS entry %s, subscribe, and "
                "queue the update.",
                this, name.c_str());
        eds_entry->second.subscribers.emplace(
            &(subscription_map->find(kEdsTypeUrl)->second.find(name)->second));
        update_queue->emplace_back(kEdsTypeUrl, name);
      }
    } else if (resource_type == kRdsTypeUrl) {
      auto rds_entry = resources_map_.rds_resources_state.find(name);
      if (rds_entry == resources_map_.rds_resources_state.end()) {
        gpr_log(
            GPR_INFO,
            "ADS[%p]: Adding new RDS entry %s to resources map and subsribe.",
            this, name.c_str());
        RdsResourceState state;
        state.subscribers.emplace(
            &(subscription_map->find(kRdsTypeUrl)->second.find(name)->second));
        resources_map_.rds_resources_state.emplace(name, std::move(state));
      } else {
        gpr_log(GPR_INFO,
                "ADS[%p]: Resources map has the RDS entry %s, subscribe, and "
                "queue the update.",
                this, name.c_str());
        rds_entry->second.subscribers.emplace(
            &(subscription_map->find(kRdsTypeUrl)->second.find(name)->second));
        update_queue->emplace_back(kRdsTypeUrl, name);
      }
    }
  }

  void ResourceUnsubscribe(SubscriptionMap* subscription_map,
                           const std::string& resource_type,
                           const std::string& name) {
    grpc_core::MutexLock lock(&ads_mu_);
    if (resource_type == kLdsTypeUrl) {
      resources_map_.lds_resources_state.find(name)->second.subscribers.erase(
          &(subscription_map->find(kLdsTypeUrl)->second.find(name)->second));
    } else if (resource_type == kCdsTypeUrl) {
      resources_map_.cds_resources_state.find(name)->second.subscribers.erase(
          &(subscription_map->find(kCdsTypeUrl)->second.find(name)->second));
    } else if (resource_type == kEdsTypeUrl) {
      resources_map_.eds_resources_state.find(name)->second.subscribers.erase(
          &(subscription_map->find(kEdsTypeUrl)->second.find(name)->second));
    } else if (resource_type == kRdsTypeUrl) {
      resources_map_.rds_resources_state.find(name)->second.subscribers.erase(
          &(subscription_map->find(kRdsTypeUrl)->second.find(name)->second));
    }
  }

  void ResourceUnsubscribeAll(SubscriptionMap* subscription_map) {
    grpc_core::MutexLock lock(&ads_mu_);
    for (auto& lds_sub : subscription_map->find(kLdsTypeUrl)->second) {
      resources_map_.lds_resources_state.find(lds_sub.first)
          ->second.subscribers.erase(&(lds_sub.second));
    }
    GPR_ASSERT(resources_map_.lds_resources_state.find(kDefaultResourceName)
                   ->second.subscribers.empty());
    for (auto& cds_sub : subscription_map->find(kCdsTypeUrl)->second) {
      resources_map_.cds_resources_state.find(cds_sub.first)
          ->second.subscribers.erase(&(cds_sub.second));
    }
    GPR_ASSERT(resources_map_.cds_resources_state.find(kDefaultResourceName)
                   ->second.subscribers.empty());
    for (auto& eds_sub : subscription_map->find(kEdsTypeUrl)->second) {
      resources_map_.eds_resources_state.find(eds_sub.first)
          ->second.subscribers.erase(&(eds_sub.second));
    }
    GPR_ASSERT(resources_map_.eds_resources_state.find(kDefaultResourceName)
                   ->second.subscribers.empty());
    for (auto& rds_sub : subscription_map->find(kRdsTypeUrl)->second) {
      resources_map_.rds_resources_state.find(rds_sub.first)
          ->second.subscribers.erase(&(rds_sub.second));
    }
    GPR_ASSERT(resources_map_.rds_resources_state.find(kDefaultResourceName)
                   ->second.subscribers.empty());
  }

  Status StreamAggregatedResources(ServerContext* context,
                                   Stream* stream) override {
    gpr_log(GPR_INFO, "ADS[%p]: StreamAggregatedResources starts", this);
    [&]() {
      {
        grpc_core::MutexLock lock(&ads_mu_);
        if (ads_done_) return;
      }
      // Balancer shouldn't receive the call credentials metadata.
      EXPECT_EQ(context->client_metadata().find(g_kCallCredsMdKey),
                context->client_metadata().end());
      // Resources (type/name pair) that have changed since the client
      // subscribed to them.
      UpdateQueue update_queue;
      // Resources that the client is subscribed to keyed by resource type url.
      SubscriptionMap subscription_map;
      SubscriberState subscriber_state;
      subscriber_state.update_queue = &update_queue;
      std::map<std::string, SubscriberState> lds_subscriber_map;
      lds_subscriber_map.emplace(kDefaultResourceName, subscriber_state);
      std::map<std::string, SubscriberState> cds_subscriber_map;
      cds_subscriber_map.emplace(kDefaultResourceName, subscriber_state);
      std::map<std::string, SubscriberState> eds_subscriber_map;
      eds_subscriber_map.emplace(kDefaultResourceName, subscriber_state);
      std::map<std::string, SubscriberState> rds_subscriber_map;
      rds_subscriber_map.emplace(kDefaultResourceName, subscriber_state);
      {
        grpc_core::MutexLock lock(&ads_mu_);
        subscription_map.emplace(kLdsTypeUrl, std::move(lds_subscriber_map));
        subscription_map.emplace(kCdsTypeUrl, std::move(cds_subscriber_map));
        subscription_map.emplace(kEdsTypeUrl, std::move(eds_subscriber_map));
        subscription_map.emplace(kRdsTypeUrl, std::move(rds_subscriber_map));
      }
      ResourceSubscribe(&subscription_map, &update_queue, kLdsTypeUrl,
                        kDefaultResourceName);
      ResourceSubscribe(&subscription_map, &update_queue, kCdsTypeUrl,
                        kDefaultResourceName);
      ResourceSubscribe(&subscription_map, &update_queue, kEdsTypeUrl,
                        kDefaultResourceName);
      ResourceSubscribe(&subscription_map, &update_queue, kRdsTypeUrl,
                        kDefaultResourceName);
      // Creating blocking thread to read from stream.
      std::deque<DiscoveryRequest> requests;
      bool stream_closed = false;
      std::thread reader(std::bind(&AdsServiceImpl::BlockingRead, this, stream,
                                   &requests, &stream_closed));
      bool first_lds_request_seen = false;
      bool first_cds_request_seen = false;
      bool first_eds_request_seen = false;
      bool first_rds_request_seen = false;
      // Main loop to look for requests and updates.
      while (true) {
        // Look for new requests and and decide what to handle.
        DiscoveryRequest request;
        DiscoveryResponse response;
        bool request_work = false;
        bool handle_lds_request = false;
        bool handle_cds_request = false;
        bool handle_eds_request = false;
        bool handle_rds_request = false;
        {
          grpc_core::MutexLock lock(&ads_mu_);
          if (stream_closed) break;
          if (!requests.empty()) {
            request = std::move(requests.front());
            requests.pop_front();
            request_work = true;
            gpr_log(GPR_INFO, "ADS[%p]: Handling request %s with content %s",
                    this, request.type_url().c_str(),
                    request.DebugString().c_str());
            if (request.type_url() == kLdsTypeUrl) {
              first_lds_request_seen = true;
              // Validate previously sent response by checking subsequent
              // requests for version string.
              if (lds_response_state_ == SENT) {
                GPR_ASSERT(!request.response_nonce().empty());
                lds_response_state_ =
                    request.version_info() == kDefaultVersionString ? ACKED
                                                                    : NACKED;
              }
              for (const auto& listener_name : request.resource_names()) {
                handle_lds_request |= ResourceUpdated(
                    &subscription_map, request.type_url(), listener_name);
              }
            } else if (request.type_url() == kCdsTypeUrl) {
              first_cds_request_seen = true;
              // Validate previously sent response by checking subsequent
              // requests for version string.
              if (cds_response_state_ == SENT) {
                GPR_ASSERT(!request.response_nonce().empty());
                cds_response_state_ =
                    request.version_info() == kDefaultVersionString ? ACKED
                                                                    : NACKED;
              }
              for (const auto& cluster_name : request.resource_names()) {
                handle_cds_request |= ResourceUpdated(
                    &subscription_map, request.type_url(), cluster_name);
              }
            } else if (request.type_url() == kEdsTypeUrl) {
              first_eds_request_seen = true;
              for (const auto& endpoint_name : request.resource_names()) {
                handle_eds_request |= ResourceUpdated(
                    &subscription_map, request.type_url(), endpoint_name);
              }
            } else if (request.type_url() == kRdsTypeUrl) {
              first_rds_request_seen = true;
              // Validate previously sent response by checking subsequent
              // requests for version string.
              if (rds_response_state_ == SENT) {
                GPR_ASSERT(!request.response_nonce().empty());
                rds_response_state_ =
                    request.version_info() == kDefaultVersionString ? ACKED
                                                                    : NACKED;
              }
              for (const auto& route_name : request.resource_names()) {
                handle_rds_request |= ResourceUpdated(
                    &subscription_map, request.type_url(), route_name);
              }
            }
          }
        }
        if (handle_lds_request) {
          for (const auto& listener_name : request.resource_names()) {
            HandleLdsRequest(stream, listener_name);
          }
        }
        if (handle_cds_request) {
          for (const auto& cluster_name : request.resource_names()) {
            HandleCdsRequest(stream, cluster_name);
          }
        }
        if (handle_eds_request) {
          IncreaseRequestCount();
          for (const auto& endpoint_name : request.resource_names()) {
            HandleEdsRequest(stream, endpoint_name);
          }
        }
        if (handle_rds_request) {
          for (const auto& route_name : request.resource_names()) {
            HandleRdsRequest(stream, route_name);
          }
        }
        // Look for updates and decide what to handle.
        std::pair<std::string, std::string> update;
        bool update_work = false;
        bool handle_lds_update = false;
        bool handle_cds_update = false;
        bool handle_eds_update = false;
        bool handle_rds_update = false;
        {
          grpc_core::MutexLock lock(&ads_mu_);
          if (!update_queue.empty()) {
            update = std::move(update_queue.front());
            update_queue.pop_front();
            update_work = true;
          }
          // Only service updates after the first EDS request/response is
          // taken place and there is a version update..
          if (update.first == kLdsTypeUrl) {
            handle_lds_update =
                first_lds_request_seen &&
                ResourceUpdated(&subscription_map, update.first, update.second);
          } else if (update.first == kCdsTypeUrl) {
            handle_cds_update =
                first_cds_request_seen &&
                ResourceUpdated(&subscription_map, update.first, update.second);
          } else if (update.first == kEdsTypeUrl) {
            handle_eds_update =
                first_eds_request_seen &&
                ResourceUpdated(&subscription_map, update.first, update.second);
          } else if (update.first == kRdsTypeUrl) {
            handle_rds_update =
                first_rds_request_seen &&
                ResourceUpdated(&subscription_map, update.first, update.second);
          }
        }
        if (handle_lds_update) {
          HandleLdsRequest(stream, update.second);
        } else if (handle_cds_update) {
          HandleCdsRequest(stream, update.second);
        } else if (handle_eds_update) {
          HandleEdsRequest(stream, update.second);
        } else if (handle_rds_update) {
          HandleRdsRequest(stream, update.second);
        }
        if (!request_work && !update_work) {
          gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(10));
        }
      }
      reader.join();
      ResourceUnsubscribeAll(&subscription_map);
      // Wait until notified done.
      ads_cond_.WaitUntil(&ads_mu_, [this] { return ads_done_; });
    }();
    gpr_log(GPR_INFO, "ADS[%p]: StreamAggregatedResources done", this);
    return Status::OK;
  }

  Listener default_listener() const { return default_listener_; }
  RouteConfiguration default_route_config() const {
    return default_route_config_;
  }
  Cluster default_cluster() const { return default_cluster_; }

  ResponseState lds_response_state() {
    grpc_core::MutexLock lock(&ads_mu_);
    return lds_response_state_;
  }

  ResponseState rds_response_state() {
    grpc_core::MutexLock lock(&ads_mu_);
    return rds_response_state_;
  }

  ResponseState cds_response_state() {
    grpc_core::MutexLock lock(&ads_mu_);
    return cds_response_state_;
  }

  void SetLdsResponse(const Listener& listener, int delay_ms = 0,
                      const std::string& name = kDefaultResourceName) {
    if (delay_ms > 0) {
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(delay_ms));
    }
    grpc_core::MutexLock lock(&ads_mu_);
    auto entry = resources_map_.lds_resources_state.find(name);
    if (entry == resources_map_.lds_resources_state.end()) {
      gpr_log(GPR_INFO, "ADS[%p]: Add a new LDS resource %s", this,
              name.c_str());
      LdsResourceState state;
      state.version++;
      state.resource = listener;
      resources_map_.lds_resources_state.emplace(name, std::move(state));
      // no subscribers yet
    } else {
      entry->second.version++;
      entry->second.resource = listener;
      gpr_log(GPR_INFO,
              "ADS[%p]: Updating an existing LDS resource %s to version %u",
              this, name.c_str(), entry->second.version);
      // update subscriber's update_queue about this updated resource.
      for (const auto& sub : entry->second.subscribers) {
        sub->update_queue->emplace_back(kLdsTypeUrl, name);
      }
    }
  }

  void set_lds_ignore() { lds_ignore_ = true; }

  void SetRdsResponse(const RouteConfiguration& route, int delay_ms = 0,
                      const std::string& name = kDefaultResourceName) {
    if (delay_ms > 0) {
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(delay_ms));
    }
    grpc_core::MutexLock lock(&ads_mu_);
    auto entry = resources_map_.rds_resources_state.find(name);
    if (entry == resources_map_.rds_resources_state.end()) {
      gpr_log(GPR_INFO, "ADS[%p]: Add a new RDS resource %s", this,
              name.c_str());
      RdsResourceState state;
      state.version++;
      state.resource = route;
      resources_map_.rds_resources_state.emplace(name, std::move(state));
      // no subscribers yet
    } else {
      entry->second.version++;
      entry->second.resource = route;
      gpr_log(GPR_INFO,
              "ADS[%p]: Updating an existing RDS resource %s to version %u",
              this, name.c_str(), entry->second.version);
      // update subscriber's update_queue about this updated resource.
      for (const auto& sub : entry->second.subscribers) {
        sub->update_queue->emplace_back(kRdsTypeUrl, name);
      }
    }
  }

  void set_rds_ignore() { rds_ignore_ = true; }

  void SetCdsResponse(const Cluster& cluster, int delay_ms = 0,
                      const std::string& name = kDefaultResourceName) {
    if (delay_ms > 0) {
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(delay_ms));
    }
    grpc_core::MutexLock lock(&ads_mu_);
    auto entry = resources_map_.cds_resources_state.find(name);
    if (entry == resources_map_.cds_resources_state.end()) {
      gpr_log(GPR_INFO, "ADS[%p]: Add a new CDS resource %s", this,
              name.c_str());
      CdsResourceState state;
      state.version++;
      state.resource = cluster;
      resources_map_.cds_resources_state.emplace(name, std::move(state));
      // no subscribers yet
    } else {
      entry->second.version++;
      entry->second.resource = cluster;
      gpr_log(GPR_INFO,
              "ADS[%p]: Updating an existing CDS resource %s to version %u",
              this, name.c_str(), entry->second.version);
      // update subscriber's update_queue about this updated resource.
      for (const auto& sub : entry->second.subscribers) {
        sub->update_queue->emplace_back(kCdsTypeUrl, name);
      }
    }
  }

  void set_cds_ignore() { cds_ignore_ = true; }

  void SetEdsResponse(const DiscoveryResponse& response, int delay_ms = 0,
                      const std::string& name = kDefaultResourceName) {
    if (delay_ms > 0) {
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(delay_ms));
    }
    grpc_core::MutexLock lock(&ads_mu_);
    auto entry = resources_map_.eds_resources_state.find(name);
    if (entry == resources_map_.eds_resources_state.end()) {
      gpr_log(GPR_INFO, "ADS[%p]: Add a new EDS resource %s", this,
              name.c_str());
      EdsResourceState state;
      state.version++;
      state.resource = response;
      resources_map_.eds_resources_state.emplace(name, std::move(state));
      // no subscribers yet
    } else {
      entry->second.version++;
      entry->second.resource = response;
      gpr_log(GPR_INFO,
              "ADS[%p]: Updating an existing EDS resource %s to version %u",
              this, name.c_str(), entry->second.version);
      // update subscriber's update_queue about this updated resource.
      for (const auto& sub : entry->second.subscribers) {
        sub->update_queue->emplace_back(kEdsTypeUrl, name);
      }
    }
  }

  void set_eds_ignore() { eds_ignore_ = true; }

  void SetLdsToUseDynamicRds() {
    auto listener = default_listener_;
    HttpConnectionManager http_connection_manager;
    http_connection_manager.mutable_rds()->set_route_config_name(
        kDefaultResourceName);
    listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
        http_connection_manager);
    SetLdsResponse(std::move(listener));
  }

  static Listener BuildListener(const RouteConfiguration& route_config) {
    HttpConnectionManager http_connection_manager;
    *(http_connection_manager.mutable_route_config()) = route_config;
    Listener listener;
    listener.set_name(kDefaultResourceName);
    listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
        http_connection_manager);
    return listener;
  }

  void Start() {
    grpc_core::MutexLock lock(&ads_mu_);
    ads_done_ = false;
  }

  void Shutdown() {
    {
      grpc_core::MutexLock lock(&ads_mu_);
      NotifyDoneWithAdsCallLocked();
    }
    gpr_log(GPR_INFO, "ADS[%p]: shut down", this);
  }

  static DiscoveryResponse BuildResponse(const ResponseArgs& args) {
    ClusterLoadAssignment assignment;
    assignment.set_cluster_name(kDefaultResourceName);
    for (const auto& locality : args.locality_list) {
      auto* endpoints = assignment.add_endpoints();
      endpoints->mutable_load_balancing_weight()->set_value(locality.lb_weight);
      endpoints->set_priority(locality.priority);
      endpoints->mutable_locality()->set_region(kDefaultLocalityRegion);
      endpoints->mutable_locality()->set_zone(kDefaultLocalityZone);
      endpoints->mutable_locality()->set_sub_zone(locality.sub_zone);
      for (size_t i = 0; i < locality.ports.size(); ++i) {
        const int& port = locality.ports[i];
        auto* lb_endpoints = endpoints->add_lb_endpoints();
        if (locality.health_statuses.size() > i &&
            locality.health_statuses[i] !=
                envoy::api::v2::HealthStatus::UNKNOWN) {
          lb_endpoints->set_health_status(locality.health_statuses[i]);
        }
        auto* endpoint = lb_endpoints->mutable_endpoint();
        auto* address = endpoint->mutable_address();
        auto* socket_address = address->mutable_socket_address();
        socket_address->set_address("127.0.0.1");
        socket_address->set_port_value(port);
      }
    }
    if (!args.drop_categories.empty()) {
      auto* policy = assignment.mutable_policy();
      for (const auto& p : args.drop_categories) {
        const grpc::string& name = p.first;
        const uint32_t parts_per_million = p.second;
        auto* drop_overload = policy->add_drop_overloads();
        drop_overload->set_category(name);
        auto* drop_percentage = drop_overload->mutable_drop_percentage();
        drop_percentage->set_numerator(parts_per_million);
        drop_percentage->set_denominator(args.drop_denominator);
      }
    }
    DiscoveryResponse response;
    response.set_type_url(kEdsTypeUrl);
    response.add_resources()->PackFrom(assignment);
    return response;
  }

  void NotifyDoneWithAdsCall() {
    grpc_core::MutexLock lock(&ads_mu_);
    NotifyDoneWithAdsCallLocked();
  }

  void NotifyDoneWithAdsCallLocked() {
    if (!ads_done_) {
      ads_done_ = true;
      ads_cond_.Broadcast();
    }
  }

 private:
  grpc_core::CondVar ads_cond_;
  // Protect the members below.
  grpc_core::Mutex ads_mu_;
  bool ads_done_ = false;
  Listener default_listener_;
  ResponseState lds_response_state_ = NOT_SENT;
  bool lds_ignore_ = false;
  RouteConfiguration default_route_config_;
  ResponseState rds_response_state_ = NOT_SENT;
  bool rds_ignore_ = false;
  Cluster default_cluster_;
  ResponseState cds_response_state_ = NOT_SENT;
  bool cds_ignore_ = false;
  bool eds_ignore_ = false;
  // An instance data member containing the current state of all resources.
  // Note that an entry will exist whenever either of the following is true:
  // - The resource exists (i.e., has been created by SetResource() and has not
  //   yet been destroyed by UnsetResource()).
  // - There is at least one subscriber for the resource.
  ResourcesMap resources_map_;
};

class LrsServiceImpl : public LrsService {
 public:
  using Stream = ServerReaderWriter<LoadStatsResponse, LoadStatsRequest>;

  explicit LrsServiceImpl(int client_load_reporting_interval_seconds)
      : client_load_reporting_interval_seconds_(
            client_load_reporting_interval_seconds) {}

  Status StreamLoadStats(ServerContext* /*context*/, Stream* stream) override {
    gpr_log(GPR_INFO, "LRS[%p]: StreamLoadStats starts", this);
    // Read request.
    LoadStatsRequest request;
    if (stream->Read(&request)) {
      if (client_load_reporting_interval_seconds_ > 0) {
        IncreaseRequestCount();
        // Send response.
        LoadStatsResponse response;
        auto server_name = request.cluster_stats()[0].cluster_name();
        GPR_ASSERT(server_name != "");
        response.add_clusters(server_name);
        response.mutable_load_reporting_interval()->set_seconds(
            client_load_reporting_interval_seconds_);
        stream->Write(response);
        IncreaseResponseCount();
        // Wait for report.
        request.Clear();
        if (stream->Read(&request)) {
          gpr_log(GPR_INFO, "LRS[%p]: received client load report message '%s'",
                  this, request.DebugString().c_str());
          GPR_ASSERT(request.cluster_stats().size() == 1);
          const ClusterStats& cluster_stats = request.cluster_stats()[0];
          // We need to acquire the lock here in order to prevent the notify_one
          // below from firing before its corresponding wait is executed.
          grpc_core::MutexLock lock(&load_report_mu_);
          GPR_ASSERT(client_stats_ == nullptr);
          client_stats_.reset(new ClientStats(cluster_stats));
          load_report_ready_ = true;
          load_report_cond_.Signal();
        }
      }
      // Wait until notified done.
      grpc_core::MutexLock lock(&lrs_mu_);
      lrs_cv_.WaitUntil(&lrs_mu_, [this] { return lrs_done; });
    }
    gpr_log(GPR_INFO, "LRS[%p]: StreamLoadStats done", this);
    return Status::OK;
  }

  void Start() {
    lrs_done = false;
    load_report_ready_ = false;
    client_stats_.reset();
  }

  void Shutdown() {
    {
      grpc_core::MutexLock lock(&lrs_mu_);
      NotifyDoneWithLrsCallLocked();
    }
    gpr_log(GPR_INFO, "LRS[%p]: shut down", this);
  }

  ClientStats* WaitForLoadReport() {
    grpc_core::MutexLock lock(&load_report_mu_);
    load_report_cond_.WaitUntil(&load_report_mu_,
                                [this] { return load_report_ready_; });
    load_report_ready_ = false;
    return client_stats_.get();
  }

  void NotifyDoneWithLrsCall() {
    grpc_core::MutexLock lock(&lrs_mu_);
    NotifyDoneWithLrsCallLocked();
  }

  void NotifyDoneWithLrsCallLocked() {
    if (!lrs_done) {
      lrs_done = true;
      lrs_cv_.Broadcast();
    }
  }

 private:
  const int client_load_reporting_interval_seconds_;

  grpc_core::CondVar lrs_cv_;
  // Protect lrs_done.
  grpc_core::Mutex lrs_mu_;
  bool lrs_done = false;

  grpc_core::CondVar load_report_cond_;
  // Protect the members below.
  grpc_core::Mutex load_report_mu_;
  std::unique_ptr<ClientStats> client_stats_;
  bool load_report_ready_ = false;
};

class TestType {
 public:
  TestType(bool use_xds_resolver, bool enable_load_reporting)
      : use_xds_resolver_(use_xds_resolver),
        enable_load_reporting_(enable_load_reporting) {}

  bool use_xds_resolver() const { return use_xds_resolver_; }
  bool enable_load_reporting() const { return enable_load_reporting_; }

  grpc::string AsString() const {
    grpc::string retval = (use_xds_resolver_ ? "XdsResolver" : "FakeResolver");
    if (enable_load_reporting_) retval += "WithLoadReporting";
    return retval;
  }

 private:
  const bool use_xds_resolver_;
  const bool enable_load_reporting_;
};

class XdsEnd2endTest : public ::testing::TestWithParam<TestType> {
 protected:
  XdsEnd2endTest(size_t num_backends, size_t num_balancers,
                 int client_load_reporting_interval_seconds = 100)
      : server_host_("localhost"),
        num_backends_(num_backends),
        num_balancers_(num_balancers),
        client_load_reporting_interval_seconds_(
            client_load_reporting_interval_seconds) {}

  static void SetUpTestCase() {
    // Make the backup poller poll very frequently in order to pick up
    // updates from all the subchannels's FDs.
    GPR_GLOBAL_CONFIG_SET(grpc_client_channel_backup_poll_interval_ms, 1);
#if TARGET_OS_IPHONE
    // Workaround Apple CFStream bug
    gpr_setenv("grpc_cfstream", "0");
#endif
    grpc_init();
  }

  static void TearDownTestCase() { grpc_shutdown(); }

  void SetUp() override {
    gpr_setenv("GRPC_XDS_BOOTSTRAP", g_bootstrap_file);
    g_port_saver->Reset();
    response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
    lb_channel_response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
    // Start the backends.
    for (size_t i = 0; i < num_backends_; ++i) {
      backends_.emplace_back(new BackendServerThread);
      backends_.back()->Start(server_host_);
    }
    // Start the load balancers.
    for (size_t i = 0; i < num_balancers_; ++i) {
      balancers_.emplace_back(
          new BalancerServerThread(GetParam().enable_load_reporting()
                                       ? client_load_reporting_interval_seconds_
                                       : 0));
      balancers_.back()->Start(server_host_);
    }
    ResetStub();
  }

  void TearDown() override {
    ShutdownAllBackends();
    for (auto& balancer : balancers_) balancer->Shutdown();
  }

  void StartAllBackends() {
    for (auto& backend : backends_) backend->Start(server_host_);
  }

  void StartBackend(size_t index) { backends_[index]->Start(server_host_); }

  void ShutdownAllBackends() {
    for (auto& backend : backends_) backend->Shutdown();
  }

  void ShutdownBackend(size_t index) { backends_[index]->Shutdown(); }

  void ResetStub(int fallback_timeout = 0, int failover_timeout = 0,
                 const grpc::string& expected_targets = "",
                 int xds_resource_does_not_exist_timeout = 0) {
    ChannelArguments args;
    // TODO(juanlishen): Add setter to ChannelArguments.
    if (fallback_timeout > 0) {
      args.SetInt(GRPC_ARG_XDS_FALLBACK_TIMEOUT_MS, fallback_timeout);
    }
    if (failover_timeout > 0) {
      args.SetInt(GRPC_ARG_XDS_FAILOVER_TIMEOUT_MS, failover_timeout);
    }
    if (xds_resource_does_not_exist_timeout > 0) {
      args.SetInt(GRPC_ARG_XDS_RESOURCE_DOES_NOT_EXIST_TIMEOUT_MS,
                  xds_resource_does_not_exist_timeout);
    }
    // If the parent channel is using the fake resolver, we inject the
    // response generator for the parent here, and then SetNextResolution()
    // will inject the xds channel's response generator via the parent's
    // response generator.
    //
    // In contrast, if we are using the xds resolver, then the parent
    // channel never uses a response generator, and we inject the xds
    // channel's response generator here.
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    GetParam().use_xds_resolver()
                        ? lb_channel_response_generator_.get()
                        : response_generator_.get());
    if (!expected_targets.empty()) {
      args.SetString(GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS, expected_targets);
    }
    grpc::string scheme =
        GetParam().use_xds_resolver() ? "xds-experimental" : "fake";
    std::ostringstream uri;
    uri << scheme << ":///" << kApplicationTargetName_;
    // TODO(dgq): templatize tests to run everything using both secure and
    // insecure channel credentials.
    grpc_channel_credentials* channel_creds =
        grpc_fake_transport_security_credentials_create();
    grpc_call_credentials* call_creds = grpc_md_only_test_credentials_create(
        g_kCallCredsMdKey, g_kCallCredsMdValue, false);
    std::shared_ptr<ChannelCredentials> creds(
        new SecureChannelCredentials(grpc_composite_channel_credentials_create(
            channel_creds, call_creds, nullptr)));
    call_creds->Unref();
    channel_creds->Unref();
    channel_ = ::grpc::CreateCustomChannel(uri.str(), creds, args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  void ResetBackendCounters() {
    for (auto& backend : backends_) backend->backend_service()->ResetCounters();
  }

  bool SeenAllBackends(size_t start_index = 0, size_t stop_index = 0) {
    if (stop_index == 0) stop_index = backends_.size();
    for (size_t i = start_index; i < stop_index; ++i) {
      if (backends_[i]->backend_service()->request_count() == 0) return false;
    }
    return true;
  }

  void SendRpcAndCount(int* num_total, int* num_ok, int* num_failure,
                       int* num_drops) {
    const Status status = SendRpc();
    if (status.ok()) {
      ++*num_ok;
    } else {
      if (status.error_message() == "Call dropped by load balancing policy") {
        ++*num_drops;
      } else {
        ++*num_failure;
      }
    }
    ++*num_total;
  }

  std::tuple<int, int, int> WaitForAllBackends(size_t start_index = 0,
                                               size_t stop_index = 0) {
    int num_ok = 0;
    int num_failure = 0;
    int num_drops = 0;
    int num_total = 0;
    while (!SeenAllBackends(start_index, stop_index)) {
      SendRpcAndCount(&num_total, &num_ok, &num_failure, &num_drops);
    }
    ResetBackendCounters();
    gpr_log(GPR_INFO,
            "Performed %d warm up requests against the backends. "
            "%d succeeded, %d failed, %d dropped.",
            num_total, num_ok, num_failure, num_drops);
    return std::make_tuple(num_ok, num_failure, num_drops);
  }

  void WaitForBackend(size_t backend_idx, bool reset_counters = true) {
    gpr_log(GPR_INFO, "========= WAITING FOR BACKEND %lu ==========",
            static_cast<unsigned long>(backend_idx));
    do {
      (void)SendRpc();
    } while (backends_[backend_idx]->backend_service()->request_count() == 0);
    if (reset_counters) ResetBackendCounters();
    gpr_log(GPR_INFO, "========= BACKEND %lu READY ==========",
            static_cast<unsigned long>(backend_idx));
  }

  grpc_core::ServerAddressList CreateAddressListFromPortList(
      const std::vector<int>& ports) {
    grpc_core::ServerAddressList addresses;
    for (int port : ports) {
      char* lb_uri_str;
      gpr_asprintf(&lb_uri_str, "ipv4:127.0.0.1:%d", port);
      grpc_uri* lb_uri = grpc_uri_parse(lb_uri_str, true);
      GPR_ASSERT(lb_uri != nullptr);
      grpc_resolved_address address;
      GPR_ASSERT(grpc_parse_uri(lb_uri, &address));
      addresses.emplace_back(address.addr, address.len, nullptr);
      grpc_uri_destroy(lb_uri);
      gpr_free(lb_uri_str);
    }
    return addresses;
  }

  void SetNextResolution(const std::vector<int>& ports,
                         grpc_core::FakeResolverResponseGenerator*
                             lb_channel_response_generator = nullptr) {
    if (GetParam().use_xds_resolver()) return;  // Not used with xds resolver.
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(ports);
    grpc_error* error = GRPC_ERROR_NONE;
    const char* service_config_json =
        GetParam().enable_load_reporting()
            ? kDefaultServiceConfig_
            : kDefaultServiceConfigWithoutLoadReporting_;
    result.service_config =
        grpc_core::ServiceConfig::Create(service_config_json, &error);
    GRPC_ERROR_UNREF(error);
    grpc_arg arg = grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
        lb_channel_response_generator == nullptr
            ? lb_channel_response_generator_.get()
            : lb_channel_response_generator);
    result.args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
    response_generator_->SetResponse(std::move(result));
  }

  void SetNextResolutionForLbChannelAllBalancers(
      const char* service_config_json = nullptr,
      grpc_core::FakeResolverResponseGenerator* lb_channel_response_generator =
          nullptr) {
    std::vector<int> ports;
    for (size_t i = 0; i < balancers_.size(); ++i) {
      ports.emplace_back(balancers_[i]->port());
    }
    SetNextResolutionForLbChannel(ports, service_config_json,
                                  lb_channel_response_generator);
  }

  void SetNextResolutionForLbChannel(
      const std::vector<int>& ports, const char* service_config_json = nullptr,
      grpc_core::FakeResolverResponseGenerator* lb_channel_response_generator =
          nullptr) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(ports);
    if (service_config_json != nullptr) {
      grpc_error* error = GRPC_ERROR_NONE;
      result.service_config =
          grpc_core::ServiceConfig::Create(service_config_json, &error);
      GRPC_ERROR_UNREF(error);
    }
    if (lb_channel_response_generator == nullptr) {
      lb_channel_response_generator = lb_channel_response_generator_.get();
    }
    lb_channel_response_generator->SetResponse(std::move(result));
  }

  void SetNextReresolutionResponse(const std::vector<int>& ports) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(ports);
    response_generator_->SetReresolutionResponse(std::move(result));
  }

  const std::vector<int> GetBackendPorts(size_t start_index = 0,
                                         size_t stop_index = 0) const {
    if (stop_index == 0) stop_index = backends_.size();
    std::vector<int> backend_ports;
    for (size_t i = start_index; i < stop_index; ++i) {
      backend_ports.push_back(backends_[i]->port());
    }
    return backend_ports;
  }

  Status SendRpc(EchoResponse* response = nullptr, int timeout_ms = 1000,
                 bool wait_for_ready = false) {
    const bool local_response = (response == nullptr);
    if (local_response) response = new EchoResponse;
    EchoRequest request;
    request.set_message(kRequestMessage_);
    ClientContext context;
    context.set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
    if (wait_for_ready) context.set_wait_for_ready(true);
    Status status = stub_->Echo(&context, request, response);
    if (local_response) delete response;
    return status;
  }

  void CheckRpcSendOk(const size_t times = 1, const int timeout_ms = 1000,
                      bool wait_for_ready = false) {
    for (size_t i = 0; i < times; ++i) {
      EchoResponse response;
      const Status status = SendRpc(&response, timeout_ms, wait_for_ready);
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
  }

  void CheckRpcSendFailure() {
    const Status status = SendRpc();
    EXPECT_FALSE(status.ok());
  }

  class ServerThread {
   public:
    ServerThread() : port_(g_port_saver->GetPort()) {}
    virtual ~ServerThread(){};

    void Start(const grpc::string& server_host) {
      gpr_log(GPR_INFO, "starting %s server on port %d", Type(), port_);
      GPR_ASSERT(!running_);
      running_ = true;
      StartAllServices();
      grpc_core::Mutex mu;
      // We need to acquire the lock here in order to prevent the notify_one
      // by ServerThread::Serve from firing before the wait below is hit.
      grpc_core::MutexLock lock(&mu);
      grpc_core::CondVar cond;
      thread_.reset(new std::thread(
          std::bind(&ServerThread::Serve, this, server_host, &mu, &cond)));
      cond.Wait(&mu);
      gpr_log(GPR_INFO, "%s server startup complete", Type());
    }

    void Serve(const grpc::string& server_host, grpc_core::Mutex* mu,
               grpc_core::CondVar* cond) {
      // We need to acquire the lock here in order to prevent the notify_one
      // below from firing before its corresponding wait is executed.
      grpc_core::MutexLock lock(mu);
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      std::shared_ptr<ServerCredentials> creds(new SecureServerCredentials(
          grpc_fake_transport_security_server_credentials_create()));
      builder.AddListeningPort(server_address.str(), creds);
      RegisterAllServices(&builder);
      server_ = builder.BuildAndStart();
      cond->Signal();
    }

    void Shutdown() {
      if (!running_) return;
      gpr_log(GPR_INFO, "%s about to shutdown", Type());
      ShutdownAllServices();
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      thread_->join();
      gpr_log(GPR_INFO, "%s shutdown completed", Type());
      running_ = false;
    }

    int port() const { return port_; }

   private:
    virtual void RegisterAllServices(ServerBuilder* builder) = 0;
    virtual void StartAllServices() = 0;
    virtual void ShutdownAllServices() = 0;

    virtual const char* Type() = 0;

    const int port_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<std::thread> thread_;
    bool running_ = false;
  };

  class BackendServerThread : public ServerThread {
   public:
    BackendServiceImpl* backend_service() { return &backend_service_; }

   private:
    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(&backend_service_);
    }

    void StartAllServices() override { backend_service_.Start(); }

    void ShutdownAllServices() override { backend_service_.Shutdown(); }

    const char* Type() override { return "Backend"; }

    BackendServiceImpl backend_service_;
  };

  class BalancerServerThread : public ServerThread {
   public:
    explicit BalancerServerThread(int client_load_reporting_interval = 0)
        : ads_service_(client_load_reporting_interval > 0),
          lrs_service_(client_load_reporting_interval) {}

    AdsServiceImpl* ads_service() { return &ads_service_; }
    LrsServiceImpl* lrs_service() { return &lrs_service_; }

   private:
    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(&ads_service_);
      builder->RegisterService(&lrs_service_);
    }

    void StartAllServices() override {
      ads_service_.Start();
      lrs_service_.Start();
    }

    void ShutdownAllServices() override {
      ads_service_.Shutdown();
      lrs_service_.Shutdown();
    }

    const char* Type() override { return "Balancer"; }

    AdsServiceImpl ads_service_;
    LrsServiceImpl lrs_service_;
  };

  const grpc::string server_host_;
  const size_t num_backends_;
  const size_t num_balancers_;
  const int client_load_reporting_interval_seconds_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::vector<std::unique_ptr<BackendServerThread>> backends_;
  std::vector<std::unique_ptr<BalancerServerThread>> balancers_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      lb_channel_response_generator_;
  const grpc::string kRequestMessage_ = "Live long and prosper.";
  const grpc::string kApplicationTargetName_ = kDefaultResourceName;
  const char* kDefaultServiceConfig_ =
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"does_not_exist\":{} },\n"
      "    { \"xds_experimental\":{\n"
      "      \"lrsLoadReportingServerName\": \"\"\n"
      "    } }\n"
      "  ]\n"
      "}";
  const char* kDefaultServiceConfigWithoutLoadReporting_ =
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"does_not_exist\":{} },\n"
      "    { \"xds_experimental\":{\n"
      "    } }\n"
      "  ]\n"
      "}";
};

class BasicTest : public XdsEnd2endTest {
 public:
  BasicTest() : XdsEnd2endTest(4, 1) {}
};

// Tests that the balancer sends the correct response to the client, and the
// client sends RPCs to the backends using the default child policy.
TEST_P(BasicTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcsPerAddress = 100;
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts()},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * num_backends_);
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
  // Check LB policy name for the channel.
  EXPECT_EQ(
      (GetParam().use_xds_resolver() ? "cds_experimental" : "xds_experimental"),
      channel_->GetLoadBalancingPolicyName());
}

TEST_P(BasicTest, IgnoresUnhealthyEndpoints) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcsPerAddress = 100;
  AdsServiceImpl::ResponseArgs args({
      {"locality0",
       GetBackendPorts(),
       kDefaultLocalityWeight,
       kDefaultLocalityPriority,
       {envoy::api::v2::HealthStatus::DRAINING}},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends(/*start_index=*/1);
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * (num_backends_ - 1));
  // Each backend should have gotten 100 requests.
  for (size_t i = 1; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

// Tests that subchannel sharing works when the same backend is listed multiple
// times.
TEST_P(BasicTest, SameBackendListedMultipleTimes) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Same backend listed twice.
  std::vector<int> ports(2, backends_[0]->port());
  AdsServiceImpl::ResponseArgs args({
      {"locality0", ports},
  });
  const size_t kNumRpcsPerAddress = 10;
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // We need to wait for the backend to come online.
  WaitForBackend(0);
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * ports.size());
  // Backend should have gotten 20 requests.
  EXPECT_EQ(kNumRpcsPerAddress * ports.size(),
            backends_[0]->backend_service()->request_count());
  // And they should have come from a single client port, because of
  // subchannel sharing.
  EXPECT_EQ(1UL, backends_[0]->backend_service()->clients().size());
}

// Tests that RPCs will be blocked until a non-empty serverlist is received.
TEST_P(BasicTest, InitiallyEmptyServerlist) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const int kServerlistDelayMs = 500 * grpc_test_slowdown_factor();
  const int kCallDeadlineMs = kServerlistDelayMs * 2;
  // First response is an empty serverlist, sent right away.
  AdsServiceImpl::ResponseArgs::Locality empty_locality("locality0", {});
  AdsServiceImpl::ResponseArgs args({
      empty_locality,
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Send non-empty serverlist only after kServerlistDelayMs.
  args = AdsServiceImpl::ResponseArgs({
      {"locality0", GetBackendPorts()},
  });
  std::thread delayed_resource_setter(
      std::bind(&AdsServiceImpl::SetEdsResponse, balancers_[0]->ads_service(),
                AdsServiceImpl::BuildResponse(args), kServerlistDelayMs,
                kDefaultResourceName));
  const auto t0 = system_clock::now();
  // Client will block: LB will initially send empty serverlist.
  CheckRpcSendOk(1, kCallDeadlineMs, true /* wait_for_ready */);
  const auto ellapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          system_clock::now() - t0);
  // but eventually, the LB sends a serverlist update that allows the call to
  // proceed. The call delay must be larger than the delay in sending the
  // populated serverlist but under the call's deadline (which is enforced by
  // the call's deadline).
  EXPECT_GT(ellapsed_ms.count(), kServerlistDelayMs);
  // The ADS service got a single request.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  // and sent two responses.
  EXPECT_EQ(2U, balancers_[0]->ads_service()->response_count());
  delayed_resource_setter.join();
}

// Tests that RPCs will fail with UNAVAILABLE instead of DEADLINE_EXCEEDED if
// all the servers are unreachable.
TEST_P(BasicTest, AllServersUnreachableFailFast) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumUnreachableServers = 5;
  std::vector<int> ports;
  for (size_t i = 0; i < kNumUnreachableServers; ++i) {
    ports.push_back(g_port_saver->GetPort());
  }
  AdsServiceImpl::ResponseArgs args({
      {"locality0", ports},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  const Status status = SendRpc();
  // The error shouldn't be DEADLINE_EXCEEDED.
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

// Tests that RPCs fail when the backends are down, and will succeed again after
// the backends are restarted.
TEST_P(BasicTest, BackendsRestart) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts()},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  WaitForAllBackends();
  // Stop backends.  RPCs should fail.
  ShutdownAllBackends();
  CheckRpcSendFailure();
  // Restart all backends.  RPCs should start succeeding again.
  StartAllBackends();
  CheckRpcSendOk(1 /* times */, 2000 /* timeout_ms */,
                 true /* wait_for_ready */);
}

using SecureNamingTest = BasicTest;

// Tests that secure naming check passes if target name is expected.
TEST_P(SecureNamingTest, TargetNameIsExpected) {
  // TODO(juanlishen): Use separate fake creds for the balancer channel.
  ResetStub(0, 0, kApplicationTargetName_ + ";lb");
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  const size_t kNumRpcsPerAddress = 100;
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts()},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * num_backends_);
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

// Tests that secure naming check fails if target name is unexpected.
TEST_P(SecureNamingTest, TargetNameIsUnexpected) {
  gpr_setenv("GRPC_XDS_BOOTSTRAP", g_bootstrap_file_bad);
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  // Make sure that we blow up (via abort() from the security connector) when
  // the name from the balancer doesn't match expectations.
  ASSERT_DEATH_IF_SUPPORTED(
      {
        ResetStub(0, 0, kApplicationTargetName_ + ";lb");
        SetNextResolution({});
        SetNextResolutionForLbChannel({balancers_[0]->port()});
        channel_->WaitForConnected(grpc_timeout_seconds_to_deadline(1));
      },
      "");
}

using LdsTest = BasicTest;

// Tests that LDS client should send an ACK upon correct LDS response (with
// inlined RDS result).
TEST_P(LdsTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state(),
            AdsServiceImpl::ACKED);
}

// Tests that LDS client should send a NACK if there is no API listener in the
// Listener in the LDS response.
TEST_P(LdsTest, NoApiListener) {
  auto listener = balancers_[0]->ads_service()->default_listener();
  listener.clear_api_listener();
  balancers_[0]->ads_service()->SetLdsResponse(listener);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that LDS client should send a NACK if the route_specifier in the
// http_connection_manager is neither inlined route_config nor RDS.
TEST_P(LdsTest, WrongRouteSpecifier) {
  auto listener = balancers_[0]->ads_service()->default_listener();
  HttpConnectionManager http_connection_manager;
  http_connection_manager.mutable_scoped_routes();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancers_[0]->ads_service()->SetLdsResponse(listener);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that LDS client should send a NACK if matching domain can't be found in
// the LDS response.
TEST_P(LdsTest, NoMatchedDomain) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  balancers_[0]->ads_service()->SetLdsResponse(
      AdsServiceImpl::BuildListener(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that LDS client should choose the virtual host with matching domain if
// multiple virtual hosts exist in the LDS response.
TEST_P(LdsTest, ChooseMatchedDomain) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  *(route_config.add_virtual_hosts()) = route_config.virtual_hosts(0);
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->mutable_cluster_header();
  balancers_[0]->ads_service()->SetLdsResponse(
      AdsServiceImpl::BuildListener(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state(),
            AdsServiceImpl::ACKED);
}

// Tests that LDS client should choose the last route in the virtual host if
// multiple routes exist in the LDS response.
TEST_P(LdsTest, ChooseLastRoute) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  *(route_config.mutable_virtual_hosts(0)->add_routes()) =
      route_config.virtual_hosts(0).routes(0);
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->mutable_cluster_header();
  balancers_[0]->ads_service()->SetLdsResponse(
      AdsServiceImpl::BuildListener(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state(),
            AdsServiceImpl::ACKED);
}

// Tests that LDS client should send a NACK if route match has non-empty prefix
// in the LDS response.
TEST_P(LdsTest, RouteMatchHasNonemptyPrefix) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_match()
      ->set_prefix("nonempty_prefix");
  balancers_[0]->ads_service()->SetLdsResponse(
      AdsServiceImpl::BuildListener(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that LDS client should send a NACK if route has an action other than
// RouteAction in the LDS response.
TEST_P(LdsTest, RouteHasNoRouteAction) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)->mutable_routes(0)->mutable_redirect();
  balancers_[0]->ads_service()->SetLdsResponse(
      AdsServiceImpl::BuildListener(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that LDS client should send a NACK if RouteAction has a
// cluster_specifier other than cluster in the LDS response.
TEST_P(LdsTest, RouteActionHasNoCluster) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->mutable_cluster_header();
  balancers_[0]->ads_service()->SetLdsResponse(
      AdsServiceImpl::BuildListener(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that LDS client times out when no response received.
TEST_P(LdsTest, Timeout) {
  ResetStub(0, 0, "", 500);
  balancers_[0]->ads_service()->set_lds_ignore();
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

using RdsTest = BasicTest;

// Tests that RDS client should send an ACK upon correct RDS response.
TEST_P(RdsTest, Vanilla) {
  balancers_[0]->ads_service()->SetLdsToUseDynamicRds();
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(balancers_[0]->ads_service()->rds_response_state(),
            AdsServiceImpl::ACKED);
}

// Tests that RDS client should send a NACK if matching domain can't be found in
// the RDS response.
TEST_P(RdsTest, NoMatchedDomain) {
  balancers_[0]->ads_service()->SetLdsToUseDynamicRds();
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  balancers_[0]->ads_service()->SetRdsResponse(std::move(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->rds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that RDS client should choose the virtual host with matching domain if
// multiple virtual hosts exist in the RDS response.
TEST_P(RdsTest, ChooseMatchedDomain) {
  balancers_[0]->ads_service()->SetLdsToUseDynamicRds();
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  *(route_config.add_virtual_hosts()) = route_config.virtual_hosts(0);
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->mutable_cluster_header();
  balancers_[0]->ads_service()->SetRdsResponse(std::move(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(balancers_[0]->ads_service()->rds_response_state(),
            AdsServiceImpl::ACKED);
}

// Tests that RDS client should choose the last route in the virtual host if
// multiple routes exist in the RDS response.
TEST_P(RdsTest, ChooseLastRoute) {
  balancers_[0]->ads_service()->SetLdsToUseDynamicRds();
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  *(route_config.mutable_virtual_hosts(0)->add_routes()) =
      route_config.virtual_hosts(0).routes(0);
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->mutable_cluster_header();
  balancers_[0]->ads_service()->SetRdsResponse(std::move(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(balancers_[0]->ads_service()->rds_response_state(),
            AdsServiceImpl::ACKED);
}

// Tests that RDS client should send a NACK if route match has non-empty prefix
// in the RDS response.
TEST_P(RdsTest, RouteMatchHasNonemptyPrefix) {
  balancers_[0]->ads_service()->SetLdsToUseDynamicRds();
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_match()
      ->set_prefix("nonempty_prefix");
  balancers_[0]->ads_service()->SetRdsResponse(std::move(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->rds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that RDS client should send a NACK if route has an action other than
// RouteAction in the RDS response.
TEST_P(RdsTest, RouteHasNoRouteAction) {
  balancers_[0]->ads_service()->SetLdsToUseDynamicRds();
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)->mutable_routes(0)->mutable_redirect();
  balancers_[0]->ads_service()->SetRdsResponse(std::move(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->rds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that RDS client should send a NACK if RouteAction has a
// cluster_specifier other than cluster in the RDS response.
TEST_P(RdsTest, RouteActionHasNoCluster) {
  balancers_[0]->ads_service()->SetLdsToUseDynamicRds();
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->mutable_cluster_header();
  balancers_[0]->ads_service()->SetRdsResponse(std::move(route_config));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->rds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that RDS client times out when no response received.
TEST_P(RdsTest, Timeout) {
  ResetStub(0, 0, "", 500);
  balancers_[0]->ads_service()->SetLdsToUseDynamicRds();
  balancers_[0]->ads_service()->set_rds_ignore();
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

using CdsTest = BasicTest;

// Tests that CDS client should send an ACK upon correct CDS response.
TEST_P(CdsTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state(),
            AdsServiceImpl::ACKED);
}

// Tests that CDS client should send a NACK if the cluster type in CDS response
// is other than EDS.
TEST_P(CdsTest, WrongClusterType) {
  auto cluster = balancers_[0]->ads_service()->default_cluster();
  cluster.set_type(envoy::api::v2::Cluster::STATIC);
  balancers_[0]->ads_service()->SetCdsResponse(std::move(cluster));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that CDS client should send a NACK if the eds_config in CDS response is
// other than ADS.
TEST_P(CdsTest, WrongEdsConfig) {
  auto cluster = balancers_[0]->ads_service()->default_cluster();
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  balancers_[0]->ads_service()->SetCdsResponse(std::move(cluster));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that CDS client should send a NACK if the lb_policy in CDS response is
// other than ROUND_ROBIN.
TEST_P(CdsTest, WrongLbPolicy) {
  auto cluster = balancers_[0]->ads_service()->default_cluster();
  cluster.set_lb_policy(envoy::api::v2::Cluster::LEAST_REQUEST);
  balancers_[0]->ads_service()->SetCdsResponse(std::move(cluster));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that CDS client should send a NACK if the lrs_server in CDS response is
// other than SELF.
TEST_P(CdsTest, WrongLrsServer) {
  auto cluster = balancers_[0]->ads_service()->default_cluster();
  cluster.mutable_lrs_server()->mutable_ads();
  balancers_[0]->ads_service()->SetCdsResponse(std::move(cluster));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state(),
            AdsServiceImpl::NACKED);
}

// Tests that CDS client times out when no response received.
TEST_P(CdsTest, Timeout) {
  ResetStub(0, 0, "", 500);
  balancers_[0]->ads_service()->set_cds_ignore();
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

using EdsTest = BasicTest;

// TODO(roth): Add tests showing that RPCs fail when EDS data is invalid.

TEST_P(EdsTest, Timeout) {
  ResetStub(0, 0, "", 500);
  balancers_[0]->ads_service()->set_eds_ignore();
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

using LocalityMapTest = BasicTest;

// Tests that the localities in a locality map are picked according to their
// weights.
TEST_P(LocalityMapTest, WeightedRoundRobin) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 5000;
  const int kLocalityWeight0 = 2;
  const int kLocalityWeight1 = 8;
  const int kTotalLocalityWeight = kLocalityWeight0 + kLocalityWeight1;
  const double kLocalityWeightRate0 =
      static_cast<double>(kLocalityWeight0) / kTotalLocalityWeight;
  const double kLocalityWeightRate1 =
      static_cast<double>(kLocalityWeight1) / kTotalLocalityWeight;
  // ADS response contains 2 localities, each of which contains 1 backend.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts(0, 1), kLocalityWeight0},
      {"locality1", GetBackendPorts(1, 2), kLocalityWeight1},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Wait for both backends to be ready.
  WaitForAllBackends(0, 2);
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(kNumRpcs);
  // The locality picking rates should be roughly equal to the expectation.
  const double locality_picked_rate_0 =
      static_cast<double>(backends_[0]->backend_service()->request_count()) /
      kNumRpcs;
  const double locality_picked_rate_1 =
      static_cast<double>(backends_[1]->backend_service()->request_count()) /
      kNumRpcs;
  const double kErrorTolerance = 0.2;
  EXPECT_THAT(locality_picked_rate_0,
              ::testing::AllOf(
                  ::testing::Ge(kLocalityWeightRate0 * (1 - kErrorTolerance)),
                  ::testing::Le(kLocalityWeightRate0 * (1 + kErrorTolerance))));
  EXPECT_THAT(locality_picked_rate_1,
              ::testing::AllOf(
                  ::testing::Ge(kLocalityWeightRate1 * (1 - kErrorTolerance)),
                  ::testing::Le(kLocalityWeightRate1 * (1 + kErrorTolerance))));
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

// Tests that the locality map can work properly even when it contains a large
// number of localities.
TEST_P(LocalityMapTest, StressTest) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumLocalities = 100;
  // The first ADS response contains kNumLocalities localities, each of which
  // contains backend 0.
  AdsServiceImpl::ResponseArgs args;
  for (size_t i = 0; i < kNumLocalities; ++i) {
    grpc::string name = "locality" + std::to_string(i);
    AdsServiceImpl::ResponseArgs::Locality locality(name,
                                                    {backends_[0]->port()});
    args.locality_list.emplace_back(std::move(locality));
  }
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // The second ADS response contains 1 locality, which contains backend 1.
  args = AdsServiceImpl::ResponseArgs({
      {"locality0", GetBackendPorts(1, 2)},
  });
  std::thread delayed_resource_setter(std::bind(
      &AdsServiceImpl::SetEdsResponse, balancers_[0]->ads_service(),
      AdsServiceImpl::BuildResponse(args), 60 * 1000, kDefaultResourceName));
  // Wait until backend 0 is ready, before which kNumLocalities localities are
  // received and handled by the xds policy.
  WaitForBackend(0, /*reset_counters=*/false);
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  // Wait until backend 1 is ready, before which kNumLocalities localities are
  // removed by the xds policy.
  WaitForBackend(1);
  // The ADS service got a single request.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  // and sent two responses.
  EXPECT_EQ(2U, balancers_[0]->ads_service()->response_count());
  delayed_resource_setter.join();
}

// Tests that the localities in a locality map are picked correctly after update
// (addition, modification, deletion).
TEST_P(LocalityMapTest, UpdateMap) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 1000;
  // The locality weight for the first 3 localities.
  const std::vector<int> kLocalityWeights0 = {2, 3, 4};
  const double kTotalLocalityWeight0 =
      std::accumulate(kLocalityWeights0.begin(), kLocalityWeights0.end(), 0);
  std::vector<double> locality_weight_rate_0;
  for (int weight : kLocalityWeights0) {
    locality_weight_rate_0.push_back(weight / kTotalLocalityWeight0);
  }
  // Delete the first locality, keep the second locality, change the third
  // locality's weight from 4 to 2, and add a new locality with weight 6.
  const std::vector<int> kLocalityWeights1 = {3, 2, 6};
  const double kTotalLocalityWeight1 =
      std::accumulate(kLocalityWeights1.begin(), kLocalityWeights1.end(), 0);
  std::vector<double> locality_weight_rate_1 = {
      0 /* placeholder for locality 0 */};
  for (int weight : kLocalityWeights1) {
    locality_weight_rate_1.push_back(weight / kTotalLocalityWeight1);
  }
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts(0, 1), 2},
      {"locality1", GetBackendPorts(1, 2), 3},
      {"locality2", GetBackendPorts(2, 3), 4},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  args = AdsServiceImpl::ResponseArgs({
      {"locality1", GetBackendPorts(1, 2), 3},
      {"locality2", GetBackendPorts(2, 3), 2},
      {"locality3", GetBackendPorts(3, 4), 6},
  });
  std::thread delayed_resource_setter(std::bind(
      &AdsServiceImpl::SetEdsResponse, balancers_[0]->ads_service(),
      AdsServiceImpl::BuildResponse(args), 5000, kDefaultResourceName));
  // Wait for the first 3 backends to be ready.
  WaitForAllBackends(0, 3);
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(kNumRpcs);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // The picking rates of the first 3 backends should be roughly equal to the
  // expectation.
  std::vector<double> locality_picked_rates;
  for (size_t i = 0; i < 3; ++i) {
    locality_picked_rates.push_back(
        static_cast<double>(backends_[i]->backend_service()->request_count()) /
        kNumRpcs);
  }
  const double kErrorTolerance = 0.2;
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_THAT(
        locality_picked_rates[i],
        ::testing::AllOf(
            ::testing::Ge(locality_weight_rate_0[i] * (1 - kErrorTolerance)),
            ::testing::Le(locality_weight_rate_0[i] * (1 + kErrorTolerance))));
  }
  // Backend 3 hasn't received any request.
  EXPECT_EQ(0U, backends_[3]->backend_service()->request_count());
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
  // Wait until the locality update has been processed, as signaled by backend 3
  // receiving a request.
  WaitForBackend(3);
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(kNumRpcs);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // Backend 0 no longer receives any request.
  EXPECT_EQ(0U, backends_[0]->backend_service()->request_count());
  // The picking rates of the last 3 backends should be roughly equal to the
  // expectation.
  locality_picked_rates = {0 /* placeholder for backend 0 */};
  for (size_t i = 1; i < 4; ++i) {
    locality_picked_rates.push_back(
        static_cast<double>(backends_[i]->backend_service()->request_count()) /
        kNumRpcs);
  }
  for (size_t i = 1; i < 4; ++i) {
    EXPECT_THAT(
        locality_picked_rates[i],
        ::testing::AllOf(
            ::testing::Ge(locality_weight_rate_1[i] * (1 - kErrorTolerance)),
            ::testing::Le(locality_weight_rate_1[i] * (1 + kErrorTolerance))));
  }
  // The ADS service got a single request.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  // and sent two responses.
  EXPECT_EQ(2U, balancers_[0]->ads_service()->response_count());
  delayed_resource_setter.join();
}

class FailoverTest : public BasicTest {
 public:
  FailoverTest() { ResetStub(0, 100, ""); }
};

// Localities with the highest priority are used when multiple priority exist.
TEST_P(FailoverTest, ChooseHighestPriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 1},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 2},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 3},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 0},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  WaitForBackend(3, false);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

// If the higher priority localities are not reachable, failover to the highest
// priority among the rest.
TEST_P(FailoverTest, Failover) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 1},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 2},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 3},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 0},
  });
  ShutdownBackend(3);
  ShutdownBackend(0);
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  WaitForBackend(1, false);
  for (size_t i = 0; i < 4; ++i) {
    if (i == 1) continue;
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

// If a locality with higher priority than the current one becomes ready,
// switch to it.
TEST_P(FailoverTest, SwitchBackToHigherPriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 100;
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 1},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 2},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 3},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 0},
  });
  ShutdownBackend(3);
  ShutdownBackend(0);
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  WaitForBackend(1, false);
  for (size_t i = 0; i < 4; ++i) {
    if (i == 1) continue;
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  StartBackend(0);
  WaitForBackend(0);
  CheckRpcSendOk(kNumRpcs);
  EXPECT_EQ(kNumRpcs, backends_[0]->backend_service()->request_count());
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

// The first update only contains unavailable priorities. The second update
// contains available priorities.
TEST_P(FailoverTest, UpdateInitialUnavailable) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 0},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 1},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  args = AdsServiceImpl::ResponseArgs({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 0},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 1},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 2},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 3},
  });
  ShutdownBackend(0);
  ShutdownBackend(1);
  std::thread delayed_resource_setter(std::bind(
      &AdsServiceImpl::SetEdsResponse, balancers_[0]->ads_service(),
      AdsServiceImpl::BuildResponse(args), 1000, kDefaultResourceName));
  gpr_timespec deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                       gpr_time_from_millis(500, GPR_TIMESPAN));
  // Send 0.5 second worth of RPCs.
  do {
    CheckRpcSendFailure();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  WaitForBackend(2, false);
  for (size_t i = 0; i < 4; ++i) {
    if (i == 2) continue;
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(2U, balancers_[0]->ads_service()->response_count());
  delayed_resource_setter.join();
}

// Tests that after the localities' priorities are updated, we still choose the
// highest READY priority with the updated localities.
TEST_P(FailoverTest, UpdatePriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 100;
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 1},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 2},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 3},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 0},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  args = AdsServiceImpl::ResponseArgs({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 2},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 0},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 1},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 3},
  });
  std::thread delayed_resource_setter(std::bind(
      &AdsServiceImpl::SetEdsResponse, balancers_[0]->ads_service(),
      AdsServiceImpl::BuildResponse(args), 1000, kDefaultResourceName));
  WaitForBackend(3, false);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  WaitForBackend(1);
  CheckRpcSendOk(kNumRpcs);
  EXPECT_EQ(kNumRpcs, backends_[1]->backend_service()->request_count());
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(2U, balancers_[0]->ads_service()->response_count());
  delayed_resource_setter.join();
}

using DropTest = BasicTest;

// Tests that RPCs are dropped according to the drop config.
TEST_P(DropTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 5000;
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 200000;
  const double kDropRateForLb = kDropPerMillionForLb / 1000000.0;
  const double kDropRateForThrottle = kDropPerMillionForThrottle / 1000000.0;
  const double KDropRateForLbAndThrottle =
      kDropRateForLb + (1 - kDropRateForLb) * kDropRateForThrottle;
  // The ADS response contains two drop categories.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  WaitForAllBackends();
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    if (!status.ok() &&
        status.error_message() == "Call dropped by load balancing policy") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
  }
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  const double kErrorTolerance = 0.2;
  EXPECT_THAT(
      seen_drop_rate,
      ::testing::AllOf(
          ::testing::Ge(KDropRateForLbAndThrottle * (1 - kErrorTolerance)),
          ::testing::Le(KDropRateForLbAndThrottle * (1 + kErrorTolerance))));
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

// Tests that drop config is converted correctly from per hundred.
TEST_P(DropTest, DropPerHundred) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 5000;
  const uint32_t kDropPerHundredForLb = 10;
  const double kDropRateForLb = kDropPerHundredForLb / 100.0;
  // The ADS response contains one drop category.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, kDropPerHundredForLb}};
  args.drop_denominator = FractionalPercent::HUNDRED;
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  WaitForAllBackends();
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    if (!status.ok() &&
        status.error_message() == "Call dropped by load balancing policy") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
  }
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  const double kErrorTolerance = 0.2;
  EXPECT_THAT(
      seen_drop_rate,
      ::testing::AllOf(::testing::Ge(kDropRateForLb * (1 - kErrorTolerance)),
                       ::testing::Le(kDropRateForLb * (1 + kErrorTolerance))));
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

// Tests that drop config is converted correctly from per ten thousand.
TEST_P(DropTest, DropPerTenThousand) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 5000;
  const uint32_t kDropPerTenThousandForLb = 1000;
  const double kDropRateForLb = kDropPerTenThousandForLb / 10000.0;
  // The ADS response contains one drop category.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, kDropPerTenThousandForLb}};
  args.drop_denominator = FractionalPercent::TEN_THOUSAND;
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  WaitForAllBackends();
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    if (!status.ok() &&
        status.error_message() == "Call dropped by load balancing policy") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
  }
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  const double kErrorTolerance = 0.2;
  EXPECT_THAT(
      seen_drop_rate,
      ::testing::AllOf(::testing::Ge(kDropRateForLb * (1 - kErrorTolerance)),
                       ::testing::Le(kDropRateForLb * (1 + kErrorTolerance))));
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

// Tests that drop is working correctly after update.
TEST_P(DropTest, Update) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 1000;
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 200000;
  const double kDropRateForLb = kDropPerMillionForLb / 1000000.0;
  const double kDropRateForThrottle = kDropPerMillionForThrottle / 1000000.0;
  const double KDropRateForLbAndThrottle =
      kDropRateForLb + (1 - kDropRateForLb) * kDropRateForThrottle;
  // The first ADS response contains one drop category.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb}};
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  WaitForAllBackends();
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = 0;
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    if (!status.ok() &&
        status.error_message() == "Call dropped by load balancing policy") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
  }
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // The drop rate should be roughly equal to the expectation.
  double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  const double kErrorTolerance = 0.3;
  EXPECT_THAT(
      seen_drop_rate,
      ::testing::AllOf(::testing::Ge(kDropRateForLb * (1 - kErrorTolerance)),
                       ::testing::Le(kDropRateForLb * (1 + kErrorTolerance))));
  // The second ADS response contains two drop categories, send an update EDS
  // response.
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Wait until the drop rate increases to the middle of the two configs, which
  // implies that the update has been in effect.
  const double kDropRateThreshold =
      (kDropRateForLb + KDropRateForLbAndThrottle) / 2;
  size_t num_rpcs = kNumRpcs;
  while (seen_drop_rate < kDropRateThreshold) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    ++num_rpcs;
    if (!status.ok() &&
        status.error_message() == "Call dropped by load balancing policy") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
    seen_drop_rate = static_cast<double>(num_drops) / num_rpcs;
  }
  // Send kNumRpcs RPCs and count the drops.
  num_drops = 0;
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    if (!status.ok() &&
        status.error_message() == "Call dropped by load balancing policy") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
  }
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // The new drop rate should be roughly equal to the expectation.
  seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(
      seen_drop_rate,
      ::testing::AllOf(
          ::testing::Ge(KDropRateForLbAndThrottle * (1 - kErrorTolerance)),
          ::testing::Le(KDropRateForLbAndThrottle * (1 + kErrorTolerance))));
  // The ADS service got a single request,
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  // and sent two responses
  EXPECT_EQ(2U, balancers_[0]->ads_service()->response_count());
}

// Tests that all the RPCs are dropped if any drop category drops 100%.
TEST_P(DropTest, DropAll) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 1000;
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 1000000;
  // The ADS response contains two drop categories.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Send kNumRpcs RPCs and all of them are dropped.
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    EXPECT_TRUE(!status.ok() && status.error_message() ==
                                    "Call dropped by load balancing policy");
  }
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

using FallbackTest = BasicTest;

// Tests that RPCs are handled by the fallback backends before the serverlist is
// received, but will be handled by the serverlist after it's received.
TEST_P(FallbackTest, Vanilla) {
  const int kFallbackTimeoutMs = 200 * grpc_test_slowdown_factor();
  const int kServerlistDelayMs = 500 * grpc_test_slowdown_factor();
  const size_t kNumBackendsInResolution = backends_.size() / 2;
  ResetStub(kFallbackTimeoutMs);
  SetNextResolution(GetBackendPorts(0, kNumBackendsInResolution));
  SetNextResolutionForLbChannelAllBalancers();
  // Send non-empty serverlist only after kServerlistDelayMs.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts(kNumBackendsInResolution)},
  });
  std::thread delayed_resource_setter(
      std::bind(&AdsServiceImpl::SetEdsResponse, balancers_[0]->ads_service(),
                AdsServiceImpl::BuildResponse(args), kServerlistDelayMs,
                kDefaultResourceName));
  // Wait until all the fallback backends are reachable.
  WaitForAllBackends(0 /* start_index */,
                     kNumBackendsInResolution /* stop_index */);
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(kNumBackendsInResolution);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // Fallback is used: each backend returned by the resolver should have
  // gotten one request.
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    EXPECT_EQ(1U, backends_[i]->backend_service()->request_count());
  }
  for (size_t i = kNumBackendsInResolution; i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  // Wait until the serverlist reception has been processed and all backends
  // in the serverlist are reachable.
  WaitForAllBackends(kNumBackendsInResolution /* start_index */);
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(backends_.size() - kNumBackendsInResolution);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // Serverlist is used: each backend returned by the balancer should
  // have gotten one request.
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  for (size_t i = kNumBackendsInResolution; i < backends_.size(); ++i) {
    EXPECT_EQ(1U, backends_[i]->backend_service()->request_count());
  }
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(0U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
  delayed_resource_setter.join();
}

// Tests that RPCs are handled by the updated fallback backends before
// serverlist is received,
TEST_P(FallbackTest, Update) {
  const int kFallbackTimeoutMs = 200 * grpc_test_slowdown_factor();
  const int kServerlistDelayMs = 500 * grpc_test_slowdown_factor();
  const size_t kNumBackendsInResolution = backends_.size() / 3;
  const size_t kNumBackendsInResolutionUpdate = backends_.size() / 3;
  ResetStub(kFallbackTimeoutMs);
  SetNextResolution(GetBackendPorts(0, kNumBackendsInResolution));
  SetNextResolutionForLbChannelAllBalancers();
  // Send non-empty serverlist only after kServerlistDelayMs.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts(kNumBackendsInResolution +
                                    kNumBackendsInResolutionUpdate)},
  });
  std::thread delayed_resource_setter(
      std::bind(&AdsServiceImpl::SetEdsResponse, balancers_[0]->ads_service(),
                AdsServiceImpl::BuildResponse(args), kServerlistDelayMs,
                kDefaultResourceName));
  // Wait until all the fallback backends are reachable.
  WaitForAllBackends(0 /* start_index */,
                     kNumBackendsInResolution /* stop_index */);
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(kNumBackendsInResolution);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // Fallback is used: each backend returned by the resolver should have
  // gotten one request.
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    EXPECT_EQ(1U, backends_[i]->backend_service()->request_count());
  }
  for (size_t i = kNumBackendsInResolution; i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  SetNextResolution(GetBackendPorts(
      kNumBackendsInResolution,
      kNumBackendsInResolution + kNumBackendsInResolutionUpdate));
  // Wait until the resolution update has been processed and all the new
  // fallback backends are reachable.
  WaitForAllBackends(kNumBackendsInResolution /* start_index */,
                     kNumBackendsInResolution +
                         kNumBackendsInResolutionUpdate /* stop_index */);
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(kNumBackendsInResolutionUpdate);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // The resolution update is used: each backend in the resolution update should
  // have gotten one request.
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  for (size_t i = kNumBackendsInResolution;
       i < kNumBackendsInResolution + kNumBackendsInResolutionUpdate; ++i) {
    EXPECT_EQ(1U, backends_[i]->backend_service()->request_count());
  }
  for (size_t i = kNumBackendsInResolution + kNumBackendsInResolutionUpdate;
       i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  // Wait until the serverlist reception has been processed and all backends
  // in the serverlist are reachable.
  WaitForAllBackends(kNumBackendsInResolution +
                     kNumBackendsInResolutionUpdate /* start_index */);
  gpr_log(GPR_INFO, "========= BEFORE THIRD BATCH ==========");
  CheckRpcSendOk(backends_.size() - kNumBackendsInResolution -
                 kNumBackendsInResolutionUpdate);
  gpr_log(GPR_INFO, "========= DONE WITH THIRD BATCH ==========");
  // Serverlist is used: each backend returned by the balancer should
  // have gotten one request.
  for (size_t i = 0;
       i < kNumBackendsInResolution + kNumBackendsInResolutionUpdate; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  for (size_t i = kNumBackendsInResolution + kNumBackendsInResolutionUpdate;
       i < backends_.size(); ++i) {
    EXPECT_EQ(1U, backends_[i]->backend_service()->request_count());
  }
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(0U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
  delayed_resource_setter.join();
}

// Tests that fallback will kick in immediately if the balancer channel fails.
TEST_P(FallbackTest, FallbackEarlyWhenBalancerChannelFails) {
  const int kFallbackTimeoutMs = 10000 * grpc_test_slowdown_factor();
  ResetStub(kFallbackTimeoutMs);
  // Return an unreachable balancer and one fallback backend.
  SetNextResolution({backends_[0]->port()});
  SetNextResolutionForLbChannel({g_port_saver->GetPort()});
  // Send RPC with deadline less than the fallback timeout and make sure it
  // succeeds.
  CheckRpcSendOk(/* times */ 1, /* timeout_ms */ 1000,
                 /* wait_for_ready */ false);
}

// Tests that fallback will kick in immediately if the balancer call fails.
TEST_P(FallbackTest, FallbackEarlyWhenBalancerCallFails) {
  const int kFallbackTimeoutMs = 10000 * grpc_test_slowdown_factor();
  ResetStub(kFallbackTimeoutMs);
  // Return one balancer and one fallback backend.
  SetNextResolution({backends_[0]->port()});
  SetNextResolutionForLbChannelAllBalancers();
  // Balancer drops call without sending a serverlist.
  balancers_[0]->ads_service()->NotifyDoneWithAdsCall();
  // Send RPC with deadline less than the fallback timeout and make sure it
  // succeeds.
  CheckRpcSendOk(/* times */ 1, /* timeout_ms */ 1000,
                 /* wait_for_ready */ false);
}

// Tests that fallback mode is entered if balancer response is received but the
// backends can't be reached.
TEST_P(FallbackTest, FallbackIfResponseReceivedButChildNotReady) {
  const int kFallbackTimeoutMs = 500 * grpc_test_slowdown_factor();
  ResetStub(kFallbackTimeoutMs);
  SetNextResolution({backends_[0]->port()});
  SetNextResolutionForLbChannelAllBalancers();
  // Send a serverlist that only contains an unreachable backend before fallback
  // timeout.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", {g_port_saver->GetPort()}},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Because no child policy is ready before fallback timeout, we enter fallback
  // mode.
  WaitForBackend(0);
}

// Tests that fallback mode is exited if the balancer tells the client to drop
// all the calls.
TEST_P(FallbackTest, FallbackModeIsExitedWhenBalancerSaysToDropAllCalls) {
  // Return an unreachable balancer and one fallback backend.
  SetNextResolution({backends_[0]->port()});
  SetNextResolutionForLbChannel({g_port_saver->GetPort()});
  // Enter fallback mode because the LB channel fails to connect.
  WaitForBackend(0);
  // Return a new balancer that sends a response to drop all calls.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, 1000000}};
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  SetNextResolutionForLbChannelAllBalancers();
  // Send RPCs until failure.
  gpr_timespec deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(5000, GPR_TIMESPAN));
  do {
    auto status = SendRpc();
    if (!status.ok()) break;
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  CheckRpcSendFailure();
}

// Tests that fallback mode is exited if the child policy becomes ready.
TEST_P(FallbackTest, FallbackModeIsExitedAfterChildRready) {
  // Return an unreachable balancer and one fallback backend.
  SetNextResolution({backends_[0]->port()});
  SetNextResolutionForLbChannel({g_port_saver->GetPort()});
  // Enter fallback mode because the LB channel fails to connect.
  WaitForBackend(0);
  // Return a new balancer that sends a dead backend.
  ShutdownBackend(1);
  AdsServiceImpl::ResponseArgs args({
      {"locality0", {backends_[1]->port()}},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  SetNextResolutionForLbChannelAllBalancers();
  // The state (TRANSIENT_FAILURE) update from the child policy will be ignored
  // because we are still in fallback mode.
  gpr_timespec deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                       gpr_time_from_millis(500, GPR_TIMESPAN));
  // Send 0.5 second worth of RPCs.
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  // After the backend is restarted, the child policy will eventually be READY,
  // and we will exit fallback mode.
  StartBackend(1);
  WaitForBackend(1);
  // We have exited fallback mode, so calls will go to the child policy
  // exclusively.
  CheckRpcSendOk(100);
  EXPECT_EQ(0U, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100U, backends_[1]->backend_service()->request_count());
}

class BalancerUpdateTest : public XdsEnd2endTest {
 public:
  BalancerUpdateTest() : XdsEnd2endTest(4, 3) {}
};

// Tests that the old LB call is still used after the balancer address update as
// long as that call is still alive.
TEST_P(BalancerUpdateTest, UpdateBalancersButKeepUsingOriginalBalancer) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::ResponseArgs args({
      {"locality0", {backends_[0]->port()}},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  args = AdsServiceImpl::ResponseArgs({
      {"locality0", {backends_[1]->port()}},
  });
  balancers_[1]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Wait until the first backend is ready.
  WaitForBackend(0);
  // Send 10 requests.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->backend_service()->request_count());
  // The ADS service of balancer 0 got a single request, and sent a single
  // response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
  EXPECT_EQ(0U, balancers_[1]->ads_service()->request_count());
  EXPECT_EQ(0U, balancers_[1]->ads_service()->response_count());
  EXPECT_EQ(0U, balancers_[2]->ads_service()->request_count());
  EXPECT_EQ(0U, balancers_[2]->ads_service()->response_count());
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolutionForLbChannel({balancers_[1]->port()});
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  gpr_timespec deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(10000, GPR_TIMESPAN));
  // Send 10 seconds worth of RPCs
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  // The current LB call is still working, so xds continued using it to the
  // first balancer, which doesn't assign the second backend.
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
  EXPECT_EQ(0U, balancers_[1]->ads_service()->request_count());
  EXPECT_EQ(0U, balancers_[1]->ads_service()->response_count());
  EXPECT_EQ(0U, balancers_[2]->ads_service()->request_count());
  EXPECT_EQ(0U, balancers_[2]->ads_service()->response_count());
}

// Tests that the old LB call is still used after multiple balancer address
// updates as long as that call is still alive. Send an update with the same set
// of LBs as the one in SetUp() in order to verify that the LB channel inside
// xds keeps the initial connection (which by definition is also present in the
// update).
TEST_P(BalancerUpdateTest, Repeated) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::ResponseArgs args({
      {"locality0", {backends_[0]->port()}},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  args = AdsServiceImpl::ResponseArgs({
      {"locality0", {backends_[1]->port()}},
  });
  balancers_[1]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Wait until the first backend is ready.
  WaitForBackend(0);
  // Send 10 requests.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->backend_service()->request_count());
  // The ADS service of balancer 0 got a single request, and sent a single
  // response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
  EXPECT_EQ(0U, balancers_[1]->ads_service()->request_count());
  EXPECT_EQ(0U, balancers_[1]->ads_service()->response_count());
  EXPECT_EQ(0U, balancers_[2]->ads_service()->request_count());
  EXPECT_EQ(0U, balancers_[2]->ads_service()->response_count());
  std::vector<int> ports;
  ports.emplace_back(balancers_[0]->port());
  ports.emplace_back(balancers_[1]->port());
  ports.emplace_back(balancers_[2]->port());
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolutionForLbChannel(ports);
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  gpr_timespec deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(10000, GPR_TIMESPAN));
  // Send 10 seconds worth of RPCs
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  // xds continued using the original LB call to the first balancer, which
  // doesn't assign the second backend.
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  ports.clear();
  ports.emplace_back(balancers_[0]->port());
  ports.emplace_back(balancers_[1]->port());
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 2 ==========");
  SetNextResolutionForLbChannel(ports);
  gpr_log(GPR_INFO, "========= UPDATE 2 DONE ==========");
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                          gpr_time_from_millis(10000, GPR_TIMESPAN));
  // Send 10 seconds worth of RPCs
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  // xds continued using the original LB call to the first balancer, which
  // doesn't assign the second backend.
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
}

// Tests that if the balancer is down, the RPCs will still be sent to the
// backends according to the last balancer response, until a new balancer is
// reachable.
TEST_P(BalancerUpdateTest, DeadUpdate) {
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  AdsServiceImpl::ResponseArgs args({
      {"locality0", {backends_[0]->port()}},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  args = AdsServiceImpl::ResponseArgs({
      {"locality0", {backends_[1]->port()}},
  });
  balancers_[1]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Start servers and send 10 RPCs per server.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->backend_service()->request_count());
  // Kill balancer 0
  gpr_log(GPR_INFO, "********** ABOUT TO KILL BALANCER 0 *************");
  balancers_[0]->Shutdown();
  gpr_log(GPR_INFO, "********** KILLED BALANCER 0 *************");
  // This is serviced by the existing child policy.
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // All 10 requests should again have gone to the first backend.
  EXPECT_EQ(20U, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  // The ADS service of balancer 0 got a single request, and sent a single
  // response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
  EXPECT_EQ(0U, balancers_[1]->ads_service()->request_count());
  EXPECT_EQ(0U, balancers_[1]->ads_service()->response_count());
  EXPECT_EQ(0U, balancers_[2]->ads_service()->request_count());
  EXPECT_EQ(0U, balancers_[2]->ads_service()->response_count());
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolutionForLbChannel({balancers_[1]->port()});
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");
  // Wait until update has been processed, as signaled by the second backend
  // receiving a request. In the meantime, the client continues to be serviced
  // (by the first backend) without interruption.
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  WaitForBackend(1);
  // This is serviced by the updated RR policy
  backends_[1]->backend_service()->ResetCounters();
  gpr_log(GPR_INFO, "========= BEFORE THIRD BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH THIRD BATCH ==========");
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(10U, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
  // The second balancer, published as part of the first update, may end up
  // getting two requests (that is, 1 <= #req <= 2) if the LB call retry timer
  // firing races with the arrival of the update containing the second
  // balancer.
  EXPECT_GE(balancers_[1]->ads_service()->request_count(), 1U);
  EXPECT_GE(balancers_[1]->ads_service()->response_count(), 1U);
  EXPECT_LE(balancers_[1]->ads_service()->request_count(), 2U);
  EXPECT_LE(balancers_[1]->ads_service()->response_count(), 2U);
  EXPECT_EQ(0U, balancers_[2]->ads_service()->request_count());
  EXPECT_EQ(0U, balancers_[2]->ads_service()->response_count());
}

// The re-resolution tests are deferred because they rely on the fallback mode,
// which hasn't been supported.

// TODO(juanlishen): Add TEST_P(BalancerUpdateTest, ReresolveDeadBackend).

// TODO(juanlishen): Add TEST_P(UpdatesWithClientLoadReportingTest,
// ReresolveDeadBalancer)

class ClientLoadReportingTest : public XdsEnd2endTest {
 public:
  ClientLoadReportingTest() : XdsEnd2endTest(4, 1, 3) {}
};

// Tests that the load report received at the balancer is correct.
TEST_P(ClientLoadReportingTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  const size_t kNumRpcsPerAddress = 100;
  // TODO(juanlishen): Partition the backends after multiple localities is
  // tested.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts()},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Wait until all backends are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * num_backends_);
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->response_count());
  // The load report received at the balancer should be correct.
  ClientStats* client_stats = balancers_[0]->lrs_service()->WaitForLoadReport();
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_ + num_ok,
            client_stats->total_successful_requests());
  EXPECT_EQ(0U, client_stats->total_requests_in_progress());
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_ + num_ok,
            client_stats->total_issued_requests());
  EXPECT_EQ(0U, client_stats->total_error_requests());
  EXPECT_EQ(0U, client_stats->total_dropped_requests());
}

// Tests that if the balancer restarts, the client load report contains the
// stats before and after the restart correctly.
TEST_P(ClientLoadReportingTest, BalancerRestart) {
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  const size_t kNumBackendsFirstPass = backends_.size() / 2;
  const size_t kNumBackendsSecondPass =
      backends_.size() - kNumBackendsFirstPass;
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts(0, kNumBackendsFirstPass)},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Wait until all backends returned by the balancer are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) =
      WaitForAllBackends(/* start_index */ 0,
                         /* stop_index */ kNumBackendsFirstPass);
  ClientStats* client_stats = balancers_[0]->lrs_service()->WaitForLoadReport();
  EXPECT_EQ(static_cast<size_t>(num_ok),
            client_stats->total_successful_requests());
  EXPECT_EQ(0U, client_stats->total_requests_in_progress());
  EXPECT_EQ(0U, client_stats->total_error_requests());
  EXPECT_EQ(0U, client_stats->total_dropped_requests());
  // Shut down the balancer.
  balancers_[0]->Shutdown();
  // We should continue using the last EDS response we received from the
  // balancer before it was shut down.
  // Note: We need to use WaitForAllBackends() here instead of just
  // CheckRpcSendOk(kNumBackendsFirstPass), because when the balancer
  // shuts down, the XdsClient will generate an error to the
  // ServiceConfigWatcher, which will cause the xds resolver to send a
  // no-op update to the LB policy.  When this update gets down to the
  // round_robin child policy for the locality, it will generate a new
  // subchannel list, which resets the start index randomly.  So we need
  // to be a little more permissive here to avoid spurious failures.
  ResetBackendCounters();
  int num_started = std::get<0>(WaitForAllBackends(
      /* start_index */ 0, /* stop_index */ kNumBackendsFirstPass));
  // Now restart the balancer, this time pointing to the new backends.
  balancers_[0]->Start(server_host_);
  args = AdsServiceImpl::ResponseArgs({
      {"locality0", GetBackendPorts(kNumBackendsFirstPass)},
  });
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  // Wait for queries to start going to one of the new backends.
  // This tells us that we're now using the new serverlist.
  std::tie(num_ok, num_failure, num_drops) =
      WaitForAllBackends(/* start_index */ kNumBackendsFirstPass);
  num_started += num_ok + num_failure + num_drops;
  // Send one RPC per backend.
  CheckRpcSendOk(kNumBackendsSecondPass);
  num_started += kNumBackendsSecondPass;
  // Check client stats.
  client_stats = balancers_[0]->lrs_service()->WaitForLoadReport();
  EXPECT_EQ(num_started, client_stats->total_successful_requests());
  EXPECT_EQ(0U, client_stats->total_requests_in_progress());
  EXPECT_EQ(0U, client_stats->total_error_requests());
  EXPECT_EQ(0U, client_stats->total_dropped_requests());
}

class ClientLoadReportingWithDropTest : public XdsEnd2endTest {
 public:
  ClientLoadReportingWithDropTest() : XdsEnd2endTest(4, 1, 20) {}
};

// Tests that the drop stats are correctly reported by client load reporting.
TEST_P(ClientLoadReportingWithDropTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 3000;
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 200000;
  const double kDropRateForLb = kDropPerMillionForLb / 1000000.0;
  const double kDropRateForThrottle = kDropPerMillionForThrottle / 1000000.0;
  const double KDropRateForLbAndThrottle =
      kDropRateForLb + (1 - kDropRateForLb) * kDropRateForThrottle;
  // The ADS response contains two drop categories.
  AdsServiceImpl::ResponseArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResponse(
      AdsServiceImpl::BuildResponse(args));
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends();
  const size_t num_warmup = num_ok + num_failure + num_drops;
  // Send kNumRpcs RPCs and count the drops.
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    if (!status.ok() &&
        status.error_message() == "Call dropped by load balancing policy") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
  }
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  const double kErrorTolerance = 0.2;
  EXPECT_THAT(
      seen_drop_rate,
      ::testing::AllOf(
          ::testing::Ge(KDropRateForLbAndThrottle * (1 - kErrorTolerance)),
          ::testing::Le(KDropRateForLbAndThrottle * (1 + kErrorTolerance))));
  // Check client stats.
  ClientStats* client_stats = balancers_[0]->lrs_service()->WaitForLoadReport();
  EXPECT_EQ(num_drops, client_stats->total_dropped_requests());
  const size_t total_rpc = num_warmup + kNumRpcs;
  EXPECT_THAT(
      client_stats->dropped_requests(kLbDropType),
      ::testing::AllOf(
          ::testing::Ge(total_rpc * kDropRateForLb * (1 - kErrorTolerance)),
          ::testing::Le(total_rpc * kDropRateForLb * (1 + kErrorTolerance))));
  EXPECT_THAT(client_stats->dropped_requests(kThrottleDropType),
              ::testing::AllOf(
                  ::testing::Ge(total_rpc * (1 - kDropRateForLb) *
                                kDropRateForThrottle * (1 - kErrorTolerance)),
                  ::testing::Le(total_rpc * (1 - kDropRateForLb) *
                                kDropRateForThrottle * (1 + kErrorTolerance))));
  // The ADS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->ads_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->ads_service()->response_count());
}

grpc::string TestTypeName(const ::testing::TestParamInfo<TestType>& info) {
  return info.param.AsString();
}

INSTANTIATE_TEST_SUITE_P(XdsTest, BasicTest,
                         ::testing::Values(TestType(false, true),
                                           TestType(false, false),
                                           TestType(true, false),
                                           TestType(true, true)),
                         &TestTypeName);

INSTANTIATE_TEST_SUITE_P(XdsTest, SecureNamingTest,
                         ::testing::Values(TestType(false, true),
                                           TestType(false, false),
                                           TestType(true, false),
                                           TestType(true, true)),
                         &TestTypeName);

// LDS depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(XdsTest, LdsTest,
                         ::testing::Values(TestType(true, false),
                                           TestType(true, true)),
                         &TestTypeName);

// RDS depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(XdsTest, RdsTest,
                         ::testing::Values(TestType(true, false),
                                           TestType(true, true)),
                         &TestTypeName);

// CDS depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(XdsTest, CdsTest,
                         ::testing::Values(TestType(true, false),
                                           TestType(true, true)),
                         &TestTypeName);

// EDS could be tested with or without XdsResolver, but the tests would
// be the same either way, so we test it only with XdsResolver.
INSTANTIATE_TEST_SUITE_P(XdsTest, EdsTest,
                         ::testing::Values(TestType(true, false),
                                           TestType(true, true)),
                         &TestTypeName);

INSTANTIATE_TEST_SUITE_P(XdsTest, LocalityMapTest,
                         ::testing::Values(TestType(false, true),
                                           TestType(false, false),
                                           TestType(true, false),
                                           TestType(true, true)),
                         &TestTypeName);

INSTANTIATE_TEST_SUITE_P(XdsTest, FailoverTest,
                         ::testing::Values(TestType(false, true),
                                           TestType(false, false),
                                           TestType(true, false),
                                           TestType(true, true)),
                         &TestTypeName);

INSTANTIATE_TEST_SUITE_P(XdsTest, DropTest,
                         ::testing::Values(TestType(false, true),
                                           TestType(false, false),
                                           TestType(true, false),
                                           TestType(true, true)),
                         &TestTypeName);

// Fallback does not work with xds resolver.
INSTANTIATE_TEST_SUITE_P(XdsTest, FallbackTest,
                         ::testing::Values(TestType(false, true),
                                           TestType(false, false)),
                         &TestTypeName);

INSTANTIATE_TEST_SUITE_P(XdsTest, BalancerUpdateTest,
                         ::testing::Values(TestType(false, true),
                                           TestType(false, false),
                                           TestType(true, true)),
                         &TestTypeName);

// Load reporting tests are not run with load reporting disabled.
INSTANTIATE_TEST_SUITE_P(XdsTest, ClientLoadReportingTest,
                         ::testing::Values(TestType(false, true),
                                           TestType(true, true)),
                         &TestTypeName);

// Load reporting tests are not run with load reporting disabled.
INSTANTIATE_TEST_SUITE_P(XdsTest, ClientLoadReportingWithDropTest,
                         ::testing::Values(TestType(false, true),
                                           TestType(true, true)),
                         &TestTypeName);

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::WriteBootstrapFiles();
  grpc::testing::g_port_saver = new grpc::testing::PortSaver();
  const auto result = RUN_ALL_TESTS();
  return result;
}
