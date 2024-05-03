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
from typing import Any, Dict, Iterable, List, Optional, Set, Union

import grpc

# pytype: disable=pyi-error
from grpc_observability import _cyobservability
from grpc_observability import _observability
from grpc_observability import _open_telemetry_measures
from grpc_observability._cyobservability import MetricsName
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

GRPC_METHOD_LABEL = "grpc.method"
GRPC_TARGET_LABEL = "grpc.target"
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

    def __init__(self, plugin: OpenTelemetryPlugin):
        self._plugin = plugin
        self._metric_to_recorder = dict()

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

        target = stats_data.labels.get(GRPC_TARGET_LABEL, "")
        if not self._plugin.target_attribute_filter(target):
            # Filter target name.
            stats_data.labels[GRPC_TARGET_LABEL] = GRPC_OTHER_LABEL_VALUE

        method = stats_data.labels.get(GRPC_METHOD_LABEL, "")
        if not self._plugin.generic_method_attribute_filter(method):
            # Filter method name.
            stats_data.labels[GRPC_METHOD_LABEL] = GRPC_OTHER_LABEL_VALUE

        value = 0
        if stats_data.measure_double:
            value = stats_data.value_float
        else:
            value = stats_data.value_int
        if isinstance(recorder, Counter):
            recorder.add(value, attributes=stats_data.labels)
        elif isinstance(recorder, Histogram):
            recorder.record(value, attributes=stats_data.labels)

    # pylint: disable=no-self-use
    def maybe_record_stats_data(self, stats_data: List[StatsData]) -> None:
        # Records stats data to MeterProvider.
        if self._should_record(stats_data):
            self._record_stats_data(stats_data)

    def _register_metrics(
        self, meter: Meter, metrics: List[_open_telemetry_measures.Metric]
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
      plugin: _OpenTelemetryPlugin to enable.
    """

    exporter: "grpc_observability.Exporter"
    _registered_method: Set[bytes]

    def __init__(
        self,
        *,
        plugins: Optional[Iterable[_OpenTelemetryPlugin]],
    ):
        self.exporter = _OpenTelemetryExporterDelegator(plugins)
        self._registered_methods = set()

    def observability_init(self):
        try:
            _cyobservability.activate_stats()
            self.set_stats(True)
        except Exception as e:  # pylint: disable=broad-except
            raise ValueError(f"Activate observability metrics failed with: {e}")

        try:
            _cyobservability.cyobservability_init(self.exporter)
        # TODO(xuanwn): Use specific exceptons
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
        registered_method = False
        if method_name in self._registered_methods:
            registered_method = True
        capsule = _cyobservability.create_client_call_tracer(
            method_name, target, trace_id, registered_method
        )
        return capsule

    def create_server_call_tracer_factory(
        self,
    ) -> ServerCallTracerFactoryCapsule:
        capsule = _cyobservability.create_server_call_tracer_factory_capsule()
        return capsule

    def delete_client_call_tracer(
        self, client_call_tracer: ClientCallTracerCapsule
    ) -> None:
        _cyobservability.delete_client_call_tracer(client_call_tracer)

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
        registered_method = False
        encoded_method = method.encode("utf8")
        if encoded_method in self._registered_methods:
            registered_method = True
        _cyobservability._record_rpc_latency(
            self.exporter,
            method,
            target,
            rpc_latency,
            status_code,
            registered_method,
        )

    def save_registered_method(self, method_name: bytes) -> None:
        self._registered_methods.add(method_name)


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
