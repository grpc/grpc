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
#include <string>
#include <thread>
#include <vector>

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

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

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

 private:
  grpc_core::Mutex mu_;
  size_t request_count_ = 0;
  size_t response_count_ = 0;
};

using LrsService = CountedService<LoadReportingService::Service>;

const char g_kCallCredsMdKey[] = "Balancer should not ...";
const char g_kCallCredsMdValue[] = "... receive me";

template <typename RpcService>
class BackendServiceImpl
    : public CountedService<TestMultipleServiceImpl<RpcService>> {
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
    CountedService<TestMultipleServiceImpl<RpcService>>::IncreaseRequestCount();
    const auto status =
        TestMultipleServiceImpl<RpcService>::Echo(context, request, response);
    CountedService<
        TestMultipleServiceImpl<RpcService>>::IncreaseResponseCount();
    AddClient(context->peer());
    return status;
  }

  Status Echo1(ServerContext* context, const EchoRequest* request,
               EchoResponse* response) override {
    return Echo(context, request, response);
  }

  Status Echo2(ServerContext* context, const EchoRequest* request,
               EchoResponse* response) override {
    return Echo(context, request, response);
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
  explicit ClientStats(const ClusterStats& cluster_stats)
      : cluster_name_(cluster_stats.cluster_name()),
        total_dropped_requests_(cluster_stats.total_dropped_requests()) {
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

  const std::string& cluster_name() const { return cluster_name_; }

  const std::map<grpc::string, LocalityStats>& locality_stats() const {
    return locality_stats_;
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
  std::string cluster_name_;
  std::map<grpc::string, LocalityStats> locality_stats_;
  uint64_t total_dropped_requests_;
  std::map<grpc::string, uint64_t> dropped_requests_;
};

class AdsServiceImpl : public AggregatedDiscoveryService::Service,
                       public std::enable_shared_from_this<AdsServiceImpl> {
 public:
  struct ResponseState {
    enum State { NOT_SENT, SENT, ACKED, NACKED };
    State state = NOT_SENT;
    std::string error_message;
  };

  struct EdsResourceArgs {
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

    EdsResourceArgs() = default;
    explicit EdsResourceArgs(std::vector<Locality> locality_list)
        : locality_list(std::move(locality_list)) {}

    std::vector<Locality> locality_list;
    std::map<grpc::string, uint32_t> drop_categories;
    FractionalPercent::DenominatorType drop_denominator =
        FractionalPercent::MILLION;
  };

  using Stream = ServerReaderWriter<DiscoveryResponse, DiscoveryRequest>;

  AdsServiceImpl(bool enable_load_reporting) {
    // Construct RDS response data.
    default_route_config_.set_name(kDefaultResourceName);
    auto* virtual_host = default_route_config_.add_virtual_hosts();
    virtual_host->add_domains("*");
    auto* route = virtual_host->add_routes();
    route->mutable_match()->set_prefix("");
    route->mutable_route()->set_cluster(kDefaultResourceName);
    SetRdsResource(default_route_config_, kDefaultResourceName);
    // Construct LDS response data (with inlined RDS result).
    default_listener_ = BuildListener(default_route_config_);
    SetLdsResource(default_listener_, kDefaultResourceName);
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
    SetCdsResource(default_cluster_, kDefaultResourceName);
  }

  Status StreamAggregatedResources(ServerContext* context,
                                   Stream* stream) override {
    gpr_log(GPR_INFO, "ADS[%p]: StreamAggregatedResources starts", this);
    // Resources (type/name pairs) that have changed since the client
    // subscribed to them.
    UpdateQueue update_queue;
    // Resources that the client will be subscribed to keyed by resource type
    // url.
    SubscriptionMap subscription_map;
    [&]() {
      {
        grpc_core::MutexLock lock(&ads_mu_);
        if (ads_done_) return;
      }
      // Balancer shouldn't receive the call credentials metadata.
      EXPECT_EQ(context->client_metadata().find(g_kCallCredsMdKey),
                context->client_metadata().end());
      // Current Version map keyed by resource type url.
      std::map<std::string, int> resource_type_version;
      // Creating blocking thread to read from stream.
      std::deque<DiscoveryRequest> requests;
      bool stream_closed = false;
      // Take a reference of the AdsServiceImpl object, reference will go
      // out of scope after the reader thread is joined.
      std::shared_ptr<AdsServiceImpl> ads_service_impl = shared_from_this();
      std::thread reader(std::bind(&AdsServiceImpl::BlockingRead, this, stream,
                                   &requests, &stream_closed));
      // Main loop to look for requests and updates.
      while (true) {
        // Look for new requests and and decide what to handle.
        absl::optional<DiscoveryResponse> response;
        // Boolean to keep track if the loop received any work to do: a request
        // or an update; regardless whether a response was actually sent out.
        bool did_work = false;
        {
          grpc_core::MutexLock lock(&ads_mu_);
          if (stream_closed) break;
          if (!requests.empty()) {
            DiscoveryRequest request = std::move(requests.front());
            requests.pop_front();
            did_work = true;
            gpr_log(GPR_INFO,
                    "ADS[%p]: Received request for type %s with content %s",
                    this, request.type_url().c_str(),
                    request.DebugString().c_str());
            // Identify ACK and NACK by looking for version information and
            // comparing it to nonce (this server ensures they are always set to
            // the same in a response.)
            if (!request.response_nonce().empty()) {
              resource_type_response_state_[request.type_url()].state =
                  (!request.version_info().empty() &&
                   request.version_info() == request.response_nonce())
                      ? ResponseState::ACKED
                      : ResponseState::NACKED;
            }
            if (request.has_error_detail()) {
              resource_type_response_state_[request.type_url()].error_message =
                  request.error_detail().message();
            }
            // As long as the test did not tell us to ignore this type of
            // request, we will loop through all resources to:
            // 1. subscribe if necessary
            // 2. update if necessary
            // 3. unsubscribe if necessary
            if (resource_types_to_ignore_.find(request.type_url()) ==
                resource_types_to_ignore_.end()) {
              auto& subscription_name_map =
                  subscription_map[request.type_url()];
              auto& resource_name_map = resource_map_[request.type_url()];
              std::set<std::string> resources_in_current_request;
              std::set<std::string> resources_added_to_response;
              for (const std::string& resource_name :
                   request.resource_names()) {
                resources_in_current_request.emplace(resource_name);
                auto& subscription_state = subscription_name_map[resource_name];
                auto& resource_state = resource_name_map[resource_name];
                MaybeSubscribe(request.type_url(), resource_name,
                               &subscription_state, &resource_state,
                               &update_queue);
                if (ClientNeedsResourceUpdate(resource_state,
                                              &subscription_state)) {
                  gpr_log(
                      GPR_INFO,
                      "ADS[%p]: Sending update for type=%s name=%s version=%d",
                      this, request.type_url().c_str(), resource_name.c_str(),
                      resource_state.version);
                  resources_added_to_response.emplace(resource_name);
                  if (!response.has_value()) response.emplace();
                  if (resource_state.resource.has_value()) {
                    response->add_resources()->CopyFrom(
                        resource_state.resource.value());
                  }
                }
              }
              // Process unsubscriptions for any resource no longer
              // present in the request's resource list.
              ProcessUnsubscriptions(
                  request.type_url(), resources_in_current_request,
                  &subscription_name_map, &resource_name_map);
              // Send response if needed.
              if (!resources_added_to_response.empty()) {
                CompleteBuildingDiscoveryResponse(
                    request.type_url(),
                    ++resource_type_version[request.type_url()],
                    subscription_name_map, resources_added_to_response,
                    &response.value());
              }
            }
          }
        }
        if (response.has_value()) {
          gpr_log(GPR_INFO, "ADS[%p]: Sending response: %s", this,
                  response->DebugString().c_str());
          stream->Write(response.value());
        }
        response.reset();
        // Look for updates and decide what to handle.
        {
          grpc_core::MutexLock lock(&ads_mu_);
          if (!update_queue.empty()) {
            const std::string resource_type =
                std::move(update_queue.front().first);
            const std::string resource_name =
                std::move(update_queue.front().second);
            update_queue.pop_front();
            did_work = true;
            gpr_log(GPR_INFO, "ADS[%p]: Received update for type=%s name=%s",
                    this, resource_type.c_str(), resource_name.c_str());
            auto& subscription_name_map = subscription_map[resource_type];
            auto& resource_name_map = resource_map_[resource_type];
            auto it = subscription_name_map.find(resource_name);
            if (it != subscription_name_map.end()) {
              SubscriptionState& subscription_state = it->second;
              ResourceState& resource_state = resource_name_map[resource_name];
              if (ClientNeedsResourceUpdate(resource_state,
                                            &subscription_state)) {
                gpr_log(
                    GPR_INFO,
                    "ADS[%p]: Sending update for type=%s name=%s version=%d",
                    this, resource_type.c_str(), resource_name.c_str(),
                    resource_state.version);
                response.emplace();
                if (resource_state.resource.has_value()) {
                  response->add_resources()->CopyFrom(
                      resource_state.resource.value());
                }
                CompleteBuildingDiscoveryResponse(
                    resource_type, ++resource_type_version[resource_type],
                    subscription_name_map, {resource_name}, &response.value());
              }
            }
          }
        }
        if (response.has_value()) {
          gpr_log(GPR_INFO, "ADS[%p]: Sending update response: %s", this,
                  response->DebugString().c_str());
          stream->Write(response.value());
        }
        // If we didn't find anything to do, delay before the next loop
        // iteration; otherwise, check whether we should exit and then
        // immediately continue.
        gpr_timespec deadline =
            grpc_timeout_milliseconds_to_deadline(did_work ? 0 : 10);
        {
          grpc_core::MutexLock lock(&ads_mu_);
          if (!ads_cond_.WaitUntil(&ads_mu_, [this] { return ads_done_; },
                                   deadline))
            break;
        }
      }
      reader.join();
    }();
    // Clean up any subscriptions that were still active when the call finished.
    {
      grpc_core::MutexLock lock(&ads_mu_);
      for (auto& p : subscription_map) {
        const std::string& type_url = p.first;
        SubscriptionNameMap& subscription_name_map = p.second;
        for (auto& q : subscription_name_map) {
          const std::string& resource_name = q.first;
          SubscriptionState& subscription_state = q.second;
          ResourceState& resource_state =
              resource_map_[type_url][resource_name];
          resource_state.subscriptions.erase(&subscription_state);
        }
      }
    }
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
    return resource_type_response_state_[kLdsTypeUrl];
  }

  ResponseState rds_response_state() {
    grpc_core::MutexLock lock(&ads_mu_);
    return resource_type_response_state_[kRdsTypeUrl];
  }

  ResponseState cds_response_state() {
    grpc_core::MutexLock lock(&ads_mu_);
    return resource_type_response_state_[kCdsTypeUrl];
  }

  ResponseState eds_response_state() {
    grpc_core::MutexLock lock(&ads_mu_);
    return resource_type_response_state_[kEdsTypeUrl];
  }

  void SetResourceIgnore(const std::string& type_url) {
    grpc_core::MutexLock lock(&ads_mu_);
    resource_types_to_ignore_.emplace(type_url);
  }

  void UnsetResource(const std::string& type_url, const std::string& name) {
    grpc_core::MutexLock lock(&ads_mu_);
    ResourceState& state = resource_map_[type_url][name];
    ++state.version;
    state.resource.reset();
    gpr_log(GPR_INFO, "ADS[%p]: Unsetting %s resource %s to version %u", this,
            type_url.c_str(), name.c_str(), state.version);
    for (SubscriptionState* subscription : state.subscriptions) {
      subscription->update_queue->emplace_back(type_url, name);
    }
  }

  void SetResource(google::protobuf::Any resource, const std::string& type_url,
                   const std::string& name) {
    grpc_core::MutexLock lock(&ads_mu_);
    ResourceState& state = resource_map_[type_url][name];
    ++state.version;
    state.resource = std::move(resource);
    gpr_log(GPR_INFO, "ADS[%p]: Updating %s resource %s to version %u", this,
            type_url.c_str(), name.c_str(), state.version);
    for (SubscriptionState* subscription : state.subscriptions) {
      subscription->update_queue->emplace_back(type_url, name);
    }
  }

  void SetLdsResource(const Listener& listener, const std::string& name) {
    google::protobuf::Any resource;
    resource.PackFrom(listener);
    SetResource(std::move(resource), kLdsTypeUrl, name);
  }

  void SetRdsResource(const RouteConfiguration& route,
                      const std::string& name) {
    google::protobuf::Any resource;
    resource.PackFrom(route);
    SetResource(std::move(resource), kRdsTypeUrl, name);
  }

  void SetCdsResource(const Cluster& cluster, const std::string& name) {
    google::protobuf::Any resource;
    resource.PackFrom(cluster);
    SetResource(std::move(resource), kCdsTypeUrl, name);
  }

  void SetEdsResource(const ClusterLoadAssignment& assignment,
                      const std::string& name) {
    google::protobuf::Any resource;
    resource.PackFrom(assignment);
    SetResource(std::move(resource), kEdsTypeUrl, name);
  }

  void SetLdsToUseDynamicRds() {
    auto listener = default_listener_;
    HttpConnectionManager http_connection_manager;
    http_connection_manager.mutable_rds()->set_route_config_name(
        kDefaultResourceName);
    listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
        http_connection_manager);
    SetLdsResource(listener, kDefaultResourceName);
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
      resource_type_response_state_.clear();
    }
    gpr_log(GPR_INFO, "ADS[%p]: shut down", this);
  }

  static ClusterLoadAssignment BuildEdsResource(
      const EdsResourceArgs& args,
      const char* cluster_name = kDefaultResourceName) {
    ClusterLoadAssignment assignment;
    assignment.set_cluster_name(cluster_name);
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
    return assignment;
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
  // A queue of resource type/name pairs that have changed since the client
  // subscribed to them.
  using UpdateQueue = std::deque<
      std::pair<std::string /* type url */, std::string /* resource name */>>;

  // A struct representing a client's subscription to a particular resource.
  struct SubscriptionState {
    // Version that the client currently knows about.
    int current_version = 0;
    // The queue upon which to place updates when the resource is updated.
    UpdateQueue* update_queue;
  };

  // A struct representing the a client's subscription to all the resources.
  using SubscriptionNameMap =
      std::map<std::string /* resource_name */, SubscriptionState>;
  using SubscriptionMap =
      std::map<std::string /* type_url */, SubscriptionNameMap>;

  // A struct representing the current state for a resource:
  // - the version of the resource that is set by the SetResource() methods.
  // - a list of subscriptions interested in this resource.
  struct ResourceState {
    int version = 0;
    absl::optional<google::protobuf::Any> resource;
    std::set<SubscriptionState*> subscriptions;
  };

  // A struct representing the current state for all resources:
  // LDS, CDS, EDS, and RDS for the class as a whole.
  using ResourceNameMap =
      std::map<std::string /* resource_name */, ResourceState>;
  using ResourceMap = std::map<std::string /* type_url */, ResourceNameMap>;

  // Starting a thread to do blocking read on the stream until cancel.
  void BlockingRead(Stream* stream, std::deque<DiscoveryRequest>* requests,
                    bool* stream_closed) {
    DiscoveryRequest request;
    bool seen_first_request = false;
    while (stream->Read(&request)) {
      if (!seen_first_request) {
        EXPECT_TRUE(request.has_node());
        ASSERT_FALSE(request.node().client_features().empty());
        EXPECT_EQ(request.node().client_features(0),
                  "envoy.lb.does_not_support_overprovisioning");
        seen_first_request = true;
      }
      {
        grpc_core::MutexLock lock(&ads_mu_);
        requests->emplace_back(std::move(request));
      }
    }
    gpr_log(GPR_INFO, "ADS[%p]: Null read, stream closed", this);
    grpc_core::MutexLock lock(&ads_mu_);
    *stream_closed = true;
  }

  // Checks whether the client needs to receive a newer version of
  // the resource.  If so, updates subscription_state->current_version and
  // returns true.
  bool ClientNeedsResourceUpdate(const ResourceState& resource_state,
                                 SubscriptionState* subscription_state) {
    if (subscription_state->current_version < resource_state.version) {
      subscription_state->current_version = resource_state.version;
      return true;
    }
    return false;
  }

  // Subscribes to a resource if not already subscribed:
  // 1. Sets the update_queue field in subscription_state.
  // 2. Adds subscription_state to resource_state->subscriptions.
  void MaybeSubscribe(const std::string& resource_type,
                      const std::string& resource_name,
                      SubscriptionState* subscription_state,
                      ResourceState* resource_state,
                      UpdateQueue* update_queue) {
    // The update_queue will be null if we were not previously subscribed.
    if (subscription_state->update_queue != nullptr) return;
    subscription_state->update_queue = update_queue;
    resource_state->subscriptions.emplace(subscription_state);
    gpr_log(GPR_INFO, "ADS[%p]: subscribe to resource type %s name %s state %p",
            this, resource_type.c_str(), resource_name.c_str(),
            &subscription_state);
  }

  // Removes subscriptions for resources no longer present in the
  // current request.
  void ProcessUnsubscriptions(
      const std::string& resource_type,
      const std::set<std::string>& resources_in_current_request,
      SubscriptionNameMap* subscription_name_map,
      ResourceNameMap* resource_name_map) {
    for (auto it = subscription_name_map->begin();
         it != subscription_name_map->end();) {
      const std::string& resource_name = it->first;
      SubscriptionState& subscription_state = it->second;
      if (resources_in_current_request.find(resource_name) !=
          resources_in_current_request.end()) {
        ++it;
        continue;
      }
      gpr_log(GPR_INFO, "ADS[%p]: Unsubscribe to type=%s name=%s state=%p",
              this, resource_type.c_str(), resource_name.c_str(),
              &subscription_state);
      auto resource_it = resource_name_map->find(resource_name);
      GPR_ASSERT(resource_it != resource_name_map->end());
      auto& resource_state = resource_it->second;
      resource_state.subscriptions.erase(&subscription_state);
      if (resource_state.subscriptions.empty() &&
          !resource_state.resource.has_value()) {
        resource_name_map->erase(resource_it);
      }
      it = subscription_name_map->erase(it);
    }
  }

  // Completing the building a DiscoveryResponse by adding common information
  // for all resources and by adding all subscribed resources for LDS and CDS.
  void CompleteBuildingDiscoveryResponse(
      const std::string& resource_type, const int version,
      const SubscriptionNameMap& subscription_name_map,
      const std::set<std::string>& resources_added_to_response,
      DiscoveryResponse* response) {
    resource_type_response_state_[resource_type].state = ResponseState::SENT;
    response->set_type_url(resource_type);
    response->set_version_info(absl::StrCat(version));
    response->set_nonce(absl::StrCat(version));
    if (resource_type == kLdsTypeUrl || resource_type == kCdsTypeUrl) {
      // For LDS and CDS we must send back all subscribed resources
      // (even the unchanged ones)
      for (const auto& p : subscription_name_map) {
        const std::string& resource_name = p.first;
        if (resources_added_to_response.find(resource_name) ==
            resources_added_to_response.end()) {
          const ResourceState& resource_state =
              resource_map_[resource_type][resource_name];
          if (resource_state.resource.has_value()) {
            response->add_resources()->CopyFrom(
                resource_state.resource.value());
          }
        }
      }
    }
  }

  grpc_core::CondVar ads_cond_;
  // Protect the members below.
  grpc_core::Mutex ads_mu_;
  bool ads_done_ = false;
  Listener default_listener_;
  RouteConfiguration default_route_config_;
  Cluster default_cluster_;
  std::map<std::string /* type_url */, ResponseState>
      resource_type_response_state_;
  std::set<std::string /*resource_type*/> resource_types_to_ignore_;
  // An instance data member containing the current state of all resources.
  // Note that an entry will exist whenever either of the following is true:
  // - The resource exists (i.e., has been created by SetResource() and has not
  //   yet been destroyed by UnsetResource()).
  // - There is at least one subscription for the resource.
  ResourceMap resource_map_;
};

class LrsServiceImpl : public LrsService,
                       public std::enable_shared_from_this<LrsServiceImpl> {
 public:
  using Stream = ServerReaderWriter<LoadStatsResponse, LoadStatsRequest>;

  explicit LrsServiceImpl(int client_load_reporting_interval_seconds)
      : client_load_reporting_interval_seconds_(
            client_load_reporting_interval_seconds),
        cluster_names_({kDefaultResourceName}) {}

  Status StreamLoadStats(ServerContext* /*context*/, Stream* stream) override {
    gpr_log(GPR_INFO, "LRS[%p]: StreamLoadStats starts", this);
    GPR_ASSERT(client_load_reporting_interval_seconds_ > 0);
    // Take a reference of the LrsServiceImpl object, reference will go
    // out of scope after this method exits.
    std::shared_ptr<LrsServiceImpl> lrs_service_impl = shared_from_this();
    // Read initial request.
    LoadStatsRequest request;
    if (stream->Read(&request)) {
      IncreaseRequestCount();  // Only for initial request.
      // Verify server name set in metadata.
      auto it =
          request.node().metadata().fields().find("PROXYLESS_CLIENT_HOSTNAME");
      GPR_ASSERT(it != request.node().metadata().fields().end());
      EXPECT_EQ(it->second.string_value(), kDefaultResourceName);
      // Send initial response.
      LoadStatsResponse response;
      for (const std::string& cluster_name : cluster_names_) {
        response.add_clusters(cluster_name);
      }
      response.mutable_load_reporting_interval()->set_seconds(
          client_load_reporting_interval_seconds_);
      stream->Write(response);
      IncreaseResponseCount();
      // Wait for report.
      request.Clear();
      while (stream->Read(&request)) {
        gpr_log(GPR_INFO, "LRS[%p]: received client load report message: %s",
                this, request.DebugString().c_str());
        std::vector<ClientStats> stats;
        for (const auto& cluster_stats : request.cluster_stats()) {
          stats.emplace_back(cluster_stats);
        }
        grpc_core::MutexLock lock(&load_report_mu_);
        result_queue_.emplace_back(std::move(stats));
        if (load_report_cond_ != nullptr) load_report_cond_->Signal();
      }
      // Wait until notified done.
      grpc_core::MutexLock lock(&lrs_mu_);
      lrs_cv_.WaitUntil(&lrs_mu_, [this] { return lrs_done_; });
    }
    gpr_log(GPR_INFO, "LRS[%p]: StreamLoadStats done", this);
    return Status::OK;
  }

  // Must be called before the LRS call is started.
  void set_cluster_names(const std::set<std::string>& cluster_names) {
    cluster_names_ = cluster_names;
  }

  void Start() {
    lrs_done_ = false;
    result_queue_.clear();
  }

  void Shutdown() {
    {
      grpc_core::MutexLock lock(&lrs_mu_);
      NotifyDoneWithLrsCallLocked();
    }
    gpr_log(GPR_INFO, "LRS[%p]: shut down", this);
  }

  std::vector<ClientStats> WaitForLoadReport() {
    grpc_core::MutexLock lock(&load_report_mu_);
    grpc_core::CondVar cv;
    if (result_queue_.empty()) {
      load_report_cond_ = &cv;
      load_report_cond_->WaitUntil(&load_report_mu_,
                                   [this] { return !result_queue_.empty(); });
      load_report_cond_ = nullptr;
    }
    std::vector<ClientStats> result = std::move(result_queue_.front());
    result_queue_.pop_front();
    return result;
  }

  void NotifyDoneWithLrsCall() {
    grpc_core::MutexLock lock(&lrs_mu_);
    NotifyDoneWithLrsCallLocked();
  }

 private:
  void NotifyDoneWithLrsCallLocked() {
    if (!lrs_done_) {
      lrs_done_ = true;
      lrs_cv_.Broadcast();
    }
  }

  const int client_load_reporting_interval_seconds_;
  std::set<std::string> cluster_names_;

  grpc_core::CondVar lrs_cv_;
  grpc_core::Mutex lrs_mu_;  // Protects lrs_done_.
  bool lrs_done_ = false;

  grpc_core::Mutex load_report_mu_;  // Protects the members below.
  grpc_core::CondVar* load_report_cond_ = nullptr;
  std::deque<std::vector<ClientStats>> result_queue_;
};

class TestType {
 public:
  TestType(bool use_xds_resolver, bool enable_load_reporting,
           bool enable_rds_testing = false)
      : use_xds_resolver_(use_xds_resolver),
        enable_load_reporting_(enable_load_reporting),
        enable_rds_testing_(enable_rds_testing) {}

  bool use_xds_resolver() const { return use_xds_resolver_; }
  bool enable_load_reporting() const { return enable_load_reporting_; }
  bool enable_rds_testing() const { return enable_rds_testing_; }

  grpc::string AsString() const {
    grpc::string retval = (use_xds_resolver_ ? "XdsResolver" : "FakeResolver");
    if (enable_load_reporting_) retval += "WithLoadReporting";
    if (enable_rds_testing_) retval += "Rds";
    return retval;
  }

 private:
  const bool use_xds_resolver_;
  const bool enable_load_reporting_;
  const bool enable_rds_testing_;
};

class XdsEnd2endTest : public ::testing::TestWithParam<TestType> {
 protected:
  XdsEnd2endTest(size_t num_backends, size_t num_balancers,
                 int client_load_reporting_interval_seconds = 100)
      : num_backends_(num_backends),
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
      backends_.back()->Start();
    }
    // Start the load balancers.
    for (size_t i = 0; i < num_balancers_; ++i) {
      balancers_.emplace_back(
          new BalancerServerThread(GetParam().enable_load_reporting()
                                       ? client_load_reporting_interval_seconds_
                                       : 0));
      balancers_.back()->Start();
      if (GetParam().enable_rds_testing()) {
        balancers_[i]->ads_service()->SetLdsToUseDynamicRds();
      }
    }
    ResetStub();
  }

  void TearDown() override {
    ShutdownAllBackends();
    for (auto& balancer : balancers_) balancer->Shutdown();
  }

  void StartAllBackends() {
    for (auto& backend : backends_) backend->Start();
  }

  void StartBackend(size_t index) { backends_[index]->Start(); }

  void ShutdownAllBackends() {
    for (auto& backend : backends_) backend->Shutdown();
  }

  void ShutdownBackend(size_t index) { backends_[index]->Shutdown(); }

  void ResetStub(int failover_timeout = 0,
                 const grpc::string& expected_targets = "",
                 int xds_resource_does_not_exist_timeout = 0,
                 bool xds_routing_enabled = false) {
    ChannelArguments args;
    if (failover_timeout > 0) {
      args.SetInt(GRPC_ARG_PRIORITY_FAILOVER_TIMEOUT_MS, failover_timeout);
    }
    if (xds_resource_does_not_exist_timeout > 0) {
      args.SetInt(GRPC_ARG_XDS_RESOURCE_DOES_NOT_EXIST_TIMEOUT_MS,
                  xds_resource_does_not_exist_timeout);
    }
    if (xds_routing_enabled) {
      args.SetInt(GRPC_ARG_XDS_ROUTING_ENABLED, 1);
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
    stub1_ = grpc::testing::EchoTest1Service::NewStub(channel_);
    stub2_ = grpc::testing::EchoTest2Service::NewStub(channel_);
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
                                               size_t stop_index = 0,
                                               bool reset_counters = true) {
    int num_ok = 0;
    int num_failure = 0;
    int num_drops = 0;
    int num_total = 0;
    while (!SeenAllBackends(start_index, stop_index)) {
      SendRpcAndCount(&num_total, &num_ok, &num_failure, &num_drops);
    }
    if (reset_counters) ResetBackendCounters();
    gpr_log(GPR_INFO,
            "Performed %d warm up requests against the backends. "
            "%d succeeded, %d failed, %d dropped.",
            num_total, num_ok, num_failure, num_drops);
    return std::make_tuple(num_ok, num_failure, num_drops);
  }

  void WaitForBackend(size_t backend_idx, bool reset_counters = true,
                      bool require_success = false) {
    gpr_log(GPR_INFO, "========= WAITING FOR BACKEND %lu ==========",
            static_cast<unsigned long>(backend_idx));
    do {
      Status status = SendRpc();
      if (require_success) {
        EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                                 << " message=" << status.error_message();
      }
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
    ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
    ASSERT_NE(result.service_config.get(), nullptr);
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
      ASSERT_NE(result.service_config.get(), nullptr);
      ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
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

  enum RpcService {
    SERVICE_ECHO,
    SERVICE_ECHO1,
    SERVICE_ECHO2,
  };

  enum RpcMethod {
    METHOD_ECHO,
    METHOD_ECHO1,
    METHOD_ECHO2,
  };

  struct RpcOptions {
    RpcService service = SERVICE_ECHO;
    RpcMethod method = METHOD_ECHO;
    int timeout_ms = 1000;
    bool wait_for_ready = false;
    bool server_fail = false;

    RpcOptions() {}

    RpcOptions& set_rpc_service(RpcService rpc_service) {
      service = rpc_service;
      return *this;
    }

    RpcOptions& set_rpc_method(RpcMethod rpc_method) {
      method = rpc_method;
      return *this;
    }

    RpcOptions& set_timeout_ms(int rpc_timeout_ms) {
      timeout_ms = rpc_timeout_ms;
      return *this;
    }

    RpcOptions& set_wait_for_ready(bool rpc_wait_for_ready) {
      wait_for_ready = rpc_wait_for_ready;
      return *this;
    }

    RpcOptions& set_server_fail(bool rpc_server_fail) {
      server_fail = rpc_server_fail;
      return *this;
    }
  };

  template <typename Stub>
  Status SendRpcMethod(Stub* stub, const RpcOptions& rpc_options,
                       ClientContext* context, EchoRequest& request,
                       EchoResponse* response) {
    switch (rpc_options.method) {
      case METHOD_ECHO:
        return (*stub)->Echo(context, request, response);
      case METHOD_ECHO1:
        return (*stub)->Echo1(context, request, response);
      case METHOD_ECHO2:
        return (*stub)->Echo2(context, request, response);
    }
  }

  Status SendRpc(const RpcOptions& rpc_options = RpcOptions(),
                 EchoResponse* response = nullptr) {
    const bool local_response = (response == nullptr);
    if (local_response) response = new EchoResponse;
    EchoRequest request;
    ClientContext context;
    context.set_deadline(
        grpc_timeout_milliseconds_to_deadline(rpc_options.timeout_ms));
    if (rpc_options.wait_for_ready) context.set_wait_for_ready(true);
    request.set_message(kRequestMessage_);
    if (rpc_options.server_fail) {
      request.mutable_param()->mutable_expected_error()->set_code(
          GRPC_STATUS_FAILED_PRECONDITION);
    }
    Status status;
    switch (rpc_options.service) {
      case SERVICE_ECHO:
        status =
            SendRpcMethod(&stub_, rpc_options, &context, request, response);
        break;
      case SERVICE_ECHO1:
        status =
            SendRpcMethod(&stub1_, rpc_options, &context, request, response);
        break;
      case SERVICE_ECHO2:
        status =
            SendRpcMethod(&stub2_, rpc_options, &context, request, response);
        break;
    }
    if (local_response) delete response;
    return status;
  }

  void CheckRpcSendOk(const size_t times = 1,
                      const RpcOptions& rpc_options = RpcOptions()) {
    for (size_t i = 0; i < times; ++i) {
      EchoResponse response;
      const Status status = SendRpc(rpc_options, &response);
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
  }

  void CheckRpcSendFailure(const size_t times = 1, bool server_fail = false) {
    for (size_t i = 0; i < times; ++i) {
      const Status status = SendRpc(RpcOptions().set_server_fail(server_fail));
      EXPECT_FALSE(status.ok());
    }
  }

  void SetRouteConfiguration(int idx, const RouteConfiguration& route_config) {
    if (GetParam().enable_rds_testing()) {
      balancers_[idx]->ads_service()->SetRdsResource(route_config,
                                                     kDefaultResourceName);
    } else {
      balancers_[idx]->ads_service()->SetLdsResource(
          AdsServiceImpl::BuildListener(route_config), kDefaultResourceName);
    }
  }

  AdsServiceImpl::ResponseState RouteConfigurationResponseState(int idx) const {
    AdsServiceImpl* ads_service = balancers_[idx]->ads_service();
    if (GetParam().enable_rds_testing()) {
      return ads_service->rds_response_state();
    }
    return ads_service->lds_response_state();
  }

 public:
  // This method could benefit test subclasses; to make it accessible
  // via bind with a qualified name, it needs to be public.
  void SetEdsResourceWithDelay(size_t i,
                               const ClusterLoadAssignment& assignment,
                               int delay_ms, const std::string& name) {
    GPR_ASSERT(delay_ms > 0);
    gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(delay_ms));
    balancers_[i]->ads_service()->SetEdsResource(assignment, name);
  }

 protected:
  class ServerThread {
   public:
    ServerThread() : port_(g_port_saver->GetPort()) {}
    virtual ~ServerThread(){};

    void Start() {
      gpr_log(GPR_INFO, "starting %s server on port %d", Type(), port_);
      GPR_ASSERT(!running_);
      running_ = true;
      StartAllServices();
      grpc_core::Mutex mu;
      // We need to acquire the lock here in order to prevent the notify_one
      // by ServerThread::Serve from firing before the wait below is hit.
      grpc_core::MutexLock lock(&mu);
      grpc_core::CondVar cond;
      thread_.reset(
          new std::thread(std::bind(&ServerThread::Serve, this, &mu, &cond)));
      cond.Wait(&mu);
      gpr_log(GPR_INFO, "%s server startup complete", Type());
    }

    void Serve(grpc_core::Mutex* mu, grpc_core::CondVar* cond) {
      // We need to acquire the lock here in order to prevent the notify_one
      // below from firing before its corresponding wait is executed.
      grpc_core::MutexLock lock(mu);
      std::ostringstream server_address;
      server_address << "localhost:" << port_;
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
    BackendServiceImpl<::grpc::testing::EchoTestService::Service>*
    backend_service() {
      return &backend_service_;
    }
    BackendServiceImpl<::grpc::testing::EchoTest1Service::Service>*
    backend_service1() {
      return &backend_service1_;
    }
    BackendServiceImpl<::grpc::testing::EchoTest2Service::Service>*
    backend_service2() {
      return &backend_service2_;
    }

   private:
    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(&backend_service_);
      builder->RegisterService(&backend_service1_);
      builder->RegisterService(&backend_service2_);
    }

    void StartAllServices() override {
      backend_service_.Start();
      backend_service1_.Start();
      backend_service2_.Start();
    }

    void ShutdownAllServices() override {
      backend_service_.Shutdown();
      backend_service1_.Shutdown();
      backend_service2_.Shutdown();
    }

    const char* Type() override { return "Backend"; }

    BackendServiceImpl<::grpc::testing::EchoTestService::Service>
        backend_service_;
    BackendServiceImpl<::grpc::testing::EchoTest1Service::Service>
        backend_service1_;
    BackendServiceImpl<::grpc::testing::EchoTest2Service::Service>
        backend_service2_;
  };

  class BalancerServerThread : public ServerThread {
   public:
    explicit BalancerServerThread(int client_load_reporting_interval = 0)
        : ads_service_(new AdsServiceImpl(client_load_reporting_interval > 0)),
          lrs_service_(new LrsServiceImpl(client_load_reporting_interval)) {}

    AdsServiceImpl* ads_service() { return ads_service_.get(); }
    LrsServiceImpl* lrs_service() { return lrs_service_.get(); }

   private:
    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(ads_service_.get());
      builder->RegisterService(lrs_service_.get());
    }

    void StartAllServices() override {
      ads_service_->Start();
      lrs_service_->Start();
    }

    void ShutdownAllServices() override {
      ads_service_->Shutdown();
      lrs_service_->Shutdown();
    }

    const char* Type() override { return "Balancer"; }

    std::shared_ptr<AdsServiceImpl> ads_service_;
    std::shared_ptr<LrsServiceImpl> lrs_service_;
  };

  const size_t num_backends_;
  const size_t num_balancers_;
  const int client_load_reporting_interval_seconds_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<grpc::testing::EchoTest1Service::Stub> stub1_;
  std::unique_ptr<grpc::testing::EchoTest2Service::Stub> stub2_;
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
      "    { \"eds_experimental\":{\n"
      "      \"clusterName\": \"application_target_name\",\n"
      "      \"lrsLoadReportingServerName\": \"\"\n"
      "    } }\n"
      "  ]\n"
      "}";
  const char* kDefaultServiceConfigWithoutLoadReporting_ =
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"does_not_exist\":{} },\n"
      "    { \"eds_experimental\":{\n"
      "      \"clusterName\": \"application_target_name\"\n"
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
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
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
  // Check LB policy name for the channel.
  EXPECT_EQ((GetParam().use_xds_resolver() ? "xds_routing_experimental"
                                           : "eds_experimental"),
            channel_->GetLoadBalancingPolicyName());
}

TEST_P(BasicTest, IgnoresUnhealthyEndpoints) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcsPerAddress = 100;
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0",
       GetBackendPorts(),
       kDefaultLocalityWeight,
       kDefaultLocalityPriority,
       {envoy::api::v2::HealthStatus::DRAINING}},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
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
}

// Tests that subchannel sharing works when the same backend is listed multiple
// times.
TEST_P(BasicTest, SameBackendListedMultipleTimes) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Same backend listed twice.
  std::vector<int> ports(2, backends_[0]->port());
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", ports},
  });
  const size_t kNumRpcsPerAddress = 10;
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
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
  AdsServiceImpl::EdsResourceArgs::Locality empty_locality("locality0", {});
  AdsServiceImpl::EdsResourceArgs args({
      empty_locality,
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Send non-empty serverlist only after kServerlistDelayMs.
  args = AdsServiceImpl::EdsResourceArgs({
      {"locality0", GetBackendPorts()},
  });
  std::thread delayed_resource_setter(
      std::bind(&BasicTest::SetEdsResourceWithDelay, this, 0,
                AdsServiceImpl::BuildEdsResource(args), kServerlistDelayMs,
                kDefaultResourceName));
  const auto t0 = system_clock::now();
  // Client will block: LB will initially send empty serverlist.
  CheckRpcSendOk(
      1, RpcOptions().set_timeout_ms(kCallDeadlineMs).set_wait_for_ready(true));
  const auto ellapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          system_clock::now() - t0);
  // but eventually, the LB sends a serverlist update that allows the call to
  // proceed. The call delay must be larger than the delay in sending the
  // populated serverlist but under the call's deadline (which is enforced by
  // the call's deadline).
  EXPECT_GT(ellapsed_ms.count(), kServerlistDelayMs);
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
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", ports},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  const Status status = SendRpc();
  // The error shouldn't be DEADLINE_EXCEEDED.
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
}

// Tests that RPCs fail when the backends are down, and will succeed again after
// the backends are restarted.
TEST_P(BasicTest, BackendsRestart) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  WaitForAllBackends();
  // Stop backends.  RPCs should fail.
  ShutdownAllBackends();
  // Sending multiple failed requests instead of just one to ensure that the
  // client notices that all backends are down before we restart them. If we
  // didn't do this, then a single RPC could fail here due to the race condition
  // between the LB pick and the GOAWAY from the chosen backend being shut down,
  // which would not actually prove that the client noticed that all of the
  // backends are down. Then, when we send another request below (which we
  // expect to succeed), if the callbacks happen in the wrong order, the same
  // race condition could happen again due to the client not yet having noticed
  // that the backends were all down.
  CheckRpcSendFailure(num_backends_);
  // Restart all backends.  RPCs should start succeeding again.
  StartAllBackends();
  CheckRpcSendOk(1, RpcOptions().set_timeout_ms(2000).set_wait_for_ready(true));
}

using XdsResolverOnlyTest = BasicTest;

// Tests switching over from one cluster to another.
TEST_P(XdsResolverOnlyTest, ChangeClusters) {
  const char* kNewClusterName = "new_cluster_name";
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // We need to wait for all backends to come online.
  WaitForAllBackends(0, 2);
  // Populate new EDS resource.
  AdsServiceImpl::EdsResourceArgs args2({
      {"locality0", GetBackendPorts(2, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args2, kNewClusterName),
      kNewClusterName);
  // Populate new CDS resource.
  Cluster new_cluster = balancers_[0]->ads_service()->default_cluster();
  new_cluster.set_name(kNewClusterName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster, kNewClusterName);
  // Change RDS resource to point to new cluster.
  RouteConfiguration new_route_config =
      balancers_[0]->ads_service()->default_route_config();
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  Listener listener =
      balancers_[0]->ads_service()->BuildListener(new_route_config);
  balancers_[0]->ads_service()->SetLdsResource(listener, kDefaultResourceName);
  // Wait for all new backends to be used.
  std::tuple<int, int, int> counts = WaitForAllBackends(2, 4);
  // Make sure no RPCs failed in the transition.
  EXPECT_EQ(0, std::get<1>(counts));
}

// Tests that things keep workng if the cluster resource disappears.
TEST_P(XdsResolverOnlyTest, ClusterRemoved) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Unset CDS resource.
  balancers_[0]->ads_service()->UnsetResource(kCdsTypeUrl,
                                              kDefaultResourceName);
  // Make sure RPCs are still succeeding.
  CheckRpcSendOk(100 * num_backends_);
  // Make sure we ACK'ed the update.
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state().state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Tests that we restart all xDS requests when we reestablish the ADS call.
TEST_P(XdsResolverOnlyTest, RestartsRequestsUponReconnection) {
  balancers_[0]->ads_service()->SetLdsToUseDynamicRds();
  const char* kNewClusterName = "new_cluster_name";
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // We need to wait for all backends to come online.
  WaitForAllBackends(0, 2);
  // Now shut down and restart the balancer.  When the client
  // reconnects, it should automatically restart the requests for all
  // resource types.
  balancers_[0]->Shutdown();
  balancers_[0]->Start();
  // Make sure things are still working.
  CheckRpcSendOk(100);
  // Populate new EDS resource.
  AdsServiceImpl::EdsResourceArgs args2({
      {"locality0", GetBackendPorts(2, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args2, kNewClusterName),
      kNewClusterName);
  // Populate new CDS resource.
  Cluster new_cluster = balancers_[0]->ads_service()->default_cluster();
  new_cluster.set_name(kNewClusterName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster, kNewClusterName);
  // Change RDS resource to point to new cluster.
  RouteConfiguration new_route_config =
      balancers_[0]->ads_service()->default_route_config();
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  balancers_[0]->ads_service()->SetRdsResource(new_route_config,
                                               kDefaultResourceName);
  // Wait for all new backends to be used.
  std::tuple<int, int, int> counts = WaitForAllBackends(2, 4);
  // Make sure no RPCs failed in the transition.
  EXPECT_EQ(0, std::get<1>(counts));
}

TEST_P(XdsResolverOnlyTest, DefaultRouteSpecifiesSlashPrefix) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_match()
      ->set_prefix("/");
  balancers_[0]->ads_service()->SetLdsResource(
      AdsServiceImpl::BuildListener(route_config), kDefaultResourceName);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
}

class XdsResolverLoadReportingOnlyTest : public XdsEnd2endTest {
 public:
  XdsResolverLoadReportingOnlyTest() : XdsEnd2endTest(4, 1, 3) {}
};

// Tests load reporting when switching over from one cluster to another.
TEST_P(XdsResolverLoadReportingOnlyTest, ChangeClusters) {
  const char* kNewClusterName = "new_cluster_name";
  balancers_[0]->lrs_service()->set_cluster_names(
      {kDefaultResourceName, kNewClusterName});
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // cluster kDefaultResourceName -> locality0 -> backends 0 and 1
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // cluster kNewClusterName -> locality1 -> backends 2 and 3
  AdsServiceImpl::EdsResourceArgs args2({
      {"locality1", GetBackendPorts(2, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args2, kNewClusterName),
      kNewClusterName);
  // CDS resource for kNewClusterName.
  Cluster new_cluster = balancers_[0]->ads_service()->default_cluster();
  new_cluster.set_name(kNewClusterName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster, kNewClusterName);
  // Wait for all backends to come online.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends(0, 2);
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancers_[0]->lrs_service()->WaitForLoadReport();
  EXPECT_THAT(
      load_report,
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&ClientStats::cluster_name, kDefaultResourceName),
          ::testing::Property(
              &ClientStats::locality_stats,
              ::testing::ElementsAre(::testing::Pair(
                  "locality0",
                  ::testing::AllOf(
                      ::testing::Field(&ClientStats::LocalityStats::
                                           total_successful_requests,
                                       num_ok),
                      ::testing::Field(&ClientStats::LocalityStats::
                                           total_requests_in_progress,
                                       0UL),
                      ::testing::Field(
                          &ClientStats::LocalityStats::total_error_requests,
                          num_failure),
                      ::testing::Field(
                          &ClientStats::LocalityStats::total_issued_requests,
                          num_failure + num_ok))))),
          ::testing::Property(&ClientStats::total_dropped_requests,
                              num_drops))));
  // Change RDS resource to point to new cluster.
  RouteConfiguration new_route_config =
      balancers_[0]->ads_service()->default_route_config();
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  Listener listener =
      balancers_[0]->ads_service()->BuildListener(new_route_config);
  balancers_[0]->ads_service()->SetLdsResource(listener, kDefaultResourceName);
  // Wait for all new backends to be used.
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends(2, 4);
  // The load report received at the balancer should be correct.
  load_report = balancers_[0]->lrs_service()->WaitForLoadReport();
  EXPECT_THAT(
      load_report,
      ::testing::ElementsAre(
          ::testing::AllOf(
              ::testing::Property(&ClientStats::cluster_name,
                                  kDefaultResourceName),
              ::testing::Property(
                  &ClientStats::locality_stats,
                  ::testing::ElementsAre(::testing::Pair(
                      "locality0",
                      ::testing::AllOf(
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_successful_requests,
                                           ::testing::Lt(num_ok)),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_requests_in_progress,
                                           0UL),
                          ::testing::Field(
                              &ClientStats::LocalityStats::total_error_requests,
                              ::testing::Le(num_failure)),
                          ::testing::Field(
                              &ClientStats::LocalityStats::
                                  total_issued_requests,
                              ::testing::Le(num_failure + num_ok)))))),
              ::testing::Property(&ClientStats::total_dropped_requests,
                                  num_drops)),
          ::testing::AllOf(
              ::testing::Property(&ClientStats::cluster_name, kNewClusterName),
              ::testing::Property(
                  &ClientStats::locality_stats,
                  ::testing::ElementsAre(::testing::Pair(
                      "locality1",
                      ::testing::AllOf(
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_successful_requests,
                                           ::testing::Le(num_ok)),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_requests_in_progress,
                                           0UL),
                          ::testing::Field(
                              &ClientStats::LocalityStats::total_error_requests,
                              ::testing::Le(num_failure)),
                          ::testing::Field(
                              &ClientStats::LocalityStats::
                                  total_issued_requests,
                              ::testing::Le(num_failure + num_ok)))))),
              ::testing::Property(&ClientStats::total_dropped_requests,
                                  num_drops))));
  int total_ok = 0;
  int total_failure = 0;
  for (const ClientStats& client_stats : load_report) {
    total_ok += client_stats.total_successful_requests();
    total_failure += client_stats.total_error_requests();
  }
  EXPECT_EQ(total_ok, num_ok);
  EXPECT_EQ(total_failure, num_failure);
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->response_count());
}

using SecureNamingTest = BasicTest;

// Tests that secure naming check passes if target name is expected.
TEST_P(SecureNamingTest, TargetNameIsExpected) {
  // TODO(juanlishen): Use separate fake creds for the balancer channel.
  ResetStub(0, kApplicationTargetName_ + ";lb");
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  const size_t kNumRpcsPerAddress = 100;
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
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
}

// Tests that secure naming check fails if target name is unexpected.
TEST_P(SecureNamingTest, TargetNameIsUnexpected) {
  gpr_setenv("GRPC_XDS_BOOTSTRAP", g_bootstrap_file_bad);
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  // Make sure that we blow up (via abort() from the security connector) when
  // the name from the balancer doesn't match expectations.
  ASSERT_DEATH_IF_SUPPORTED(
      {
        ResetStub(0, kApplicationTargetName_ + ";lb");
        SetNextResolution({});
        SetNextResolutionForLbChannel({balancers_[0]->port()});
        channel_->WaitForConnected(grpc_timeout_seconds_to_deadline(1));
      },
      "");
}

using LdsTest = BasicTest;

// Tests that LDS client should send a NACK if there is no API listener in the
// Listener in the LDS response.
TEST_P(LdsTest, NoApiListener) {
  auto listener = balancers_[0]->ads_service()->default_listener();
  listener.clear_api_listener();
  balancers_[0]->ads_service()->SetLdsResource(listener, kDefaultResourceName);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "Listener has no ApiListener.");
}

// Tests that LDS client should send a NACK if the route_specifier in the
// http_connection_manager is neither inlined route_config nor RDS.
TEST_P(LdsTest, WrongRouteSpecifier) {
  auto listener = balancers_[0]->ads_service()->default_listener();
  HttpConnectionManager http_connection_manager;
  http_connection_manager.mutable_scoped_routes();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancers_[0]->ads_service()->SetLdsResource(listener, kDefaultResourceName);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message,
            "HttpConnectionManager neither has inlined route_config nor RDS.");
}

using LdsRdsTest = BasicTest;

// Tests that LDS client should send an ACK upon correct LDS response (with
// inlined RDS result).
TEST_P(LdsRdsTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should send a NACK if matching domain can't be found in
// the LDS response.
TEST_P(LdsRdsTest, NoMatchedDomain) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message,
            "No matched virtual host found in the route config.");
}

// Tests that LDS client should choose the virtual host with matching domain if
// multiple virtual hosts exist in the LDS response.
TEST_P(LdsRdsTest, ChooseMatchedDomain) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  *(route_config.add_virtual_hosts()) = route_config.virtual_hosts(0);
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->mutable_cluster_header();
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should choose the last route in the virtual host if
// multiple routes exist in the LDS response.
TEST_P(LdsRdsTest, ChooseLastRoute) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  *(route_config.mutable_virtual_hosts(0)->add_routes()) =
      route_config.virtual_hosts(0).routes(0);
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->mutable_cluster_header();
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should send a NACK if route match has non-empty prefix
// as the only route (default) in the LDS response.
TEST_P(LdsRdsTest, RouteMatchHasNonemptyPrefix) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_match()
      ->set_prefix("/nonempty_prefix/");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message,
            "Default route must have empty service and method");
}

// Tests that LDS client should send a NACK if route match has a prefix
// string with no "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixNonEmptyNoSlash) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("grpc.testing.EchoTest1Service");
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultResourceName);
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "Prefix does not start with a /");
}

// Tests that LDS client should send a NACK if route match has a prefix
// string does not end with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixNoEndingSlash) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message,
            "Prefix not in the required format of /service/");
}

// Tests that LDS client should send a NACK if route match has a prefix
// string does not start with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixNoLeadingSlash) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("grpc.testing.EchoTest1Service/");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "Prefix does not start with a /");
}

// Tests that LDS client should send a NACK if route match has a prefix
// string with extra content outside of "/service/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixExtraContent) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/Echo1");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "Prefix does not end with a /");
}

// Tests that LDS client should send a NACK if route match has a prefix
// string "//".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixNoContent) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("//");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "Prefix contains empty service name");
}

// Tests that LDS client should send a NACK if route match has path
// but it's empty.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathEmptyPath) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultResourceName);
  route1->mutable_match()->set_path("");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "Path if set cannot be empty");
}

// Tests that LDS client should send a NACK if route match has path
// string does not start with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathNoLeadingSlash) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultResourceName);
  route1->mutable_match()->set_path("grpc.testing.EchoTest1Service/Echo1");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "Path does not start with a /");
}

// Tests that LDS client should send a NACK if route match has path
// string that ends with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathEndsWithSlash) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultResourceName);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/Echo1/");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message,
            "Path not in the required format of /service/method");
}

// Tests that LDS client should send a NACK if route match has path
// string that misses "/" between service and method.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathMissingMiddleSlash) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultResourceName);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service.Echo1");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message,
            "Path not in the required format of /service/method");
}

// Tests that LDS client should send a NACK if route match has path
// string that is missing service.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathMissingService) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultResourceName);
  route1->mutable_match()->set_path("//Echo1");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "Path contains empty service name");
}

// Tests that LDS client should send a NACK if route match has path
// string that is missing method.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathMissingMethod) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultResourceName);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "Path contains empty method name");
}

// Tests that LDS client should send a NACK if route has an action other than
// RouteAction in the LDS response.
TEST_P(LdsRdsTest, RouteHasNoRouteAction) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)->mutable_routes(0)->mutable_redirect();
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "No RouteAction found in route.");
}

// Tests that LDS client should send a NACK if RouteAction has a
// cluster_specifier other than cluster in the LDS response.
TEST_P(LdsRdsTest, RouteActionHasNoCluster) {
  RouteConfiguration route_config =
      balancers_[0]->ads_service()->default_route_config();
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->mutable_cluster_header();
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "No cluster found in RouteAction.");
}

// Tests that LDS client times out when no response received.
TEST_P(LdsRdsTest, Timeout) {
  ResetStub(0, "", 500);
  if (GetParam().enable_rds_testing()) {
    balancers_[0]->ads_service()->SetResourceIgnore(kRdsTypeUrl);
  } else {
    balancers_[0]->ads_service()->SetResourceIgnore(kLdsTypeUrl);
  }
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

// Tests that LDS client should choose the default route (with no matching
// specified) after unable to find a match with previous routes.
TEST_P(LdsRdsTest, XdsRoutingPathMatching) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEcho2Rpcs = 20;
  const size_t kNumEchoRpcs = 30;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 2)},
  });
  AdsServiceImpl::EdsResourceArgs args1({
      {"locality0", GetBackendPorts(2, 3)},
  });
  AdsServiceImpl::EdsResourceArgs args2({
      {"locality0", GetBackendPorts(3, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args1, kNewCluster1Name),
      kNewCluster1Name);
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args2, kNewCluster2Name),
      kNewCluster2Name);
  // Populate new CDS resources.
  Cluster new_cluster1 = balancers_[0]->ads_service()->default_cluster();
  new_cluster1.set_name(kNewCluster1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1, kNewCluster1Name);
  Cluster new_cluster2 = balancers_[0]->ads_service()->default_cluster();
  new_cluster2.set_name(kNewCluster2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2, kNewCluster2Name);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/Echo1");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_path("/grpc.testing.EchoTest2Service/Echo2");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* route3 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route3->mutable_match()->set_path("/grpc.testing.EchoTest3Service/Echo3");
  route3->mutable_route()->set_cluster(kDefaultResourceName);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultResourceName);
  SetRouteConfiguration(0, new_route_config);
  WaitForAllBackends(0, 2);
  CheckRpcSendOk(kNumEchoRpcs, RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(kNumEcho1Rpcs, RpcOptions()
                                    .set_rpc_service(SERVICE_ECHO1)
                                    .set_rpc_method(METHOD_ECHO1)
                                    .set_wait_for_ready(true));
  CheckRpcSendOk(kNumEcho2Rpcs, RpcOptions()
                                    .set_rpc_service(SERVICE_ECHO2)
                                    .set_rpc_method(METHOD_ECHO2)
                                    .set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(kNumEchoRpcs / 2,
              backends_[i]->backend_service()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service1()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service2()->request_count());
  }
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_EQ(kNumEcho2Rpcs, backends_[3]->backend_service2()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingPrefixMatching) {
  ResetStub(/*failover_timeout=*/0,
            /*expected_targets=*/"",
            /*xds_resource_does_not_exist_timeout*/ 0,
            /*xds_routing_enabled=*/true);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEcho2Rpcs = 20;
  const size_t kNumEchoRpcs = 30;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 2)},
  });
  AdsServiceImpl::EdsResourceArgs args1({
      {"locality0", GetBackendPorts(2, 3)},
  });
  AdsServiceImpl::EdsResourceArgs args2({
      {"locality0", GetBackendPorts(3, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args1, kNewCluster1Name),
      kNewCluster1Name);
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args2, kNewCluster2Name),
      kNewCluster2Name);
  // Populate new CDS resources.
  Cluster new_cluster1 = balancers_[0]->ads_service()->default_cluster();
  new_cluster1.set_name(kNewCluster1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1, kNewCluster1Name);
  Cluster new_cluster2 = balancers_[0]->ads_service()->default_cluster();
  new_cluster2.set_name(kNewCluster2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2, kNewCluster2Name);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config =
      balancers_[0]->ads_service()->default_route_config();
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_prefix("/grpc.testing.EchoTest2Service/");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultResourceName);
  SetRouteConfiguration(0, new_route_config);
  WaitForAllBackends(0, 2);
  CheckRpcSendOk(kNumEchoRpcs, RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(
      kNumEcho1Rpcs,
      RpcOptions().set_rpc_service(SERVICE_ECHO1).set_wait_for_ready(true));
  CheckRpcSendOk(
      kNumEcho2Rpcs,
      RpcOptions().set_rpc_service(SERVICE_ECHO2).set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(kNumEchoRpcs / 2,
              backends_[i]->backend_service()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service1()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service2()->request_count());
  }
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_EQ(kNumEcho2Rpcs, backends_[3]->backend_service2()->request_count());
}

using CdsTest = BasicTest;

// Tests that CDS client should send an ACK upon correct CDS response.
TEST_P(CdsTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state().state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Tests that CDS client should send a NACK if the cluster type in CDS response
// is other than EDS.
TEST_P(CdsTest, WrongClusterType) {
  auto cluster = balancers_[0]->ads_service()->default_cluster();
  cluster.set_type(envoy::api::v2::Cluster::STATIC);
  balancers_[0]->ads_service()->SetCdsResource(cluster, kDefaultResourceName);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "DiscoveryType is not EDS.");
}

// Tests that CDS client should send a NACK if the eds_config in CDS response is
// other than ADS.
TEST_P(CdsTest, WrongEdsConfig) {
  auto cluster = balancers_[0]->ads_service()->default_cluster();
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  balancers_[0]->ads_service()->SetCdsResource(cluster, kDefaultResourceName);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "EDS ConfigSource is not ADS.");
}

// Tests that CDS client should send a NACK if the lb_policy in CDS response is
// other than ROUND_ROBIN.
TEST_P(CdsTest, WrongLbPolicy) {
  auto cluster = balancers_[0]->ads_service()->default_cluster();
  cluster.set_lb_policy(envoy::api::v2::Cluster::LEAST_REQUEST);
  balancers_[0]->ads_service()->SetCdsResource(cluster, kDefaultResourceName);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "LB policy is not ROUND_ROBIN.");
}

// Tests that CDS client should send a NACK if the lrs_server in CDS response is
// other than SELF.
TEST_P(CdsTest, WrongLrsServer) {
  auto cluster = balancers_[0]->ads_service()->default_cluster();
  cluster.mutable_lrs_server()->mutable_ads();
  balancers_[0]->ads_service()->SetCdsResource(cluster, kDefaultResourceName);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  const auto& response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message, "LRS ConfigSource is not self.");
}

// Tests that CDS client times out when no response received.
TEST_P(CdsTest, Timeout) {
  ResetStub(0, "", 500);
  balancers_[0]->ads_service()->SetResourceIgnore(kCdsTypeUrl);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

using EdsTest = BasicTest;

TEST_P(EdsTest, Timeout) {
  ResetStub(0, "", 500);
  balancers_[0]->ads_service()->SetResourceIgnore(kEdsTypeUrl);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

// Tests that EDS client should send a NACK if the EDS update contains
// sparse priorities.
TEST_P(EdsTest, NacksSparsePriorityList) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(), kDefaultLocalityWeight, 1},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  CheckRpcSendFailure();
  const auto& response_state =
      balancers_[0]->ads_service()->eds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_EQ(response_state.error_message,
            "EDS update includes sparse priority list");
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
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 1), kLocalityWeight0},
      {"locality1", GetBackendPorts(1, 2), kLocalityWeight1},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
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
}

// Tests that we correctly handle a locality containing no endpoints.
TEST_P(LocalityMapTest, LocalityContainingNoEndpoints) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 5000;
  // EDS response contains 2 localities, one with no endpoints.
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
      {"locality1", {}},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Wait for both backends to be ready.
  WaitForAllBackends();
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(kNumRpcs);
  // All traffic should go to the reachable locality.
  EXPECT_EQ(backends_[0]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
  EXPECT_EQ(backends_[1]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
  EXPECT_EQ(backends_[2]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
  EXPECT_EQ(backends_[3]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
}

// EDS update with no localities.
TEST_P(LocalityMapTest, NoLocalities) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // EDS response contains 2 localities, one with no endpoints.
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource({}), kDefaultResourceName);
  Status status = SendRpc();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
}

// Tests that the locality map can work properly even when it contains a large
// number of localities.
TEST_P(LocalityMapTest, StressTest) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumLocalities = 100;
  // The first ADS response contains kNumLocalities localities, each of which
  // contains backend 0.
  AdsServiceImpl::EdsResourceArgs args;
  for (size_t i = 0; i < kNumLocalities; ++i) {
    std::string name = absl::StrCat("locality", i);
    AdsServiceImpl::EdsResourceArgs::Locality locality(name,
                                                       {backends_[0]->port()});
    args.locality_list.emplace_back(std::move(locality));
  }
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // The second ADS response contains 1 locality, which contains backend 1.
  args = AdsServiceImpl::EdsResourceArgs({
      {"locality0", GetBackendPorts(1, 2)},
  });
  std::thread delayed_resource_setter(std::bind(
      &BasicTest::SetEdsResourceWithDelay, this, 0,
      AdsServiceImpl::BuildEdsResource(args), 60 * 1000, kDefaultResourceName));
  // Wait until backend 0 is ready, before which kNumLocalities localities are
  // received and handled by the xds policy.
  WaitForBackend(0, /*reset_counters=*/false);
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  // Wait until backend 1 is ready, before which kNumLocalities localities are
  // removed by the xds policy.
  WaitForBackend(1);
  delayed_resource_setter.join();
}

// Tests that the localities in a locality map are picked correctly after update
// (addition, modification, deletion).
TEST_P(LocalityMapTest, UpdateMap) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 3000;
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
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 1), 2},
      {"locality1", GetBackendPorts(1, 2), 3},
      {"locality2", GetBackendPorts(2, 3), 4},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
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
    gpr_log(GPR_INFO, "Locality %" PRIuPTR " rate %f", i,
            locality_picked_rates[i]);
    EXPECT_THAT(
        locality_picked_rates[i],
        ::testing::AllOf(
            ::testing::Ge(locality_weight_rate_0[i] * (1 - kErrorTolerance)),
            ::testing::Le(locality_weight_rate_0[i] * (1 + kErrorTolerance))));
  }
  args = AdsServiceImpl::EdsResourceArgs({
      {"locality1", GetBackendPorts(1, 2), 3},
      {"locality2", GetBackendPorts(2, 3), 2},
      {"locality3", GetBackendPorts(3, 4), 6},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Backend 3 hasn't received any request.
  EXPECT_EQ(0U, backends_[3]->backend_service()->request_count());
  // Wait until the locality update has been processed, as signaled by backend 3
  // receiving a request.
  WaitForAllBackends(3, 4);
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
    gpr_log(GPR_INFO, "Locality %" PRIuPTR " rate %f", i,
            locality_picked_rates[i]);
    EXPECT_THAT(
        locality_picked_rates[i],
        ::testing::AllOf(
            ::testing::Ge(locality_weight_rate_1[i] * (1 - kErrorTolerance)),
            ::testing::Le(locality_weight_rate_1[i] * (1 + kErrorTolerance))));
  }
}

// Tests that we don't fail RPCs when replacing all of the localities in
// a given priority.
TEST_P(LocalityMapTest, ReplaceAllLocalitiesInPriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  args = AdsServiceImpl::EdsResourceArgs({
      {"locality1", GetBackendPorts(1, 2)},
  });
  std::thread delayed_resource_setter(std::bind(
      &BasicTest::SetEdsResourceWithDelay, this, 0,
      AdsServiceImpl::BuildEdsResource(args), 5000, kDefaultResourceName));
  // Wait for the first backend to be ready.
  WaitForBackend(0);
  // Keep sending RPCs until we switch over to backend 1, which tells us
  // that we received the update.  No RPCs should fail during this
  // transition.
  WaitForBackend(1, /*reset_counters=*/true, /*require_success=*/true);
  delayed_resource_setter.join();
}

class FailoverTest : public BasicTest {
 public:
  void SetUp() override {
    BasicTest::SetUp();
    ResetStub(100, "");
  }
};

// Localities with the highest priority are used when multiple priority exist.
TEST_P(FailoverTest, ChooseHighestPriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 1},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 2},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 3},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 0},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  WaitForBackend(3, false);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
}

// Does not choose priority with no endpoints.
TEST_P(FailoverTest, DoesNotUsePriorityWithNoEndpoints) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 1},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 2},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 3},
      {"locality3", {}, kDefaultLocalityWeight, 0},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  WaitForBackend(0, false);
  for (size_t i = 1; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
}

// Does not choose locality with no endpoints.
TEST_P(FailoverTest, DoesNotUseLocalityWithNoEndpoints) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", {}, kDefaultLocalityWeight, 0},
      {"locality1", GetBackendPorts(), kDefaultLocalityWeight, 0},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Wait for all backends to be used.
  std::tuple<int, int, int> counts = WaitForAllBackends();
  // Make sure no RPCs failed in the transition.
  EXPECT_EQ(0, std::get<1>(counts));
}

// If the higher priority localities are not reachable, failover to the highest
// priority among the rest.
TEST_P(FailoverTest, Failover) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 1},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 2},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 3},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 0},
  });
  ShutdownBackend(3);
  ShutdownBackend(0);
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  WaitForBackend(1, false);
  for (size_t i = 0; i < 4; ++i) {
    if (i == 1) continue;
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
}

// If a locality with higher priority than the current one becomes ready,
// switch to it.
TEST_P(FailoverTest, SwitchBackToHigherPriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 100;
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 1},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 2},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 3},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 0},
  });
  ShutdownBackend(3);
  ShutdownBackend(0);
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  WaitForBackend(1, false);
  for (size_t i = 0; i < 4; ++i) {
    if (i == 1) continue;
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  StartBackend(0);
  WaitForBackend(0);
  CheckRpcSendOk(kNumRpcs);
  EXPECT_EQ(kNumRpcs, backends_[0]->backend_service()->request_count());
}

// The first update only contains unavailable priorities. The second update
// contains available priorities.
TEST_P(FailoverTest, UpdateInitialUnavailable) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 0},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 1},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  args = AdsServiceImpl::EdsResourceArgs({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 0},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 1},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 2},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 3},
  });
  ShutdownBackend(0);
  ShutdownBackend(1);
  std::thread delayed_resource_setter(std::bind(
      &BasicTest::SetEdsResourceWithDelay, this, 0,
      AdsServiceImpl::BuildEdsResource(args), 1000, kDefaultResourceName));
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
  delayed_resource_setter.join();
}

// Tests that after the localities' priorities are updated, we still choose the
// highest READY priority with the updated localities.
TEST_P(FailoverTest, UpdatePriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 100;
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 1},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 2},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 3},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 0},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  args = AdsServiceImpl::EdsResourceArgs({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 2},
      {"locality1", GetBackendPorts(1, 2), kDefaultLocalityWeight, 0},
      {"locality2", GetBackendPorts(2, 3), kDefaultLocalityWeight, 1},
      {"locality3", GetBackendPorts(3, 4), kDefaultLocalityWeight, 3},
  });
  std::thread delayed_resource_setter(std::bind(
      &BasicTest::SetEdsResourceWithDelay, this, 0,
      AdsServiceImpl::BuildEdsResource(args), 1000, kDefaultResourceName));
  WaitForBackend(3, false);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  WaitForBackend(1);
  CheckRpcSendOk(kNumRpcs);
  EXPECT_EQ(kNumRpcs, backends_[1]->backend_service()->request_count());
  delayed_resource_setter.join();
}

// Moves all localities in the current priority to a higher priority.
TEST_P(FailoverTest, MoveAllLocalitiesInCurrentPriorityToHigherPriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // First update:
  // - Priority 0 is locality 0, containing backend 0, which is down.
  // - Priority 1 is locality 1, containing backends 1 and 2, which are up.
  ShutdownBackend(0);
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 0},
      {"locality1", GetBackendPorts(1, 3), kDefaultLocalityWeight, 1},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Second update:
  // - Priority 0 contains both localities 0 and 1.
  // - Priority 1 is not present.
  // - We add backend 3 to locality 1, just so we have a way to know
  //   when the update has been seen by the client.
  args = AdsServiceImpl::EdsResourceArgs({
      {"locality0", GetBackendPorts(0, 1), kDefaultLocalityWeight, 0},
      {"locality1", GetBackendPorts(1, 4), kDefaultLocalityWeight, 0},
  });
  std::thread delayed_resource_setter(std::bind(
      &BasicTest::SetEdsResourceWithDelay, this, 0,
      AdsServiceImpl::BuildEdsResource(args), 1000, kDefaultResourceName));
  // When we get the first update, all backends in priority 0 are down,
  // so we will create priority 1.  Backends 1 and 2 should have traffic,
  // but backend 3 should not.
  WaitForAllBackends(1, 3, false);
  EXPECT_EQ(0UL, backends_[3]->backend_service()->request_count());
  // When backend 3 gets traffic, we know the second update has been seen.
  WaitForBackend(3);
  // The ADS service of balancer 0 got at least 1 response.
  EXPECT_GT(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
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
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  WaitForAllBackends();
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
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
}

// Tests that drop config is converted correctly from per hundred.
TEST_P(DropTest, DropPerHundred) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 5000;
  const uint32_t kDropPerHundredForLb = 10;
  const double kDropRateForLb = kDropPerHundredForLb / 100.0;
  // The ADS response contains one drop category.
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, kDropPerHundredForLb}};
  args.drop_denominator = FractionalPercent::HUNDRED;
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  WaitForAllBackends();
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
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
}

// Tests that drop config is converted correctly from per ten thousand.
TEST_P(DropTest, DropPerTenThousand) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 5000;
  const uint32_t kDropPerTenThousandForLb = 1000;
  const double kDropRateForLb = kDropPerTenThousandForLb / 10000.0;
  // The ADS response contains one drop category.
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, kDropPerTenThousandForLb}};
  args.drop_denominator = FractionalPercent::TEN_THOUSAND;
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  WaitForAllBackends();
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
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
}

// Tests that drop is working correctly after update.
TEST_P(DropTest, Update) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 3000;
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 200000;
  const double kDropRateForLb = kDropPerMillionForLb / 1000000.0;
  const double kDropRateForThrottle = kDropPerMillionForThrottle / 1000000.0;
  const double KDropRateForLbAndThrottle =
      kDropRateForLb + (1 - kDropRateForLb) * kDropRateForThrottle;
  // The first ADS response contains one drop category.
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb}};
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  WaitForAllBackends();
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = 0;
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
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
  gpr_log(GPR_INFO, "First batch drop rate %f", seen_drop_rate);
  const double kErrorTolerance = 0.3;
  EXPECT_THAT(
      seen_drop_rate,
      ::testing::AllOf(::testing::Ge(kDropRateForLb * (1 - kErrorTolerance)),
                       ::testing::Le(kDropRateForLb * (1 + kErrorTolerance))));
  // The second ADS response contains two drop categories, send an update EDS
  // response.
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Wait until the drop rate increases to the middle of the two configs, which
  // implies that the update has been in effect.
  const double kDropRateThreshold =
      (kDropRateForLb + KDropRateForLbAndThrottle) / 2;
  size_t num_rpcs = kNumRpcs;
  while (seen_drop_rate < kDropRateThreshold) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
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
    const Status status = SendRpc(RpcOptions(), &response);
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
  gpr_log(GPR_INFO, "Second batch drop rate %f", seen_drop_rate);
  EXPECT_THAT(
      seen_drop_rate,
      ::testing::AllOf(
          ::testing::Ge(KDropRateForLbAndThrottle * (1 - kErrorTolerance)),
          ::testing::Le(KDropRateForLbAndThrottle * (1 + kErrorTolerance))));
}

// Tests that all the RPCs are dropped if any drop category drops 100%.
TEST_P(DropTest, DropAll) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 1000;
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 1000000;
  // The ADS response contains two drop categories.
  AdsServiceImpl::EdsResourceArgs args;
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Send kNumRpcs RPCs and all of them are dropped.
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
    EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
    EXPECT_EQ(status.error_message(), "Call dropped by load balancing policy");
  }
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
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", {backends_[0]->port()}},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  args = AdsServiceImpl::EdsResourceArgs({
      {"locality0", {backends_[1]->port()}},
  });
  balancers_[1]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Wait until the first backend is ready.
  WaitForBackend(0);
  // Send 10 requests.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->backend_service()->request_count());
  // The ADS service of balancer 0 sent at least 1 response.
  EXPECT_GT(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
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
  // The ADS service of balancer 0 sent at least 1 response.
  EXPECT_GT(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
}

// Tests that the old LB call is still used after multiple balancer address
// updates as long as that call is still alive. Send an update with the same set
// of LBs as the one in SetUp() in order to verify that the LB channel inside
// xds keeps the initial connection (which by definition is also present in the
// update).
TEST_P(BalancerUpdateTest, Repeated) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", {backends_[0]->port()}},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  args = AdsServiceImpl::EdsResourceArgs({
      {"locality0", {backends_[1]->port()}},
  });
  balancers_[1]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Wait until the first backend is ready.
  WaitForBackend(0);
  // Send 10 requests.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->backend_service()->request_count());
  // The ADS service of balancer 0 sent at least 1 response.
  EXPECT_GT(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
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
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", {backends_[0]->port()}},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  args = AdsServiceImpl::EdsResourceArgs({
      {"locality0", {backends_[1]->port()}},
  });
  balancers_[1]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Start servers and send 10 RPCs per server.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->backend_service()->request_count());
  // The ADS service of balancer 0 sent at least 1 response.
  EXPECT_GT(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
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
  // The ADS service of no balancers sent anything
  EXPECT_EQ(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
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
  // The ADS service of balancer 1 sent at least 1 response.
  EXPECT_EQ(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_GT(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
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
  const size_t kNumRpcsPerAddress = 10;
  const size_t kNumFailuresPerAddress = 3;
  // TODO(juanlishen): Partition the backends after multiple localities is
  // tested.
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Wait until all backends are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * num_backends_);
  CheckRpcSendFailure(kNumFailuresPerAddress * num_backends_,
                      /*server_fail=*/true);
  // Check that each backend got the right number of requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress + kNumFailuresPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancers_[0]->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  ClientStats& client_stats = load_report.front();
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_ + num_ok,
            client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ((kNumRpcsPerAddress + kNumFailuresPerAddress) * num_backends_ +
                num_ok + num_failure,
            client_stats.total_issued_requests());
  EXPECT_EQ(kNumFailuresPerAddress * num_backends_ + num_failure,
            client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->response_count());
}

// Tests that we don't include stats for clusters that are not requested
// by the LRS server.
TEST_P(ClientLoadReportingTest, HonorsClustersRequestedByLrsServer) {
  balancers_[0]->lrs_service()->set_cluster_names({"bogus"});
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  const size_t kNumRpcsPerAddress = 100;
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
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
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->response_count());
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancers_[0]->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 0UL);
}

// Tests that if the balancer restarts, the client load report contains the
// stats before and after the restart correctly.
TEST_P(ClientLoadReportingTest, BalancerRestart) {
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  const size_t kNumBackendsFirstPass = backends_.size() / 2;
  const size_t kNumBackendsSecondPass =
      backends_.size() - kNumBackendsFirstPass;
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts(0, kNumBackendsFirstPass)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Wait until all backends returned by the balancer are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) =
      WaitForAllBackends(/* start_index */ 0,
                         /* stop_index */ kNumBackendsFirstPass);
  std::vector<ClientStats> load_report =
      balancers_[0]->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  ClientStats client_stats = std::move(load_report.front());
  EXPECT_EQ(static_cast<size_t>(num_ok),
            client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ(0U, client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
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
  balancers_[0]->Start();
  args = AdsServiceImpl::EdsResourceArgs({
      {"locality0", GetBackendPorts(kNumBackendsFirstPass)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  // Wait for queries to start going to one of the new backends.
  // This tells us that we're now using the new serverlist.
  std::tie(num_ok, num_failure, num_drops) =
      WaitForAllBackends(/* start_index */ kNumBackendsFirstPass);
  num_started += num_ok + num_failure + num_drops;
  // Send one RPC per backend.
  CheckRpcSendOk(kNumBackendsSecondPass);
  num_started += kNumBackendsSecondPass;
  // Check client stats.
  load_report = balancers_[0]->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  client_stats = std::move(load_report.front());
  EXPECT_EQ(num_started, client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ(0U, client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
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
  AdsServiceImpl::EdsResourceArgs args({
      {"locality0", GetBackendPorts()},
  });
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResource(
      AdsServiceImpl::BuildEdsResource(args), kDefaultResourceName);
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends();
  const size_t num_warmup = num_ok + num_failure + num_drops;
  // Send kNumRpcs RPCs and count the drops.
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
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
  std::vector<ClientStats> load_report =
      balancers_[0]->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  ClientStats& client_stats = load_report.front();
  EXPECT_EQ(num_drops, client_stats.total_dropped_requests());
  const size_t total_rpc = num_warmup + kNumRpcs;
  EXPECT_THAT(
      client_stats.dropped_requests(kLbDropType),
      ::testing::AllOf(
          ::testing::Ge(total_rpc * kDropRateForLb * (1 - kErrorTolerance)),
          ::testing::Le(total_rpc * kDropRateForLb * (1 + kErrorTolerance))));
  EXPECT_THAT(client_stats.dropped_requests(kThrottleDropType),
              ::testing::AllOf(
                  ::testing::Ge(total_rpc * (1 - kDropRateForLb) *
                                kDropRateForThrottle * (1 - kErrorTolerance)),
                  ::testing::Le(total_rpc * (1 - kDropRateForLb) *
                                kDropRateForThrottle * (1 + kErrorTolerance))));
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

// LDS RDS Commmon tests  depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(XdsTest, LdsRdsTest,
                         ::testing::Values(TestType(true, false),
                                           TestType(true, true),
                                           TestType(true, false, true),
                                           TestType(true, true, true)),
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

// XdsResolverOnlyTest depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(XdsTest, XdsResolverOnlyTest,
                         ::testing::Values(TestType(true, false),
                                           TestType(true, true)),
                         &TestTypeName);

// XdsResolverLoadReprtingOnlyTest depends on XdsResolver and load reporting.
INSTANTIATE_TEST_SUITE_P(XdsTest, XdsResolverLoadReportingOnlyTest,
                         ::testing::Values(TestType(true, true)),
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
