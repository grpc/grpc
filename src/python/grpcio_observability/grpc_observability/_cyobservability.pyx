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
from cython.operator cimport dereference

import enum
import logging
from threading import Thread
from typing import List, Mapping, Tuple

import grpc_observability

# Time we wait for batch exporting census data
EXPORT_BATCH_INTERVAL = 0.5
cdef const char* CLIENT_CALL_TRACER = "client_call_tracer"
cdef const char* SERVER_CALL_TRACER_FACTORY = "server_call_tracer_factory"
cdef bint GLOBAL_SHUTDOWN_EXPORT_THREAD = False
cdef object GLOBAL_EXPORT_THREAD

_LOGGER = logging.getLogger(__name__)

class _CyMetricsName:
  CY_CLIENT_API_LATENCY = kRpcClientApiLatencyMeasureName
  CY_CLIENT_SNET_MESSSAGES_PER_RPC = kRpcClientSentMessagesPerRpcMeasureName
  CY_CLIENT_SEND_BYTES_PER_RPC = kRpcClientSentBytesPerRpcMeasureName
  CY_CLIENT_RECEIVED_MESSAGES_PER_RPC = kRpcClientReceivedMessagesPerRpcMeasureName
  CY_CLIENT_RECEIVED_BYTES_PER_RPC = kRpcClientReceivedBytesPerRpcMeasureName
  CY_CLIENT_ROUNDTRIP_LATENCY = kRpcClientRoundtripLatencyMeasureName
  CY_CLIENT_SERVER_LATENCY = kRpcClientServerLatencyMeasureName
  CY_CLIENT_STARTED_RPCS = kRpcClientStartedRpcsMeasureName
  CY_CLIENT_RETRIES_PER_CALL = kRpcClientRetriesPerCallMeasureName
  CY_CLIENT_TRANSPARENT_RETRIES_PER_CALL = kRpcClientTransparentRetriesPerCallMeasureName
  CY_CLIENT_RETRY_DELAY_PER_CALL = kRpcClientRetryDelayPerCallMeasureName
  CY_CLIENT_TRANSPORT_LATENCY = kRpcClientTransportLatencyMeasureName
  CY_SERVER_SENT_MESSAGES_PER_RPC = kRpcServerSentMessagesPerRpcMeasureName
  CY_SERVER_SENT_BYTES_PER_RPC = kRpcServerSentBytesPerRpcMeasureName
  CY_SERVER_RECEIVED_MESSAGES_PER_RPC = kRpcServerReceivedMessagesPerRpcMeasureName
  CY_SERVER_RECEIVED_BYTES_PER_RPC = kRpcServerReceivedBytesPerRpcMeasureName
  CY_SERVER_SERVER_LATENCY = kRpcServerServerLatencyMeasureName
  CY_SERVER_STARTED_RPCS = kRpcServerStartedRpcsMeasureName

@enum.unique
class MetricsName(enum.Enum):
  CLIENT_STARTED_RPCS = _CyMetricsName.CY_CLIENT_STARTED_RPCS
  CLIENT_API_LATENCY = _CyMetricsName.CY_CLIENT_API_LATENCY
  CLIENT_SNET_MESSSAGES_PER_RPC = _CyMetricsName.CY_CLIENT_SNET_MESSSAGES_PER_RPC
  CLIENT_SEND_BYTES_PER_RPC = _CyMetricsName.CY_CLIENT_SEND_BYTES_PER_RPC
  CLIENT_RECEIVED_MESSAGES_PER_RPC = _CyMetricsName.CY_CLIENT_RECEIVED_MESSAGES_PER_RPC
  CLIENT_RECEIVED_BYTES_PER_RPC = _CyMetricsName.CY_CLIENT_RECEIVED_BYTES_PER_RPC
  CLIENT_ROUNDTRIP_LATENCY = _CyMetricsName.CY_CLIENT_ROUNDTRIP_LATENCY
  CLIENT_SERVER_LATENCY = _CyMetricsName.CY_CLIENT_SERVER_LATENCY
  CLIENT_RETRIES_PER_CALL = _CyMetricsName.CY_CLIENT_RETRIES_PER_CALL
  CLIENT_TRANSPARENT_RETRIES_PER_CALL = _CyMetricsName.CY_CLIENT_TRANSPARENT_RETRIES_PER_CALL
  CLIENT_RETRY_DELAY_PER_CALL = _CyMetricsName.CY_CLIENT_RETRY_DELAY_PER_CALL
  CLIENT_TRANSPORT_LATENCY = _CyMetricsName.CY_CLIENT_TRANSPORT_LATENCY
  SERVER_SENT_MESSAGES_PER_RPC = _CyMetricsName.CY_SERVER_SENT_MESSAGES_PER_RPC
  SERVER_SENT_BYTES_PER_RPC = _CyMetricsName.CY_SERVER_SENT_BYTES_PER_RPC
  SERVER_RECEIVED_MESSAGES_PER_RPC = _CyMetricsName.CY_SERVER_RECEIVED_MESSAGES_PER_RPC
  SERVER_RECEIVED_BYTES_PER_RPC = _CyMetricsName.CY_SERVER_RECEIVED_BYTES_PER_RPC
  SERVER_SERVER_LATENCY = _CyMetricsName.CY_SERVER_SERVER_LATENCY
  SERVER_STARTED_RPCS = _CyMetricsName.CY_SERVER_STARTED_RPCS

# Delay map creation due to circular dependencies
_CY_METRICS_NAME_TO_PY_METRICS_NAME_MAPPING = {x.value: x for x in MetricsName}

def cyobservability_init(object exporter) -> None:
  NativeObservabilityInit()
  _start_exporting_thread(exporter)


def _start_exporting_thread(object exporter) -> None:
  global GLOBAL_EXPORT_THREAD
  global GLOBAL_SHUTDOWN_EXPORT_THREAD
  GLOBAL_SHUTDOWN_EXPORT_THREAD = False
  GLOBAL_EXPORT_THREAD = Thread(target=_export_census_data, args=(exporter,))
  GLOBAL_EXPORT_THREAD.start()


def set_gcp_observability_config(object py_config) -> bool:
  """
    Returns:
      bool: True if configuration is valid, False otherwise.
  """
  py_labels = {}
  sampling_rate = 0.0

  cdef cGcpObservabilityConfig c_config = ReadAndActivateObservabilityConfig()
  if not c_config.is_valid:
    return False

  for label in c_config.labels:
    py_labels[_decode(label.key)] = _decode(label.value)

  if PythonOpenCensusTracingEnabled():
    sampling_rate = c_config.cloud_trace.sampling_rate
    # Save sampling rate to global sampler.
    ProbabilitySampler.Get().SetThreshold(sampling_rate)

  py_config.set_configuration(_decode(c_config.project_id), sampling_rate, py_labels,
                              PythonOpenCensusTracingEnabled(), PythonOpenCensusStatsEnabled())
  return True


def create_client_call_tracer(bytes method_name, bytes trace_id,
                                      bytes parent_span_id=b'') -> cpython.PyObject:
  cdef char* c_method = cpython.PyBytes_AsString(method_name)
  cdef char* c_trace_id = cpython.PyBytes_AsString(trace_id)
  cdef char* c_parent_span_id = cpython.PyBytes_AsString(parent_span_id)

  cdef void* call_tracer = CreateClientCallTracer(c_method, c_trace_id, c_parent_span_id)
  capsule = cpython.PyCapsule_New(call_tracer, CLIENT_CALL_TRACER, NULL)
  return capsule


def create_server_call_tracer_factory_capsule() -> cpython.PyObject:
  cdef void* call_tracer_factory = CreateServerCallTracerFactory()
  capsule = cpython.PyCapsule_New(call_tracer_factory, SERVER_CALL_TRACER_FACTORY, NULL)
  return capsule


def delete_client_call_tracer(object client_call_tracer) -> None:
  if cpython.PyCapsule_IsValid(client_call_tracer, CLIENT_CALL_TRACER):
    capsule_ptr = cpython.PyCapsule_GetPointer(client_call_tracer, CLIENT_CALL_TRACER)
    call_tracer_ptr = <ClientCallTracer*>capsule_ptr
    del call_tracer_ptr


def _c_label_to_labels(object cLabels) -> Mapping[str, str]:
  py_labels = {}
  for label in cLabels:
    py_labels[_decode(label['key'])] = _decode(label['value'])
  return py_labels


def _c_annotation_to_annotations(object cAnnotations) -> List[Tuple[str, str]]:
  py_annotations = []
  for annotation in cAnnotations:
    py_annotations.append((_decode(annotation['time_stamp']),
                          _decode(annotation['description'])))
  return py_annotations


def at_observability_exit() -> None:
  _shutdown_exporting_thread()


def _cy_metric_name_to_py_metric_name(object metric_name) -> grpc_observability.MetricsName:
  try:
      return _CY_METRICS_NAME_TO_PY_METRICS_NAME_MAPPING[metric_name]
  except KeyError:
      raise ValueError('Invalid metric name %s' % metric_name)


def _get_stats_data(object measurement, object labels) -> grpc_observability.StatsData:
  metric_name = _cy_metric_name_to_py_metric_name(measurement['name'])
  if measurement['type'] == kMeasurementDouble:
    py_stat = grpc_observability.StatsData(name=metric_name, measure_double=True,
                                           value_float=measurement['value']['value_double'],
                                           labels=labels)
  else:
    py_stat = grpc_observability.StatsData(name=metric_name, measure_double=False,
                                           value_int=measurement['value']['value_int'],
                                           labels=labels)
  return py_stat


def _get_tracing_data(object span_data, object span_labels, object span_annotations) -> grpc_observability.TracingData:
  py_span_labels = _c_label_to_labels(span_labels)
  py_span_annotations = _c_annotation_to_annotations(span_annotations)
  return grpc_observability.TracingData(name=_decode(span_data['name']),
                                   start_time = _decode(span_data['start_time']),
                                   end_time = _decode(span_data['end_time']),
                                   trace_id = _decode(span_data['trace_id']),
                                   span_id = _decode(span_data['span_id']),
                                   parent_span_id = _decode(span_data['parent_span_id']),
                                   status = _decode(span_data['status']),
                                   should_sample = span_data['should_sample'],
                                   child_span_count = span_data['child_span_count'],
                                   span_labels = py_span_labels,
                                   span_annotations = py_span_annotations)


def _record_rpc_latency(object exporter, str method, float rpc_latency, str status_code) -> None:
  measurement = {}
  measurement['name'] = kRpcClientApiLatencyMeasureName
  measurement['type'] = kMeasurementDouble
  measurement['value'] = {'value_double': rpc_latency}

  labels = {}
  labels[_decode(kClientMethod)] = method.strip("/")
  labels[_decode(kClientStatus)] = status_code
  metric = _get_stats_data(measurement, labels)
  exporter.export_stats_data([metric])


cdef void _export_census_data(object exporter):
  cdef int export_interval_ms = EXPORT_BATCH_INTERVAL * 1000
  while True:
    with nogil:
      while not GLOBAL_SHUTDOWN_EXPORT_THREAD:
        lk = new unique_lock[mutex](g_census_data_buffer_mutex)
        # Wait for next batch of census data OR timeout at fixed interval.
        # Batch export census data to minimize the time we acquiring the GIL.
        # TODO(xuanwn): change interval to a more appropriate number
        AwaitNextBatchLocked(dereference(lk), export_interval_ms)

        # Break only when buffer have data
        if not g_census_data_buffer.empty():
          del lk
          break
        else:
          del lk

    _flush_census_data(exporter)

    if GLOBAL_SHUTDOWN_EXPORT_THREAD:
      break # Break to shutdown exporting thead


cdef void _flush_census_data(object exporter):
  lk = new unique_lock[mutex](g_census_data_buffer_mutex)
  with nogil:
    if g_census_data_buffer.empty():
      del lk
      return
  py_metrics_batch = []
  py_spans_batch = []
  while not g_census_data_buffer.empty():
    cCensusData = g_census_data_buffer.front()
    if cCensusData.type == kMetricData:
      py_labels = _c_label_to_labels(cCensusData.labels)
      py_metric = _get_stats_data(cCensusData.measurement_data, py_labels)
      py_metrics_batch.append(py_metric)
    else:
      py_span = _get_tracing_data(cCensusData.span_data, cCensusData.span_data.span_labels,
                                  cCensusData.span_data.span_annotations)
      py_spans_batch.append(py_span)
    g_census_data_buffer.pop()

  del lk
  exporter.export_stats_data(py_metrics_batch)
  exporter.export_tracing_data(py_spans_batch)


cdef void _shutdown_exporting_thread():
  with nogil:
    global GLOBAL_SHUTDOWN_EXPORT_THREAD
    GLOBAL_SHUTDOWN_EXPORT_THREAD = True
    g_census_data_buffer_cv.notify_all()
  GLOBAL_EXPORT_THREAD.join()


cdef str _decode(bytes bytestring):
    if isinstance(bytestring, (str,)):
        return <str>bytestring
    else:
        try:
            return bytestring.decode('utf8')
        except UnicodeDecodeError:
            _LOGGER.exception('Invalid encoding on %s', bytestring)
            return bytestring.decode('latin1')
