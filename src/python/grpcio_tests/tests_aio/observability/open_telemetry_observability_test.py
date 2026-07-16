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

import asyncio
import collections
import datetime
import logging
import os
import sys
from typing import Any, Callable, List, Optional, Set
import unittest

import grpc_observability
from grpc_observability import _open_telemetry_measures
from opentelemetry.sdk import metrics as otel_metrics
from opentelemetry.sdk.metrics import export as otel_metrics_export
from opentelemetry.sdk.metrics import view as otel_metrics_view

from tests_aio.observability import _test_server
from tests_aio.unit._test_base import AioTestBase

logger = logging.getLogger(__name__)

STREAM_LENGTH = 5
OTEL_EXPORT_INTERVAL_S = 0.5
_RETRY_METRIC_NAMES = [
    metric.name for metric in _open_telemetry_measures.retry_metrics()
]
_BASE_METRIC_COUNT = len(_open_telemetry_measures.base_metrics())
_NUM_FAILED_ATTEMPTS = 2


class OTelMetricExporter(otel_metrics_export.MetricExporter):
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
        all_metrics: dict[str, List],
        preferred_temporality: (
            dict[type, otel_metrics_export.AggregationTemporality] | None
        ) = None,
        preferred_aggregation: (
            dict[type, otel_metrics_view.Aggregation] | None
        ) = None,
    ):
        super().__init__(
            preferred_temporality=preferred_temporality,
            preferred_aggregation=preferred_aggregation,
        )
        self.all_metrics = all_metrics

    def export(
        self,
        metrics_data: otel_metrics_export.MetricsData,
        timeout_millis: float = 10_000,
        **kwargs,
    ) -> otel_metrics_export.MetricExportResult:
        self.record_metric(metrics_data)
        return otel_metrics_export.MetricExportResult.SUCCESS

    def shutdown(self, timeout_millis: float = 30_000, **kwargs) -> None:
        pass

    def force_flush(self, timeout_millis: float = 10_000) -> bool:
        return True

    def record_metric(
        self, metrics_data: otel_metrics_export.MetricsData
    ) -> None:
        for resource_metric in metrics_data.resource_metrics:
            for scope_metric in resource_metric.scope_metrics:
                for metric in scope_metric.metrics:
                    for data_point in metric.data.data_points:
                        self.all_metrics[metric.name].append(
                            data_point.attributes
                        )


class _ObservabilityTestBase(AioTestBase):
    async def assert_eventually(
        self,
        predicate: Callable[[], bool],
        *,
        timeout: Optional[datetime.timedelta] = None,
        message: Optional[Callable[[], str]] = None,
    ) -> None:
        message = message or (lambda: "Proposition did not evaluate to true")
        timeout = timeout or datetime.timedelta(seconds=5)
        end = datetime.datetime.now() + timeout
        while datetime.datetime.now() < end:
            if predicate():
                return
            await asyncio.sleep(OTEL_EXPORT_INTERVAL_S)
        self.fail(message())

    async def _validate_metrics_exist(
        self,
        all_metrics: dict[str, Any],
        expected_count: int = _BASE_METRIC_COUNT,
    ) -> None:
        # Wait until we have at least the expected number of metrics from the
        # OTel MetricExporter instead of relying on a fixed sleep, so that we
        # do not race with exports that are still in flight.
        await self.assert_eventually(
            lambda: len(all_metrics.keys()) >= expected_count,
            message=lambda: (
                f"Expected at least {expected_count} metrics, got "
                f"{len(all_metrics.keys())}: {all_metrics.keys()}"
            ),
        )


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class OpenTelemetryObservabilityTest(_ObservabilityTestBase):
    async def setUp(self):
        self.all_metrics = collections.defaultdict(list)
        otel_exporter = OTelMetricExporter(self.all_metrics)
        reader = otel_metrics_export.PeriodicExportingMetricReader(
            exporter=otel_exporter,
            export_interval_millis=OTEL_EXPORT_INTERVAL_S * 1000,
        )
        self._provider = otel_metrics.MeterProvider(metric_readers=(reader,))
        self._otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        )
        self._otel_plugin.register_global()
        self._server, self._port = await _test_server.start_server()

    async def tearDown(self):
        await self._server.stop(0)
        self._otel_plugin.deregister_global()
        self._provider.shutdown(timeout_millis=1_000)

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

    def _validate_all_metrics_names(self, metric_names: Set[str]) -> None:
        self._validate_server_metrics_names(metric_names)
        self._validate_client_metrics_names(metric_names)

    def _validate_server_metrics_names(self, metric_names: Set[str]) -> None:
        for base_metric in _open_telemetry_measures.base_metrics():
            if "grpc.server" in base_metric.name:
                self.assertTrue(
                    base_metric.name in metric_names,
                    msg=(
                        f"metric {base_metric.name} not found"
                        f"in exported metrics: {metric_names}!"
                    ),
                )

    def _validate_client_metrics_names(self, metric_names: Set[str]) -> None:
        for base_metric in _open_telemetry_measures.base_metrics():
            if "grpc.client" in base_metric.name:
                self.assertTrue(
                    base_metric.name in metric_names,
                    msg=(
                        f"metric {base_metric.name} not found"
                        f"in exported metrics: {metric_names}!"
                    ),
                )


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class OpenTelemetryObservabilityRetryTest(_ObservabilityTestBase):
    """Validates that the per-call retry metrics are recorded for the AsyncIO
    stack, which uses the same call tracer as the sync stack."""

    async def setUp(self):
        self.all_metrics = collections.defaultdict(list)
        otel_exporter = OTelMetricExporter(self.all_metrics)
        reader = otel_metrics_export.PeriodicExportingMetricReader(
            exporter=otel_exporter,
            export_interval_millis=OTEL_EXPORT_INTERVAL_S * 1000,
        )
        self._provider = otel_metrics.MeterProvider(metric_readers=(reader,))
        self._otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider,
            additional_metrics=_RETRY_METRIC_NAMES,
        )
        self._otel_plugin.register_global()
        self._server, self._port = await _test_server.start_flaky_server(
            num_failed_attempts=_NUM_FAILED_ATTEMPTS
        )

    async def tearDown(self):
        await self._server.stop(0)
        self._otel_plugin.deregister_global()
        self._provider.shutdown(timeout_millis=1_000)

    async def test_record_retry_metrics(self):
        await _test_server.unary_unary_call_with_retries(port=self._port)

        # Base metrics plus grpc.client.call.retries and
        # grpc.client.call.retry_delay. Transparent retries stay at 0 and are
        # therefore not reported.
        await self._validate_metrics_exist(
            self.all_metrics, expected_count=_BASE_METRIC_COUNT + 2
        )
        for metric in (
            _open_telemetry_measures.CLIENT_CALL_RETRIES,
            _open_telemetry_measures.CLIENT_CALL_RETRY_DELAY,
        ):
            self.assertTrue(
                metric.name in self.all_metrics,
                msg=(
                    f"metric {metric.name} not found"
                    f"in exported metrics: {self.all_metrics.keys()}!"
                ),
            )
        # No transparent retry happened, so 0 should not be reported.
        self.assertNotIn(
            _open_telemetry_measures.CLIENT_CALL_TRANSPARENT_RETRIES.name,
            self.all_metrics,
        )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
