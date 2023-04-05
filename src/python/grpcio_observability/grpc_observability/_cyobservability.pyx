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

cimport cpython

import sys
import os
import logging
from threading import Thread
from typing import List, Tuple, Mapping

from grpc_observability import open_census
from grpc_observability import measures

cdef const char* CLIENT_CALL_TRACER = "client_call_tracer"
cdef const char* SERVER_CALL_TRACER_FACTORY = "server_call_tracer_factory"
cdef bint GLOBAL_SHUTDOWN_EXPORT_THREAD = False
cdef object global_export_thread

_LOGGER = logging.getLogger(__name__)

class PyMetric:
  def __init__(self, measurement, labels):
    self.name = measurement['name']
    self.labels = labels
    self.measure = METRICS_NAME_TO_MEASURE.get(self.name)
    if measurement['type'] == kMeasurementDouble:
      self.measure_double = True
      self.measure_value = measurement['value']['value_double']
    else:
      self.measure_double = False
      self.measure_value = measurement['value']['value_int']


class PySpan:
  def __init__(self, span_data, span_labels, span_annotations):
      self.name = _decode(span_data['name'])
      self.start_time = _decode(span_data['start_time'])
      self.end_time = _decode(span_data['end_time'])
      self.trace_id = _decode(span_data['trace_id'])
      self.span_id = _decode(span_data['span_id'])
      self.parent_span_id = _decode(span_data['parent_span_id'])
      self.status = _decode(span_data['status'])
      self.span_labels = span_labels
      self.span_annotations = span_annotations
      self.should_sample = span_data['should_sample']
      self.child_span_count = span_data['child_span_count']


def observability_init() -> None:
  gcpObservabilityInit() # remove print buffer
  _start_exporting_thread()


def _start_exporting_thread() -> None:
  global global_export_thread
  global_export_thread = Thread(target=ExportSensusData)
  global_export_thread.start()


def read_gcp_observability_config() -> None:
  py_labels = {}
  sampling_rate = 0.0
  tracing_enabled = False
  monitoring_enabled = False

  cdef cGcpObservabilityConfig c_config = ReadObservabilityConfig()

  for label in c_config.labels:
    py_labels[_decode(label.key)] = _decode(label.value)

  if OpenCensusTracingEnabled():
    sampling_rate = c_config.cloud_trace.sampling_rate
    tracing_enabled = True
    os.environ['GRPC_OPEN_CENSUS_TRACING_ENABLED'] = 'True'
    # Save sampling rate to global sampler.
    ProbabilitySampler.Get().SetThreshold(sampling_rate)

  if OpenCensusStatsEnabled():
    monitoring_enabled = True
    os.environ['GRPC_OPEN_CENSUS_STATS_ENABLED'] = 'True'

  py_config = open_census.gcpObservabilityConfig.get()
  py_config.set_configuration(_decode(c_config.project_id), sampling_rate,
                              py_labels, tracing_enabled, monitoring_enabled)


def create_client_call_tracer_capsule(bytes method, bytes trace_id,
                                      bytes parent_span_id=b'') -> cpython.PyObject:
  cdef char* c_method = cpython.PyBytes_AsString(method)
  cdef char* c_trace_id = cpython.PyBytes_AsString(trace_id)
  cdef char* c_parent_span_id = cpython.PyBytes_AsString(parent_span_id)

  cdef void* call_tracer = CreateClientCallTracer(c_method, c_trace_id, c_parent_span_id)
  capsule = cpython.PyCapsule_New(call_tracer, CLIENT_CALL_TRACER, NULL)
  return capsule


def create_server_call_tracer_factory_capsule() -> cpython.PyObject:
  cdef void* call_tracer_factory = CreateServerCallTracerFactory()

  capsule = cpython.PyCapsule_New(call_tracer_factory, SERVER_CALL_TRACER_FACTORY, NULL)
  return capsule


def _c_label_to_labels(cLabels) -> Mapping[str, str]:
  py_labels = {}
  for label in cLabels:
    py_labels[_decode(label['key'])] = _decode(label['value'])
  return py_labels


def _c_annotation_to_annotations(cAnnotations) -> List[Tuple[str, str]]:
  py_annotations = []
  for annotation in cAnnotations:
    py_annotations.append((_decode(annotation['time_stamp']),
                          _decode(annotation['description'])))
  return py_annotations


def at_observability_exit() -> None:
  _shutdown_exporting_thread()


cdef void ExportSensusData():
  while True:
    with nogil:
      while not GLOBAL_SHUTDOWN_EXPORT_THREAD:
        # Wait for next batch of sensus data OR timeout at fixed interval.
        AwaitNextBatch(500)

        # Break only when buffer have data
        LockSensusDataBuffer()
        if not kSensusDataBuffer.empty():
          UnlockSensusDataBuffer()
          break
        else:
          UnlockSensusDataBuffer()

    if GLOBAL_SHUTDOWN_EXPORT_THREAD:
      # Flush remaining data before shutdown thread
      LockSensusDataBuffer()
      if not kSensusDataBuffer.empty():
        FlushSensusData()
      UnlockSensusDataBuffer()
      break # Break to shutdown exporting thead

    LockSensusDataBuffer()
    FlushSensusData()
    UnlockSensusDataBuffer()


cdef void FlushSensusData():
  py_metrics_batch = []
  py_spans_batch = []
  while not kSensusDataBuffer.empty():
    cSensusData = kSensusDataBuffer.front()
    if cSensusData.type == kMetricData:
      py_labels = _c_label_to_labels(cSensusData.labels)
      py_metric = PyMetric(cSensusData.measurement_data, py_labels)
      py_metrics_batch.append(py_metric)
    else:
      py_span_labels = _c_label_to_labels(cSensusData.span_data.span_labels)
      py_span_annotations = _c_annotation_to_annotations(cSensusData.span_data.span_annotations)
      py_span = PySpan(cSensusData.span_data, py_span_labels, py_span_annotations)
      py_spans_batch.append(py_span)
    kSensusDataBuffer.pop()

  open_census.export_metric_batch(py_metrics_batch)
  open_census.export_span_batch(py_spans_batch)


cdef void _shutdown_exporting_thread():
  with nogil:
    global GLOBAL_SHUTDOWN_EXPORT_THREAD
    GLOBAL_SHUTDOWN_EXPORT_THREAD = True
  global_export_thread.join()


cdef str _decode(bytes bytestring):
    if isinstance(bytestring, (str,)):
        return <str>bytestring
    else:
        try:
            return bytestring.decode('utf8')
        except UnicodeDecodeError:
            _LOGGER.exception('Invalid encoding on %s', bytestring)
            return bytestring.decode('latin1')
