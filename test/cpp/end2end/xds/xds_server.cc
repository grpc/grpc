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
  for (Reactor* reactor : resource_state.subscriptions) {
    reactor->MaybeStartWrite(type_url);
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
  for (Reactor* reactor : resource_state.subscriptions) {
    reactor->MaybeStartWrite(type_url);
  }
}

void AdsServiceImpl::Shutdown() {
  {
    grpc_core::MutexLock lock(&ads_mu_);
    resource_type_response_state_.clear();
  }
  LOG(INFO) << "ADS[" << debug_label_ << "]: shut down";
}

//
// AdsServiceImpl::Reactor
//

AdsServiceImpl::Reactor::Reactor(
    std::shared_ptr<AdsServiceImpl> ads_service_impl,
    CallbackServerContext* context)
    : ads_service_impl_(std::move(ads_service_impl)), context_(context) {
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
            << this << ": StreamAggregatedResources starts";
  {
    grpc_core::MutexLock lock(&ads_service_impl_->ads_mu_);
    if (ads_service_impl_->forced_ads_failure_.has_value()) {
      LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
                << this
                << ": StreamAggregatedResources forcing early failure "
                   "with status code: "
                << ads_service_impl_->forced_ads_failure_->error_code()
                << ", message: "
                << ads_service_impl_->forced_ads_failure_->error_message();
      MaybeFinish(*ads_service_impl_->forced_ads_failure_);
      return;
    }
  }
  ads_service_impl_->AddClient(context->peer());
  StartRead(&request_);
}

void AdsServiceImpl::Reactor::OnDone() {
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
            << this << ": OnDone()";
  delete this;
}

void AdsServiceImpl::Reactor::OnCancel() {
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
            << this << ": OnCancel()";
  // Clean up any subscriptions that were still active when the call
  // finished.
  {
    grpc_core::MutexLock lock(&ads_service_impl_->ads_mu_);
    for (auto& [type_url, type_state] : type_state_map_) {
      auto& resource_name_map =
          ads_service_impl_->resource_map_[type_url].resource_name_map;
      for (auto& [resource_name, _] : type_state.subscriptions) {
        ResourceState& resource_state = resource_name_map[resource_name];
        resource_state.subscriptions.erase(this);
      }
    }
  }
  ads_service_impl_->RemoveClient(context_->peer());
  MaybeFinish(Status::OK);
}

void AdsServiceImpl::Reactor::OnReadDone(bool ok) {
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
            << this << ": OnReadDone(" << ok << ")";
  if (!ok) return;
  grpc_core::MutexLock lock(&ads_service_impl_->ads_mu_);
  if (!seen_first_request_) {
    if (ads_service_impl_->check_first_request_ != nullptr) {
      ads_service_impl_->check_first_request_(request_);
    }
    seen_first_request_ = true;
  }
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
            << this << ": Received request for type " << request_.type_url()
            << " with content " << request_.DebugString();
  auto& type_state = type_state_map_[request_.type_url()];
  // Check the nonce sent by the client, if any.
  // (This will be absent on the first request on a stream.)
  if (request_.response_nonce().empty()) {
    int client_resource_type_version = 0;
    if (!request_.version_info().empty()) {
      CHECK(absl::SimpleAtoi(request_.version_info(),
                             &client_resource_type_version));
    }
    if (ads_service_impl_->check_version_callback_ != nullptr) {
      ads_service_impl_->check_version_callback_(request_.type_url(),
                                                 client_resource_type_version);
    }
  } else {
    int client_nonce;
    CHECK(absl::SimpleAtoi(request_.response_nonce(), &client_nonce));
    // Check for ACK or NACK.
    ResponseState response_state;
    if (!request_.has_error_detail()) {
      response_state.state = ResponseState::ACKED;
      LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
                << this
                << ": client ACKed resource_type=" << request_.type_url()
                << " version=" << request_.version_info();
    } else {
      response_state.state = ResponseState::NACKED;
      if (ads_service_impl_->check_nack_status_code_ != nullptr) {
        ads_service_impl_->check_nack_status_code_(
            static_cast<absl::StatusCode>(request_.error_detail().code()));
      }
      response_state.error_message = request_.error_detail().message();
      LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
                << this
                << ": client NACKed resource_type=" << request_.type_url()
                << " version=" << request_.version_info() << ": "
                << response_state.error_message;
    }
    ads_service_impl_->resource_type_response_state_[request_.type_url()]
        .emplace_back(std::move(response_state));
    // Ignore requests with stale nonces.
    if (client_nonce < type_state.nonce) {
      request_.Clear();
      StartRead(&request_);
      return;
    }
  }
  // Ignore resource types as requested by tests.
  if (ads_service_impl_->resource_types_to_ignore_.find(request_.type_url()) !=
      ads_service_impl_->resource_types_to_ignore_.end()) {
    request_.Clear();
    StartRead(&request_);
    return;
  }
  // Get the map of resources for this type.
  auto& resource_type_state =
      ads_service_impl_->resource_map_[request_.type_url()];
  auto& resource_name_map = resource_type_state.resource_name_map;
  // Subscribe to any new resource names in the request.
  for (const std::string& resource_name : request_.resource_names()) {
    auto& resource_state = resource_name_map[resource_name];
    if (!type_state.subscriptions.contains(resource_name)) {
      type_state.subscriptions.emplace(resource_name, true);
      resource_state.subscriptions.emplace(this);
      LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
                << this << ": subscribe to resource type "
                << request_.type_url() << " name " << resource_name;
    }
  }
  // Unsubscribe from any resource not present in the request.
  const absl::flat_hash_set<absl::string_view> resources_in_request(
      request_.resource_names().begin(), request_.resource_names().end());
  for (auto it = type_state.subscriptions.begin();
       it != type_state.subscriptions.end();) {
    const std::string& resource_name = it->first;
    if (resources_in_request.contains(resource_name)) {
      ++it;
      continue;
    }
    LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
              << this << ": Unsubscribe to type=" << request_.type_url()
              << " name=" << resource_name;
    auto it2 = resource_name_map.find(resource_name);
    CHECK(it2 != resource_name_map.end()) << resource_name;
    auto& resource_state = it2->second;
    resource_state.subscriptions.erase(this);
    if (resource_state.subscriptions.empty() &&
        !resource_state.resource.has_value()) {
      resource_name_map.erase(it2);
    }
    type_state.subscriptions.erase(it++);
  }
  MaybeStartWrite(request_.type_url());
  request_.Clear();
  StartRead(&request_);
}

void AdsServiceImpl::Reactor::MaybeStartWrite(
    const std::string& resource_type) {
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
            << this << ": MaybeStartWrite(" << resource_type << ")";
  if (write_pending_) {
    response_needed_.insert(resource_type);
    return;
  }
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
            << this << ": Constructing response";
  auto& type_state = type_state_map_[resource_type];
  auto& resource_type_state = ads_service_impl_->resource_map_[resource_type];
  auto& resource_name_map = resource_type_state.resource_name_map;
  bool resource_needed_update = false;
  for (auto& [resource_name, new_subscription] : type_state.subscriptions) {
    auto& resource_state = resource_name_map[resource_name];
    bool needs_update =
        new_subscription || (type_state.resource_type_version <
                                 resource_type_state.resource_type_version &&
                             resource_state.resource_type_version <=
                                 resource_type_state.resource_type_version);
    new_subscription = false;
    if (needs_update) {
      LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
                << this << ": Sending update for: " << resource_name;
      resource_needed_update = true;
    }
    if (resource_type == kLdsTypeUrl || resource_type == kCdsTypeUrl ||
        needs_update) {
      if (resource_state.resource.has_value()) {
        auto* resource = response_.add_resources();
        resource->CopyFrom(*resource_state.resource);
        if (ads_service_impl_->wrap_resources_) {
          envoy::service::discovery::v3::Resource resource_wrapper;
          *resource_wrapper.mutable_resource() = std::move(*resource);
          resource->PackFrom(resource_wrapper);
        }
      }
    } else {
      LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
                << this
                << ": client does not need update for: " << resource_name;
    }
  }
  if (!resource_needed_update) {
    LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
              << this << ": no resources to send for type=" << resource_type;
    response_.Clear();
    MaybeStartNextWrite();
    return;
  }
  response_.set_type_url(resource_type);
  response_.set_nonce(std::to_string(++type_state.nonce));
  response_.set_version_info(
      std::to_string(resource_type_state.resource_type_version));
  type_state.resource_type_version = resource_type_state.resource_type_version;
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
            << this << ": sending response: " << response_.DebugString();
  write_pending_ = true;
  StartWrite(&response_);
}

void AdsServiceImpl::Reactor::OnWriteDone(bool ok) {
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
            << this << ": OnWriteDone(" << ok << ")";
  grpc_core::MutexLock lock(&ads_service_impl_->ads_mu_);
  write_pending_ = false;
  response_.Clear();
  if (!ok) return;
  MaybeStartNextWrite();
}

void AdsServiceImpl::Reactor::MaybeStartNextWrite() {
  auto it = response_needed_.begin();
  if (it == response_needed_.end()) return;
  std::string resource_type = *it;
  response_needed_.erase(it);
  MaybeStartWrite(resource_type);
}

void AdsServiceImpl::Reactor::MaybeFinish(const grpc::Status& status) {
  if (called_finish_.exchange(true)) return;
  LOG(INFO) << "ADS[" << ads_service_impl_->debug_label_ << "]: reactor "
            << this << ": calling Finish()";
  Finish(status);
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
  grpc_core::MutexLock lock(&load_report_mu_);
  result_queue_.clear();
}

void LrsServiceImpl::Shutdown() {
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

//
// LrsServiceImpl::Reactor
//

LrsServiceImpl::Reactor::Reactor(
    std::shared_ptr<LrsServiceImpl> lrs_service_impl)
    : lrs_service_impl_(std::move(lrs_service_impl)) {
  LOG(INFO) << "LRS[" << lrs_service_impl_->debug_label_ << "]: reactor "
            << this << ": StreamLoadStats starts";
  if (lrs_service_impl_->stream_started_callback_ != nullptr) {
    lrs_service_impl_->stream_started_callback_();
  }
  StartRead(&request_);
}

void LrsServiceImpl::Reactor::OnReadDone(bool ok) {
  if (!ok) return;
  if (!seen_first_request_.exchange(true)) {
    // Handle initial request.
    LOG(INFO) << "LRS[" << lrs_service_impl_->debug_label_ << "]: reactor "
              << this << ": read initial request: " << request_.DebugString();
    lrs_service_impl_->IncreaseRequestCount();
    if (lrs_service_impl_->check_first_request_ != nullptr) {
      lrs_service_impl_->check_first_request_(request_);
    }
    // Send initial response.
    if (lrs_service_impl_->send_all_clusters_) {
      response_.set_send_all_clusters(true);
    } else {
      for (const std::string& cluster_name :
           lrs_service_impl_->cluster_names_) {
        response_.add_clusters(cluster_name);
      }
    }
    response_.mutable_load_reporting_interval()->set_seconds(
        lrs_service_impl_->client_load_reporting_interval_seconds_ *
        grpc_test_slowdown_factor());
    StartWrite(&response_);
  } else {
    // Handle load reports.
    LOG(INFO) << "LRS[" << lrs_service_impl_->debug_label_ << "]: reactor "
              << this << ": received load report: " << request_.DebugString();
    std::vector<ClientStats> stats;
    for (const auto& cluster_stats : request_.cluster_stats()) {
      stats.emplace_back(cluster_stats);
    }
    grpc_core::MutexLock lock(&lrs_service_impl_->load_report_mu_);
    lrs_service_impl_->result_queue_.emplace_back(std::move(stats));
    if (lrs_service_impl_->load_report_cond_ != nullptr) {
      lrs_service_impl_->load_report_cond_->Signal();
    }
  }
  StartRead(&request_);
}

void LrsServiceImpl::Reactor::OnWriteDone(bool /*ok*/) {
  LOG(INFO) << "LRS[" << lrs_service_impl_->debug_label_ << "]: reactor "
            << this << ": OnWriteDone()";
  lrs_service_impl_->IncreaseResponseCount();
}

void LrsServiceImpl::Reactor::OnDone() {
  LOG(INFO) << "LRS[" << lrs_service_impl_->debug_label_ << "]: reactor "
            << this << ": OnDone()";
  delete this;
}

void LrsServiceImpl::Reactor::OnCancel() {
  LOG(INFO) << "LRS[" << lrs_service_impl_->debug_label_ << "]: reactor "
            << this << ": OnCancel()";
  Finish(Status::OK);
}

}  // namespace testing
}  // namespace grpc
