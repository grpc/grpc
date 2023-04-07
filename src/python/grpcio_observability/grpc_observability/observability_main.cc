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

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"

#include <limits.h>

#include <atomic>

namespace grpc_observability {

std::queue<CensusData> kSensusDataBuffer;
std::mutex kSensusDataBufferMutex;
std::condition_variable SensusDataBufferCV;
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


void RecordSpan(SpanSensusData span_sensus_data) {
  CensusData data = CensusData(span_sensus_data);
  AddCensusDataToBuffer(data);
}


void* CreateClientCallTracer(char* method, char* trace_id, char* parent_span_id) {
    void* client_call_tracer = new PythonOpenCensusCallTracer(method, trace_id, parent_span_id, OpenCensusTracingEnabled());
    return client_call_tracer;
}


void* CreateServerCallTracerFactory() {
    void* server_call_tracer_factory = new PythonOpenCensusServerCallTracerFactory();
    return server_call_tracer_factory;
}


void AwaitNextBatch(int timeout_ms) {
  std::unique_lock<std::mutex> lk(kSensusDataBufferMutex);
  auto now = std::chrono::system_clock::now();
  SensusDataBufferCV.wait_until(lk, now + std::chrono::milliseconds(timeout_ms));
}


void LockSensusDataBuffer() {
  kSensusDataBufferMutex.lock();
}


void UnlockSensusDataBuffer() {
  kSensusDataBufferMutex.unlock();
}


void AddCensusDataToBuffer(CensusData data) {
  std::unique_lock<std::mutex> lk(kSensusDataBufferMutex);
  kSensusDataBuffer.push(data);
   // TODO(xuanwn): Change kExportThreshold to a proper value.
  if (kSensusDataBuffer.size() >= kExportThreshold) {
      SensusDataBufferCV.notify_all();
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
    EnableOpenCensusTracing(false);
  }
  if (!config->cloud_monitoring.has_value()) {
    EnableOpenCensusStats(false);
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
      labels.push_back(Label{label.first, label.second});
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

  return GcpObservabilityConfig(cloud_monitoring_config, cloud_trace_config, cloud_logging_config, project_id, labels);
}

}  // namespace grpc_observability
