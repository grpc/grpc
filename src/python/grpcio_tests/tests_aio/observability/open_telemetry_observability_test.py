# Copyright 2026 gRPC authors.
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
import asyncio
import logging
import os
import sys
from typing import Any, Dict, List, Set
import unittest

import grpc_observability
from grpc_observability import _open_telemetry_measures
from opentelemetry.sdk.metrics import MeterProvider
from opentelemetry.sdk.metrics.export import AggregationTemporality
from opentelemetry.sdk.metrics.export import MetricExportResult
from opentelemetry.sdk.metrics.export import MetricExporter
from opentelemetry.sdk.metrics.export import MetricsData
from opentelemetry.sdk.metrics.export import PeriodicExportingMetricReader

from tests_aio.observability import _test_server
from tests_aio.unit._test_base import AioTestBase

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


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class OpenTelemetryObservabilityTest(AioTestBase):
    async def setUp(self):
        self.all_metrics = defaultdict(list)
        otel_exporter = OTelMetricExporter(self.all_metrics)
        reader = PeriodicExportingMetricReader(
            exporter=otel_exporter,
            export_interval_millis=OTEL_EXPORT_INTERVAL_S * 1000,
        )
        self._provider = MeterProvider(metric_readers=[reader])
        self._otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        )
        self._otel_plugin.register_global()
        self._server, self._port = await _test_server.start_server()

    async def tearDown(self):
        await self._server.stop(0)
        self._otel_plugin.deregister_global()

    async def test_record_unary_unary(self):
        await _test_server.unary_unary_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    async def test_record_unary_stream(self):
        await _test_server.unary_stream_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    async def test_record_stream_unary(self):
        await _test_server.stream_unary_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    async def test_record_stream_stream(self):
        await _test_server.stream_stream_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    async def _validate_metrics_exist(self, all_metrics: Dict[str, Any]) -> None:
        # Sleep here to make sure we have at least one export from OTel MetricExporter.
        await asyncio.sleep(5)
        self.assertTrue(
            len(all_metrics.keys()) > 1,
            msg="No metrics were exported after 5s",
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
