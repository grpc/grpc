# Copyright 2020 gRPC authors.
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
import dataclasses
import datetime
import enum
import logging
from typing import Any, Dict, List, Optional, Set

from googleapiclient import discovery
import googleapiclient.errors

import framework.errors
from framework.helpers import retryers
from framework.infrastructure import gcp

logger = logging.getLogger(__name__)


class ComputeV1(
    gcp.api.GcpProjectApiResource
):  # pylint: disable=too-many-public-methods
    # TODO(sergiitk): move someplace better
    _WAIT_FOR_BACKEND_SEC = 60 * 10
    _WAIT_FOR_BACKEND_SLEEP_SEC = 4
    _WAIT_FOR_OPERATION_SEC = 60 * 10

    @dataclasses.dataclass(frozen=True)
    class GcpResource:
        name: str
        url: str

    @dataclasses.dataclass(frozen=True)
    class ZonalGcpResource(GcpResource):
        zone: str

    def __init__(
        self,
        api_manager: gcp.api.GcpApiManager,
        project: str,
        version: str = "v1",
    ):
        super().__init__(api_manager.compute(version), project)

    class HealthCheckProtocol(enum.Enum):
        TCP = enum.auto()
        GRPC = enum.auto()

    class BackendServiceProtocol(enum.Enum):
        HTTP2 = enum.auto()
        GRPC = enum.auto()

    def create_health_check(
        self,
        name: str,
        protocol: HealthCheckProtocol,
        *,
        port: Optional[int] = None,
    ) -> "GcpResource":
        if protocol is self.HealthCheckProtocol.TCP:
            health_check_field = "tcpHealthCheck"
        elif protocol is self.HealthCheckProtocol.GRPC:
            health_check_field = "grpcHealthCheck"
        else:
            raise TypeError(f"Unexpected Health Check protocol: {protocol}")

        health_check_settings = {}
        if port is None:
            health_check_settings["portSpecification"] = "USE_SERVING_PORT"
        else:
            health_check_settings["portSpecification"] = "USE_FIXED_PORT"
            health_check_settings["port"] = port

        return self._insert_resource(
            self.api.healthChecks(),
            {
                "name": name,
                "type": protocol.name,
                health_check_field: health_check_settings,
            },
        )

    def list_health_check(self):
        return self._list_resource(self.api.healthChecks())

    def delete_health_check(self, name):
        self._delete_resource(self.api.healthChecks(), "healthCheck", name)

    def create_firewall_rule(
        self,
        name: str,
        network_url: str,
        source_ranges: List[str],
        ports: List[str],
    ) -> Optional["GcpResource"]:
        try:
            return self._insert_resource(
                self.api.firewalls(),
                {
                    "allowed": [{"IPProtocol": "tcp", "ports": ports}],
                    "direction": "INGRESS",
                    "name": name,
                    "network": network_url,
                    "priority": 1000,
                    "sourceRanges": source_ranges,
                    "targetTags": ["allow-health-checks"],
                },
            )
        except googleapiclient.errors.HttpError as http_error:
            # TODO(lidiz) use status_code() when we upgrade googleapiclient
            if http_error.resp.status == 409:
                logger.debug("Firewall rule %s already existed", name)
                return None
            else:
                raise

    def delete_firewall_rule(self, name):
        self._delete_resource(self.api.firewalls(), "firewall", name)

    def create_backend_service_traffic_director(
        self,
        name: str,
        health_check: "GcpResource",
        affinity_header: Optional[str] = None,
        protocol: Optional[BackendServiceProtocol] = None,
        subset_size: Optional[int] = None,
        locality_lb_policies: Optional[List[dict]] = None,
        outlier_detection: Optional[dict] = None,
    ) -> "GcpResource":
        if not isinstance(protocol, self.BackendServiceProtocol):
            raise TypeError(f"Unexpected Backend Service protocol: {protocol}")
        body = {
            "name": name,
            "loadBalancingScheme": "INTERNAL_SELF_MANAGED",  # Traffic Director
            "healthChecks": [health_check.url],
            "protocol": protocol.name,
        }
        # If affinity header is specified, config the backend service to support
        # affinity, and set affinity header to the one given.
        if affinity_header:
            body["sessionAffinity"] = "HEADER_FIELD"
            body["localityLbPolicy"] = "RING_HASH"
            body["consistentHash"] = {
                "httpHeaderName": affinity_header,
            }
        if subset_size:
            body["subsetting"] = {
                "policy": "CONSISTENT_HASH_SUBSETTING",
                "subsetSize": subset_size,
            }
        if locality_lb_policies:
            body["localityLbPolicies"] = locality_lb_policies
        if outlier_detection:
            body["outlierDetection"] = outlier_detection
        return self._insert_resource(self.api.backendServices(), body)

    def get_backend_service_traffic_director(self, name: str) -> "GcpResource":
        return self._get_resource(
            self.api.backendServices(), backendService=name
        )

    def patch_backend_service(self, backend_service, body, **kwargs):
        self._patch_resource(
            collection=self.api.backendServices(),
            backendService=backend_service.name,
            body=body,
            **kwargs,
        )

    def backend_service_patch_backends(
        self,
        backend_service,
        backends,
        max_rate_per_endpoint: Optional[int] = None,
    ):
        if max_rate_per_endpoint is None:
            max_rate_per_endpoint = 5
        backend_list = [
            {
                "group": backend.url,
                "balancingMode": "RATE",
                "maxRatePerEndpoint": max_rate_per_endpoint,
            }
            for backend in backends
        ]

        self._patch_resource(
            collection=self.api.backendServices(),
            body={"backends": backend_list},
            backendService=backend_service.name,
        )

    def backend_service_remove_all_backends(self, backend_service):
        self._patch_resource(
            collection=self.api.backendServices(),
            body={"backends": []},
            backendService=backend_service.name,
        )

    def delete_backend_service(self, name):
        self._delete_resource(
            self.api.backendServices(), "backendService", name
        )

    def create_url_map(
        self,
        name: str,
        matcher_name: str,
        src_hosts,
        dst_default_backend_service: "GcpResource",
        dst_host_rule_match_backend_service: Optional["GcpResource"] = None,
    ) -> "GcpResource":
        if dst_host_rule_match_backend_service is None:
            dst_host_rule_match_backend_service = dst_default_backend_service
        return self._insert_resource(
            self.api.urlMaps(),
            {
                "name": name,
                "defaultService": dst_default_backend_service.url,
                "hostRules": [
                    {
                        "hosts": src_hosts,
                        "pathMatcher": matcher_name,
                    }
                ],
                "pathMatchers": [
                    {
                        "name": matcher_name,
                        "defaultService": dst_host_rule_match_backend_service.url,
                    }
                ],
            },
        )

    def create_url_map_with_content(self, url_map_body: Any) -> "GcpResource":
        return self._insert_resource(self.api.urlMaps(), url_map_body)

    def patch_url_map(self, url_map: "GcpResource", body, **kwargs):
        self._patch_resource(
            collection=self.api.urlMaps(),
            urlMap=url_map.name,
            body=body,
            **kwargs,
        )

    def delete_url_map(self, name):
        self._delete_resource(self.api.urlMaps(), "urlMap", name)

    def create_target_grpc_proxy(
        self,
        name: str,
        url_map: "GcpResource",
        validate_for_proxyless: bool = True,
    ) -> "GcpResource":
        return self._insert_resource(
            self.api.targetGrpcProxies(),
            {
                "name": name,
                "url_map": url_map.url,
                "validate_for_proxyless": validate_for_proxyless,
            },
        )

    def delete_target_grpc_proxy(self, name):
        self._delete_resource(
            self.api.targetGrpcProxies(), "targetGrpcProxy", name
        )

    def create_target_http_proxy(
        self,
        name: str,
        url_map: "GcpResource",
    ) -> "GcpResource":
        return self._insert_resource(
            self.api.targetHttpProxies(),
            {
                "name": name,
                "url_map": url_map.url,
            },
        )

    def delete_target_http_proxy(self, name):
        self._delete_resource(
            self.api.targetHttpProxies(), "targetHttpProxy", name
        )

    def create_forwarding_rule(
        self,
        name: str,
        src_port: int,
        target_proxy: "GcpResource",
        network_url: str,
        *,
        ip_address: str = "0.0.0.0",
    ) -> "GcpResource":
        return self._insert_resource(
            self.api.globalForwardingRules(),
            {
                "name": name,
                "loadBalancingScheme": "INTERNAL_SELF_MANAGED",  # Traffic Director
                "portRange": src_port,
                "IPAddress": ip_address,
                "network": network_url,
                "target": target_proxy.url,
            },
        )

    def exists_forwarding_rule(self, src_port) -> bool:
        # TODO(sergiitk): Better approach for confirming the port is available.
        #   It's possible a rule allocates actual port range, e.g 8000-9000,
        #   and this wouldn't catch it. For now, we assume there's no
        #   port ranges used in the project.
        filter_str = (
            f'(portRange eq "{src_port}-{src_port}") '
            '(IPAddress eq "0.0.0.0")'
            '(loadBalancingScheme eq "INTERNAL_SELF_MANAGED")'
        )
        return self._exists_resource(
            self.api.globalForwardingRules(), resource_filter=filter_str
        )

    def delete_forwarding_rule(self, name):
        self._delete_resource(
            self.api.globalForwardingRules(), "forwardingRule", name
        )

    def wait_for_network_endpoint_group(
        self,
        name: str,
        zone: str,
        *,
        timeout_sec=_WAIT_FOR_BACKEND_SEC,
        wait_sec=_WAIT_FOR_BACKEND_SLEEP_SEC,
    ):
        retryer = retryers.constant_retryer(
            wait_fixed=datetime.timedelta(seconds=wait_sec),
            timeout=datetime.timedelta(seconds=timeout_sec),
            check_result=lambda neg: neg and neg.get("size", 0) > 0,
        )
        network_endpoint_group = retryer(
            self._retry_load_network_endpoint_group, name, zone
        )
        # TODO(sergiitk): dataclass
        return self.ZonalGcpResource(
            network_endpoint_group["name"],
            network_endpoint_group["selfLink"],
            zone,
        )

    def _retry_load_network_endpoint_group(self, name: str, zone: str):
        try:
            neg = self.get_network_endpoint_group(name, zone)
            logger.debug(
                "Waiting for endpoints: NEG %s in zone %s, current count %s",
                neg["name"],
                zone,
                neg.get("size"),
            )
        except googleapiclient.errors.HttpError as error:
            # noinspection PyProtectedMember
            reason = error._get_reason()
            logger.debug(
                "Retrying NEG load, got %s, details %s",
                error.resp.status,
                reason,
            )
            raise
        return neg

    def get_network_endpoint_group(self, name, zone):
        neg = (
            self.api.networkEndpointGroups()
            .get(project=self.project, networkEndpointGroup=name, zone=zone)
            .execute()
        )
        # TODO(sergiitk): dataclass
        return neg

    def wait_for_backends_healthy_status(
        self,
        backend_service: GcpResource,
        backends: Set[ZonalGcpResource],
        *,
        timeout_sec: int = _WAIT_FOR_BACKEND_SEC,
        wait_sec: int = _WAIT_FOR_BACKEND_SLEEP_SEC,
    ) -> None:
        if not backends:
            raise ValueError("The list of backends to wait on is empty")

        timeout = datetime.timedelta(seconds=timeout_sec)
        retryer = retryers.constant_retryer(
            wait_fixed=datetime.timedelta(seconds=wait_sec),
            timeout=timeout,
            check_result=lambda result: result,
        )
        pending = set(backends)
        try:
            retryer(self._retry_backends_health, backend_service, pending)
        except retryers.RetryError as retry_err:
            unhealthy_backends: str = ",".join(
                [backend.name for backend in pending]
            )

            # Attempt to load backend health info for better debug info.
            try:
                unhealthy = []
                # Everything left in pending was unhealthy on the last retry.
                for backend in pending:
                    # It's possible the health status has changed since we
                    # gave up retrying, but this should be very rare.
                    health_status = self.get_backend_service_backend_health(
                        backend_service,
                        backend,
                    )
                    unhealthy.append(
                        {"name": backend.name, "health_status": health_status}
                    )

                # Override the plain list of unhealthy backend name with
                # the one showing the latest backend statuses.
                unhealthy_backends = self.resources_pretty_format(
                    unhealthy,
                    highlight=False,
                )
            except Exception as error:  # noqa pylint: disable=broad-except
                logger.error(
                    "Couldn't load backend health info, plain list name"
                    "will be printed instead. Error: %r",
                    error,
                )

            retry_err.add_note(
                framework.errors.FrameworkError.note_blanket_error_info_below(
                    "One or several NEGs (Network Endpoint Groups) didn't"
                    " report HEALTHY status within expected timeout.",
                    info_below=(
                        f"Timeout {timeout} (h:mm:ss) waiting for backend"
                        f" service '{backend_service.name}' to report all NEGs"
                        " in the HEALTHY status:"
                        f" {[backend.name for backend in backends]}."
                        f"\nUnhealthy backends:\n{unhealthy_backends}"
                    ),
                )
            )

            raise

    def _retry_backends_health(
        self, backend_service: GcpResource, pending: Set[ZonalGcpResource]
    ):
        for backend in pending:
            result = self.get_backend_service_backend_health(
                backend_service, backend
            )
            if "healthStatus" not in result:
                logger.debug(
                    "Waiting for instances: backend %s, zone %s",
                    backend.name,
                    backend.zone,
                )
                continue

            backend_healthy = True
            for instance in result["healthStatus"]:
                logger.debug(
                    "Backend %s in zone %s: instance %s:%s health: %s",
                    backend.name,
                    backend.zone,
                    instance["ipAddress"],
                    instance["port"],
                    instance["healthState"],
                )
                if instance["healthState"] != "HEALTHY":
                    backend_healthy = False

            if backend_healthy:
                logger.info(
                    "Backend %s in zone %s reported healthy",
                    backend.name,
                    backend.zone,
                )
                pending.remove(backend)

        return not pending

    def get_backend_service_backend_health(self, backend_service, backend):
        return (
            self.api.backendServices()
            .getHealth(
                project=self.project,
                backendService=backend_service.name,
                body={"group": backend.url},
            )
            .execute()
        )

    def _get_resource(
        self, collection: discovery.Resource, **kwargs
    ) -> "GcpResource":
        resp = collection.get(project=self.project, **kwargs).execute()
        logger.info(
            "Loaded compute resource:\n%s", self.resource_pretty_format(resp)
        )
        return self.GcpResource(resp["name"], resp["selfLink"])

    def _exists_resource(
        self, collection: discovery.Resource, resource_filter: str
    ) -> bool:
        resp = collection.list(
            project=self.project, filter=resource_filter, maxResults=1
        ).execute(num_retries=self._GCP_API_RETRIES)
        if "kind" not in resp:
            # TODO(sergiitk): better error
            raise ValueError('List response "kind" is missing')
        return "items" in resp and resp["items"]

    def _insert_resource(
        self, collection: discovery.Resource, body: Dict[str, Any]
    ) -> "GcpResource":
        logger.info(
            "Creating compute resource:\n%s", self.resource_pretty_format(body)
        )
        resp = self._execute(collection.insert(project=self.project, body=body))
        return self.GcpResource(body["name"], resp["targetLink"])

    def _patch_resource(self, collection, body, **kwargs):
        logger.info(
            "Patching compute resource:\n%s", self.resource_pretty_format(body)
        )
        self._execute(
            collection.patch(project=self.project, body=body, **kwargs)
        )

    def _list_resource(self, collection: discovery.Resource):
        return collection.list(project=self.project).execute(
            num_retries=self._GCP_API_RETRIES
        )

    def _delete_resource(
        self,
        collection: discovery.Resource,
        resource_type: str,
        resource_name: str,
    ) -> bool:
        try:
            params = {"project": self.project, resource_type: resource_name}
            self._execute(collection.delete(**params))
            return True
        except googleapiclient.errors.HttpError as error:
            if error.resp and error.resp.status == 404:
                logger.info(
                    'Resource %s "%s" not deleted since it does not exist',
                    resource_type,
                    resource_name,
                )
            else:
                logger.warning(
                    'Failed to delete %s "%s", %r',
                    resource_type,
                    resource_name,
                    error,
                )
        return False

    @staticmethod
    def _operation_status_done(operation):
        return "status" in operation and operation["status"] == "DONE"

    def _execute(  # pylint: disable=arguments-differ
        self, request, *, timeout_sec=_WAIT_FOR_OPERATION_SEC
    ):
        operation = request.execute(num_retries=self._GCP_API_RETRIES)
        logger.debug("Operation %s", operation)
        return self._wait(operation["name"], timeout_sec)

    def _wait(
        self, operation_id: str, timeout_sec: int = _WAIT_FOR_OPERATION_SEC
    ) -> dict:
        logger.info(
            "Waiting %s sec for compute operation id: %s",
            timeout_sec,
            operation_id,
        )

        # TODO(sergiitk) try using wait() here
        # https://googleapis.github.io/google-api-python-client/docs/dyn/compute_v1.globalOperations.html#wait
        op_request = self.api.globalOperations().get(
            project=self.project, operation=operation_id
        )
        operation = self.wait_for_operation(
            operation_request=op_request,
            test_success_fn=self._operation_status_done,
            timeout_sec=timeout_sec,
        )

        logger.debug("Completed operation: %s", operation)
        if "error" in operation:
            # This shouldn't normally happen: gcp library raises on errors.
            raise Exception(
                f"Compute operation {operation_id} failed: {operation}"
            )
        return operation
