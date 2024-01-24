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

from typing import Dict, List

from opentelemetry.sdk.metrics.export import AggregationTemporality
from opentelemetry.sdk.metrics.export import MetricExportResult
from opentelemetry.sdk.metrics.export import MetricExporter
from opentelemetry.sdk.metrics.export import MetricsData


class OTelMetricExporter(MetricExporter):
    """Implementation of :class:`MetricExporter` that export metrics to the
    provided metric_list.

    all_metrics: A dict whose keys are grpc_observability._opentelemetry_measures.Metric.name,
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
        print_live: bool = False,
    ):
        super().__init__(
            preferred_temporality=preferred_temporality,
            preferred_aggregation=preferred_aggregation,
        )
        self._all_metrics = all_metrics
        self._print_live = print_live

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
                        self._all_metrics[metric.name].append(
                            data_point.attributes
                        )
                        if self._print_live:
                            print(f"Metric exporter received: {metric.name}")
