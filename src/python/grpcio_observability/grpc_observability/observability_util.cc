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

#include "observability_util.h"

#include <chrono>
#include <cstdlib>
#include <map>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "client_call_tracer.h"
#include "constants.h"
#include "python_census_context.h"
#include "server_call_tracer.h"

#include <grpc/support/log.h>

namespace grpc_observability {

std::queue<CensusData>* g_census_data_buffer;
std::mutex g_census_data_buffer_mutex;
std::condition_variable g_census_data_buffer_cv;
// TODO(xuanwn): Change below to a more appropriate number.
// Assume buffer will store 100 CensusData and start export when buffer is 70%
// full.
constexpr float kExportThreshold = 0.7;
constexpr int kMaxExportBufferSize = 10000;

namespace {

float GetExportThreadHold() {
  const char* value = std::getenv("GRPC_PYTHON_CENSUS_EXPORT_THRESHOLD");
  if (value != nullptr) {
    return std::stof(value);
  }
  return kExportThreshold;
}

int GetMaxExportBufferSize() {
  const char* value = std::getenv("GRPC_PYTHON_CENSUS_MAX_EXPORT_BUFFER_SIZE");
  if (value != nullptr) {
    return std::stoi(value);
  }
  return kMaxExportBufferSize;
}

}  // namespace

void RecordIntMetric(MetricsName name, int64_t value,
                     const std::vector<Label>& labels) {
  Measurement measurement_data;
  measurement_data.type = kMeasurementInt;
  measurement_data.name = name;
  measurement_data.value.value_int = value;

  CensusData data = CensusData(measurement_data, labels);
  AddCensusDataToBuffer(data);
}

void RecordDoubleMetric(MetricsName name, double value,
                        const std::vector<Label>& labels) {
  Measurement measurement_data;
  measurement_data.type = kMeasurementDouble;
  measurement_data.name = name;
  measurement_data.value.value_double = value;

  CensusData data = CensusData(measurement_data, labels);
  AddCensusDataToBuffer(data);
}

void RecordSpan(const SpanCensusData& span_census_data) {
  CensusData data = CensusData(span_census_data);
  AddCensusDataToBuffer(data);
}

void NativeObservabilityInit() {
  g_census_data_buffer = new std::queue<CensusData>;
}

void* CreateClientCallTracer(const char* method, const char* target,
                             const char* trace_id, const char* parent_span_id) {
  void* client_call_tracer = new PythonOpenCensusCallTracer(
      method, target, trace_id, parent_span_id, PythonCensusTracingEnabled());
  return client_call_tracer;
}

void* CreateServerCallTracerFactory() {
  void* server_call_tracer_factory =
      new PythonOpenCensusServerCallTracerFactory();
  return server_call_tracer_factory;
}

void AwaitNextBatchLocked(std::unique_lock<std::mutex>& lock, int timeout_ms) {
  auto now = std::chrono::system_clock::now();
  g_census_data_buffer_cv.wait_until(
      lock, now + std::chrono::milliseconds(timeout_ms));
}

void AddCensusDataToBuffer(const CensusData& data) {
  std::unique_lock<std::mutex> lk(g_census_data_buffer_mutex);
  if (g_census_data_buffer->size() >= GetMaxExportBufferSize()) {
    gpr_log(GPR_DEBUG,
            "Reached maximum census data buffer size, discarding this "
            "CensusData entry");
  } else {
    g_census_data_buffer->push(data);
  }
  if (g_census_data_buffer->size() >=
      (GetExportThreadHold() * GetMaxExportBufferSize())) {
    g_census_data_buffer_cv.notify_all();
  }
}

absl::string_view StatusCodeToString(grpc_status_code code) {
  switch (code) {
    case GRPC_STATUS_OK:
      return "OK";
    case GRPC_STATUS_CANCELLED:
      return "CANCELLED";
    case GRPC_STATUS_UNKNOWN:
      return "UNKNOWN";
    case GRPC_STATUS_INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case GRPC_STATUS_DEADLINE_EXCEEDED:
      return "DEADLINE_EXCEEDED";
    case GRPC_STATUS_NOT_FOUND:
      return "NOT_FOUND";
    case GRPC_STATUS_ALREADY_EXISTS:
      return "ALREADY_EXISTS";
    case GRPC_STATUS_PERMISSION_DENIED:
      return "PERMISSION_DENIED";
    case GRPC_STATUS_UNAUTHENTICATED:
      return "UNAUTHENTICATED";
    case GRPC_STATUS_RESOURCE_EXHAUSTED:
      return "RESOURCE_EXHAUSTED";
    case GRPC_STATUS_FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case GRPC_STATUS_ABORTED:
      return "ABORTED";
    case GRPC_STATUS_OUT_OF_RANGE:
      return "OUT_OF_RANGE";
    case GRPC_STATUS_UNIMPLEMENTED:
      return "UNIMPLEMENTED";
    case GRPC_STATUS_INTERNAL:
      return "INTERNAL";
    case GRPC_STATUS_UNAVAILABLE:
      return "UNAVAILABLE";
    case GRPC_STATUS_DATA_LOSS:
      return "DATA_LOSS";
    default:
      // gRPC wants users of this enum to include a default branch so that
      // adding values is not a breaking change.
      return "UNKNOWN_STATUS";
  }
}

}  // namespace grpc_observability
