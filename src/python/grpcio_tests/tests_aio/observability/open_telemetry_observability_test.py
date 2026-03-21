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
from typing import Any, Callable, List, Optional, Sequence, Set, Tuple
import unittest

import grpc_observability
from grpc_observability import _open_telemetry_measures
from opentelemetry.sdk import metrics as otel_metrics
from opentelemetry.sdk.metrics import export as otel_metrics_export
from opentelemetry.sdk.metrics import view as otel_metrics_view
from opentelemetry.sdk import trace as otel_trace
from opentelemetry.sdk.trace import export as otel_trace_export
from opentelemetry.sdk.trace.export import in_memory_span_exporter

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


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class OpenTelemetryObservabilityTest(AioTestBase):
    async def setUp(self):
        self.all_metrics = collections.defaultdict(list)
        otel_exporter = OTelMetricExporter(self.all_metrics)
        metric_reader = otel_metrics_export.PeriodicExportingMetricReader(
            exporter=otel_exporter,
            export_interval_millis=OTEL_EXPORT_INTERVAL_S * 1000,
        )
        self._meter_provider = otel_metrics.MeterProvider(
            metric_readers=(metric_reader,)
        )
        otel_tracer_provider = otel_trace.TracerProvider()
        self._span_exporter = in_memory_span_exporter.InMemorySpanExporter()
        span_processor = otel_trace_export.SimpleSpanProcessor(self._span_exporter)
        otel_tracer_provider.add_span_processor(span_processor)
        self._tracer_provider = otel_tracer_provider
        self._otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider,
            tracer_provider=self._tracer_provider,
        )
        self._otel_plugin.register_global()
        self._server, self._port = await _test_server.start_server()

    async def tearDown(self):
        await self._server.stop(0)
        self._otel_plugin.deregister_global()
        self._meter_provider.shutdown(timeout_millis=1_000)

    async def test_metrics_unary_unary(self):
        await _test_server.unary_unary_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    async def test_traces_unary_unary(self):
        await _test_server.unary_unary_call(port=self._port)

        await self._validate_spans_exist(self._span_exporter)
        self._validate_spans(
            spans=self._span_exporter.get_finished_spans(),
            expected_span_size=3,
            expected_server_events=[
                (
                    "Outbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
            ],
            expected_attempt_events=[
                (
                    "Outbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
            ],
        )

    async def test_metrics_unary_stream(self):
        await _test_server.unary_stream_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    async def test_traces_unary_stream(self):
        await _test_server.unary_stream_call(port=self._port)

        await self._validate_spans_exist(self._span_exporter)
        self._validate_spans(
            spans=self._span_exporter.get_finished_spans(),
            expected_span_size=3,
            expected_server_events=[
                (
                    "Outbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "1", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "2", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "3", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "4", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
            ],
            expected_attempt_events=[
                (
                    "Outbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "1", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "2", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "3", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "4", "message-size": "3"},
                ),
            ],
        )

    async def test_metrics_stream_unary(self):
        await _test_server.stream_unary_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    async def test_traces_stream_unary(self):
        await _test_server.stream_unary_call(port=self._port)

        await self._validate_spans_exist(self._span_exporter)
        self._validate_spans(
            spans=self._span_exporter.get_finished_spans(),
            expected_span_size=3,
            expected_server_events=[
                (
                    "Outbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "1", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "2", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "3", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "4", "message-size": "3"},
                ),
            ],
            expected_attempt_events=[
                (
                    "Inbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "1", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "2", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "3", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "4", "message-size": "3"},
                ),
            ],
        )

    async def test_metrics_stream_stream(self):
        await _test_server.stream_stream_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    async def test_traces_stream_stream(self):
        await _test_server.stream_stream_call(port=self._port)

        await self._validate_metrics_exist(self.all_metrics)
        self._validate_spans(
            spans=self._span_exporter.get_finished_spans(),
            expected_span_size=3,
            expected_server_events=[
                (
                    "Outbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "1", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "2", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "3", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "4", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "1", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "2", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "3", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "4", "message-size": "3"},
                ),
            ],
            expected_attempt_events=[
                (
                    "Outbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "1", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "2", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "3", "message-size": "3"},
                ),
                (
                    "Outbound message",
                    {"sequence-number": "4", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "0", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "1", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "2", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "3", "message-size": "3"},
                ),
                (
                    "Inbound message",
                    {"sequence-number": "4", "message-size": "3"},
                ),
            ],
        )

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
                break
            await asyncio.sleep(0.5)
        else:
            self.fail(message() + " after " + str(timeout))

    async def _validate_metrics_exist(
        self, all_metrics: dict[str, Any]
    ) -> None:
        # Sleep here to make sure we have at least one export from
        # OTel MetricExporter.
        await self.assert_eventually(
            lambda: len(all_metrics.keys()) > 1,
            message=lambda: f"No metrics were exported",
        )

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

    async def _validate_spans_exist(
        self, span_exporter: otel_trace_export.SpanExporter
    ):
        # Sleep here to make sure we have at least one export from
        # OTel SpanExporter.
        await self.assert_eventually(
            lambda: len(span_exporter.get_finished_spans()) > 1,
            message=lambda: f"No traces were exported",
        )

    def _validate_spans(
        self,
        spans: Sequence[otel_trace.ReadableSpan],
        expected_span_size: int,
        expected_server_events: Sequence[Tuple[str, dict[str, str]]],
        expected_attempt_events: Sequence[Tuple[str, dict[str, str]]],
    ) -> None:
        self.assertTrue(
            expr=(len(spans) == expected_span_size),
            msg=f"Expected span size {expected_span_size}, got: {len(spans)}!",
        )

        client_span = next(
            (span for span in spans if span.name.startswith("Sent.")), None
        )
        self.assertIsNotNone(client_span)

        attempt_span = next(
            (span for span in spans if span.name.startswith("Attempt.")), None
        )
        self.assertIsNotNone(attempt_span)

        server_span = next(
            (span for span in spans if span.name.startswith("Recv.")), None
        )
        self.assertIsNotNone(server_span)

        # validate span statuses
        self.assertTrue(client_span.status.is_ok)
        self.assertTrue(attempt_span.status.is_ok)
        self.assertTrue(server_span.status.is_ok)

        # validate mandatory attributes
        attempt_attrs = dict(attempt_span.attributes)
        self.assertIn("transparent-retry", attempt_attrs)
        self.assertEqual(attempt_attrs["transparent-retry"], "0")
        self.assertIn("previous-rpc-attempts", attempt_attrs)
        self.assertEqual(attempt_attrs["previous-rpc-attempts"], "0")

        # validate parent-child relationship
        self.assertEqual(
            client_span.get_span_context().trace_id,
            attempt_span.get_span_context().trace_id
        )
        self.assertEqual(
            attempt_span.parent.span_id,
            client_span.get_span_context().span_id
        )
        self.assertEqual(
            attempt_span.get_span_context().trace_id,
            server_span.get_span_context().trace_id
        )
        self.assertEqual(
            server_span.parent.span_id,
            attempt_span.get_span_context().span_id
        )

        # validate server span traced events
        server_span_events_packed = [
            (ev.name, ev.attributes) for ev in server_span.events
        ]
        for expected_ev in expected_server_events:
            self.assertTrue(
                expr=(expected_ev in server_span_events_packed),
                msg=f"Expected server event missing: {expected_ev}!",
            )

        # validate attempt span traced events
        attempt_span_events_packed = [
            (ev.name, ev.attributes) for ev in attempt_span.events
        ]
        for expected_ev in expected_attempt_events:
            self.assertTrue(
                expr=(expected_ev in attempt_span_events_packed),
                msg=f"Expected attempt event missing: {expected_ev}!",
            )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
