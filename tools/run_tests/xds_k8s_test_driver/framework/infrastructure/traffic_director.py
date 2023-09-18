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
import functools
import logging
import random
from typing import Any, Dict, List, Optional, Set

from framework import xds_flags
from framework.infrastructure import gcp

logger = logging.getLogger(__name__)

# Type aliases
# Compute
_ComputeV1 = gcp.compute.ComputeV1
GcpResource = _ComputeV1.GcpResource
HealthCheckProtocol = _ComputeV1.HealthCheckProtocol
SessionAffinityProtocol = _ComputeV1.SessionAffinityProtocol
ZonalGcpResource = _ComputeV1.ZonalGcpResource
BackendServiceProtocol = _ComputeV1.BackendServiceProtocol
_BackendGRPC = BackendServiceProtocol.GRPC
_HealthCheckGRPC = HealthCheckProtocol.GRPC
_SessionAffinityHTTP = SessionAffinityProtocol.HTTP

# Network Security
_NetworkSecurityV1Beta1 = gcp.network_security.NetworkSecurityV1Beta1
ServerTlsPolicy = gcp.network_security.ServerTlsPolicy
ClientTlsPolicy = gcp.network_security.ClientTlsPolicy
AuthorizationPolicy = gcp.network_security.AuthorizationPolicy

# Network Services
_NetworkServicesV1Alpha1 = gcp.network_services.NetworkServicesV1Alpha1
_NetworkServicesV1Beta1 = gcp.network_services.NetworkServicesV1Beta1
EndpointPolicy = gcp.network_services.EndpointPolicy
GrpcRoute = gcp.network_services.GrpcRoute
HttpRoute = gcp.network_services.HttpRoute
Mesh = gcp.network_services.Mesh

# Testing metadata consts
TEST_AFFINITY_METADATA_KEY = "xds_md"


class TrafficDirectorManager:  # pylint: disable=too-many-public-methods
    compute: _ComputeV1
    resource_prefix: str
    resource_suffix: str

    BACKEND_SERVICE_NAME = "backend-service"
    ALTERNATIVE_BACKEND_SERVICE_NAME = "backend-service-alt"
    AFFINITY_BACKEND_SERVICE_NAME = "backend-service-affinity"
    HEALTH_CHECK_NAME = "health-check"
    URL_MAP_NAME = "url-map"
    ALTERNATIVE_URL_MAP_NAME = "url-map-alt"
    URL_MAP_PATH_MATCHER_NAME = "path-matcher"
    TARGET_PROXY_NAME = "target-proxy"
    ALTERNATIVE_TARGET_PROXY_NAME = "target-proxy-alt"
    FORWARDING_RULE_NAME = "forwarding-rule"
    ALTERNATIVE_FORWARDING_RULE_NAME = "forwarding-rule-alt"
    FIREWALL_RULE_NAME = "allow-health-checks"
    SESSION_AFFINITY_NAME = "session-affinity-policy"

    def __init__(
        self,
        gcp_api_manager: gcp.api.GcpApiManager,
        project: str,
        *,
        resource_prefix: str,
        resource_suffix: str,
        network: str = "default",
        compute_api_version: str = "v1",
    ):
        # API
        self.compute = _ComputeV1(
            gcp_api_manager, project, version=compute_api_version
        )

        # Settings
        self.project: str = project
        self.network: str = network
        self.resource_prefix: str = resource_prefix
        self.resource_suffix: str = resource_suffix

        # Managed resources
        self.health_check: Optional[GcpResource] = None
        self.backend_service: Optional[GcpResource] = None
        # TODO(sergiitk): remove this flag once backend service resource loaded
        self.backend_service_protocol: Optional[BackendServiceProtocol] = None
        self.url_map: Optional[GcpResource] = None
        self.alternative_url_map: Optional[GcpResource] = None
        self.firewall_rule: Optional[GcpResource] = None
        self.target_proxy: Optional[GcpResource] = None
        # TODO(sergiitk): remove this flag once target proxy resource loaded
        self.target_proxy_is_http: bool = False
        self.alternative_target_proxy: Optional[GcpResource] = None
        self.forwarding_rule: Optional[GcpResource] = None
        self.alternative_forwarding_rule: Optional[GcpResource] = None
        self.backends: Set[ZonalGcpResource] = set()
        self.alternative_backend_service: Optional[GcpResource] = None
        # TODO(sergiitk): remove this flag once backend service resource loaded
        self.alternative_backend_service_protocol: Optional[
            BackendServiceProtocol
        ] = None
        self.alternative_backends: Set[ZonalGcpResource] = set()
        self.affinity_backend_service: Optional[GcpResource] = None
        # TODO(sergiitk): remove this flag once backend service resource loaded
        self.affinity_backend_service_protocol: Optional[
            BackendServiceProtocol
        ] = None
        self.affinity_backends: Set[ZonalGcpResource] = set()

    @property
    def network_url(self):
        return f"global/networks/{self.network}"

    def setup_for_grpc(
        self,
        service_host,
        service_port,
        *,
        backend_protocol: Optional[BackendServiceProtocol] = _BackendGRPC,
        health_check_port: Optional[int] = None,
    ):
        self.setup_backend_for_grpc(
            protocol=backend_protocol, health_check_port=health_check_port
        )
        self.setup_routing_rule_map_for_grpc(service_host, service_port)

    def setup_backend_for_grpc(
        self,
        *,
        protocol: Optional[BackendServiceProtocol] = _BackendGRPC,
        health_check_port: Optional[int] = None,
    ):
        self.create_health_check(port=health_check_port)
        self.create_backend_service(protocol)

    def setup_routing_rule_map_for_grpc(self, service_host, service_port):
        self.create_url_map(service_host, service_port)
        self.create_target_proxy()
        self.create_forwarding_rule(service_port)

    def cleanup(self, *, force=False):
        # Cleanup in the reverse order of creation
        self.delete_forwarding_rule(force=force)
        self.delete_alternative_forwarding_rule(force=force)
        self.delete_target_http_proxy(force=force)
        self.delete_target_grpc_proxy(force=force)
        self.delete_alternative_target_grpc_proxy(force=force)
        self.delete_url_map(force=force)
        self.delete_alternative_url_map(force=force)
        self.delete_backend_service(force=force)
        self.delete_alternative_backend_service(force=force)
        self.delete_affinity_backend_service(force=force)
        self.delete_health_check(force=force)

    @functools.lru_cache(None)
    def make_resource_name(self, name: str) -> str:
        """Make dash-separated resource name with resource prefix and suffix."""
        parts = [self.resource_prefix, name]
        # Avoid trailing dash when the suffix is empty.
        if self.resource_suffix:
            parts.append(self.resource_suffix)
        return "-".join(parts)

    def create_health_check(
        self,
        *,
        protocol: Optional[HealthCheckProtocol] = _HealthCheckGRPC,
        port: Optional[int] = None,
    ):
        if self.health_check:
            raise ValueError(
                f"Health check {self.health_check.name} "
                "already created, delete it first"
            )
        if protocol is None:
            protocol = _HealthCheckGRPC

        name = self.make_resource_name(self.HEALTH_CHECK_NAME)
        logger.info('Creating %s Health Check "%s"', protocol.name, name)
        resource = self.compute.create_health_check(name, protocol, port=port)
        self.health_check = resource

    def delete_health_check(self, force=False):
        if force:
            name = self.make_resource_name(self.HEALTH_CHECK_NAME)
        elif self.health_check:
            name = self.health_check.name
        else:
            return
        logger.info('Deleting Health Check "%s"', name)
        self.compute.delete_health_check(name)
        self.health_check = None

    def create_backend_service(
        self,
        protocol: Optional[BackendServiceProtocol] = _BackendGRPC,
        subset_size: Optional[int] = None,
        affinity_header: Optional[str] = None,
        locality_lb_policies: Optional[List[dict]] = None,
        outlier_detection: Optional[dict] = None,
    ):
        if protocol is None:
            protocol = _BackendGRPC

        name = self.make_resource_name(self.BACKEND_SERVICE_NAME)
        logger.info('Creating %s Backend Service "%s"', protocol.name, name)
        resource = self.compute.create_backend_service_traffic_director(
            name,
            health_check=self.health_check,
            protocol=protocol,
            subset_size=subset_size,
            affinity_header=affinity_header,
            locality_lb_policies=locality_lb_policies,
            outlier_detection=outlier_detection,
        )
        self.backend_service = resource
        self.backend_service_protocol = protocol

    def load_backend_service(self):
        name = self.make_resource_name(self.BACKEND_SERVICE_NAME)
        resource = self.compute.get_backend_service_traffic_director(name)
        self.backend_service = resource

    def delete_backend_service(self, force=False):
        if force:
            name = self.make_resource_name(self.BACKEND_SERVICE_NAME)
        elif self.backend_service:
            name = self.backend_service.name
        else:
            return
        logger.info('Deleting Backend Service "%s"', name)
        self.compute.delete_backend_service(name)
        self.backend_service = None

    def backend_service_add_neg_backends(
        self, name, zones, max_rate_per_endpoint: Optional[int] = None
    ):
        self.backend_service_load_neg_backends(name, zones)
        if not self.backends:
            raise ValueError("Unexpected: no backends were loaded.")
        self.backend_service_patch_backends(max_rate_per_endpoint)

    def backend_service_load_neg_backends(self, name, zones):
        logger.info("Waiting for Network Endpoint Groups to load endpoints.")
        for zone in zones:
            backend = self.compute.wait_for_network_endpoint_group(name, zone)
            logger.info(
                'Loaded NEG "%s" in zone %s', backend.name, backend.zone
            )
            self.backends.add(backend)

    def backend_service_remove_neg_backends(self, name, zones):
        logger.info("Waiting for Network Endpoint Groups to load endpoints.")
        for zone in zones:
            backend = self.compute.wait_for_network_endpoint_group(name, zone)
            logger.info(
                'Loaded NEG "%s" in zone %s', backend.name, backend.zone
            )
            self.backends.remove(backend)
        self.backend_service_patch_backends()

    def backend_service_patch_backends(
        self, max_rate_per_endpoint: Optional[int] = None
    ):
        logging.info(
            "Adding backends to Backend Service %s: %r",
            self.backend_service.name,
            self.backends,
        )
        self.compute.backend_service_patch_backends(
            self.backend_service, self.backends, max_rate_per_endpoint
        )

    def backend_service_remove_all_backends(self):
        logging.info(
            "Removing backends from Backend Service %s",
            self.backend_service.name,
        )
        self.compute.backend_service_remove_all_backends(self.backend_service)

    def wait_for_backends_healthy_status(self):
        logger.info(
            "Waiting for Backend Service %s to report all backends healthy: %r",
            self.backend_service.name,
            [backend.name for backend in self.backends],
        )
        self.compute.wait_for_backends_healthy_status(
            self.backend_service, self.backends
        )

    def create_alternative_backend_service(
        self, protocol: Optional[BackendServiceProtocol] = _BackendGRPC
    ):
        if protocol is None:
            protocol = _BackendGRPC
        name = self.make_resource_name(self.ALTERNATIVE_BACKEND_SERVICE_NAME)
        logger.info(
            'Creating %s Alternative Backend Service "%s"', protocol.name, name
        )
        resource = self.compute.create_backend_service_traffic_director(
            name, health_check=self.health_check, protocol=protocol
        )
        self.alternative_backend_service = resource
        self.alternative_backend_service_protocol = protocol

    def load_alternative_backend_service(self):
        name = self.make_resource_name(self.ALTERNATIVE_BACKEND_SERVICE_NAME)
        resource = self.compute.get_backend_service_traffic_director(name)
        self.alternative_backend_service = resource

    def delete_alternative_backend_service(self, force=False):
        if force:
            name = self.make_resource_name(
                self.ALTERNATIVE_BACKEND_SERVICE_NAME
            )
        elif self.alternative_backend_service:
            name = self.alternative_backend_service.name
        else:
            return
        logger.info('Deleting Alternative Backend Service "%s"', name)
        self.compute.delete_backend_service(name)
        self.alternative_backend_service = None

    def alternative_backend_service_add_neg_backends(self, name, zones):
        logger.info("Waiting for Network Endpoint Groups to load endpoints.")
        for zone in zones:
            backend = self.compute.wait_for_network_endpoint_group(name, zone)
            logger.info(
                'Loaded NEG "%s" in zone %s', backend.name, backend.zone
            )
            self.alternative_backends.add(backend)
        self.alternative_backend_service_patch_backends()

    def alternative_backend_service_patch_backends(self):
        logging.info(
            "Adding backends to Backend Service %s: %r",
            self.alternative_backend_service.name,
            self.alternative_backends,
        )
        self.compute.backend_service_patch_backends(
            self.alternative_backend_service, self.alternative_backends
        )

    def alternative_backend_service_remove_all_backends(self):
        logging.info(
            "Removing backends from Backend Service %s",
            self.alternative_backend_service.name,
        )
        self.compute.backend_service_remove_all_backends(
            self.alternative_backend_service
        )

    def wait_for_alternative_backends_healthy_status(self):
        logger.debug(
            "Waiting for Backend Service %s to report all backends healthy %r",
            self.alternative_backend_service,
            self.alternative_backends,
        )
        self.compute.wait_for_backends_healthy_status(
            self.alternative_backend_service, self.alternative_backends
        )

    def create_affinity_backend_service(
        self, protocol: Optional[BackendServiceProtocol] = _BackendGRPC
    ):
        if protocol is None:
            protocol = _BackendGRPC
        name = self.make_resource_name(self.AFFINITY_BACKEND_SERVICE_NAME)
        logger.info(
            'Creating %s Affinity Backend Service "%s"', protocol.name, name
        )
        resource = self.compute.create_backend_service_traffic_director(
            name,
            health_check=self.health_check,
            protocol=protocol,
            affinity_header=TEST_AFFINITY_METADATA_KEY,
        )
        self.affinity_backend_service = resource
        self.affinity_backend_service_protocol = protocol

    def load_affinity_backend_service(self):
        name = self.make_resource_name(self.AFFINITY_BACKEND_SERVICE_NAME)
        resource = self.compute.get_backend_service_traffic_director(name)
        self.affinity_backend_service = resource

    def delete_affinity_backend_service(self, force=False):
        if force:
            name = self.make_resource_name(self.AFFINITY_BACKEND_SERVICE_NAME)
        elif self.affinity_backend_service:
            name = self.affinity_backend_service.name
        else:
            return
        logger.info('Deleting Affinity Backend Service "%s"', name)
        self.compute.delete_backend_service(name)
        self.affinity_backend_service = None

    def affinity_backend_service_add_neg_backends(self, name, zones):
        logger.info("Waiting for Network Endpoint Groups to load endpoints.")
        for zone in zones:
            backend = self.compute.wait_for_network_endpoint_group(name, zone)
            logger.info(
                'Loaded NEG "%s" in zone %s', backend.name, backend.zone
            )
            self.affinity_backends.add(backend)
        self.affinity_backend_service_patch_backends()

    def affinity_backend_service_patch_backends(self):
        logging.info(
            "Adding backends to Backend Service %s: %r",
            self.affinity_backend_service.name,
            self.affinity_backends,
        )
        self.compute.backend_service_patch_backends(
            self.affinity_backend_service, self.affinity_backends
        )

    def affinity_backend_service_remove_all_backends(self):
        logging.info(
            "Removing backends from Backend Service %s",
            self.affinity_backend_service.name,
        )
        self.compute.backend_service_remove_all_backends(
            self.affinity_backend_service
        )

    def wait_for_affinity_backends_healthy_status(self):
        logger.debug(
            "Waiting for Backend Service %s to report all backends healthy %r",
            self.affinity_backend_service,
            self.affinity_backends,
        )
        self.compute.wait_for_backends_healthy_status(
            self.affinity_backend_service, self.affinity_backends
        )

    @staticmethod
    def _generate_url_map_body(
        name: str,
        matcher_name: str,
        src_hosts,
        dst_default_backend_service: GcpResource,
        dst_host_rule_match_backend_service: Optional[GcpResource] = None,
    ) -> Dict[str, Any]:
        if dst_host_rule_match_backend_service is None:
            dst_host_rule_match_backend_service = dst_default_backend_service
        return {
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
        }

    def create_url_map(self, src_host: str, src_port: int) -> GcpResource:
        src_address = f"{src_host}:{src_port}"
        name = self.make_resource_name(self.URL_MAP_NAME)
        matcher_name = self.make_resource_name(self.URL_MAP_PATH_MATCHER_NAME)
        logger.info(
            'Creating URL map "%s": %s -> %s',
            name,
            src_address,
            self.backend_service.name,
        )
        resource = self.compute.create_url_map_with_content(
            self._generate_url_map_body(
                name, matcher_name, [src_address], self.backend_service
            )
        )
        self.url_map = resource
        return resource

    def patch_url_map(
        self, src_host: str, src_port: int, backend_service: GcpResource
    ):
        src_address = f"{src_host}:{src_port}"
        name = self.make_resource_name(self.URL_MAP_NAME)
        matcher_name = self.make_resource_name(self.URL_MAP_PATH_MATCHER_NAME)
        logger.info(
            'Patching URL map "%s": %s -> %s',
            name,
            src_address,
            backend_service.name,
        )
        self.compute.patch_url_map(
            self.url_map,
            self._generate_url_map_body(
                name, matcher_name, [src_address], backend_service
            ),
        )

    def create_url_map_with_content(self, url_map_body: Any) -> GcpResource:
        logger.info("Creating URL map: %s", url_map_body)
        resource = self.compute.create_url_map_with_content(url_map_body)
        self.url_map = resource
        return resource

    def delete_url_map(self, force=False):
        if force:
            name = self.make_resource_name(self.URL_MAP_NAME)
        elif self.url_map:
            name = self.url_map.name
        else:
            return
        logger.info('Deleting URL Map "%s"', name)
        self.compute.delete_url_map(name)
        self.url_map = None

    def create_alternative_url_map(
        self,
        src_host: str,
        src_port: int,
        backend_service: Optional[GcpResource] = None,
    ) -> GcpResource:
        name = self.make_resource_name(self.ALTERNATIVE_URL_MAP_NAME)
        src_address = f"{src_host}:{src_port}"
        matcher_name = self.make_resource_name(self.URL_MAP_PATH_MATCHER_NAME)
        if backend_service is None:
            backend_service = self.alternative_backend_service
        logger.info(
            'Creating alternative URL map "%s": %s -> %s',
            name,
            src_address,
            backend_service.name,
        )
        resource = self.compute.create_url_map_with_content(
            self._generate_url_map_body(
                name, matcher_name, [src_address], backend_service
            )
        )
        self.alternative_url_map = resource
        return resource

    def delete_alternative_url_map(self, force=False):
        if force:
            name = self.make_resource_name(self.ALTERNATIVE_URL_MAP_NAME)
        elif self.alternative_url_map:
            name = self.alternative_url_map.name
        else:
            return
        logger.info('Deleting alternative URL Map "%s"', name)
        self.compute.delete_url_map(name)
        self.url_map = None

    def create_target_proxy(self):
        name = self.make_resource_name(self.TARGET_PROXY_NAME)
        if self.backend_service_protocol is BackendServiceProtocol.GRPC:
            target_proxy_type = "GRPC"
            create_proxy_fn = self.compute.create_target_grpc_proxy
            self.target_proxy_is_http = False
        elif self.backend_service_protocol is BackendServiceProtocol.HTTP2:
            target_proxy_type = "HTTP"
            create_proxy_fn = self.compute.create_target_http_proxy
            self.target_proxy_is_http = True
        else:
            raise TypeError("Unexpected backend service protocol")

        logger.info(
            'Creating target %s proxy "%s" to URL map %s',
            name,
            target_proxy_type,
            self.url_map.name,
        )
        self.target_proxy = create_proxy_fn(name, self.url_map)

    def delete_target_grpc_proxy(self, force=False):
        if force:
            name = self.make_resource_name(self.TARGET_PROXY_NAME)
        elif self.target_proxy:
            name = self.target_proxy.name
        else:
            return
        logger.info('Deleting Target GRPC proxy "%s"', name)
        self.compute.delete_target_grpc_proxy(name)
        self.target_proxy = None
        self.target_proxy_is_http = False

    def delete_target_http_proxy(self, force=False):
        if force:
            name = self.make_resource_name(self.TARGET_PROXY_NAME)
        elif self.target_proxy and self.target_proxy_is_http:
            name = self.target_proxy.name
        else:
            return
        logger.info('Deleting HTTP Target proxy "%s"', name)
        self.compute.delete_target_http_proxy(name)
        self.target_proxy = None
        self.target_proxy_is_http = False

    def create_alternative_target_proxy(self):
        name = self.make_resource_name(self.ALTERNATIVE_TARGET_PROXY_NAME)
        if self.backend_service_protocol is BackendServiceProtocol.GRPC:
            logger.info(
                'Creating alternative target GRPC proxy "%s" to URL map %s',
                name,
                self.alternative_url_map.name,
            )
            self.alternative_target_proxy = (
                self.compute.create_target_grpc_proxy(
                    name, self.alternative_url_map, False
                )
            )
        else:
            raise TypeError("Unexpected backend service protocol")

    def delete_alternative_target_grpc_proxy(self, force=False):
        if force:
            name = self.make_resource_name(self.ALTERNATIVE_TARGET_PROXY_NAME)
        elif self.alternative_target_proxy:
            name = self.alternative_target_proxy.name
        else:
            return
        logger.info('Deleting alternative Target GRPC proxy "%s"', name)
        self.compute.delete_target_grpc_proxy(name)
        self.alternative_target_proxy = None

    def find_unused_forwarding_rule_port(
        self,
        *,
        lo: int = 1024,  # To avoid confusion, skip well-known ports.
        hi: int = 65535,
        attempts: int = 25,
    ) -> int:
        for _ in range(attempts):
            src_port = random.randint(lo, hi)
            if not self.compute.exists_forwarding_rule(src_port):
                return src_port
        # TODO(sergiitk): custom exception
        raise RuntimeError("Couldn't find unused forwarding rule port")

    def create_forwarding_rule(self, src_port: int):
        name = self.make_resource_name(self.FORWARDING_RULE_NAME)
        src_port = int(src_port)
        logging.info(
            'Creating forwarding rule "%s" in network "%s": 0.0.0.0:%s -> %s',
            name,
            self.network,
            src_port,
            self.target_proxy.url,
        )
        resource = self.compute.create_forwarding_rule(
            name, src_port, self.target_proxy, self.network_url
        )
        self.forwarding_rule = resource
        return resource

    def delete_forwarding_rule(self, force=False):
        if force:
            name = self.make_resource_name(self.FORWARDING_RULE_NAME)
        elif self.forwarding_rule:
            name = self.forwarding_rule.name
        else:
            return
        logger.info('Deleting Forwarding rule "%s"', name)
        self.compute.delete_forwarding_rule(name)
        self.forwarding_rule = None

    def create_alternative_forwarding_rule(
        self, src_port: int, ip_address="0.0.0.0"
    ):
        name = self.make_resource_name(self.ALTERNATIVE_FORWARDING_RULE_NAME)
        src_port = int(src_port)
        logging.info(
            (
                'Creating alternative forwarding rule "%s" in network "%s":'
                " %s:%s -> %s"
            ),
            name,
            self.network,
            ip_address,
            src_port,
            self.alternative_target_proxy.url,
        )
        resource = self.compute.create_forwarding_rule(
            name,
            src_port,
            self.alternative_target_proxy,
            self.network_url,
            ip_address=ip_address,
        )
        self.alternative_forwarding_rule = resource
        return resource

    def delete_alternative_forwarding_rule(self, force=False):
        if force:
            name = self.make_resource_name(
                self.ALTERNATIVE_FORWARDING_RULE_NAME
            )
        elif self.alternative_forwarding_rule:
            name = self.alternative_forwarding_rule.name
        else:
            return
        logger.info('Deleting alternative Forwarding rule "%s"', name)
        self.compute.delete_forwarding_rule(name)
        self.alternative_forwarding_rule = None

    def create_firewall_rule(self, allowed_ports: List[str]):
        name = self.make_resource_name(self.FIREWALL_RULE_NAME)
        logging.info(
            'Creating firewall rule "%s" in network "%s" with allowed ports %s',
            name,
            self.network,
            allowed_ports,
        )
        resource = self.compute.create_firewall_rule(
            name,
            self.network_url,
            xds_flags.FIREWALL_SOURCE_RANGE.value,
            allowed_ports,
        )
        self.firewall_rule = resource

    def delete_firewall_rule(self, force=False):
        """The firewall rule won't be automatically removed."""
        if force:
            name = self.make_resource_name(self.FIREWALL_RULE_NAME)
        elif self.firewall_rule:
            name = self.firewall_rule.name
        else:
            return
        logger.info('Deleting Firewall Rule "%s"', name)
        self.compute.delete_firewall_rule(name)
        self.firewall_rule = None


class TrafficDirectorAppNetManager(TrafficDirectorManager):
    GRPC_ROUTE_NAME = "grpc-route"
    HTTP_ROUTE_NAME = "http-route"
    MESH_NAME = "mesh"

    netsvc: _NetworkServicesV1Alpha1

    def __init__(
        self,
        gcp_api_manager: gcp.api.GcpApiManager,
        project: str,
        *,
        resource_prefix: str,
        resource_suffix: Optional[str] = None,
        network: str = "default",
        compute_api_version: str = "v1",
    ):
        super().__init__(
            gcp_api_manager,
            project,
            resource_prefix=resource_prefix,
            resource_suffix=resource_suffix,
            network=network,
            compute_api_version=compute_api_version,
        )

        # API
        self.netsvc = _NetworkServicesV1Alpha1(gcp_api_manager, project)

        # Managed resources
        # TODO(gnossen) PTAL at the pylint error
        self.grpc_route: Optional[GrpcRoute] = None
        self.http_route: Optional[HttpRoute] = None
        self.mesh: Optional[Mesh] = None

    def create_mesh(self) -> GcpResource:
        name = self.make_resource_name(self.MESH_NAME)
        logger.info("Creating Mesh %s", name)
        body = {}
        resource = self.netsvc.create_mesh(name, body)
        self.mesh = self.netsvc.get_mesh(name)
        logger.debug("Loaded Mesh: %s", self.mesh)
        return resource

    def delete_mesh(self, force=False):
        if force:
            name = self.make_resource_name(self.MESH_NAME)
        elif self.mesh:
            name = self.mesh.name
        else:
            return
        logger.info("Deleting Mesh %s", name)
        self.netsvc.delete_mesh(name)
        self.mesh = None

    def create_grpc_route(self, src_host: str, src_port: int) -> GcpResource:
        host = f"{src_host}:{src_port}"
        service_name = self.netsvc.resource_full_name(
            self.backend_service.name, "backendServices"
        )
        body = {
            "meshes": [self.mesh.url],
            "hostnames": host,
            "rules": [
                {"action": {"destinations": [{"serviceName": service_name}]}}
            ],
        }
        name = self.make_resource_name(self.GRPC_ROUTE_NAME)
        logger.info("Creating GrpcRoute %s", name)
        resource = self.netsvc.create_grpc_route(name, body)
        self.grpc_route = self.netsvc.get_grpc_route(name)
        logger.debug("Loaded GrpcRoute: %s", self.grpc_route)
        return resource

    def create_grpc_route_with_content(self, body: Any) -> GcpResource:
        name = self.make_resource_name(self.GRPC_ROUTE_NAME)
        logger.info("Creating GrpcRoute %s", name)
        resource = self.netsvc.create_grpc_route(name, body)
        self.grpc_route = self.netsvc.get_grpc_route(name)
        logger.debug("Loaded GrpcRoute: %s", self.grpc_route)
        return resource

    def create_http_route_with_content(self, body: Any) -> GcpResource:
        name = self.make_resource_name(self.HTTP_ROUTE_NAME)
        logger.info("Creating HttpRoute %s", name)
        resource = self.netsvc.create_http_route(name, body)
        self.http_route = self.netsvc.get_http_route(name)
        logger.debug("Loaded HttpRoute: %s", self.http_route)
        return resource

    def delete_grpc_route(self, force=False):
        if force:
            name = self.make_resource_name(self.GRPC_ROUTE_NAME)
        elif self.grpc_route:
            name = self.grpc_route.name
        else:
            return
        logger.info("Deleting GrpcRoute %s", name)
        self.netsvc.delete_grpc_route(name)
        self.grpc_route = None

    def delete_http_route(self, force=False):
        if force:
            name = self.make_resource_name(self.HTTP_ROUTE_NAME)
        elif self.http_route:
            name = self.http_route.name
        else:
            return
        logger.info("Deleting HttpRoute %s", name)
        self.netsvc.delete_http_route(name)
        self.http_route = None

    def cleanup(self, *, force=False):
        self.delete_http_route(force=force)
        self.delete_grpc_route(force=force)
        self.delete_mesh(force=force)
        super().cleanup(force=force)


class TrafficDirectorSecureManager(TrafficDirectorManager):
    SERVER_TLS_POLICY_NAME = "server-tls-policy"
    CLIENT_TLS_POLICY_NAME = "client-tls-policy"
    AUTHZ_POLICY_NAME = "authz-policy"
    ENDPOINT_POLICY = "endpoint-policy"
    CERTIFICATE_PROVIDER_INSTANCE = "google_cloud_private_spiffe"

    netsec: _NetworkSecurityV1Beta1
    netsvc: _NetworkServicesV1Beta1

    def __init__(
        self,
        gcp_api_manager: gcp.api.GcpApiManager,
        project: str,
        *,
        resource_prefix: str,
        resource_suffix: Optional[str] = None,
        network: str = "default",
        compute_api_version: str = "v1",
    ):
        super().__init__(
            gcp_api_manager,
            project,
            resource_prefix=resource_prefix,
            resource_suffix=resource_suffix,
            network=network,
            compute_api_version=compute_api_version,
        )

        # API
        self.netsec = _NetworkSecurityV1Beta1(gcp_api_manager, project)
        self.netsvc = _NetworkServicesV1Beta1(gcp_api_manager, project)

        # Managed resources
        self.server_tls_policy: Optional[ServerTlsPolicy] = None
        self.client_tls_policy: Optional[ClientTlsPolicy] = None
        self.authz_policy: Optional[AuthorizationPolicy] = None
        self.endpoint_policy: Optional[EndpointPolicy] = None

    def setup_server_security(
        self, *, server_namespace, server_name, server_port, tls=True, mtls=True
    ):
        self.create_server_tls_policy(tls=tls, mtls=mtls)
        self.create_endpoint_policy(
            server_namespace=server_namespace,
            server_name=server_name,
            server_port=server_port,
        )

    def setup_client_security(
        self, *, server_namespace, server_name, tls=True, mtls=True
    ):
        self.create_client_tls_policy(tls=tls, mtls=mtls)
        self.backend_service_apply_client_mtls_policy(
            server_namespace, server_name
        )

    def cleanup(self, *, force=False):
        # Cleanup in the reverse order of creation
        super().cleanup(force=force)
        self.delete_endpoint_policy(force=force)
        self.delete_server_tls_policy(force=force)
        self.delete_client_tls_policy(force=force)
        self.delete_authz_policy(force=force)

    def create_server_tls_policy(self, *, tls, mtls):
        name = self.make_resource_name(self.SERVER_TLS_POLICY_NAME)
        logger.info("Creating Server TLS Policy %s", name)
        if not tls and not mtls:
            logger.warning(
                (
                    "Server TLS Policy %s neither TLS, nor mTLS "
                    "policy. Skipping creation"
                ),
                name,
            )
            return

        certificate_provider = self._get_certificate_provider()
        policy = {}
        if tls:
            policy["serverCertificate"] = certificate_provider
        if mtls:
            policy["mtlsPolicy"] = {
                "clientValidationCa": [certificate_provider],
            }

        self.netsec.create_server_tls_policy(name, policy)
        self.server_tls_policy = self.netsec.get_server_tls_policy(name)
        logger.debug("Server TLS Policy loaded: %r", self.server_tls_policy)

    def delete_server_tls_policy(self, force=False):
        if force:
            name = self.make_resource_name(self.SERVER_TLS_POLICY_NAME)
        elif self.server_tls_policy:
            name = self.server_tls_policy.name
        else:
            return
        logger.info("Deleting Server TLS Policy %s", name)
        self.netsec.delete_server_tls_policy(name)
        self.server_tls_policy = None

    def create_authz_policy(self, *, action: str, rules: list):
        name = self.make_resource_name(self.AUTHZ_POLICY_NAME)
        logger.info("Creating Authz Policy %s", name)
        policy = {
            "action": action,
            "rules": rules,
        }

        self.netsec.create_authz_policy(name, policy)
        self.authz_policy = self.netsec.get_authz_policy(name)
        logger.debug("Authz Policy loaded: %r", self.authz_policy)

    def delete_authz_policy(self, force=False):
        if force:
            name = self.make_resource_name(self.AUTHZ_POLICY_NAME)
        elif self.authz_policy:
            name = self.authz_policy.name
        else:
            return
        logger.info("Deleting Authz Policy %s", name)
        self.netsec.delete_authz_policy(name)
        self.authz_policy = None

    def create_endpoint_policy(
        self, *, server_namespace: str, server_name: str, server_port: int
    ) -> None:
        name = self.make_resource_name(self.ENDPOINT_POLICY)
        logger.info("Creating Endpoint Policy %s", name)
        endpoint_matcher_labels = [
            {
                "labelName": "app",
                "labelValue": f"{server_namespace}-{server_name}",
            }
        ]
        port_selector = {"ports": [str(server_port)]}
        label_matcher_all = {
            "metadataLabelMatchCriteria": "MATCH_ALL",
            "metadataLabels": endpoint_matcher_labels,
        }
        config = {
            "type": "GRPC_SERVER",
            "trafficPortSelector": port_selector,
            "endpointMatcher": {
                "metadataLabelMatcher": label_matcher_all,
            },
        }
        if self.server_tls_policy:
            config["serverTlsPolicy"] = self.server_tls_policy.name
        else:
            logger.warning(
                (
                    "Creating Endpoint Policy %s with "
                    "no Server TLS policy attached"
                ),
                name,
            )
        if self.authz_policy:
            config["authorizationPolicy"] = self.authz_policy.name

        self.netsvc.create_endpoint_policy(name, config)
        self.endpoint_policy = self.netsvc.get_endpoint_policy(name)
        logger.debug("Loaded Endpoint Policy: %r", self.endpoint_policy)

    def delete_endpoint_policy(self, force: bool = False) -> None:
        if force:
            name = self.make_resource_name(self.ENDPOINT_POLICY)
        elif self.endpoint_policy:
            name = self.endpoint_policy.name
        else:
            return
        logger.info("Deleting Endpoint Policy %s", name)
        self.netsvc.delete_endpoint_policy(name)
        self.endpoint_policy = None

    def create_client_tls_policy(self, *, tls, mtls):
        name = self.make_resource_name(self.CLIENT_TLS_POLICY_NAME)
        logger.info("Creating Client TLS Policy %s", name)
        if not tls and not mtls:
            logger.warning(
                (
                    "Client TLS Policy %s neither TLS, nor mTLS "
                    "policy. Skipping creation"
                ),
                name,
            )
            return

        certificate_provider = self._get_certificate_provider()
        policy = {}
        if tls:
            policy["serverValidationCa"] = [certificate_provider]
        if mtls:
            policy["clientCertificate"] = certificate_provider

        self.netsec.create_client_tls_policy(name, policy)
        self.client_tls_policy = self.netsec.get_client_tls_policy(name)
        logger.debug("Client TLS Policy loaded: %r", self.client_tls_policy)

    def delete_client_tls_policy(self, force=False):
        if force:
            name = self.make_resource_name(self.CLIENT_TLS_POLICY_NAME)
        elif self.client_tls_policy:
            name = self.client_tls_policy.name
        else:
            return
        logger.info("Deleting Client TLS Policy %s", name)
        self.netsec.delete_client_tls_policy(name)
        self.client_tls_policy = None

    def backend_service_apply_client_mtls_policy(
        self,
        server_namespace,
        server_name,
    ):
        if not self.client_tls_policy:
            logger.warning(
                (
                    "Client TLS policy not created, "
                    "skipping attaching to Backend Service %s"
                ),
                self.backend_service.name,
            )
            return

        server_spiffe = (
            f"spiffe://{self.project}.svc.id.goog/"
            f"ns/{server_namespace}/sa/{server_name}"
        )
        logging.info(
            "Adding Client TLS Policy to Backend Service %s: %s, server %s",
            self.backend_service.name,
            self.client_tls_policy.url,
            server_spiffe,
        )

        self.compute.patch_backend_service(
            self.backend_service,
            {
                "securitySettings": {
                    "clientTlsPolicy": self.client_tls_policy.url,
                    "subjectAltNames": [server_spiffe],
                }
            },
        )

    @classmethod
    def _get_certificate_provider(cls):
        return {
            "certificateProviderInstance": {
                "pluginInstance": cls.CERTIFICATE_PROVIDER_INSTANCE,
            },
        }
