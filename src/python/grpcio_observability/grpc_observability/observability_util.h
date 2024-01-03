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

#ifndef OBSERVABILITY_MAIN_H
#define OBSERVABILITY_MAIN_H

#include <stdint.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "constants.h"
#include "python_census_context.h"

#include <grpc/status.h>

namespace grpc_observability {

struct CensusData {
  DataType type;
  std::vector<Label> labels;
  // TODO(xuanwn): We can use union here
  SpanCensusData span_data;
  Measurement measurement_data;
  CensusData() {}
  CensusData(const Measurement& mm, const std::vector<Label>& labels)
      : type(kMetricData), labels(std::move(labels)), measurement_data(mm) {}
  CensusData(const SpanCensusData& sd) : type(kSpanData), span_data(sd) {}
};

// extern is required for Cython
extern std::queue<CensusData>* g_census_data_buffer;
extern std::mutex g_census_data_buffer_mutex;
extern std::condition_variable g_census_data_buffer_cv;

void* CreateClientCallTracer(const char* method, const char* trace_id,
                             const char* parent_span_id);

void* CreateServerCallTracerFactory();

void NativeObservabilityInit();

void AwaitNextBatchLocked(std::unique_lock<std::mutex>& lock, int timeout_ms);

void AddCensusDataToBuffer(const CensusData& buffer);

void RecordIntMetric(MetricsName name, int64_t value,
                     const std::vector<Label>& labels);

void RecordDoubleMetric(MetricsName name, double value,
                        const std::vector<Label>& labels);

void RecordSpan(const SpanCensusData& span_census_data);

absl::string_view StatusCodeToString(grpc_status_code code);

}  // namespace grpc_observability

#endif  // OBSERVABILITY_MAIN_H
