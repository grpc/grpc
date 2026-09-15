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
import threading
import time
from typing import Any, Callable, Dict, List, Optional, Set
import unittest

import grpc
import grpc_observability
from grpc_observability import _open_telemetry_measures
from grpc_observability._open_telemetry_observability import (
    GRPC_OTHER_LABEL_VALUE,
)
from grpc_observability._open_telemetry_observability import (
    _OpenTelemetryPlugin,
)
from grpc_observability._open_telemetry_observability import GRPC_METHOD_LABEL
from grpc_observability._open_telemetry_observability import GRPC_TARGET_LABEL
from grpc_observability._open_telemetry_plugin import OpenTelemetryPluginOption
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
# Generous: these waits only elapse in full if the test is already failing.
_ACTIVATION_TIMEOUT_S = 10.0


class _AlwaysActivePluginOption(OpenTelemetryPluginOption):
    def is_active_on_client_channel(self, target: str) -> bool:
        return True

    def is_active_on_server(self, xds: bool) -> bool:
        return True


# The two options below deliberately break the side-effect-free rule that
# activate_*_plugin_options documents: signalling and blocking from the
# predicate is the only way to hold an activating thread at a known point in the
# list. Test scaffolding only -- they touch no plugin state. Real plugin options
# must not do this.
class _PausingPluginOption(OpenTelemetryPluginOption):
    """Blocks activation mid-list until the test lets it continue."""

    def __init__(self, entered: threading.Event, resume: threading.Event):
        self._entered = entered
        self._resume = resume

    def _pause(self) -> bool:
        self._entered.set()
        self._resume.wait(_ACTIVATION_TIMEOUT_S)
        return True

    def is_active_on_client_channel(self, target: str) -> bool:
        return self._pause()

    def is_active_on_server(self, xds: bool) -> bool:
        return self._pause()


class _BarrierPluginOption(OpenTelemetryPluginOption):
    """Holds every activating thread mid-list until all of them arrive."""

    def __init__(self, barrier: threading.Barrier):
        self._barrier = barrier

    def is_active_on_client_channel(self, target: str) -> bool:
        self._barrier.wait()
        return True

    def is_active_on_server(self, xds: bool) -> bool:
        self._barrier.wait()
        return True


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

    def testRecordUnaryUnaryUseContextManager(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    def testRecordUnaryUnaryUseGlobalInit(self):
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        )
        otel_plugin.register_global()

        server, port = _test_server.start_server()
        self._server = server
        _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())
        otel_plugin.deregister_global()

    def testCallGlobalInitThrowErrorWhenGlobalCalled(self):
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        )
        otel_plugin.register_global()
        try:
            otel_plugin.register_global()
        except RuntimeError as exp:
            self.assertIn(
                "gRPC Python observability was already initialized", str(exp)
            )

        otel_plugin.deregister_global()

    def testCallGlobalInitThrowErrorWhenContextManagerCalled(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        ):
            try:
                otel_plugin = grpc_observability.OpenTelemetryPlugin(
                    meter_provider=self._provider
                )
                otel_plugin.register_global()
            except RuntimeError as exp:
                self.assertIn(
                    "gRPC Python observability was already initialized",
                    str(exp),
                )

    def testCallContextManagerThrowErrorWhenGlobalInitCalled(self):
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        )
        otel_plugin.register_global()
        try:
            with grpc_observability.OpenTelemetryPlugin(
                meter_provider=self._provider
            ):
                pass
        except RuntimeError as exp:
            self.assertIn(
                "gRPC Python observability was already initialized", str(exp)
            )
        otel_plugin.deregister_global()

    def testContextManagerThrowErrorWhenContextManagerCalled(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        ):
            try:
                with grpc_observability.OpenTelemetryPlugin(
                    meter_provider=self._provider
                ):
                    pass
            except RuntimeError as exp:
                self.assertIn(
                    "gRPC Python observability was already initialized",
                    str(exp),
                )

    def testNoErrorCallGlobalInitThenContextManager(self):
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        )
        otel_plugin.register_global()
        otel_plugin.deregister_global()

        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        ):
            pass

    def testNoErrorCallContextManagerThenGlobalInit(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        ):
            pass
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        )
        otel_plugin.register_global()
        otel_plugin.deregister_global()

    def testRecordUnaryUnaryWithClientInterceptor(self):
        interceptor = _ClientUnaryUnaryInterceptor()
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
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
            meter_provider=self._provider
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
            meter_provider=self._provider
        ):
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(
            self.all_metrics,
            expected_count=sum(
                1
                for m in _open_telemetry_measures.base_metrics()
                if "grpc.client" in m.name
            ),
        )
        self._validate_client_metrics_names(self.all_metrics)

    def testNoRecordBeforeInit(self):
        server, port = _test_server.start_server()
        _test_server.unary_unary_call(port=port)
        self.assertEqual(len(self.all_metrics), 0)
        server.stop(0)

        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    def testNoRecordAfterExitUseContextManager(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        ):
            server, port = _test_server.start_server()
            self._server = server
            self._port = port
            _test_server.unary_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

        self.all_metrics = defaultdict(list)
        _test_server.unary_unary_call(port=self._port)
        with self.assertRaisesRegex(
            AssertionError, r"Expected at least \d+ metrics, got 0"
        ):
            self._validate_metrics_exist(self.all_metrics)

    def testNoRecordAfterExitUseGlobal(self):
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        )
        otel_plugin.register_global()

        server, port = _test_server.start_server()
        self._server = server
        self._port = port
        _test_server.unary_unary_call(port=port)
        otel_plugin.deregister_global()

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

        self.all_metrics = defaultdict(list)
        _test_server.unary_unary_call(port=self._port)
        with self.assertRaisesRegex(
            AssertionError, r"Expected at least \d+ metrics, got 0"
        ):
            self._validate_metrics_exist(self.all_metrics)

    def testRecordUnaryStream(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.unary_stream_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    def testRecordStreamUnary(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_unary_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

    def testRecordStreamStream(self):
        with grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider
        ):
            server, port = _test_server.start_server()
            self._server = server
            _test_server.stream_stream_call(port=port)

        self._validate_metrics_exist(self.all_metrics)
        self._validate_all_metrics_names(self.all_metrics.keys())

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
            meter_provider=self._provider, target_attribute_filter=target_filter
        ):
            _test_server.unary_unary_call(port=main_port)
            _test_server.unary_unary_call(port=backup_port)

        self._validate_metrics_exist(
            self.all_metrics,
            expected_count=sum(
                1
                for m in _open_telemetry_measures.base_metrics()
                if "grpc.client" in m.name
            ),
        )
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
            meter_provider=self._provider,
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
            meter_provider=self._provider
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
            meter_provider=self._provider
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

    def _validate_metrics_exist(
        self,
        all_metrics: dict[str, Any],
        expected_count: int = len(_open_telemetry_measures.base_metrics()),
    ) -> None:
        # Sleep here to make sure we have at least expected number of metrics
        # from OTel MetricExporter.
        self.assert_eventually(
            lambda: len(all_metrics.keys()) >= expected_count,
            message=lambda: (
                f"Expected at least {expected_count} metrics, got "
                f"{len(all_metrics.keys())}"
            ),
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


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class DecodeLabelsTest(unittest.TestCase):
    def testInvalidUtf8ValueDoesNotRaise(self):
        key = "key"
        invalid_value = b"\xffbad"
        decoded = _OpenTelemetryPlugin.decode_labels({key: invalid_value})
        self.assertIn(key, decoded)
        self.assertIsInstance(decoded[key], str)

    def testInvalidUtf8KeyDoesNotRaise(self):
        invalid_key = b"\xffbad"
        value = "value"
        decoded = _OpenTelemetryPlugin.decode_labels({invalid_key: value})
        self.assertNotIn(invalid_key, decoded)
        self.assertEqual(len(decoded), 1)

    def testBytesKeyAndBytesValueAreDecodedToStr(self):
        key_as_bytes = b"my_key"
        key_as_str = "my_key"
        value_as_bytes = b"my_value"
        decoded = _OpenTelemetryPlugin.decode_labels(
            {key_as_bytes: value_as_bytes}
        )
        self.assertNotIn(key_as_bytes, decoded)
        self.assertIn(key_as_str, decoded)
        self.assertIsInstance(decoded[key_as_str], str)

    def testStrKeyAndStrValuePassThroughUnchanged(self):
        key = "key"
        value = "value"
        decoded = _OpenTelemetryPlugin.decode_labels({key: value})
        self.assertIn(key, decoded)
        self.assertEqual(decoded[key], value)
        self.assertIsInstance(decoded[key], str)


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class ActivatePluginOptionsSingleAssignmentTest(unittest.TestCase):
    """Documents why activate_*_plugin_options must publish in one assignment.

    The AsyncIO client path reads the observability plugin without holding
    _plugin_lock, so activation can run on several threads at once. Assigning
    the enabled list empty and appending to it would let a concurrent reader
    observe it half-built: non-empty, so that reader skips activation, but
    missing options, so it silently builds incomplete exchange labels.

    Each test drives that interleaving deterministically by pausing the
    activation predicate of one plugin option partway through the list.
    """

    def _plugin_with_options(
        self, plugin_options: List[OpenTelemetryPluginOption]
    ) -> _OpenTelemetryPlugin:
        return _OpenTelemetryPlugin(
            grpc_observability.OpenTelemetryPlugin(
                plugin_options=plugin_options, meter_provider=None
            )
        )

    def _assert_never_half_built(self, activate_on_client: bool) -> None:
        entered = threading.Event()
        resume = threading.Event()
        # The pausing option sits in the middle, so a half-built list would be
        # observably shorter than the full one rather than empty.
        plugin_options = [
            _AlwaysActivePluginOption(),
            _PausingPluginOption(entered, resume),
            _AlwaysActivePluginOption(),
        ]
        plugin = self._plugin_with_options(plugin_options)

        if activate_on_client:
            activate = lambda: plugin.activate_client_plugin_options(
                b"localhost:50051"
            )
            published = lambda: plugin._enabled_client_plugin_options
        else:
            activate = lambda: plugin.activate_server_plugin_options(False)
            published = lambda: plugin._enabled_server_plugin_options

        activator = threading.Thread(target=activate, daemon=True)
        activator.start()
        try:
            self.assertTrue(
                entered.wait(_ACTIVATION_TIMEOUT_S),
                "activation never reached the pausing plugin option",
            )
            # Read from this thread while the activating thread is still
            # iterating. Nothing may be published yet. Snapshot the length now:
            # a list published early keeps growing, so measuring it after the
            # activator finishes would hide how short it was here.
            observed = published()
            observed_count = None if observed is None else len(observed)
        finally:
            resume.set()
            activator.join(_ACTIVATION_TIMEOUT_S)

        self.assertIsNone(
            observed,
            "a concurrent reader saw the enabled list mid-build with "
            f"{observed_count} of {len(plugin_options)} option(s); it must be "
            "published in a single assignment",
        )
        self.assertEqual(len(published()), len(plugin_options))

    def testClientOptionsAreNeverObservedHalfBuilt(self):
        self._assert_never_half_built(activate_on_client=True)

    def testServerOptionsAreNeverObservedHalfBuilt(self):
        self._assert_never_half_built(activate_on_client=False)

    def testConcurrentActivationBothPublishCompleteList(self):
        # The barrier only clears if both threads actually run the activation.
        # Publishing early would let the second thread find a non-empty list and
        # skip activation altogether -- so it never reaches the barrier, and the
        # options it needed are missing. With a single assignment both threads
        # build a full list independently and the last writer wins.
        barrier = threading.Barrier(2, timeout=_ACTIVATION_TIMEOUT_S)
        plugin_options = [
            _AlwaysActivePluginOption(),
            _BarrierPluginOption(barrier),
            _AlwaysActivePluginOption(),
        ]
        plugin = self._plugin_with_options(plugin_options)

        activators = [
            threading.Thread(
                target=plugin.activate_client_plugin_options,
                args=(b"localhost:50051",),
                daemon=True,
            )
            for _ in range(2)
        ]
        for activator in activators:
            activator.start()
        for activator in activators:
            activator.join(_ACTIVATION_TIMEOUT_S)
            self.assertFalse(activator.is_alive(), "activation did not finish")

        self.assertEqual(
            len(plugin._enabled_client_plugin_options), len(plugin_options)
        )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
