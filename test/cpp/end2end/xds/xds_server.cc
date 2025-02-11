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

}  // namespace testing
}  // namespace grpc
