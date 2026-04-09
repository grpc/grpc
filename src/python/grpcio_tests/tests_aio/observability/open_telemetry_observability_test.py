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
import time
from typing import Any, Callable, Iterable, Optional
import unittest

import grpc_observability
from grpc_observability import _open_telemetry_measures
from grpc_observability._open_telemetry_observability import (
    GRPC_OTHER_LABEL_VALUE,
)
from grpc_observability._open_telemetry_observability import GRPC_METHOD_LABEL
from opentelemetry.sdk import metrics as otel_metrics
from opentelemetry.sdk.metrics import export as otel_metrics_export
from opentelemetry.sdk.metrics import view as otel_metrics_view

from tests_aio.observability import _test_server
from tests_aio.unit._test_base import AioTestBase

logger = logging.getLogger(__name__)

STREAM_LENGTH = 5
OTEL_EXPORT_INTERVAL_S = 0.5


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
        all_metrics: dict[str, list],
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


class OpenTelemetryObservabilityBase(AioTestBase):
    async def setUp(self):
        self.all_metrics: dict[str, list[str]] = collections.defaultdict(list)
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
        self._server = None
        self._port = None

    async def tearDown(self):
        if self._server:
            await self._server.stop(0)
        self._otel_plugin.deregister_global()
        self._provider.shutdown(timeout_millis=1_000)

    def assert_eventually(
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
                break
            time.sleep(0.5)
        else:
            self.fail(message() + " after " + str(timeout))

    async def _validate_metrics_exist(
        self, all_metrics: dict[str, Any]
    ) -> None:
        # Sleep here to make sure we have at least one export from
        # OTel MetricExporter.
        self.assert_eventually(
            lambda: len(all_metrics.keys()) > 1,
            message=lambda: f"No metrics were exported",
        )

    def _validate_all_metrics_names(self, metric_names: Iterable[str]) -> None:
        self._validate_server_metrics_names(metric_names)
        self._validate_client_metrics_names(metric_names)

    def _validate_server_metrics_names(
        self, metric_names: Iterable[str]
    ) -> None:
        for base_metric in _open_telemetry_measures.base_metrics():
            if "grpc.server" in base_metric.name:
                self.assertTrue(
                    base_metric.name in metric_names,
                    msg=(
                        f"metric {base_metric.name} not found"
                        f"in exported metrics: {metric_names}!"
                    ),
                )

    def _validate_client_metrics_names(
        self, metric_names: Iterable[str]
    ) -> None:
        for base_metric in _open_telemetry_measures.base_metrics():
            if "grpc.client" in base_metric.name:
                self.assertTrue(
                    base_metric.name in metric_names,
                    msg=(
                        f"metric {base_metric.name} not found"
                        f"in exported metrics: {metric_names}!"
                    ),
                )

    def _validate_all_method_labels(
        self,
        all_metrics: dict[str, list[str]],
        registered_method_name: str = "",
    ) -> None:
        client_method_values = set()
        server_method_values = set()
        for metric_name, label_list in all_metrics.items():
            for labels in label_list:
                if GRPC_METHOD_LABEL in labels:
                    if "grpc.server" in metric_name:
                        server_method_values.add(labels[GRPC_METHOD_LABEL])
                    elif "grpc.client" in metric_name:
                        client_method_values.add(labels[GRPC_METHOD_LABEL])

        self.assertEqual(len(client_method_values), 1)
        self.assertEqual(len(server_method_values), 1)

        if registered_method_name:
            self.assertTrue(registered_method_name in client_method_values)
            self.assertTrue(registered_method_name in server_method_values)
        else:
            self.assertTrue(GRPC_OTHER_LABEL_VALUE in client_method_values)
            self.assertTrue(GRPC_OTHER_LABEL_VALUE in server_method_values)


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class OpenTelemetryObservabilityUnregisteredMethodsTest(
    OpenTelemetryObservabilityBase
):
    async def test_record_unary_unary(self):
        self._server, self._port = await _test_server.start_server(
            register_method=False
        )
        await _test_server.unary_unary_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        self._validate_all_method_labels(self.all_metrics)

    async def test_record_unary_stream(self):
        self._server, self._port = await _test_server.start_server(
            register_method=False
        )
        await _test_server.unary_stream_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        self._validate_all_method_labels(self.all_metrics)

    async def test_record_stream_unary(self):
        self._server, self._port = await _test_server.start_server(
            register_method=False
        )
        await _test_server.stream_unary_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        self._validate_all_method_labels(self.all_metrics)

    async def test_record_stream_stream(self):
        self._server, self._port = await _test_server.start_server(
            register_method=False
        )
        await _test_server.stream_stream_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        self._validate_all_method_labels(self.all_metrics)


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class OpenTelemetryObservabilityRegisteredMethodsTest(
    OpenTelemetryObservabilityBase
):
    async def test_record_unary_unary(self):
        self._server, self._port = await _test_server.start_server(
            register_method=True
        )
        await _test_server.unary_unary_call(
            port=self._port, registered_method=True
        )

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        self._validate_all_method_labels(self.all_metrics, "test/UnaryUnary")

    async def test_record_unary_stream(self):
        self._server, self._port = await _test_server.start_server(
            register_method=True
        )
        await _test_server.unary_stream_call(
            port=self._port, registered_method=True
        )

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        self._validate_all_method_labels(self.all_metrics, "test/UnaryStream")

    async def test_record_stream_unary(self):
        self._server, self._port = await _test_server.start_server(
            register_method=True
        )
        await _test_server.stream_unary_call(
            port=self._port, registered_method=True
        )

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        self._validate_all_method_labels(self.all_metrics, "test/StreamUnary")

    async def test_record_stream_stream(self):
        self._server, self._port = await _test_server.start_server(
            register_method=True
        )
        await _test_server.stream_stream_call(
            port=self._port, registered_method=True
        )

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        self._validate_all_method_labels(self.all_metrics, "test/StreamStream")


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
