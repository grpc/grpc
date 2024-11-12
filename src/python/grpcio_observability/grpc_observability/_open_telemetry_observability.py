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
from typing import Any, AnyStr, Dict, Iterable, List, Optional, Set, Union

import grpc

# pytype: disable=pyi-error
from grpc_observability import _cyobservability
from grpc_observability import _observability
from grpc_observability import _open_telemetry_measures
from grpc_observability._cyobservability import MetricsName
from grpc_observability._cyobservability import PLUGIN_IDENTIFIER_SEP
from grpc_observability._observability import OptionalLabelType
from grpc_observability._observability import StatsData
from opentelemetry.metrics import Counter
from opentelemetry.metrics import Histogram
from opentelemetry.metrics import Meter

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


class _OpenTelemetryPlugin:
    _plugin: OpenTelemetryPlugin
    _metric_to_recorder: Dict[MetricsName, Union[Counter, Histogram]]
    _enabled_client_plugin_options: Optional[List[OpenTelemetryPluginOption]]
    _enabled_server_plugin_options: Optional[List[OpenTelemetryPluginOption]]
    identifier: str

    def __init__(self, plugin: OpenTelemetryPlugin):
        self._plugin = plugin
        self._metric_to_recorder = dict()
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

    def _should_record(self, stats_data: StatsData) -> bool:
        # Decide if this plugin should record the stats_data.
        return stats_data.name in self._metric_to_recorder.keys()

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

    def maybe_record_stats_data(self, stats_data: List[StatsData]) -> None:
        # Records stats data to MeterProvider.
        if self._should_record(stats_data):
            self._record_stats_data(stats_data)

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
            elif metric == _open_telemetry_measures.CLIENT_ATTEMPT_DURATION:
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.CLIENT_RPC_DURATION:
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.CLIENT_ATTEMPT_SEND_BYTES:
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif (
                metric == _open_telemetry_measures.CLIENT_ATTEMPT_RECEIVED_BYTES
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
            elif metric == _open_telemetry_measures.SERVER_RPC_DURATION:
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.SERVER_RPC_SEND_BYTES:
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.SERVER_RPC_RECEIVED_BYTES:
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
        pass


# pylint: disable=no-self-use
class OpenTelemetryObservability(grpc._observability.ObservabilityPlugin):
    """OpenTelemetry based plugin implementation.

    This is class is part of an EXPERIMENTAL API.

    Args:
      plugins: _OpenTelemetryPlugins to enable.
    """

    _exporter: "grpc_observability.Exporter"
    _plugins: List[_OpenTelemetryPlugin]
    _registered_method: Set[bytes]
    _client_option_activated: bool
    _server_option_activated: bool

    def __init__(
        self,
        *,
        plugins: Optional[Iterable[_OpenTelemetryPlugin]],
    ):
        self._exporter = _OpenTelemetryExporterDelegator(plugins)
        self._registered_methods = set()
        self._plugins = plugins
        self._client_option_activated = False
        self._server_option_activated = False

    def observability_init(self):
        try:
            _cyobservability.activate_stats()
            self.set_stats(True)
        except Exception as e:  # pylint: disable=broad-except
            raise ValueError(f"Activate observability metrics failed with: {e}")

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

    def create_client_call_tracer(
        self, method_name: bytes, target: bytes
    ) -> ClientCallTracerCapsule:
        trace_id = b"TRACE_ID"
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
        pass

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
        plugin_identifiers = []
        for _plugin in self._plugins:
            plugin_identifiers.append(_plugin.identifier)
        return PLUGIN_IDENTIFIER_SEP.join(plugin_identifiers)

    def get_enabled_optional_labels(self) -> List[OptionalLabelType]:
        return []


def _start_open_telemetry_observability(
    otel_o11y: OpenTelemetryObservability,
) -> None:
    global _OPEN_TELEMETRY_OBSERVABILITY  # pylint: disable=global-statement
    with _observability_lock:
        if _OPEN_TELEMETRY_OBSERVABILITY is None:
            _OPEN_TELEMETRY_OBSERVABILITY = otel_o11y
            _OPEN_TELEMETRY_OBSERVABILITY.observability_init()
        else:
            raise RuntimeError(
                "gPRC Python observability was already initialized!"
            )


def _end_open_telemetry_observability() -> None:
    global _OPEN_TELEMETRY_OBSERVABILITY  # pylint: disable=global-statement
    with _observability_lock:
        if not _OPEN_TELEMETRY_OBSERVABILITY:
            raise RuntimeError(
                "Trying to end gPRC Python observability without initialize first!"
            )
        else:
            _OPEN_TELEMETRY_OBSERVABILITY.observability_deinit()
            _OPEN_TELEMETRY_OBSERVABILITY = None
