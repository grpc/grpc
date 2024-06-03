# Copyright 2023 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from libcpp.string cimport string
from libcpp.vector cimport vector

ctypedef   signed long long int64_t

cdef extern from "<queue>" namespace "std" nogil:
  cdef cppclass queue[T]:
    bint empty()
    T& front()
    void pop()

cdef extern from "<mutex>" namespace "std" nogil:
  cdef cppclass mutex:
    mutex()

  cdef cppclass unique_lock[Mutex]:
    unique_lock(Mutex&)

cdef extern from "<condition_variable>" namespace "std" nogil:
  cdef cppclass condition_variable:
    void notify_all()

cdef extern from "src/core/telemetry/call_tracer.h" namespace "grpc_core":
  cdef cppclass ClientCallTracer:
    pass

cdef extern from "python_observability_context.h" namespace "grpc_observability":
  cdef void EnablePythonCensusStats(bint enable) nogil
  cdef void EnablePythonCensusTracing(bint enable) nogil

  union MeasurementValue:
    double value_double
    int64_t value_int

  ctypedef struct Label:
    string key
    string value

  ctypedef struct Annotation:
    string time_stamp
    string description

  ctypedef struct Measurement:
    cMetricsName name
    MeasurementType type
    MeasurementValue value
    bint registered_method
    bint include_exchange_labels

  ctypedef struct SpanCensusData:
    string name
    string start_time
    string end_time
    string trace_id
    string span_id
    string parent_span_id
    string status
    vector[Label] span_labels
    vector[Annotation] span_annotations
    int64_t child_span_count
    bint should_sample

cdef extern from "observability_util.h" namespace "grpc_observability":
  cdef cGcpObservabilityConfig ReadAndActivateObservabilityConfig() nogil
  cdef void NativeObservabilityInit() except +
  cdef void* CreateClientCallTracer(const char* method,
                                    const char* target,
                                    const char* trace_id,
                                    const char* parent_span_id,
                                    const char* identifier,
                                    const vector[Label] exchange_labels,
                                    bint add_csm_optional_labels,
                                    bint registered_method) except +
  cdef void* CreateServerCallTracerFactory(const vector[Label] exchange_labels, const char* identifier) except +
  cdef queue[NativeCensusData]* g_census_data_buffer
  cdef void AwaitNextBatchLocked(unique_lock[mutex]&, int) nogil
  cdef bint PythonCensusStatsEnabled() nogil
  cdef bint PythonCensusTracingEnabled() nogil
  cdef mutex g_census_data_buffer_mutex
  cdef condition_variable g_census_data_buffer_cv

  cppclass NativeCensusData "::grpc_observability::CensusData":
    DataType type
    string identifier
    Measurement measurement_data
    SpanCensusData span_data
    vector[Label] labels

  ctypedef struct CloudMonitoring:
    pass

  ctypedef struct CloudTrace:
    float sampling_rate

  ctypedef struct CloudLogging:
    pass

  ctypedef struct cGcpObservabilityConfig "::grpc_observability::GcpObservabilityConfig":
    CloudMonitoring cloud_monitoring
    CloudTrace cloud_trace
    CloudLogging cloud_logging
    string project_id
    vector[Label] labels
    bint is_valid

cdef extern from "constants.h" namespace "grpc_observability":
  ctypedef enum DataType:
    kSpanData
    kMetricData

  ctypedef enum MeasurementType:
    kMeasurementDouble
    kMeasurementInt

  ctypedef enum cMetricsName "::grpc_observability::MetricsName":
    # Client
    kRpcClientApiLatencyMeasureName
    kRpcClientSentMessagesPerRpcMeasureName
    kRpcClientSentBytesPerRpcMeasureName
    kRpcClientReceivedMessagesPerRpcMeasureName
    kRpcClientReceivedBytesPerRpcMeasureName
    kRpcClientRoundtripLatencyMeasureName
    kRpcClientCompletedRpcMeasureName
    kRpcClientServerLatencyMeasureName
    kRpcClientStartedRpcsMeasureName
    kRpcClientRetriesPerCallMeasureName
    kRpcClientTransparentRetriesPerCallMeasureName
    kRpcClientRetryDelayPerCallMeasureName
    kRpcClientTransportLatencyMeasureName

    # Server
    kRpcServerSentMessagesPerRpcMeasureName
    kRpcServerSentBytesPerRpcMeasureName
    kRpcServerReceivedMessagesPerRpcMeasureName
    kRpcServerReceivedBytesPerRpcMeasureName
    kRpcServerServerLatencyMeasureName
    kRpcServerCompletedRpcMeasureName
    kRpcServerStartedRpcsMeasureName

  string kClientMethod
  string kClientTarget
  string kClientStatus
  string kRegisteredMethod

cdef extern from "sampler.h" namespace "grpc_observability":
  cdef cppclass ProbabilitySampler:
    @staticmethod
    ProbabilitySampler& Get()

    void SetThreshold(double sampling_rate)
