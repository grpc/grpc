#ifndef GRPC_TEST_CORE_LIB_RESOURCE_TRACKER_TEST_RESOURCE_TRACKER_H_
#define GRPC_TEST_CORE_LIB_RESOURCE_TRACKER_TEST_RESOURCE_TRACKER_H_

#include <string>
#include <vector>

#include "src/core/lib/resource_tracker/resource_tracker.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

namespace grpc_core {
namespace testing {

class TestResourceTracker : public ResourceTracker {
 public:
  std::vector<std::string> GetMetrics() const override;

  absl::StatusOr<double> GetMetricValue(
      const std::string& metric_name) const override;

  void SetMetricValue(const std::string& metric_name, double value);

 private:
  absl::flat_hash_map<std::string, double> metric_values_;
};

}  // namespace testing
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_LIB_RESOURCE_TRACKER_TEST_RESOURCE_TRACKER_H_
