// Copyright 2023 gRPC authors.
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

// TODO(xuanwn): clean up includes
#include <stdint.h>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <mutex>
#include <map>

#include <grpc/grpc.h>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/synchronization/mutex.h"
#include "absl/strings/strip.h"
#include "absl/strings/escaping.h"
#include "absl/status/statusor.h"

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/context.h"
#include "src/cpp/ext/gcp/observability_config.h"

#include "src/python/grpcio_observability/grpc_observability/python_census_context.h"
#include "src/python/grpcio_observability/grpc_observability/constants.h"

#ifndef OBSERVABILITY_MAIN_H
#define OBSERVABILITY_MAIN_H

namespace grpc_observability {

struct CensusData {
  DataType type;
  std::vector<Label> labels;
  // TODO(xuanwn): We can use union here
  SpanCensusData span_data;
  Measurement measurement_data;
  CensusData() {}
  CensusData(Measurement mm, std::vector<Label> labels)
      : type(kMetricData), labels(std::move(labels)), measurement_data(mm)  {}
  CensusData(SpanCensusData sd)
      : type(kSpanData), span_data(sd) {}
};

struct CloudMonitoring {
  CloudMonitoring() {}
};

struct CloudTrace {
  float sampling_rate = 0.0;
  CloudTrace() {}
  CloudTrace(double sr) : sampling_rate(sr) {}
};

struct CloudLogging {
  CloudLogging() {}
};

struct GcpObservabilityConfig {
  CloudMonitoring cloud_monitoring;
  CloudTrace cloud_trace;
  CloudLogging cloud_logging;
  std::string project_id;
  std::vector<Label> labels;
  bool is_valid;
  GcpObservabilityConfig(): is_valid(false) {}
  GcpObservabilityConfig(CloudMonitoring cloud_monitoring, CloudTrace cloud_trace, CloudLogging cloud_logging,
                         std::string project_id, std::vector<Label> labels)
    : cloud_monitoring(cloud_monitoring), cloud_trace(cloud_trace), cloud_logging(cloud_logging),
      project_id(project_id), labels(labels), is_valid(true) {}
};

// extern is requeired for Cython
extern std::queue<CensusData>* kCensusDataBuffer;
extern std::mutex kCensusDataBufferMutex;
extern std::condition_variable CensusDataBufferCV;

void* CreateClientCallTracer(char* method, char* trace_id, char* parent_span_id);

void* CreateServerCallTracerFactory();

void NativeObservabilityInit();

void AwaitNextBatchLocked(std::unique_lock<std::mutex>& lock, int timeout_ms);

void AddCensusDataToBuffer(CensusData buffer);

void RecordIntMetric(MetricsName name, int64_t value, std::vector<Label> labels);

void RecordDoubleMetric(MetricsName name, double value, std::vector<Label> labels);

void RecordSpan(SpanCensusData span_census_data);

GcpObservabilityConfig ReadObservabilityConfig();

}  // namespace grpc_observability

#endif  // OBSERVABILITY_MAIN_H
