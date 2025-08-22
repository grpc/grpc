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
