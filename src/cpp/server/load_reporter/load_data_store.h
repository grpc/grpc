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

#ifndef GRPC_SRC_CPP_SERVER_LOAD_REPORTER_LOAD_DATA_STORE_H
#define GRPC_SRC_CPP_SERVER_LOAD_REPORTER_LOAD_DATA_STORE_H

#include <grpc/impl/codegen/port_platform.h>

#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/config.h>

#include "src/cpp/server/load_reporter/util.h"

namespace grpc {

class CallMetricValue {
 public:
  CallMetricValue(uint64_t count, double total)
      : count_(count), total_(total) {}

  void MergeFrom(CallMetricValue other) {
    count_ += other.count_;
    total_ += other.total_;
  }

  // Getters.
  uint64_t count() const { return count_; }
  double total() const { return total_; }

 private:
  uint64_t count_;
  double total_;
};

class Key {
 public:
  explicit Key(grpc::string lb_id, grpc::string lb_tag, grpc::string user_id,
               grpc::string client_ip_hex)
      : lb_id_(std::move(lb_id)),
        lb_tag_(std::move(lb_tag)),
        user_id_(std::move(user_id)),
        client_ip_hex_(std::move(client_ip_hex)) {}

  grpc::string ToString() const {
    return "[lb_id_=" + lb_id_ + ", lb_tag_=" + lb_tag_ +
           ", user_id_=" + user_id_ + ", client_ip_hex_=" + client_ip_hex_ +
           "]";
  }

  bool operator==(const Key& other) const {
    return lb_id_ == other.lb_id_ && lb_tag_ == other.lb_tag_ &&
           user_id_ == other.user_id_ && client_ip_hex_ == other.client_ip_hex_;
  }

  // Getters.
  const grpc::string& lb_id() const { return lb_id_; }
  const grpc::string& lb_tag() const { return lb_tag_; }
  const grpc::string& user_id() const { return user_id_; }
  const grpc::string& client_ip_hex() const { return client_ip_hex_; }

 private:
  struct Hasher {
    size_t operator()(const Key& k) const {
      return std::hash<grpc::string>()(k.lb_id_) ^
             std::hash<grpc::string>()(k.lb_tag_) ^
             std::hash<grpc::string>()(k.user_id_) ^
             std::hash<grpc::string>()(k.client_ip_hex_);
    }
  };

  // To use Hasher.
  friend class PerBalancerStore;
  grpc::string lb_id_;
  grpc::string lb_tag_;
  grpc::string user_id_;
  grpc::string client_ip_hex_;
};

class Value {
 public:
  Value(uint64_t start_count = 0, uint64_t ok_count = 0,
        uint64_t error_count = 0, double bytes_sent = 0, double bytes_recv = 0,
        double latency_ms = 0)
      : start_count_(start_count),
        ok_count_(ok_count),
        error_count_(error_count),
        bytes_sent_(bytes_sent),
        bytes_recv_(bytes_recv),
        latency_ms_(latency_ms) {}

  void MergeFrom(const Value& other) {
    start_count_ += other.start_count_;
    ok_count_ += other.ok_count_;
    error_count_ += other.error_count_;
    bytes_sent_ += other.bytes_sent_;
    bytes_recv_ += other.bytes_recv_;
    latency_ms_ += other.latency_ms_;
    for (auto other_call_metric : other.call_metrics_) {
      grpc::string metric_name = other_call_metric.first;
      CallMetricValue other_metric_value = other_call_metric.second;
      auto it_this_metric = call_metrics_.find(metric_name);
      if (it_this_metric == call_metrics_.end()) {
        call_metrics_.insert({metric_name, other_metric_value});
      } else {
        it_this_metric->second.MergeFrom(other_metric_value);
      }
    }
  }

  int64_t GetNumCallsInProgressDelta() const {
    return static_cast<int64_t>(start_count_ - ok_count_ - error_count_);
  }

  grpc::string ToString() const {
    return "[start_count_=" + grpc::to_string(start_count_) +
           ", ok_count_=" + grpc::to_string(ok_count_) +
           ", error_count_=" + grpc::to_string(error_count_) +
           ", bytes_sent_=" + grpc::to_string(bytes_sent_) +
           ", "
           "bytes_recv_=" +
           grpc::to_string(bytes_recv_) +
           ", latency_ms_=" + grpc::to_string(latency_ms_) + "]";
  }

  bool InsertCallMetric(const grpc::string& metric_name,
                        const CallMetricValue& metric_value) {
    return call_metrics_.insert({metric_name, metric_value}).second;
  }

  // Getters.
  uint64_t start_count() const { return start_count_; }
  uint64_t ok_count() const { return ok_count_; }
  uint64_t error_count() const { return error_count_; }
  double bytes_sent() const { return bytes_sent_; }
  double bytes_recv() const { return bytes_recv_; }
  double latency_ms() const { return latency_ms_; }
  const std::unordered_map<grpc::string, CallMetricValue>& call_metrics()
      const {
    return call_metrics_;
  }

 private:
  uint64_t start_count_ = 0;
  uint64_t ok_count_ = 0;
  uint64_t error_count_ = 0;
  double bytes_sent_ = 0;
  double bytes_recv_ = 0;
  double latency_ms_ = 0;
  std::unordered_map<grpc::string, CallMetricValue> call_metrics_;
};

// The data associated with a particular LB ID.
class PerBalancerStore {
 public:
  PerBalancerStore(grpc::string lb_id, grpc::string load_key)
      : lb_id_(std::move(lb_id)), load_key_(std::move(load_key)) {}

  void MergeRow(const Key& key, const Value& value);

  bool IsSuspended() const { return suspended_; }

  void Suspend();

  void Resume();

  bool IsNumCallsInProgressChangedSinceLastReport() const {
    return num_calls_in_progress_ != last_reported_num_calls_in_progress_;
  }

  uint64_t GetNumCallsInProgressForReport();

  std::unordered_map<Key, Value, Key::Hasher>& container() {
    return container_;
  }

  void ClearContainer() { container_.clear(); }

  grpc::string ToString() {
    return "[PerBalancerStore lb_id_=" + lb_id_ + " load_key_=" + load_key_ +
           "]";
  }

  // Getters.
  const grpc::string& lb_id() const { return lb_id_; }
  const grpc::string& load_key() const { return load_key_; }

 private:
  grpc::string lb_id_;
  // TODO(juanlishen): Use bytestring protobuf type?
  grpc::string load_key_;
  std::unordered_map<Key, Value, Key::Hasher> container_;
  uint64_t num_calls_in_progress_ = 0;
  uint64_t last_reported_num_calls_in_progress_ = 0;
  bool suspended_ = false;
};

class PerHostStore {
 public:
  ~PerHostStore();

  void ReportStreamCreated(grpc::string lb_id, grpc::string load_key);

  void ReportStreamClosed(grpc::string lb_id);

 private:
  friend class LoadDataStore;

  void InternalAddLb(grpc::string lb_id, grpc::string load_key);

  void AssignOrphanedStore(PerBalancerStore* orphaned_store,
                           grpc::string new_receiver);

  std::unordered_multimap<grpc::string, grpc::string>
      load_key_to_receiving_lb_ids_;

  // Key: LB ID. The key set includes all the LB IDs that have been
  // allocated for reporting streams so far.
  std::unordered_map<grpc::string, PerBalancerStore*> per_balancer_stores_;

  // Key: LB ID. The key set includes the LB IDs of the balancers that are
  // currently receiving report.
  std::unordered_multimap<grpc::string, PerBalancerStore*> assigned_stores_;
};

// Two-level bookkeeper of all the load data.
// Note: We never remove any store objects from this class, as per the
// current spec. That's because premature removal of the store objects
// may lead to loss of critical information, e.g., mapping from lb_id to
// load_key, and the number of in-progress calls. Such loss will cause
// information inconsistency when the balancer is re-connected. Keeping
// all the stores should be fine for PerHostStore, since we assume there
// should only be a few hostnames. But it's a potential problem for
// PerBalancerStore.
class LoadDataStore {
 public:
  PerBalancerStore* GetPerBalancerStoreForTest(grpc::string hostname,
                                               grpc::string lb_id) const;

  void MergeRow(grpc::string hostname, Key key, Value value);

  bool IsTrackedUnknownBalancerId(const grpc::string& lb_id) const {
    return unknown_balancer_id_trackers.find(lb_id) !=
           unknown_balancer_id_trackers.end();
  }

  const std::vector<PerBalancerStore*> GetAssignedStores(grpc::string hostname,
                                                         grpc::string lb_id);

  void ReportStreamCreated(grpc::string hostname, grpc::string lb_id,
                           grpc::string load_key);

  void ReportStreamClosed(grpc::string hostname, grpc::string lb_id);

 private:
  // Buffered data that was fetched from Census but hasn't been sent to
  // balancer. We need to keep this data ourselves because Census will
  // delete the data once it's returned.
  std::unordered_map<grpc::string, PerHostStore> per_host_stores_;

  std::unordered_map<grpc::string, uint64_t> unknown_balancer_id_trackers;
};

}  // namespace grpc

#endif  // GRPC_SRC_CPP_SERVER_LOAD_REPORTER_LOAD_DATA_STORE_H
