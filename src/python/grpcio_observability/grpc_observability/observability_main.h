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

#include <stdint.h>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <grpc/grpc.h>
#include <chrono>
#include <mutex>
#include <map>

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

#include "python_census_context.h"
#include "constants.h"

#ifndef OBSERVABILITY_MAIN_H
#define OBSERVABILITY_MAIN_H

namespace grpc_observability {

struct CensusData {
  CensusData() {}
  CensusData(Measurement mm, std::vector<Label> labels)
      : type(kMetricData), labels(std::move(labels)), measurement_data(mm)  {}
  CensusData(SpanSensusData sd)
      : type(kSpanData), span_data(sd) {}

  DataType type;
  std::vector<Label> labels;
  // TODO(xuanwn): We can use union here
  SpanSensusData span_data;
  Measurement measurement_data;
};

struct CloudMonitoring {
  CloudMonitoring() {}
};

struct CloudTrace {
  CloudTrace() {}
  CloudTrace(double sr) : sampling_rate(sr) {}
  float sampling_rate = 0.0;
};

struct CloudLogging {
  CloudLogging() {}
};

struct GcpObservabilityConfig {
  GcpObservabilityConfig() {}
  GcpObservabilityConfig(CloudMonitoring cm, CloudTrace ct, CloudLogging cl,
             std::string pi, std::vector<Label> ls)
      : cloud_monitoring(cm), cloud_trace(ct), cloud_logging(cl), project_id(pi), labels(ls) {}
  CloudMonitoring cloud_monitoring;
  CloudTrace cloud_trace;
  CloudLogging cloud_logging;
  std::string project_id;
  std::vector<Label> labels;
};

extern std::queue<CensusData> kSensusDataBuffer;
extern std::mutex kSensusDataBufferMutex;
extern std::condition_variable SensusDataBufferCV;

void* CreateClientCallTracer(char* method, char* trace_id, char* parent_span_id);

void* CreateServerCallTracerFactory();

void AwaitNextBatch(int delay);

void LockSensusDataBuffer();

void UnlockSensusDataBuffer();

void AddCensusDataToBuffer(CensusData buffer);

void RecordIntMetric(MetricsName name, int64_t value, std::vector<Label> labels);

void RecordDoubleMetric(MetricsName name, double value, std::vector<Label> labels);

void RecordSpan(SpanSensusData span_sensus_data);

GcpObservabilityConfig ReadObservabilityConfig();

}  // namespace grpc_observability

#endif  // OBSERVABILITY_MAIN_H
