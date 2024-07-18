# Copyright 2024 gRPC authors.
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
import time
from typing import Any, AnyStr, Callable, Dict, List, Optional, Set
import unittest

from google.protobuf import struct_pb2
import grpc_observability
from grpc_observability import _open_telemetry_measures
from grpc_observability._open_telemetry_plugin import OpenTelemetryLabelInjector
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
CSM_METADATA_EXCHANGE_LABEL_KEY = "exchange_labels_key"

# The following metrics should have optional labels when optional
# labels is enabled through OpenTelemetryPlugin.
METRIC_NAME_WITH_OPTIONAL_LABEL = [
    "grpc.client.attempt.duration"
    "grpc.client.attempt.sent_total_compressed_message_size",
    "grpc.client.attempt.rcvd_total_compressed_message_size",
]
CSM_OPTIONAL_LABEL_KEYS = ["csm.service_name", "csm.service_namespace_name"]

# The following metrics should have metadata exchange labels when metadata
# exchange flow is triggered.
METRIC_NAME_WITH_EXCHANGE_LABEL = [
    "grpc.client.attempt.duration"
    "grpc.client.attempt.sent_total_compressed_message_size",
    "grpc.client.attempt.rcvd_total_compressed_message_size",
    "grpc.server.call.duration",
    "grpc.server.call.sent_total_compressed_message_size",
    "grpc.server.call.rcvd_total_compressed_message_size",
]


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


class TestLabelInjector(OpenTelemetryLabelInjector):
    _exchange_labels: Dict[str, AnyStr]
    _local_labels: Dict[str, str]

    def __init__(
        self, local_labels: Dict[str, str], exchange_labels: Dict[str, str]
    ):
        self._exchange_labels = exchange_labels
        self._local_labels = local_labels

    def get_labels_for_exchange(self) -> Dict[str, AnyStr]:
        return self._exchange_labels

    def get_additional_labels(
        self, include_exchange_labels: bool
    ) -> Dict[str, str]:
        return self._local_labels

    def deserialize_labels(
        self, labels: Dict[str, AnyStr]
    ) -> Dict[str, AnyStr]:
        deserialized_labels = {}
        for key, value in labels.items():
            if "XEnvoyPeerMetadata" == key:
                struct = struct_pb2.Struct()
                struct.ParseFromString(value)

                exchange_labels_value = self._get_value_from_struct(
                    CSM_METADATA_EXCHANGE_LABEL_KEY, struct
                )
                deserialized_labels[
                    CSM_METADATA_EXCHANGE_LABEL_KEY
                ] = exchange_labels_value
            else:
                deserialized_labels[key] = value

        return deserialized_labels

    def _get_value_from_struct(
        self, key: str, struct: struct_pb2.Struct
    ) -> str:
        value = struct.fields.get(key)
        if not value:
            return "unknown"
        return value.string_value


class TestOpenTelemetryPluginOption(OpenTelemetryPluginOption):
    _label_injector: OpenTelemetryLabelInjector
    _active_on_client: bool
    _active_on_server: bool

    def __init__(
        self,
        label_injector: OpenTelemetryLabelInjector,
        active_on_client: Optional[bool] = True,
        active_on_server: Optional[bool] = True,
    ):
        self._label_injector = label_injector
        self._active_on_client = active_on_client
        self._active_on_server = active_on_server

    def is_active_on_client_channel(self, target: str) -> bool:
        return self._active_on_client

    def is_active_on_server(self, xds: bool) -> bool:
        return self._active_on_server

    def get_label_injector(self) -> OpenTelemetryLabelInjector:
        return self._label_injector


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class ObservabilityPluginTest(unittest.TestCase):
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

    def testLabelInjectorWithLocalLabels(self):
        """Local labels in label injector should be added to all metrics."""
        label_injector = TestLabelInjector(
            local_labels={"local_labels_key": "local_labels_value"},
            exchange_labels={},
        )
        plugin_option = TestOpenTelemetryPluginOption(
            label_injector=label_injector
        )
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider, plugin_options=[plugin_option]
        )

        otel_plugin.register_global()
        self._server, port = _test_server.start_server()
        _test_server.unary_unary_call(port=port)
        otel_plugin.deregister_global()

        self._validate_metrics_exist(self.all_metrics)
        for name, label_list in self.all_metrics.items():
            self._validate_label_exist(name, label_list, ["local_labels_key"])

    def testClientSidePluginOption(self):
        label_injector = TestLabelInjector(
            local_labels={"local_labels_key": "local_labels_value"},
            exchange_labels={},
        )
        plugin_option = TestOpenTelemetryPluginOption(
            label_injector=label_injector, active_on_server=False
        )
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider, plugin_options=[plugin_option]
        )

        otel_plugin.register_global()
        server, port = _test_server.start_server()
        self._server = server
        _test_server.unary_unary_call(port=port)
        otel_plugin.deregister_global()

        self._validate_metrics_exist(self.all_metrics)
        for name, label_list in self.all_metrics.items():
            if "grpc.client" in name:
                self._validate_label_exist(
                    name, label_list, ["local_labels_key"]
                )
        for name, label_list in self.all_metrics.items():
            if "grpc.server" in name:
                self._validate_label_not_exist(
                    name, label_list, ["local_labels_key"]
                )

    def testServerSidePluginOption(self):
        label_injector = TestLabelInjector(
            local_labels={"local_labels_key": "local_labels_value"},
            exchange_labels={},
        )
        plugin_option = TestOpenTelemetryPluginOption(
            label_injector=label_injector, active_on_client=False
        )
        otel_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider, plugin_options=[plugin_option]
        )

        otel_plugin.register_global()
        server, port = _test_server.start_server()
        self._server = server
        _test_server.unary_unary_call(port=port)
        otel_plugin.deregister_global()

        self._validate_metrics_exist(self.all_metrics)
        for name, label_list in self.all_metrics.items():
            if "grpc.client" in name:
                self._validate_label_not_exist(
                    name, label_list, ["local_labels_key"]
                )
        for name, label_list in self.all_metrics.items():
            if "grpc.server" in name:
                self._validate_label_exist(
                    name, label_list, ["local_labels_key"]
                )

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
        # Sleep here to make sure we have at least one export from OTel MetricExporter.
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

    def _validate_label_exist(
        self,
        metric_name: str,
        metric_label_list: List[str],
        labels_to_check: List[str],
    ) -> None:
        for metric_label in metric_label_list:
            for label in labels_to_check:
                self.assertTrue(
                    label in metric_label,
                    msg=f"label with key {label} not found in metric {metric_name}, found label list: {metric_label}",
                )

    def _validate_label_not_exist(
        self,
        metric_name: str,
        metric_label_list: List[str],
        labels_to_check: List[str],
    ) -> None:
        for metric_label in metric_label_list:
            for label in labels_to_check:
                self.assertFalse(
                    label in metric_label,
                    msg=f"found unexpected label with key {label} in metric {metric_name}, found label list: {metric_label}",
                )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
