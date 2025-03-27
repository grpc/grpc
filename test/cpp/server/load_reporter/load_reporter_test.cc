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

#include "src/cpp/server/load_reporter/load_reporter.h"

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <set>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opencensus/stats/testing/test_utils.h"
#include "src/core/ext/filters/load_reporting/registered_opencensus_objects.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/cpp/server/load_reporter/constants.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace testing {
namespace {

using ::grpc::lb::v1::LoadBalancingFeedback;
using ::grpc::load_reporter::CensusViewProvider;
using ::grpc::load_reporter::CpuStatsProvider;
using ::grpc::load_reporter::LoadReporter;
using ::opencensus::stats::ViewDescriptor;
using ::testing::DoubleNear;
using ::testing::Return;

constexpr uint64_t kFeedbackSampleWindowSeconds = 5;
constexpr uint64_t kFetchAndSampleIntervalSeconds = 1;
constexpr uint64_t kNumFeedbackSamplesInWindow =
    kFeedbackSampleWindowSeconds / kFetchAndSampleIntervalSeconds;

class MockCensusViewProvider : public CensusViewProvider {
 public:
  MOCK_METHOD0(FetchViewData, CensusViewProvider::ViewDataMap());

  const ViewDescriptor& FindViewDescriptor(const std::string& view_name) {
    auto it = view_descriptor_map().find(view_name);
    CHECK(it != view_descriptor_map().end());
    return it->second;
  }
};

class MockCpuStatsProvider : public CpuStatsProvider {
 public:
  MOCK_METHOD0(GetCpuStats, CpuStatsProvider::CpuStatsSample());
};

class LoadReporterTest : public ::testing::Test {
 public:
  LoadReporterTest() {}

  MockCensusViewProvider* mock_census_view_provider() {
    return static_cast<MockCensusViewProvider*>(
        load_reporter_->census_view_provider());
  }

  void PrepareCpuExpectation(size_t call_num) {
    auto mock_cpu_stats_provider = static_cast<MockCpuStatsProvider*>(
        load_reporter_->cpu_stats_provider());
    ::testing::InSequence s;
    for (size_t i = 0; i < call_num; ++i) {
      EXPECT_CALL(*mock_cpu_stats_provider, GetCpuStats())
          .WillOnce(Return(kCpuStatsSamples[i]))
          .RetiresOnSaturation();
    }
  }

  CpuStatsProvider::CpuStatsSample initial_cpu_stats_{2, 20};
  const std::vector<CpuStatsProvider::CpuStatsSample> kCpuStatsSamples = {
      {13, 53},    {64, 96},     {245, 345},  {314, 785},
      {874, 1230}, {1236, 2145}, {1864, 2974}};

  std::unique_ptr<LoadReporter> load_reporter_;

  const std::string kHostname1 = "kHostname1";
  const std::string kHostname2 = "kHostname2";
  const std::string kHostname3 = "kHostname3";
  // Pad to the length of a valid LB ID.
  const std::string kLbId1 = "kLbId111";
  const std::string kLbId2 = "kLbId222";
  const std::string kLbId3 = "kLbId333";
  const std::string kLbId4 = "kLbId444";
  const std::string kLoadKey1 = "kLoadKey1";
  const std::string kLoadKey2 = "kLoadKey2";
  const std::string kLoadKey3 = "kLoadKey3";
  const std::string kLbTag1 = "kLbTag1";
  const std::string kLbTag2 = "kLbTag2";
  const std::string kLbToken1 = "kLbId111kLbTag1";
  const std::string kLbToken2 = "kLbId222kLbTag2";
  const std::string kUser1 = "kUser1";
  const std::string kUser2 = "kUser2";
  const std::string kUser3 = "kUser3";
  const std::string kClientIp0 = "00";
  const std::string kClientIp1 = "0800000001";
  const std::string kClientIp2 = "3200000000000000000000000000000002";
  const std::string kMetric1 = "kMetric1";
  const std::string kMetric2 = "kMetric2";

 private:
  void SetUp() override {
    // Access the measures to make them valid.
    grpc::load_reporter::MeasureStartCount();
    grpc::load_reporter::MeasureEndCount();
    grpc::load_reporter::MeasureEndBytesSent();
    grpc::load_reporter::MeasureEndBytesReceived();
    grpc::load_reporter::MeasureEndLatencyMs();
    grpc::load_reporter::MeasureOtherCallMetric();
    // Set up the load reporter.
    auto mock_cpu = new MockCpuStatsProvider();
    auto mock_census = new MockCensusViewProvider();
    // Prepare the initial CPU stats data. Note that the expectation should be
    // set up before the load reporter is initialized, because CPU stats is
    // sampled at that point.
    EXPECT_CALL(*mock_cpu, GetCpuStats())
        .WillOnce(Return(initial_cpu_stats_))
        .RetiresOnSaturation();
    load_reporter_ = std::make_unique<LoadReporter>(
        kFeedbackSampleWindowSeconds,
        std::unique_ptr<CensusViewProvider>(mock_census),
        std::unique_ptr<CpuStatsProvider>(mock_cpu));
  }
};

class LbFeedbackTest : public LoadReporterTest {
 public:
  // Note that [start, start + count) of the fake samples (maybe plus the
  // initial record) are in the window now.
  void VerifyLbFeedback(const LoadBalancingFeedback& lb_feedback, size_t start,
                        size_t count) {
    const CpuStatsProvider::CpuStatsSample* base =
        start == 0 ? &initial_cpu_stats_ : &kCpuStatsSamples[start - 1];
    double expected_cpu_util =
        static_cast<double>(kCpuStatsSamples[start + count - 1].first -
                            base->first) /
        static_cast<double>(kCpuStatsSamples[start + count - 1].second -
                            base->second);
    ASSERT_THAT(static_cast<double>(lb_feedback.server_utilization()),
                DoubleNear(expected_cpu_util, 0.00001));
    double qps_sum = 0, eps_sum = 0;
    for (size_t i = 0; i < count; ++i) {
      qps_sum += kQpsEpsSamples[start + i].first;
      eps_sum += kQpsEpsSamples[start + i].second;
    }
    double expected_qps = qps_sum / count;
    double expected_eps = eps_sum / count;
    // TODO(juanlishen): The error is big because we use sleep(). It should be
    // much smaller when we use fake clock.
    ASSERT_THAT(static_cast<double>(lb_feedback.calls_per_second()),
                DoubleNear(expected_qps, expected_qps * 0.3));
    ASSERT_THAT(static_cast<double>(lb_feedback.errors_per_second()),
                DoubleNear(expected_eps, expected_eps * 0.3));
    LOG(INFO) << "Verified LB feedback matches the samples of index [" << start
              << ", " << start + count << ").";
  }

  const std::vector<std::pair<double, double>> kQpsEpsSamples = {
      {546.1, 153.1},  {62.1, 54.1},   {578.1, 154.2}, {978.1, 645.1},
      {1132.1, 846.4}, {531.5, 315.4}, {874.1, 324.9}};
};

TEST_F(LbFeedbackTest, ZeroDuration) {
  PrepareCpuExpectation(kCpuStatsSamples.size());
  EXPECT_CALL(*mock_census_view_provider(), FetchViewData())
      .WillRepeatedly(
          Return(grpc::load_reporter::CensusViewProvider::ViewDataMap()));
  // Verify that divide-by-zero exception doesn't happen.
  for (size_t i = 0; i < kCpuStatsSamples.size(); ++i) {
    load_reporter_->FetchAndSample();
  }
  load_reporter_->GenerateLoadBalancingFeedback();
}

TEST_F(LbFeedbackTest, Normal) {
  // Prepare view data list using the <QPS, EPS> samples.
  std::vector<CensusViewProvider::ViewDataMap> view_data_map_list;
  for (const auto& p : LbFeedbackTest::kQpsEpsSamples) {
    double qps = p.first;
    double eps = p.second;
    double ok_count = (qps - eps) * kFetchAndSampleIntervalSeconds;
    double error_count = eps * kFetchAndSampleIntervalSeconds;
    double ok_count_1 = ok_count / 3.0;
    double ok_count_2 = ok_count - ok_count_1;
    auto end_count_vd = ::opencensus::stats::testing::TestUtils::MakeViewData(
        mock_census_view_provider()->FindViewDescriptor(
            grpc::load_reporter::kViewEndCount),
        {{{kClientIp0 + kLbToken1, kHostname1, kUser1,
           grpc::load_reporter::kCallStatusOk},
          ok_count_1},
         {{kClientIp0 + kLbToken1, kHostname1, kUser2,
           grpc::load_reporter::kCallStatusOk},
          ok_count_2},
         {{kClientIp0 + kLbToken1, kHostname1, kUser1,
           grpc::load_reporter::kCallStatusClientError},
          error_count}});
    // Values for other view data don't matter.
    auto end_bytes_sent_vd =
        ::opencensus::stats::testing::TestUtils::MakeViewData(
            mock_census_view_provider()->FindViewDescriptor(
                grpc::load_reporter::kViewEndBytesSent),
            {{{kClientIp0 + kLbToken1, kHostname1, kUser1,
               grpc::load_reporter::kCallStatusOk},
              0},
             {{kClientIp0 + kLbToken1, kHostname1, kUser2,
               grpc::load_reporter::kCallStatusOk},
              0},
             {{kClientIp0 + kLbToken1, kHostname1, kUser1,
               grpc::load_reporter::kCallStatusClientError},
              0}});
    auto end_bytes_received_vd =
        ::opencensus::stats::testing::TestUtils::MakeViewData(
            mock_census_view_provider()->FindViewDescriptor(
                grpc::load_reporter::kViewEndBytesReceived),
            {{{kClientIp0 + kLbToken1, kHostname1, kUser1,
               grpc::load_reporter::kCallStatusOk},
              0},
             {{kClientIp0 + kLbToken1, kHostname1, kUser2,
               grpc::load_reporter::kCallStatusOk},
              0},
             {{kClientIp0 + kLbToken1, kHostname1, kUser1,
               grpc::load_reporter::kCallStatusClientError},
              0}});
    auto end_latency_vd = ::opencensus::stats::testing::TestUtils::MakeViewData(
        mock_census_view_provider()->FindViewDescriptor(
            grpc::load_reporter::kViewEndLatencyMs),
        {{{kClientIp0 + kLbToken1, kHostname1, kUser1,
           grpc::load_reporter::kCallStatusOk},
          0},
         {{kClientIp0 + kLbToken1, kHostname1, kUser2,
           grpc::load_reporter::kCallStatusOk},
          0},
         {{kClientIp0 + kLbToken1, kHostname1, kUser1,
           grpc::load_reporter::kCallStatusClientError},
          0}});
    view_data_map_list.push_back(
        {{::grpc::load_reporter::kViewEndCount, end_count_vd},
         {::grpc::load_reporter::kViewEndBytesSent, end_bytes_sent_vd},
         {::grpc::load_reporter::kViewEndBytesReceived, end_bytes_received_vd},
         {::grpc::load_reporter::kViewEndLatencyMs, end_latency_vd}});
  }
  {
    ::testing::InSequence s;
    for (size_t i = 0; i < view_data_map_list.size(); ++i) {
      EXPECT_CALL(*mock_census_view_provider(), FetchViewData())
          .WillOnce(Return(view_data_map_list[i]))
          .RetiresOnSaturation();
    }
  }
  PrepareCpuExpectation(kNumFeedbackSamplesInWindow + 2);
  // When the load reporter is created, a trivial LB feedback record is added.
  // But that's not enough for generating an LB feedback.
  // Fetch some view data so that non-trivial LB feedback can be generated.
  for (size_t i = 0; i < kNumFeedbackSamplesInWindow / 2; ++i) {
    // TODO(juanlishen): Find some fake clock to speed up testing.
    sleep(1);
    load_reporter_->FetchAndSample();
  }
  VerifyLbFeedback(load_reporter_->GenerateLoadBalancingFeedback(), 0,
                   kNumFeedbackSamplesInWindow / 2);
  // Fetch more view data so that the feedback record window is just full (the
  // initial record just falls out of the window).
  for (size_t i = 0; i < (kNumFeedbackSamplesInWindow + 1) / 2; ++i) {
    sleep(1);
    load_reporter_->FetchAndSample();
  }
  VerifyLbFeedback(load_reporter_->GenerateLoadBalancingFeedback(), 0,
                   kNumFeedbackSamplesInWindow);
  // Further fetching will cause the old records to fall out of the window.
  for (size_t i = 0; i < 2; ++i) {
    sleep(1);
    load_reporter_->FetchAndSample();
  }
  VerifyLbFeedback(load_reporter_->GenerateLoadBalancingFeedback(), 2,
                   kNumFeedbackSamplesInWindow);
}

using LoadReportTest = LoadReporterTest;

TEST_F(LoadReportTest, BasicReport) {
  // Make up the first view data map.
  CensusViewProvider::ViewDataMap vdm1;
  vdm1.emplace(
      grpc::load_reporter::kViewStartCount,
      ::opencensus::stats::testing::TestUtils::MakeViewData(
          mock_census_view_provider()->FindViewDescriptor(
              grpc::load_reporter::kViewStartCount),
          {{{kClientIp1 + kLbToken1, kHostname1, kUser1}, 1234},
           {{kClientIp2 + kLbToken1, kHostname1, kUser1}, 1225},
           {{kClientIp0 + kLbToken1, kHostname1, kUser1}, 10},
           {{kClientIp2 + kLbToken1, kHostname1, kUser2}, 464},
           {{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3}, 101},
           {{kClientIp1 + kLbToken2, kHostname2, kUser3}, 17},
           {{kClientIp2 + kLbId3 + kLbTag2, kHostname2, kUser3}, 23}}));
  vdm1.emplace(grpc::load_reporter::kViewEndCount,
               ::opencensus::stats::testing::TestUtils::MakeViewData(
                   mock_census_view_provider()->FindViewDescriptor(
                       grpc::load_reporter::kViewEndCount),
                   {{{kClientIp1 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusOk},
                     641},
                    {{kClientIp2 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusClientError},
                     272},
                    {{kClientIp2 + kLbToken1, kHostname1, kUser2,
                      grpc::load_reporter::kCallStatusOk},
                     996},
                    {{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3,
                      grpc::load_reporter::kCallStatusClientError},
                     34},
                    {{kClientIp1 + kLbToken2, kHostname2, kUser2,
                      grpc::load_reporter::kCallStatusOk},
                     18}}));
  vdm1.emplace(grpc::load_reporter::kViewEndBytesSent,
               ::opencensus::stats::testing::TestUtils::MakeViewData(
                   mock_census_view_provider()->FindViewDescriptor(
                       grpc::load_reporter::kViewEndBytesSent),
                   {{{kClientIp1 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusOk},
                     8977},
                    {{kClientIp2 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusClientError},
                     266},
                    {{kClientIp2 + kLbToken1, kHostname1, kUser2,
                      grpc::load_reporter::kCallStatusOk},
                     1276},
                    {{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3,
                      grpc::load_reporter::kCallStatusClientError},
                     77823},
                    {{kClientIp1 + kLbToken2, kHostname2, kUser2,
                      grpc::load_reporter::kCallStatusOk},
                     48}}));
  vdm1.emplace(grpc::load_reporter::kViewEndBytesReceived,
               ::opencensus::stats::testing::TestUtils::MakeViewData(
                   mock_census_view_provider()->FindViewDescriptor(
                       grpc::load_reporter::kViewEndBytesReceived),
                   {{{kClientIp1 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusOk},
                     2341},
                    {{kClientIp2 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusClientError},
                     466},
                    {{kClientIp2 + kLbToken1, kHostname1, kUser2,
                      grpc::load_reporter::kCallStatusOk},
                     518},
                    {{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3,
                      grpc::load_reporter::kCallStatusClientError},
                     81},
                    {{kClientIp1 + kLbToken2, kHostname2, kUser2,
                      grpc::load_reporter::kCallStatusOk},
                     27}}));
  vdm1.emplace(grpc::load_reporter::kViewEndLatencyMs,
               ::opencensus::stats::testing::TestUtils::MakeViewData(
                   mock_census_view_provider()->FindViewDescriptor(
                       grpc::load_reporter::kViewEndLatencyMs),
                   {{{kClientIp1 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusOk},
                     3.14},
                    {{kClientIp2 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusClientError},
                     5.26},
                    {{kClientIp2 + kLbToken1, kHostname1, kUser2,
                      grpc::load_reporter::kCallStatusOk},
                     45.4},
                    {{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3,
                      grpc::load_reporter::kCallStatusClientError},
                     4.4},
                    {{kClientIp1 + kLbToken2, kHostname2, kUser2,
                      grpc::load_reporter::kCallStatusOk},
                     2348.0}}));
  vdm1.emplace(
      grpc::load_reporter::kViewOtherCallMetricCount,
      ::opencensus::stats::testing::TestUtils::MakeViewData(
          mock_census_view_provider()->FindViewDescriptor(
              grpc::load_reporter::kViewOtherCallMetricCount),
          {{{kClientIp1 + kLbToken1, kHostname1, kUser2, kMetric1}, 1},
           {{kClientIp1 + kLbToken1, kHostname1, kUser2, kMetric1}, 1},
           {{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3, kMetric2},
            1}}));
  vdm1.emplace(
      grpc::load_reporter::kViewOtherCallMetricValue,
      ::opencensus::stats::testing::TestUtils::MakeViewData(
          mock_census_view_provider()->FindViewDescriptor(
              grpc::load_reporter::kViewOtherCallMetricValue),
          {{{kClientIp1 + kLbToken1, kHostname1, kUser2, kMetric1}, 1.2},
           {{kClientIp1 + kLbToken1, kHostname1, kUser2, kMetric1}, 1.2},
           {{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3, kMetric2},
            3.2}}));
  // Make up the second view data map.
  CensusViewProvider::ViewDataMap vdm2;
  vdm2.emplace(
      grpc::load_reporter::kViewStartCount,
      ::opencensus::stats::testing::TestUtils::MakeViewData(
          mock_census_view_provider()->FindViewDescriptor(
              grpc::load_reporter::kViewStartCount),
          {{{kClientIp2 + kLbToken1, kHostname1, kUser1}, 3},
           {{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3}, 778}}));
  vdm2.emplace(grpc::load_reporter::kViewEndCount,
               ::opencensus::stats::testing::TestUtils::MakeViewData(
                   mock_census_view_provider()->FindViewDescriptor(
                       grpc::load_reporter::kViewEndCount),
                   {{{kClientIp1 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusOk},
                     24},
                    {{kClientIp1 + kLbToken2, kHostname2, kUser3,
                      grpc::load_reporter::kCallStatusClientError},
                     546}}));
  vdm2.emplace(grpc::load_reporter::kViewEndBytesSent,
               ::opencensus::stats::testing::TestUtils::MakeViewData(
                   mock_census_view_provider()->FindViewDescriptor(
                       grpc::load_reporter::kViewEndBytesSent),
                   {{{kClientIp1 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusOk},
                     747},
                    {{kClientIp1 + kLbToken2, kHostname2, kUser3,
                      grpc::load_reporter::kCallStatusClientError},
                     229}}));
  vdm2.emplace(grpc::load_reporter::kViewEndBytesReceived,
               ::opencensus::stats::testing::TestUtils::MakeViewData(
                   mock_census_view_provider()->FindViewDescriptor(
                       grpc::load_reporter::kViewEndBytesReceived),
                   {{{kClientIp1 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusOk},
                     173},
                    {{kClientIp1 + kLbToken2, kHostname2, kUser3,
                      grpc::load_reporter::kCallStatusClientError},
                     438}}));
  vdm2.emplace(grpc::load_reporter::kViewEndLatencyMs,
               ::opencensus::stats::testing::TestUtils::MakeViewData(
                   mock_census_view_provider()->FindViewDescriptor(
                       grpc::load_reporter::kViewEndLatencyMs),
                   {{{kClientIp1 + kLbToken1, kHostname1, kUser1,
                      grpc::load_reporter::kCallStatusOk},
                     187},
                    {{kClientIp1 + kLbToken2, kHostname2, kUser3,
                      grpc::load_reporter::kCallStatusClientError},
                     34}}));
  vdm2.emplace(
      grpc::load_reporter::kViewOtherCallMetricCount,
      ::opencensus::stats::testing::TestUtils::MakeViewData(
          mock_census_view_provider()->FindViewDescriptor(
              grpc::load_reporter::kViewOtherCallMetricCount),
          {{{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3, kMetric1}, 1},
           {{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3, kMetric2},
            1}}));
  vdm2.emplace(
      grpc::load_reporter::kViewOtherCallMetricValue,
      ::opencensus::stats::testing::TestUtils::MakeViewData(
          mock_census_view_provider()->FindViewDescriptor(
              grpc::load_reporter::kViewOtherCallMetricValue),
          {{{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3, kMetric1}, 9.6},
           {{kClientIp1 + kLbId2 + kLbTag1, kHostname2, kUser3, kMetric2},
            5.7}}));
  // Set up mock expectation.
  EXPECT_CALL(*mock_census_view_provider(), FetchViewData())
      .WillOnce(Return(vdm1))
      .WillOnce(Return(vdm2));
  PrepareCpuExpectation(2);
  // Start testing.
  load_reporter_->ReportStreamCreated(kHostname1, kLbId1, kLoadKey1);
  load_reporter_->ReportStreamCreated(kHostname2, kLbId2, kLoadKey2);
  load_reporter_->ReportStreamCreated(kHostname2, kLbId3, kLoadKey3);
  // First fetch.
  load_reporter_->FetchAndSample();
  load_reporter_->GenerateLoads(kHostname1, kLbId1);
  LOG(INFO) << "First load generated.";
  // Second fetch.
  load_reporter_->FetchAndSample();
  load_reporter_->GenerateLoads(kHostname2, kLbId2);
  LOG(INFO) << "Second load generated.";
  // TODO(juanlishen): Verify the data.
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
