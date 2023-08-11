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
import os
import sys
import time
from typing import Any, Dict, Optional, Set
import unittest

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
    """

    def __init__(
        self,
        all_metrics: Dict[str, Any],
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
                    self.all_metrics["name"].add(metric.name)
                    for data_point in metric.data.data_points:
                        for key, value in data_point.attributes.items():
                            cur_value = self.all_metrics["label"].get(
                                key, set()
                            )
                            cur_value.add(value)
                            self.all_metrics["label"][key] = cur_value


class BaseTestOpenTelemetryPlugin(grpc_observability.OpenTelemetryPlugin):
    def __init__(self, provider: MeterProvider):
        self.provider = provider

    def get_meter_provider(self) -> Optional[MeterProvider]:
        return self.provider


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class OpenTelemetryObservabilityTest(unittest.TestCase):
    def setUp(self):
        self.all_metrics = dict()
        self.all_metrics["name"] = set()
        self.all_metrics["label"] = dict()
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

    def testRecordUnaryStream(self):
        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)

        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_stream_call(port=port)

        self._validate_metrics_exist(self.all_metrics)

    def testRecordStreamUnary(self):
        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)

        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)

    def testRecordStreamStream(self):
        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)

        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_stream_call(port=port)

        self._validate_metrics_exist(self.all_metrics)

    def testTargetAttributeFilter(self):
        # If target label contains 'localhost', is should be replaced with 'other'.
        def target_filter(target: str) -> bool:
            if "localhost" in target:
                return False
            return True

        otel_plugin = BaseTestOpenTelemetryPlugin(self._provider)
        otel_plugin.target_attribute_filter = target_filter

        with grpc_observability.OpenTelemetryObservability(
            plugins=[otel_plugin]
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        target_values = self.all_metrics["label"].get(GRPC_TARGET_LABEL)
        self.assertTrue(GRPC_OTHER_LABEL_VALUE in target_values)

    def testMethodAttributeFilter(self):
        # If method label contains 'UnaryUnaryFiltered', is should be replaced with 'other'.
        FILTERED_METHOD_NAME = "UnaryUnaryFiltered"

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
        method_values = self.all_metrics["label"].get(GRPC_METHOD_LABEL)
        self.assertTrue(GRPC_OTHER_LABEL_VALUE in method_values)
        self.assertTrue(FILTERED_METHOD_NAME not in method_values)

    def _validate_metrics_exist(self, all_metrics: Dict[str, Any]) -> None:
        # Sleep here to make sure we have at least one export from OTel MetricExporter.
        time.sleep(OTEL_EXPORT_INTERVAL_S)
        self.assertGreater(len(all_metrics["name"]), 0)
        self._validate_metrics_names(all_metrics["name"])

    def _validate_metrics_names(self, metric_names: Set[str]) -> None:
        for base_metric in _open_telemetry_measures.base_metrics():
            if base_metric.name not in metric_names:
                logger.error(
                    "metric %s not found in exported metrics: %s!",
                    base_metric.name,
                    metric_names,
                )
            self.assertTrue(base_metric.name in metric_names)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
