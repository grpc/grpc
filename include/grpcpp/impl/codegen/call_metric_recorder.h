/*
 *
 * Copyright 2022 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_CALL_METRIC_RECORDER_H
#define GRPCPP_IMPL_CODEGEN_CALL_METRIC_RECORDER_H

// IWYU pragma: private

#include <memory>
#include <string>

#include "absl/strings/string_view.h"

#include <grpcpp/impl/codegen/slice.h>

namespace grpc_core {
struct BackendMetricData;
}

namespace grpc {

namespace experimental {
class OrcaServerInterceptor;
}

class CallMetricRecorder {
 public:
  CallMetricRecorder();
  ~CallMetricRecorder();
  CallMetricRecorder& RecordCpuUtilizationMetric(double value);
  CallMetricRecorder& RecordMemoryUtilizationMetric(double value);
  CallMetricRecorder& RecordRequestsPerSecond(uint32_t value);
  CallMetricRecorder& RecordUtilizationMetric(const absl::string_view& name,
                                              double value);
  CallMetricRecorder& RecordRequestCostMetric(const absl::string_view& name,
                                              double value);
  bool disabled() const { return disabled_; }

 private:
  std::string CreateSerializedReport();
  std::unique_ptr<grpc_core::BackendMetricData> backend_metric_data_;
  bool disabled_ = false;
  friend class experimental::OrcaServerInterceptor;
};

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CALL_METRIC_RECORDER_H
