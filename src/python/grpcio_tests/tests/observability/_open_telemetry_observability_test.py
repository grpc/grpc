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

from collections import defaultdict
import datetime
import logging
import os
import sys
import time
from typing import Any, Callable, Dict, List, Optional, Set
import unittest

import grpc
import grpc_observability
from grpc_observability import _open_telemetry_measures
from grpc_observability._open_telemetry_plugin import GRPC_METHOD_LABEL
from grpc_observability._open_telemetry_plugin import GRPC_OTHER_LABEL_VALUE
from grpc_observability._open_telemetry_plugin import GRPC_TARGET_LABEL
from opentelemetry.sdk.metrics import MeterProvider
from opentelemetry.sdk.metrics.export import AggregationTemporality
from opentelemetry.sdk.metrics.export import MetricExportResult
from opentelemetry.sdk.metrics.export import MetricExporter
from opentelemetry.sdk.metrics.export import MetricsData
from opentelemetry.sdk.metrics.export import PeriodicExportingMetricReader

from tests.observability import _test_server

logger = logging.getLogger(__name__)

STREAM_LENGTH = 5
OTEL_EXPORT_INTERVAL_S = 0.5


class OTelMetricExporter(MetricExporter):
    """Implementation of :class:`MetricExporter` that export metrics to the
    provided metric_list.

    all_metrics: A dict which key is grpc_observability._opentelemetry_measures.Metric.name,
        value is a list of labels recorded for that metric.
        An example item of this dict:
            {"grpc.client.attempt.started":
              [{'grpc.method': 'test/UnaryUnary', 'grpc.target': 'localhost:42517'},
               {'grpc.method': 'other', 'grpc.target': 'localhost:42517'}]}
    """

    def __init__(
        self,
        all_metrics: Dict[str, List],
        preferred_temporality: Dict[type, AggregationTemporality] = None,
        preferred_aggregation: Dict[
            type, "opentelemetry.sdk.metrics.view.Aggregation"
        ] = None,
    ):
        super().__init__(
            preferred_temporality=preferred_temporality,
            preferred_aggregation=preferred_aggregation,
        )
        self.all_metrics = all_metrics

    def export(
        self,
        metrics_data: MetricsData,
        timeout_millis: float = 10_000,
        **kwargs,
    ) -> MetricExportResult:
        self.record_metric(metrics_data)
        return MetricExportResult.SUCCESS

    def shutdown(self, timeout_millis: float = 30_000, **kwargs) -> None:
        pass

    def force_flush(self, timeout_millis: float = 10_000) -> bool:
        return True

    def record_metric(self, metrics_data: MetricsData) -> None:
        for resource_metric in metrics_data.resource_metrics:
            for scope_metric in resource_metric.scope_metrics:
                for metric in scope_metric.metrics:
                    for data_point in metric.data.data_points:
                        self.all_metrics[metric.name].append(
                            data_point.attributes
                        )


class BaseTestOpenTelemetryPlugin(grpc_observability.OpenTelemetryPlugin):
    def __init__(self, provider: MeterProvider):
        self.provider = provider

    def get_meter_provider(self) -> Optional[MeterProvider]:
        return self.provider


class _ClientUnaryUnaryInterceptor(grpc.UnaryUnaryClientInterceptor):
    def intercept_unary_unary(
        self, continuation, client_call_details, request_or_iterator
    ):
        response = continuation(client_call_details, request_or_iterator)
        return response


class _ServerInterceptor(grpc.ServerInterceptor):
    def intercept_service(self, continuation, handler_call_details):
        return continuation(handler_call_details)


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class OpenTelemetryObservabilityTest(unittest.TestCase):
    def setUp(self):
        self.all_metrics = defaultdict(list)
        otel_exporter = OTelMetricExporter(self.all_metrics)
        reader = PeriodicExportingMetricReader(
            exporter=otel_exporter,
            export_interval_millis=OTEL_EXPORT_INTERVAL_S * 1000,
        )
        self._provider = MeterProvider(metric_readers=[reader])
        self._server = None
        self._port = None

    def tearDown(self):
        if self._server:
            self._server.stop(0)

    def testRecordUnaryUnary(self):
        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)
        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics)

    def testRecordUnaryUnaryWithClientInterceptor(self):
        interceptor = _ClientUnaryUnaryInterceptor()
        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)
        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.intercepted_unary_unary_call(
                port=port, interceptors=interceptor
            )

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics)

    def testRecordUnaryUnaryWithServerInterceptor(self):
        interceptor = _ServerInterceptor()
        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)
        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server(interceptors=[interceptor])
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics)

    def testThrowErrorWhenCallingMultipleInit(self):
        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)
        with self.assertRaises(ValueError):
            with grpc_observability.OpenTelemetryObservability(
                plugins=[otel_plugin]
            ) as o11y:
                grpc._observability.observability_init(o11y)

    def testRecordUnaryUnaryClientOnly(self):
        server, port = _test_server.start_server()
        self._server = server

        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)
        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_client_metrics_names(self.all_metrics)

    def testNoRecordBeforeInit(self):
        server, port = _test_server.start_server()
        _test_server.unary_unary_call(port=port)
        self.assertEqual(len(self.all_metrics), 0)
        server.stop(0)

        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)
        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics)

    def testNoRecordAfterExit(self):
        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)
        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            self._port = port
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics)

        self.all_metrics = defaultdict(list)
        _test_server.unary_unary_call(port=self._port)
        with self.assertRaisesRegex(AssertionError, "No metrics was exported"):
            self._validate_metrics_exist(self.all_metrics)

    def testRecordUnaryStream(self):
        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)

        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_stream_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics)

    def testRecordStreamUnary(self):
        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)

        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics)

    def testRecordStreamStream(self):
        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)

        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_stream_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics)

    def testTargetAttributeFilter(self):
        main_server, main_port = _test_server.start_server()
        backup_server, backup_port = _test_server.start_server()
        main_target = f"localhost:{main_port}"
        backup_target = f"localhost:{backup_port}"

        # Replace target label with 'other' for main_server.
        def target_filter(target: str) -> bool:
            if main_target in target:
                return False
            return True

        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)
        otel_plugin.target_attribute_filter = target_filter

        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            _test_server.unary_unary_call(port=main_port)
            _test_server.unary_unary_call(port=backup_port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_client_metrics_names(self.all_metrics)

        target_values = set()
        for label_list in self.all_metrics.values():
            for labels in label_list:
                if GRPC_TARGET_LABEL in labels:
                    target_values.add(labels[GRPC_TARGET_LABEL])
        self.assertTrue(GRPC_OTHER_LABEL_VALUE in target_values)
        self.assertTrue(backup_target in target_values)

        main_server.stop(0)
        backup_server.stop(0)

    def testMethodAttributeFilter(self):
        # If method name is 'test/UnaryUnaryFiltered', is should be replaced with 'other'.
        FILTERED_METHOD_NAME = "test/UnaryUnaryFiltered"

        def method_filter(method: str) -> bool:
            if FILTERED_METHOD_NAME in method:
                return False
            return True

        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)
        otel_plugin.generic_method_attribute_filter = method_filter

        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)
            _test_server.unary_unary_filtered_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics)
        method_values = set()
        for label_list in self.all_metrics.values():
            for labels in label_list:
                if GRPC_METHOD_LABEL in labels:
                    method_values.add(labels[GRPC_METHOD_LABEL])
        self.assertTrue(GRPC_OTHER_LABEL_VALUE in method_values)
        self.assertTrue(FILTERED_METHOD_NAME not in method_values)

    def assert_eventually(
        self,
        predicate: Callable[[], bool],
        *,
        timeout: Optional[datetime.timedelta] = None,
        message: Optional[Callable[[], str]] = None,
    ) -> None:
        message = message or (lambda: "Proposition did not evaluate to true")
        timeout = timeout or datetime.timedelta(seconds=10)
        end = datetime.datetime.now() + timeout
        while datetime.datetime.now() < end:
            if predicate():
                break
            time.sleep(0.5)
        else:
            self.fail(message() + " after " + str(timeout))

    def _validate_metrics_exist(self, all_metrics: Dict[str, Any]) -> None:
        # Sleep here to make sure we have at least one export from OTel MetricExporter.
        self.assert_eventually(
            lambda: len(all_metrics.keys()) > 1,
            message=lambda: f"No metrics was exported",
        )

    def _validate_all_metrics_names(self, metric_names: Set[str]) -> None:
        self._validate_server_metrics_names(metric_names)
        self._validate_client_metrics_names(metric_names)

    def _validate_server_metrics_names(self, metric_names: Set[str]) -> None:
        for base_metric in _open_telemetry_measures.base_metrics():
            if "grpc.server" in base_metric.name:
                self.assertTrue(
                    base_metric.name in metric_names,
                    msg=f"metric {base_metric.name} not found in exported metrics: {metric_names}!",
                )

    def _validate_client_metrics_names(self, metric_names: Set[str]) -> None:
        for base_metric in _open_telemetry_measures.base_metrics():
            if "grpc.client" in base_metric.name:
                self.assertTrue(
                    base_metric.name in metric_names,
                    msg=f"metric {base_metric.name} not found in exported metrics: {metric_names}!",
                )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
