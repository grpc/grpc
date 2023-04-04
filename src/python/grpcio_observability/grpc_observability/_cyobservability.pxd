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
from libc.stdio cimport printf

ctypedef   signed long long int64_t

cdef extern from "<queue>" namespace "std" nogil:
    cdef cppclass queue[T]:
        queue()
        bint empty()
        T& front()
        T& back()
        void pop()
        void push(T&)
        size_t size()

cdef extern from "python_census_context.h" namespace "grpc_observability":
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

  ctypedef struct SpanSensusData:
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

cdef extern from "observability_main.h" namespace "grpc_observability":
  cdef cGcpObservabilityConfig ReadObservabilityConfig() nogil
  cdef void gcpObservabilityInit() except +
  cdef void* CreateClientCallTracer(char* method, char* trace_id, char* parent_span_id) except +
  cdef void* CreateServerCallTracerFactory() except +
  cdef queue[cCensusData] kSensusDataBuffer
  cdef void AwaitNextBatch(int) nogil
  cdef void LockSensusDataBuffer() nogil
  cdef void UnlockSensusDataBuffer() nogil
  cdef bint OpenCensusStatsEnabled() nogil
  cdef bint OpenCensusTracingEnabled() nogil

  cppclass cCensusData "::grpc_observability::CensusData":
    DataType type
    Measurement measurement_data
    SpanSensusData span_data
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

cdef extern from "constants.h" namespace "grpc_observability":
  ctypedef enum DataType:
    kSpanData
    kMetricData

  ctypedef enum MeasurementType:
    kMeasurementDouble
    kMeasurementInt

cdef extern from "sampler.h" namespace "grpc_observability":
  cdef cppclass ProbabilitySampler:
    @staticmethod
    ProbabilitySampler& Get()

    void SetThreshold(double sampling_rate)
