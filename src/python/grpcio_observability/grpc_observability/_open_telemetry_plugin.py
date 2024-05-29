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

from typing import AnyStr, Callable, Dict, Iterable, List, Optional

# pytype: disable=pyi-error
from grpc_observability import _open_telemetry_observability
from grpc_observability._observability import OptionalLabelType
from opentelemetry.metrics import MeterProvider

GRPC_METHOD_LABEL = "grpc.method"
GRPC_TARGET_LABEL = "grpc.target"
GRPC_CLIENT_METRIC_PREFIX = "grpc.client"
GRPC_OTHER_LABEL_VALUE = "other"


class OpenTelemetryLabelInjector:
    """
    An interface that allows you to add additional labels on the calls traced.
    """

    def get_labels_for_exchange(self) -> Dict[str, AnyStr]:
        """
        Get labels used for metadata exchange.

        Returns:
          A dict of labels, with a string as key representing label name, string or bytes
        as value representing label value.
        """
        raise NotImplementedError()

    def get_additional_labels(
        self, include_exchange_labels: bool
    ) -> Dict[str, str]:
        """
        Get additional labels added by this injector.

        The return value from this method will be added directly to metric data.

        Args:
          include_exchange_labels: Whether to add additional metadata exchange related labels.

        Returns:
          A dict of labels.
        """
        raise NotImplementedError()

    # pylint: disable=no-self-use
    def deserialize_labels(
        self, labels: Dict[str, AnyStr]
    ) -> Dict[str, AnyStr]:
        """
        Deserialize the labels if required.

        If this injector added labels for metadata exchange, this method will be called to
        deserialize the exchanged labels.

        For example, if this injector added xds_peer_metadata_label for exchange:

            labels: {"labelA": b"valueA", "xds_peer_metadata_label": b"exchanged_bytes"}

        This method should deserialize xds_peer_metadata_label and return labels as:

            labels: {"labelA": b"valueA", "xds_label_A": "xds_label_A",
                     "xds_label_B": "xds_label_B"}

        Returns:
          A dict of deserialized labels.
        """
        return labels


class OpenTelemetryPluginOption:
    """
    An interface that allows you to add additional function to OpenTelemetryPlugin.
    """


# pylint: disable=no-self-use
class OpenTelemetryPlugin:
    """Describes a Plugin for OpenTelemetry observability."""

    plugin_options: Iterable[OpenTelemetryPluginOption]
    meter_provider: Optional[MeterProvider]
    target_attribute_filter: Callable[[str], bool]
    generic_method_attribute_filter: Callable[[str], bool]
    _plugins: List[_open_telemetry_observability._OpenTelemetryPlugin]

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
          target_attribute_filter: [DEPRECATED] This attribute is deprecated and should
        not be used.
        Once provided, this will be called per channel to decide whether to record the
        target attribute on client or to replace it with "other".
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
        self.target_attribute_filter = target_attribute_filter or (
            lambda target: True
        )
        self.generic_method_attribute_filter = (
            generic_method_attribute_filter or (lambda target: False)
        )
        self._plugins = [
            _open_telemetry_observability._OpenTelemetryPlugin(self)
        ]

    def register_global(self) -> None:
        """
        Registers a global plugin that acts on all channels and servers running on the process.

        Raises:
            RuntimeError: If a global plugin was already registered.
        """
        _open_telemetry_observability.start_open_telemetry_observability(
            plugins=self._plugins
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
            plugins=self._plugins
        )

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        _open_telemetry_observability.end_open_telemetry_observability()

    def _get_enabled_optional_labels(self) -> List[OptionalLabelType]:
        return []
