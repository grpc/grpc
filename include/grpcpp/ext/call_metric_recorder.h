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

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpcpp/impl/codegen/slice.h>
#include <grpcpp/impl/codegen/sync.h>

namespace grpc_core {
class Arena;
struct BackendMetricData;
}  // namespace grpc_core

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
  explicit CallMetricRecorder(grpc_core::Arena* arena);
  ~CallMetricRecorder();

  /// Records a call metric measurement for CPU utilization.
  /// Multiple calls to this method will override the stored value.
  CallMetricRecorder& RecordCpuUtilizationMetric(double value);

  /// Records a call metric measurement for memory utilization.
  /// Multiple calls to this method will override the stored value.
  CallMetricRecorder& RecordMemoryUtilizationMetric(double value);

  /// Records a call metric measurement for utilization.
  /// Multiple calls to this method with the same name will
  /// override the corresponding stored value. The lifetime of the
  /// name string needs to be longer than the lifetime of the RPC
  /// itself, since it's going to be sent as trailers after the RPC
  /// finishes. It is assumed the strings are common names that
  /// are global constants.
  CallMetricRecorder& RecordUtilizationMetric(string_ref name, double value);

  /// Records a call metric measurement for request cost.
  /// Multiple calls to this method with the same name will
  /// override the corresponding stored value. The lifetime of the
  /// name string needs to be longer than the lifetime of the RPC
  /// itself, since it's going to be sent as trailers after the RPC
  /// finishes. It is assumed the strings are common names that
  /// are global constants.
  CallMetricRecorder& RecordRequestCostMetric(string_ref name, double value);

 private:
  absl::optional<std::string> CreateSerializedReport();

  internal::Mutex mu_;
  grpc_core::BackendMetricData* backend_metric_data_ ABSL_GUARDED_BY(&mu_);
  friend class experimental::OrcaServerInterceptor;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_EXT_CALL_METRIC_RECORDER_H
