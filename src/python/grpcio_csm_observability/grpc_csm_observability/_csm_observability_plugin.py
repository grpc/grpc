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

import json
import os
import re
from typing import AnyStr, Callable, Dict, Iterable, List, Optional

from google.protobuf import struct_pb2

# pytype: disable=pyi-error
from grpc_observability import _open_telemetry_observability
from grpc_observability._observability import OptionalLabelType
from grpc_observability._open_telemetry_plugin import OpenTelemetryLabelInjector
from grpc_observability._open_telemetry_plugin import OpenTelemetryPlugin
from grpc_observability._open_telemetry_plugin import OpenTelemetryPluginOption
from opentelemetry.metrics import MeterProvider
from opentelemetry.resourcedetector.gcp_resource_detector import (
    GoogleCloudResourceDetector,
)
from opentelemetry.sdk.resources import Resource
from opentelemetry.sdk.resources import get_aggregated_resources
from opentelemetry.semconv.resource import ResourceAttributes

TRAFFIC_DIRECTOR_AUTHORITY = "traffic-director-global.xds.googleapis.com"
GRPC_METHOD_LABEL = "grpc.method"
GRPC_TARGET_LABEL = "grpc.target"
GRPC_OTHER_LABEL_VALUE = "other"
UNKNOWN_VALUE = "unknown"
TYPE_GCE = "gcp_compute_engine"
TYPE_GKE = "gcp_kubernetes_engine"

METADATA_EXCHANGE_KEY_FIXED_MAP = {
    "type": "csm.remote_workload_type",
    "canonical_service": "csm.remote_workload_canonical_service",
}

METADATA_EXCHANGE_KEY_GKE_MAP = {
    "workload_name": "csm.remote_workload_name",
    "namespace_name": "csm.remote_workload_namespace_name",
    "cluster_name": "csm.remote_workload_cluster_name",
    "location": "csm.remote_workload_location",
    "project_id": "csm.remote_workload_project_id",
}

METADATA_EXCHANGE_KEY_GCE_MAP = {
    "workload_name": "csm.remote_workload_name",
    "location": "csm.remote_workload_location",
    "project_id": "csm.remote_workload_project_id",
}


class CSMOpenTelemetryLabelInjector(OpenTelemetryLabelInjector):
    """
    An implementation of OpenTelemetryLabelInjector for CSM.
    """

    _exchange_labels: Dict[str, AnyStr]
    _local_labels: Dict[str, str]

    def __init__(self):
        fields = {}
        self._exchange_labels = {}
        self._local_labels = {}

        # Labels from environment
        canonical_service_value = os.getenv(
            "CSM_CANONICAL_SERVICE_NAME", UNKNOWN_VALUE
        )
        workload_name_value = os.getenv("CSM_WORKLOAD_NAME", UNKNOWN_VALUE)

        # Labels from resource detector.
        gcp_resource = get_aggregated_resources([GoogleCloudResourceDetector()])
        resource_type_value = self._get_str_value_from_resource(
            ResourceAttributes.CLOUD_PLATFORM, gcp_resource
        )
        namespace_value = self._get_str_value_from_resource(
            ResourceAttributes.K8S_NAMESPACE_NAME, gcp_resource
        )
        cluster_name_value = self._get_str_value_from_resource(
            ResourceAttributes.K8S_CLUSTER_NAME, gcp_resource
        )
        location_value = self._get_str_value_from_resource(
            ResourceAttributes.CLOUD_AVAILABILITY_ZONE, gcp_resource
        )
        if UNKNOWN_VALUE == location_value:
            location_value = self._get_str_value_from_resource(
                ResourceAttributes.CLOUD_REGION, gcp_resource
            )
        project_id_value = self._get_str_value_from_resource(
            ResourceAttributes.CLOUD_ACCOUNT_ID, gcp_resource
        )

        fields["type"] = struct_pb2.Value(string_value=resource_type_value)
        fields["canonical_service"] = struct_pb2.Value(
            string_value=canonical_service_value
        )
        if resource_type_value == "GKE":
            fields["workload_name"] = struct_pb2.Value(
                string_value=workload_name_value
            )
            fields["namespace_name"] = struct_pb2.Value(
                string_value=namespace_value
            )
            fields["cluster_name"] = struct_pb2.Value(
                string_value=cluster_name_value
            )
            fields["location"] = struct_pb2.Value(string_value=location_value)
            fields["project_id"] = struct_pb2.Value(
                string_value=project_id_value
            )
        elif resource_type_value == "GCE":
            fields["workload_name"] = struct_pb2.Value(
                string_value=workload_name_value
            )
            fields["location"] = struct_pb2.Value(string_value=location_value)
            fields["project_id"] = struct_pb2.Value(
                string_value=project_id_value
            )

        serialized_struct = struct_pb2.Struct(fields=fields)
        serialized_str = serialized_struct.SerializeToString()

        self._exchange_labels = {"XEnvoyPeerMetadata": serialized_str}
        self._local_labels[
            "csm.workload_canonical_service"
        ] = canonical_service_value
        self._local_labels["csm.mesh_id"] = self._get_mesh_id()

    def get_labels_for_exchange(self) -> Dict[str, AnyStr]:
        return self._exchange_labels

    def get_additional_labels(self) -> Dict[str, str]:
        return self._local_labels

    def deserialize_labels(
        self, labels: Dict[str, AnyStr]
    ) -> Dict[str, AnyStr]:
        deserialized_labels = {}
        for key, value in labels.items():
            if "XEnvoyPeerMetadata" == key:
                pb_struct = struct_pb2.Struct()
                pb_struct.ParseFromString(value)

                remote_type = self._get_value_from_struct("type", pb_struct)

                for key, remote_key in METADATA_EXCHANGE_KEY_FIXED_MAP.items():
                    deserialized_labels[
                        remote_key
                    ] = self._get_value_from_struct(key, pb_struct)

                if TYPE_GKE in remote_type:
                    for (
                        key,
                        remote_key,
                    ) in METADATA_EXCHANGE_KEY_GKE_MAP.items():
                        deserialized_labels[
                            remote_key
                        ] = self._get_value_from_struct(key, pb_struct)
                elif TYPE_GCE in remote_type:
                    for (
                        key,
                        remote_key,
                    ) in METADATA_EXCHANGE_KEY_GCE_MAP.items():
                        deserialized_labels[
                            remote_key
                        ] = self._get_value_from_struct(key, pb_struct)
            else:
                deserialized_labels[key] = value

        return deserialized_labels

    def _get_value_from_struct(
        self, key: str, struct: struct_pb2.Struct
    ) -> str:
        value = struct.fields.get(key)
        if not value:
            return UNKNOWN_VALUE
        return value.string_value

    def _get_str_value_from_resource(
        self, attribute: ResourceAttributes, resource: Resource
    ) -> str:
        value = resource.attributes.get(attribute, UNKNOWN_VALUE)
        return str(value)

    # Returns the mesh ID by reading and parsing the bootstrap file. Returns "unknown"
    # if for some reason, mesh ID could not be figured out.
    def _get_mesh_id(self) -> str:
        config_contents = self._get_bootstrap_config_contents()

        try:
            config_json = json.loads(config_contents)
            # The expected format of the Node ID is -
            # projects/[GCP Project number]/networks/mesh:[Mesh ID]/nodes/[UUID]
            node_id_parts = config_json.get("node", {}).get("id", "").split("/")
            if len(node_id_parts) == 6 and "mesh:" in node_id_parts[3]:
                return node_id_parts[3][len("mesh:"):]
        except json.decoder.JSONDecodeError:
            return UNKNOWN_VALUE

        return UNKNOWN_VALUE

    def _get_bootstrap_config_contents(self) -> str:
        """Get the contents of the bootstrap config from environment variable or file.

        Returns:
            The content from environment variable. Or empty str if no config was found.
        """
        contents_str = ""
        for source in ("GRPC_XDS_BOOTSTRAP", "GRPC_XDS_BOOTSTRAP_CONFIG"):
            config = os.getenv(source)
            if config:
                if os.path.isfile(config):  # Prioritize file over raw config
                    with open(config, "r") as f:
                        contents_str = f.read()
                else:
                    contents_str = config

        return contents_str


class CsmOpenTelemetryPluginOption(OpenTelemetryPluginOption):
    """
    An implementation of OpenTelemetryPlugin for CSM.
    """

    _label_injector: CSMOpenTelemetryLabelInjector

    def __init__(self):
        self._label_injector = CSMOpenTelemetryLabelInjector()

    def is_active_on_client_channel(self, target: str) -> bool:
        """Determines whether this plugin option is active on a channel based on target.

        Args:
          target: Required. The target for the RPC.

        Returns:
          True if this this plugin option is active on the channel, false otherwise.
        """
        # CSM channels should have an "xds" scheme
        if not target.startswith("xds"):
            return False
        # If scheme is correct, the authority should be TD if exist
        authority_pattern = r"^xds:\/\/([^/]+)"
        match = re.search(authority_pattern, target)
        if match:
            return TRAFFIC_DIRECTOR_AUTHORITY in match.group(1)
        else:
            return True

    def is_active_on_server(self, xds: bool) -> bool:
        """Determines whether this plugin option is active on a given server.

        Since servers don't need to be xds enabled to work as part of a service
        mesh, we're returning True and enable this PluginOption for all servers.

        Args:
          xds: Required. if this server is build for xds.

        Returns:
          True if this this plugin option is active on the server, false otherwise.
        """
        return True

    def get_label_injector(self) -> OpenTelemetryLabelInjector:
        return self._label_injector


# pylint: disable=no-self-use
class CsmOpenTelemetryPlugin(OpenTelemetryPlugin):
    """Describes a Plugin for CSM OpenTelemetry observability.

    This is class is part of an EXPERIMENTAL API.
    """

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
        self.plugin_options = list(plugin_options) + [
            CsmOpenTelemetryPluginOption()
        ]
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
        self._plugins = [
            _open_telemetry_observability._OpenTelemetryPlugin(self)
        ]

    def _get_enabled_optional_labels(self) -> List[OptionalLabelType]:
        return [OptionalLabelType.XDS_SERVICE_LABELS]
