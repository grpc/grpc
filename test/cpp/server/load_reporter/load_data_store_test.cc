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
#include "src/cpp/server/load_reporter/load_reporter.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

using namespace ::grpc::load_reporter;

const grpc::string HOSTNAME_1 = "HOSTNAME_1";
const grpc::string HOSTNAME_2 = "HOSTNAME_2";
const grpc::string LB_ID_1 = "LB_ID_1";
const grpc::string LB_ID_2 = "LB_ID_2";
const grpc::string LB_ID_3 = "LB_ID_3";
const grpc::string LB_ID_4 = "LB_ID_4";
const grpc::string LOAD_KEY_1 = "LOAD_KEY_1";
const grpc::string LOAD_KEY_2 = "LOAD_KEY_2";
const grpc::string LB_TAG_1 = "LB_TAG_1";
const grpc::string LB_TAG_2 = "LB_TAG_2";
const grpc::string USER_1 = "USER_1";
const grpc::string USER_2 = "USER_2";
const grpc::string CLIENT_IP_1 = "00";
const grpc::string CLIENT_IP_2 = "02";
const LoadRecordKey KEY_1(LB_ID_1, LB_TAG_1, USER_1, CLIENT_IP_1);
const LoadRecordKey KEY_2(LB_ID_2, LB_TAG_2, USER_2, CLIENT_IP_2);
const grpc::string METRIC_1 = "METRIC_1";
const grpc::string METRIC_2 = "METRIC_2";

// Check whether per_balancer_stores contains a store which was originally
// created for <hostname, lb_id, and load_key>.
bool PerBalancerStoresContains(
    const LoadDataStore& load_data_store,
    const std::vector<PerBalancerStore*>& per_balancer_stores,
    const grpc::string hostname, const grpc::string lb_id,
    const grpc::string load_key) {
  auto original_per_balancer_store =
      load_data_store.FindPerBalancerStore(hostname, lb_id);
  EXPECT_NE(original_per_balancer_store, nullptr);
  EXPECT_EQ(original_per_balancer_store->lb_id(), lb_id);
  EXPECT_EQ(original_per_balancer_store->load_key(), load_key);
  for (auto per_balancer_store : per_balancer_stores) {
    if (per_balancer_store == original_per_balancer_store) {
      return true;
    }
  }
  return false;
}

grpc::string FormatLbId(size_t index) {
  return "LB_ID_" + std::to_string(index);
}

TEST(LoadDataStoreTest, AssignToSelf) {
  LoadDataStore load_data_store;
  load_data_store.ReportStreamCreated(HOSTNAME_1, LB_ID_1, LOAD_KEY_1);
  auto assigned_stores = load_data_store.GetAssignedStores(HOSTNAME_1, LB_ID_1);
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_stores,
                                        HOSTNAME_1, LB_ID_1, LOAD_KEY_1));
}

TEST(LoadDataStoreTest, ReassignOrphanStores) {
  LoadDataStore load_data_store;
  load_data_store.ReportStreamCreated(HOSTNAME_1, LB_ID_1, LOAD_KEY_1);
  load_data_store.ReportStreamCreated(HOSTNAME_1, LB_ID_2, LOAD_KEY_1);
  load_data_store.ReportStreamCreated(HOSTNAME_1, LB_ID_3, LOAD_KEY_2);
  load_data_store.ReportStreamCreated(HOSTNAME_2, LB_ID_4, LOAD_KEY_1);
  // 1. Close the second stream.
  load_data_store.ReportStreamClosed(HOSTNAME_1, LB_ID_2);
  auto assigned_to_lb_id_1 =
      load_data_store.GetAssignedStores(HOSTNAME_1, LB_ID_1);
  // The orphaned store is re-assigned to LB_ID_1 with the same load key.
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_1,
                                        HOSTNAME_1, LB_ID_1, LOAD_KEY_1));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_1,
                                        HOSTNAME_1, LB_ID_2, LOAD_KEY_1));
  // 2. Close the first stream.
  load_data_store.ReportStreamClosed(HOSTNAME_1, LB_ID_1);
  auto assigned_to_lb_id_3 =
      load_data_store.GetAssignedStores(HOSTNAME_1, LB_ID_3);
  // The orphaned stores are re-assigned to LB_ID_3 with the same host,
  // because there isn't any LB with the same load key.
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        HOSTNAME_1, LB_ID_1, LOAD_KEY_1));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        HOSTNAME_1, LB_ID_2, LOAD_KEY_1));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        HOSTNAME_1, LB_ID_3, LOAD_KEY_2));
  // 3. Close the third stream.
  load_data_store.ReportStreamClosed(HOSTNAME_1, LB_ID_3);
  auto assigned_to_lb_id_4 =
      load_data_store.GetAssignedStores(HOSTNAME_2, LB_ID_4);
  // There is no active LB for the first host now. LB_ID_4 is active but
  // it's for the second host, so it wll NOT adopt the orphaned stores.
  EXPECT_FALSE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_4,
                                         HOSTNAME_1, LB_ID_1, LOAD_KEY_1));
  EXPECT_FALSE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_4,
                                         HOSTNAME_1, LB_ID_2, LOAD_KEY_1));
  EXPECT_FALSE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_4,
                                         HOSTNAME_1, LB_ID_3, LOAD_KEY_2));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_4,
                                        HOSTNAME_2, LB_ID_4, LOAD_KEY_1));
}

TEST(LoadDataStoreTest, OrphanAssignmentIsSticky) {
  LoadDataStore load_data_store;
  std::set<grpc::string> active_lb_ids;
  size_t num_lb_ids = 1000;
  for (size_t i = 0; i < num_lb_ids; ++i) {
    load_data_store.ReportStreamCreated(HOSTNAME_1, FormatLbId(i), LOAD_KEY_1);
    active_lb_ids.insert(FormatLbId(i));
  }
  grpc::string orphaned_lb_id = FormatLbId(std::rand() % num_lb_ids);
  load_data_store.ReportStreamClosed(HOSTNAME_1, orphaned_lb_id);
  active_lb_ids.erase(orphaned_lb_id);
  // Find which LB is assigned the orphaned store.
  grpc::string assigned_lb_id = "";
  for (auto lb_id : active_lb_ids) {
    if (PerBalancerStoresContains(
            load_data_store,
            load_data_store.GetAssignedStores(HOSTNAME_1, lb_id), HOSTNAME_1,
            orphaned_lb_id, LOAD_KEY_1)) {
      assigned_lb_id = lb_id;
      break;
    }
  }
  EXPECT_STRNE(assigned_lb_id.c_str(), "");
  // Close 10 more stream, skipping the assigned_lb_id. The assignment of
  // orphaned_lb_id shouldn't change.
  for (size_t _ = 0; _ < 10; ++_) {
    grpc::string lb_id_to_close = "";
    for (auto lb_id : active_lb_ids) {
      if (lb_id != assigned_lb_id) {
        lb_id_to_close = lb_id;
        break;
      }
    }
    EXPECT_STRNE(lb_id_to_close.c_str(), "");
    load_data_store.ReportStreamClosed(HOSTNAME_1, lb_id_to_close);
    active_lb_ids.erase(lb_id_to_close);
    EXPECT_TRUE(PerBalancerStoresContains(
        load_data_store,
        load_data_store.GetAssignedStores(HOSTNAME_1, assigned_lb_id),
        HOSTNAME_1, orphaned_lb_id, LOAD_KEY_1));
  }
  // Close the assigned_lb_id, orphaned_lb_id will be re-assigned again.
  load_data_store.ReportStreamClosed(HOSTNAME_1, assigned_lb_id);
  active_lb_ids.erase(assigned_lb_id);
  size_t orphaned_lb_id_occurences = 0;
  for (auto lb_id : active_lb_ids) {
    if (PerBalancerStoresContains(
            load_data_store,
            load_data_store.GetAssignedStores(HOSTNAME_1, lb_id), HOSTNAME_1,
            orphaned_lb_id, LOAD_KEY_1)) {
      orphaned_lb_id_occurences++;
    }
  }
  EXPECT_EQ(orphaned_lb_id_occurences, 1U);
}

TEST(LoadDataStoreTest, HostTemporarilyLoseAllStreams) {
  LoadDataStore load_data_store;
  load_data_store.ReportStreamCreated(HOSTNAME_1, LB_ID_1, LOAD_KEY_1);
  load_data_store.ReportStreamCreated(HOSTNAME_2, LB_ID_2, LOAD_KEY_1);
  auto store_lb_id_1 =
      load_data_store.FindPerBalancerStore(HOSTNAME_1, LB_ID_1);
  auto store_invalid_lb_id_1 =
      load_data_store.FindPerBalancerStore(HOSTNAME_1, INVALID_LBID);
  EXPECT_FALSE(store_lb_id_1->IsSuspended());
  EXPECT_FALSE(store_invalid_lb_id_1->IsSuspended());
  // Disconnect all the streams of the first host.
  load_data_store.ReportStreamClosed(HOSTNAME_1, LB_ID_1);
  // All the streams of that host are suspended.
  EXPECT_TRUE(store_lb_id_1->IsSuspended());
  EXPECT_TRUE(store_invalid_lb_id_1->IsSuspended());
  // Detailed load data won't be kept when the PerBalancerStore is suspended.
  store_lb_id_1->MergeRow(KEY_1, LoadRecordValue());
  store_invalid_lb_id_1->MergeRow(KEY_1, LoadRecordValue());
  EXPECT_EQ(store_lb_id_1->container().size(), 0U);
  EXPECT_EQ(store_invalid_lb_id_1->container().size(), 0U);
  // The stores for different hosts won't mix, even if the load key is the same.
  auto assigned_to_lb_id_2 =
      load_data_store.GetAssignedStores(HOSTNAME_2, LB_ID_2);
  EXPECT_EQ(assigned_to_lb_id_2.size(), 2U);
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_2,
                                        HOSTNAME_2, LB_ID_2, LOAD_KEY_1));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_2,
                                        HOSTNAME_2, INVALID_LBID, ""));
  // A new stream is created for the first host.
  load_data_store.ReportStreamCreated(HOSTNAME_1, LB_ID_3, LOAD_KEY_2);
  // The stores for the first host are resumed.
  EXPECT_FALSE(store_lb_id_1->IsSuspended());
  EXPECT_FALSE(store_invalid_lb_id_1->IsSuspended());
  store_lb_id_1->MergeRow(KEY_1, LoadRecordValue());
  store_invalid_lb_id_1->MergeRow(KEY_1, LoadRecordValue());
  EXPECT_EQ(store_lb_id_1->container().size(), 1U);
  EXPECT_EQ(store_invalid_lb_id_1->container().size(), 1U);
  // The resumed stores are assigned to the new LB.
  auto assigned_to_lb_id_3 =
      load_data_store.GetAssignedStores(HOSTNAME_1, LB_ID_3);
  EXPECT_EQ(assigned_to_lb_id_3.size(), 3U);
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        HOSTNAME_1, LB_ID_1, LOAD_KEY_1));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        HOSTNAME_1, INVALID_LBID, ""));
  EXPECT_TRUE(PerBalancerStoresContains(load_data_store, assigned_to_lb_id_3,
                                        HOSTNAME_1, LB_ID_3, LOAD_KEY_2));
}

TEST(LoadDataStoreTest, OneStorePerLbId) {
  LoadDataStore load_data_store;
  EXPECT_EQ(load_data_store.FindPerBalancerStore(HOSTNAME_1, LB_ID_1), nullptr);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(HOSTNAME_1, INVALID_LBID),
            nullptr);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(HOSTNAME_2, LB_ID_2), nullptr);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(HOSTNAME_2, LB_ID_3), nullptr);
  // Create The first stream.
  load_data_store.ReportStreamCreated(HOSTNAME_1, LB_ID_1, LOAD_KEY_1);
  auto store_lb_id_1 =
      load_data_store.FindPerBalancerStore(HOSTNAME_1, LB_ID_1);
  auto store_invalid_lb_id_1 =
      load_data_store.FindPerBalancerStore(HOSTNAME_1, INVALID_LBID);
  // Two stores will be created: one is for the stream; the other one is for
  // INVALID_LBID.
  EXPECT_NE(store_lb_id_1, nullptr);
  EXPECT_NE(store_invalid_lb_id_1, nullptr);
  EXPECT_NE(store_lb_id_1, store_invalid_lb_id_1);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(HOSTNAME_2, LB_ID_2), nullptr);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(HOSTNAME_2, LB_ID_3), nullptr);
  // Create the second stream.
  load_data_store.ReportStreamCreated(HOSTNAME_2, LB_ID_3, LOAD_KEY_1);
  auto store_lb_id_3 =
      load_data_store.FindPerBalancerStore(HOSTNAME_2, LB_ID_3);
  auto store_invalid_lb_id_2 =
      load_data_store.FindPerBalancerStore(HOSTNAME_2, INVALID_LBID);
  EXPECT_NE(store_lb_id_3, nullptr);
  EXPECT_NE(store_invalid_lb_id_2, nullptr);
  EXPECT_NE(store_lb_id_3, store_invalid_lb_id_2);
  // The PerBalancerStores created for different hosts are independent.
  EXPECT_NE(store_lb_id_3, store_invalid_lb_id_1);
  EXPECT_NE(store_invalid_lb_id_2, store_invalid_lb_id_1);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(HOSTNAME_2, LB_ID_2), nullptr);
}

TEST(LoadDataStoreTest, ExactlyOnceAssignment) {
  LoadDataStore load_data_store;
  size_t num_create = 100;
  size_t num_close = 50;
  for (size_t i = 0; i < num_create; ++i) {
    load_data_store.ReportStreamCreated(HOSTNAME_1, FormatLbId(i), LOAD_KEY_1);
  }
  for (size_t i = 0; i < num_close; ++i) {
    load_data_store.ReportStreamClosed(HOSTNAME_1, FormatLbId(i));
  }
  std::set<grpc::string> reported_lb_ids;
  for (size_t i = num_close; i < num_create; ++i) {
    for (auto assigned_store :
         load_data_store.GetAssignedStores(HOSTNAME_1, FormatLbId(i))) {
      EXPECT_TRUE(reported_lb_ids.insert(assigned_store->lb_id()).second);
    }
  }
  // Add one for INVALID_LBID.
  EXPECT_EQ(reported_lb_ids.size(), (num_create + 1));
  EXPECT_NE(reported_lb_ids.find(INVALID_LBID), reported_lb_ids.end());
}

TEST(LoadDataStoreTest, UnknownBalancerIdTracking) {
  LoadDataStore load_data_store;
  load_data_store.ReportStreamCreated(HOSTNAME_1, LB_ID_1, LOAD_KEY_1);
  // Merge data for a known LB ID.
  LoadRecordValue v1(192);
  load_data_store.MergeRow(HOSTNAME_1, KEY_1, v1);
  // Merge data for unknown LB ID.
  LoadRecordValue v2(23);
  EXPECT_FALSE(load_data_store.IsTrackedUnknownBalancerId(LB_ID_2));
  load_data_store.MergeRow(
      HOSTNAME_1, LoadRecordKey(LB_ID_2, LB_TAG_1, USER_1, CLIENT_IP_1), v2);
  EXPECT_TRUE(load_data_store.IsTrackedUnknownBalancerId(LB_ID_2));
  LoadRecordValue v3(952);
  load_data_store.MergeRow(
      HOSTNAME_2, LoadRecordKey(LB_ID_3, LB_TAG_1, USER_1, CLIENT_IP_1), v3);
  EXPECT_TRUE(load_data_store.IsTrackedUnknownBalancerId(LB_ID_3));
  // The data kept for a known LB ID is correct.
  auto store_lb_id_1 =
      load_data_store.FindPerBalancerStore(HOSTNAME_1, LB_ID_1);
  EXPECT_EQ(store_lb_id_1->container().size(), 1U);
  EXPECT_EQ(store_lb_id_1->container().find(KEY_1)->second.start_count(),
            v1.start_count());
  EXPECT_EQ(store_lb_id_1->GetNumCallsInProgressForReport(), v1.start_count());
  // No PerBalancerStore created for Unknown LB ID.
  EXPECT_EQ(load_data_store.FindPerBalancerStore(HOSTNAME_1, LB_ID_2), nullptr);
  EXPECT_EQ(load_data_store.FindPerBalancerStore(HOSTNAME_2, LB_ID_3), nullptr);
  // End all the started RPCs for LB_ID_1.
  LoadRecordValue v4(0, v1.start_count());
  load_data_store.MergeRow(HOSTNAME_1, KEY_1, v4);
  EXPECT_EQ(store_lb_id_1->container().size(), 1U);
  EXPECT_EQ(store_lb_id_1->container().find(KEY_1)->second.start_count(),
            v1.start_count());
  EXPECT_EQ(store_lb_id_1->container().find(KEY_1)->second.ok_count(),
            v4.ok_count());
  EXPECT_EQ(store_lb_id_1->GetNumCallsInProgressForReport(), 0U);
  EXPECT_FALSE(load_data_store.IsTrackedUnknownBalancerId(LB_ID_1));
  // End all the started RPCs for LB_ID_2.
  LoadRecordValue v5(0, v2.start_count());
  load_data_store.MergeRow(
      HOSTNAME_1, LoadRecordKey(LB_ID_2, LB_TAG_1, USER_1, CLIENT_IP_1), v5);
  EXPECT_FALSE(load_data_store.IsTrackedUnknownBalancerId(LB_ID_2));
  // End some of the started RPCs for LB_ID_3.
  LoadRecordValue v6(0, v3.start_count() / 2);
  load_data_store.MergeRow(
      HOSTNAME_2, LoadRecordKey(LB_ID_3, LB_TAG_1, USER_1, CLIENT_IP_1), v6);
  EXPECT_TRUE(load_data_store.IsTrackedUnknownBalancerId(LB_ID_3));
}

TEST(PerBalancerStoreTest, Suspend) {
  PerBalancerStore per_balancer_store(LB_ID_1, LOAD_KEY_1);
  EXPECT_FALSE(per_balancer_store.IsSuspended());
  // Suspend the store.
  per_balancer_store.Suspend();
  EXPECT_TRUE(per_balancer_store.IsSuspended());
  EXPECT_EQ(0U, per_balancer_store.container().size());
  // Data merged when the store is suspended won't be kept.
  LoadRecordValue v1(139, 19);
  per_balancer_store.MergeRow(KEY_1, v1);
  EXPECT_EQ(0U, per_balancer_store.container().size());
  // Resume the store.
  per_balancer_store.Resume();
  EXPECT_FALSE(per_balancer_store.IsSuspended());
  EXPECT_EQ(0U, per_balancer_store.container().size());
  // Data merged after the store is resumed will be kept.
  LoadRecordValue v2(23, 0, 51);
  per_balancer_store.MergeRow(KEY_1, v2);
  EXPECT_EQ(1U, per_balancer_store.container().size());
  // Suspend the store.
  per_balancer_store.Suspend();
  EXPECT_TRUE(per_balancer_store.IsSuspended());
  EXPECT_EQ(0U, per_balancer_store.container().size());
  // Data merged when the store is suspended won't be kept.
  LoadRecordValue v3(62, 11);
  per_balancer_store.MergeRow(KEY_1, v3);
  EXPECT_EQ(0U, per_balancer_store.container().size());
  // Resume the store.
  per_balancer_store.Resume();
  EXPECT_FALSE(per_balancer_store.IsSuspended());
  EXPECT_EQ(0U, per_balancer_store.container().size());
  // Data merged after the store is resumed will be kept.
  LoadRecordValue v4(225, 98);
  per_balancer_store.MergeRow(KEY_1, v4);
  EXPECT_EQ(1U, per_balancer_store.container().size());
  // In-progress count is always kept.
  EXPECT_EQ(per_balancer_store.GetNumCallsInProgressForReport(),
            v1.start_count() - v1.ok_count() + v2.start_count() -
                v2.error_count() + v3.start_count() - v3.ok_count() +
                v4.start_count() - v4.ok_count());
}

TEST(PerBalancerStoreTest, DataAggregation) {
  PerBalancerStore per_balancer_store(LB_ID_1, LOAD_KEY_1);
  // Construct some Values.
  LoadRecordValue v1(992, 34, 13, 234.0, 164.0, 173467.38);
  v1.InsertCallMetric(METRIC_1, CallMetricValue(3, 2773.2));
  LoadRecordValue v2(4842, 213, 9, 393.0, 974.0, 1345.2398);
  v2.InsertCallMetric(METRIC_1, CallMetricValue(7, 25.234));
  v2.InsertCallMetric(METRIC_2, CallMetricValue(2, 387.08));
  // v3 doesn't change the number of in-progress RPCs.
  LoadRecordValue v3(293, 55, 293 - 55, 28764, 5284, 5772);
  v3.InsertCallMetric(METRIC_1, CallMetricValue(61, 3465.0));
  v3.InsertCallMetric(METRIC_2, CallMetricValue(13, 672.0));
  // The initial state of the store.
  uint64_t num_calls_in_progress = 0;
  EXPECT_FALSE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  EXPECT_EQ(per_balancer_store.GetNumCallsInProgressForReport(),
            num_calls_in_progress);
  // Merge v1 and get report of the number of in-progress calls.
  per_balancer_store.MergeRow(KEY_1, v1);
  EXPECT_TRUE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  EXPECT_EQ(per_balancer_store.GetNumCallsInProgressForReport(),
            num_calls_in_progress +=
            (v1.start_count() - v1.ok_count() - v1.error_count()));
  EXPECT_FALSE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  // Merge v2 and get report of the number of in-progress calls.
  per_balancer_store.MergeRow(KEY_2, v2);
  EXPECT_TRUE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  EXPECT_EQ(per_balancer_store.GetNumCallsInProgressForReport(),
            num_calls_in_progress +=
            (v2.start_count() - v2.ok_count() - v2.error_count()));
  EXPECT_FALSE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  // Merge v3 and get report of the number of in-progress calls.
  per_balancer_store.MergeRow(KEY_1, v3);
  EXPECT_FALSE(per_balancer_store.IsNumCallsInProgressChangedSinceLastReport());
  EXPECT_EQ(per_balancer_store.GetNumCallsInProgressForReport(),
            num_calls_in_progress);
  // LoadRecordValue for KEY_1 is aggregated correctly.
  LoadRecordValue value_for_key1 =
      per_balancer_store.container().find(KEY_1)->second;
  EXPECT_EQ(value_for_key1.start_count(), v1.start_count() + v3.start_count());
  EXPECT_EQ(value_for_key1.ok_count(), v1.ok_count() + v3.ok_count());
  EXPECT_EQ(value_for_key1.error_count(), v1.error_count() + v3.error_count());
  EXPECT_EQ(value_for_key1.bytes_sent(), v1.bytes_sent() + v3.bytes_sent());
  EXPECT_EQ(value_for_key1.bytes_recv(), v1.bytes_recv() + v3.bytes_recv());
  EXPECT_EQ(value_for_key1.latency_ms(), v1.latency_ms() + v3.latency_ms());
  EXPECT_EQ(value_for_key1.call_metrics().size(), 2U);
  EXPECT_EQ(value_for_key1.call_metrics().find(METRIC_1)->second.count(),
            v1.call_metrics().find(METRIC_1)->second.count() +
                v3.call_metrics().find(METRIC_1)->second.count());
  EXPECT_EQ(value_for_key1.call_metrics().find(METRIC_1)->second.total(),
            v1.call_metrics().find(METRIC_1)->second.total() +
                v3.call_metrics().find(METRIC_1)->second.total());
  EXPECT_EQ(value_for_key1.call_metrics().find(METRIC_2)->second.count(),
            v3.call_metrics().find(METRIC_2)->second.count());
  EXPECT_EQ(value_for_key1.call_metrics().find(METRIC_2)->second.total(),
            v3.call_metrics().find(METRIC_2)->second.total());
  // LoadRecordValue for KEY_2 is aggregated (trivially) correctly.
  LoadRecordValue value_for_key2 =
      per_balancer_store.container().find(KEY_2)->second;
  EXPECT_EQ(value_for_key2.start_count(), v2.start_count());
  EXPECT_EQ(value_for_key2.ok_count(), v2.ok_count());
  EXPECT_EQ(value_for_key2.error_count(), v2.error_count());
  EXPECT_EQ(value_for_key2.bytes_sent(), v2.bytes_sent());
  EXPECT_EQ(value_for_key2.bytes_recv(), v2.bytes_recv());
  EXPECT_EQ(value_for_key2.latency_ms(), v2.latency_ms());
  EXPECT_EQ(value_for_key2.call_metrics().size(), 2U);
  EXPECT_EQ(value_for_key2.call_metrics().find(METRIC_1)->second.count(),
            v2.call_metrics().find(METRIC_1)->second.count());
  EXPECT_EQ(value_for_key2.call_metrics().find(METRIC_1)->second.total(),
            v2.call_metrics().find(METRIC_1)->second.total());
  EXPECT_EQ(value_for_key2.call_metrics().find(METRIC_2)->second.count(),
            v2.call_metrics().find(METRIC_2)->second.count());
  EXPECT_EQ(value_for_key2.call_metrics().find(METRIC_2)->second.total(),
            v2.call_metrics().find(METRIC_2)->second.total());
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
