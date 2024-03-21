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
from typing import Callable, Dict, Iterable, List, Optional

# pytype: disable=pyi-error
from grpc_observability import _open_telemetry_observability
from opentelemetry.metrics import MeterProvider


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
    """Describes a Plugin for OpenTelemetry observability."""

    plugin_options: Iterable[OpenTelemetryPluginOption]
    meter_provider: Optional[MeterProvider]
    target_attribute_filter: Callable[[str], bool]
    generic_method_attribute_filter: Callable[[str], bool]
    _plugin: _open_telemetry_observability._OpenTelemetryPlugin

    def __init__(
        self,
        *,
        plugin_options: Iterable[OpenTelemetryPluginOption] = [],
        meter_provider: Optional[MeterProvider] = None,
        target_attribute_filter: Optional[Callable[[str], bool]] = None,
        generic_method_attribute_filter: Optional[Callable[[str], bool]] = None,
    ):
        """
        Args:
          plugin_options: An Iterable of OpenTelemetryPluginOption which will be
        enabled for this OpenTelemetryPlugin.
          meter_provider: A MeterProvider which will be used to collect telemetry data,
        or None which means no metrics will be collected.
          target_attribute_filter: Once provided, this will be called per channel to decide
        whether to record the target attribute on client or to replace it with "other".
        This helps reduce the cardinality on metrics in cases where many channels
        are created with different targets in the same binary (which might happen
        for example, if the channel target string uses IP addresses directly).
        Return True means the original target string will be used, False means target string
        will be replaced with "other".
          generic_method_attribute_filter: Once provided, this will be called with a generic
        method type to decide whether to record the method name or to replace it with
        "other". Note that pre-registered methods will always be recorded no matter what
        this function returns.
        Return True means the original method name will be used, False means method name will
        be replaced with "other".
        """
        self.plugin_options = plugin_options
        self.meter_provider = meter_provider
        if target_attribute_filter:
            self.target_attribute_filter = target_attribute_filter
        else:
            self.target_attribute_filter = lambda target: True
        if generic_method_attribute_filter:
            self.generic_method_attribute_filter = (
                generic_method_attribute_filter
            )
        else:
            self.generic_method_attribute_filter = lambda method: False
        self._plugin = _open_telemetry_observability._OpenTelemetryPlugin(self)

    def register_global(self) -> None:
        """
        Registers a global plugin that acts on all channels and servers running on the process.

        Raises:
            RuntimeError: If a global plugin was already registered.
        """
        _open_telemetry_observability.start_open_telemetry_observability(
            plugins=[self._plugin]
        )

    def deregister_global(self) -> None:
        """
        De-register the global plugin that acts on all channels and servers running on the process.

        Raises:
            RuntimeError: If no global plugin was registered.
        """
        _open_telemetry_observability.end_open_telemetry_observability()

    def __enter__(self) -> None:
        _open_telemetry_observability.start_open_telemetry_observability(
            plugins=[self._plugin]
        )

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        _open_telemetry_observability.end_open_telemetry_observability()
