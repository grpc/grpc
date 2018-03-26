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

#include <iomanip>
#include <sstream>

#include "src/cpp/server/load_reporter/load_reporter.h"

namespace grpc {

std::pair<double, double> CpuStatsProviderDefaultImpl::GetCpuStats() {
  uint64_t busy, total;
  // Read the accumulative CPU stats.
  FILE* fp;
  fp = fopen("/proc/stat", "r");
  uint64_t user, nice, system, idle;
  fscanf(fp, "cpu %lu %lu %lu %lu", &user, &nice, &system, &idle);
  fclose(fp);
  busy = user + nice + system;
  total = busy + idle;
  return std::make_pair(busy, total);
}

grpc::string LoadReporter::GenerateLbId() {
  while (true) {
    int64_t lb_id = next_lb_id_++;
    // Check that there is no overflow.
    GPR_ASSERT(lb_id >= 0);
    std::stringstream ss;
    // Convert to padded hex string for a 32-bit LB ID. E.g, "0000ca5b".
    ss << std::setfill('0') << std::setw(8) << std::hex << lb_id;
    grpc::string lb_id_str = ss.str();
    GPR_ASSERT(LB_ID_LEN == lb_id_str.length());
    // The client may send requests with LB ID that has never been allocated
    // by this load reporter. Those IDs are tracked and will be skipped when
    // we generate a new ID.
    if (!load_data_store_.IsTrackedUnknownBalancerId(lb_id_str)) {
      return lb_id_str;
    }
  }
}

LoadBalancingFeedback LoadReporter::GenerateLoadBalancingFeedback() {
  std::unique_lock<std::mutex> lock(feedback_mu_);
  if (feedback_records_.size() < 2) {
    return LoadBalancingFeedback::default_instance();
  }
  LoadBalancingFeedbackRecord* first = &feedback_records_[0];
  LoadBalancingFeedbackRecord* last =
      &feedback_records_[feedback_records_.size() - 1];
  if (first->end_time_ == last->end_time_ ||
      last->cpu_limit_ == first->cpu_limit_) {
    return LoadBalancingFeedback::default_instance();
  }
  uint64_t rpcs = 0;
  uint64_t errors = 0;
  for (auto record : feedback_records_) {
    rpcs += record.rpcs_;
    errors += record.errors_;
  }
  double cpu_usage = fake_cpu_usage_per_rpc_ < 0
                         ? last->cpu_usage_ - first->cpu_usage_
                         : rpcs * fake_cpu_usage_per_rpc_;
  double cpu_limit = last->cpu_limit_ - first->cpu_limit_;
  double duration_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                last->end_time_ - first->end_time_)
                                .count();
  lock.release();
  LoadBalancingFeedback feedback;
  feedback.set_server_utilization(static_cast<float>(cpu_usage / cpu_limit));
  feedback.set_calls_per_second(static_cast<float>(rpcs / duration_seconds));
  feedback.set_errors_per_second(static_cast<float>(errors / duration_seconds));
  return feedback;
}

::google::protobuf::RepeatedPtrField<Load> LoadReporter::GenerateLoads(
    grpc::string hostname, grpc::string lb_id) {
  std::lock_guard<std::mutex> lock(store_mu_);
  std::vector<PerBalancerStore*> per_balancer_stores =
      load_data_store_.GetAssignedStores(std::move(hostname), lb_id);
  ::google::protobuf::RepeatedPtrField<Load> loads;
  for (auto per_balancer_store : per_balancer_stores) {
    GPR_ASSERT(!per_balancer_store->IsSuspended());
    if (!per_balancer_store->container().empty()) {
      for (auto entry : per_balancer_store->container()) {
        Key key = entry.first;
        Value value = entry.second;
        auto load = loads.Add();
        load->set_load_balance_tag(key.lb_tag());
        load->set_user_id(key.user_id());
        // TODO(juanlishen): Bytes field. Need decoding?
        load->set_client_ip_address(key.client_ip_hex());
        load->set_num_calls_started(value.start_count());
        load->set_num_calls_finished_without_error(value.ok_count());
        load->set_num_calls_finished_with_error(value.error_count());
        load->set_total_bytes_sent(static_cast<int64_t>(value.bytes_sent()));
        load->set_total_bytes_received(
            static_cast<int64_t>(value.bytes_recv()));
        Duration latency;
        latency.set_seconds(static_cast<int64_t>(value.latency_ms() / 1000));
        latency.set_nanos((static_cast<int32_t>(value.latency_ms()) % 1000) *
                          1000);
        load->set_allocated_total_latency(&latency);
        for (auto call_metric : value.call_metrics()) {
          auto call_metric_data = load->add_metric_data();
          call_metric_data->set_metric_name(call_metric.first);
          call_metric_data->set_num_calls_finished_with_metric(
              call_metric.second.count());
          call_metric_data->set_total_metric_value(call_metric.second.total());
        }
        if (per_balancer_store->lb_id() != lb_id) {
          // This per-balancer store is an orphan assigned to this receiving
          // balancer. Extract an OrphanedLoadIdentifier from the
          // per-balancer store and attach it to the load.
          AttachOrphanLoadId(load, per_balancer_store);
        }
      }
      per_balancer_store->ClearContainer();
    }
    if (per_balancer_store->IsNumCallsInProgressChangedSinceLastReport()) {
      auto load = loads.Add();
      load->set_num_calls_in_progress(
          per_balancer_store->GetNumCallsInProgressForReport());
      if (per_balancer_store->lb_id() != lb_id) {
        AttachOrphanLoadId(load, per_balancer_store);
      }
    }
  }
  return loads;
}

void LoadReporter::AttachOrphanLoadId(
    Load* load, const PerBalancerStore* per_balancer_store) {
  if (per_balancer_store->lb_id() == INVALID_LBID) {
    load->set_load_key_unknown(true);
  } else {
    load->set_load_key_unknown(false);
    OrphanedLoadIdentifier orphan_load_identifier;
    orphan_load_identifier.set_load_key(per_balancer_store->load_key());
    orphan_load_identifier.set_load_balancer_id(per_balancer_store->lb_id());
    load->set_allocated_orphaned_load_identifier(&orphan_load_identifier);
  }
}

void LoadReporter::AppendNewFeedbackRecord(uint64_t rpcs, uint64_t errors) {
  auto cpu_stats = cpu_stats_provider_->GetCpuStats();
  feedback_records_.push_back(
      LoadBalancingFeedbackRecord{std::chrono::system_clock::now(), rpcs,
                                  errors, cpu_stats.first, cpu_stats.second});
}

void LoadReporter::ReportStreamCreated(grpc::string hostname,
                                       grpc::string lb_id,
                                       grpc::string load_key) {
  std::lock_guard<std::mutex> lock(store_mu_);
  load_data_store_.ReportStreamCreated(std::move(hostname), std::move(lb_id),
                                       std::move(load_key));
}

void LoadReporter::ReportStreamClosed(grpc::string hostname,
                                      grpc::string lb_id) {
  std::lock_guard<std::mutex> lock(store_mu_);
  load_data_store_.ReportStreamClosed(std::move(hostname), std::move(lb_id));
}

void LoadReporter::FetchAndSample() {
  // TODO(juanlishen): Implement when Census has unblocked.
}

}  // namespace grpc