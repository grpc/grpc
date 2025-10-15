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

#include "src/core/lib/resource_tracker/resource_tracker.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"

namespace grpc_core {
namespace {

// Mock implementation for testing.
class MockResourceTracker : public ResourceTracker {
 public:
  MOCK_METHOD(std::vector<std::string>, GetMetrics, (), (const, override));
  MOCK_METHOD(absl::StatusOr<double>, GetMetricValue, (const std::string&),
              (const, override));
};

TEST(ResourceTrackerTest, InitialState) {
  EXPECT_EQ(ResourceTracker::Get(), nullptr);
}

TEST(ResourceTrackerTest, SetAndGet) {
  MockResourceTracker tracker;
  ResourceTracker::Set(&tracker);
  EXPECT_EQ(ResourceTracker::Get(), &tracker);
  // Clean up
  ResourceTracker::Set(nullptr);
}

TEST(ResourceTrackerTest, SetNull) {
  MockResourceTracker tracker;
  ResourceTracker::Set(&tracker);
  EXPECT_EQ(ResourceTracker::Get(), &tracker);
  ResourceTracker::Set(nullptr);
  EXPECT_EQ(ResourceTracker::Get(), nullptr);
}

}  // namespace
}  // namespace grpc_core
