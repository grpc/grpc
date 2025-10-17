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

import logging
import threading
import time
from typing import Any, AnyStr, Dict, Iterable, List, Optional, Set, Tuple, Union

import grpc

# pytype: disable=pyi-error
from grpc_observability import _cyobservability
from grpc_observability import _observability
from grpc_observability import _open_telemetry_measures
from grpc_observability._cyobservability import MetricsName
from grpc_observability._cyobservability import PLUGIN_IDENTIFIER_SEP
from grpc_observability._observability import OptionalLabelType
from grpc_observability._observability import TracingData
from grpc_observability._observability import StatsData
from opentelemetry import trace
from opentelemetry.context.context import Context
from opentelemetry.metrics import Counter
from opentelemetry.metrics import Histogram
from opentelemetry.metrics import Meter
from opentelemetry.sdk.trace import IdGenerator, Tracer
from opentelemetry.sdk.trace.id_generator import RandomIdGenerator
from opentelemetry.trace.propagation.tracecontext import TraceContextTextMapPropagator

_LOGGER = logging.getLogger(__name__)

ClientCallTracerCapsule = Any  # it appears only once in the function signature
ServerCallTracerFactoryCapsule = (
    Any  # it appears only once in the function signature
)
grpc_observability = Any  # grpc_observability.py imports this module.
OpenTelemetryPlugin = Any  # _open_telemetry_plugin.py imports this module.
OpenTelemetryPluginOption = (
    Any  # _open_telemetry_plugin.py imports this module.
)

GRPC_METHOD_LABEL = "grpc.method"
GRPC_TARGET_LABEL = "grpc.target"
GRPC_CLIENT_METRIC_PREFIX = "grpc.client"
GRPC_OTHER_LABEL_VALUE = "other"
_observability_lock: threading.RLock = threading.RLock()
_OPEN_TELEMETRY_OBSERVABILITY: Optional["OpenTelemetryObservability"] = None

GRPC_STATUS_CODE_TO_STRING = {
    grpc.StatusCode.OK: "OK",
    grpc.StatusCode.CANCELLED: "CANCELLED",
    grpc.StatusCode.UNKNOWN: "UNKNOWN",
    grpc.StatusCode.INVALID_ARGUMENT: "INVALID_ARGUMENT",
    grpc.StatusCode.DEADLINE_EXCEEDED: "DEADLINE_EXCEEDED",
    grpc.StatusCode.NOT_FOUND: "NOT_FOUND",
    grpc.StatusCode.ALREADY_EXISTS: "ALREADY_EXISTS",
    grpc.StatusCode.PERMISSION_DENIED: "PERMISSION_DENIED",
    grpc.StatusCode.UNAUTHENTICATED: "UNAUTHENTICATED",
    grpc.StatusCode.RESOURCE_EXHAUSTED: "RESOURCE_EXHAUSTED",
    grpc.StatusCode.FAILED_PRECONDITION: "FAILED_PRECONDITION",
    grpc.StatusCode.ABORTED: "ABORTED",
    grpc.StatusCode.OUT_OF_RANGE: "OUT_OF_RANGE",
    grpc.StatusCode.UNIMPLEMENTED: "UNIMPLEMENTED",
    grpc.StatusCode.INTERNAL: "INTERNAL",
    grpc.StatusCode.UNAVAILABLE: "UNAVAILABLE",
    grpc.StatusCode.DATA_LOSS: "DATA_LOSS",
}


class _FixedIdGenerator(IdGenerator):
    """IdGenerator child class to handle correctly span ID propagation.

    Span IDs are generated in C context anyway. In Python we just need to use those.
    """
    def __init__(self, trace_id: str, span_id: str):
        self._trace_id = int(trace_id, 16)
        self._span_id = int(span_id, 16)

    def generate_span_id(self) -> int:
        return self._span_id
      
    def generate_trace_id(self) -> int:
        return self._trace_id


class _OpenTelemetryPlugin:
    _plugin: OpenTelemetryPlugin
    _metric_to_recorder: Dict[MetricsName, Union[Counter, Histogram]]
    _tracer: Optional[Tracer]
    _trace_ctx: Optional[Context]
    _text_map_propagator: Optional[TraceContextTextMapPropagator]
    _enabled_client_plugin_options: Optional[List[OpenTelemetryPluginOption]]
    _enabled_server_plugin_options: Optional[List[OpenTelemetryPluginOption]]
    identifier: str

    def __init__(self, plugin: OpenTelemetryPlugin):
        self._plugin = plugin
        self._metric_to_recorder = {}
        self._tracer = None
        self._trace_ctx = None
        self._text_map_propagator = None
        self.identifier = str(id(self))
        self._enabled_client_plugin_options = None
        self._enabled_server_plugin_options = None

        meter_provider = self._plugin.meter_provider
        if meter_provider:
            meter = meter_provider.get_meter("grpc-python", grpc.__version__)
            enabled_metrics = _open_telemetry_measures.base_metrics()
            self._metric_to_recorder = self._register_metrics(
                meter, enabled_metrics
            )

        tracer_provider = self._plugin.tracer_provider
        text_map_propagator = self._plugin.text_map_propagator
        if tracer_provider and text_map_propagator:
            self._tracer = tracer_provider.get_tracer("grpc-python", grpc.__version__)
            self._text_map_propagator = text_map_propagator

    def _should_record(self, stats_data: StatsData) -> bool:
        # Decide if this plugin should record the stats_data.
        return stats_data.name in self._metric_to_recorder

    def _record_stats_data(self, stats_data: StatsData) -> None:
        recorder = self._metric_to_recorder[stats_data.name]
        enabled_plugin_options = []
        if GRPC_CLIENT_METRIC_PREFIX in recorder.name:
            enabled_plugin_options = self._enabled_client_plugin_options
        else:
            enabled_plugin_options = self._enabled_server_plugin_options
        # Only deserialize labels if we need add exchanged labels.
        if stats_data.include_exchange_labels:
            deserialized_labels = self._deserialize_labels(
                stats_data.labels, enabled_plugin_options
            )
        else:
            deserialized_labels = stats_data.labels
        labels = self._maybe_add_labels(
            stats_data.include_exchange_labels,
            deserialized_labels,
            enabled_plugin_options,
        )
        decoded_labels = self.decode_labels(labels)

        target = decoded_labels.get(GRPC_TARGET_LABEL, "")
        if not self._plugin.target_attribute_filter(target):
            # Filter target name.
            decoded_labels[GRPC_TARGET_LABEL] = GRPC_OTHER_LABEL_VALUE

        method = decoded_labels.get(GRPC_METHOD_LABEL, "")
        if not (
            stats_data.registered_method
            or self._plugin.generic_method_attribute_filter(method)
        ):
            # Filter method name if it's not registered method and
            # generic_method_attribute_filter returns false.
            decoded_labels[GRPC_METHOD_LABEL] = GRPC_OTHER_LABEL_VALUE

        value = 0
        if stats_data.measure_double:
            value = stats_data.value_float
        else:
            value = stats_data.value_int
        if isinstance(recorder, Counter):
            recorder.add(value, attributes=decoded_labels)
        elif isinstance(recorder, Histogram):
            recorder.record(value, attributes=decoded_labels)

    def maybe_record_stats_data(self, stats_data: StatsData) -> None:
        # Records stats data to MeterProvider.
        if self._should_record(stats_data):
            self._record_stats_data(stats_data)

    def is_tracing_configured(self) -> bool:
        return self._tracer is not None and self._text_map_propagator is not None

    def save_trace_context(
        self, trace_id: str, span_id: str, is_sampled: bool, version_id: str = "00"
    ) -> None:
        if self.is_tracing_configured():
            # Header formatting as per https://www.w3.org/TR/trace-context/#traceparent-header
            traceparent = f"{version_id}-{trace_id}-{span_id}-{is_sampled:02x}"
            self._trace_ctx = self._text_map_propagator.extract(
                carrier={"traceparent": traceparent},
                context=self._trace_ctx
            )

    def _status_to_otel_status(self, status: str) -> trace.status.Status:
        return trace.status.Status(
            status_code=trace.status.StatusCode.OK if status == "OK"
                                                   else trace.status.StatusCode.ERROR,
            description=None if status == "OK" else status
        )

    def _record_tracing_data(self, tracing_data: TracingData) -> None:
        """Exports tracing data gathered in core library in batches.

        We need to transpose from `TracingData` object to `trace.ReadableSpan` object here. However,
        instantiating objects of `trace.ReadableSpan` type is restricted in OpenTelemetry framework.
        Therefore, we need to manually create spans and take care of parent-to-child relationships
        using custom `trace.IdGenerator`. With this approach IDs are propagated correctly to
        configured exporter instance.
        """
        # this step is needed to propagate parent span ID correctly
        self.save_trace_context(
            trace_id=tracing_data.trace_id,
            span_id=tracing_data.parent_span_id,
            is_sampled=tracing_data.should_sample
        )

        # this step is needed to propagate span ID correctly
        self._tracer.id_generator = _FixedIdGenerator(
            trace_id=tracing_data.trace_id,
            span_id=tracing_data.span_id
        )

        span = self._tracer.start_span(
            name=tracing_data.name,
            context=self._trace_ctx,
            kind=trace.SpanKind.INTERNAL,
            attributes=tracing_data.span_labels,
            links=None,
            start_time=tracing_data.start_time
        )
        for event in tracing_data.span_events:
            span.add_event(
                name=event["name"],
                attributes=event["attributes"],
                timestamp=event["time_stamp"]
            )
        span.set_status(self._status_to_otel_status(tracing_data.status))
        span.end(end_time=tracing_data.end_time)

    def maybe_record_tracing_data(
        self, tracing_data: List[_observability.TracingData]
    ) -> None:
        """Records tracing data to SpanExporter configured for given TracerProvider."""
        if self.is_tracing_configured():
            id_generator = self._tracer.id_generator
            for data in tracing_data:
                self._record_tracing_data(data)
            # restore original `id_generator` after data collection
            self._tracer.id_generator = id_generator

    def get_client_exchange_labels(self) -> Dict[str, AnyStr]:
        """Get labels used for client side Metadata Exchange."""
        labels_for_exchange = {}
        for plugin_option in self._enabled_client_plugin_options:
            if hasattr(plugin_option, "get_label_injector") and hasattr(
                plugin_option.get_label_injector(), "get_labels_for_exchange"
            ):
                labels_for_exchange.update(
                    plugin_option.get_label_injector().get_labels_for_exchange()
                )
        return labels_for_exchange

    def get_server_exchange_labels(self) -> Dict[str, str]:
        """Get labels used for server side Metadata Exchange."""
        labels_for_exchange = {}
        for plugin_option in self._enabled_server_plugin_options:
            if hasattr(plugin_option, "get_label_injector") and hasattr(
                plugin_option.get_label_injector(), "get_labels_for_exchange"
            ):
                labels_for_exchange.update(
                    plugin_option.get_label_injector().get_labels_for_exchange()
                )
        return labels_for_exchange

    def activate_client_plugin_options(self, target: bytes) -> None:
        """Activate client plugin options based on option settings."""
        target_str = target.decode("utf-8", "replace")
        if not self._enabled_client_plugin_options:
            self._enabled_client_plugin_options = []
            for plugin_option in self._plugin.plugin_options:
                if hasattr(
                    plugin_option, "is_active_on_client_channel"
                ) and plugin_option.is_active_on_client_channel(target_str):
                    self._enabled_client_plugin_options.append(plugin_option)

    def activate_server_plugin_options(self, xds: bool) -> None:
        """Activate server plugin options based on option settings."""
        if not self._enabled_server_plugin_options:
            self._enabled_server_plugin_options = []
            for plugin_option in self._plugin.plugin_options:
                if hasattr(
                    plugin_option, "is_active_on_server"
                ) and plugin_option.is_active_on_server(xds):
                    self._enabled_server_plugin_options.append(plugin_option)

    @staticmethod
    def _deserialize_labels(
        labels: Dict[str, AnyStr],
        enabled_plugin_options: List[OpenTelemetryPluginOption],
    ) -> Dict[str, AnyStr]:
        for plugin_option in enabled_plugin_options:
            if all(
                [
                    hasattr(plugin_option, "get_label_injector"),
                    hasattr(
                        plugin_option.get_label_injector(), "deserialize_labels"
                    ),
                ]
            ):
                labels = plugin_option.get_label_injector().deserialize_labels(
                    labels
                )
        return labels

    @staticmethod
    def _maybe_add_labels(
        include_exchange_labels: bool,
        labels: Dict[str, str],
        enabled_plugin_options: List[OpenTelemetryPluginOption],
    ) -> Dict[str, AnyStr]:
        for plugin_option in enabled_plugin_options:
            if all(
                [
                    hasattr(plugin_option, "get_label_injector"),
                    hasattr(
                        plugin_option.get_label_injector(),
                        "get_additional_labels",
                    ),
                ]
            ):
                labels.update(
                    plugin_option.get_label_injector().get_additional_labels(
                        include_exchange_labels
                    )
                )
        return labels

    def get_enabled_optional_labels(self) -> List[OptionalLabelType]:
        return self._plugin._get_enabled_optional_labels()

    @staticmethod
    def _register_metrics(
        meter: Meter, metrics: List[_open_telemetry_measures.Metric]
    ) -> Dict[MetricsName, Union[Counter, Histogram]]:
        metric_to_recorder_map = {}
        recorder = None
        for metric in metrics:
            if metric == _open_telemetry_measures.CLIENT_ATTEMPT_STARTED:
                recorder = meter.create_counter(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric in (
                _open_telemetry_measures.CLIENT_ATTEMPT_DURATION,
                _open_telemetry_measures.CLIENT_RPC_DURATION,
                _open_telemetry_measures.CLIENT_ATTEMPT_SEND_BYTES,
                _open_telemetry_measures.CLIENT_ATTEMPT_RECEIVED_BYTES,
            ):
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.SERVER_STARTED_RPCS:
                recorder = meter.create_counter(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric in (
                _open_telemetry_measures.SERVER_RPC_DURATION,
                _open_telemetry_measures.SERVER_RPC_SEND_BYTES,
                _open_telemetry_measures.SERVER_RPC_RECEIVED_BYTES,
            ):
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            metric_to_recorder_map[metric.cyname] = recorder
        return metric_to_recorder_map

    @staticmethod
    def decode_labels(labels: Dict[str, AnyStr]) -> Dict[str, str]:
        decoded_labels = {}
        for key, value in labels.items():
            if isinstance(value, bytes):
                value = value.decode()
            decoded_labels[key] = value
        return decoded_labels


def start_open_telemetry_observability(
    *,
    plugins: Iterable[_OpenTelemetryPlugin],
) -> None:
    _start_open_telemetry_observability(
        OpenTelemetryObservability(plugins=plugins)
    )


def end_open_telemetry_observability() -> None:
    _end_open_telemetry_observability()


class _OpenTelemetryExporterDelegator(_observability.Exporter):
    _plugins: Iterable[_OpenTelemetryPlugin]

    def __init__(self, plugins: Iterable[_OpenTelemetryPlugin]):
        self._plugins = plugins

    def export_stats_data(
        self, stats_data: List[_observability.StatsData]
    ) -> None:
        # Records stats data to MeterProvider.
        for data in stats_data:
            for plugin in self._plugins:
                plugin.maybe_record_stats_data(data)

    def export_tracing_data(
        self, tracing_data: List[_observability.TracingData]
    ) -> None:
        for plugin in self._plugins:
            plugin.maybe_record_tracing_data(tracing_data)


# pylint: disable=no-self-use
class OpenTelemetryObservability(grpc._observability.ObservabilityPlugin):
    """OpenTelemetry based plugin implementation.

    This is class is part of an EXPERIMENTAL API.

    Args:
      plugins: _OpenTelemetryPlugins to enable.
    """

    _exporter: "grpc_observability.Exporter"
    _plugins: List[_OpenTelemetryPlugin]
    _registered_methods: Set[bytes]
    _client_option_activated: bool
    _server_option_activated: bool

    def __init__(
        self,
        *,
        plugins: Iterable[_OpenTelemetryPlugin],
    ):
        self._exporter = _OpenTelemetryExporterDelegator(plugins)
        self._registered_methods = set()
        self._plugins = list(plugins)
        self._client_option_activated = False
        self._server_option_activated = False

    def observability_init(self):
        try:
            _cyobservability.activate_stats()
            self.set_stats(True)
        except Exception as e:  # pylint: disable=broad-except
            error_msg = f"Activate observability metrics failed with: {e}"
            raise ValueError(error_msg)

        if self._should_enable_tracing():
            try:
                _cyobservability.activate_tracing()
                self.set_tracing(True)
            except Exception as e:  # pylint: disable=broad-except
                error_msg = f"Activate observability tracing failed with: {e}"
                raise ValueError(error_msg)

        try:
            _cyobservability.cyobservability_init(self._exporter)
        # TODO(xuanwn): Use specific exceptions
        except Exception as e:  # pylint: disable=broad-except
            _LOGGER.exception("Initiate observability failed with: %s", e)

        grpc._observability.observability_init(self)

    def observability_deinit(self) -> None:
        # Sleep so we don't loss any data. If we shutdown export thread
        # immediately after exit, it's possible that core didn't call RecordEnd
        # in callTracer, and all data recorded by calling RecordEnd will be
        # lost.
        # CENSUS_EXPORT_BATCH_INTERVAL_SECS: The time equals to the time in
        # AwaitNextBatchLocked.
        # TODO(xuanwn): explicit synchronization
        # https://github.com/grpc/grpc/issues/33262
        time.sleep(_cyobservability.CENSUS_EXPORT_BATCH_INTERVAL_SECS)
        self.set_tracing(False)
        self.set_stats(False)
        _cyobservability.observability_deinit()
        grpc._observability.observability_deinit()

    def _generate_ids(self) -> Tuple[bytes, bytes]:
        """ Generates trace ID and parent span ID

        Non empty `parent_span_id` means that:
        1. We have current thread local span that needs to be used
        or
        2. Tracing is enabled for this RPC.
        """
        # current span is used to track the trace ID. Since it is preserved across different RPCs,
        # we can safely use just the context of the first plugin.
        current_span = trace.get_current_span(
            context=self._plugins[0]._trace_ctx
        ).get_span_context()
        if current_span.is_valid:
            generator = RandomIdGenerator()
            trace_id = f"{current_span.trace_id:032x}".encode("utf-8")
            parent_span_id = f"{generator.generate_span_id():016x}".encode("utf-8")
        elif self._should_enable_tracing():
            generator = RandomIdGenerator()
            trace_id = f"{generator.generate_trace_id():032x}".encode("utf-8")
            parent_span_id = f"{generator.generate_span_id():016x}".encode("utf-8")
        else:
            trace_id = b"TRACE_ID"
            parent_span_id = b""
        return (trace_id, parent_span_id)

    def create_client_call_tracer(
        self, method_name: bytes, target: bytes
    ) -> ClientCallTracerCapsule:
        trace_id, parent_span_id = self._generate_ids()
        self._maybe_activate_client_plugin_options(target)
        exchange_labels = self._get_client_exchange_labels()
        enabled_optional_labels = set()
        for plugin in self._plugins:
            enabled_optional_labels.update(plugin.get_enabled_optional_labels())

        capsule = _cyobservability.create_client_call_tracer(
            method_name,
            target,
            trace_id,
            self._get_identifier(),
            exchange_labels,
            enabled_optional_labels,
            method_name in self._registered_methods,
            parent_span_id
        )
        return capsule

    def create_server_call_tracer_factory(
        self,
        *,
        xds: bool = False,
    ) -> Optional[ServerCallTracerFactoryCapsule]:
        capsule = None
        self._maybe_activate_server_plugin_options(xds)
        exchange_labels = self._get_server_exchange_labels()
        capsule = _cyobservability.create_server_call_tracer_factory_capsule(
            exchange_labels, self._get_identifier()
        )
        return capsule

    def save_trace_context(
        self, trace_id: str, span_id: str, is_sampled: bool
    ) -> None:
        for _plugin in self._plugins:
            _plugin.save_trace_context(trace_id, span_id, is_sampled)

    def record_rpc_latency(
        self,
        method: str,
        target: str,
        rpc_latency: float,
        status_code: grpc.StatusCode,
    ) -> None:
        status_code = GRPC_STATUS_CODE_TO_STRING.get(status_code, "UNKNOWN")
        encoded_method = method.encode("utf8")
        _cyobservability._record_rpc_latency(
            self._exporter,
            method,
            target,
            rpc_latency,
            status_code,
            self._get_identifier(),
            encoded_method in self._registered_methods,
        )

    def save_registered_method(self, method_name: bytes) -> None:
        self._registered_methods.add(method_name)

    def _get_client_exchange_labels(self) -> Dict[str, AnyStr]:
        client_exchange_labels = {}
        for _plugin in self._plugins:
            client_exchange_labels.update(_plugin.get_client_exchange_labels())
        return client_exchange_labels

    def _get_server_exchange_labels(self) -> Dict[str, AnyStr]:
        server_exchange_labels = {}
        for _plugin in self._plugins:
            server_exchange_labels.update(_plugin.get_server_exchange_labels())
        return server_exchange_labels

    def _maybe_activate_client_plugin_options(self, target: bytes) -> None:
        if not self._client_option_activated:
            for _plugin in self._plugins:
                _plugin.activate_client_plugin_options(target)
            self._client_option_activated = True

    def _maybe_activate_server_plugin_options(self, xds: bool) -> None:
        if not self._server_option_activated:
            for _plugin in self._plugins:
                _plugin.activate_server_plugin_options(xds)
            self._server_option_activated = True

    def _get_identifier(self) -> str:
        return PLUGIN_IDENTIFIER_SEP.join(
            _plugin.identifier for _plugin in self._plugins
        )

    def _should_enable_tracing(self) -> bool:
        return any(_plugin.is_tracing_configured() for _plugin in self._plugins)

    def get_enabled_optional_labels(self) -> List[OptionalLabelType]:
        return []


def _start_open_telemetry_observability(
    otel_o11y: OpenTelemetryObservability,
) -> None:
    global _OPEN_TELEMETRY_OBSERVABILITY  # pylint: disable=global-statement # noqa: PLW0603
    with _observability_lock:
        if _OPEN_TELEMETRY_OBSERVABILITY is None:
            _OPEN_TELEMETRY_OBSERVABILITY = otel_o11y
            _OPEN_TELEMETRY_OBSERVABILITY.observability_init()
        else:
            error_msg = "gPRC Python observability was already initialized!"
            raise RuntimeError(error_msg)


def _end_open_telemetry_observability() -> None:
    global _OPEN_TELEMETRY_OBSERVABILITY  # pylint: disable=global-statement # noqa: PLW0603
    with _observability_lock:
        if not _OPEN_TELEMETRY_OBSERVABILITY:
            error_msg = "Trying to end gPRC Python observability without initialize first!"
            raise RuntimeError(error_msg)
        _OPEN_TELEMETRY_OBSERVABILITY.observability_deinit()
        _OPEN_TELEMETRY_OBSERVABILITY = None
