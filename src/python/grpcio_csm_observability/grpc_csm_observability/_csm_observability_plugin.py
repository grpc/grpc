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
from typing import AnyStr, Callable, Dict, Iterable, List, Optional, Union

from google.protobuf import struct_pb2
from grpc_observability._observability import OptionalLabelType
from grpc_observability._open_telemetry_plugin import OpenTelemetryLabelInjector
from grpc_observability._open_telemetry_plugin import OpenTelemetryPlugin
from grpc_observability._open_telemetry_plugin import OpenTelemetryPluginOption

# pytype: disable=pyi-error
from opentelemetry.metrics import MeterProvider
from opentelemetry.resourcedetector.gcp_resource_detector import (
    GoogleCloudResourceDetector,
)
from opentelemetry.sdk.resources import Resource
from opentelemetry.semconv.resource import ResourceAttributes

TRAFFIC_DIRECTOR_AUTHORITY = "traffic-director-global.xds.googleapis.com"
UNKNOWN_VALUE = "unknown"
TYPE_GCE = "gcp_compute_engine"
TYPE_GKE = "gcp_kubernetes_engine"
MESH_ID_PREFIX = "mesh:"

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

    This injector will fetch labels from GCP resource detector and
    environment, it's also responsible for serialize and deserialize
    metadata exchange labels.
    """

    _exchange_labels: Dict[str, AnyStr]
    _additional_exchange_labels: Dict[str, str]

    def __init__(self):
        fields = {}
        self._exchange_labels = {}
        self._additional_exchange_labels = {}

        # Labels from environment
        canonical_service_value = os.getenv(
            "CSM_CANONICAL_SERVICE_NAME", UNKNOWN_VALUE
        )
        workload_name_value = os.getenv("CSM_WORKLOAD_NAME", UNKNOWN_VALUE)

        gcp_resource = GoogleCloudResourceDetector().detect()
        resource_type_value = get_resource_type(gcp_resource)
        namespace_value = get_str_value_from_resource(
            ResourceAttributes.K8S_NAMESPACE_NAME, gcp_resource
        )
        cluster_name_value = get_str_value_from_resource(
            ResourceAttributes.K8S_CLUSTER_NAME, gcp_resource
        )
        # ResourceAttributes.CLOUD_AVAILABILITY_ZONE are called
        # "zones" on Google Cloud.
        location_value = get_str_value_from_resource("cloud.zone", gcp_resource)
        if UNKNOWN_VALUE == location_value:
            location_value = get_str_value_from_resource(
                ResourceAttributes.CLOUD_REGION, gcp_resource
            )
        project_id_value = get_str_value_from_resource(
            ResourceAttributes.CLOUD_ACCOUNT_ID, gcp_resource
        )

        fields["type"] = struct_pb2.Value(string_value=resource_type_value)
        fields["canonical_service"] = struct_pb2.Value(
            string_value=canonical_service_value
        )
        if resource_type_value == TYPE_GKE:
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
        elif resource_type_value == TYPE_GCE:
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
        self._additional_exchange_labels[
            "csm.workload_canonical_service"
        ] = canonical_service_value
        self._additional_exchange_labels["csm.mesh_id"] = get_mesh_id()

    def get_labels_for_exchange(self) -> Dict[str, AnyStr]:
        return self._exchange_labels

    def get_additional_labels(
        self, include_exchange_labels: bool
    ) -> Dict[str, str]:
        if include_exchange_labels:
            return self._additional_exchange_labels
        else:
            return {}

    @staticmethod
    def deserialize_labels(labels: Dict[str, AnyStr]) -> Dict[str, AnyStr]:
        deserialized_labels = {}
        for key, value in labels.items():
            if "XEnvoyPeerMetadata" == key:
                pb_struct = struct_pb2.Struct()
                pb_struct.ParseFromString(value)

                remote_type = get_value_from_struct("type", pb_struct)

                for (
                    local_key,
                    remote_key,
                ) in METADATA_EXCHANGE_KEY_FIXED_MAP.items():
                    deserialized_labels[remote_key] = get_value_from_struct(
                        local_key, pb_struct
                    )
                if remote_type == TYPE_GKE:
                    for (
                        local_key,
                        remote_key,
                    ) in METADATA_EXCHANGE_KEY_GKE_MAP.items():
                        deserialized_labels[remote_key] = get_value_from_struct(
                            local_key, pb_struct
                        )
                elif remote_type == TYPE_GCE:
                    for (
                        local_key,
                        remote_key,
                    ) in METADATA_EXCHANGE_KEY_GCE_MAP.items():
                        deserialized_labels[remote_key] = get_value_from_struct(
                            local_key, pb_struct
                        )
            # If CSM label injector is enabled on server side but client didn't send
            # XEnvoyPeerMetadata, we'll record remote label as unknown.
            else:
                for _, remote_key in METADATA_EXCHANGE_KEY_FIXED_MAP.items():
                    deserialized_labels[remote_key] = UNKNOWN_VALUE
                deserialized_labels[key] = value

        return deserialized_labels


class CsmOpenTelemetryPluginOption(OpenTelemetryPluginOption):
    """
    An implementation of OpenTelemetryPlugin for CSM.
    """

    _label_injector: CSMOpenTelemetryLabelInjector

    def __init__(self):
        self._label_injector = CSMOpenTelemetryLabelInjector()

    @staticmethod
    def is_active_on_client_channel(target: str) -> bool:
        """Determines whether this plugin option is active on a channel based on target.

        Args:
          target: Required. The target for the RPC.

        Returns:
          True if this this plugin option is active on the channel, false otherwise.
        """
        # CSM channels should have an "xds" scheme
        if not target.startswith("xds:"):
            return False
        # If scheme is correct, the authority should be TD if exist
        authority_pattern = r"^xds:\/\/([^/]+)"
        match = re.search(authority_pattern, target)
        if match:
            return TRAFFIC_DIRECTOR_AUTHORITY in match.group(1)
        else:
            # Return True if the authority doesn't exist
            return True

    @staticmethod
    def is_active_on_server(
        xds: bool,  # pylint: disable=unused-argument
    ) -> bool:
        """Determines whether this plugin option is active on a given server.

        Since servers don't need to be xds enabled to work as part of a service
        mesh, we're returning True and enable this PluginOption for all servers.

        Note: This always returns true because server can be part of the mesh even
        if it's not xds-enabled. And we want CSM labels for those servers too.

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
    generic_method_attribute_filter: Callable[[str], bool]

    def __init__(
        self,
        *,
        plugin_options: Iterable[OpenTelemetryPluginOption] = [],
        meter_provider: Optional[MeterProvider] = None,
        generic_method_attribute_filter: Optional[Callable[[str], bool]] = None,
    ):
        new_options = list(plugin_options) + [CsmOpenTelemetryPluginOption()]
        super().__init__(
            plugin_options=new_options,
            meter_provider=meter_provider,
            generic_method_attribute_filter=generic_method_attribute_filter,
        )

    def _get_enabled_optional_labels(self) -> List[OptionalLabelType]:
        return [OptionalLabelType.XDS_SERVICE_LABELS]


def get_value_from_struct(key: str, struct: struct_pb2.Struct) -> str:
    value = struct.fields.get(key)
    if not value:
        return UNKNOWN_VALUE
    return value.string_value


def get_str_value_from_resource(
    attribute: Union[ResourceAttributes, str], resource: Resource
) -> str:
    value = resource.attributes.get(attribute, UNKNOWN_VALUE)
    return str(value)


# pylint: disable=line-too-long
def get_resource_type(gcp_resource: Resource) -> str:
    # Convert resource type from GoogleCloudResourceDetector to the value we used for
    # metadata exchange.
    # Reference: https://github.com/GoogleCloudPlatform/opentelemetry-operations-python/blob/cc61f23a5ff2f16f4aa2c38d07e55153828849cc/opentelemetry-resourcedetector-gcp/src/opentelemetry/resourcedetector/gcp_resource_detector/__init__.py#L96
    gcp_resource_type = get_str_value_from_resource(
        "gcp.resource_type", gcp_resource
    )
    if gcp_resource_type == "gke_container":
        return TYPE_GKE
    elif gcp_resource_type == "gce_instance":
        return TYPE_GCE
    else:
        return gcp_resource_type


# Returns the mesh ID by reading and parsing the bootstrap file. Returns "unknown"
# if for some reason, mesh ID could not be figured out.
def get_mesh_id() -> str:
    config_contents = get_bootstrap_config_contents()

    try:
        config_json = json.loads(config_contents)
        # The expected format of the Node ID is -
        # projects/[GCP Project number]/networks/mesh:[Mesh ID]/nodes/[UUID]
        node_id_parts = config_json.get("node", {}).get("id", "").split("/")
        if len(node_id_parts) == 6 and node_id_parts[3].startswith(
            MESH_ID_PREFIX
        ):
            return node_id_parts[3][len(MESH_ID_PREFIX) :]
    except json.decoder.JSONDecodeError:
        return UNKNOWN_VALUE

    return UNKNOWN_VALUE


def get_bootstrap_config_contents() -> str:
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
