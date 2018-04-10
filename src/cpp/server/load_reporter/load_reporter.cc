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
namespace load_reporter {

std::pair<double, double> CpuStatsProviderDefaultImpl::GetCpuStats() {
  uint64_t busy = 0, total = 0;
  // Read the accumulative CPU stats.
#if defined(GPR_LINUX)
  FILE* fp;
  fp = fopen("/proc/stat", "r");
  uint64_t user, nice, system, idle;
  fscanf(fp, "cpu %lu %lu %lu %lu", &user, &nice, &system, &idle);
  fclose(fp);
  busy = user + nice + system;
  total = busy + idle;
  // TODO(juanlishen): Not verified.
#elif defined(GPR_WINDOWS)
  uint64_t idle, kernel, user;
  if (GetSystemTimes(&idle, &kernel, &user) != 0) {
    total = kernal + user;
    busy = total - idle;
  }
#elif defined(GPR_APPLE)
  host_cpu_load_info_data_t cpuinfo;
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                      (host_info_t)&cpuinfo, &count) == KERN_SUCCESS) {
    unsigned long long totalTicks = 0;
    for (int i = 0; i < CPU_STATE_MAX; i++) totalTicks += cpuinfo.cpu_ticks[i];
    return CalculateCPULoad(cpuinfo.cpu_ticks[CPU_STATE_IDLE], totalTicks);
  } else
    return -1.0f;
#else
  GPR_ASSERT(false);
#endif
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

::grpc::lb::v1::LoadBalancingFeedback
LoadReporter::GenerateLoadBalancingFeedback() {
  std::unique_lock<std::mutex> lock(feedback_mu_);
  auto now = std::chrono::system_clock::now();
  // Discard records outside the window until there is only one record
  // outside the window, which is used as the base for difference.
  while (feedback_records_.size() > 1 &&
         !IsRecordInWindow(feedback_records_[1], now)) {
    feedback_records_.pop_front();
  }
  if (feedback_records_.size() < 2) {
    return ::grpc::lb::v1::LoadBalancingFeedback::default_instance();
  }
  LoadBalancingFeedbackRecord* first = &feedback_records_[0];
  LoadBalancingFeedbackRecord* last =
      &feedback_records_[feedback_records_.size() - 1];
  if (first->end_time == last->end_time ||
      last->cpu_limit == first->cpu_limit) {
    return ::grpc::lb::v1::LoadBalancingFeedback::default_instance();
  }
  uint64_t rpcs = 0;
  uint64_t errors = 0;
  for (const auto& record : feedback_records_) {
    rpcs += record.rpcs;
    errors += record.errors;
  }
  double cpu_usage = fake_cpu_usage_per_rpc_ < 0
                         ? last->cpu_usage - first->cpu_usage
                         : rpcs * fake_cpu_usage_per_rpc_;
  double cpu_limit = last->cpu_limit - first->cpu_limit;
  double duration_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                last->end_time - first->end_time)
                                .count();
  lock.release();
  ::grpc::lb::v1::LoadBalancingFeedback feedback;
  feedback.set_server_utilization(static_cast<float>(cpu_usage / cpu_limit));
  feedback.set_calls_per_second(static_cast<float>(rpcs / duration_seconds));
  feedback.set_errors_per_second(static_cast<float>(errors / duration_seconds));
  return feedback;
}

::google::protobuf::RepeatedPtrField<::grpc::lb::v1::Load>
LoadReporter::GenerateLoads(const grpc::string& hostname,
                            const grpc::string& lb_id) {
  std::lock_guard<std::mutex> lock(store_mu_);
  auto assigned_stores = load_data_store_.GetAssignedStores(hostname, lb_id);
  GPR_ASSERT(!assigned_stores.empty());
  ::google::protobuf::RepeatedPtrField<::grpc::lb::v1::Load> loads;
  for (auto& per_balancer_store : assigned_stores) {
    GPR_ASSERT(!per_balancer_store->IsSuspended());
    if (!per_balancer_store->container().empty()) {
      for (const auto& entry : per_balancer_store->container()) {
        const auto& key = entry.first;
        const auto& value = entry.second;
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
        ::google::protobuf::Duration latency;
        latency.set_seconds(static_cast<int64_t>(value.latency_ms() / 1000));
        latency.set_nanos((static_cast<int32_t>(value.latency_ms()) % 1000) *
                          1000);
        load->set_allocated_total_latency(&latency);
        for (const auto& call_metric : value.call_metrics()) {
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
      per_balancer_store->container().clear();
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
    ::grpc::lb::v1::Load* load,
    const std::shared_ptr<PerBalancerStore> per_balancer_store) {
  if (per_balancer_store->lb_id() == kInvalidLbId) {
    load->set_load_key_unknown(true);
  } else {
    load->set_load_key_unknown(false);
    ::grpc::lb::v1::OrphanedLoadIdentifier orphan_load_identifier;
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

void LoadReporter::ReportStreamCreated(const grpc::string& hostname,
                                       const grpc::string& lb_id,
                                       const grpc::string& load_key) {
  std::lock_guard<std::mutex> lock(store_mu_);
  load_data_store_.ReportStreamCreated(hostname, lb_id, load_key);
}

void LoadReporter::ReportStreamClosed(const grpc::string& hostname,
                                      const grpc::string& lb_id) {
  std::lock_guard<std::mutex> lock(store_mu_);
  load_data_store_.ReportStreamClosed(hostname, lb_id);
}

void LoadReporter::FetchAndSample() {
  // TODO(juanlishen): Implement when Census has unblocked.
}

}  // namespace load_reporter
}  // namespace grpc