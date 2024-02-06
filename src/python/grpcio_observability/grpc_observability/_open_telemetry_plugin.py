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

import abc
from typing import Dict, Iterable, List, Optional, Union

# pytype: disable=pyi-error
import grpc
from grpc_observability import _open_telemetry_measures
from grpc_observability._cyobservability import MetricsName
from grpc_observability._observability import StatsData
from opentelemetry.metrics import Counter
from opentelemetry.metrics import Histogram
from opentelemetry.metrics import Meter
from opentelemetry.metrics import MeterProvider

GRPC_METHOD_LABEL = "grpc.method"
GRPC_TARGET_LABEL = "grpc.target"
GRPC_OTHER_LABEL_VALUE = "other"


class OpenTelemetryLabelInjector(abc.ABC):
    """
    An interface that allows you to add additional labels on the calls traced.

    Please note that this class is still work in progress and NOT READY to be used.
    """

    _labels: List[Dict[str, str]]

    def __init__(self):
        # Calls Python OTel API to detect resource and get labels, save
        # those lables to OpenTelemetryLabelInjector.labels.
        pass

    @abc.abstractmethod
    def get_labels(self):
        # Get additional labels for this OpenTelemetryLabelInjector.
        raise NotImplementedError()


class OpenTelemetryPluginOption(abc.ABC):
    """
    An interface that allows you to add additional function to OpenTelemetryPlugin.

    Please note that this class is still work in progress and NOT READY to be used.
    """

    @abc.abstractmethod
    def is_active_on_method(self, method: str) -> bool:
        """Determines whether this plugin option is active on a given method.

        Args:
          method: Required. The RPC method, for example: `/helloworld.Greeter/SayHello`.

        Returns:
          True if this this plugin option is active on the giving method, false otherwise.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def is_active_on_server(self, channel_args: List[str]) -> bool:
        """Determines whether this plugin option is active on a given server.

        Args:
          channel_args: Required. The channel args used for server.
          TODO(xuanwn): detail on what channel_args will contain.

        Returns:
          True if this this plugin option is active on the server, false otherwise.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def get_label_injector(self) -> Optional[OpenTelemetryLabelInjector]:
        # Returns the LabelsInjector used by this plugin option, or None.
        raise NotImplementedError()


# pylint: disable=no-self-use
class OpenTelemetryPlugin:
    """Describes a Plugin for OpenTelemetry observability.

    This is class is part of an EXPERIMENTAL API.
    """

    def get_plugin_options(
        self,
    ) -> Iterable[OpenTelemetryPluginOption]:
        """
        This function will be used to get plugin options which are enabled for
        this OpenTelemetryPlugin instance.

        Returns:
            An Iterable of class OpenTelemetryPluginOption which will be enabled for
            this OpenTelemetryPlugin.
        """
        return []

    def get_meter_provider(self) -> Optional[MeterProvider]:
        """
        This function will be used to get the MeterProvider for this OpenTelemetryPlugin
        instance.

        Returns:
            A MeterProvider which will be used to collect telemetry data, or None which
            means no metrics will be collected.
        """
        return None

    def target_attribute_filter(
        self, target: str  # pylint: disable=unused-argument
    ) -> bool:
        """
        Once overridden, this will be called per channel to decide whether to record the
        target attribute on client or to replace it with "other".
        This helps reduce the cardinality on metrics in cases where many channels
        are created with different targets in the same binary (which might happen
        for example, if the channel target string uses IP addresses directly).

        Args:
            target: The target for the RPC.

        Returns:
            bool: True means the original target string will be used, False means target string
            will be replaced with "other".
        """
        return True

    def generic_method_attribute_filter(
        self, method: str  # pylint: disable=unused-argument
    ) -> bool:
        """
        Once overridden, this will be called with a generic method type to decide whether to
        record the method name or to replace it with "other".

        Note that pre-registered methods will always be recorded no matter what this
        function returns.

        Args:
            method: The method name for the RPC.

        Returns:
            bool: True means the original method name will be used, False means method name
            will be replaced with "other".
        """
        return False


class _OpenTelemetryPlugin:
    _plugin: OpenTelemetryPlugin
    _metric_to_recorder: Dict[MetricsName, Union[Counter, Histogram]]

    def __init__(self, plugin: OpenTelemetryPlugin):
        self._plugin = plugin
        self._metric_to_recorder = dict()

        meter_provider = self._plugin.get_meter_provider()
        if meter_provider:
            meter = meter_provider.get_meter("grpc-python", grpc.__version__)
            enabled_metrics = _open_telemetry_measures.base_metrics()
            self._metric_to_recorder = self._register_metrics(
                meter, enabled_metrics
            )

    def _should_record(self, stats_data: StatsData) -> bool:
        # Decide if this plugin should record the stats_data.
        return stats_data.name in self._metric_to_recorder.keys()

    def _record_stats_data(self, stats_data: StatsData) -> None:
        recorder = self._metric_to_recorder[stats_data.name]

        target = stats_data.labels.get(GRPC_TARGET_LABEL, "")
        if not self._plugin.target_attribute_filter(target):
            # Filter target name.
            stats_data.labels[GRPC_TARGET_LABEL] = GRPC_OTHER_LABEL_VALUE

        method = stats_data.labels.get(GRPC_METHOD_LABEL, "")
        if not self._plugin.generic_method_attribute_filter(method):
            # Filter method name.
            stats_data.labels[GRPC_METHOD_LABEL] = GRPC_OTHER_LABEL_VALUE

        value = 0
        if stats_data.measure_double:
            value = stats_data.value_float
        else:
            value = stats_data.value_int
        if isinstance(recorder, Counter):
            recorder.add(value, attributes=stats_data.labels)
        elif isinstance(recorder, Histogram):
            recorder.record(value, attributes=stats_data.labels)

    # pylint: disable=no-self-use
    def maybe_record_stats_data(self, stats_data: List[StatsData]) -> None:
        # Records stats data to MeterProvider.
        if self._should_record(stats_data):
            self._record_stats_data(stats_data)

    def _register_metrics(
        self, meter: Meter, metrics: List[_open_telemetry_measures.Metric]
    ) -> Dict[MetricsName, Union[Counter, Histogram]]:
        metric_to_recorder_map = {}
        recorder = None
        for metric in metrics:
            if metric == _open_telemetry_measures.CLIENT_ATTEMPT_STARTED:
                recorder = meter.create_counter(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.CLIENT_ATTEMPT_DURATION:
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.CLIENT_RPC_DURATION:
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.CLIENT_ATTEMPT_SEND_BYTES:
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif (
                metric == _open_telemetry_measures.CLIENT_ATTEMPT_RECEIVED_BYTES
            ):
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.SERVER_STARTED_RPCS:
                recorder = meter.create_counter(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.SERVER_RPC_DURATION:
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.SERVER_RPC_SEND_BYTES:
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            elif metric == _open_telemetry_measures.SERVER_RPC_RECEIVED_BYTES:
                recorder = meter.create_histogram(
                    name=metric.name,
                    unit=metric.unit,
                    description=metric.description,
                )
            metric_to_recorder_map[metric.cyname] = recorder
        return metric_to_recorder_map
