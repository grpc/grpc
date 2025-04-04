//
// Copyright 2017 gRPC authors.
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

#ifndef GRPC_TEST_CPP_END2END_XDS_XDS_SERVER_H
#define GRPC_TEST_CPP_END2END_XDS_XDS_SERVER_H

#include <grpcpp/support/status.h>

#include <deque>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/util/crash.h"
#include "src/core/util/sync.h"
#include "src/proto/grpc/testing/xds/v3/ads.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/discovery.pb.h"
#include "src/proto/grpc/testing/xds/v3/lrs.grpc.pb.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/counted_service.h"

namespace grpc {
namespace testing {

constexpr char kLdsTypeUrl[] =
    "type.googleapis.com/envoy.config.listener.v3.Listener";
constexpr char kRdsTypeUrl[] =
    "type.googleapis.com/envoy.config.route.v3.RouteConfiguration";
constexpr char kCdsTypeUrl[] =
    "type.googleapis.com/envoy.config.cluster.v3.Cluster";
constexpr char kEdsTypeUrl[] =
    "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment";

// An ADS service implementation.
class AdsServiceImpl
    : public CountedService<
          ::envoy::service::discovery::v3::AggregatedDiscoveryService::CallbackService>,
      public std::enable_shared_from_this<AdsServiceImpl> {
 public:
  using DiscoveryRequest = ::envoy::service::discovery::v3::DiscoveryRequest;
  using DiscoveryResponse = ::envoy::service::discovery::v3::DiscoveryResponse;

  // State for a given xDS resource type.
  struct ResponseState {
    enum State {
      ACKED,   // ACK received.
      NACKED,  // NACK received; error_message will contain the error.
    };
    State state = ACKED;
    std::string error_message;
  };

  explicit AdsServiceImpl(
      std::function<void(const DiscoveryRequest& request)> check_first_request =
          nullptr,
      std::function<void(absl::StatusCode)> check_nack_status_code = nullptr,
      absl::string_view debug_label = "")
      : check_first_request_(std::move(check_first_request)),
        check_nack_status_code_(std::move(check_nack_status_code)),
        debug_label_(absl::StrFormat(
            "%p%s%s", this, debug_label.empty() ? "" : ":", debug_label)) {}

  void set_wrap_resources(bool wrap_resources) {
    grpc_core::MutexLock lock(&ads_mu_);
    wrap_resources_ = wrap_resources;
  }

  // Sets a resource to a particular value, overwriting any previous value.
  void SetResource(google::protobuf::Any resource, const std::string& type_url,
                   const std::string& name);

  // Removes a resource from the server's state.
  void UnsetResource(const std::string& type_url, const std::string& name);

  void SetLdsResource(const ::envoy::config::listener::v3::Listener& listener) {
    google::protobuf::Any resource;
    resource.PackFrom(listener);
    SetResource(std::move(resource), kLdsTypeUrl, listener.name());
  }

  void SetRdsResource(
      const ::envoy::config::route::v3::RouteConfiguration& route) {
    google::protobuf::Any resource;
    resource.PackFrom(route);
    SetResource(std::move(resource), kRdsTypeUrl, route.name());
  }

  void SetCdsResource(const ::envoy::config::cluster::v3::Cluster& cluster) {
    google::protobuf::Any resource;
    resource.PackFrom(cluster);
    SetResource(std::move(resource), kCdsTypeUrl, cluster.name());
  }

  void SetEdsResource(
      const ::envoy::config::endpoint::v3::ClusterLoadAssignment& assignment) {
    google::protobuf::Any resource;
    resource.PackFrom(assignment);
    SetResource(std::move(resource), kEdsTypeUrl, assignment.cluster_name());
  }

  // Tells the server to ignore requests from the client for a given
  // resource type.
  void IgnoreResourceType(const std::string& type_url) {
    grpc_core::MutexLock lock(&ads_mu_);
    resource_types_to_ignore_.emplace(type_url);
  }

  // Sets a callback to be invoked on request messages with respoonse_nonce
  // set.  The callback is passed the resource type and version.
  void SetCheckVersionCallback(
      std::function<void(absl::string_view, int)> check_version_callback) {
    grpc_core::MutexLock lock(&ads_mu_);
    check_version_callback_ = std::move(check_version_callback);
  }

  // Get the list of response state for each resource type.
  // TODO(roth): Consider adding an absl::Notification-based mechanism
  // here to avoid the need for tests to poll the response state.
  std::optional<ResponseState> GetResponseState(const std::string& type_url) {
    grpc_core::MutexLock lock(&ads_mu_);
    if (resource_type_response_state_[type_url].empty()) {
      return std::nullopt;
    }
    auto response = resource_type_response_state_[type_url].front();
    resource_type_response_state_[type_url].pop_front();
    return response;
  }
  std::optional<ResponseState> lds_response_state() {
    return GetResponseState(kLdsTypeUrl);
  }
  std::optional<ResponseState> rds_response_state() {
    return GetResponseState(kRdsTypeUrl);
  }
  std::optional<ResponseState> cds_response_state() {
    return GetResponseState(kCdsTypeUrl);
  }
  std::optional<ResponseState> eds_response_state() {
    return GetResponseState(kEdsTypeUrl);
  }

  // Starts the service.
  void Start() {}

  // Shuts down the service.
  void Shutdown();

  // Returns the peer names of clients currently connected to the service.
  std::set<std::string> clients() {
    grpc_core::MutexLock lock(&clients_mu_);
    return clients_;
  }

  void ForceADSFailure(Status status) {
    grpc_core::MutexLock lock(&ads_mu_);
    forced_ads_failure_ = std::move(status);
  }

  void ClearADSFailure() {
    grpc_core::MutexLock lock(&ads_mu_);
    forced_ads_failure_ = std::nullopt;
  }

 private:
  class Reactor
      : public ServerBidiReactor<DiscoveryRequest, DiscoveryResponse> {
   public:
    Reactor(std::shared_ptr<AdsServiceImpl> ads_service_impl,
            CallbackServerContext* context);

    void MaybeStartWrite(const std::string& resource_type)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&AdsServiceImpl::ads_mu_);

   private:
    // State for a given resource type.
    struct ResourceTypeState {
      int nonce = 0;
      int resource_type_version = 0;
      absl::flat_hash_map<std::string /*resource_name*/,
                          bool /*new_subscription*/> subscriptions;
    };

    void OnReadDone(bool ok) override;
    void OnWriteDone(bool ok) override;
    void OnDone() override;
    void OnCancel() override;

    void MaybeStartNextWrite()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&AdsServiceImpl::ads_mu_);

    std::shared_ptr<AdsServiceImpl> ads_service_impl_;
    CallbackServerContext* context_;

// FIXME: use our own lock

    std::map<std::string /*type_url*/, ResourceTypeState> type_state_map_
        ABSL_GUARDED_BY(&AdsServiceImpl::ads_mu_);
    bool seen_first_request_ ABSL_GUARDED_BY(&AdsServiceImpl::ads_mu_) = false;
    bool write_pending_ ABSL_GUARDED_BY(&AdsServiceImpl::ads_mu_) = false;
    std::set<std::string /*type_url*/> response_needed_
        ABSL_GUARDED_BY(&AdsServiceImpl::ads_mu_);

    DiscoveryRequest request_;
    DiscoveryResponse response_;
  };

  // A struct representing the current state for an individual resource.
  struct ResourceState {
    // The resource itself, if present.
    std::optional<google::protobuf::Any> resource;
    // The resource type version that this resource was last updated in.
    int resource_type_version = 0;
    // A list of subscriptions to this resource.
    std::set<Reactor*> subscriptions;
  };

  // The current state for all individual resources of a given type.
  using ResourceNameMap =
      absl::flat_hash_map<std::string /*resource_name*/, ResourceState>;

  struct ResourceTypeState {
    int resource_type_version = 0;
    ResourceNameMap resource_name_map;
  };

  using ResourceMap = std::map<std::string /*type_url*/, ResourceTypeState>;

  ServerBidiReactor<DiscoveryRequest, DiscoveryResponse>*
  StreamAggregatedResources(CallbackServerContext* context) override {
    return new Reactor(shared_from_this(), context);
  }

  void AddClient(const std::string& client) {
    grpc_core::MutexLock lock(&clients_mu_);
    clients_.insert(client);
  }

  void RemoveClient(const std::string& client) {
    grpc_core::MutexLock lock(&clients_mu_);
    clients_.erase(client);
  }

  std::function<void(const DiscoveryRequest& request)> check_first_request_;
  std::function<void(absl::StatusCode)> check_nack_status_code_;
  std::string debug_label_;

  grpc_core::Mutex ads_mu_;
// FIXME: combine various maps
  std::map<std::string /*type_url*/, std::deque<ResponseState>>
      resource_type_response_state_ ABSL_GUARDED_BY(ads_mu_);
  std::set<std::string /*resource_type*/> resource_types_to_ignore_
      ABSL_GUARDED_BY(ads_mu_);
  std::function<void(absl::string_view, int)> check_version_callback_
      ABSL_GUARDED_BY(ads_mu_);
  // An instance data member containing the current state of all resources.
  // Note that an entry will exist whenever either of the following is true:
  // - The resource exists (i.e., has been created by SetResource() and has not
  //   yet been destroyed by UnsetResource()).
  // - There is at least one subscription for the resource.
  ResourceMap resource_map_ ABSL_GUARDED_BY(ads_mu_);
  std::optional<Status> forced_ads_failure_ ABSL_GUARDED_BY(ads_mu_);
  bool wrap_resources_ ABSL_GUARDED_BY(ads_mu_) = false;

  grpc_core::Mutex clients_mu_;
  std::set<std::string> clients_ ABSL_GUARDED_BY(clients_mu_);
};

// An LRS service implementation.
class LrsServiceImpl
    : public CountedService<
          ::envoy::service::load_stats::v3::LoadReportingService::Service>,
      public std::enable_shared_from_this<LrsServiceImpl> {
 public:
  using LoadStatsRequest = ::envoy::service::load_stats::v3::LoadStatsRequest;
  using LoadStatsResponse = ::envoy::service::load_stats::v3::LoadStatsResponse;

  // Stats reported by client.
  class ClientStats {
   public:
    // Stats for a given locality.
    struct LocalityStats {
      struct LoadMetric {
        uint64_t num_requests_finished_with_metric = 0;
        double total_metric_value = 0;

        LoadMetric() = default;

        // Works for both EndpointLoadMetricStats and
        // UnnamedEndpointLoadMetricStats.
        template <typename T>
        explicit LoadMetric(const T& stats)
            : num_requests_finished_with_metric(
                  stats.num_requests_finished_with_metric()),
              total_metric_value(stats.total_metric_value()) {}

        LoadMetric& operator+=(const LoadMetric& other) {
          num_requests_finished_with_metric +=
              other.num_requests_finished_with_metric;
          total_metric_value += other.total_metric_value;
          return *this;
        }
      };

      LocalityStats() {}

      // Converts from proto message class.
      explicit LocalityStats(
          const ::envoy::config::endpoint::v3::UpstreamLocalityStats&
              upstream_locality_stats)
          : total_successful_requests(
                upstream_locality_stats.total_successful_requests()),
            total_requests_in_progress(
                upstream_locality_stats.total_requests_in_progress()),
            total_error_requests(
                upstream_locality_stats.total_error_requests()),
            total_issued_requests(
                upstream_locality_stats.total_issued_requests()),
            cpu_utilization(upstream_locality_stats.cpu_utilization()),
            mem_utilization(upstream_locality_stats.mem_utilization()),
            application_utilization(
                upstream_locality_stats.application_utilization()) {
        for (const auto& s : upstream_locality_stats.load_metric_stats()) {
          load_metrics[s.metric_name()] += LoadMetric(s);
        }
      }

      LocalityStats& operator+=(const LocalityStats& other) {
        total_successful_requests += other.total_successful_requests;
        total_requests_in_progress += other.total_requests_in_progress;
        total_error_requests += other.total_error_requests;
        total_issued_requests += other.total_issued_requests;
        cpu_utilization += other.cpu_utilization;
        mem_utilization += other.mem_utilization;
        application_utilization += other.application_utilization;
        for (const auto& [key, value] : other.load_metrics) {
          load_metrics[key] += value;
        }
        return *this;
      }

      uint64_t total_successful_requests = 0;
      uint64_t total_requests_in_progress = 0;
      uint64_t total_error_requests = 0;
      uint64_t total_issued_requests = 0;
      LoadMetric cpu_utilization;
      LoadMetric mem_utilization;
      LoadMetric application_utilization;
      std::map<std::string, LoadMetric> load_metrics;
    };

    ClientStats() {}

    // Converts from proto message class.
    explicit ClientStats(
        const ::envoy::config::endpoint::v3::ClusterStats& cluster_stats)
        : cluster_name_(cluster_stats.cluster_name()),
          eds_service_name_(cluster_stats.cluster_service_name()),
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
    const std::string& eds_service_name() const { return eds_service_name_; }

    const std::map<std::string, LocalityStats>& locality_stats() const {
      return locality_stats_;
    }

    uint64_t total_successful_requests() const;
    uint64_t total_requests_in_progress() const;
    uint64_t total_error_requests() const;
    uint64_t total_issued_requests() const;

    uint64_t total_dropped_requests() const { return total_dropped_requests_; }

    uint64_t dropped_requests(const std::string& category) const;

    ClientStats& operator+=(const ClientStats& other);

   private:
    std::string cluster_name_;
    std::string eds_service_name_;
    std::map<std::string, LocalityStats> locality_stats_;
    uint64_t total_dropped_requests_ = 0;
    std::map<std::string, uint64_t> dropped_requests_;
  };

  LrsServiceImpl(int client_load_reporting_interval_seconds,
                 std::set<std::string> cluster_names,
                 std::function<void()> stream_started_callback = nullptr,
                 std::function<void(const LoadStatsRequest& request)>
                     check_first_request = nullptr,
                 absl::string_view debug_label = "")
      : client_load_reporting_interval_seconds_(
            client_load_reporting_interval_seconds),
        cluster_names_(std::move(cluster_names)),
        stream_started_callback_(std::move(stream_started_callback)),
        check_first_request_(std::move(check_first_request)),
        debug_label_(absl::StrFormat(
            "%p%s%s", this, debug_label.empty() ? "" : ":", debug_label)) {}

  // Must be called before the LRS call is started.
  void set_send_all_clusters(bool send_all_clusters) {
    send_all_clusters_ = send_all_clusters;
  }
  void set_cluster_names(const std::set<std::string>& cluster_names) {
    cluster_names_ = cluster_names;
  }

  void Start() ABSL_LOCKS_EXCLUDED(lrs_mu_, load_report_mu_);

  void Shutdown();

  // Returns an empty vector if the timeout elapses with no load report.
  // TODO(roth): Change the default here to a finite duration and verify
  // that it doesn't cause failures in any existing tests.
  std::vector<ClientStats> WaitForLoadReport(
      absl::Duration timeout = absl::InfiniteDuration());

 private:
  using Stream = ServerReaderWriter<LoadStatsResponse, LoadStatsRequest>;

  Status StreamLoadStats(ServerContext* /*context*/, Stream* stream) override;

  const int client_load_reporting_interval_seconds_;
  bool send_all_clusters_ = false;
  std::set<std::string> cluster_names_;
  std::function<void()> stream_started_callback_;
  std::function<void(const LoadStatsRequest& request)> check_first_request_;
  std::string debug_label_;

  grpc_core::CondVar lrs_cv_;
  grpc_core::Mutex lrs_mu_;
  bool lrs_done_ ABSL_GUARDED_BY(lrs_mu_) = false;

  grpc_core::Mutex load_report_mu_;
  grpc_core::CondVar* load_report_cond_ ABSL_GUARDED_BY(load_report_mu_) =
      nullptr;
  std::deque<std::vector<ClientStats>> result_queue_
      ABSL_GUARDED_BY(load_report_mu_);
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_XDS_XDS_SERVER_H
