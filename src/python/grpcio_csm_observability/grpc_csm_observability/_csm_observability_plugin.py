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

    def __init__(self) -> None:
        fields = {}
        self._exchange_labels = {}
        self._additional_exchange_labels = {}

        # Labels from environment
        canonical_service_value = os.getenv(
            "CSM_CANONICAL_SERVICE_NAME", UNKNOWN_VALUE
        )
        workload_name_value = os.getenv("CSM_WORKLOAD_NAME", UNKNOWN_VALUE)
        mesh_id = os.getenv("CSM_MESH_ID", UNKNOWN_VALUE)

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
        self._additional_exchange_labels["csm.workload_canonical_service"] = (
            canonical_service_value
        )
        self._additional_exchange_labels["csm.mesh_id"] = mesh_id

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

    def __init__(self) -> None:
        self._label_injector = CSMOpenTelemetryLabelInjector()

    @staticmethod
    def is_active_on_client_channel(target: str) -> bool:
        # CSM is active on client channel if the target is Traffic Director.
        return target == TRAFFIC_DIRECTOR_AUTHORITY

    @staticmethod
    def is_active_on_server(
        xds: bool,  # pylint: disable=unused-argument
    ) -> bool:
        # CSM is active on server if CSM_MESH_ID is set.
        return os.getenv("CSM_MESH_ID") is not None

    def get_label_injector(self) -> OpenTelemetryLabelInjector:
        return self._label_injector


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
    ) -> None:
        self.plugin_options = plugin_options
        self.meter_provider = meter_provider
        self.generic_method_attribute_filter = (
            generic_method_attribute_filter or (lambda _: True)
        )

    def _get_enabled_optional_labels(self) -> List[OptionalLabelType]:
        return ["csm.workload_canonical_service", "csm.mesh_id"]


def get_value_from_struct(key: str, struct: struct_pb2.Struct) -> str:
    return struct.fields[key].string_value


def get_str_value_from_resource(
    attribute: Union[ResourceAttributes, str], resource: Resource
) -> str:
    value = resource.attributes.get(attribute, UNKNOWN_VALUE)
    return value if isinstance(value, str) else UNKNOWN_VALUE


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
