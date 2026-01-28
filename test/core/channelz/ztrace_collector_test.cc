// Copyright 2025 gRPC authors.
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

#include "src/core/channelz/ztrace_collector.h"

#include <grpc/grpc.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/channelz/channelz.h"
#include "src/core/channelz/property_list.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/notification.h"
#include "src/proto/grpc/channelz/v2/property_list.pb.h"
#include "src/proto/grpc/channelz/v2/service.pb.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc_core::channelz {

struct TestData {
  int n;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("n", n);
  }
};

class TestConfig {
 public:
  explicit TestConfig(const ZTrace::Args& args) {
    auto it = args.find("test_arg");
    EXPECT_NE(it, args.end());
    EXPECT_EQ(std::get<std::string>(it->second), "test_value");
  }

  bool Finishes(TestData n) { return n.n == 42; }
};

TEST(ZTraceCollectorTest, NoOp) {
  ZTraceCollector<TestConfig, TestData> collector;
}

TEST(ZTraceCollectorTest, AppendToNoTraceWorks) {
  ZTraceCollector<TestConfig, TestData> collector;
  collector.Append(TestData{123});
  collector.Append([]() { return TestData{100}; });
}

void ValidateSimpleTrace(const std::vector<std::string>& result,
                         int num_appends) {
  int num_events = 0;
  for (size_t i = 0; i < result.size(); ++i) {
    const auto& entry = result[i];
    grpc::channelz::v2::QueryTraceResponse response;
    CHECK(response.ParseFromString(entry));
    const int num_events_skipped =
        response.num_events_matched() - response.events().size();
    if (num_events_skipped > 0) {
      LOG(INFO) << "num_events_skipped: " << num_events_skipped;
    }
    CHECK_GE(num_events_skipped, 0);
    CHECK_LE(num_events_skipped, num_appends - num_events);
    num_events += num_events_skipped;
    for (const auto& event : response.events()) {
      CHECK_EQ(event.description(), "");
      absl::Time event_time =
          absl::FromUnixSeconds(event.timestamp().seconds()) +
          absl::Nanoseconds(event.timestamp().nanos());
      CHECK_LT(event_time, absl::Now());
      CHECK_GT(event_time, absl::Now() - absl::Seconds(30));
      CHECK_EQ(event.data().size(), 1);
      const auto& data = event.data(0);
      // data.name() is TypeName<TestData>(), which is unstable across
      // compilers. No verification here.
      CHECK_EQ(data.value().type_url(),
               "type.googleapis.com/grpc.channelz.v2.PropertyList");
      grpc::channelz::v2::PropertyList property_list;
      CHECK(data.value().UnpackTo(&property_list));
      CHECK_EQ(property_list.properties().size(), 1);
      const auto& property = property_list.properties(0);
      CHECK_EQ(property.key(), "n");
      CHECK_EQ(property.value().kind_case(),
               grpc::channelz::v2::PropertyValue::KindCase::kInt64Value);
      CHECK_EQ(property.value().int64_value(), 1000 + num_events);
      ++num_events;
    }
  }
  CHECK_EQ(num_events, num_appends);
}

TEST(ZTraceCollectorTest, SingleTraceWorks) {
  grpc_init();
  ZTraceCollector<TestConfig, TestData> collector;
  Notification n;
  std::vector<std::string> result;
  auto deadline = Timestamp::Now() + Duration::Milliseconds(100);
  auto ztrace = collector.MakeZTrace();
  ztrace->Run({{"memory_cap", int64_t{1024 * 1024 * 1024}},
               {"test_arg", std::string("test_value")}},
              grpc_event_engine::experimental::GetDefaultEventEngine(),
              [&n, &result](absl::StatusOr<std::optional<std::string>> r) {
                if (!r.ok()) {
                  n.Notify();
                  return;
                }
                if (!r->has_value()) {
                  n.Notify();
                  return;
                }
                result.push_back(std::move(**r));
              });
  int i = 0;
  while (Timestamp::Now() < deadline && !n.HasBeenNotified()) {
    collector.Append(TestData{1000 + i});
    i++;
  }
  ztrace.reset();
  n.WaitForNotification();
  ValidateSimpleTrace(result, i);
  grpc_shutdown();
}

TEST(ZTraceCollectorTest, MultipleTracesWork) {
  grpc_init();
  ZTraceCollector<TestConfig, TestData> collector;
  Notification n1;
  std::vector<std::string> result1;
  Notification n2;
  std::vector<std::string> result2;
  auto deadline = Timestamp::Now() + Duration::Milliseconds(100);
  auto ztrace1 = collector.MakeZTrace();
  ztrace1->Run({{"memory_cap", int64_t{1024 * 1024 * 1024}},
                {"test_arg", std::string("test_value")}},
               grpc_event_engine::experimental::GetDefaultEventEngine(),
               [&n1, &result1](absl::StatusOr<std::optional<std::string>> r) {
                 if (!r.ok()) {
                   n1.Notify();
                   return;
                 }
                 if (!r->has_value()) {
                   n1.Notify();
                   return;
                 }
                 result1.push_back(std::move(**r));
               });
  auto ztrace2 = collector.MakeZTrace();
  ztrace2->Run({{"memory_cap", int64_t{1024 * 1024 * 1024}},
                {"test_arg", std::string("test_value")}},
               grpc_event_engine::experimental::GetDefaultEventEngine(),
               [&n2, &result2](absl::StatusOr<std::optional<std::string>> r) {
                 if (!r.ok()) {
                   n2.Notify();
                   return;
                 }
                 if (!r->has_value()) {
                   n2.Notify();
                   return;
                 }
                 result2.push_back(std::move(**r));
               });
  int i = 0;
  while (Timestamp::Now() < deadline &&
         (!n1.HasBeenNotified() || !n2.HasBeenNotified())) {
    collector.Append(TestData{1000 + i});
    i++;
  }
  ztrace1.reset();
  ztrace2.reset();
  n1.WaitForNotification();
  n2.WaitForNotification();
  ValidateSimpleTrace(result1, i);
  ValidateSimpleTrace(result2, i);
  grpc_shutdown();
}

TEST(ZTraceCollectorTest, EarlyTerminationWorks) {
  grpc_init();
  ZTraceCollector<TestConfig, TestData> collector;
  Notification n;
  std::vector<std::string> result;
  auto ztrace = collector.MakeZTrace();
  ztrace->Run({{"test_arg", std::string("test_value")}},
              grpc_event_engine::experimental::GetDefaultEventEngine(),
              [&n, &result](absl::StatusOr<std::optional<std::string>> r) {
                if (!r.ok()) {
                  n.Notify();
                  return;
                }
                if (!r->has_value()) {
                  n.Notify();
                  return;
                }
                result.push_back(std::move(**r));
              });
  int i = 0;
  while (!n.HasBeenNotified()) {
    collector.Append(TestData{i});
    i++;
  }
  ztrace.reset();
  int event_count = 0;
  for (const auto& entry : result) {
    grpc::channelz::v2::QueryTraceResponse response;
    CHECK(response.ParseFromString(entry));
    event_count += response.events().size();
  }
  EXPECT_EQ(event_count, 43);
  grpc_shutdown();
}

struct ExhaustionResult {
  explicit ExhaustionResult(std::unique_ptr<ZTrace> ztrace)
      : ztrace(std::move(ztrace)) {}
  absl::StatusOr<std::vector<std::string>> result{std::vector<std::string>()};
  Notification n;
  std::unique_ptr<ZTrace> ztrace;
};

TEST(ZTraceCollectorTest, ExhaustionTest) {
  grpc_init();
  ZTraceCollector<TestConfig, TestData> collector;
  std::vector<std::unique_ptr<ExhaustionResult>> results;
  for (size_t i = 0; i < 10000; i++) {
    results.emplace_back(
        std::make_unique<ExhaustionResult>(collector.MakeZTrace()));
    auto* r = results.back().get();
    CHECK(r->result.ok());
    r->ztrace->Run({{"test_arg", std::string("test_value")}},
                   grpc_event_engine::experimental::GetDefaultEventEngine(),
                   [r](absl::StatusOr<std::optional<std::string>> res) {
                     CHECK(r->result.ok());
                     if (!res.ok()) {
                       r->result = res.status();
                       r->n.Notify();
                       return;
                     }
                     if (!res->has_value()) {
                       r->n.Notify();
                       return;
                     }
                     r->result->push_back(std::move(**res));
                   });
  }
  absl::SleepFor(absl::Seconds(1));
  size_t num_completed_before_finish = 0;
  for (auto& r : results) {
    if (r->n.HasBeenNotified()) ++num_completed_before_finish;
  }
  ASSERT_GT(num_completed_before_finish, 9000);
  ASSERT_LT(num_completed_before_finish, 10000);
  collector.Append(TestData{42});
  for (auto& r : results) {
    r->n.WaitForNotification();
    if (r->result.ok()) {
      // Succeeded
    } else {
      EXPECT_EQ(r->result.status().code(),
                absl::StatusCode::kResourceExhausted);
    }
  }
  grpc_shutdown();
}

}  // namespace grpc_core::channelz
