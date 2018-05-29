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

#include <stdint.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "src/cpp/server/load_reporter/get_cpu_stats.h"
#include "src/cpp/server/load_reporter/load_reporter.h"
#include "src/cpp/server/load_reporter/util.h"

#include "opencensus/stats/internal/set_aggregation_window.h"

namespace grpc {
namespace load_reporter {

CpuStatsProvider::CpuStatsSample CpuStatsProviderDefaultImpl::GetCpuStats() {
  return get_cpu_stats();
}

CensusViewProvider::CensusViewProvider()
    : tag_key_token_(::opencensus::stats::TagKey::Register(kTagKeyToken)),
      tag_key_host_(::opencensus::stats::TagKey::Register(kTagKeyHost)),
      tag_key_user_id_(::opencensus::stats::TagKey::Register(kTagKeyUserId)),
      tag_key_status_(::opencensus::stats::TagKey::Register(kTagKeyStatus)),
      tag_key_metric_name_(
          ::opencensus::stats::TagKey::Register(kTagKeyMetricName)) {
  // One view related to starting a call.
  auto vd_start_count =
      ::opencensus::stats::ViewDescriptor()
          .set_name(kViewStartCount)
          .set_measure(kMeasureStartCount)
          .set_aggregation(::opencensus::stats::Aggregation::Sum())
          .add_column(tag_key_token_)
          .add_column(tag_key_host_)
          .add_column(tag_key_user_id_)
          .set_description(
              "Delta count of calls started broken down by <token, host, "
              "user_id>.");
  ::opencensus::stats::SetAggregationWindow(
      ::opencensus::stats::AggregationWindow::Delta(), &vd_start_count);
  view_descriptor_map_.emplace(kViewStartCount, vd_start_count);
  // Four views related to ending a call.
  // If this view is set as Count of kMeasureEndBytesSent (in hope of saving one
  // measure), it's infeasible to prepare fake data for testing. That's because
  // the OpenCensus API to make up view data will add the input data as separate
  // measurements instead of setting the data values directly.
  auto vd_end_count =
      ::opencensus::stats::ViewDescriptor()
          .set_name((kViewEndCount))
          .set_measure((kMeasureEndCount))
          .set_aggregation(::opencensus::stats::Aggregation::Sum())
          .add_column(tag_key_token_)
          .add_column(tag_key_host_)
          .add_column(tag_key_user_id_)
          .add_column(tag_key_status_)
          .set_description(
              "Delta count of calls ended broken down by <token, host, "
              "user_id, status>.");
  ::opencensus::stats::SetAggregationWindow(
      ::opencensus::stats::AggregationWindow::Delta(), &vd_end_count);
  view_descriptor_map_.emplace(kViewEndCount, vd_end_count);
  auto vd_end_bytes_sent =
      ::opencensus::stats::ViewDescriptor()
          .set_name((kViewEndBytesSent))
          .set_measure((kMeasureEndBytesSent))
          .set_aggregation(::opencensus::stats::Aggregation::Sum())
          .add_column(tag_key_token_)
          .add_column(tag_key_host_)
          .add_column(tag_key_user_id_)
          .add_column(tag_key_status_)
          .set_description(
              "Delta sum of bytes sent broken down by <token, host, user_id, "
              "status>.");
  ::opencensus::stats::SetAggregationWindow(
      ::opencensus::stats::AggregationWindow::Delta(), &vd_end_bytes_sent);
  view_descriptor_map_.emplace(kViewEndBytesSent, vd_end_bytes_sent);
  auto vd_end_bytes_received =
      ::opencensus::stats::ViewDescriptor()
          .set_name((kViewEndBytesReceived))
          .set_measure((kMeasureEndBytesReceived))
          .set_aggregation(::opencensus::stats::Aggregation::Sum())
          .add_column(tag_key_token_)
          .add_column(tag_key_host_)
          .add_column(tag_key_user_id_)
          .add_column(tag_key_status_)
          .set_description(
              "Delta sum of bytes received broken down by <token, host, "
              "user_id, status>.");
  ::opencensus::stats::SetAggregationWindow(
      ::opencensus::stats::AggregationWindow::Delta(), &vd_end_bytes_received);
  view_descriptor_map_.emplace(kViewEndBytesReceived, vd_end_bytes_received);
  auto vd_end_latency_ms =
      ::opencensus::stats::ViewDescriptor()
          .set_name((kViewEndLatencyMs))
          .set_measure((kMeasureEndLatencyMs))
          .set_aggregation(::opencensus::stats::Aggregation::Sum())
          .add_column(tag_key_token_)
          .add_column(tag_key_host_)
          .add_column(tag_key_user_id_)
          .add_column(tag_key_status_)
          .set_description(
              "Delta sum of latency in ms broken down by <token, host, "
              "user_id, status>.");
  ::opencensus::stats::SetAggregationWindow(
      ::opencensus::stats::AggregationWindow::Delta(), &vd_end_latency_ms);
  view_descriptor_map_.emplace(kViewEndLatencyMs, vd_end_latency_ms);
  // Two views related to other call metrics.
  auto vd_metric_call_count =
      ::opencensus::stats::ViewDescriptor()
          .set_name((kViewOtherCallMetricCount))
          .set_measure((kMeasureOtherCallMetric))
          .set_aggregation(::opencensus::stats::Aggregation::Count())
          .add_column(tag_key_token_)
          .add_column(tag_key_host_)
          .add_column(tag_key_user_id_)
          .add_column(tag_key_metric_name_)
          .set_description(
              "Delta count of calls broken down by <token, host, user_id, "
              "metric_name>.");
  ::opencensus::stats::SetAggregationWindow(
      ::opencensus::stats::AggregationWindow::Delta(), &vd_metric_call_count);
  view_descriptor_map_.emplace(kViewOtherCallMetricCount, vd_metric_call_count);
  auto vd_metric_value =
      ::opencensus::stats::ViewDescriptor()
          .set_name((kViewOtherCallMetricValue))
          .set_measure((kMeasureOtherCallMetric))
          .set_aggregation(::opencensus::stats::Aggregation::Sum())
          .add_column(tag_key_token_)
          .add_column(tag_key_host_)
          .add_column(tag_key_user_id_)
          .add_column(tag_key_metric_name_)
          .set_description(
              "Delta sum of call metric value broken down "
              "by <token, host, user_id, metric_name>.");
  ::opencensus::stats::SetAggregationWindow(
      ::opencensus::stats::AggregationWindow::Delta(), &vd_metric_value);
  view_descriptor_map_.emplace(kViewOtherCallMetricValue, vd_metric_value);
}

double CensusViewProvider::GetRelatedViewDataRowDouble(
    const ViewDataMap& view_data_map, const char* view_name,
    size_t view_name_len, const std::vector<grpc::string>& tag_values) {
  auto it_vd = view_data_map.find(grpc::string(view_name, view_name_len));
  GPR_ASSERT(it_vd != view_data_map.end());
  auto it_row = it_vd->second.double_data().find(tag_values);
  GPR_ASSERT(it_row != it_vd->second.double_data().end());
  return it_row->second;
}

CensusViewProviderDefaultImpl::CensusViewProviderDefaultImpl() {
  for (const auto& p : view_descriptor_map_) {
    const grpc::string& view_name = p.first;
    const ::opencensus::stats::ViewDescriptor& vd = p.second;
    // We need to use pair's piecewise ctor here, otherwise the deleted copy
    // ctor of View will be called.
    view_map_.emplace(std::piecewise_construct,
                      std::forward_as_tuple(view_name),
                      std::forward_as_tuple(vd));
  }
}

CensusViewProvider::ViewDataMap CensusViewProviderDefaultImpl::FetchViewData() {
  gpr_log(GPR_DEBUG, "[CVP %p] Starts fetching Census view data.", this);
  ViewDataMap view_data_map;
  for (auto& p : view_map_) {
    const grpc::string& view_name = p.first;
    ::opencensus::stats::View& view = p.second;
    if (view.IsValid()) {
      view_data_map.emplace(view_name, view.GetData());
      gpr_log(GPR_DEBUG, "[CVP %p] Fetched view data (view: %s).", this,
              view_name.c_str());
    } else {
      gpr_log(
          GPR_DEBUG,
          "[CVP %p] Can't fetch view data because view is invalid (view: %s).",
          this, view_name.c_str());
    }
  }
  return view_data_map;
}

grpc::string LoadReporter::GenerateLbId() {
  while (true) {
    if (next_lb_id_ > UINT32_MAX) {
      gpr_log(GPR_ERROR, "[LR %p] The LB ID exceeds the max valid value!",
              this);
      return "";
    }
    int64_t lb_id = next_lb_id_++;
    // Overflow should never happen.
    GPR_ASSERT(lb_id >= 0);
    std::stringstream ss;
    // Convert to padded hex string for a 32-bit LB ID. E.g, "0000ca5b".
    ss << std::setfill('0') << std::setw(kLbIdLength) << std::hex << lb_id;
    grpc::string lb_id_str = ss.str();
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
  // Find the longest range with valid ends.
  LoadBalancingFeedbackRecord* oldest = &feedback_records_[0];
  LoadBalancingFeedbackRecord* newest =
      &feedback_records_[feedback_records_.size() - 1];
  while (newest > oldest &&
         (newest->cpu_limit == 0 || oldest->cpu_limit == 0)) {
    // A zero limit means that the system info reading was failed, so these
    // records can't be used to calculate CPU utilization.
    if (newest->cpu_limit == 0) --newest;
    if (oldest->cpu_limit == 0) ++oldest;
  }
  if (newest - oldest < 1 || oldest->end_time == newest->end_time ||
      newest->cpu_limit == oldest->cpu_limit) {
    return ::grpc::lb::v1::LoadBalancingFeedback::default_instance();
  }
  double rpcs = 0;
  double errors = 0;
  for (LoadBalancingFeedbackRecord* p = newest; p != oldest; --p) {
    // Because these two numbers are counters, the oldest record shouldn't be
    // included.
    rpcs += p->rpcs;
    errors += p->errors;
  }
  double cpu_usage = newest->cpu_usage - oldest->cpu_usage;
  double cpu_limit = newest->cpu_limit - oldest->cpu_limit;
  std::chrono::duration<double> duration_seconds =
      newest->end_time - oldest->end_time;
  lock.unlock();
  ::grpc::lb::v1::LoadBalancingFeedback feedback;
  feedback.set_server_utilization(static_cast<float>(cpu_usage / cpu_limit));
  feedback.set_calls_per_second(
      static_cast<float>(rpcs / duration_seconds.count()));
  feedback.set_errors_per_second(
      static_cast<float>(errors / duration_seconds.count()));
  return feedback;
}

::google::protobuf::RepeatedPtrField<::grpc::lb::v1::Load>
LoadReporter::GenerateLoads(const grpc::string& hostname,
                            const grpc::string& lb_id) {
  std::lock_guard<std::mutex> lock(store_mu_);
  auto assigned_stores = load_data_store_.GetAssignedStores(hostname, lb_id);
  GPR_ASSERT(assigned_stores != nullptr);
  GPR_ASSERT(!assigned_stores->empty());
  ::google::protobuf::RepeatedPtrField<::grpc::lb::v1::Load> loads;
  for (PerBalancerStore* per_balancer_store : *assigned_stores) {
    GPR_ASSERT(!per_balancer_store->IsSuspended());
    if (!per_balancer_store->load_record_map().empty()) {
      for (const auto& p : per_balancer_store->load_record_map()) {
        const auto& key = p.first;
        const auto& value = p.second;
        auto load = loads.Add();
        load->set_load_balance_tag(key.lb_tag());
        load->set_user_id(key.user_id());
        load->set_client_ip_address(key.GetClientIpBytes());
        load->set_num_calls_started(static_cast<int64_t>(value.start_count()));
        load->set_num_calls_finished_without_error(
            static_cast<int64_t>(value.ok_count()));
        load->set_num_calls_finished_with_error(
            static_cast<int64_t>(value.error_count()));
        load->set_total_bytes_sent(static_cast<int64_t>(value.bytes_sent()));
        load->set_total_bytes_received(
            static_cast<int64_t>(value.bytes_recv()));
        load->mutable_total_latency()->set_seconds(
            static_cast<int64_t>(value.latency_ms() / 1000));
        load->mutable_total_latency()->set_nanos(
            (static_cast<int32_t>(value.latency_ms()) % 1000) * 1000000);
        for (const auto& p : value.call_metrics()) {
          const grpc::string& metric_name = p.first;
          const CallMetricValue& metric_value = p.second;
          auto call_metric_data = load->add_metric_data();
          call_metric_data->set_metric_name(metric_name);
          call_metric_data->set_num_calls_finished_with_metric(
              metric_value.num_calls());
          call_metric_data->set_total_metric_value(
              metric_value.total_metric_value());
        }
        if (per_balancer_store->lb_id() != lb_id) {
          // This per-balancer store is an orphan assigned to this receiving
          // balancer.
          AttachOrphanLoadId(load, *per_balancer_store);
        }
      }
      per_balancer_store->ClearLoadRecordMap();
    }
    if (per_balancer_store->IsNumCallsInProgressChangedSinceLastReport()) {
      auto load = loads.Add();
      load->set_num_calls_in_progress(
          per_balancer_store->GetNumCallsInProgressForReport());
      if (per_balancer_store->lb_id() != lb_id) {
        // This per-balancer store is an orphan assigned to this receiving
        // balancer.
        AttachOrphanLoadId(load, *per_balancer_store);
      }
    }
  }
  return loads;
}

void LoadReporter::AttachOrphanLoadId(
    ::grpc::lb::v1::Load* load, const PerBalancerStore& per_balancer_store) {
  if (per_balancer_store.lb_id() == kInvalidLbId) {
    load->set_load_key_unknown(true);
  } else {
    load->set_load_key_unknown(false);
    load->mutable_orphaned_load_identifier()->set_load_key(
        per_balancer_store.load_key());
    load->mutable_orphaned_load_identifier()->set_load_balancer_id(
        per_balancer_store.lb_id());
  }
}

void LoadReporter::AppendNewFeedbackRecord(double rpcs, double errors) {
  CpuStatsProvider::CpuStatsSample cpu_stats;
  if (cpu_stats_provider_ != nullptr) {
    cpu_stats = cpu_stats_provider_->GetCpuStats();
  } else {
    // This will make the load balancing feedback generation a no-op.
    cpu_stats = {0, 0};
  }
  std::unique_lock<std::mutex> lock(feedback_mu_);
  feedback_records_.push_back(
      LoadBalancingFeedbackRecord{std::chrono::system_clock::now(), rpcs,
                                  errors, cpu_stats.first, cpu_stats.second});
}

void LoadReporter::ReportStreamCreated(const grpc::string& hostname,
                                       const grpc::string& lb_id,
                                       const grpc::string& load_key) {
  std::lock_guard<std::mutex> lock(store_mu_);
  load_data_store_.ReportStreamCreated(hostname, lb_id, load_key);
  gpr_log(GPR_INFO,
          "[LR %p] Report stream created (host: %s, LB ID: %s, load key: %s).",
          this, hostname.c_str(), lb_id.c_str(), load_key.c_str());
}

void LoadReporter::ReportStreamClosed(const grpc::string& hostname,
                                      const grpc::string& lb_id) {
  std::lock_guard<std::mutex> lock(store_mu_);
  load_data_store_.ReportStreamClosed(hostname, lb_id);
  gpr_log(GPR_INFO, "[LR %p] Report stream closed (host: %s, LB ID: %s).", this,
          hostname.c_str(), lb_id.c_str());
}

void LoadReporter::FetchAndSample() {
  gpr_log(GPR_DEBUG,
          "[LR %p] Starts fetching Census view data and sampling LB feedback "
          "record.",
          this);
  CensusViewProvider::ViewDataMap view_data_map =
      census_view_provider_->FetchViewData();
  // Process view data about starting call.
  auto it = view_data_map.find(kViewStartCount);
  if (it != view_data_map.end()) {
    // Note that the data type for any Sum view is double, whatever the data
    // type of the original measure.
    for (const auto& p : it->second.double_data()) {
      const std::vector<grpc::string>& tag_values = p.first;
      const double start_count = p.second;
      const grpc::string& client_ip_and_token = tag_values[0];
      const grpc::string& host = tag_values[1];
      const grpc::string& user_id = tag_values[2];
      LoadRecordKey key(client_ip_and_token, user_id);
      LoadRecordValue value = LoadRecordValue(start_count);
      {
        std::unique_lock<std::mutex> lock(store_mu_);
        load_data_store_.MergeRow(host, key, value);
      }
    }
  }
  // Process view data about ending call.
  double total_end_count = 0;
  double total_error_count = 0;
  it = view_data_map.find(kViewEndCount);
  if (it != view_data_map.end()) {
    // Note that the data type for any Sum view is double, whatever the data
    // type of the original measure.
    for (const auto& p : it->second.double_data()) {
      const std::vector<grpc::string>& tag_values = p.first;
      const double end_count = p.second;
      const grpc::string& client_ip_and_token = tag_values[0];
      const grpc::string& host = tag_values[1];
      const grpc::string& user_id = tag_values[2];
      const grpc::string& status = tag_values[3];
      // This is due to a bug reported internally of Java server load reporting
      // implementation.
      // TODO(juanlishen): Check whether this situation happens in OSS C++.
      if (client_ip_and_token.size() == 0) {
        gpr_log(GPR_DEBUG,
                "Skipping processing Opencensus record with empty "
                "client_ip_and_token tag.");
        continue;
      }
      LoadRecordKey key(client_ip_and_token, user_id);
      const double bytes_sent = CensusViewProvider::GetRelatedViewDataRowDouble(
          view_data_map, kViewEndBytesSent, sizeof(kViewEndBytesSent) - 1,
          tag_values);
      const double bytes_received =
          CensusViewProvider::GetRelatedViewDataRowDouble(
              view_data_map, kViewEndBytesReceived,
              sizeof(kViewEndBytesReceived) - 1, tag_values);
      const double latency_ms = CensusViewProvider::GetRelatedViewDataRowDouble(
          view_data_map, kViewEndLatencyMs, sizeof(kViewEndLatencyMs) - 1,
          tag_values);
      double ok_count = 0;
      double error_count = 0;
      total_end_count += end_count;
      if (std::strcmp(status.c_str(), kCallStatusOk) == 0) {
        ok_count = end_count;
      } else {
        error_count = end_count;
        total_error_count += end_count;
      }
      LoadRecordValue value = LoadRecordValue(
          0, ok_count, error_count, bytes_sent, bytes_received, latency_ms);
      {
        std::unique_lock<std::mutex> lock(store_mu_);
        load_data_store_.MergeRow(host, key, value);
      }
    }
  }
  // Append a load balancing feedback record.
  AppendNewFeedbackRecord(total_end_count, total_error_count);
  // Process view data about other metrics.
  it = view_data_map.find(kViewOtherCallMetricCount);
  if (it != view_data_map.end()) {
    for (const auto& p : it->second.int_data()) {
      const std::vector<grpc::string>& tag_values = p.first;
      const int64_t num_calls = p.second;
      const grpc::string& client_ip_and_token = tag_values[0];
      const grpc::string& host = tag_values[1];
      const grpc::string& user_id = tag_values[2];
      const grpc::string& metric_name = tag_values[3];
      LoadRecordKey key(client_ip_and_token, user_id);
      const double total_metric_value =
          CensusViewProvider::GetRelatedViewDataRowDouble(
              view_data_map, kViewOtherCallMetricValue,
              sizeof(kViewOtherCallMetricValue) - 1, tag_values);
      LoadRecordValue value = LoadRecordValue(
          metric_name, static_cast<uint64_t>(num_calls), total_metric_value);
      {
        std::unique_lock<std::mutex> lock(store_mu_);
        load_data_store_.MergeRow(host, key, value);
      }
    }
  }
}

}  // namespace load_reporter
}  // namespace grpc
