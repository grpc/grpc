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
from typing import List
import unittest

import grpc
import grpc_observability
from grpc_observability import _cyobservability
from grpc_observability import _observability

from tests.observability import _test_server

logger = logging.getLogger(__name__)

STREAM_LENGTH = 5


class TestExporter(_observability.Exporter):
    def __init__(
        self,
        metrics: List[_observability.StatsData],
        spans: List[_observability.TracingData],
    ):
        self.span_collecter = spans
        self.metric_collecter = metrics
        self._server = None

    def export_stats_data(
        self, stats_data: List[_observability.StatsData]
    ) -> None:
        self.metric_collecter.extend(stats_data)

    def export_tracing_data(
        self, tracing_data: List[_observability.TracingData]
    ) -> None:
        self.span_collecter.extend(tracing_data)


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
class ObservabilityTest(unittest.TestCase):
    def setUp(self):
        self.all_metric = []
        self.all_span = []
        self.test_exporter = TestExporter(self.all_metric, self.all_span)
        self._server = None
        self._port = None

    def tearDown(self):
        if self._server:
            self._server.stop(0)

    def testRecordUnaryUnary(self):
        with grpc_observability.OpenTelemetryObservability(
            exporter=self.test_exporter
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)

        self.assertGreater(len(self.all_metric), 0)
        self._validate_metrics(self.all_metric)

    def testRecordUnaryUnaryWithClientInterceptor(self):
        interceptor = _ClientUnaryUnaryInterceptor()
        with grpc_observability.OpenTelemetryObservability(
            exporter=self.test_exporter
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.intercepted_unary_unary_call(
                port=port, interceptors=interceptor
            )

        self.assertGreater(len(self.all_metric), 0)
        self._validate_metrics(self.all_metric)

    def testRecordUnaryUnaryWithServerInterceptor(self):
        interceptor = _ServerInterceptor()
        with grpc_observability.OpenTelemetryObservability(
            exporter=self.test_exporter
        ):
            server, port = _test_server.start_server(interceptors=[interceptor])
            self._server = server
            _test_server.unary_unary_call(port=port)

        self.assertGreater(len(self.all_metric), 0)
        self._validate_metrics(self.all_metric)

    def testThrowErrorWhenCallingMultipleInit(self):
        with self.assertRaises(ValueError):
            with grpc_observability.OpenTelemetryObservability(
                exporter=self.test_exporter
            ) as o11y:
                grpc._observability.observability_init(o11y)

    def testRecordUnaryStream(self):
        with grpc_observability.OpenTelemetryObservability(
            exporter=self.test_exporter
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_stream_call(port=port)

        self.assertGreater(len(self.all_metric), 0)
        self._validate_metrics(self.all_metric)

    def testRecordStreamUnary(self):
        with grpc_observability.OpenTelemetryObservability(
            exporter=self.test_exporter
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_unary_call(port=port)

        self.assertTrue(len(self.all_metric) > 0)
        self._validate_metrics(self.all_metric)

    def testRecordStreamStream(self):
        with grpc_observability.OpenTelemetryObservability(
            exporter=self.test_exporter
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_stream_call(port=port)

        self.assertGreater(len(self.all_metric), 0)
        self._validate_metrics(self.all_metric)

    def testNoRecordBeforeInit(self):
        server, port = _test_server.start_server()
        _test_server.unary_unary_call(port=port)
        self.assertEqual(len(self.all_metric), 0)
        server.stop(0)

        with grpc_observability.OpenTelemetryObservability(
            exporter=self.test_exporter
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)

        self.assertGreater(len(self.all_metric), 0)
        self._validate_metrics(self.all_metric)

    def testNoRecordAfterExit(self):
        with grpc_observability.OpenTelemetryObservability(
            exporter=self.test_exporter
        ):
            server, port = _test_server.start_server()
            self._server = server
            self._port = port
            _test_server.unary_unary_call(port=port)

        self.assertGreater(len(self.all_metric), 0)
        current_metric_len = len(self.all_metric)
        self._validate_metrics(self.all_metric)

        _test_server.unary_unary_call(port=self._port)
        self.assertEqual(len(self.all_metric), current_metric_len)

    def _validate_metrics(
        self, metrics: List[_observability.StatsData]
    ) -> None:
        metric_names = set(metric.name for metric in metrics)
        for name in _cyobservability.MetricsName:
            if name not in metric_names:
                logger.error(
                    "metric %s not found in exported metrics: %s!",
                    name,
                    metric_names,
                )
            self.assertTrue(name in metric_names)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
