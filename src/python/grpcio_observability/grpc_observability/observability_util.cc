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

#include <string>
#include <cstdlib>

#include "absl/strings/string_view.h"

#include "src/python/grpcio_observability/grpc_observability/observability_util.h"
#include "src/python/grpcio_observability/grpc_observability/server_call_tracer.h"
#include "src/python/grpcio_observability/grpc_observability/client_call_tracer.h"

namespace grpc_observability {

std::queue<CensusData>* g_census_data_buffer;
std::mutex g_census_data_buffer_mutex;
std::condition_variable g_census_data_buffer_cv;
// TODO(xuanwn): Change below to a more appropriate number.
// Assume buffer will store 100 CensusData and start export when buffer is 50% full.
// kMaxExportBufferSizeBytes = 100 * (2048(kMaxTagsLen) + 1024(Buffer))
constexpr float kExportThreshold = 0.5;
constexpr int kMaxExportBufferSizeBytes = 100 * 1024 * 3;

namespace {

float GetExportThreadHold() {
  const char* value = std::getenv("GRPC_PYTHON_CENSUS_EXPORT_THRESHOLD");
  if (value != nullptr) {
    return std::stof(value);
  }
  return kExportThreshold;
}

int GetMaxExportBufferSize() {
  const char* value = std::getenv("GRPC_PYTHON_CENSUS_MAX_EXPORT_BUFFER_SIZE_BYTES");
  if (value != nullptr) {
    return std::stoi(value);
  }
  return kMaxExportBufferSizeBytes;
}

}  // namespace


void RecordIntMetric(MetricsName name, int64_t value, std::vector<Label> labels) {
  Measurement measurement_data;
  measurement_data.type = kMeasurementInt;
  measurement_data.name = name;
  measurement_data.value.value_int = value;

  CensusData data = CensusData(measurement_data, labels);
  AddCensusDataToBuffer(data);
}


void RecordDoubleMetric(MetricsName name, double value, std::vector<Label> labels) {
  Measurement measurement_data;
  measurement_data.type = kMeasurementDouble;
  measurement_data.name = name;
  measurement_data.value.value_double = value;

  CensusData data = CensusData(measurement_data, labels);
  AddCensusDataToBuffer(data);
}


void RecordSpan(SpanCensusData span_census_data) {
  CensusData data = CensusData(span_census_data);
  AddCensusDataToBuffer(data);
}


void NativeObservabilityInit() {
    g_census_data_buffer= new std::queue<CensusData>;
}


void* CreateClientCallTracer(char* method, char* trace_id, char* parent_span_id) {
    void* client_call_tracer = new PythonOpenCensusCallTracer(method, trace_id, parent_span_id, PythonOpenCensusTracingEnabled());
    return client_call_tracer;
}


void* CreateServerCallTracerFactory() {
    void* server_call_tracer_factory = new PythonOpenCensusServerCallTracerFactory();
    return server_call_tracer_factory;
}


void AwaitNextBatchLocked(std::unique_lock<std::mutex>& lock, int timeout_ms) {
  auto now = std::chrono::system_clock::now();
  g_census_data_buffer_cv.wait_until(lock, now + std::chrono::milliseconds(timeout_ms));
}


void AddCensusDataToBuffer(CensusData data) {
  std::unique_lock<std::mutex> lk(g_census_data_buffer_mutex);
  if (g_census_data_buffer->size() >= GetMaxExportBufferSize()) {
    gpr_log(GPR_DEBUG, "Reached maximum sensus data buffer size, discarding this CensusData entry");
  } else {
    g_census_data_buffer->push(data);
  }
  if (g_census_data_buffer->size() >= (GetExportThreadHold() * GetMaxExportBufferSize())) {
      g_census_data_buffer_cv.notify_all();
  }
}


GcpObservabilityConfig ReadAndActivateObservabilityConfig() {
  auto config = grpc::internal::GcpObservabilityConfig::ReadFromEnv();

  if (!config.ok()) {
    return GcpObservabilityConfig();
  }

  if (!config->cloud_trace.has_value() &&
      !config->cloud_monitoring.has_value() &&
      !config->cloud_logging.has_value()) {
    return GcpObservabilityConfig();
  }

  if (!config->cloud_trace.has_value()) {
    EnablePythonOpenCensusTracing(false);
  } else {
    EnablePythonOpenCensusTracing(true);
  }
  if (!config->cloud_monitoring.has_value()) {
    EnablePythonOpenCensusStats(false);
  } else {
      EnablePythonOpenCensusStats(true);
  }

  std::vector<Label> labels;
  std::string project_id = config->project_id;
  CloudMonitoring cloud_monitoring_config = CloudMonitoring();
  CloudTrace cloud_trace_config = CloudTrace();
  CloudLogging cloud_logging_config = CloudLogging();

  if (config->cloud_trace.has_value() || config->cloud_monitoring.has_value()) {
    labels.reserve(config->labels.size());
    // Insert in user defined labels from the GCP Observability config.
    for (const auto& label : config->labels) {
      labels.emplace_back(Label{label.first, label.second});
    }

    if (config->cloud_trace.has_value()) {
      double sampleRate = config->cloud_trace->sampling_rate;
      cloud_trace_config = CloudTrace(sampleRate);
    }
    if (config->cloud_monitoring.has_value()) {
      cloud_monitoring_config = CloudMonitoring();
    }
  }

  // Clound logging
  if (config->cloud_logging.has_value()) {
    // TODO(xuanwn): Read cloud logging config
  }

  return GcpObservabilityConfig(cloud_monitoring_config, cloud_trace_config,
                                cloud_logging_config, project_id, labels);
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
