//
//
// Copyright 2018 gRPC authors.
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
//

#include "src/cpp/server/load_reporter/load_data_store.h"

#include <grpc/support/port_platform.h>
#include <stdint.h>
#include <stdio.h>

#include <cstdlib>
#include <iterator>
#include <set>
#include <unordered_map>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/cpp/server/load_reporter/constants.h"

namespace grpc {
namespace load_reporter {

// Some helper functions.
namespace {

// Given a map from type K to a set of value type V, finds the set associated
// with the given key and erases the value from the set. If the set becomes
// empty, also erases the key-set pair. Returns true if the value is erased
// successfully.
template <typename K, typename V>
bool UnorderedMapOfSetEraseKeyValue(std::unordered_map<K, std::set<V>>& map,
                                    const K& key, const V& value) {
  auto it = map.find(key);
  if (it != map.end()) {
    size_t erased = it->second.erase(value);
    if (it->second.empty()) {
      map.erase(it);
    }
    return erased;
  }
  return false;
};

// Given a map from type K to a set of value type V, removes the given key and
// the associated set, and returns the set. Returns an empty set if the key is
// not found.
template <typename K, typename V>
std::set<V> UnorderedMapOfSetExtract(std::unordered_map<K, std::set<V>>& map,
                                     const K& key) {
  auto it = map.find(key);
  if (it != map.end()) {
    auto set = std::move(it->second);
    map.erase(it);
    return set;
  }
  return {};
};

// From a non-empty container, returns a pointer to a random element.
template <typename C>
const typename C::value_type* RandomElement(const C& container) {
  CHECK(!container.empty());
  auto it = container.begin();
  std::advance(it, std::rand() % container.size());
  return &(*it);
}

}  // namespace

LoadRecordKey::LoadRecordKey(const std::string& client_ip_and_token,
                             std::string user_id)
    : user_id_(std::move(user_id)) {
  CHECK_GE(client_ip_and_token.size(), 2u);
  int ip_hex_size;
  CHECK(sscanf(client_ip_and_token.substr(0, 2).c_str(), "%d", &ip_hex_size) ==
        1);
  CHECK(ip_hex_size == 0 || ip_hex_size == kIpv4AddressLength ||
        ip_hex_size == kIpv6AddressLength);
  size_t cur_pos = 2;
  client_ip_hex_ = client_ip_and_token.substr(cur_pos, ip_hex_size);
  cur_pos += ip_hex_size;
  if (client_ip_and_token.size() - cur_pos < kLbIdLength) {
    lb_id_ = kInvalidLbId;
    lb_tag_ = "";
  } else {
    lb_id_ = client_ip_and_token.substr(cur_pos, kLbIdLength);
    lb_tag_ = client_ip_and_token.substr(cur_pos + kLbIdLength);
  }
}

std::string LoadRecordKey::GetClientIpBytes() const {
  if (client_ip_hex_.empty()) {
    return "";
  } else if (client_ip_hex_.size() == kIpv4AddressLength) {
    uint32_t ip_bytes;
    if (sscanf(client_ip_hex_.c_str(), "%x", &ip_bytes) != 1) {
      LOG(ERROR) << "Can't parse client IP (" << client_ip_hex_
                 << ") from a hex string to an integer.";
      return "";
    }
    ip_bytes = grpc_htonl(ip_bytes);
    return std::string(reinterpret_cast<const char*>(&ip_bytes),
                       sizeof(ip_bytes));
  } else if (client_ip_hex_.size() == kIpv6AddressLength) {
    uint32_t ip_bytes[4];
    for (size_t i = 0; i < 4; ++i) {
      if (sscanf(client_ip_hex_.substr(i * 8, (i + 1) * 8).c_str(), "%x",
                 ip_bytes + i) != 1) {
        LOG(ERROR) << "Can't parse client IP part ("
                   << client_ip_hex_.substr(i * 8, (i + 1) * 8)
                   << ") from a hex string to an integer.";
        return "";
      }
      ip_bytes[i] = grpc_htonl(ip_bytes[i]);
    }
    return std::string(reinterpret_cast<const char*>(ip_bytes),
                       sizeof(ip_bytes));
  } else {
    GPR_UNREACHABLE_CODE(return "");
  }
}

LoadRecordValue::LoadRecordValue(std::string metric_name, uint64_t num_calls,
                                 double total_metric_value) {
  call_metrics_.emplace(std::move(metric_name),
                        CallMetricValue(num_calls, total_metric_value));
}

void PerBalancerStore::MergeRow(const LoadRecordKey& key,
                                const LoadRecordValue& value) {
  // During suspension, the load data received will be dropped.
  if (!suspended_) {
    load_record_map_[key].MergeFrom(value);
    VLOG(2) << "[PerBalancerStore " << this
            << "] Load data merged (Key: " << key.ToString()
            << ", Value: " << value.ToString() << ").";
  } else {
    VLOG(2) << "[PerBalancerStore " << this
            << "] Load data dropped (Key: " << key.ToString()
            << ", Value: " << value.ToString() << ").";
  }
  // We always keep track of num_calls_in_progress_, so that when this
  // store is resumed, we still have a correct value of
  // num_calls_in_progress_.
  CHECK(static_cast<int64_t>(num_calls_in_progress_) +
            value.GetNumCallsInProgressDelta() >=
        0);
  num_calls_in_progress_ += value.GetNumCallsInProgressDelta();
}

void PerBalancerStore::Suspend() {
  suspended_ = true;
  load_record_map_.clear();
  VLOG(2) << "[PerBalancerStore " << this << "] Suspended.";
}

void PerBalancerStore::Resume() {
  suspended_ = false;
  VLOG(2) << "[PerBalancerStore " << this << "] Resumed.";
}

uint64_t PerBalancerStore::GetNumCallsInProgressForReport() {
  CHECK(!suspended_);
  last_reported_num_calls_in_progress_ = num_calls_in_progress_;
  return num_calls_in_progress_;
}

void PerHostStore::ReportStreamCreated(const std::string& lb_id,
                                       const std::string& load_key) {
  CHECK(lb_id != kInvalidLbId);
  SetUpForNewLbId(lb_id, load_key);
  // Prior to this one, there was no load balancer receiving report, so we may
  // have unassigned orphaned stores to assign to this new balancer.
  // TODO(juanlishen): If the load key of this new stream is the same with
  // some previously adopted orphan store, we may want to take the orphan to
  // this stream. Need to discuss with LB team.
  if (assigned_stores_.size() == 1) {
    for (const auto& p : per_balancer_stores_) {
      const std::string& other_lb_id = p.first;
      const std::unique_ptr<PerBalancerStore>& orphaned_store = p.second;
      if (other_lb_id != lb_id) {
        orphaned_store->Resume();
        AssignOrphanedStore(orphaned_store.get(), lb_id);
      }
    }
  }
  // The first connected balancer will adopt the kInvalidLbId.
  if (per_balancer_stores_.size() == 1) {
    SetUpForNewLbId(kInvalidLbId, "");
    ReportStreamClosed(kInvalidLbId);
  }
}

void PerHostStore::ReportStreamClosed(const std::string& lb_id) {
  auto it_store_for_gone_lb = per_balancer_stores_.find(lb_id);
  CHECK(it_store_for_gone_lb != per_balancer_stores_.end());
  // Remove this closed stream from our records.
  CHECK(UnorderedMapOfSetEraseKeyValue(load_key_to_receiving_lb_ids_,
                                       it_store_for_gone_lb->second->load_key(),
                                       lb_id));
  std::set<PerBalancerStore*> orphaned_stores =
      UnorderedMapOfSetExtract(assigned_stores_, lb_id);
  // The stores that were assigned to this balancer are orphaned now. They
  // should be re-assigned to other balancers which are still receiving reports.
  for (PerBalancerStore* orphaned_store : orphaned_stores) {
    const std::string* new_receiver = nullptr;
    auto it = load_key_to_receiving_lb_ids_.find(orphaned_store->load_key());
    if (it != load_key_to_receiving_lb_ids_.end()) {
      // First, try to pick from the active balancers with the same load key.
      new_receiver = RandomElement(it->second);
    } else if (!assigned_stores_.empty()) {
      // If failed, pick from all the remaining active balancers.
      new_receiver = &(RandomElement(assigned_stores_)->first);
    }
    if (new_receiver != nullptr) {
      AssignOrphanedStore(orphaned_store, *new_receiver);
    } else {
      // Load data for an LB ID that can't be assigned to any stream should
      // be dropped.
      orphaned_store->Suspend();
    }
  }
}

PerBalancerStore* PerHostStore::FindPerBalancerStore(
    const std::string& lb_id) const {
  return per_balancer_stores_.find(lb_id) != per_balancer_stores_.end()
             ? per_balancer_stores_.find(lb_id)->second.get()
             : nullptr;
}

const std::set<PerBalancerStore*>* PerHostStore::GetAssignedStores(
    const std::string& lb_id) const {
  auto it = assigned_stores_.find(lb_id);
  if (it == assigned_stores_.end()) return nullptr;
  return &(it->second);
}

void PerHostStore::AssignOrphanedStore(PerBalancerStore* orphaned_store,
                                       const std::string& new_receiver) {
  auto it = assigned_stores_.find(new_receiver);
  CHECK(it != assigned_stores_.end());
  it->second.insert(orphaned_store);
  LOG(INFO) << "[PerHostStore " << this << "] Re-assigned orphaned store ("
            << orphaned_store << ") with original LB ID of "
            << orphaned_store->lb_id() << " to new receiver " << new_receiver;
}

void PerHostStore::SetUpForNewLbId(const std::string& lb_id,
                                   const std::string& load_key) {
  // The top-level caller (i.e., LoadReportService) should guarantee the
  // lb_id is unique for each reporting stream.
  CHECK(per_balancer_stores_.find(lb_id) == per_balancer_stores_.end());
  CHECK(assigned_stores_.find(lb_id) == assigned_stores_.end());
  load_key_to_receiving_lb_ids_[load_key].insert(lb_id);
  std::unique_ptr<PerBalancerStore> per_balancer_store(
      new PerBalancerStore(lb_id, load_key));
  assigned_stores_[lb_id] = {per_balancer_store.get()};
  per_balancer_stores_[lb_id] = std::move(per_balancer_store);
}

PerBalancerStore* LoadDataStore::FindPerBalancerStore(
    const string& hostname, const string& lb_id) const {
  auto it = per_host_stores_.find(hostname);
  if (it != per_host_stores_.end()) {
    const PerHostStore& per_host_store = it->second;
    return per_host_store.FindPerBalancerStore(lb_id);
  } else {
    return nullptr;
  }
}

void LoadDataStore::MergeRow(const std::string& hostname,
                             const LoadRecordKey& key,
                             const LoadRecordValue& value) {
  PerBalancerStore* per_balancer_store =
      FindPerBalancerStore(hostname, key.lb_id());
  if (per_balancer_store != nullptr) {
    per_balancer_store->MergeRow(key, value);
    return;
  }
  // Unknown LB ID. Track it until its number of in-progress calls drops to
  // zero.
  int64_t in_progress_delta = value.GetNumCallsInProgressDelta();
  if (in_progress_delta != 0) {
    auto it_tracker = unknown_balancer_id_trackers_.find(key.lb_id());
    if (it_tracker == unknown_balancer_id_trackers_.end()) {
      VLOG(2) << "[LoadDataStore " << this
              << "] Start tracking unknown balancer (lb_id_: " << key.lb_id()
              << ").";
      unknown_balancer_id_trackers_.insert(
          {key.lb_id(), static_cast<uint64_t>(in_progress_delta)});
    } else if ((it_tracker->second += in_progress_delta) == 0) {
      unknown_balancer_id_trackers_.erase(it_tracker);
      VLOG(2) << "[LoadDataStore " << this
              << "] Stop tracking unknown balancer (lb_id_: " << key.lb_id()
              << ").";
    }
  }
}

const std::set<PerBalancerStore*>* LoadDataStore::GetAssignedStores(
    const std::string& hostname, const std::string& lb_id) {
  auto it = per_host_stores_.find(hostname);
  if (it == per_host_stores_.end()) return nullptr;
  return it->second.GetAssignedStores(lb_id);
}

void LoadDataStore::ReportStreamCreated(const std::string& hostname,
                                        const std::string& lb_id,
                                        const std::string& load_key) {
  per_host_stores_[hostname].ReportStreamCreated(lb_id, load_key);
}

void LoadDataStore::ReportStreamClosed(const std::string& hostname,
                                       const std::string& lb_id) {
  auto it_per_host_store = per_host_stores_.find(hostname);
  CHECK(it_per_host_store != per_host_stores_.end());
  it_per_host_store->second.ReportStreamClosed(lb_id);
}

}  // namespace load_reporter
}  // namespace grpc
