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

from __future__ import annotations

import abc
from datetime import datetime
import logging
import sys
from typing import Any, AnyStr, List, Mapping, Optional, Tuple

from google.rpc import code_pb2
import grpc_observability
from grpc_observability import _measures
from grpc_observability import _views
from opencensus.stats import stats as stats_module
from opencensus.tags.tag_key import TagKey
from opencensus.tags.tag_map import TagMap
from opencensus.tags.tag_value import TagValue
from opencensus.trace import execution_context
from opencensus.trace import samplers
from opencensus.trace import span_context as span_context_module
from opencensus.trace import span_data as span_data_module
from opencensus.trace import time_event
from opencensus.trace import trace_options as trace_options_module
from opencensus.trace.span import SpanKind
from opencensus.trace.status import Status
from opencensus.trace.tracer import Tracer

logger = logging.getLogger(__name__)

VIEW_NAMES = [
    "grpc.io/client/started_rpcs",
    #   "grpc.io/client/completed_rpcs",
    #   "grpc.io/client/roundtrip_latency",
    #   "grpc.io/client/sent_compressed_message_bytes_per_rpc",
    #   "grpc.io/client/received_compressed_message_bytes_per_rpc",
    #   "grpc.io/server/started_rpcs",
    #   "grpc.io/server/completed_rpcs",
    #   "grpc.io/server/sent_compressed_message_bytes_per_rpc",
    #   "grpc.io/server/received_compressed_message_bytes_per_rpc",
    #   "grpc.io/server/server_latency"
]

METRICS_NAME_TO_MEASURE = {
    grpc_observability.MetricsName.CLIENT_STARTED_RPCS:
        _measures.rpc_client_started_rpcs(),
    grpc_observability.MetricsName.CLIENT_ROUNDTRIP_LATENCY:
        _measures.rpc_client_roundtrip_latency(),
    grpc_observability.MetricsName.CLIENT_API_LATENCY:
        _measures.rpc_client_api_latency(),
    grpc_observability.MetricsName.CLIENT_SEND_BYTES_PER_RPC:
        _measures.rpc_client_send_bytes_per_prc(),
    grpc_observability.MetricsName.CLIENT_RECEIVED_BYTES_PER_RPC:
        _measures.rpc_client_received_bytes_per_rpc(),
    grpc_observability.MetricsName.SERVER_STARTED_RPCS:
        _measures.rpc_server_started_rpcs(),
    grpc_observability.MetricsName.SERVER_SENT_BYTES_PER_RPC:
        _measures.rpc_server_sent_bytes_per_rpc(),
    grpc_observability.MetricsName.SERVER_RECEIVED_BYTES_PER_RPC:
        _measures.rpc_server_received_bytes_per_rpc(),
    grpc_observability.MetricsName.SERVER_SERVER_LATENCY:
        _measures.rpc_server_server_latency(),
}


class OpenCensusExporter(grpc_observability.Exporter):
    default_labels: Mapping[str, str]

    def __init__(self, labels: Optional[Mapping[str, str]] = None):
        self.default_labels = labels
        view_manager = stats_module.stats.view_manager
        self._register_open_census_views(view_manager)

    def _register_open_census_views(self, view_manager) -> None:
        # Client
        view_manager.register_view(
            _views.client_started_rpcs(self.default_labels))
        view_manager.register_view(
            _views.client_completed_rpcs(self.default_labels))
        view_manager.register_view(
            _views.client_roundtrip_latency(self.default_labels))
        view_manager.register_view(
            _views.client_api_latency(self.default_labels))
        view_manager.register_view(
            _views.client_sent_compressed_message_bytes_per_rpc(
                self.default_labels))
        view_manager.register_view(
            _views.client_received_compressed_message_bytes_per_rpc(
                self.default_labels))

        # Server
        view_manager.register_view(
            _views.server_started_rpcs(self.default_labels))
        view_manager.register_view(
            _views.server_completed_rpcs(self.default_labels))
        view_manager.register_view(
            _views.server_sent_compressed_message_bytes_per_rpc(
                self.default_labels))
        view_manager.register_view(
            _views.server_received_compressed_message_bytes_per_rpc(
                self.default_labels))
        view_manager.register_view(
            _views.server_server_latency(self.default_labels))

    def export_stats_data(
            self, stats_data: List[grpc_observability.StatsData]) -> None:
        stats = stats_module.stats
        stats_recorder = stats.stats_recorder
        mmap = stats_recorder.new_measurement_map()

        for metric in stats_data:
            measure = METRICS_NAME_TO_MEASURE.get(metric.name, None)
            if measure is None:
                continue

            labels = metric.labels
            labels.update(self.default_labels)
            tag_map = TagMap()
            for key, value in labels.items():
                tag_map.insert(TagKey(key), TagValue(value))

            if metric.measure_double:
                sys.stderr.write(
                    f"---->>> Metric name:{measure.name}, value: {metric.value_float}, tags: {tag_map.map}\n"
                )
                sys.stderr.flush()
                mmap.measure_float_put(measure, metric.value_float)
            else:
                sys.stderr.write(
                    f"---->>> Metric name:{measure.name}, value: {metric.value_int}, tags: {tag_map.map}\n"
                )
                sys.stderr.flush()
                mmap.measure_int_put(measure, metric.value_int)

            mmap.record(tag_map)

    def export_tracing_data(
            self, tracing_data: List[grpc_observability.TracingData]) -> None:
        for span_data in tracing_data:
            span_context = span_context_module.SpanContext(
                trace_id=span_data.trace_id,
                span_id=span_data.span_id,
                trace_options=trace_options_module.TraceOptions(1))
            span_data = _get_span_datas(span_data, span_context,
                                        self.default_labels)
            sys.stderr.write(f"---->>> Span: {span_data}\n")

    # def set_tracer(self) -> None:
    #     current_tracer = execution_context.get_opencensus_tracer()
    #     trace_id = current_tracer.span_context.trace_id
    #     span_id = current_tracer.span_context.span_id
    #     if not span_id:
    #         span_id = span_context_module.generate_span_id()
    #     span_context = span_context_module.SpanContext(trace_id=trace_id,
    #                                                    span_id=span_id)
    #     # Create and Saves Tracer and Sampler to ContextVar
    #     sampler = samplers.ProbabilitySampler(rate=self.sampling_rate)
    #     # TODO: check existing SpanContext
    #     tracer = Tracer(sampler=sampler, span_context=span_context)


def _get_span_annotations(
        span_annotations: List[Tuple[bytes,
                                     bytes]]) -> List[time_event.Annotation]:
    annotations = []

    for time_stamp, description in span_annotations:
        time = datetime.fromisoformat(time_stamp)
        annotations.append(time_event.Annotation(time, description))

    return annotations


def _status_to_span_status(status: str) -> Optional[Status]:
    if status == "OK":
        return Status(code_pb2.OK, message=status)
    elif status == "CANCELLED":
        return Status(code_pb2.CANCELLED, message=status)
    elif status == "UNKNOWN":
        return Status(code_pb2.UNKNOWN, message=status)
    elif status == "INVALID_ARGUMENT":
        return Status(code_pb2.INVALID_ARGUMENT, message=status)
    elif status == "DEADLINE_EXCEEDED":
        return Status(code_pb2.DEADLINE_EXCEEDED, message=status)
    elif status == "NOT_FOUND":
        return Status(code_pb2.NOT_FOUND, message=status)
    elif status == "ALREADY_EXISTS":
        return Status(code_pb2.ALREADY_EXISTS, message=status)
    elif status == "PERMISSION_DENIED":
        return Status(code_pb2.PERMISSION_DENIED, message=status)
    elif status == "UNAUTHENTICATED":
        return Status(code_pb2.UNAUTHENTICATED, message=status)
    elif status == "RESOURCE_EXHAUSTED":
        return Status(code_pb2.RESOURCE_EXHAUSTED, message=status)
    elif status == "FAILED_PRECONDITION":
        return Status(code_pb2.FAILED_PRECONDITION, message=status)
    elif status == "ABORTED":
        return Status(code_pb2.ABORTED, message=status)
    elif status == "OUT_OF_RANGE":
        return Status(code_pb2.OUT_OF_RANGE, message=status)
    elif status == "UNIMPLEMENTED":
        return Status(code_pb2.UNIMPLEMENTED, message=status)
    elif status == "INTERNAL":
        return Status(code_pb2.INTERNAL, message=status)
    elif status == "UNAVAILABLE":
        return Status(code_pb2.UNAVAILABLE, message=status)
    elif status == "DATA_LOSS":
        return Status(code_pb2.DATA_LOSS, message=status)
    else:
        return None


def _get_span_datas(span_data, span_context, labels: Mapping[str, str]):
    """Extracts a list of SpanData tuples from a span

    :rtype: list of opencensus.trace.span_data.SpanData
    :return list of SpanData tuples
    """
    span_attributes = span_data.span_labels
    span_attributes.update(labels)
    span_status = _status_to_span_status(span_data.status)
    span_annotations = _get_span_annotations(span_data.span_annotations)
    span_datas = [
        span_data_module.SpanData(name=span_data.name,
                                  context=span_context,
                                  span_id=span_data.span_id,
                                  parent_span_id=span_data.parent_span_id
                                  if span_data.parent_span_id else None,
                                  attributes=span_attributes,
                                  start_time=span_data.start_time,
                                  end_time=span_data.end_time,
                                  child_span_count=span_data.child_span_count,
                                  stack_trace=None,
                                  annotations=span_annotations,
                                  message_events=None,
                                  links=None,
                                  status=span_status,
                                  same_process_as_parent_span=True
                                  if span_data.parent_span_id else None,
                                  span_kind=SpanKind.UNSPECIFIED)
    ]

    return span_datas
