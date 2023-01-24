#ifndef LOCAL_GOOGLE_HOME_YSSEUNG_WORK_GRPC_GIT_GRPC_SRC_CPP_SERVER_BACKEND_METRIC_RECORDER_H_
#define LOCAL_GOOGLE_HOME_YSSEUNG_WORK_GRPC_GIT_GRPC_SRC_CPP_SERVER_BACKEND_METRIC_RECORDER_H_

#include <map>

#include <grpcpp/ext/call_metric_recorder.h>

#include "src/core/ext/filters/backend_metrics/backend_metric_provider.h"

namespace grpc {

class BackendMetricState : public grpc_core::BackendMetricProvider,
                           public experimental::CallMetricRecorder {
 public:
  // `server_metric_recorder` is optional. When set, GetBackendMetricData()
  // merges metrics from `server_metric_recorder` with metrics recorded to this.
  explicit BackendMetricState(
      experimental::ServerMetricRecorder* server_metric_recorder)
      : server_metric_recorder_(server_metric_recorder) {}
  ~BackendMetricState() override = default;
  experimental::CallMetricRecorder& RecordCpuUtilizationMetric(
      double value) override;
  experimental::CallMetricRecorder& RecordMemoryUtilizationMetric(
      double value) override;
  experimental::CallMetricRecorder& RecordQpsMetric(double value) override;
  experimental::CallMetricRecorder& RecordUtilizationMetric(
      string_ref name, double value) override;
  experimental::CallMetricRecorder& RecordRequestCostMetric(
      string_ref name, double value) override;

 private:
  // This clears metrics currently recorded. Don't call twice.
  grpc_core::BackendMetricData GetBackendMetricData() override;

  experimental::ServerMetricRecorder* server_metric_recorder_;
  std::atomic<double> cpu_utilization_{-1.0};
  std::atomic<double> mem_utilization_{-1.0};
  std::atomic<double> qps_{-1.0};
  internal::Mutex mu_;
  std::map<absl::string_view, double> utilization_ ABSL_GUARDED_BY(mu_);
  std::map<absl::string_view, double> request_cost_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc

#endif  // LOCAL_GOOGLE_HOME_YSSEUNG_WORK_GRPC_GIT_GRPC_SRC_CPP_SERVER_BACKEND_METRIC_RECORDER_H_
