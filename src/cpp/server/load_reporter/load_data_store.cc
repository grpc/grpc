/*
 *
 * Copyright 2018 gRPC authors.
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

#include <algorithm>
#include <set>
#include <unordered_map>
#include <vector>

#include "src/cpp/server/load_reporter/load_data_store.h"

namespace grpc {
namespace load_reporter {

void PerBalancerStore::MergeRow(const LoadRecordKey& key,
                                const LoadRecordValue& value) {
  // During suspension, the load data received will be dropped.
  if (!suspended_) {
    if (container_.find(key) == container_.end()) {
      container_.insert({key, LoadRecordValue()});
    }
    container_.find(key)->second.MergeFrom(value);
    gpr_log(GPR_DEBUG,
            "[PerBalancerStore %p] Load data merged (Key: %s, Value: %s).",
            this, key.ToString().c_str(), value.ToString().c_str());
  } else {
    gpr_log(GPR_DEBUG,
            "[PerBalancerStore %p] Load data dropped (Key: %s, Value: %s).",
            this, key.ToString().c_str(), value.ToString().c_str());
  }
  // We always keep track of num_calls_in_progress_, so that when this
  // store is resumed, we still have a correct value of
  // num_calls_in_progress_.
  GPR_ASSERT(static_cast<int64_t>(num_calls_in_progress_) +
                 value.GetNumCallsInProgressDelta() >=
             0);
  num_calls_in_progress_ += value.GetNumCallsInProgressDelta();
}

void PerBalancerStore::Suspend() {
  suspended_ = true;
  ClearContainer();
  gpr_log(GPR_DEBUG, "[PerBalancerStore %p] Suspended.", this);
}

void PerBalancerStore::Resume() {
  suspended_ = false;
  gpr_log(GPR_DEBUG, "[PerBalancerStore %p] Resumed.", this);
}

uint64_t PerBalancerStore::GetNumCallsInProgressForReport() {
  GPR_ASSERT(!suspended_);
  last_reported_num_calls_in_progress_ = num_calls_in_progress_;
  return num_calls_in_progress_;
}

PerHostStore::~PerHostStore() {
  for (auto it_per_balancer_store : per_balancer_stores_) {
    delete it_per_balancer_store.second;
  }
}

void PerHostStore::ReportStreamCreated(grpc::string lb_id,
                                       grpc::string load_key) {
  GPR_ASSERT(lb_id != INVALID_LBID);
  InternalAddLb(lb_id, load_key);
  // Prior to this one, there was no load balancer receiving report, so we may
  // have unassigned orphaned stores to assign to this new balancer.
  if (assigned_stores_.size() == 1) {
    for (auto it_per_balancer_store : per_balancer_stores_) {
      if (it_per_balancer_store.first != lb_id) {
        auto orphaned_store = it_per_balancer_store.second;
        orphaned_store->Resume();
        AssignOrphanedStore(orphaned_store, lb_id);
      }
    }
  }
  // The first connected balancer will adopt the INVALID_LBID.
  if (assigned_stores_.size() == 1) {
    InternalAddLb(INVALID_LBID, "");
    ReportStreamClosed(INVALID_LBID);
  }
}

void PerHostStore::ReportStreamClosed(grpc::string lb_id) {
  auto it_store_for_gone_lb = per_balancer_stores_.find(lb_id);
  GPR_ASSERT(it_store_for_gone_lb != per_balancer_stores_.end());
  GPR_ASSERT(UnorderedMultimapEraseKeyValue(
      load_key_to_receiving_lb_ids_, it_store_for_gone_lb->second->load_key(),
      lb_id));
  // The stores that were assigned to this balancer are orphaned now. They
  // should be re-assigned to other balancers which are still receiving reports.
  auto orphaned_stores = UnorderedMultimapRemoveAll(assigned_stores_, lb_id);
  for (auto orphaned_store : orphaned_stores) {
    std::vector<grpc::string> lb_ids_with_same_load_key =
        UnorderedMultimapFindAll(load_key_to_receiving_lb_ids_,
                                 orphaned_store->load_key());
    std::vector<grpc::string> candidates;
    if (!lb_ids_with_same_load_key.empty()) {
      // First, try to pick from the active balancers with the same load key.
      candidates = lb_ids_with_same_load_key;
    } else {
      // If failed, pick from all the active balancers.
      candidates = UnorderedMultimapKeys(assigned_stores_);
    }
    if (candidates.empty()) {
      // Load data for an LB ID that can't be assigned to any stream should
      // be dropped.
      orphaned_store->Suspend();
    } else {
      grpc::string new_receiver = candidates[std::rand() % candidates.size()];
      AssignOrphanedStore(orphaned_store, new_receiver);
    }
  }
}

PerBalancerStore* PerHostStore::FindPerBalancerStore(
    const grpc::string& lb_id) const {
  auto it_balancer = per_balancer_stores_.find(lb_id);
  if (it_balancer != per_balancer_stores_.end()) {
    return it_balancer->second;
  }
  return nullptr;
}

const std::vector<PerBalancerStore*> PerHostStore::GetAssignedStores(
    const grpc::string& lb_id) const {
  return UnorderedMultimapFindAll(assigned_stores_, lb_id);
}

void PerHostStore::AssignOrphanedStore(PerBalancerStore* orphaned_store,
                                       grpc::string new_receiver) {
  assigned_stores_.insert({new_receiver, orphaned_store});
  gpr_log(GPR_INFO,
          "[PerHostStore %p] Re-assigned orphaned store (%p) with original LB"
          " ID of %s to new receiver %s",
          this, orphaned_store, orphaned_store->lb_id().c_str(),
          new_receiver.c_str());
}

void PerHostStore::InternalAddLb(grpc::string lb_id, grpc::string load_key) {
  // The top-level caller (i.e., LoadReportService) should guarantee the
  // lb_id is unique for each reporting stream.
  GPR_ASSERT(per_balancer_stores_.find(lb_id) == per_balancer_stores_.end());
  GPR_ASSERT(assigned_stores_.find(lb_id) == assigned_stores_.end());
  load_key_to_receiving_lb_ids_.insert({load_key, lb_id});
  PerBalancerStore* per_balancer_store = new PerBalancerStore(lb_id, load_key);
  per_balancer_stores_.insert({lb_id, per_balancer_store});
  assigned_stores_.insert({lb_id, per_balancer_store});
}

PerBalancerStore* LoadDataStore::FindPerBalancerStore(
    const string& hostname, const string& lb_id) const {
  auto it_host = per_host_stores_.find(hostname);
  if (it_host != per_host_stores_.end()) {
    return it_host->second.FindPerBalancerStore(lb_id);
  }
  return nullptr;
}

void LoadDataStore::MergeRow(grpc::string hostname, LoadRecordKey key,
                             LoadRecordValue value) {
  auto per_balancer_store = FindPerBalancerStore(hostname, key.lb_id());
  if (per_balancer_store != nullptr) {
    per_balancer_store->MergeRow(key, value);
    return;
  }
  // Unknown LB ID. Track it until its number of in-progress calls drops to
  // zero.
  int64_t in_progress_delta = value.GetNumCallsInProgressDelta();
  if (in_progress_delta != 0) {
    auto it_tracker = unknown_balancer_id_trackers.find(key.lb_id());
    if (it_tracker == unknown_balancer_id_trackers.end()) {
      gpr_log(
          GPR_DEBUG,
          "[LoadDataStore %p] Start tracking unknown balancer (lb_id_: %s).",
          this, key.lb_id().c_str());
      unknown_balancer_id_trackers.insert(
          {key.lb_id(), static_cast<uint64_t>(in_progress_delta)});
    } else if ((it_tracker->second += in_progress_delta) == 0) {
      unknown_balancer_id_trackers.erase(it_tracker);
      gpr_log(GPR_DEBUG,
              "[LoadDataStore %p] Stop tracking unknown balancer (lb_id_: %s).",
              this, key.lb_id().c_str());
    }
  }
}

const std::vector<PerBalancerStore*> LoadDataStore::GetAssignedStores(
    const grpc::string& hostname, const grpc::string& lb_id) {
  auto it_per_host_store = per_host_stores_.find(hostname);
  if (it_per_host_store != per_host_stores_.end()) {
    return it_per_host_store->second.GetAssignedStores(lb_id);
  }
  return {};
}

void LoadDataStore::ReportStreamCreated(grpc::string hostname,
                                        grpc::string lb_id,
                                        grpc::string load_key) {
  auto rc = per_host_stores_.insert({hostname, PerHostStore()});
  rc.first->second.ReportStreamCreated(std::move(lb_id), std::move(load_key));
}

void LoadDataStore::ReportStreamClosed(grpc::string hostname,
                                       grpc::string lb_id) {
  auto it_per_host_store = per_host_stores_.find(hostname);
  GPR_ASSERT(it_per_host_store != per_host_stores_.end());
  it_per_host_store->second.ReportStreamClosed(std::move(lb_id));
}

}  // namespace load_reporter
}  // namespace grpc
