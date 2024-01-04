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

from datetime import datetime
import os
from typing import List, Mapping, Optional, Tuple

from google.rpc import code_pb2
from grpc_observability import _observability  # pytype: disable=pyi-error
from grpc_observability import _observability_config
from grpc_observability import _views
from opencensus.common.transports import async_
from opencensus.ext.stackdriver import stats_exporter
from opencensus.ext.stackdriver import trace_exporter
from opencensus.stats import stats as stats_module
from opencensus.stats.stats_recorder import StatsRecorder
from opencensus.stats.view_manager import ViewManager
from opencensus.tags.tag_key import TagKey
from opencensus.tags.tag_map import TagMap
from opencensus.tags.tag_value import TagValue
from opencensus.trace import execution_context
from opencensus.trace import samplers
from opencensus.trace import span
from opencensus.trace import span_context as span_context_module
from opencensus.trace import span_data as span_data_module
from opencensus.trace import status
from opencensus.trace import time_event
from opencensus.trace import trace_options
from opencensus.trace import tracer

# 60s is the default time for open census to call export.
CENSUS_UPLOAD_INTERVAL_SECS = int(
    os.environ.get("GRPC_PYTHON_CENSUS_EXPORT_UPLOAD_INTERVAL_SECS", 20)
)


class StackDriverAsyncTransport(async_.AsyncTransport):
    """Wrapper class used to pass wait_period.

    This is required because current StackDriver Tracing Exporter doesn't allow
    us pass wait_period to AsyncTransport directly.

    Args:
      exporter: An opencensus.trace.base_exporter.Exporter object.
    """

    def __init__(self, exporter):
        super().__init__(exporter, wait_period=CENSUS_UPLOAD_INTERVAL_SECS)


class OpenCensusExporter(_observability.Exporter):
    config: _observability_config.GcpObservabilityConfig
    default_labels: Optional[Mapping[str, str]]
    project_id: str
    tracer: Optional[tracer.Tracer]
    stats_recorder: Optional[StatsRecorder]
    view_manager: Optional[ViewManager]

    def __init__(self, config: _observability_config.GcpObservabilityConfig):
        self.config = config.get()
        self.default_labels = self.config.labels
        self.project_id = self.config.project_id
        self.tracer = None
        self.stats_recorder = None
        self.view_manager = None
        self._setup_open_census_stackdriver_exporter()

    def _setup_open_census_stackdriver_exporter(self) -> None:
        if self.config.stats_enabled:
            stats = stats_module.stats
            self.stats_recorder = stats.stats_recorder
            self.view_manager = stats.view_manager
            # If testing locally please add resource="global" to Options, otherwise
            # StackDriver might override project_id based on detected resource.
            options = stats_exporter.Options(project_id=self.project_id)
            metrics_exporter = stats_exporter.new_stats_exporter(
                options, interval=CENSUS_UPLOAD_INTERVAL_SECS
            )
            self.view_manager.register_exporter(metrics_exporter)
            self._register_open_census_views()

        if self.config.tracing_enabled:
            current_tracer = execution_context.get_opencensus_tracer()
            trace_id = current_tracer.span_context.trace_id
            span_id = current_tracer.span_context.span_id
            if not span_id:
                span_id = span_context_module.generate_span_id()
            span_context = span_context_module.SpanContext(
                trace_id=trace_id, span_id=span_id
            )
            # Create and Saves Tracer and Sampler to ContextVar
            sampler = samplers.ProbabilitySampler(
                rate=self.config.sampling_rate
            )
            self.trace_exporter = trace_exporter.StackdriverExporter(
                project_id=self.project_id,
                transport=StackDriverAsyncTransport,
            )
            self.tracer = tracer.Tracer(
                sampler=sampler,
                span_context=span_context,
                exporter=self.trace_exporter,
            )

    def export_stats_data(
        self, stats_data: List[_observability.StatsData]
    ) -> None:
        if not self.config.stats_enabled:
            return
        for data in stats_data:
            measure = _views.METRICS_NAME_TO_MEASURE.get(data.name, None)
            if not measure:
                continue
            # Create a measurement map for each metric, otherwise metrics will
            # be override instead of accumulate.
            measurement_map = self.stats_recorder.new_measurement_map()
            # Add data label to default labels.
            labels = data.labels
            labels.update(self.default_labels)
            tag_map = TagMap()
            for key, value in labels.items():
                tag_map.insert(TagKey(key), TagValue(value))

            if data.measure_double:
                measurement_map.measure_float_put(measure, data.value_float)
            else:
                measurement_map.measure_int_put(measure, data.value_int)
            measurement_map.record(tag_map)

    def export_tracing_data(
        self, tracing_data: List[_observability.TracingData]
    ) -> None:
        if not self.config.tracing_enabled:
            return
        for span_data in tracing_data:
            # Only traced data will be exported, thus TraceOptions=1.
            span_context = span_context_module.SpanContext(
                trace_id=span_data.trace_id,
                span_id=span_data.span_id,
                trace_options=trace_options.TraceOptions(1),
            )
            span_datas = _get_span_data(
                span_data, span_context, self.default_labels
            )
            self.trace_exporter.export(span_datas)

    def _register_open_census_views(self) -> None:
        # Client
        self.view_manager.register_view(
            _views.client_started_rpcs(self.default_labels)
        )
        self.view_manager.register_view(
            _views.client_completed_rpcs(self.default_labels)
        )
        self.view_manager.register_view(
            _views.client_roundtrip_latency(self.default_labels)
        )
        self.view_manager.register_view(
            _views.client_api_latency(self.default_labels)
        )
        self.view_manager.register_view(
            _views.client_sent_compressed_message_bytes_per_rpc(
                self.default_labels
            )
        )
        self.view_manager.register_view(
            _views.client_received_compressed_message_bytes_per_rpc(
                self.default_labels
            )
        )

        # Server
        self.view_manager.register_view(
            _views.server_started_rpcs(self.default_labels)
        )
        self.view_manager.register_view(
            _views.server_completed_rpcs(self.default_labels)
        )
        self.view_manager.register_view(
            _views.server_sent_compressed_message_bytes_per_rpc(
                self.default_labels
            )
        )
        self.view_manager.register_view(
            _views.server_received_compressed_message_bytes_per_rpc(
                self.default_labels
            )
        )
        self.view_manager.register_view(
            _views.server_server_latency(self.default_labels)
        )


def _get_span_annotations(
    span_annotations: List[Tuple[str, str]]
) -> List[time_event.Annotation]:
    annotations = []

    for time_stamp, description in span_annotations:
        time = datetime.fromisoformat(time_stamp)
        annotations.append(time_event.Annotation(time, description))

    return annotations


# pylint: disable=too-many-return-statements
# pylint: disable=too-many-branches
def _status_to_span_status(span_status: str) -> Optional[status.Status]:
    if status == "OK":
        return status.Status(code_pb2.OK, message=span_status)
    elif status == "CANCELLED":
        return status.Status(code_pb2.CANCELLED, message=span_status)
    elif status == "UNKNOWN":
        return status.Status(code_pb2.UNKNOWN, message=span_status)
    elif status == "INVALID_ARGUMENT":
        return status.Status(code_pb2.INVALID_ARGUMENT, message=span_status)
    elif status == "DEADLINE_EXCEEDED":
        return status.Status(code_pb2.DEADLINE_EXCEEDED, message=span_status)
    elif status == "NOT_FOUND":
        return status.Status(code_pb2.NOT_FOUND, message=span_status)
    elif status == "ALREADY_EXISTS":
        return status.Status(code_pb2.ALREADY_EXISTS, message=span_status)
    elif status == "PERMISSION_DENIED":
        return status.Status(code_pb2.PERMISSION_DENIED, message=span_status)
    elif status == "UNAUTHENTICATED":
        return status.Status(code_pb2.UNAUTHENTICATED, message=span_status)
    elif status == "RESOURCE_EXHAUSTED":
        return status.Status(code_pb2.RESOURCE_EXHAUSTED, message=span_status)
    elif status == "FAILED_PRECONDITION":
        return status.Status(code_pb2.FAILED_PRECONDITION, message=span_status)
    elif status == "ABORTED":
        return status.Status(code_pb2.ABORTED, message=span_status)
    elif status == "OUT_OF_RANGE":
        return status.Status(code_pb2.OUT_OF_RANGE, message=span_status)
    elif status == "UNIMPLEMENTED":
        return status.Status(code_pb2.UNIMPLEMENTED, message=span_status)
    elif status == "INTERNAL":
        return status.Status(code_pb2.INTERNAL, message=span_status)
    elif status == "UNAVAILABLE":
        return status.Status(code_pb2.UNAVAILABLE, message=span_status)
    elif status == "DATA_LOSS":
        return status.Status(code_pb2.DATA_LOSS, message=span_status)
    else:
        return None


def _get_span_data(
    span_data: _observability.TracingData,
    span_context: span_context_module.SpanContext,
    labels: Mapping[str, str],
) -> List[span_data_module.SpanData]:
    """Extracts a list of SpanData tuples from a span.

    Args:
    span_data: _observability.TracingData to convert.
    span_context: The context related to the span_data.
    labels: Labels to be added to SpanData.

    Returns:
    A list of opencensus.trace.span_data.SpanData.
    """
    span_attributes = span_data.span_labels
    span_attributes.update(labels)
    span_status = _status_to_span_status(span_data.status)
    span_annotations = _get_span_annotations(span_data.span_annotations)
    span_datas = [
        span_data_module.SpanData(
            name=span_data.name,
            context=span_context,
            span_id=span_data.span_id,
            parent_span_id=span_data.parent_span_id
            if span_data.parent_span_id
            else None,
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
            if span_data.parent_span_id
            else None,
            span_kind=span.SpanKind.UNSPECIFIED,
        )
    ]

    return span_datas
