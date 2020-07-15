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

#include <grpc/impl/codegen/port_platform.h>

#include <set>
#include <vector>

#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/cpp/server/load_reporter/load_data_store.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

using ::grpc::load_reporter::CallMetricValue;
using ::grpc::load_reporter::kInvalidLbId;
using ::grpc::load_reporter::LoadDataStore;
using ::grpc::load_reporter::LoadRecordKey;
using ::grpc::load_reporter::LoadRecordValue;
using ::grpc::load_reporter::PerBalancerStore;

class LoadDataStoreTest : public ::testing::Test {
 public:
  LoadDataStoreTest()
      : kKey1(kLbId1, kLbTag1, kUser1, kClientIp1),
        kKey2(kLbId2, kLbTag2, kUser2, kClientIp2) {}

  // Check whether per_balancer_stores contains a store which was originally
  // created for <hostname, lb_id, and load_key>.
  bool PerBalancerStoresContains(
      const LoadDataStore& load_data_store,
      const std::set<PerBalancerStore*>* per_balancer_stores,
      const std::string& hostname, const std::string& lb_id,
      const std::string& load_key) {
    auto original_per_balancer_store =
        load_data_store.FindPerBalancerStore(hostname, lb_id);
    EXPECT_NE(original_per_balancer_store, nullptr);
    EXPECT_EQ(original_per_balancer_store->lb_id(), lb_id);
    EXPECT_EQ(original_per_balancer_store->load_key(), load_key);
    for (auto per_balancer_store : *per_balancer_stores) {
      if (per_balancer_store == original_per_balancer_store) {
        return true;
      }
    }
    return false;
  }

  std::string FormatLbId(size_t index) {
    return "kLbId" + std::to_string(index);
  }

  const std::string kHostname1 = "kHostname1";
  const std::string kHostname2 = "kHostname2";
  const std::string kLbId1 = "kLbId1";
  const std::string kLbId2 = "kLbId2";
  const std::string kLbId3 = "kLbId3";
  const std::string kLbId4 = "kLbId4";
  const std::string kLoadKey1 = "kLoadKey1";
  const std::string kLoadKey2 = "kLoadKey2";
  const std::string kLbTag1 = "kLbTag1";
  const std::string kLbTag2 = "kLbTag2";
  const std::string kUser1 = "kUser1";
  const std::string kUser2 = "kUser2";
  const std::string kClientIp1 = "00";
  const std::string kClientIp2 = "02";
  const std::string kMetric1 = "kMetric1";
  const std::string kMetric2 = "kMetric2";
  const LoadRecordKey kKey1;
  const LoadRecordKey kKey2;
};

using PerBalancerStoreTest = LoadDataStoreTest;

TEST_F(LoadDataStoreTest, AssignToSelf) {
  LoadDataStore load_data_store;
  load_data_store.ReportStreamCreated(kHostname1, kLbId1, kLoadKey1);
  auto assigned_stores = load_data_store.GetAssignedStores(kHostname1, kLbId1);
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_stores,
                                        kHostname1, kLbId1, kLoadKey1));
}

TEST_F(LoadDataStoreTest, ReassignOrphanStores) {
  LoadDataStore load_data_store;
  load_data_store.ReportStreamCreated(kHostname1, kLbId1, kLoadKey1);
  load_data_store.ReportStreamCreated(kHostname1, kLbId2, kLoadKey1);
  load_data_store.ReportStreamCreated(kHostname1, kLbId3, kLoadKey2);
  load_data_store.ReportStreamCreated(kHostname2, kLbId4, kLoadKey1);
  // 1. Close the second stream.
  load_data_store.ReportStreamClosed(kHostname1, kLbId2);
  auto assigned_to_lb_id_1 =
      load_data_store.GetAssignedStores(kHostname1, kLbId1);
  // The orphaned store is re-assigned to kLbId1 with the same load key.
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_1,
                                        kHostname1, kLbId1, kLoadKey1));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_1,
                                        kHostname1, kLbId2, kLoadKey1));
  // 2. Close the first stream.
  load_data_store.ReportStreamClosed(kHostname1, kLbId1);
  auto assigned_to_lb_id_3 =
      load_data_store.GetAssignedStores(kHostname1, kLbId3);
  // The orphaned stores are re-assigned to kLbId3 with the same host,
  // because there isn't any LB with the same load key.
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        kHostname1, kLbId1, kLoadKey1));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        kHostname1, kLbId2, kLoadKey1));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        kHostname1, kLbId3, kLoadKey2));
  // 3. Close the third stream.
  load_data_store.ReportStreamClosed(kHostname1, kLbId3);
  auto assigned_to_lb_id_4 =
      load_data_store.GetAssignedStores(kHostname2, kLbId4);
  // There is no active LB for the first host now. kLbId4 is active but
  // it's for the second host, so it wll NOT adopt the orphaned stores.
  EXPECT_FALSE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_4,
                                         kHostname1, kLbId1, kLoadKey1));
  EXPECT_FALSE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_4,
                                         kHostname1, kLbId2, kLoadKey1));
  EXPECT_FALSE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_4,
                                         kHostname1, kLbId3, kLoadKey2));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_4,
                                        kHostname2, kLbId4, kLoadKey1));
}

TEST_F(LoadDataStoreTest, OrphanAssignmentIsSticky) {
  LoadDataStore load_data_store;
  std::set<std::string> active_lb_ids;
  size_t num_lb_ids = 1000;
  for (size_t i = 0; i < num_lb_ids; ++i) {
    load_data_store.ReportStreamCreated(kHostname1, FormatLbId(i), kLoadKey1);
    active_lb_ids.insert(FormatLbId(i));
  }
  std::string orphaned_lb_id = FormatLbId(std::rand() % num_lb_ids);
  load_data_store.ReportStreamClosed(kHostname1, orphaned_lb_id);
  active_lb_ids.erase(orphaned_lb_id);
  // Find which LB is assigned the orphaned store.
  std::string assigned_lb_id = "";
  for (const auto& lb_id : active_lb_ids) {
    if (PerBalancerStoresContains(
            load_data_store,
            load_data_store.GetAssignedStores(kHostname1, lb_id), kHostname1,
            orphaned_lb_id, kLoadKey1)) {
      assigned_lb_id = lb_id;
      break;
    }
  }
  EXPECT_STRNE(assigned_lb_id.c_str(), "");
  // Close 10 more stream, skipping the assigned_lb_id. The assignment of
  // orphaned_lb_id shouldn't change.
  for (size_t _ = 0; _ < 10; ++_) {
    std::string lb_id_to_close = "";
    for (const auto& lb_id : active_lb_ids) {
      if (lb_id != assigned_lb_id) {
        lb_id_to_close = lb_id;
        break;
      }
    }
    EXPECT_STRNE(lb_id_to_close.c_str(), "");
    load_data_store.ReportStreamClosed(kHostname1, lb_id_to_close);
    active_lb_ids.erase(lb_id_to_close);
    EXPECT_TRUE(PerBalancerStoresContains(
        load_data_store,
        load_data_store.GetAssignedStores(kHostname1, assigned_lb_id),
        kHostname1, orphaned_lb_id, kLoadKey1));
  }
  // Close the assigned_lb_id, orphaned_lb_id will be re-assigned again.
  load_data_store.ReportStreamClosed(kHostname1, assigned_lb_id);
  active_lb_ids.erase(assigned_lb_id);
  size_t orphaned_lb_id_occurences = 0;
  for (const auto& lb_id : active_lb_ids) {
    if (PerBalancerStoresContains(
            load_data_store,
            load_data_store.GetAssignedStores(kHostname1, lb_id), kHostname1,
            orphaned_lb_id, kLoadKey1)) {
      orphaned_lb_id_occurences++;
    }
  }
  EXPECT_EQ(orphaned_lb_id_occurences, 1U);
}

TEST_F(LoadDataStoreTest, HostTemporarilyLoseAllStreams) {
  LoadDataStore load_data_store;
  load_data_store.ReportStreamCreated(kHostname1, kLbId1, kLoadKey1);
  load_data_store.ReportStreamCreated(kHostname2, kLbId2, kLoadKey1);
  auto store_lb_id_1 = load_data_store.FindPerBalancerStore(kHostname1, kLbId1);
  auto store_invalid_lb_id_1 =
      load_data_store.FindPerBalancerStore(kHostname1, kInvalidLbId);
  EXPECT_FALSE(store_lb_id_1->IsSuspended());
  EXPECT_FALSE(store_invalid_lb_id_1->IsSuspended());
  // Disconnect all the streams of the first host.
  load_data_store.ReportStreamClosed(kHostname1, kLbId1);
  // All the streams of that host are suspended.
  EXPECT_TRUE(store_lb_id_1->IsSuspended());
  EXPECT_TRUE(store_invalid_lb_id_1->IsSuspended());
  // Detailed load data won't be kept when the PerBalancerStore is suspended.
  store_lb_id_1->MergeRow(kKey1, LoadRecordValue());
  store_invalid_lb_id_1->MergeRow(kKey1, LoadRecordValue());
  EXPECT_EQ(store_lb_id_1->load_record_map().size(), 0U);
  EXPECT_EQ(store_invalid_lb_id_1->load_record_map().size(), 0U);
  // The stores for different hosts won't mix, even if the load key is the same.
  auto assigned_to_lb_id_2 =
      load_data_store.GetAssignedStores(kHostname2, kLbId2);
  EXPECT_EQ(assigned_to_lb_id_2->size(), 2U);
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_2,
                                        kHostname2, kLbId2, kLoadKey1));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_2,
                                        kHostname2, kInvalidLbId, ""));
  // A new stream is created for the first host.
  load_data_store.ReportStreamCreated(kHostname1, kLbId3, kLoadKey2);
  // The stores for the first host are resumed.
  EXPECT_FALSE(store_lb_id_1->IsSuspended());
  EXPECT_FALSE(store_invalid_lb_id_1->IsSuspended());
  store_lb_id_1->MergeRow(kKey1, LoadRecordValue());
  store_invalid_lb_id_1->MergeRow(kKey1, LoadRecordValue());
  EXPECT_EQ(store_lb_id_1->load_record_map().size(), 1U);
  EXPECT_EQ(store_invalid_lb_id_1->load_record_map().size(), 1U);
  // The resumed stores are assigned to the new LB.
  auto assigned_to_lb_id_3 =
      load_data_store.GetAssignedStores(kHostname1, kLbId3);
  EXPECT_EQ(assigned_to_lb_id_3->size(), 3U);
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        kHostname1, kLbId1, kLoadKey1));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        kHostname1, kInvalidLbId, ""));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        kHostname1, kLbId3, kLoadKey2));
}

TEST_F(LoadDataStoreTest, OneStorePerLbId) {
  LoadDataStore load_data_store;
  EXPECT_EQ(load_data_store.FindPerBalancerStore(kHostname1, kLbId1), nullptr);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(kHostname1, kInvalidLbId),
            nullptr);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(kHostname2, kLbId2), nullptr);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(kHostname2, kLbId3), nullptr);
  // Create The first stream.
  load_data_store.ReportStreamCreated(kHostname1, kLbId1, kLoadKey1);
  auto store_lb_id_1 = load_data_store.FindPerBalancerStore(kHostname1, kLbId1);
  auto store_invalid_lb_id_1 =
      load_data_store.FindPerBalancerStore(kHostname1, kInvalidLbId);
  // Two stores will be created: one is for the stream; the other one is for
  // kInvalidLbId.
  EXPECT_NE(store_lb_id_1, nullptr);
  EXPECT_NE(store_invalid_lb_id_1, nullptr);
  EXPECT_NE(store_lb_id_1, store_invalid_lb_id_1);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(kHostname2, kLbId2), nullptr);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(kHostname2, kLbId3), nullptr);
  // Create the second stream.
  load_data_store.ReportStreamCreated(kHostname2, kLbId3, kLoadKey1);
  auto store_lb_id_3 = load_data_store.FindPerBalancerStore(kHostname2, kLbId3);
  auto store_invalid_lb_id_2 =
      load_data_store.FindPerBalancerStore(kHostname2, kInvalidLbId);
  EXPECT_NE(store_lb_id_3, nullptr);
  EXPECT_NE(store_invalid_lb_id_2, nullptr);
  EXPECT_NE(store_lb_id_3, store_invalid_lb_id_2);
  // The PerBalancerStores created for different hosts are independent.
  EXPECT_NE(store_lb_id_3, store_invalid_lb_id_1);
  EXPECT_NE(store_invalid_lb_id_2, store_invalid_lb_id_1);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(kHostname2, kLbId2), nullptr);
}

TEST_F(LoadDataStoreTest, ExactlyOnceAssignment) {
  LoadDataStore load_data_store;
  size_t num_create = 100;
  size_t num_close = 50;
  for (size_t i = 0; i < num_create; ++i) {
    load_data_store.ReportStreamCreated(kHostname1, FormatLbId(i), kLoadKey1);
  }
  for (size_t i = 0; i < num_close; ++i) {
    load_data_store.ReportStreamClosed(kHostname1, FormatLbId(i));
  }
  std::set<std::string> reported_lb_ids;
  for (size_t i = num_close; i < num_create; ++i) {
    for (auto assigned_store :
         *load_data_store.GetAssignedStores(kHostname1, FormatLbId(i))) {
      EXPECT_TRUE(reported_lb_ids.insert(assigned_store->lb_id()).second);
    }
  }
  // Add one for kInvalidLbId.
  EXPECT_EQ(reported_lb_ids.size(), (num_create + 1));
  EXPECT_NE(reported_lb_ids.find(kInvalidLbId), reported_lb_ids.end());
}

TEST_F(LoadDataStoreTest, UnknownBalancerIdTracking) {
  LoadDataStore load_data_store;
  load_data_store.ReportStreamCreated(kHostname1, kLbId1, kLoadKey1);
  // Merge data for a known LB ID.
  LoadRecordValue v1(192);
  load_data_store.MergeRow(kHostname1, kKey1, v1);
  // Merge data for unknown LB ID.
  LoadRecordValue v2(23);
  EXPECT_FALSE(load_data_store.IsTrackedUnknownBalancerId(kLbId2));
  load_data_store.MergeRow(
      kHostname1, LoadRecordKey(kLbId2, kLbTag1, kUser1, kClientIp1), v2);
  EXPECT_TRUE(load_data_store.IsTrackedUnknownBalancerId(kLbId2));
  LoadRecordValue v3(952);
  load_data_store.MergeRow(
      kHostname2, LoadRecordKey(kLbId3, kLbTag1, kUser1, kClientIp1), v3);
  EXPECT_TRUE(load_data_store.IsTrackedUnknownBalancerId(kLbId3));
  // The data kept for a known LB ID is correct.
  auto store_lb_id_1 = load_data_store.FindPerBalancerStore(kHostname1, kLbId1);
  EXPECT_EQ(store_lb_id_1->load_record_map().size(), 1U);
  EXPECT_EQ(store_lb_id_1->load_record_map().find(kKey1)->second.start_count(),
            v1.start_count());
  EXPECT_EQ(store_lb_id_1->GetNumCallsInProgressForReport(), v1.start_count());
  // No PerBalancerStore created for Unknown LB ID.
  EXPECT_EQ(load_data_store.FindPerBalancerStore(kHostname1, kLbId2), nullptr);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(kHostname2, kLbId3), nullptr);
  // End all the started RPCs for kLbId1.
  LoadRecordValue v4(0, v1.start_count());
  load_data_store.MergeRow(kHostname1, kKey1, v4);
  EXPECT_EQ(store_lb_id_1->load_record_map().size(), 1U);
  EXPECT_EQ(store_lb_id_1->load_record_map().find(kKey1)->second.start_count(),
            v1.start_count());
  EXPECT_EQ(store_lb_id_1->load_record_map().find(kKey1)->second.ok_count(),
            v4.ok_count());
  EXPECT_EQ(store_lb_id_1->GetNumCallsInProgressForReport(), 0U);
  EXPECT_FALSE(load_data_store.IsTrackedUnknownBalancerId(kLbId1));
  // End all the started RPCs for kLbId2.
  LoadRecordValue v5(0, v2.start_count());
  load_data_store.MergeRow(
      kHostname1, LoadRecordKey(kLbId2, kLbTag1, kUser1, kClientIp1), v5);
  EXPECT_FALSE(load_data_store.IsTrackedUnknownBalancerId(kLbId2));
  // End some of the started RPCs for kLbId3.
  LoadRecordValue v6(0, v3.start_count() / 2);
  load_data_store.MergeRow(
      kHostname2, LoadRecordKey(kLbId3, kLbTag1, kUser1, kClientIp1), v6);
  EXPECT_TRUE(load_data_store.IsTrackedUnknownBalancerId(kLbId3));
}

TEST_F(PerBalancerStoreTest, Suspend) {
  PerBalancerStore per_balancer_store(kLbId1, kLoadKey1);
  EXPECT_FALSE(per_balancer_store.IsSuspended());
  // Suspend the store.
  per_balancer_store.Suspend();
  EXPECT_TRUE(per_balancer_store.IsSuspended());
  EXPECT_EQ(0U, per_balancer_store.load_record_map().size());
  // Data merged when the store is suspended won't be kept.
  LoadRecordValue v1(139, 19);
  per_balancer_store.MergeRow(kKey1, v1);
  EXPECT_EQ(0U, per_balancer_store.load_record_map().size());
  // Resume the store.
  per_balancer_store.Resume();
  EXPECT_FALSE(per_balancer_store.IsSuspended());
  EXPECT_EQ(0U, per_balancer_store.load_record_map().size());
  // Data merged after the store is resumed will be kept.
  LoadRecordValue v2(23, 0, 51);
  per_balancer_store.MergeRow(kKey1, v2);
  EXPECT_EQ(1U, per_balancer_store.load_record_map().size());
  // Suspend the store.
  per_balancer_store.Suspend();
  EXPECT_TRUE(per_balancer_store.IsSuspended());
  EXPECT_EQ(0U, per_balancer_store.load_record_map().size());
  // Data merged when the store is suspended won't be kept.
  LoadRecordValue v3(62, 11);
  per_balancer_store.MergeRow(kKey1, v3);
  EXPECT_EQ(0U, per_balancer_store.load_record_map().size());
  // Resume the store.
  per_balancer_store.Resume();
  EXPECT_FALSE(per_balancer_store.IsSuspended());
  EXPECT_EQ(0U, per_balancer_store.load_record_map().size());
  // Data merged after the store is resumed will be kept.
  LoadRecordValue v4(225, 98);
  per_balancer_store.MergeRow(kKey1, v4);
  EXPECT_EQ(1U, per_balancer_store.load_record_map().size());
  // In-progress count is always kept.
  EXPECT_EQ(per_balancer_store.GetNumCallsInProgressForReport(),
            v1.start_count() - v1.ok_count() + v2.start_count() -
                v2.error_count() + v3.start_count() - v3.ok_count() +
                v4.start_count() - v4.ok_count());
}

TEST_F(PerBalancerStoreTest, DataAggregation) {
  PerBalancerStore per_balancer_store(kLbId1, kLoadKey1);
  // Construct some Values.
  LoadRecordValue v1(992, 34, 13, 234, 164, 173467);
  v1.InsertCallMetric(kMetric1, CallMetricValue(3, 2773.2));
  LoadRecordValue v2(4842, 213, 9, 393, 974, 1345);
  v2.InsertCallMetric(kMetric1, CallMetricValue(7, 25.234));
  v2.InsertCallMetric(kMetric2, CallMetricValue(2, 387.08));
  // v3 doesn't change the number of in-progress RPCs.
  LoadRecordValue v3(293, 55, 293 - 55, 28764, 5284, 5772);
  v3.InsertCallMetric(kMetric1, CallMetricValue(61, 3465.0));
  v3.InsertCallMetric(kMetric2, CallMetricValue(13, 672.0));
  // The initial state of the store.
  uint64_t num_calls_in_progress = 0;
  EXPECT_FALSE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  EXPECT_EQ(per_balancer_store.GetNumCallsInProgressForReport(),
            num_calls_in_progress);
  // Merge v1 and get report of the number of in-progress calls.
  per_balancer_store.MergeRow(kKey1, v1);
  EXPECT_TRUE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  EXPECT_EQ(per_balancer_store.GetNumCallsInProgressForReport(),
            num_calls_in_progress +=
            (v1.start_count() - v1.ok_count() - v1.error_count()));
  EXPECT_FALSE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  // Merge v2 and get report of the number of in-progress calls.
  per_balancer_store.MergeRow(kKey2, v2);
  EXPECT_TRUE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  EXPECT_EQ(per_balancer_store.GetNumCallsInProgressForReport(),
            num_calls_in_progress +=
            (v2.start_count() - v2.ok_count() - v2.error_count()));
  EXPECT_FALSE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  // Merge v3 and get report of the number of in-progress calls.
  per_balancer_store.MergeRow(kKey1, v3);
  EXPECT_FALSE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  EXPECT_EQ(per_balancer_store.GetNumCallsInProgressForReport(),
            num_calls_in_progress);
  // LoadRecordValue for kKey1 is aggregated correctly.
  LoadRecordValue value_for_key1 =
      per_balancer_store.load_record_map().find(kKey1)->second;
  EXPECT_EQ(value_for_key1.start_count(), v1.start_count() + v3.start_count());
  EXPECT_EQ(value_for_key1.ok_count(), v1.ok_count() + v3.ok_count());
  EXPECT_EQ(value_for_key1.error_count(), v1.error_count() + v3.error_count());
  EXPECT_EQ(value_for_key1.bytes_sent(), v1.bytes_sent() + v3.bytes_sent());
  EXPECT_EQ(value_for_key1.bytes_recv(), v1.bytes_recv() + v3.bytes_recv());
  EXPECT_EQ(value_for_key1.latency_ms(), v1.latency_ms() + v3.latency_ms());
  EXPECT_EQ(value_for_key1.call_metrics().size(), 2U);
  EXPECT_EQ(value_for_key1.call_metrics().find(kMetric1)->second.num_calls(),
            v1.call_metrics().find(kMetric1)->second.num_calls() +
                v3.call_metrics().find(kMetric1)->second.num_calls());
  EXPECT_EQ(
      value_for_key1.call_metrics().find(kMetric1)->second.total_metric_value(),
      v1.call_metrics().find(kMetric1)->second.total_metric_value() +
          v3.call_metrics().find(kMetric1)->second.total_metric_value());
  EXPECT_EQ(value_for_key1.call_metrics().find(kMetric2)->second.num_calls(),
            v3.call_metrics().find(kMetric2)->second.num_calls());
  EXPECT_EQ(
      value_for_key1.call_metrics().find(kMetric2)->second.total_metric_value(),
      v3.call_metrics().find(kMetric2)->second.total_metric_value());
  // LoadRecordValue for kKey2 is aggregated (trivially) correctly.
  LoadRecordValue value_for_key2 =
      per_balancer_store.load_record_map().find(kKey2)->second;
  EXPECT_EQ(value_for_key2.start_count(), v2.start_count());
  EXPECT_EQ(value_for_key2.ok_count(), v2.ok_count());
  EXPECT_EQ(value_for_key2.error_count(), v2.error_count());
  EXPECT_EQ(value_for_key2.bytes_sent(), v2.bytes_sent());
  EXPECT_EQ(value_for_key2.bytes_recv(), v2.bytes_recv());
  EXPECT_EQ(value_for_key2.latency_ms(), v2.latency_ms());
  EXPECT_EQ(value_for_key2.call_metrics().size(), 2U);
  EXPECT_EQ(value_for_key2.call_metrics().find(kMetric1)->second.num_calls(),
            v2.call_metrics().find(kMetric1)->second.num_calls());
  EXPECT_EQ(
      value_for_key2.call_metrics().find(kMetric1)->second.total_metric_value(),
      v2.call_metrics().find(kMetric1)->second.total_metric_value());
  EXPECT_EQ(value_for_key2.call_metrics().find(kMetric2)->second.num_calls(),
            v2.call_metrics().find(kMetric2)->second.num_calls());
  EXPECT_EQ(
      value_for_key2.call_metrics().find(kMetric2)->second.total_metric_value(),
      v2.call_metrics().find(kMetric2)->second.total_metric_value());
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
