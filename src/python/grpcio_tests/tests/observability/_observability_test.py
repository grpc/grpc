from concurrent import futures
import logging
import os
from typing import List
import unittest

import grpc
import grpc_observability

logger = logging.getLogger(__name__)

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x00'

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'
STREAM_LENGTH = 5

_VALID_CONFIG_TRACING_STATS = """
{
    "project_id":"test-project",
    "cloud_trace":{
       "sampling_rate":1.00
    },
    "cloud_monitoring":{},
}
"""

_VALID_CONFIG_TRACING_ONLY = """
{
    "project_id":"test-project",
    "cloud_trace":{
       "sampling_rate":1.00
    }
}
"""
_VALID_CONFIG_STATS_ONLY = """
{
    "project_id":"test-project",
    "cloud_monitoring":{}
}
"""
_INVALID_CONFIG = 'INVALID'
# The following metrcis might not exist
_SKIP_VEFIRY = [grpc_observability.MetricsName.CLIENT_TRANSPORT_LATENCY]
_SPAN_PREFIXS = ['Recv', 'Sent', 'Attempt']


class TestExporter(grpc_observability.Exporter):

    def __init__(self, metrics: List[grpc_observability.StatsData],
                 spans: List[grpc_observability.TracingData]):
        self.span_collecter = spans
        self.metric_collecter = metrics
        self._server = None

    def export_stats_data(
            self, stats_data: List[grpc_observability.StatsData]) -> None:
        self.metric_collecter.extend(stats_data)

    def export_tracing_data(
            self, tracing_data: List[grpc_observability.TracingData]) -> None:
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

    def tearDown(self):
        os.environ['GRPC_GCP_OBSERVABILITY_CONFIG'] = ''

    def testRecordUnaryUnary(self):
        os.environ[
            'GRPC_GCP_OBSERVABILITY_CONFIG'] = _VALID_CONFIG_TRACING_STATS
        with grpc_observability.GCPOpenCensusObservability() as o11y:
            o11y.init(exporter=self.test_exporter)

            port = self._start_server()
            self.unary_unary_call(port)

        self.assertTrue(len(self.all_metric) > 0)
        self.assertTrue(len(self.all_span) > 0)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

        self._server.stop(0)

    def testThrowErrorWithoutConfig(self):
        with self.assertRaises(ValueError):
            with grpc_observability.GCPOpenCensusObservability() as o11y:
                o11y.init(exporter=self.test_exporter)

    def testThrowErrorWithInvalidConfig(self):
        os.environ['GRPC_GCP_OBSERVABILITY_CONFIG'] = _INVALID_CONFIG
        with self.assertRaises(ValueError):
            with grpc_observability.GCPOpenCensusObservability() as o11y:
                o11y.init(exporter=self.test_exporter)

    def testRecordUnaryUnaryStatsOnly(self):
        os.environ['GRPC_GCP_OBSERVABILITY_CONFIG'] = _VALID_CONFIG_STATS_ONLY
        with grpc_observability.GCPOpenCensusObservability() as o11y:
            o11y.init(exporter=self.test_exporter)

            port = self._start_server()
            self.unary_unary_call(port)

        self.assertEqual(len(self.all_span), 0)
        self.assertTrue(len(self.all_metric) > 0)
        self._validate_metrics(self.all_metric)

        self._server.stop(0)

    def testRecordUnaryUnaryTracingOnly(self):
        os.environ['GRPC_GCP_OBSERVABILITY_CONFIG'] = _VALID_CONFIG_TRACING_ONLY
        with grpc_observability.GCPOpenCensusObservability() as o11y:
            o11y.init(exporter=self.test_exporter)

            port = self._start_server()
            self.unary_unary_call(port)

        self.assertEqual(len(self.all_metric), 0)
        self.assertTrue(len(self.all_span) > 0)
        self._validate_spans(self.all_span)

        self._server.stop(0)

    def testRecordUnaryStream(self):
        os.environ[
            'GRPC_GCP_OBSERVABILITY_CONFIG'] = _VALID_CONFIG_TRACING_STATS
        with grpc_observability.GCPOpenCensusObservability() as o11y:
            o11y.init(exporter=self.test_exporter)

            port = self._start_server()
            self.unary_stream_call(port)

        self.assertTrue(len(self.all_metric) > 0)
        self.assertTrue(len(self.all_span) > 0)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

        self._server.stop(0)

    def testRecordStreamUnary(self):
        os.environ[
            'GRPC_GCP_OBSERVABILITY_CONFIG'] = _VALID_CONFIG_TRACING_STATS
        with grpc_observability.GCPOpenCensusObservability() as o11y:
            o11y.init(exporter=self.test_exporter)

            port = self._start_server()
            self.stream_unary_call(port)

        self.assertTrue(len(self.all_metric) > 0)
        self.assertTrue(len(self.all_span) > 0)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

        self._server.stop(0)

    def testRecordStreamStream(self):
        os.environ[
            'GRPC_GCP_OBSERVABILITY_CONFIG'] = _VALID_CONFIG_TRACING_STATS
        with grpc_observability.GCPOpenCensusObservability() as o11y:
            o11y.init(exporter=self.test_exporter)

            port = self._start_server()
            self.stream_stream_call(port)

        self.assertTrue(len(self.all_metric) > 0)
        self.assertTrue(len(self.all_span) > 0)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

        self._server.stop(0)

    def testNoRecordBeforeInit(self):
        os.environ[
            'GRPC_GCP_OBSERVABILITY_CONFIG'] = _VALID_CONFIG_TRACING_STATS
        port = self._start_server()
        self.unary_unary_call(port)

        self.assertEqual(len(self.all_metric), 0)
        self.assertEqual(len(self.all_span), 0)

        self._server.stop(0)

        with grpc_observability.GCPOpenCensusObservability() as o11y:
            o11y.init(exporter=self.test_exporter)

            port = self._start_server()
            self.unary_unary_call(port)

        self.assertTrue(len(self.all_metric) > 0)
        self.assertTrue(len(self.all_span) > 0)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

        self._server.stop(0)

    def testNoRecordAfterExit(self):
        os.environ[
            'GRPC_GCP_OBSERVABILITY_CONFIG'] = _VALID_CONFIG_TRACING_STATS
        with grpc_observability.GCPOpenCensusObservability() as o11y:
            o11y.init(exporter=self.test_exporter)

            port = self._start_server()
            self.unary_unary_call(port)

        self.assertTrue(len(self.all_metric) > 0)
        self.assertTrue(len(self.all_span) > 0)
        current_metric_len = len(self.all_metric)
        current_spans_len = len(self.all_span)
        self._validate_metrics(self.all_metric)
        self._validate_spans(self.all_span)

        self.unary_unary_call(port)
        self.assertEqual(len(self.all_metric), current_metric_len)
        self.assertEqual(len(self.all_span), current_spans_len)

        self._server.stop(0)

    def unary_unary_call(self, port):
        with grpc.insecure_channel('localhost:{}'.format(port)) as channel:
            multi_callable = channel.unary_unary(_UNARY_UNARY)
            unused_response, call = multi_callable.with_call(_REQUEST)

    def unary_stream_call(self, port):
        with grpc.insecure_channel('localhost:{}'.format(port)) as channel:
            multi_callable = channel.unary_stream(_UNARY_STREAM)
            call = multi_callable(_REQUEST)
            for _ in call:
                pass

    def stream_unary_call(self, port):
        with grpc.insecure_channel('localhost:{}'.format(port)) as channel:
            multi_callable = channel.stream_unary(_STREAM_UNARY)
            unused_response, call = multi_callable.with_call(
                iter([_REQUEST] * STREAM_LENGTH))

    def stream_stream_call(self, port):
        with grpc.insecure_channel('localhost:{}'.format(port)) as channel:
            multi_callable = channel.stream_stream(_STREAM_STREAM)
            call = multi_callable(iter([_REQUEST] * STREAM_LENGTH))
            for _ in call:
                pass

    def _start_server(self) -> int:
        self._server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        port = self._server.add_insecure_port('[::]:55555')

        self._server.start()
        return port

    def _validate_metrics(self,
                          metrics: List[grpc_observability.StatsData]) -> None:
        metric_names = set(metric.name for metric in metrics)
        for name in grpc_observability.MetricsName:
            if name in _SKIP_VEFIRY:
                continue
            if name not in metric_names:
                logger.error('metric %s not found in exported metrics: %s!',
                             name, metric_names)
            self.assertTrue(name in metric_names)

    def _validate_spans(
            self, tracing_data: List[grpc_observability.TracingData]) -> None:
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
