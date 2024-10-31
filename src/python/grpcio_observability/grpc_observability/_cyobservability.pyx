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
import functools
import logging
import os
from threading import Thread
from typing import AnyStr, Dict, List, Mapping, Tuple, Union

from grpc_observability import _observability

# Time we wait for batch exporting census data
# TODO(xuanwn): change interval to a more appropriate number
CENSUS_EXPORT_BATCH_INTERVAL_SECS = float(os.environ.get('GRPC_PYTHON_CENSUS_EXPORT_BATCH_INTERVAL_SECS', 0.5))
GRPC_PYTHON_CENSUS_EXPORT_THREAD_TIMEOUT = float(os.environ.get('GRPC_PYTHON_CENSUS_EXPORT_THREAD_TIMEOUT', 10))
cdef const char* CLIENT_CALL_TRACER = "client_call_tracer"
cdef const char* SERVER_CALL_TRACER_FACTORY = "server_call_tracer_factory"
cdef bint GLOBAL_SHUTDOWN_EXPORT_THREAD = False
cdef object GLOBAL_EXPORT_THREAD

PLUGIN_IDENTIFIER_SEP = ","

_LOGGER = logging.getLogger(__name__)


class _CyMetricsName:
  CY_CLIENT_API_LATENCY = kRpcClientApiLatencyMeasureName
  CY_CLIENT_SNET_MESSSAGES_PER_RPC = kRpcClientSentMessagesPerRpcMeasureName
  CY_CLIENT_SEND_BYTES_PER_RPC = kRpcClientSentBytesPerRpcMeasureName
  CY_CLIENT_RECEIVED_MESSAGES_PER_RPC = kRpcClientReceivedMessagesPerRpcMeasureName
  CY_CLIENT_RECEIVED_BYTES_PER_RPC = kRpcClientReceivedBytesPerRpcMeasureName
  CY_CLIENT_ROUNDTRIP_LATENCY = kRpcClientRoundtripLatencyMeasureName
  CY_CLIENT_COMPLETED_RPC = kRpcClientCompletedRpcMeasureName
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
  CY_SERVER_COMPLETED_RPC = kRpcServerCompletedRpcMeasureName
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
  CLIENT_COMPLETED_RPC = _CyMetricsName.CY_CLIENT_COMPLETED_RPC
  CLIENT_SERVER_LATENCY = _CyMetricsName.CY_CLIENT_SERVER_LATENCY
  CLIENT_RETRIES_PER_CALL = _CyMetricsName.CY_CLIENT_RETRIES_PER_CALL
  CLIENT_TRANSPARENT_RETRIES_PER_CALL = _CyMetricsName.CY_CLIENT_TRANSPARENT_RETRIES_PER_CALL
  CLIENT_RETRY_DELAY_PER_CALL = _CyMetricsName.CY_CLIENT_RETRY_DELAY_PER_CALL
  SERVER_SENT_MESSAGES_PER_RPC = _CyMetricsName.CY_SERVER_SENT_MESSAGES_PER_RPC
  SERVER_SENT_BYTES_PER_RPC = _CyMetricsName.CY_SERVER_SENT_BYTES_PER_RPC
  SERVER_RECEIVED_MESSAGES_PER_RPC = _CyMetricsName.CY_SERVER_RECEIVED_MESSAGES_PER_RPC
  SERVER_RECEIVED_BYTES_PER_RPC = _CyMetricsName.CY_SERVER_RECEIVED_BYTES_PER_RPC
  SERVER_SERVER_LATENCY = _CyMetricsName.CY_SERVER_SERVER_LATENCY
  SERVER_COMPLETED_RPC = _CyMetricsName.CY_SERVER_COMPLETED_RPC
  SERVER_STARTED_RPCS = _CyMetricsName.CY_SERVER_STARTED_RPCS

# Delay map creation due to circular dependencies
_CY_METRICS_NAME_TO_PY_METRICS_NAME_MAPPING = {x.value: x for x in MetricsName}

def cyobservability_init(object exporter) -> None:
  exporter: _observability.Exporter

  NativeObservabilityInit()
  _start_exporting_thread(exporter)


def _start_exporting_thread(object exporter) -> None:
  exporter: _observability.Exporter

  global GLOBAL_EXPORT_THREAD
  global GLOBAL_SHUTDOWN_EXPORT_THREAD
  GLOBAL_SHUTDOWN_EXPORT_THREAD = False
  # TODO(xuanwn): Change it to daemon thread.
  GLOBAL_EXPORT_THREAD = Thread(target=_export_census_data, args=(exporter,))
  GLOBAL_EXPORT_THREAD.start()

def activate_config(object py_config) -> None:
  py_config: "_observability_config.GcpObservabilityConfig"

  if (py_config.tracing_enabled):
    EnablePythonCensusTracing(True);
    # Save sampling rate to global sampler.
    ProbabilitySampler.Get().SetThreshold(py_config.sampling_rate)

  if (py_config.stats_enabled):
    EnablePythonCensusStats(True);

def activate_stats() -> None:
  EnablePythonCensusStats(True);

def create_client_call_tracer(bytes method_name, bytes target, bytes trace_id, str identifier,
                              dict exchange_labels, object enabled_optional_labels,
                              bint registered_method, bytes parent_span_id=b'') -> cpython.PyObject:
  """Create a ClientCallTracer and save to PyCapsule.

  Returns: A grpc_observability._observability.ClientCallTracerCapsule object.
  """
  cdef char* c_method = cpython.PyBytes_AsString(method_name)
  cdef char* c_target = cpython.PyBytes_AsString(target)
  cdef char* c_trace_id = cpython.PyBytes_AsString(trace_id)
  cdef char* c_parent_span_id = cpython.PyBytes_AsString(parent_span_id)
  identifier_bytes = _encode(identifier)
  cdef char* c_identifier = cpython.PyBytes_AsString(identifier_bytes)
  cdef vector[Label] c_labels = _labels_to_c_labels(exchange_labels)
  cdef bint add_csm_optional_labels = False

  for label_type in enabled_optional_labels:
    if label_type == _observability.OptionalLabelType.XDS_SERVICE_LABELS:
      add_csm_optional_labels = True

  cdef void* call_tracer = CreateClientCallTracer(c_method, c_target, c_trace_id, c_parent_span_id,
                                                  c_identifier, c_labels, add_csm_optional_labels,
                                                  registered_method)
  capsule = cpython.PyCapsule_New(call_tracer, CLIENT_CALL_TRACER, NULL)
  return capsule


def create_server_call_tracer_factory_capsule(dict exchange_labels, str identifier) -> cpython.PyObject:
  """Create a ServerCallTracerFactory and save to PyCapsule.

  Returns: A grpc_observability._observability.ServerCallTracerFactoryCapsule object.
  """
  cdef vector[Label] c_labels = _labels_to_c_labels(exchange_labels)
  cdef char* c_identifier = cpython.PyBytes_AsString(_encode(identifier))
  cdef void* call_tracer_factory = CreateServerCallTracerFactory(c_labels, c_identifier)
  capsule = cpython.PyCapsule_New(call_tracer_factory, SERVER_CALL_TRACER_FACTORY, NULL)
  return capsule


def _c_label_to_labels(vector[Label] c_labels) -> Dict[str, AnyStr]:
  py_labels = {}
  for label in c_labels:
    py_labels[_decode(label.key)] = label.value
  return py_labels


def _labels_to_c_labels(dict py_labels) -> vector[Label]:
  cdef vector[Label] c_labels
  cdef Label label

  for key, value in py_labels.items():
      label.key = _encode(key)
      label.value = _encode(value)
      c_labels.push_back(label)

  return c_labels


def _c_measurement_to_measurement(object measurement
  ) -> Mapping[str, Union[enum, Mapping[str, Union[float, int], bool]]]:
  """Convert Cython Measurement to Python measurement.

  Args:
  measurement: Actual measurement repesented by Cython type Measurement, using object here
   since Cython refuse to automatically convert a union with unsafe type combinations.

  Returns:
   A mapping object with keys and values as following:
    name -> cMetricsName
    type -> MeasurementType
    registered_method -> bool
    include_exchange_labels -> bool
    value -> {value_double: float | value_int: int}
  """
  measurement: Measurement

  py_measurement = {}
  py_measurement['name'] = measurement['name']
  py_measurement['type'] = measurement['type']
  py_measurement['registered_method'] = measurement['registered_method']
  py_measurement['include_exchange_labels'] = measurement['include_exchange_labels']
  if measurement['type'] == kMeasurementDouble:
    py_measurement['value'] = {'value_double': measurement['value']['value_double']}
  else:
    py_measurement['value'] = {'value_int': measurement['value']['value_int']}
  return py_measurement


def _c_annotation_to_annotations(vector[Annotation] c_annotations) -> List[Tuple[str, str]]:
  py_annotations = []
  for annotation in c_annotations:
    py_annotations.append((_decode(annotation.time_stamp),
                          _decode(annotation.description)))
  return py_annotations


def observability_deinit() -> None:
  _shutdown_exporting_thread()
  EnablePythonCensusStats(False)
  EnablePythonCensusTracing(False)


@functools.lru_cache(maxsize=None)
def _cy_metric_name_to_py_metric_name(cMetricsName metric_name) -> MetricsName:
  try:
    return _CY_METRICS_NAME_TO_PY_METRICS_NAME_MAPPING[metric_name]
  except KeyError:
    raise ValueError('Invalid metric name %s' % metric_name)


def _get_stats_data(object measurement, object labels, object identifier) -> _observability.StatsData:
  """Convert a Python measurement to StatsData.

  Args:
  measurement: A dict of type Mapping[str, Union[enum, Mapping[str, Union[float, int]]]]
    with keys and values as following:
      name -> cMetricsName
      type -> MeasurementType
      registered_method -> bool
      include_exchange_labels -> bool
      value -> {value_double: float | value_int: int}
  labels: Labels assciociated with stats data with type of Mapping[str, AnyStr].
  identifier: Specifies the plugins associated with this stats data.
  """
  measurement: Measurement
  labels: Mapping[str, AnyStr]

  metric_name = _cy_metric_name_to_py_metric_name(measurement['name'])
  identifiers = set(identifier.split(PLUGIN_IDENTIFIER_SEP))
  if measurement['type'] == kMeasurementDouble:
    py_stat = _observability.StatsData(name=metric_name, measure_double=True,
                                       value_float=measurement['value']['value_double'],
                                       labels=labels,
                                       identifiers=identifiers,
                                       registered_method=measurement['registered_method'],
                                       include_exchange_labels=measurement['include_exchange_labels'],)
  else:
    py_stat = _observability.StatsData(name=metric_name, measure_double=False,
                                       value_int=measurement['value']['value_int'],
                                       labels=labels,
                                       identifiers=identifiers,
                                       registered_method=measurement['registered_method'],
                                       include_exchange_labels=measurement['include_exchange_labels'],)
  return py_stat


def _get_tracing_data(SpanCensusData span_data, vector[Label] span_labels,
                      vector[Annotation] span_annotations) -> _observability.TracingData:
  py_span_labels = _c_label_to_labels(span_labels)
  py_span_annotations = _c_annotation_to_annotations(span_annotations)
  return _observability.TracingData(name=_decode(span_data.name),
                                    start_time = _decode(span_data.start_time),
                                    end_time = _decode(span_data.end_time),
                                    trace_id = _decode(span_data.trace_id),
                                    span_id = _decode(span_data.span_id),
                                    parent_span_id = _decode(span_data.parent_span_id),
                                    status = _decode(span_data.status),
                                    should_sample = span_data.should_sample,
                                    child_span_count = span_data.child_span_count,
                                    span_labels = py_span_labels,
                                    span_annotations = py_span_annotations)


def _record_rpc_latency(object exporter, str method, str target, float rpc_latency,
                        str status_code, str identifier, bint registered_method) -> None:
  exporter: _observability.Exporter

  measurement = {}
  measurement['name'] = kRpcClientApiLatencyMeasureName
  measurement['type'] = kMeasurementDouble
  measurement['value'] = {'value_double': rpc_latency}
  measurement['registered_method'] = registered_method
  measurement['include_exchange_labels'] = False

  labels = {}
  labels[_decode(kClientMethod)] = method.strip("/")
  labels[_decode(kClientTarget)] = target
  labels[_decode(kClientStatus)] = status_code
  metric = _get_stats_data(measurement, labels, identifier)
  exporter.export_stats_data([metric])


cdef void _export_census_data(object exporter):
  """Main function running in export thread."""
  exporter: _observability.Exporter

  cdef int export_interval_ms = CENSUS_EXPORT_BATCH_INTERVAL_SECS * 1000
  while True:
    with nogil:
      while not GLOBAL_SHUTDOWN_EXPORT_THREAD:
        lk = new unique_lock[mutex](g_census_data_buffer_mutex)
        # Wait for next batch of census data OR timeout at fixed interval.
        # Batch export census data to minimize the time we acquiring the GIL.
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

  # Flush one last time before shutdown thread
  _flush_census_data(exporter)


cdef void _flush_census_data(object exporter):
  exporter: _observability.Exporter

  lk = new unique_lock[mutex](g_census_data_buffer_mutex)
  if g_census_data_buffer.empty():
    del lk
    return
  py_metrics_batch = []
  py_spans_batch = []
  while not g_census_data_buffer.empty():
    c_census_data = g_census_data_buffer.front()
    if c_census_data.type == kMetricData:
      py_labels = _c_label_to_labels(c_census_data.labels)
      py_identifier = _decode(c_census_data.identifier)
      py_measurement = _c_measurement_to_measurement(c_census_data.measurement_data)
      py_metric = _get_stats_data(py_measurement, py_labels, py_identifier)
      py_metrics_batch.append(py_metric)
    else:
      py_span = _get_tracing_data(c_census_data.span_data, c_census_data.span_data.span_labels,
                                  c_census_data.span_data.span_annotations)
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
  GLOBAL_EXPORT_THREAD.join(timeout=GRPC_PYTHON_CENSUS_EXPORT_THREAD_TIMEOUT)


cdef str _decode(bytes bytestring):
  if isinstance(bytestring, (str,)):
    return <str>bytestring
  else:
    try:
      return bytestring.decode('utf8')
    except UnicodeDecodeError:
      _LOGGER.exception('Invalid encoding on %s', bytestring)
      return bytestring.decode('latin1')


cdef bytes _encode(object string_or_none):
  if string_or_none is None:
    return b''
  elif isinstance(string_or_none, (bytes,)):
    return <bytes>string_or_none
  elif isinstance(string_or_none, (unicode,)):
    return string_or_none.encode('utf8')
  else:
    raise TypeError('Expected str, not {}'.format(type(string_or_none)))
