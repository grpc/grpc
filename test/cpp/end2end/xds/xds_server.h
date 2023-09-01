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

#include <deque>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/proto/grpc/testing/xds/v3/ads.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/discovery.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/discovery.pb.h"
#include "src/proto/grpc/testing/xds/v3/endpoint.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/listener.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/lrs.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/route.grpc.pb.h"
#include "test/core/util/test_config.h"
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
          ::envoy::service::discovery::v3::AggregatedDiscoveryService::Service>,
      public std::enable_shared_from_this<AdsServiceImpl> {
 public:
  // State for a given xDS resource type.
  struct ResponseState {
    enum State {
      ACKED,   // ACK received.
      NACKED,  // NACK received; error_message will contain the error.
    };
    State state = ACKED;
    std::string error_message;
  };

  AdsServiceImpl() {}

  void set_wrap_resources(bool wrap_resources) {
    grpc_core::MutexLock lock(&ads_mu_);
    wrap_resources_ = wrap_resources;
  }

  void set_inject_bad_resources_for_resource_type(const std::string& type_url) {
    grpc_core::MutexLock lock(&ads_mu_);
    inject_bad_resources_for_resource_type_ = type_url;
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

  // Sets the minimum version that the server will accept for a given
  // resource type.  Will cause a gmock expectation failure if we see a
  // lower version.
  void SetResourceMinVersion(const std::string& type_url, int version) {
    grpc_core::MutexLock lock(&ads_mu_);
    resource_type_min_versions_[type_url] = version;
  }

  // Get the list of response state for each resource type.
  // TODO(roth): Consider adding an absl::Notification-based mechanism
  // here to avoid the need for tests to poll the response state.
  absl::optional<ResponseState> GetResponseState(const std::string& type_url) {
    grpc_core::MutexLock lock(&ads_mu_);
    if (resource_type_response_state_[type_url].empty()) {
      return absl::nullopt;
    }
    auto response = resource_type_response_state_[type_url].front();
    resource_type_response_state_[type_url].pop_front();
    return response;
  }
  absl::optional<ResponseState> lds_response_state() {
    return GetResponseState(kLdsTypeUrl);
  }
  absl::optional<ResponseState> rds_response_state() {
    return GetResponseState(kRdsTypeUrl);
  }
  absl::optional<ResponseState> cds_response_state() {
    return GetResponseState(kCdsTypeUrl);
  }
  absl::optional<ResponseState> eds_response_state() {
    return GetResponseState(kEdsTypeUrl);
  }

  // Starts the service.
  void Start();

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

 private:
  // A queue of resource type/name pairs that have changed since the client
  // subscribed to them.
  using UpdateQueue = std::deque<
      std::pair<std::string /* type url */, std::string /* resource name */>>;

  // A struct representing a client's subscription to a particular resource.
  struct SubscriptionState {
    // The queue upon which to place updates when the resource is updated.
    UpdateQueue* update_queue;
  };

  // A struct representing the a client's subscription to all the resources.
  using SubscriptionNameMap =
      std::map<std::string /* resource_name */, SubscriptionState>;
  using SubscriptionMap =
      std::map<std::string /* type_url */, SubscriptionNameMap>;

  // Sent state for a given resource type.
  struct SentState {
    int nonce = 0;
    int resource_type_version = 0;
  };

  // A struct representing the current state for an individual resource.
  struct ResourceState {
    // The resource itself, if present.
    absl::optional<google::protobuf::Any> resource;
    // The resource type version that this resource was last updated in.
    int resource_type_version = 0;
    // A list of subscriptions to this resource.
    std::set<SubscriptionState*> subscriptions;
  };

  // The current state for all individual resources of a given type.
  using ResourceNameMap =
      std::map<std::string /* resource_name */, ResourceState>;

  struct ResourceTypeState {
    int resource_type_version = 0;
    ResourceNameMap resource_name_map;
  };

  using ResourceMap = std::map<std::string /* type_url */, ResourceTypeState>;

  using DiscoveryRequest = ::envoy::service::discovery::v3::DiscoveryRequest;
  using DiscoveryResponse = ::envoy::service::discovery::v3::DiscoveryResponse;
  using Stream = ServerReaderWriter<DiscoveryResponse, DiscoveryRequest>;

  Status StreamAggregatedResources(ServerContext* context,
                                   Stream* stream) override {
    gpr_log(GPR_INFO, "ADS[%p]: StreamAggregatedResources starts", this);
    {
      grpc_core::MutexLock lock(&ads_mu_);
      if (forced_ads_failure_.has_value()) {
        gpr_log(GPR_INFO,
                "ADS[%p]: StreamAggregatedResources forcing early failure "
                "with status code: %d, message: %s",
                this, forced_ads_failure_.value().error_code(),
                forced_ads_failure_.value().error_message().c_str());
        return forced_ads_failure_.value();
      }
    }
    AddClient(context->peer());
    // Take a reference of the AdsServiceImpl object, which will go
    // out of scope when this request handler returns.  This ensures
    // that the parent won't be destroyed until this stream is complete.
    std::shared_ptr<AdsServiceImpl> ads_service_impl = shared_from_this();
    // Resources (type/name pairs) that have changed since the client
    // subscribed to them.
    UpdateQueue update_queue;
    // Resources that the client will be subscribed to keyed by resource type
    // url.
    SubscriptionMap subscription_map;
    // Sent state for each resource type.
    std::map<std::string /*type_url*/, SentState> sent_state_map;
    // Spawn a thread to read requests from the stream.
    // Requests will be delivered to this thread in a queue.
    std::deque<DiscoveryRequest> requests;
    bool stream_closed = false;
    std::thread reader(std::bind(&AdsServiceImpl::BlockingRead, this, stream,
                                 &requests, &stream_closed));
    // Main loop to process requests and updates.
    while (true) {
      // Boolean to keep track if the loop received any work to do: a
      // request or an update; regardless whether a response was actually
      // sent out.
      bool did_work = false;
      // Look for new requests and decide what to handle.
      absl::optional<DiscoveryResponse> response;
      {
        grpc_core::MutexLock lock(&ads_mu_);
        // If the stream has been closed or our parent is being shut
        // down, stop immediately.
        if (stream_closed || ads_done_) break;
        // Otherwise, see if there's a request to read from the queue.
        if (!requests.empty()) {
          DiscoveryRequest request = std::move(requests.front());
          requests.pop_front();
          did_work = true;
          gpr_log(GPR_INFO,
                  "ADS[%p]: Received request for type %s with content %s", this,
                  request.type_url().c_str(), request.DebugString().c_str());
          SentState& sent_state = sent_state_map[request.type_url()];
          // Process request.
          ProcessRequest(request, &update_queue, &subscription_map, &sent_state,
                         &response);
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
          SentState& sent_state = sent_state_map[resource_type];
          ProcessUpdate(resource_type, resource_name, &subscription_map,
                        &sent_state, &response);
        }
      }
      if (response.has_value()) {
        gpr_log(GPR_INFO, "ADS[%p]: Sending update response: %s", this,
                response->DebugString().c_str());
        stream->Write(response.value());
      }
      {
        grpc_core::MutexLock lock(&ads_mu_);
        if (ads_done_) {
          break;
        }
      }
      // If we didn't find anything to do, delay before the next loop
      // iteration; otherwise, check whether we should exit and then
      // immediately continue.
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(did_work ? 0 : 10));
    }
    // Done with main loop.  Clean up before returning.
    // Join reader thread.
    reader.join();
    // Clean up any subscriptions that were still active when the call
    // finished.
    {
      grpc_core::MutexLock lock(&ads_mu_);
      for (auto& p : subscription_map) {
        const std::string& type_url = p.first;
        SubscriptionNameMap& subscription_name_map = p.second;
        for (auto& q : subscription_name_map) {
          const std::string& resource_name = q.first;
          SubscriptionState& subscription_state = q.second;
          ResourceNameMap& resource_name_map =
              resource_map_[type_url].resource_name_map;
          ResourceState& resource_state = resource_name_map[resource_name];
          resource_state.subscriptions.erase(&subscription_state);
        }
      }
    }
    gpr_log(GPR_INFO, "ADS[%p]: StreamAggregatedResources done", this);
    RemoveClient(context->peer());
    return Status::OK;
  }

  // Processes a response read from the client.
  // Populates response if needed.
  void ProcessRequest(const DiscoveryRequest& request,
                      UpdateQueue* update_queue,
                      SubscriptionMap* subscription_map, SentState* sent_state,
                      absl::optional<DiscoveryResponse>* response)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(ads_mu_) {
    // Check the nonce sent by the client, if any.
    // (This will be absent on the first request on a stream.)
    if (request.response_nonce().empty()) {
      int client_resource_type_version = 0;
      if (!request.version_info().empty()) {
        GPR_ASSERT(absl::SimpleAtoi(request.version_info(),
                                    &client_resource_type_version));
      }
      EXPECT_GE(client_resource_type_version,
                resource_type_min_versions_[request.type_url()])
          << "resource_type: " << request.type_url();
    } else {
      int client_nonce;
      GPR_ASSERT(absl::SimpleAtoi(request.response_nonce(), &client_nonce));
      // Check for ACK or NACK.
      ResponseState response_state;
      if (!request.has_error_detail()) {
        response_state.state = ResponseState::ACKED;
        gpr_log(GPR_INFO, "ADS[%p]: client ACKed resource_type=%s version=%s",
                this, request.type_url().c_str(),
                request.version_info().c_str());
      } else {
        response_state.state = ResponseState::NACKED;
        EXPECT_EQ(request.error_detail().code(), GRPC_STATUS_INVALID_ARGUMENT);
        response_state.error_message = request.error_detail().message();
        gpr_log(GPR_INFO,
                "ADS[%p]: client NACKed resource_type=%s version=%s: %s", this,
                request.type_url().c_str(), request.version_info().c_str(),
                response_state.error_message.c_str());
      }
      resource_type_response_state_[request.type_url()].emplace_back(
          std::move(response_state));
      // Ignore requests with stale nonces.
      if (client_nonce < sent_state->nonce) return;
    }
    // Ignore resource types as requested by tests.
    if (resource_types_to_ignore_.find(request.type_url()) !=
        resource_types_to_ignore_.end()) {
      return;
    }
    // Inject bad resources if needed.
    if (inject_bad_resources_for_resource_type_ == request.type_url()) {
      response->emplace();
      // Unparseable Resource wrapper.
      auto* resource = (*response)->add_resources();
      resource->set_type_url(
          "type.googleapis.com/envoy.service.discovery.v3.Resource");
      resource->set_value(std::string("\0", 1));
      // Unparseable resource within Resource wrapper.
      envoy::service::discovery::v3::Resource resource_wrapper;
      resource_wrapper.set_name("foo");
      resource = resource_wrapper.mutable_resource();
      resource->set_type_url(request.type_url());
      resource->set_value(std::string("\0", 1));
      (*response)->add_resources()->PackFrom(resource_wrapper);
    }
    // Look at all the resource names in the request.
    auto& subscription_name_map = (*subscription_map)[request.type_url()];
    auto& resource_type_state = resource_map_[request.type_url()];
    auto& resource_name_map = resource_type_state.resource_name_map;
    std::set<std::string> resources_in_current_request;
    std::set<std::string> resources_added_to_response;
    for (const std::string& resource_name : request.resource_names()) {
      resources_in_current_request.emplace(resource_name);
      auto& subscription_state = subscription_name_map[resource_name];
      auto& resource_state = resource_name_map[resource_name];
      // Subscribe if needed.
      // Send the resource in the response if either (a) this is
      // a new subscription or (b) there is an updated version of
      // this resource to send.
      if (MaybeSubscribe(request.type_url(), resource_name, &subscription_state,
                         &resource_state, update_queue) ||
          ClientNeedsResourceUpdate(resource_type_state, resource_state,
                                    sent_state->resource_type_version)) {
        gpr_log(GPR_INFO, "ADS[%p]: Sending update for type=%s name=%s", this,
                request.type_url().c_str(), resource_name.c_str());
        resources_added_to_response.emplace(resource_name);
        if (!response->has_value()) response->emplace();
        if (resource_state.resource.has_value()) {
          auto* resource = (*response)->add_resources();
          resource->CopyFrom(resource_state.resource.value());
          if (wrap_resources_) {
            envoy::service::discovery::v3::Resource resource_wrapper;
            *resource_wrapper.mutable_resource() = std::move(*resource);
            resource->PackFrom(resource_wrapper);
          }
        }
      } else {
        gpr_log(GPR_INFO,
                "ADS[%p]: client does not need update for type=%s name=%s",
                this, request.type_url().c_str(), resource_name.c_str());
      }
    }
    // Process unsubscriptions for any resource no longer
    // present in the request's resource list.
    ProcessUnsubscriptions(request.type_url(), resources_in_current_request,
                           &subscription_name_map, &resource_name_map);
    // Construct response if needed.
    if (!resources_added_to_response.empty()) {
      CompleteBuildingDiscoveryResponse(
          request.type_url(), resource_type_state.resource_type_version,
          subscription_name_map, resources_added_to_response, sent_state,
          &response->value());
    }
  }

  // Processes a resource update from the test.
  // Populates response if needed.
  void ProcessUpdate(const std::string& resource_type,
                     const std::string& resource_name,
                     SubscriptionMap* subscription_map, SentState* sent_state,
                     absl::optional<DiscoveryResponse>* response)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(ads_mu_) {
    gpr_log(GPR_INFO, "ADS[%p]: Received update for type=%s name=%s", this,
            resource_type.c_str(), resource_name.c_str());
    auto& subscription_name_map = (*subscription_map)[resource_type];
    auto& resource_type_state = resource_map_[resource_type];
    auto& resource_name_map = resource_type_state.resource_name_map;
    auto it = subscription_name_map.find(resource_name);
    if (it != subscription_name_map.end()) {
      ResourceState& resource_state = resource_name_map[resource_name];
      if (ClientNeedsResourceUpdate(resource_type_state, resource_state,
                                    sent_state->resource_type_version)) {
        gpr_log(GPR_INFO, "ADS[%p]: Sending update for type=%s name=%s", this,
                resource_type.c_str(), resource_name.c_str());
        response->emplace();
        if (resource_state.resource.has_value()) {
          auto* resource = (*response)->add_resources();
          resource->CopyFrom(resource_state.resource.value());
        }
        CompleteBuildingDiscoveryResponse(
            resource_type, resource_type_state.resource_type_version,
            subscription_name_map, {resource_name}, sent_state,
            &response->value());
      }
    }
  }

  // Starting a thread to do blocking read on the stream until cancel.
  void BlockingRead(Stream* stream, std::deque<DiscoveryRequest>* requests,
                    bool* stream_closed) {
    DiscoveryRequest request;
    bool seen_first_request = false;
    while (stream->Read(&request)) {
      if (!seen_first_request) {
        EXPECT_TRUE(request.has_node());
        EXPECT_THAT(request.node().client_features(),
                    ::testing::UnorderedElementsAre(
                        "envoy.lb.does_not_support_overprovisioning",
                        "xds.config.resource-in-sotw"));
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

  // Completing the building a DiscoveryResponse by adding common information
  // for all resources and by adding all subscribed resources for LDS and CDS.
  void CompleteBuildingDiscoveryResponse(
      const std::string& resource_type, const int version,
      const SubscriptionNameMap& subscription_name_map,
      const std::set<std::string>& resources_added_to_response,
      SentState* sent_state, DiscoveryResponse* response)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(ads_mu_) {
    response->set_type_url(resource_type);
    response->set_version_info(std::to_string(version));
    response->set_nonce(std::to_string(++sent_state->nonce));
    if (resource_type == kLdsTypeUrl || resource_type == kCdsTypeUrl) {
      // For LDS and CDS we must send back all subscribed resources
      // (even the unchanged ones)
      for (const auto& p : subscription_name_map) {
        const std::string& resource_name = p.first;
        if (resources_added_to_response.find(resource_name) ==
            resources_added_to_response.end()) {
          ResourceNameMap& resource_name_map =
              resource_map_[resource_type].resource_name_map;
          const ResourceState& resource_state =
              resource_name_map[resource_name];
          if (resource_state.resource.has_value()) {
            auto* resource = response->add_resources();
            resource->CopyFrom(resource_state.resource.value());
          }
        }
      }
    }
    sent_state->resource_type_version = version;
  }

  // Checks whether the client needs to receive a newer version of
  // the resource.
  static bool ClientNeedsResourceUpdate(
      const ResourceTypeState& resource_type_state,
      const ResourceState& resource_state, int client_resource_type_version);

  // Subscribes to a resource if not already subscribed:
  // 1. Sets the update_queue field in subscription_state.
  // 2. Adds subscription_state to resource_state->subscriptions.
  bool MaybeSubscribe(const std::string& resource_type,
                      const std::string& resource_name,
                      SubscriptionState* subscription_state,
                      ResourceState* resource_state, UpdateQueue* update_queue);

  // Removes subscriptions for resources no longer present in the
  // current request.
  void ProcessUnsubscriptions(
      const std::string& resource_type,
      const std::set<std::string>& resources_in_current_request,
      SubscriptionNameMap* subscription_name_map,
      ResourceNameMap* resource_name_map);

  void AddClient(const std::string& client) {
    grpc_core::MutexLock lock(&clients_mu_);
    clients_.insert(client);
  }

  void RemoveClient(const std::string& client) {
    grpc_core::MutexLock lock(&clients_mu_);
    clients_.erase(client);
  }

  grpc_core::CondVar ads_cond_;
  grpc_core::Mutex ads_mu_;
  bool ads_done_ ABSL_GUARDED_BY(ads_mu_) = false;
  std::map<std::string /* type_url */, std::deque<ResponseState>>
      resource_type_response_state_ ABSL_GUARDED_BY(ads_mu_);
  std::set<std::string /*resource_type*/> resource_types_to_ignore_
      ABSL_GUARDED_BY(ads_mu_);
  std::map<std::string /*resource_type*/, int> resource_type_min_versions_
      ABSL_GUARDED_BY(ads_mu_);
  // An instance data member containing the current state of all resources.
  // Note that an entry will exist whenever either of the following is true:
  // - The resource exists (i.e., has been created by SetResource() and has not
  //   yet been destroyed by UnsetResource()).
  // - There is at least one subscription for the resource.
  ResourceMap resource_map_ ABSL_GUARDED_BY(ads_mu_);
  absl::optional<Status> forced_ads_failure_ ABSL_GUARDED_BY(ads_mu_);
  bool wrap_resources_ ABSL_GUARDED_BY(ads_mu_) = false;
  std::string inject_bad_resources_for_resource_type_ ABSL_GUARDED_BY(ads_mu_);

  grpc_core::Mutex clients_mu_;
  std::set<std::string> clients_ ABSL_GUARDED_BY(clients_mu_);
};

// An LRS service implementation.
class LrsServiceImpl
    : public CountedService<
          ::envoy::service::load_stats::v3::LoadReportingService::Service>,
      public std::enable_shared_from_this<LrsServiceImpl> {
 public:
  // Stats reported by client.
  class ClientStats {
   public:
    // Stats for a given locality.
    struct LocalityStats {
      struct LoadMetric {
        uint64_t num_requests_finished_with_metric;
        double total_metric_value;
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
                upstream_locality_stats.total_issued_requests()) {
        for (const auto& s : upstream_locality_stats.load_metric_stats()) {
          load_metrics[s.metric_name()] += LoadMetric{
              s.num_requests_finished_with_metric(), s.total_metric_value()};
        }
      }

      LocalityStats& operator+=(const LocalityStats& other) {
        total_successful_requests += other.total_successful_requests;
        total_requests_in_progress += other.total_requests_in_progress;
        total_error_requests += other.total_error_requests;
        total_issued_requests += other.total_issued_requests;
        for (const auto& p : other.load_metrics) {
          load_metrics[p.first] += p.second;
        }
        return *this;
      }

      uint64_t total_successful_requests = 0;
      uint64_t total_requests_in_progress = 0;
      uint64_t total_error_requests = 0;
      uint64_t total_issued_requests = 0;
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
                 std::set<std::string> cluster_names)
      : client_load_reporting_interval_seconds_(
            client_load_reporting_interval_seconds),
        cluster_names_(std::move(cluster_names)) {}

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
  using LoadStatsRequest = ::envoy::service::load_stats::v3::LoadStatsRequest;
  using LoadStatsResponse = ::envoy::service::load_stats::v3::LoadStatsResponse;
  using Stream = ServerReaderWriter<LoadStatsResponse, LoadStatsRequest>;

  Status StreamLoadStats(ServerContext* /*context*/, Stream* stream) override {
    gpr_log(GPR_INFO, "LRS[%p]: StreamLoadStats starts", this);
    EXPECT_GT(client_load_reporting_interval_seconds_, 0);
    // Take a reference of the LrsServiceImpl object, reference will go
    // out of scope after this method exits.
    std::shared_ptr<LrsServiceImpl> lrs_service_impl = shared_from_this();
    // Read initial request.
    LoadStatsRequest request;
    if (stream->Read(&request)) {
      IncreaseRequestCount();
      // Verify client features.
      EXPECT_THAT(request.node().client_features(),
                  ::testing::Contains("envoy.lrs.supports_send_all_clusters"));
      // Send initial response.
      LoadStatsResponse response;
      if (send_all_clusters_) {
        response.set_send_all_clusters(true);
      } else {
        for (const std::string& cluster_name : cluster_names_) {
          response.add_clusters(cluster_name);
        }
      }
      response.mutable_load_reporting_interval()->set_seconds(
          client_load_reporting_interval_seconds_ *
          grpc_test_slowdown_factor());
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
        if (load_report_cond_ != nullptr) {
          load_report_cond_->Signal();
        }
      }
      // Wait until notified done.
      grpc_core::MutexLock lock(&lrs_mu_);
      while (!lrs_done_) {
        lrs_cv_.Wait(&lrs_mu_);
      }
    }
    gpr_log(GPR_INFO, "LRS[%p]: StreamLoadStats done", this);
    return Status::OK;
  }

  const int client_load_reporting_interval_seconds_;
  bool send_all_clusters_ = false;
  std::set<std::string> cluster_names_;

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
