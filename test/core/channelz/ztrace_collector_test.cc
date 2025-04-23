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

#include "gtest/gtest.h"
#include "src/core/util/notification.h"

namespace grpc_core::channelz {

struct TestData {
  int n;

  void RenderJson(Json::Object& json) const { json["n"] = Json::FromNumber(n); }
  size_t MemoryUsage() const { return sizeof(TestData); }
};

class TestConfig {
 public:
  explicit TestConfig(std::map<std::string, std::string> args) {
    EXPECT_EQ(args["test_arg"], "test_value");
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

void ValidateSimpleTrace(const Json& result, int num_appends) {
  ASSERT_EQ(result.type(), Json::Type::kObject);
  auto entries_it = result.object().find("entries");
  ASSERT_NE(entries_it, result.object().end());
  const auto& entries = entries_it->second;
  ASSERT_EQ(entries.type(), Json::Type::kArray);
  const auto& entries_array = entries.array();
  EXPECT_LE(entries_array.size(), num_appends);
  int i = 0;
  for (const auto& entry : entries_array) {
    ASSERT_EQ(entry.type(), Json::Type::kObject);
    const auto& entry_object = entry.object();
    auto n_it = entry_object.find("n");
    ASSERT_NE(n_it, entry_object.end());
    ASSERT_EQ(n_it->second.type(), Json::Type::kNumber);
    EXPECT_EQ(n_it->second.string(), std::to_string(1000 + i));
    i++;
  }
}

TEST(ZTraceCollectorTest, SingleTraceWorks) {
  grpc_init();
  ZTraceCollector<TestConfig, TestData> collector;
  Notification n;
  Json result;
  collector.MakeZTrace()->Run(
      Timestamp::Now() + Duration::Milliseconds(100),
      {{"memory_cap", std::to_string(1024 * 1024 * 1024)},
       {"test_arg", "test_value"}},
      grpc_event_engine::experimental::GetDefaultEventEngine(),
      [&n, &result](Json r) {
        result = r;
        n.Notify();
      });
  int i = 0;
  while (!n.HasBeenNotified()) {
    collector.Append(TestData{1000 + i});
    i++;
  }
  ValidateSimpleTrace(result, i);
  grpc_shutdown();
}

TEST(ZTraceCollectorTest, MultipleTracesWork) {
  grpc_init();
  ZTraceCollector<TestConfig, TestData> collector;
  Notification n1;
  Json result1;
  Notification n2;
  Json result2;
  collector.MakeZTrace()->Run(
      Timestamp::Now() + Duration::Milliseconds(100),
      {{"memory_cap", std::to_string(1024 * 1024 * 1024)},
       {"test_arg", "test_value"}},
      grpc_event_engine::experimental::GetDefaultEventEngine(),
      [&n1, &result1](Json r) {
        result1 = r;
        n1.Notify();
      });
  collector.MakeZTrace()->Run(
      Timestamp::Now() + Duration::Milliseconds(100),
      {{"memory_cap", std::to_string(1024 * 1024 * 1024)},
       {"test_arg", "test_value"}},
      grpc_event_engine::experimental::GetDefaultEventEngine(),
      [&n2, &result2](Json r) {
        result2 = r;
        n2.Notify();
      });
  int i = 0;
  while (!n1.HasBeenNotified() || !n2.HasBeenNotified()) {
    collector.Append(TestData{1000 + i});
    i++;
  }
  ValidateSimpleTrace(result1, i);
  ValidateSimpleTrace(result2, i);
  grpc_shutdown();
}

TEST(ZTraceCollectorTest, EarlyTerminationWorks) {
  grpc_init();
  ZTraceCollector<TestConfig, TestData> collector;
  Notification n;
  Json result;
  collector.MakeZTrace()->Run(
      Timestamp::Now() + Duration::Hours(100), {{"test_arg", "test_value"}},
      grpc_event_engine::experimental::GetDefaultEventEngine(),
      [&n, &result](Json r) {
        result = r;
        n.Notify();
      });
  int i = 0;
  while (!n.HasBeenNotified()) {
    collector.Append(TestData{i});
    i++;
  }
  ASSERT_EQ(result.type(), Json::Type::kObject);
  auto entries_it = result.object().find("entries");
  ASSERT_NE(entries_it, result.object().end());
  const auto& entries = entries_it->second;
  ASSERT_EQ(entries.type(), Json::Type::kArray);
  const auto& entries_array = entries.array();
  EXPECT_EQ(entries_array.size(), 43);
  i = 0;
  for (const auto& entry : entries_array) {
    ASSERT_EQ(entry.type(), Json::Type::kObject);
    const auto& entry_object = entry.object();
    auto n_it = entry_object.find("n");
    ASSERT_NE(n_it, entry_object.end());
    ASSERT_EQ(n_it->second.type(), Json::Type::kNumber);
    EXPECT_EQ(n_it->second.string(), std::to_string(i));
    i++;
  }
  grpc_shutdown();
}

struct ExhaustionResult {
  Json result;
  Notification n;
};

TEST(ZTraceCollectorTest, ExhaustionTest) {
  grpc_init();
  ZTraceCollector<TestConfig, TestData> collector;
  std::vector<std::unique_ptr<ExhaustionResult>> results;
  for (size_t i = 0; i < 10000; i++) {
    results.emplace_back(std::make_unique<ExhaustionResult>());
    auto* r = results.back().get();
    collector.MakeZTrace()->Run(
        Timestamp::Now() + Duration::Hours(100), {{"test_arg", "test_value"}},
        grpc_event_engine::experimental::GetDefaultEventEngine(), [r](Json j) {
          r->result = j;
          r->n.Notify();
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
    ASSERT_EQ(r->result.type(), Json::Type::kObject);
    auto status_it = r->result.object().find("status");
    ASSERT_NE(status_it, r->result.object().end());
    ASSERT_EQ(status_it->second.type(), Json::Type::kString);
  }
  grpc_shutdown();
}

}  // namespace grpc_core::channelz
