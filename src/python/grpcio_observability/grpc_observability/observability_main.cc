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

#include "observability_main.h"
#include "server_call_tracer.h"
#include "client_call_tracer.h"

#include "absl/strings/string_view.h"

namespace grpc_observability {

std::queue<CensusData>* kCensusDataBuffer;
std::mutex kCensusDataBufferMutex;
std::condition_variable CensusDataBufferCV;
// TODO(xuanwn): Change it to a more appropriate number
constexpr int kExportThreshold = 2;


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
    setbuf(stdout, nullptr);
    kCensusDataBuffer= new std::queue<CensusData>;
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
  auto status = CensusDataBufferCV.wait_until(lock, now + std::chrono::milliseconds(timeout_ms));
}


void AddCensusDataToBuffer(CensusData data) {
  std::unique_lock<std::mutex> lk(kCensusDataBufferMutex);
  kCensusDataBuffer->push(data);
  if (kCensusDataBuffer->size() >= kExportThreshold) {
      CensusDataBufferCV.notify_all();
  }
}


GcpObservabilityConfig ReadObservabilityConfig() {
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
