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
#include "src/core/lib/gprpp/sync.h"
#include "src/proto/grpc/testing/xds/ads_for_test.grpc.pb.h"
#include "src/proto/grpc/testing/xds/lrs_for_test.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/ads.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/discovery.grpc.pb.h"
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

constexpr char kLdsV2TypeUrl[] = "type.googleapis.com/envoy.api.v2.Listener";
constexpr char kRdsV2TypeUrl[] =
    "type.googleapis.com/envoy.api.v2.RouteConfiguration";
constexpr char kCdsV2TypeUrl[] = "type.googleapis.com/envoy.api.v2.Cluster";
constexpr char kEdsV2TypeUrl[] =
    "type.googleapis.com/envoy.api.v2.ClusterLoadAssignment";

// An ADS service implementation.
class AdsServiceImpl : public std::enable_shared_from_this<AdsServiceImpl> {
 public:
  // State for a given xDS resource type.
  struct ResponseState {
    enum State {
      NOT_SENT,  // No response sent yet.
      SENT,      // Response was sent, but no ACK/NACK received.
      ACKED,     // ACK received.
      NACKED,    // NACK received; error_message will contain the error.
    };
    State state = NOT_SENT;
    std::string error_message;
  };

  AdsServiceImpl()
      : v2_rpc_service_(this, /*is_v2=*/true),
        v3_rpc_service_(this, /*is_v2=*/false) {}

  bool seen_v2_client() const { return seen_v2_client_; }
  bool seen_v3_client() const { return seen_v3_client_; }

  ::envoy::service::discovery::v2::AggregatedDiscoveryService::Service*
  v2_rpc_service() {
    return &v2_rpc_service_;
  }

  ::envoy::service::discovery::v3::AggregatedDiscoveryService::Service*
  v3_rpc_service() {
    return &v3_rpc_service_;
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

  // Get the latest response state for each resource type.
  ResponseState GetResponseState(const std::string& type_url) {
    grpc_core::MutexLock lock(&ads_mu_);
    return resource_type_response_state_[type_url];
  }
  ResponseState lds_response_state() { return GetResponseState(kLdsTypeUrl); }
  ResponseState rds_response_state() { return GetResponseState(kRdsTypeUrl); }
  ResponseState cds_response_state() { return GetResponseState(kCdsTypeUrl); }
  ResponseState eds_response_state() { return GetResponseState(kEdsTypeUrl); }

  // Starts the service.
  void Start();

  // Shuts down the service.
  void Shutdown();

  // Returns the peer names of clients currently connected to the service.
  std::set<std::string> clients() {
    grpc_core::MutexLock lock(&clients_mu_);
    return clients_;
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

  // Templated RPC service implementation, works for both v2 and v3.
  template <class RpcApi, class DiscoveryRequest, class DiscoveryResponse>
  class RpcService : public RpcApi::Service {
   public:
    using Stream = ServerReaderWriter<DiscoveryResponse, DiscoveryRequest>;

    RpcService(AdsServiceImpl* parent, bool is_v2)
        : parent_(parent), is_v2_(is_v2) {}

    Status StreamAggregatedResources(ServerContext* context,
                                     Stream* stream) override {
      gpr_log(GPR_INFO, "ADS[%p]: StreamAggregatedResources starts", this);
      parent_->AddClient(context->peer());
      if (is_v2_) {
        parent_->seen_v2_client_ = true;
      } else {
        parent_->seen_v3_client_ = true;
      }
      // Take a reference of the AdsServiceImpl object, which will go
      // out of scope when this request handler returns.  This ensures
      // that the parent won't be destroyed until this stream is complete.
      std::shared_ptr<AdsServiceImpl> ads_service_impl =
          parent_->shared_from_this();
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
      std::thread reader(std::bind(&RpcService::BlockingRead, this, stream,
                                   &requests, &stream_closed));
      // Main loop to process requests and updates.
      while (true) {
        // Boolean to keep track if the loop received any work to do: a
        // request or an update; regardless whether a response was actually
        // sent out.
        bool did_work = false;
        // Look for new requests and and decide what to handle.
        absl::optional<DiscoveryResponse> response;
        {
          grpc_core::MutexLock lock(&parent_->ads_mu_);
          // If the stream has been closed or our parent is being shut
          // down, stop immediately.
          if (stream_closed || parent_->ads_done_) break;
          // Otherwise, see if there's a request to read from the queue.
          if (!requests.empty()) {
            DiscoveryRequest request = std::move(requests.front());
            requests.pop_front();
            did_work = true;
            gpr_log(GPR_INFO,
                    "ADS[%p]: Received request for type %s with content %s",
                    this, request.type_url().c_str(),
                    request.DebugString().c_str());
            const std::string v3_resource_type =
                TypeUrlToV3(request.type_url());
            SentState& sent_state = sent_state_map[v3_resource_type];
            // Process request.
            ProcessRequest(request, v3_resource_type, &update_queue,
                           &subscription_map, &sent_state, &response);
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
          grpc_core::MutexLock lock(&parent_->ads_mu_);
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
          grpc_core::MutexLock lock(&parent_->ads_mu_);
          if (parent_->ads_done_) {
            break;
          }
        }
        // If we didn't find anything to do, delay before the next loop
        // iteration; otherwise, check whether we should exit and then
        // immediately continue.
        gpr_sleep_until(
            grpc_timeout_milliseconds_to_deadline(did_work ? 0 : 10));
      }
      // Done with main loop.  Clean up before returning.
      // Join reader thread.
      reader.join();
      // Clean up any subscriptions that were still active when the call
      // finished.
      {
        grpc_core::MutexLock lock(&parent_->ads_mu_);
        for (auto& p : subscription_map) {
          const std::string& type_url = p.first;
          SubscriptionNameMap& subscription_name_map = p.second;
          for (auto& q : subscription_name_map) {
            const std::string& resource_name = q.first;
            SubscriptionState& subscription_state = q.second;
            ResourceNameMap& resource_name_map =
                parent_->resource_map_[type_url].resource_name_map;
            ResourceState& resource_state = resource_name_map[resource_name];
            resource_state.subscriptions.erase(&subscription_state);
          }
        }
      }
      gpr_log(GPR_INFO, "ADS[%p]: StreamAggregatedResources done", this);
      parent_->RemoveClient(context->peer());
      return Status::OK;
    }

   private:
    // NB: clang's annotalysis is confused by the use of inner template
    // classes here and *ignores* the exclusive lock annotation on some
    // functions. See https://bugs.llvm.org/show_bug.cgi?id=51368.
    //
    // This class is used for a dual purpose:
    // - it convinces clang that the lock is held in a given scope
    // - when used in a function that is annotated to require the inner lock it
    //   will cause compilation to fail if the upstream bug is fixed!
    //
    // If you arrive here because of a compilation failure, that might mean the
    // clang bug is fixed! Please report that on the ticket.
    //
    // Since the buggy compiler will still need to be supported, consider
    // wrapping this class in a compiler version #if and replace its usage
    // with a macro whose expansion is conditional on the compiler version. In
    // time (years? decades?) this code can be deleted altogether.
    class ABSL_SCOPED_LOCKABLE NoopMutexLock {
     public:
      explicit NoopMutexLock(grpc_core::Mutex& mu)
          ABSL_EXCLUSIVE_LOCK_FUNCTION(mu) {}
      ~NoopMutexLock() ABSL_UNLOCK_FUNCTION() {}
    };
    // Processes a response read from the client.
    // Populates response if needed.
    void ProcessRequest(const DiscoveryRequest& request,
                        const std::string& v3_resource_type,
                        UpdateQueue* update_queue,
                        SubscriptionMap* subscription_map,
                        SentState* sent_state,
                        absl::optional<DiscoveryResponse>* response)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(parent_->ads_mu_) {
      NoopMutexLock mu(parent_->ads_mu_);
      // Check the nonce sent by the client, if any.
      // (This will be absent on the first request on a stream.)
      if (request.response_nonce().empty()) {
        int client_resource_type_version = 0;
        if (!request.version_info().empty()) {
          GPR_ASSERT(absl::SimpleAtoi(request.version_info(),
                                      &client_resource_type_version));
        }
        EXPECT_GE(client_resource_type_version,
                  parent_->resource_type_min_versions_[v3_resource_type])
            << "resource_type: " << v3_resource_type;
      } else {
        int client_nonce;
        GPR_ASSERT(absl::SimpleAtoi(request.response_nonce(), &client_nonce));
        // Ignore requests with stale nonces.
        if (client_nonce < sent_state->nonce) return;
        // Check for ACK or NACK.
        auto it = parent_->resource_type_response_state_.find(v3_resource_type);
        if (it != parent_->resource_type_response_state_.end()) {
          if (!request.has_error_detail()) {
            it->second.state = ResponseState::ACKED;
            it->second.error_message.clear();
            gpr_log(GPR_INFO,
                    "ADS[%p]: client ACKed resource_type=%s version=%s", this,
                    request.type_url().c_str(), request.version_info().c_str());
          } else {
            it->second.state = ResponseState::NACKED;
            EXPECT_EQ(request.error_detail().code(),
                      GRPC_STATUS_INVALID_ARGUMENT);
            it->second.error_message = request.error_detail().message();
            gpr_log(GPR_INFO,
                    "ADS[%p]: client NACKed resource_type=%s version=%s: %s",
                    this, request.type_url().c_str(),
                    request.version_info().c_str(),
                    it->second.error_message.c_str());
          }
        }
      }
      // Ignore resource types as requested by tests.
      if (parent_->resource_types_to_ignore_.find(v3_resource_type) !=
          parent_->resource_types_to_ignore_.end()) {
        return;
      }
      // Look at all the resource names in the request.
      auto& subscription_name_map = (*subscription_map)[v3_resource_type];
      auto& resource_type_state = parent_->resource_map_[v3_resource_type];
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
        if (parent_->MaybeSubscribe(v3_resource_type, resource_name,
                                    &subscription_state, &resource_state,
                                    update_queue) ||
            ClientNeedsResourceUpdate(resource_type_state, resource_state,
                                      sent_state->resource_type_version)) {
          gpr_log(GPR_INFO, "ADS[%p]: Sending update for type=%s name=%s", this,
                  request.type_url().c_str(), resource_name.c_str());
          resources_added_to_response.emplace(resource_name);
          if (!response->has_value()) response->emplace();
          if (resource_state.resource.has_value()) {
            auto* resource = (*response)->add_resources();
            resource->CopyFrom(resource_state.resource.value());
            if (is_v2_) {
              resource->set_type_url(request.type_url());
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
      parent_->ProcessUnsubscriptions(
          v3_resource_type, resources_in_current_request,
          &subscription_name_map, &resource_name_map);
      // Construct response if needed.
      if (!resources_added_to_response.empty()) {
        CompleteBuildingDiscoveryResponse(
            v3_resource_type, request.type_url(),
            resource_type_state.resource_type_version, subscription_name_map,
            resources_added_to_response, sent_state, &response->value());
      }
    }

    // Processes a resource update from the test.
    // Populates response if needed.
    void ProcessUpdate(const std::string& resource_type,
                       const std::string& resource_name,
                       SubscriptionMap* subscription_map, SentState* sent_state,
                       absl::optional<DiscoveryResponse>* response)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(parent_->ads_mu_) {
      NoopMutexLock mu(parent_->ads_mu_);
      const std::string v2_resource_type = TypeUrlToV2(resource_type);
      gpr_log(GPR_INFO, "ADS[%p]: Received update for type=%s name=%s", this,
              resource_type.c_str(), resource_name.c_str());
      auto& subscription_name_map = (*subscription_map)[resource_type];
      auto& resource_type_state = parent_->resource_map_[resource_type];
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
            if (is_v2_) {
              resource->set_type_url(v2_resource_type);
            }
          }
          CompleteBuildingDiscoveryResponse(
              resource_type, v2_resource_type,
              resource_type_state.resource_type_version, subscription_name_map,
              {resource_name}, sent_state, &response->value());
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
          ASSERT_FALSE(request.node().client_features().empty());
          EXPECT_EQ(request.node().client_features(0),
                    "envoy.lb.does_not_support_overprovisioning");
          CheckBuildVersion(request);
          seen_first_request = true;
        }
        {
          grpc_core::MutexLock lock(&parent_->ads_mu_);
          requests->emplace_back(std::move(request));
        }
      }
      gpr_log(GPR_INFO, "ADS[%p]: Null read, stream closed", this);
      grpc_core::MutexLock lock(&parent_->ads_mu_);
      *stream_closed = true;
    }

    // Completing the building a DiscoveryResponse by adding common information
    // for all resources and by adding all subscribed resources for LDS and CDS.
    void CompleteBuildingDiscoveryResponse(
        const std::string& resource_type, const std::string& v2_resource_type,
        const int version, const SubscriptionNameMap& subscription_name_map,
        const std::set<std::string>& resources_added_to_response,
        SentState* sent_state, DiscoveryResponse* response)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(parent_->ads_mu_) {
      NoopMutexLock mu(parent_->ads_mu_);
      auto& response_state =
          parent_->resource_type_response_state_[resource_type];
      if (response_state.state == ResponseState::NOT_SENT) {
        response_state.state = ResponseState::SENT;
      }
      response->set_type_url(is_v2_ ? v2_resource_type : resource_type);
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
                parent_->resource_map_[resource_type].resource_name_map;
            const ResourceState& resource_state =
                resource_name_map[resource_name];
            if (resource_state.resource.has_value()) {
              auto* resource = response->add_resources();
              resource->CopyFrom(resource_state.resource.value());
              if (is_v2_) {
                resource->set_type_url(v2_resource_type);
              }
            }
          }
        }
      }
      sent_state->resource_type_version = version;
    }

    static std::string TypeUrlToV2(const std::string& resource_type) {
      if (resource_type == kLdsTypeUrl) return kLdsV2TypeUrl;
      if (resource_type == kRdsTypeUrl) return kRdsV2TypeUrl;
      if (resource_type == kCdsTypeUrl) return kCdsV2TypeUrl;
      if (resource_type == kEdsTypeUrl) return kEdsV2TypeUrl;
      return resource_type;
    }

    static std::string TypeUrlToV3(const std::string& resource_type) {
      if (resource_type == kLdsV2TypeUrl) return kLdsTypeUrl;
      if (resource_type == kRdsV2TypeUrl) return kRdsTypeUrl;
      if (resource_type == kCdsV2TypeUrl) return kCdsTypeUrl;
      if (resource_type == kEdsV2TypeUrl) return kEdsTypeUrl;
      return resource_type;
    }

    static void CheckBuildVersion(
        const ::envoy::api::v2::DiscoveryRequest& request) {
      EXPECT_FALSE(request.node().build_version().empty());
    }

    static void CheckBuildVersion(
        const ::envoy::service::discovery::v3::DiscoveryRequest& /*request*/) {}

    AdsServiceImpl* parent_;
    const bool is_v2_;
  };

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

  RpcService<::envoy::service::discovery::v2::AggregatedDiscoveryService,
             ::envoy::api::v2::DiscoveryRequest,
             ::envoy::api::v2::DiscoveryResponse>
      v2_rpc_service_;
  RpcService<::envoy::service::discovery::v3::AggregatedDiscoveryService,
             ::envoy::service::discovery::v3::DiscoveryRequest,
             ::envoy::service::discovery::v3::DiscoveryResponse>
      v3_rpc_service_;

  std::atomic_bool seen_v2_client_{false};
  std::atomic_bool seen_v3_client_{false};

  grpc_core::CondVar ads_cond_;
  grpc_core::Mutex ads_mu_;
  bool ads_done_ ABSL_GUARDED_BY(ads_mu_) = false;
  std::map<std::string /* type_url */, ResponseState>
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

  grpc_core::Mutex clients_mu_;
  std::set<std::string> clients_ ABSL_GUARDED_BY(clients_mu_);
};

// An LRS service implementation.
class LrsServiceImpl : public std::enable_shared_from_this<LrsServiceImpl> {
 public:
  // Stats reported by client.
  class ClientStats {
   public:
    // Stats for a given locality.
    struct LocalityStats {
      LocalityStats() {}

      // Converts from proto message class.
      template <class UpstreamLocalityStats>
      explicit LocalityStats(
          const UpstreamLocalityStats& upstream_locality_stats)
          : total_successful_requests(
                upstream_locality_stats.total_successful_requests()),
            total_requests_in_progress(
                upstream_locality_stats.total_requests_in_progress()),
            total_error_requests(
                upstream_locality_stats.total_error_requests()),
            total_issued_requests(
                upstream_locality_stats.total_issued_requests()) {}

      LocalityStats& operator+=(const LocalityStats& other) {
        total_successful_requests += other.total_successful_requests;
        total_requests_in_progress += other.total_requests_in_progress;
        total_error_requests += other.total_error_requests;
        total_issued_requests += other.total_issued_requests;
        return *this;
      }

      uint64_t total_successful_requests = 0;
      uint64_t total_requests_in_progress = 0;
      uint64_t total_error_requests = 0;
      uint64_t total_issued_requests = 0;
    };

    ClientStats() {}

    // Converts from proto message class.
    template <class ClusterStats>
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
    std::map<std::string, LocalityStats> locality_stats_;
    uint64_t total_dropped_requests_ = 0;
    std::map<std::string, uint64_t> dropped_requests_;
  };

  LrsServiceImpl(int client_load_reporting_interval_seconds,
                 std::set<std::string> cluster_names)
      : v2_rpc_service_(this),
        v3_rpc_service_(this),
        client_load_reporting_interval_seconds_(
            client_load_reporting_interval_seconds),
        cluster_names_(std::move(cluster_names)) {}

  ::envoy::service::load_stats::v2::LoadReportingService::Service*
  v2_rpc_service() {
    return &v2_rpc_service_;
  }

  ::envoy::service::load_stats::v3::LoadReportingService::Service*
  v3_rpc_service() {
    return &v3_rpc_service_;
  }

  size_t request_count() {
    return v2_rpc_service_.request_count() + v3_rpc_service_.request_count();
  }

  size_t response_count() {
    return v2_rpc_service_.response_count() + v3_rpc_service_.response_count();
  }

  // Must be called before the LRS call is started.
  void set_send_all_clusters(bool send_all_clusters) {
    send_all_clusters_ = send_all_clusters;
  }
  void set_cluster_names(const std::set<std::string>& cluster_names) {
    cluster_names_ = cluster_names;
  }

  void Start() ABSL_LOCKS_EXCLUDED(lrs_mu_, load_report_mu_);

  void Shutdown();

  std::vector<ClientStats> WaitForLoadReport();

 private:
  // Templated RPC service implementation, works for both v2 and v3.
  template <class RpcApi, class LoadStatsRequest, class LoadStatsResponse>
  class RpcService : public CountedService<typename RpcApi::Service> {
   public:
    using Stream = ServerReaderWriter<LoadStatsResponse, LoadStatsRequest>;

    explicit RpcService(LrsServiceImpl* parent) : parent_(parent) {}

    Status StreamLoadStats(ServerContext* /*context*/,
                           Stream* stream) override {
      gpr_log(GPR_INFO, "LRS[%p]: StreamLoadStats starts", this);
      EXPECT_GT(parent_->client_load_reporting_interval_seconds_, 0);
      // Take a reference of the LrsServiceImpl object, reference will go
      // out of scope after this method exits.
      std::shared_ptr<LrsServiceImpl> lrs_service_impl =
          parent_->shared_from_this();
      // Read initial request.
      LoadStatsRequest request;
      if (stream->Read(&request)) {
        CountedService<typename RpcApi::Service>::IncreaseRequestCount();
        // Verify client features.
        EXPECT_THAT(
            request.node().client_features(),
            ::testing::Contains("envoy.lrs.supports_send_all_clusters"));
        // Send initial response.
        LoadStatsResponse response;
        if (parent_->send_all_clusters_) {
          response.set_send_all_clusters(true);
        } else {
          for (const std::string& cluster_name : parent_->cluster_names_) {
            response.add_clusters(cluster_name);
          }
        }
        response.mutable_load_reporting_interval()->set_seconds(
            parent_->client_load_reporting_interval_seconds_);
        stream->Write(response);
        CountedService<typename RpcApi::Service>::IncreaseResponseCount();
        // Wait for report.
        request.Clear();
        while (stream->Read(&request)) {
          gpr_log(GPR_INFO, "LRS[%p]: received client load report message: %s",
                  this, request.DebugString().c_str());
          std::vector<ClientStats> stats;
          for (const auto& cluster_stats : request.cluster_stats()) {
            stats.emplace_back(cluster_stats);
          }
          grpc_core::MutexLock lock(&parent_->load_report_mu_);
          parent_->result_queue_.emplace_back(std::move(stats));
          if (parent_->load_report_cond_ != nullptr) {
            parent_->load_report_cond_->Signal();
          }
        }
        // Wait until notified done.
        grpc_core::MutexLock lock(&parent_->lrs_mu_);
        while (!parent_->lrs_done_) {
          parent_->lrs_cv_.Wait(&parent_->lrs_mu_);
        }
      }
      gpr_log(GPR_INFO, "LRS[%p]: StreamLoadStats done", this);
      return Status::OK;
    }

   private:
    LrsServiceImpl* parent_;
  };

  RpcService<::envoy::service::load_stats::v2::LoadReportingService,
             ::envoy::service::load_stats::v2::LoadStatsRequest,
             ::envoy::service::load_stats::v2::LoadStatsResponse>
      v2_rpc_service_;
  RpcService<::envoy::service::load_stats::v3::LoadReportingService,
             ::envoy::service::load_stats::v3::LoadStatsRequest,
             ::envoy::service::load_stats::v3::LoadStatsResponse>
      v3_rpc_service_;

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
