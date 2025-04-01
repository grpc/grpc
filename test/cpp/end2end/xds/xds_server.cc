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

#include "test/cpp/end2end/xds/xds_server.h"

#include <deque>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/util/crash.h"
#include "src/core/util/sync.h"
#include "src/proto/grpc/testing/xds/v3/ads.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/discovery.pb.h"
#include "src/proto/grpc/testing/xds/v3/lrs.grpc.pb.h"

namespace grpc {
namespace testing {

//
// AdsServiceImpl
//

void AdsServiceImpl::SetResource(google::protobuf::Any resource,
                                 const std::string& type_url,
                                 const std::string& name) {
  grpc_core::MutexLock lock(&ads_mu_);
  ResourceTypeState& resource_type_state = resource_map_[type_url];
  ++resource_type_state.resource_type_version;
  ResourceState& resource_state = resource_type_state.resource_name_map[name];
  resource_state.resource_type_version =
      resource_type_state.resource_type_version;
  resource_state.resource = std::move(resource);
  LOG(INFO) << "ADS[" << debug_label_ << "]: Updating " << type_url
            << " resource " << name << "; resource_type_version now "
            << resource_type_state.resource_type_version;
  for (SubscriptionState* subscription : resource_state.subscriptions) {
    subscription->update_queue->emplace_back(type_url, name);
  }
}

void AdsServiceImpl::UnsetResource(const std::string& type_url,
                                   const std::string& name) {
  grpc_core::MutexLock lock(&ads_mu_);
  ResourceTypeState& resource_type_state = resource_map_[type_url];
  ++resource_type_state.resource_type_version;
  ResourceState& resource_state = resource_type_state.resource_name_map[name];
  resource_state.resource_type_version =
      resource_type_state.resource_type_version;
  resource_state.resource.reset();
  LOG(INFO) << "ADS[" << debug_label_ << "]: Unsetting " << type_url
            << " resource " << name << "; resource_type_version now "
            << resource_type_state.resource_type_version;
  for (SubscriptionState* subscription : resource_state.subscriptions) {
    subscription->update_queue->emplace_back(type_url, name);
  }
}

AdsServiceImpl::Reactor::Reactor(
    std::shared_ptr<AdsServiceImpl> ads_service_impl,
    CallbackServerContext* context)
    : ads_service_impl_(std::move(ads_service_impl)), context_(context) {
  LOG(INFO) << "ADS[" << debug_label_ << "]: StreamAggregatedResources starts";
  {
    grpc_core::MutexLock lock(&ads_service_impl_->ads_mu_);
    if (ads_service_impl_->forced_ads_failure_.has_value()) {
      LOG(INFO) << "ADS[" << debug_label_
                << "]: StreamAggregatedResources forcing early failure "
                   "with status code: "
                << ads_service_impl_->forced_ads_failure_.value().error_code()
                << ", message: "
                << ads_service_impl_->forced_ads_failure_.value().error_message();
      return *ads_service_impl_->forced_ads_failure_;
    }
  }
  ads_service_impl_->AddClient(context->peer());
  StartRead(&request_);
}

void AdsServiceImpl::Reactor::OnDone() {
  LOG(INFO) << "ADS[" << debug_label_ << "]: OnDone()";
  // Clean up any subscriptions that were still active when the call
  // finished.
  {
    grpc_core::MutexLock lock(&ads_service_impl_->ads_mu_);
    for (auto& [type_url, subscription_name_map] : subscription_map_) {
      for (auto& [resource_name, subscription_state] : subscription_name_map) {
        ResourceNameMap& resource_name_map =
            ads_service_impl_->resource_map_[type_url].resource_name_map;
        ResourceState& resource_state = resource_name_map[resource_name];
        resource_state.subscriptions.erase(&subscription_state);
      }
    }
  }
  ads_service_impl_->RemoveClient(context_->peer());
}

void AdsServiceImpl::Reactor::OnReadDone(bool ok) {
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: OnReadDone("
            << ok << ")";
  if (!seen_first_request_) {
    if (ads_service_impl_->check_first_request_ != nullptr) {
      ads_service_impl_->check_first_request_(request_);
    }
    seen_first_request_ = true;
  }
  grpc_core::MutexLock lock(&ads_service_impl_->ads_mu_);
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_
            << "]: Received request for type "
            << request_.type_url() << " with content "
            << request_.DebugString();
  SentState& sent_state = sent_state_map[request_.type_url()];
  // Check the nonce sent by the client, if any.
  // (This will be absent on the first request on a stream.)
  if (request_.response_nonce().empty()) {
    int client_resource_type_version = 0;
    if (!request_.version_info().empty()) {
      CHECK(absl::SimpleAtoi(request_.version_info(),
                             &client_resource_type_version));
    }
    if (ads_service_impl_->check_version_callback_ != nullptr) {
      ads_service_impl_->check_version_callback_(
          request_.type_url(), client_resource_type_version);
    }
  } else {
    int client_nonce;
    CHECK(absl::SimpleAtoi(request_.response_nonce(), &client_nonce));
    // Check for ACK or NACK.
    ResponseState response_state;
    if (!request_.has_error_detail()) {
      response_state.state = ResponseState::ACKED;
      LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_
                << "]: client ACKed resource_type=" << request_.type_url()
                << " version=" << request_.version_info();
    } else {
      response_state.state = ResponseState::NACKED;
      if (ads_service_impl_->check_nack_status_code_ != nullptr) {
        ads_service_impl_->check_nack_status_code_(
            static_cast<absl::StatusCode>(request_.error_detail().code()));
      }
      response_state.error_message = request_.error_detail().message();
      LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_
                << "]: client NACKed resource_type=" << request_.type_url()
                << " version=" << request_.version_info() << ": "
                << response_state.error_message;
    }
    ads_service_impl_->resource_type_response_state_[request_.type_url()]
        .emplace_back(
            std::move(response_state));
    // Ignore requests with stale nonces.
    if (client_nonce < sent_state->nonce) return;
  }
  // Ignore resource types as requested by tests.
  if (ads_service_impl_->resource_types_to_ignore_.find(request_.type_url()) !=
      ads_service_impl_->resource_types_to_ignore_.end()) {
    return;
  }
  // Look at all the resource names in the request.
  auto& subscription_name_map = subscription_map_[request_.type_url()];
  auto& resource_type_state =
      ads_service_impl_->resource_map_[request_.type_url()];
  auto& resource_name_map = resource_type_state.resource_name_map;
  std::set<std::string> resources_in_current_request;
  std::set<std::string> resources_added_to_response;
  for (const std::string& resource_name : request_.resource_names()) {
    resources_in_current_request.emplace(resource_name);
    auto& subscription_state = subscription_name_map[resource_name];
    auto& resource_state = resource_name_map[resource_name];
    // Subscribe if needed.
    // Send the resource in the response if either (a) this is
    // a new subscription or (b) there is an updated version of
    // this resource to send.
// FIXME
    if (MaybeSubscribe(request_.type_url(), resource_name, &subscription_state,
                       &resource_state, update_queue) ||
        ClientNeedsResourceUpdate(resource_type_state, resource_state,
                                  sent_state->resource_type_version)) {
      LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_
                << "]: Sending update for type=" << request_.type_url()
                << " name=" << resource_name;
      resources_added_to_response.emplace(resource_name);
      if (!response->has_value()) response->emplace();
      if (resource_state.resource.has_value()) {
        auto* resource = (*response)->add_resources();
        resource->CopyFrom(*resource_state.resource);
        if (ads_service_impl_->wrap_resources_) {
          envoy::service::discovery::v3::Resource resource_wrapper;
          *resource_wrapper.mutable_resource() = std::move(*resource);
          resource->PackFrom(resource_wrapper);
        }
      }
    } else {
      LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_
                << "]: client does not need update for type="
                << request_.type_url() << " name=" << resource_name;
    }
  }
  // Process unsubscriptions for any resource no longer
  // present in the request's resource list.
  ProcessUnsubscriptions(request_.type_url(), resources_in_current_request,
                         &subscription_name_map, &resource_name_map);
  // Construct response if needed.
  if (!resources_added_to_response.empty()) {
    CompleteBuildingDiscoveryResponse(
        request_.type_url(), resource_type_state.resource_type_version,
        subscription_name_map, resources_added_to_response, sent_state,
        &response->value());
  }
}


// FIXME: redo the rest of this old main loop
#if 0

  // Main loop to process requests and updates.
  while (true) {
    // Boolean to keep track if the loop received any work to do: a
    // request or an update; regardless whether a response was actually
    // sent out.
    bool did_work = false;
    // Look for new requests and decide what to handle.
    std::optional<DiscoveryResponse> response;
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
// ALREADY MOVED
      }

    }
    if (response.has_value()) {
      LOG(INFO) << "ADS[" << debug_label_
                << "]: Sending response: " << response->DebugString();
      stream->Write(response.value());
    }
    response.reset();
    // Look for updates and decide what to handle.
    {
      grpc_core::MutexLock lock(&ads_mu_);
      if (!update_queue.empty()) {
        auto [resource_type, resource_name] = std::move(update_queue.front());
        update_queue.pop_front();
        did_work = true;
        SentState& sent_state = sent_state_map[resource_type];
        ProcessUpdate(resource_type, resource_name, &subscription_map,
                      &sent_state, &response);
      }
    }
    if (response.has_value()) {
      LOG(INFO) << "ADS[" << debug_label_
                << "]: Sending update response: " << response->DebugString();
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

  return Status::OK;
}
#endif

void AdsServiceImpl::ProcessUpdate(const std::string& resource_type,
                                   const std::string& resource_name,
                                   SubscriptionMap* subscription_map,
                                   SentState* sent_state,
                                   std::optional<DiscoveryResponse>* response) {
  LOG(INFO) << "ADS[" << debug_label_
            << "]: Received update for type=" << resource_type
            << " name=" << resource_name;
  auto& subscription_name_map = (*subscription_map)[resource_type];
  auto& resource_type_state = resource_map_[resource_type];
  auto& resource_name_map = resource_type_state.resource_name_map;
  auto it = subscription_name_map.find(resource_name);
  if (it != subscription_name_map.end()) {
    ResourceState& resource_state = resource_name_map[resource_name];
    if (ClientNeedsResourceUpdate(resource_type_state, resource_state,
                                  sent_state->resource_type_version)) {
      LOG(INFO) << "ADS[" << debug_label_
                << "]: Sending update for type=" << resource_type
                << " name=" << resource_name;
      response->emplace();
      if (resource_state.resource.has_value()) {
        auto* resource = (*response)->add_resources();
        resource->CopyFrom(*resource_state.resource);
      }
      CompleteBuildingDiscoveryResponse(
          resource_type, resource_type_state.resource_type_version,
          subscription_name_map, {resource_name}, sent_state,
          &response->value());
    }
  }
}

void AdsServiceImpl::CompleteBuildingDiscoveryResponse(
    const std::string& resource_type, const int version,
    const SubscriptionNameMap& subscription_name_map,
    const std::set<std::string>& resources_added_to_response,
    SentState* sent_state, DiscoveryResponse* response) {
  response->set_type_url(resource_type);
  response->set_version_info(std::to_string(version));
  response->set_nonce(std::to_string(++sent_state->nonce));
  if (resource_type == kLdsTypeUrl || resource_type == kCdsTypeUrl) {
    // For LDS and CDS we must send back all subscribed resources
    // (even the unchanged ones)
    for (const auto& [resource_name, _] : subscription_name_map) {
      if (resources_added_to_response.find(resource_name) ==
          resources_added_to_response.end()) {
        ResourceNameMap& resource_name_map =
            resource_map_[resource_type].resource_name_map;
        const ResourceState& resource_state = resource_name_map[resource_name];
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
bool AdsServiceImpl::ClientNeedsResourceUpdate(
    const ResourceTypeState& resource_type_state,
    const ResourceState& resource_state, int client_resource_type_version) {
  return client_resource_type_version <
             resource_type_state.resource_type_version &&
         resource_state.resource_type_version <=
             resource_type_state.resource_type_version;
}

// Subscribes to a resource if not already subscribed:
// 1. Sets the update_queue field in subscription_state.
// 2. Adds subscription_state to resource_state->subscriptions.
bool AdsServiceImpl::MaybeSubscribe(const std::string& resource_type,
                                    const std::string& resource_name,
                                    SubscriptionState* subscription_state,
                                    ResourceState* resource_state,
                                    UpdateQueue* update_queue) {
  // The update_queue will be null if we were not previously subscribed.
  if (subscription_state->update_queue != nullptr) return false;
  subscription_state->update_queue = update_queue;
  resource_state->subscriptions.emplace(subscription_state);
  LOG(INFO) << "ADS[" << debug_label_ << "]: subscribe to resource type "
            << resource_type << " name " << resource_name << " state "
            << &subscription_state;
  return true;
}

// Removes subscriptions for resources no longer present in the
// current request.
void AdsServiceImpl::ProcessUnsubscriptions(
    const std::string& resource_type,
    const std::set<std::string>& resources_in_current_request,
    SubscriptionNameMap* subscription_name_map,
    ResourceNameMap* resource_name_map) {
  for (auto it = subscription_name_map->begin();
       it != subscription_name_map->end();) {
    auto& [resource_name, subscription_state] = *it;
    if (resources_in_current_request.find(resource_name) !=
        resources_in_current_request.end()) {
      ++it;
      continue;
    }
    LOG(INFO) << "ADS[" << debug_label_
              << "]: Unsubscribe to type=" << resource_type
              << " name=" << resource_name << " state=" << &subscription_state;
    auto resource_it = resource_name_map->find(resource_name);
    CHECK(resource_it != resource_name_map->end());
    auto& resource_state = resource_it->second;
    resource_state.subscriptions.erase(&subscription_state);
    if (resource_state.subscriptions.empty() &&
        !resource_state.resource.has_value()) {
      resource_name_map->erase(resource_it);
    }
    it = subscription_name_map->erase(it);
  }
}

void AdsServiceImpl::Start() {
  grpc_core::MutexLock lock(&ads_mu_);
  ads_done_ = false;
}

void AdsServiceImpl::Shutdown() {
  {
    grpc_core::MutexLock lock(&ads_mu_);
    if (!ads_done_) {
      ads_done_ = true;
      ads_cond_.SignalAll();
    }
    resource_type_response_state_.clear();
  }
  LOG(INFO) << "ADS[" << debug_label_ << "]: shut down";
}

//
// LrsServiceImpl::ClientStats
//

uint64_t LrsServiceImpl::ClientStats::total_successful_requests() const {
  uint64_t sum = 0;
  for (auto& [_, stats] : locality_stats_) {
    sum += stats.total_successful_requests;
  }
  return sum;
}

uint64_t LrsServiceImpl::ClientStats::total_requests_in_progress() const {
  uint64_t sum = 0;
  for (auto& [_, stats] : locality_stats_) {
    sum += stats.total_requests_in_progress;
  }
  return sum;
}

uint64_t LrsServiceImpl::ClientStats::total_error_requests() const {
  uint64_t sum = 0;
  for (auto& [_, stats] : locality_stats_) {
    sum += stats.total_error_requests;
  }
  return sum;
}

uint64_t LrsServiceImpl::ClientStats::total_issued_requests() const {
  uint64_t sum = 0;
  for (auto& [_, stats] : locality_stats_) {
    sum += stats.total_issued_requests;
  }
  return sum;
}

uint64_t LrsServiceImpl::ClientStats::dropped_requests(
    const std::string& category) const {
  auto iter = dropped_requests_.find(category);
  CHECK(iter != dropped_requests_.end());
  return iter->second;
}

LrsServiceImpl::ClientStats& LrsServiceImpl::ClientStats::operator+=(
    const ClientStats& other) {
  for (const auto& [name, stats] : other.locality_stats_) {
    locality_stats_[name] += stats;
  }
  total_dropped_requests_ += other.total_dropped_requests_;
  for (const auto& [category, count] : other.dropped_requests_) {
    dropped_requests_[category] += count;
  }
  return *this;
}

//
// LrsServiceImpl
//

void LrsServiceImpl::Start() {
  {
    grpc_core::MutexLock lock(&lrs_mu_);
    lrs_done_ = false;
  }
  {
    grpc_core::MutexLock lock(&load_report_mu_);
    result_queue_.clear();
  }
}

void LrsServiceImpl::Shutdown() {
  {
    grpc_core::MutexLock lock(&lrs_mu_);
    if (!lrs_done_) {
      lrs_done_ = true;
      lrs_cv_.SignalAll();
    }
  }
  LOG(INFO) << "LRS[" << debug_label_ << "]: shut down";
}

std::vector<LrsServiceImpl::ClientStats> LrsServiceImpl::WaitForLoadReport(
    absl::Duration timeout) {
  timeout *= grpc_test_slowdown_factor();
  grpc_core::MutexLock lock(&load_report_mu_);
  grpc_core::CondVar cv;
  if (result_queue_.empty()) {
    load_report_cond_ = &cv;
    while (result_queue_.empty()) {
      if (cv.WaitWithTimeout(&load_report_mu_, timeout)) {
        LOG(ERROR) << "timed out waiting for load report";
        return {};
      }
    }
    load_report_cond_ = nullptr;
  }
  std::vector<ClientStats> result = std::move(result_queue_.front());
  result_queue_.pop_front();
  return result;
}

Status LrsServiceImpl::StreamLoadStats(ServerContext* /*context*/,
                                       Stream* stream) {
  LOG(INFO) << "LRS[" << debug_label_ << "]: StreamLoadStats starts";
  if (stream_started_callback_ != nullptr) stream_started_callback_();
  // Take a reference of the LrsServiceImpl object, reference will go
  // out of scope after this method exits.
  std::shared_ptr<LrsServiceImpl> lrs_service_impl = shared_from_this();
  // Read initial request.
  LoadStatsRequest request;
  if (stream->Read(&request)) {
    IncreaseRequestCount();
    if (check_first_request_ != nullptr) check_first_request_(request);
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
        client_load_reporting_interval_seconds_ * grpc_test_slowdown_factor());
    stream->Write(response);
    IncreaseResponseCount();
    // Wait for report.
    request.Clear();
    while (stream->Read(&request)) {
      LOG(INFO) << "LRS[" << debug_label_
                << "]: received client load report message: "
                << request.DebugString();
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
  LOG(INFO) << "LRS[" << debug_label_ << "]: StreamLoadStats done";
  return Status::OK;
}

}  // namespace testing
}  // namespace grpc
