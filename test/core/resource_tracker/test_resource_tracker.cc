#include "test/core/resource_tracker/test_resource_tracker.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {
namespace testing {

std::vector<std::string> TestResourceTracker::GetMetrics() const {
  std::vector<std::string> keys;
  for (const auto& pair : metric_values_) {
    keys.push_back(pair.first);
  }
  return keys;
}

absl::StatusOr<double> TestResourceTracker::GetMetricValue(
    const std::string& metric_name) const {
  auto it = metric_values_.find(metric_name);
  if (it != metric_values_.end()) {
    return it->second;
  }
  return absl::NotFoundError("Metric not found");
}

void TestResourceTracker::SetMetricValue(const std::string& metric_name,
                                         double value) {
  metric_values_[metric_name] = value;
}

}  // namespace testing
}  // namespace grpc_core
