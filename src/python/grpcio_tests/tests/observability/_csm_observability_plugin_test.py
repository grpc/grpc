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
from typing import Any, Callable, Dict, List, Optional, Set
import unittest
from unittest import mock

from grpc_csm_observability import CsmOpenTelemetryPlugin
from grpc_csm_observability._csm_observability_plugin import (
    CSMOpenTelemetryLabelInjector,
)
from grpc_csm_observability._csm_observability_plugin import TYPE_GCE
from grpc_csm_observability._csm_observability_plugin import TYPE_GKE
from grpc_csm_observability._csm_observability_plugin import UNKNOWN_VALUE
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
from opentelemetry.sdk.resources import Resource

from tests.observability import _test_server

logger = logging.getLogger(__name__)

OTEL_EXPORT_INTERVAL_S = 0.5
# We only expect basic labels to be exchanged.
CSM_METADATA_EXCHANGE_DEFAULT_LABELS = [
    "csm.remote_workload_type",
    "csm.remote_workload_canonical_service",
]

# The following metrics should have optional labels when optional
# labels is enabled through OpenTelemetryPlugin.
METRIC_NAME_WITH_OPTIONAL_LABEL = [
    "grpc.client.attempt.duration",
    "grpc.client.attempt.sent_total_compressed_message_size",
    "grpc.client.attempt.rcvd_total_compressed_message_size",
]
CSM_OPTIONAL_LABEL_KEYS = ["csm.service_name", "csm.service_namespace_name"]

# The following metrics should have metadata exchange labels when metadata
# exchange flow is triggered.
METRIC_NAME_WITH_EXCHANGE_LABEL = [
    "grpc.client.attempt.duration",
    "grpc.client.attempt.sent_total_compressed_message_size",
    "grpc.client.attempt.rcvd_total_compressed_message_size",
    "grpc.server.call.duration",
    "grpc.server.call.sent_total_compressed_message_size",
    "grpc.server.call.rcvd_total_compressed_message_size",
]

MOCK_GKE_RESOURCE = Resource.create(
    attributes={
        "gcp.resource_type": "gke_container",
        "k8s.pod.name": "pod",
        "k8s.container.name": "container",
        "k8s.namespace.name": "namespace",
        "k8s.cluster.name": "cluster",
        "cloud.region": "region",
        "cloud.account.id": "id",
    }
)

MOCK_GCE_RESOURCE = Resource.create(
    attributes={
        "gcp.resource_type": "gce_instance",
        "cloud.zone": "zone",
        "cloud.account.id": "id",
    }
)

MOCK_UNKNOWN_RESOURCE = Resource.create(
    attributes={
        "gcp.resource_type": "random",
    }
)


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
class CSMObservabilityPluginTest(unittest.TestCase):
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

    def testOptionalXdsServiceLabelExist(self):
        csm_plugin = CsmOpenTelemetryPlugin(
            meter_provider=self._provider,
        )

        csm_plugin.register_global()
        self._server, port = _test_server.start_server()
        _test_server.unary_unary_call(port=port)
        csm_plugin.deregister_global()

        validate_metrics_exist(self, self.all_metrics)
        for name, label_list in self.all_metrics.items():
            if name in METRIC_NAME_WITH_OPTIONAL_LABEL:
                self._validate_label_exist(
                    name, label_list, CSM_OPTIONAL_LABEL_KEYS
                )
            else:
                self._validate_label_not_exist(
                    name, label_list, CSM_OPTIONAL_LABEL_KEYS
                )

    def testPluginOptionOnlyEnabledForXdsTargets(self):
        csm_plugin = CsmOpenTelemetryPlugin(
            meter_provider=self._provider,
        )
        csm_plugin_option = csm_plugin.plugin_options[0]
        self.assertFalse(
            csm_plugin_option.is_active_on_client_channel("foo.bar.google.com")
        )
        self.assertFalse(
            csm_plugin_option.is_active_on_client_channel(
                "dns:///foo.bar.google.com"
            )
        )
        self.assertFalse(
            csm_plugin_option.is_active_on_client_channel(
                "dns:///foo.bar.google.com:1234"
            )
        )
        self.assertFalse(
            csm_plugin_option.is_active_on_client_channel(
                "dns://authority/foo.bar.google.com:1234"
            )
        )
        self.assertFalse(
            csm_plugin_option.is_active_on_client_channel("xds://authority/foo")
        )

        self.assertTrue(
            csm_plugin_option.is_active_on_client_channel("xds:///foo")
        )
        self.assertTrue(
            csm_plugin_option.is_active_on_client_channel(
                "xds://traffic-director-global.xds.googleapis.com/foo"
            )
        )
        self.assertTrue(
            csm_plugin_option.is_active_on_client_channel(
                "xds://traffic-director-global.xds.googleapis.com/foo.bar"
            )
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


@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Observability is not supported in Windows and MacOS",
)
class MetadataExchangeTest(unittest.TestCase):
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

    @mock.patch(
        "opentelemetry.resourcedetector.gcp_resource_detector.GoogleCloudResourceDetector.detect"
    )
    def testMetadataExchangeClientDoesNotSendMetadata(self, mock_detector):
        mock_detector.return_value = MOCK_GKE_RESOURCE
        with mock.patch.dict(
            os.environ,
            {
                "CSM_CANONICAL_SERVICE_NAME": "canonical_service",
                "CSM_WORKLOAD_NAME": "workload",
            },
        ):
            plugin_option = TestOpenTelemetryPluginOption(
                label_injector=CSMOpenTelemetryLabelInjector(),
                active_on_client=False,
            )

        # Manually create csm_plugin so that it's always disabled on client.
        csm_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider, plugin_options=[plugin_option]
        )

        csm_plugin.register_global()
        self._server, port = _test_server.start_server()
        _test_server.unary_unary_call(port=port)
        csm_plugin.deregister_global()

        validate_metrics_exist(self, self.all_metrics)
        for name, label_list in self.all_metrics.items():
            for labels in label_list:
                # Verifies that the server records unknown when the client does not send metadata
                if name in ["grpc.server.call.duration"]:
                    self.assertEqual(
                        labels["csm.workload_canonical_service"],
                        "canonical_service",
                    )
                    self.assertEqual(
                        labels["csm.remote_workload_canonical_service"],
                        "unknown",
                    )
                # Client metric should not have CSM labels.
                elif "grpc.client" in name:
                    self.assertTrue(
                        "csm.workload_canonical_service" not in labels.keys()
                    )
                    self.assertTrue(
                        "csm.remote_workload_canonical_service"
                        not in labels.keys()
                    )

    @mock.patch(
        "opentelemetry.resourcedetector.gcp_resource_detector.GoogleCloudResourceDetector.detect"
    )
    def testResourceDetectorGCE(self, mock_detector):
        mock_detector.return_value = MOCK_GCE_RESOURCE
        with mock.patch.dict(
            os.environ,
            {
                "CSM_CANONICAL_SERVICE_NAME": "canonical_service",
                "CSM_WORKLOAD_NAME": "workload",
            },
        ):
            plugin_option = TestOpenTelemetryPluginOption(
                label_injector=CSMOpenTelemetryLabelInjector(),
            )

        # Have to manually create csm_plugin so that we can enable it for all
        # channels.
        csm_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider, plugin_options=[plugin_option]
        )

        csm_plugin.register_global()
        self._server, port = _test_server.start_server()
        _test_server.unary_unary_call(port=port)
        _test_server.unary_unary_call(port=port)
        csm_plugin.deregister_global()

        validate_metrics_exist(self, self.all_metrics)
        for name, label_list in self.all_metrics.items():
            # started metrics shouldn't have any csm labels.
            if name in [
                "grpc.client.attempt.started",
                "grpc.server.call.started",
            ]:
                self._verify_no_service_mesh_attributes(label_list)
            # duration metrics should have all csm related labels.
            elif name in [
                "grpc.client.attempt.duration",
                "grpc.server.call.duration",
            ]:
                self._verify_service_mesh_attributes(label_list, TYPE_GCE)

    @mock.patch(
        "opentelemetry.resourcedetector.gcp_resource_detector.GoogleCloudResourceDetector.detect"
    )
    def testResourceDetectorGKE(self, mock_detector):
        mock_detector.return_value = MOCK_GKE_RESOURCE
        with mock.patch.dict(
            os.environ,
            {
                "CSM_CANONICAL_SERVICE_NAME": "canonical_service",
                "CSM_WORKLOAD_NAME": "workload",
            },
        ):
            plugin_option = TestOpenTelemetryPluginOption(
                label_injector=CSMOpenTelemetryLabelInjector()
            )

        # Have to manually create csm_plugin so that we can enable it for all
        # channels.
        csm_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider, plugin_options=[plugin_option]
        )

        csm_plugin.register_global()
        self._server, port = _test_server.start_server()
        _test_server.unary_unary_call(port=port)
        csm_plugin.deregister_global()

        validate_metrics_exist(self, self.all_metrics)
        for name, label_list in self.all_metrics.items():
            # started metrics shouldn't have any csm labels.
            if name in [
                "grpc.client.attempt.started",
                "grpc.server.call.started",
            ]:
                self._verify_no_service_mesh_attributes(label_list)
            # duration metrics should have all csm related labels.
            elif name in [
                "grpc.client.attempt.duration",
                "grpc.server.call.duration",
            ]:
                self._verify_service_mesh_attributes(label_list, TYPE_GKE)

    @mock.patch(
        "opentelemetry.resourcedetector.gcp_resource_detector.GoogleCloudResourceDetector.detect"
    )
    def testResourceDetectorUnknown(self, mock_detector):
        mock_detector.return_value = MOCK_UNKNOWN_RESOURCE
        with mock.patch.dict(
            os.environ,
            {
                "CSM_CANONICAL_SERVICE_NAME": "canonical_service",
                "CSM_WORKLOAD_NAME": "workload",
            },
        ):
            plugin_option = TestOpenTelemetryPluginOption(
                label_injector=CSMOpenTelemetryLabelInjector()
            )

        # Have to manually create csm_plugin so that we can enable it for all
        # channels.
        csm_plugin = grpc_observability.OpenTelemetryPlugin(
            meter_provider=self._provider, plugin_options=[plugin_option]
        )

        csm_plugin.register_global()
        self._server, port = _test_server.start_server()
        _test_server.unary_unary_call(port=port)
        csm_plugin.deregister_global()

        validate_metrics_exist(self, self.all_metrics)
        for name, label_list in self.all_metrics.items():
            # started metrics shouldn't have any csm labels.
            if name in [
                "grpc.client.attempt.started",
                "grpc.server.call.started",
            ]:
                self._verify_no_service_mesh_attributes(label_list)
            # duration metrics should have all csm related labels.
            elif name in [
                "grpc.client.attempt.duration",
                "grpc.server.call.duration",
            ]:
                self._verify_service_mesh_attributes(label_list, UNKNOWN_VALUE)

    def _verify_service_mesh_attributes(
        self, label_list: List[Dict[str, str]], resource_type: str
    ):
        for labels in label_list:
            # Assuming attributes is a dictionary
            self.assertEqual(
                labels["csm.workload_canonical_service"], "canonical_service"
            )
            self.assertEqual(
                labels["csm.remote_workload_canonical_service"],
                "canonical_service",
            )

            if resource_type == TYPE_GKE:
                self.assertEqual(
                    labels["csm.remote_workload_type"], "gcp_kubernetes_engine"
                )
                self.assertEqual(labels["csm.remote_workload_name"], "workload")
                self.assertEqual(
                    labels["csm.remote_workload_namespace_name"], "namespace"
                )
                self.assertEqual(
                    labels["csm.remote_workload_cluster_name"], "cluster"
                )
                self.assertEqual(
                    labels["csm.remote_workload_location"], "region"
                )
                self.assertEqual(labels["csm.remote_workload_project_id"], "id")
            elif resource_type == TYPE_GCE:
                self.assertEqual(
                    labels["csm.remote_workload_type"], "gcp_compute_engine"
                )
                self.assertEqual(labels["csm.remote_workload_name"], "workload")
                self.assertEqual(labels["csm.remote_workload_location"], "zone")
                self.assertEqual(labels["csm.remote_workload_project_id"], "id")
            elif resource_type == UNKNOWN_VALUE:
                self.assertEqual(labels["csm.remote_workload_type"], "random")

    def _verify_no_service_mesh_attributes(
        self, label_list: List[Dict[str, str]]
    ):
        for labels in label_list:
            self.assertTrue(
                "csm.remote_workload_canonical_service" not in labels.keys()
            )
            self.assertTrue("csm.remote_workload_type" not in labels.keys())
            self.assertTrue(
                "csm.workload_canonical_service" not in labels.keys()
            )
            self.assertTrue("csm.workload_type" not in labels.keys())
            self.assertTrue("csm.mesh_id" not in labels.keys())


def validate_metrics_exist(
    testCase: unittest.TestCase, all_metrics: Dict[str, Any]
) -> None:
    # Sleep here to make sure we have at least one export from OTel MetricExporter.
    assert_eventually(
        testCase=testCase,
        predicate=lambda: len(all_metrics.keys()) > 1,
        message=lambda: f"No metrics was exported",
    )


def assert_eventually(
    testCase: unittest.TestCase,
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
        testCase.fail(message() + " after " + str(timeout))


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
