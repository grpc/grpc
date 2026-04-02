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

import collections
import datetime
import logging
import os
import sys
import time
from typing import Any, Callable, Dict, List, Optional, Sequence, Set, Tuple
import unittest

import grpc
import grpc_observability
from grpc_observability import _open_telemetry_measures
from grpc_observability._open_telemetry_observability import (
    GRPC_OTHER_LABEL_VALUE,
)
from grpc_observability._open_telemetry_observability import GRPC_METHOD_LABEL
from grpc_observability._open_telemetry_observability import GRPC_TARGET_LABEL
from opentelemetry.sdk import metrics as otel_metrics
from opentelemetry.sdk import trace as otel_trace
from opentelemetry.sdk.metrics import export as otel_metrics_export
from opentelemetry.sdk.metrics import view as otel_metrics_view
from opentelemetry.sdk.trace import export as otel_trace_export
from opentelemetry.sdk.trace.export import in_memory_span_exporter
from opentelemetry.trace.propagation.tracecontext import (
    TraceContextTextMapPropagator,
)

from tests.observability import _test_server

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


class _ClientUnaryUnaryInterceptor(grpc.UnaryUnaryClientInterceptor):
    def intercept_unary_unary(
        self, continuation, client_call_details, request_or_iterator
    ):
        response = continuation(client_call_details, request_or_iterator)
        return response


class _ServerInterceptor(grpc.ServerInterceptor):
    def intercept_service(self, continuation, handler_call_details):
        return continuation(handler_call_details)


class _MetadataCapturingServerInterceptor(grpc.ServerInterceptor):
    def __init__(self):
        self.captured_metadata = []

    def intercept_service(self, continuation, handler_call_details):
        self.captured_metadata.append(
            dict(handler_call_details.invocation_metadata)
        )
        return continuation(handler_call_details)


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class OpenTelemetryObservabilityTest(unittest.TestCase):
    def setUp(self):
        self.all_metrics = collections.defaultdict(list)
        otel_metric_exporter = OTelMetricExporter(self.all_metrics)
        metric_reader = otel_metrics_export.PeriodicExportingMetricReader(
            exporter=otel_metric_exporter,
            export_interval_millis=OTEL_EXPORT_INTERVAL_S * 1000,
        )
        self._meter_provider = otel_metrics.MeterProvider(
            metric_readers=(metric_reader,)
        )
        otel_tracer_provider = otel_trace.TracerProvider()
        self._span_exporter = in_memory_span_exporter.InMemorySpanExporter()
        span_processor = otel_trace_export.SimpleSpanProcessor(
            self._span_exporter
        )
        otel_tracer_provider.add_span_processor(span_processor)
        self._tracer_provider = otel_tracer_provider
        self._server = None
        self._port = None

    def tearDown(self):
        if self._server:
            self._server.stop(0)

    def testMetricsForUnaryUnaryCallWithContextManager(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider,
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    def testTracesForUnaryUnaryCallWithContextManager(self):
        with grpc_observability.OpenTelemetryPlugin(
            tracer_provider=self._tracer_provider,
            text_map_propagator=TraceContextTextMapPropagator(),
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_spans_exist(self._span_exporter)
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

    def testTracesForUnaryUnaryCallWithoutPropagator(self):
        with grpc_observability.OpenTelemetryPlugin(
            tracer_provider=self._tracer_provider,
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_spans_exist(self._span_exporter)
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

    def testTracesForTwoUnaryUnaryCallsWithContextManager(self):
        with grpc_observability.OpenTelemetryPlugin(
            tracer_provider=self._tracer_provider,
            text_map_propagator=TraceContextTextMapPropagator(),
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)
            _test_server.unary_unary_call(port=port)

        self._validate_spans_exist(self._span_exporter)

        # for each unary unary server call, 3 spans are created, which must be
        # validated separately
        first_rpc_spans = self._span_exporter.get_finished_spans()[0:3]
        second_rpc_spans = self._span_exporter.get_finished_spans()[3:]
        self._validate_spans(
            spans=first_rpc_spans,
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
        self._validate_spans(
            spans=second_rpc_spans,
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

    def testMetricsForUnaryUnaryCallWithGlobalInit(self):
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider,
        )
        otel_plugin.register_global()

        server, port = _test_server.start_server()
        self._server = server
        _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        otel_plugin.deregister_global()

    def testTracesForUnaryUnaryCallWithGlobalInit(self):
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            tracer_provider=self._tracer_provider,
            text_map_propagator=TraceContextTextMapPropagator(),
        )
        otel_plugin.register_global()

        server, port = _test_server.start_server()
        self._server = server
        _test_server.unary_unary_call(port=port)

        self._validate_spans_exist(self._span_exporter)
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
        otel_plugin.deregister_global()

    def testCallGlobalInitThrowErrorWhenGlobalCalled(self):
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        )
        otel_plugin.register_global()
        try:
            otel_plugin.register_global()
        except RuntimeError as exp:
            self.assertIn(
                "gPRC Python observability was already initialized", str(exp)
            )

        otel_plugin.deregister_global()

    def testCallGlobalInitThrowErrorWhenContextManagerCalled(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        ):
            try:
                otel_plugin = grpc_observability.OpenTelemetryPlugin(
                    meter_provider=self._meter_provider
                )
                otel_plugin.register_global()
            except RuntimeError as exp:
                self.assertIn(
                    "gPRC Python observability was already initialized",
                    str(exp),
                )

    def testCallContextManagerThrowErrorWhenGlobalInitCalled(self):
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        )
        otel_plugin.register_global()
        try:
            with grpc_observability.OpenTelemetryPlugin(
                meter_provider=self._meter_provider
            ):
                pass
        except RuntimeError as exp:
            self.assertIn(
                "gPRC Python observability was already initialized", str(exp)
            )
        otel_plugin.deregister_global()

    def testContextManagerThrowErrorWhenContextManagerCalled(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        ):
            try:
                with grpc_observability.OpenTelemetryPlugin(
                    meter_provider=self._meter_provider
                ):
                    pass
            except RuntimeError as exp:
                self.assertIn(
                    "gPRC Python observability was already initialized",
                    str(exp),
                )

    def testNoErrorCallGlobalInitThenContextManager(self):
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        )
        otel_plugin.register_global()
        otel_plugin.deregister_global()

        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        ):
            pass

    def testNoErrorCallContextManagerThenGlobalInit(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        ):
            pass
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        )
        otel_plugin.register_global()
        otel_plugin.deregister_global()

    def testRecordUnaryUnaryWithClientInterceptor(self):
        interceptor = _ClientUnaryUnaryInterceptor()
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.intercepted_unary_unary_call(
                port=port, interceptors=interceptor
            )

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    def testRecordUnaryUnaryWithServerInterceptor(self):
        interceptor = _ServerInterceptor()
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        ):
            server, port = _test_server.start_server(interceptors=[interceptor])
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    def testRecordUnaryUnaryClientOnly(self):
        server, port = _test_server.start_server()
        self._server = server

        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        ):
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_client_metrics_names(self.all_metrics)

    def testNoRecordBeforeInit(self):
        server, port = _test_server.start_server()
        _test_server.unary_unary_call(port=port)
        self.assertEqual(len(self.all_metrics), 0)
        server.stop(0)

        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    def testNoRecordAfterExitUseContextManager(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        ):
            server, port = _test_server.start_server()
            self._server = server
            self._port = port
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

        self.all_metrics = collections.defaultdict(list)
        _test_server.unary_unary_call(port=self._port)
        with self.assertRaisesRegex(AssertionError, "No metrics was exported"):
            self._validate_metrics_exist(self.all_metrics)

    def testNoRecordAfterExitUseGlobal(self):
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        )
        otel_plugin.register_global()

        server, port = _test_server.start_server()
        self._server = server
        self._port = port
        _test_server.unary_unary_call(port=port)
        otel_plugin.deregister_global()

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

        self.all_metrics = collections.defaultdict(list)
        _test_server.unary_unary_call(port=self._port)
        with self.assertRaisesRegex(AssertionError, "No metrics was exported"):
            self._validate_metrics_exist(self.all_metrics)

    def testMetricsForUnaryStreamCall(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider,
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_stream_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    def testTracesForUnaryStreamCall(self):
        with grpc_observability.OpenTelemetryPlugin(
            tracer_provider=self._tracer_provider,
            text_map_propagator=TraceContextTextMapPropagator(),
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_stream_call(port=port)

        self._validate_spans_exist(self._span_exporter)
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

    def testMetricsForStreamUnaryCall(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider,
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    def testTracesForStreamUnaryCall(self):
        with grpc_observability.OpenTelemetryPlugin(
            tracer_provider=self._tracer_provider,
            text_map_propagator=TraceContextTextMapPropagator(),
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_unary_call(port=port)

        self._validate_spans_exist(self._span_exporter)
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

    def testMetricsForStreamStreamCall(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider,
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_stream_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    def testTracesForStreamStreamCall(self):
        with grpc_observability.OpenTelemetryPlugin(
            tracer_provider=self._tracer_provider,
            text_map_propagator=TraceContextTextMapPropagator(),
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_stream_call(port=port)

        self._validate_spans_exist(self._span_exporter)
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

        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider,
            target_attribute_filter=target_filter,
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
        # method_filter should replace method name 'test/UnaryUnaryFiltered' with 'other'.
        FILTERED_METHOD_NAME = "test/UnaryUnaryFiltered"

        def method_filter(method: str) -> bool:
            if FILTERED_METHOD_NAME in method:
                return False
            return True

        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider,
            generic_method_attribute_filter=method_filter,
        ):
            server, port = _test_server.start_server(register_method=False)
            self._server = server
            _test_server.unary_unary_call(port=port, registered_method=True)
            _test_server.unary_unary_filtered_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        method_values = set()
        for label_list in self.all_metrics.values():
            for labels in label_list:
                if GRPC_METHOD_LABEL in labels:
                    method_values.add(labels[GRPC_METHOD_LABEL])
        self.assertTrue(GRPC_OTHER_LABEL_VALUE in method_values)
        self.assertTrue(FILTERED_METHOD_NAME not in method_values)

    def testClientNonRegisteredMethod(self):
        UNARY_METHOD_NAME = "test/UnaryUnary"

        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        ):
            server, port = _test_server.start_server(register_method=True)
            self._server = server
            _test_server.unary_unary_call(port=port, registered_method=False)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        client_method_values = set()
        server_method_values = set()
        for metric_name, label_list in self.all_metrics.items():
            for labels in label_list:
                if GRPC_METHOD_LABEL in labels:
                    if "grpc.client" in metric_name:
                        client_method_values.add(labels[GRPC_METHOD_LABEL])
                    elif "grpc.server" in metric_name:
                        server_method_values.add(labels[GRPC_METHOD_LABEL])
        # For client metrics, all method name should be replaced with 'other'.
        self.assertTrue(GRPC_OTHER_LABEL_VALUE in client_method_values)
        self.assertTrue(UNARY_METHOD_NAME not in client_method_values)

        # For server metrics, all method name should be 'test/UnaryUnary'.
        self.assertTrue(GRPC_OTHER_LABEL_VALUE not in server_method_values)
        self.assertTrue(UNARY_METHOD_NAME in server_method_values)

    def testServerNonRegisteredMethod(self):
        UNARY_METHOD_NAME = "test/UnaryUnary"

        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._meter_provider
        ):
            server, port = _test_server.start_server(register_method=False)
            self._server = server
            _test_server.unary_unary_call(port=port, registered_method=True)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        client_method_values = set()
        server_method_values = set()
        for metric_name, label_list in self.all_metrics.items():
            for labels in label_list:
                if GRPC_METHOD_LABEL in labels:
                    if "grpc.client" in metric_name:
                        client_method_values.add(labels[GRPC_METHOD_LABEL])
                    elif "grpc.server" in metric_name:
                        server_method_values.add(labels[GRPC_METHOD_LABEL])
        # For client metrics, all method name should be 'test/UnaryUnary'.
        self.assertTrue(GRPC_OTHER_LABEL_VALUE not in client_method_values)
        self.assertTrue(UNARY_METHOD_NAME in client_method_values)

        # For server metrics, all method name should be replaced with 'other'.
        self.assertTrue(GRPC_OTHER_LABEL_VALUE in server_method_values)
        self.assertTrue(UNARY_METHOD_NAME not in server_method_values)

    def testWrongPluginConfigurationForTracing(self):
        class UserDefinedIdGenerator(otel_trace.IdGenerator):
            def generate_span_id(self) -> int:
                return 0

            def generate_trace_id(self) -> int:
                return 0

        otel_tracer_provider = otel_trace.TracerProvider(
            id_generator=UserDefinedIdGenerator()
        )
        with self.assertRaisesRegex(
            ValueError, "User-defined IdGenerators are not allowed."
        ):
            grpc_observability.OpenTelemetryPlugin(
                tracer_provider=otel_tracer_provider,
                text_map_propagator=TraceContextTextMapPropagator(),
            )

    def testWrongPluginConfigurationForTracingWithoutPropagator(self):
        class UserDefinedIdGenerator(otel_trace.IdGenerator):
            def generate_span_id(self) -> int:
                return 0

            def generate_trace_id(self) -> int:
                return 0

        otel_tracer_provider = otel_trace.TracerProvider(
            id_generator=UserDefinedIdGenerator()
        )
        with self.assertRaisesRegex(
            ValueError, "User-defined IdGenerators are not allowed."
        ):
            grpc_observability.OpenTelemetryPlugin(
                tracer_provider=otel_tracer_provider,
            )

    def testSpanStatusForUnimplementedRpcMethod(self):
        with grpc_observability.OpenTelemetryPlugin(
            tracer_provider=self._tracer_provider,
            text_map_propagator=TraceContextTextMapPropagator(),
        ):
            server, port = _test_server.start_server()
            self._server = server
            # Call a non existent method to trigger UNIMPLEMENTED status
            with grpc.insecure_channel(f"localhost:{port}") as channel:
                multi_callable = channel.unary_unary("/test/NonExistent")
                try:
                    multi_callable(b"\x00\x00\x00")
                except grpc.RpcError:
                    pass

        self._validate_spans_exist(self._span_exporter)
        spans = self._span_exporter.get_finished_spans()
        self.assertTrue(len(spans) == 3)

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
        self.assertFalse(client_span.status.is_ok)
        self.assertIn("UNIMPLEMENTED", client_span.status.description)

        self.assertFalse(attempt_span.status.is_ok)
        self.assertIn("UNIMPLEMENTED", attempt_span.status.description)

        self.assertFalse(server_span.status.is_ok)
        self.assertIn("UNIMPLEMENTED", server_span.status.description)

        # validate parent-child relationship
        self.assertEqual(
            client_span.get_span_context().trace_id,
            attempt_span.get_span_context().trace_id,
        )
        self.assertEqual(
            attempt_span.parent.span_id, client_span.get_span_context().span_id
        )
        self.assertEqual(
            attempt_span.get_span_context().trace_id,
            server_span.get_span_context().trace_id,
        )
        self.assertEqual(
            server_span.parent.span_id, attempt_span.get_span_context().span_id
        )

    @unittest.expectedFailure
    # TODO(Zgoda): Implement header injection with map propagator object
    def testTraceparentHeaderInjectedWithTextMapPropagator(self):
        interceptor = _MetadataCapturingServerInterceptor()

        with grpc_observability.OpenTelemetryPlugin(
            tracer_provider=self._tracer_provider,
            text_map_propagator=TraceContextTextMapPropagator(),
        ):
            server, port = _test_server.start_server(
                interceptors=(interceptor,)
            )
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_spans_exist(self._span_exporter)
        spans = self._span_exporter.get_finished_spans()

        client_span = next(
            (span for span in spans if span.name.startswith("Sent.")), None
        )
        self.assertIsNotNone(client_span)
        expected_trace_id = format(
            client_span.get_span_context().trace_id, "032x"
        )

        self.assertEqual(len(interceptor.captured_metadata), 1)
        metadata = interceptor.captured_metadata[0]
        self.assertIn(
            "traceparent",
            metadata,
            msg=(
                f"traceparent header not found in metadata: ",
                f"{list(metadata.keys())}",
            ),
        )

        traceparent = metadata["traceparent"]
        parts = traceparent.split("-")
        self.assertEqual(
            len(parts), 4, msg=f"Invalid traceparent format: {traceparent}"
        )
        self.assertEqual(parts[0], "00", msg="Expected traceparent version 00")
        self.assertEqual(
            parts[1],
            expected_trace_id,
            msg=(
                f"TraceID mismatch: traceparent has {parts[1]}, ",
                f"span has {expected_trace_id}",
            ),
        )

    def testServiceToServiceTraceIdPropagation(self):
        with grpc_observability.OpenTelemetryPlugin(
            tracer_provider=self._tracer_provider,
            text_map_propagator=TraceContextTextMapPropagator(),
        ):
            server, port = _test_server.start_server()
            self._server = server
            # Trigger nested RPC to a new server
            _test_server.unary_unary_call(
                port=port,
                metadata=[
                    _test_server.TRIGGER_RPC_METADATA,
                    _test_server.TRIGGER_RPC_TO_NEW_SERVER_METADATA,
                ],
            )

        self._validate_spans_exist(self._span_exporter)
        spans = self._span_exporter.get_finished_spans()

        # Expect 6 spans: Sent/Attempt/Recv for original RPC +
        # Sent/Attempt/Recv for the nested RPC to the second server
        self.assertEqual(
            len(spans), 6, msg=f"Expected 6 spans, got: {len(spans)}"
        )

        # All spans must share the same trace ID
        trace_ids = {span.get_span_context().trace_id for span in spans}
        self.assertEqual(
            len(trace_ids),
            1,
            msg=(
                f"Expected all spans to share one trace ID, ",
                f"got {len(trace_ids)} distinct trace IDs",
            ),
        )

        span_ids = {span.get_span_context().span_id for span in spans}

        root_client_span = next(
            s
            for s in spans
            if s.name.startswith("Sent.")
            and not s.parent
        )
        self.assertIsNotNone(root_client_span, "Root client span not found")

        attempt_span = next(
            s
            for s in spans 
            if s.name.startswith("Attempt.")
            and (
                s.parent.span_id == root_client_span.get_span_context().span_id
            )
        )
        self.assertIsNotNone(attempt_span, "Attempt span not found")

        propagating_server_span = next(
            s
            for s in spans 
            if s.name.startswith("Recv.")
            and (
                s.parent.span_id == attempt_span.get_span_context().span_id
            )
        )
        self.assertIsNotNone(
            propagating_server_span, "Propagating server span not found"
        )

        propagating_client_span = next(
            s
            for s in spans 
            if s.name.startswith("Sent.")
            and (
                s.parent.span_id ==
                propagating_server_span.get_span_context().span_id
            )
        )
        self.assertIsNotNone(
            propagating_client_span, "Propagating client span not found"
        )

        propagating_attempt_span = next(
            s
            for s in spans 
            if s.name.startswith("Attempt.")
            and (
                s.parent.span_id ==
                propagating_client_span.get_span_context().span_id
            )
        )
        self.assertIsNotNone(
            propagating_attempt_span, "Propagating attempt span not found"
        )

        server_span = next(
            s
            for s in spans 
            if s.name.startswith("Recv.")
            and (
                s.parent.span_id ==
                propagating_attempt_span.get_span_context().span_id
            )
        )
        self.assertIsNotNone(server_span, "Server span not found")

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

    def _validate_metrics_exist(self, all_metrics: Dict[str, Any]) -> None:
        # Sleep here to make sure we have at least one export from OTel otel_metrics_export.MetricExporter.
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

    def _validate_spans_exist(
        self, span_exporter: otel_trace_export.SpanExporter
    ):
        # Sleep here to make sure we have at least one export from
        # OTel SpanExporter.
        self.assert_eventually(
            lambda: len(span_exporter.get_finished_spans()) > 1,
            message=lambda: f"No traces were exported",
        )

    def _validate_spans(
        self,
        spans: Sequence[otel_trace.ReadableSpan],
        expected_span_size: int,
        expected_server_events: Sequence[Tuple[str, Dict[str, str]]],
        expected_attempt_events: Sequence[Tuple[str, Dict[str, str]]],
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
            attempt_span.get_span_context().trace_id,
        )
        self.assertEqual(
            attempt_span.parent.span_id, client_span.get_span_context().span_id
        )
        self.assertEqual(
            attempt_span.get_span_context().trace_id,
            server_span.get_span_context().trace_id,
        )
        self.assertEqual(
            server_span.parent.span_id, attempt_span.get_span_context().span_id
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
