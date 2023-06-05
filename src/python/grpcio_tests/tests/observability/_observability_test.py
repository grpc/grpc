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

from concurrent import futures
import json
import logging
import os
import random
from typing import Any, Dict, List
import unittest

import grpc
import grpc_observability
from grpc_observability import _cyobservability
from grpc_observability import _observability

logger = logging.getLogger(__name__)

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x00'

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'
STREAM_LENGTH = 5

CONFIG_ENV_VAR_NAME = 'GRPC_GCP_OBSERVABILITY_CONFIG'
CONFIG_FILE_ENV_VAR_NAME = 'GRPC_GCP_OBSERVABILITY_CONFIG_FILE'

_VALID_CONFIG_TRACING_STATS = {
    'project_id': 'test-project',
    'cloud_trace': {
        'sampling_rate': 1.00
    },
    'cloud_monitoring': {}
}
_VALID_CONFIG_TRACING_ONLY = {
    'project_id': 'test-project',
    'cloud_trace': {
        'sampling_rate': 1.00
    },
}
_VALID_CONFIG_STATS_ONLY = {
    'project_id': 'test-project',
    'cloud_monitoring': {}
}
_VALID_CONFIG_STATS_ONLY_STR = """
{
    'project_id': 'test-project',
    'cloud_monitoring': {}
}
"""
# Depends on grpc_core::IsTransportSuppliesClientLatencyEnabled,
# the following metrcis might not exist.
_SKIP_VEFIRY = [_cyobservability.MetricsName.CLIENT_TRANSPORT_LATENCY]
_SPAN_PREFIXS = ['Recv', 'Sent', 'Attempt']


class TestExporter(_observability.Exporter):

    def __init__(self, metrics: List[_observability.StatsData],
                 spans: List[_observability.TracingData]):
        self.span_collecter = spans
        self.metric_collecter = metrics
        self._server = None

    def export_stats_data(self,
                          stats_data: List[_observability.StatsData]) -> None:
        self.metric_collecter.extend(stats_data)

    def export_tracing_data(
            self, tracing_data: List[_observability.TracingData]) -> None:
        self.span_collecter.extend(tracing_data)


def handle_unary_unary(request, servicer_context):
    return _RESPONSE


def handle_unary_stream(request, servicer_context):
    for _ in range(STREAM_LENGTH):
        yield _RESPONSE


def handle_stream_unary(request_iterator, servicer_context):
    return _RESPONSE


def handle_stream_stream(request_iterator, servicer_context):
    for request in request_iterator:
        yield _RESPONSE


class _MethodHandler(grpc.RpcMethodHandler):

    def __init__(self, request_streaming, response_streaming):
        self.request_streaming = request_streaming
        self.response_streaming = response_streaming
        self.request_deserializer = None
        self.response_serializer = None
        self.unary_unary = None
        self.unary_stream = None
        self.stream_unary = None
        self.stream_stream = None
        if self.request_streaming and self.response_streaming:
            self.stream_stream = lambda x, y: handle_stream_stream(x, y)
        elif self.request_streaming:
            self.stream_unary = lambda x, y: handle_stream_unary(x, y)
        elif self.response_streaming:
            self.unary_stream = lambda x, y: handle_unary_stream(x, y)
        else:
            self.unary_unary = lambda x, y: handle_unary_unary(x, y)


class _GenericHandler(grpc.GenericRpcHandler):

    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return _MethodHandler(False, False)
        elif handler_call_details.method == _UNARY_STREAM:
            return _MethodHandler(False, True)
        elif handler_call_details.method == _STREAM_UNARY:
            return _MethodHandler(True, False)
        elif handler_call_details.method == _STREAM_STREAM:
            return _MethodHandler(True, True)
        else:
            return None


class ObservabilityTest(unittest.TestCase):

    def setUp(self):
        self.all_metric = []
        self.all_span = []
        self.test_exporter = TestExporter(self.all_metric, self.all_span)
        self._server = None
        self._port = None

    def tearDown(self):
        os.environ[CONFIG_ENV_VAR_NAME] = ''
        os.environ[CONFIG_FILE_ENV_VAR_NAME] = ''
        if self._server:
            self._server.stop(0)

    def testRecordUnaryUnary(self):
        self._set_config_file(_VALID_CONFIG_TRACING_STATS)
        with grpc_observability.GCPOpenCensusObservability(
                exporter=self.test_exporter):
            self._start_server()
            self.unary_unary_call()

        self.assertGreater(len(self.all_metric), 0)
        self.assertGreater(len(self.all_span), 0)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

    def testThrowErrorWithoutConfig(self):
        with self.assertRaises(ValueError):
            with grpc_observability.GCPOpenCensusObservability(
                    exporter=self.test_exporter):
                pass

    def testThrowErrorWithInvalidConfig(self):
        _INVALID_CONFIG = 'INVALID'
        self._set_config_file(_INVALID_CONFIG)
        with self.assertRaises(ValueError):
            with grpc_observability.GCPOpenCensusObservability(
                    exporter=self.test_exporter):
                pass

    def testNoErrorAndDataWithEmptyConfig(self):
        _EMPTY_CONFIG = {}
        self._set_config_file(_EMPTY_CONFIG)
        # Empty config still require project_id
        os.environ['GCP_PROJECT'] = 'test-project'
        with grpc_observability.GCPOpenCensusObservability(
                exporter=self.test_exporter):
            self._start_server()
            self.unary_unary_call()

        self.assertEqual(len(self.all_metric), 0)
        self.assertEqual(len(self.all_span), 0)

    def testThrowErrorWhenCallingMultipleInit(self):
        self._set_config_file(_VALID_CONFIG_TRACING_STATS)
        with self.assertRaises(ValueError):
            with grpc_observability.GCPOpenCensusObservability(
                    exporter=self.test_exporter) as o11y:
                grpc._observability.observability_init(o11y)

    def testRecordUnaryUnaryStatsOnly(self):
        self._set_config_file(_VALID_CONFIG_STATS_ONLY)
        with grpc_observability.GCPOpenCensusObservability(
                exporter=self.test_exporter):
            self._start_server()
            self.unary_unary_call()

        self.assertEqual(len(self.all_span), 0)
        self.assertGreater(len(self.all_metric), 0)
        self._validate_metrics(self.all_metric)

    def testRecordUnaryUnaryTracingOnly(self):
        self._set_config_file(_VALID_CONFIG_TRACING_ONLY)
        with grpc_observability.GCPOpenCensusObservability(
                exporter=self.test_exporter):
            self._start_server()
            self.unary_unary_call()

        self.assertEqual(len(self.all_metric), 0)
        self.assertGreater(len(self.all_span), 0)
        self._validate_spans(self.all_span)

    def testRecordUnaryStream(self):
        self._set_config_file(_VALID_CONFIG_TRACING_STATS)
        with grpc_observability.GCPOpenCensusObservability(
                exporter=self.test_exporter):
            self._start_server()
            self.unary_stream_call()

        self.assertGreater(len(self.all_metric), 0)
        self.assertGreater(len(self.all_span), 0)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

    def testRecordStreamUnary(self):
        self._set_config_file(_VALID_CONFIG_TRACING_STATS)
        with grpc_observability.GCPOpenCensusObservability(
                exporter=self.test_exporter):
            self._start_server()
            self.stream_unary_call()

        self.assertTrue(len(self.all_metric) > 0)
        self.assertTrue(len(self.all_span) > 0)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

    def testRecordStreamStream(self):
        self._set_config_file(_VALID_CONFIG_TRACING_STATS)
        with grpc_observability.GCPOpenCensusObservability(
                exporter=self.test_exporter):
            self._start_server()
            self.stream_stream_call()

        self.assertGreater(len(self.all_metric), 0)
        self.assertGreater(len(self.all_span), 0)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

    def testNoRecordBeforeInit(self):
        self._set_config_file(_VALID_CONFIG_TRACING_STATS)
        self._start_server()
        self.unary_unary_call()
        self.assertEqual(len(self.all_metric), 0)
        self.assertEqual(len(self.all_span), 0)
        self._server.stop(0)

        with grpc_observability.GCPOpenCensusObservability(
                exporter=self.test_exporter):
            self._start_server()
            self.unary_unary_call()

        self.assertGreater(len(self.all_metric), 0)
        self.assertGreater(len(self.all_span), 0)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

    def testNoRecordAfterExit(self):
        self._set_config_file(_VALID_CONFIG_TRACING_STATS)
        with grpc_observability.GCPOpenCensusObservability(
                exporter=self.test_exporter):
            self._start_server()
            self.unary_unary_call()

        self.assertGreater(len(self.all_metric), 0)
        self.assertGreater(len(self.all_span), 0)
        current_metric_len = len(self.all_metric)
        current_spans_len = len(self.all_span)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

        self.unary_unary_call()
        self.assertEqual(len(self.all_metric), current_metric_len)
        self.assertEqual(len(self.all_span), current_spans_len)

    def testTraceSamplingRate(self):
        # Make 40 UnaryCall's
        # With 50% sampling rate, we should get 10-30 traces with >99.93% probability
        # Each trace will have three span (Send, Recv, Attempt)
        _CALLS = 40
        _LOWER_BOUND = 10 * 3
        _HIGHER_BOUND = 30 * 3
        _VALID_CONFIG_TRACING_ONLY_SAMPLE_HALF = {
            'project_id': 'test-project',
            'cloud_trace': {
                'sampling_rate': 0.5
            },
        }
        self._set_config_file(_VALID_CONFIG_TRACING_ONLY_SAMPLE_HALF)
        with grpc_observability.GCPOpenCensusObservability(
                exporter=self.test_exporter):
            self._start_server()
            for _ in range(_CALLS):
                self.unary_unary_call()

        self.assertEqual(len(self.all_metric), 0)
        self.assertGreaterEqual(len(self.all_span), _LOWER_BOUND)
        self.assertLessEqual(len(self.all_span), _HIGHER_BOUND)
        self._validate_spans(self.all_span)

    def testConfigFileOverEnvVar(self):
        # env var have only stats enabled
        os.environ[CONFIG_ENV_VAR_NAME] = _VALID_CONFIG_STATS_ONLY_STR
        # config_file have only tracing enabled
        self._set_config_file(_VALID_CONFIG_TRACING_ONLY)

        with grpc_observability.GCPOpenCensusObservability(
                exporter=self.test_exporter):
            self._start_server()
            self.unary_unary_call()

        self.assertEqual(len(self.all_metric), 0)
        self.assertGreater(len(self.all_span), 0)
        self._validate_spans(self.all_span)

    def _set_config_file(self, config: Dict[str, Any]) -> None:
        # Using random name here so multiple tests can run with different config files.
        config_file_path = '/tmp/' + str(random.randint(0, 100000))
        with open(config_file_path, 'w', encoding='utf-8') as f:
            f.write(json.dumps(config))
        os.environ[CONFIG_FILE_ENV_VAR_NAME] = config_file_path

    def unary_unary_call(self):
        with grpc.insecure_channel(f'localhost:{self._port}') as channel:
            multi_callable = channel.unary_unary(_UNARY_UNARY)
            unused_response, call = multi_callable.with_call(_REQUEST)

    def unary_stream_call(self):
        with grpc.insecure_channel(f'localhost:{self._port}') as channel:
            multi_callable = channel.unary_stream(_UNARY_STREAM)
            call = multi_callable(_REQUEST)
            for _ in call:
                pass

    def stream_unary_call(self):
        with grpc.insecure_channel(f'localhost:{self._port}') as channel:
            multi_callable = channel.stream_unary(_STREAM_UNARY)
            unused_response, call = multi_callable.with_call(
                iter([_REQUEST] * STREAM_LENGTH))

    def stream_stream_call(self):
        with grpc.insecure_channel(f'localhost:{self._port}') as channel:
            multi_callable = channel.stream_stream(_STREAM_STREAM)
            call = multi_callable(iter([_REQUEST] * STREAM_LENGTH))
            for _ in call:
                pass

    def _start_server(self) -> None:
        self._server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        self._port = self._server.add_insecure_port('[::]:0')

        self._server.start()

    def _validate_metrics(self,
                          metrics: List[_observability.StatsData]) -> None:
        metric_names = set(metric.name for metric in metrics)
        for name in _cyobservability.MetricsName:
            if name in _SKIP_VEFIRY:
                continue
            if name not in metric_names:
                logger.error('metric %s not found in exported metrics: %s!',
                             name, metric_names)
            self.assertTrue(name in metric_names)

    def _validate_spans(self,
                        tracing_data: List[_observability.TracingData]) -> None:
        span_names = set(data.name for data in tracing_data)
        for prefix in _SPAN_PREFIXS:
            prefix_exist = any(prefix in name for name in span_names)
            if not prefix_exist:
                logger.error(
                    'missing span with prefix %s in exported spans: %s!',
                    prefix, span_names)
            self.assertTrue(prefix_exist)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
