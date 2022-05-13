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

#ifndef GRPCPP_EXT_CALL_METRIC_RECORDER_H
#define GRPCPP_EXT_CALL_METRIC_RECORDER_H

// IWYU pragma: private, include <grpcpp/call_metric_recorder.h>

#include <memory>
#include <string>

#include "absl/strings/string_view.h"

#include <grpcpp/impl/codegen/slice.h>

namespace grpc_core {
struct BackendMetricData;
}

namespace grpc {
class ServerBuilder;

namespace experimental {
class OrcaServerInterceptor;

// Registers the per-rpc orca load reporter into the \a ServerBuilder.
// Once this is done, the server will automatically send the load metrics
// after each RPC as they were reported. In order to report load metrics,
// call the \a ServerContext::ExperimentalGetCallMetricRecorder() method to
// retrieve the recorder for the current call.
void EnableCallMetricRecording(ServerBuilder*);

/// Records call metrics for the purpose of load balancing.
/// During an RPC, call \a ServerContext::ExperimentalGetCallMetricRecorder()
/// method to retrive the recorder for the current call.
class CallMetricRecorder {
 public:
  CallMetricRecorder();
  ~CallMetricRecorder();

  /// Records a call metric measurement for CPU utilization.
  /// Multiple calls to this method will override the stored value.
  CallMetricRecorder& RecordCpuUtilizationMetric(double value);

  /// Records a call metric measurement for memory utilization.
  /// Multiple calls to this method will override the stored value.
  CallMetricRecorder& RecordMemoryUtilizationMetric(double value);

  /// Records a call metric measurement for utilization.
  /// Multiple calls to this method with the same name will
  /// override the corresponding stored value.
  CallMetricRecorder& RecordUtilizationMetric(grpc::string_ref name,
                                              double value);

  /// Records a call metric measurement for request cost.
  /// Multiple calls to this method with the same name will
  /// override the corresponding stored value.
  CallMetricRecorder& RecordRequestCostMetric(grpc::string_ref name,
                                              double value);
  bool disabled() const { return disabled_; }

 private:
  std::string CreateSerializedReport();
  std::unique_ptr<grpc_core::BackendMetricData> backend_metric_data_;
  bool disabled_ = false;
  friend class experimental::OrcaServerInterceptor;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_EXT_CALL_METRIC_RECORDER_H
