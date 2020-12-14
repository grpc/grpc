# Copyright 2016 gRPC authors.
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
import logging
from typing import Optional, Set

from framework.infrastructure import gcp

logger = logging.getLogger(__name__)

# Type aliases
# Compute
ComputeV1 = gcp.compute.ComputeV1
HealthCheckProtocol = ComputeV1.HealthCheckProtocol
BackendServiceProtocol = ComputeV1.BackendServiceProtocol
GcpResource = ComputeV1.GcpResource
ZonalGcpResource = ComputeV1.ZonalGcpResource

# Network Security
NetworkSecurityV1Alpha1 = gcp.network_security.NetworkSecurityV1Alpha1
ServerTlsPolicy = NetworkSecurityV1Alpha1.ServerTlsPolicy
ClientTlsPolicy = NetworkSecurityV1Alpha1.ClientTlsPolicy

# Network Services
NetworkServicesV1Alpha1 = gcp.network_services.NetworkServicesV1Alpha1
EndpointConfigSelector = NetworkServicesV1Alpha1.EndpointConfigSelector


class TrafficDirectorManager:
    compute: ComputeV1
    BACKEND_SERVICE_NAME = "backend-service"
    HEALTH_CHECK_NAME = "health-check"
    URL_MAP_NAME = "url-map"
    URL_MAP_PATH_MATCHER_NAME = "path-matcher"
    TARGET_PROXY_NAME = "target-proxy"
    FORWARDING_RULE_NAME = "forwarding-rule"

    def __init__(
            self,
            gcp_api_manager: gcp.api.GcpApiManager,
            project: str,
            *,
            resource_prefix: str,
            network: str = 'default',
    ):
        # API
        self.compute = ComputeV1(gcp_api_manager, project)

        # Settings
        self.project: str = project
        self.network: str = network
        self.resource_prefix: str = resource_prefix

        # Managed resources
        self.health_check: Optional[GcpResource] = None
        self.backend_service: Optional[GcpResource] = None
        self.url_map: Optional[GcpResource] = None
        self.target_proxy: Optional[GcpResource] = None
        # todo(sergiitk): fix
        self.target_proxy_is_http: bool = False
        self.forwarding_rule: Optional[GcpResource] = None
        self.backends: Set[ZonalGcpResource] = set()

    @property
    def network_url(self):
        return f'global/networks/{self.network}'

    def setup_for_grpc(self,
                       service_host,
                       service_port,
                       *,
                       backend_protocol=BackendServiceProtocol.GRPC):
        self.create_health_check()
        self.create_backend_service(protocol=backend_protocol)
        self.create_url_map(service_host, service_port)
        if backend_protocol is BackendServiceProtocol.GRPC:
            self.create_target_grpc_proxy()
        else:
            self.create_target_http_proxy()
        self.create_forwarding_rule(service_port)

    def cleanup(self, *, force=False):
        # Cleanup in the reverse order of creation
        self.delete_forwarding_rule(force=force)
        if self.target_proxy_is_http:
            self.delete_target_http_proxy(force=force)
        else:
            self.delete_target_grpc_proxy(force=force)
        self.delete_url_map(force=force)
        self.delete_backend_service(force=force)
        self.delete_health_check(force=force)

    def _ns_name(self, name):
        return f'{self.resource_prefix}-{name}'

    def create_health_check(self, protocol=HealthCheckProtocol.TCP):
        if self.health_check:
            raise ValueError('Health check %s already created, delete it first',
                             self.health_check.name)
        name = self._ns_name(self.HEALTH_CHECK_NAME)
        logger.info('Creating %s Health Check %s', protocol.name, name)
        if protocol is HealthCheckProtocol.TCP:
            resource = self.compute.create_health_check_tcp(
                name, use_serving_port=True)
        else:
            raise ValueError('Unexpected protocol')
        self.health_check = resource

    def delete_health_check(self, force=False):
        if force:
            name = self._ns_name(self.HEALTH_CHECK_NAME)
        elif self.health_check:
            name = self.health_check.name
        else:
            return
        logger.info('Deleting Health Check %s', name)
        self.compute.delete_health_check(name)
        self.health_check = None

    def create_backend_service(
            self,
            protocol: BackendServiceProtocol = BackendServiceProtocol.GRPC):
        name = self._ns_name(self.BACKEND_SERVICE_NAME)
        logger.info('Creating %s Backend Service %s', protocol.name, name)
        resource = self.compute.create_backend_service_traffic_director(
            name, health_check=self.health_check, protocol=protocol)
        self.backend_service = resource

    def load_backend_service(self):
        name = self._ns_name(self.BACKEND_SERVICE_NAME)
        resource = self.compute.get_backend_service_traffic_director(name)
        self.backend_service = resource

    def delete_backend_service(self, force=False):
        if force:
            name = self._ns_name(self.BACKEND_SERVICE_NAME)
        elif self.backend_service:
            name = self.backend_service.name
        else:
            return
        logger.info('Deleting Backend Service %s', name)
        self.compute.delete_backend_service(name)
        self.backend_service = None

    def backend_service_add_neg_backends(self, name, zones):
        logger.info('Loading NEGs')
        for zone in zones:
            backend = self.compute.wait_for_network_endpoint_group(name, zone)
            logger.info('Loaded NEG %s in zone %s', backend.name, backend.zone)
            self.backends.add(backend)

        self.backend_service_add_backends()
        self.wait_for_backends_healthy_status()

    def backend_service_add_backends(self):
        logging.info('Adding backends to Backend Service %s: %r',
                     self.backend_service.name, self.backends)
        self.compute.backend_service_add_backends(self.backend_service,
                                                  self.backends)

    def backend_service_remove_all_backends(self):
        logging.info('Removing backends from Backend Service %s',
                     self.backend_service.name)
        self.compute.backend_service_remove_all_backends(self.backend_service)

    def wait_for_backends_healthy_status(self):
        logger.debug(
            "Waiting for Backend Service %s to report all backends healthy %r",
            self.backend_service, self.backends)
        self.compute.wait_for_backends_healthy_status(self.backend_service,
                                                      self.backends)

    def create_url_map(
            self,
            src_host: str,
            src_port: int,
    ) -> GcpResource:
        src_address = f'{src_host}:{src_port}'
        name = self._ns_name(self.URL_MAP_NAME)
        matcher_name = self._ns_name(self.URL_MAP_PATH_MATCHER_NAME)
        logger.info('Creating URL map %s %s -> %s', name, src_address,
                    self.backend_service.name)
        resource = self.compute.create_url_map(name, matcher_name,
                                               [src_address],
                                               self.backend_service)
        self.url_map = resource
        return resource

    def delete_url_map(self, force=False):
        if force:
            name = self._ns_name(self.URL_MAP_NAME)
        elif self.url_map:
            name = self.url_map.name
        else:
            return
        logger.info('Deleting URL Map %s', name)
        self.compute.delete_url_map(name)
        self.url_map = None

    def create_target_grpc_proxy(self):
        # todo: different kinds
        name = self._ns_name(self.TARGET_PROXY_NAME)
        logger.info('Creating target GRPC proxy %s to url map %s', name,
                    self.url_map.name)
        resource = self.compute.create_target_grpc_proxy(name, self.url_map)
        self.target_proxy = resource

    def delete_target_grpc_proxy(self, force=False):
        if force:
            name = self._ns_name(self.TARGET_PROXY_NAME)
        elif self.target_proxy:
            name = self.target_proxy.name
        else:
            return
        logger.info('Deleting Target GRPC proxy %s', name)
        self.compute.delete_target_grpc_proxy(name)
        self.target_proxy = None
        self.target_proxy_is_http = False

    def create_target_http_proxy(self):
        # todo: different kinds
        name = self._ns_name(self.TARGET_PROXY_NAME)
        logger.info('Creating target HTTP proxy %s to url map %s', name,
                    self.url_map.name)
        resource = self.compute.create_target_http_proxy(name, self.url_map)
        self.target_proxy = resource
        self.target_proxy_is_http = True

    def delete_target_http_proxy(self, force=False):
        if force:
            name = self._ns_name(self.TARGET_PROXY_NAME)
        elif self.target_proxy:
            name = self.target_proxy.name
        else:
            return
        logger.info('Deleting HTTP Target proxy %s', name)
        self.compute.delete_target_http_proxy(name)
        self.target_proxy = None
        self.target_proxy_is_http = False

    def create_forwarding_rule(self, src_port: int):
        name = self._ns_name(self.FORWARDING_RULE_NAME)
        src_port = int(src_port)
        logging.info('Creating forwarding rule %s 0.0.0.0:%s -> %s in %s', name,
                     src_port, self.target_proxy.url, self.network)
        resource = self.compute.create_forwarding_rule(name, src_port,
                                                       self.target_proxy,
                                                       self.network_url)
        self.forwarding_rule = resource
        return resource

    def delete_forwarding_rule(self, force=False):
        if force:
            name = self._ns_name(self.FORWARDING_RULE_NAME)
        elif self.forwarding_rule:
            name = self.forwarding_rule.name
        else:
            return
        logger.info('Deleting Forwarding rule %s', name)
        self.compute.delete_forwarding_rule(name)
        self.forwarding_rule = None


class TrafficDirectorSecureManager(TrafficDirectorManager):
    netsec: Optional[NetworkSecurityV1Alpha1]
    SERVER_TLS_POLICY_NAME = "server-tls-policy"
    CLIENT_TLS_POLICY_NAME = "client-tls-policy"
    ENDPOINT_CONFIG_SELECTOR_NAME = "endpoint-config-selector"
    GRPC_ENDPOINT_TARGET_URI = "unix:/var/cert/node-agent.0"

    def __init__(
            self,
            gcp_api_manager: gcp.api.GcpApiManager,
            project: str,
            *,
            resource_prefix: str,
            network: str = 'default',
    ):
        super().__init__(gcp_api_manager,
                         project,
                         resource_prefix=resource_prefix,
                         network=network)

        # API
        self.netsec = NetworkSecurityV1Alpha1(gcp_api_manager, project)
        self.netsvc = NetworkServicesV1Alpha1(gcp_api_manager, project)

        # Managed resources
        self.server_tls_policy: Optional[ServerTlsPolicy] = None
        self.ecs: Optional[EndpointConfigSelector] = None
        self.client_tls_policy: Optional[ClientTlsPolicy] = None

    def setup_for_grpc(self,
                       service_host,
                       service_port,
                       *,
                       backend_protocol=BackendServiceProtocol.HTTP2):
        super().setup_for_grpc(service_host,
                               service_port,
                               backend_protocol=backend_protocol)

    def setup_server_security(self, server_port, *, tls, mtls):
        self.create_server_tls_policy(tls=tls, mtls=mtls)
        self.create_endpoint_config_selector(server_port)

    def setup_client_security(self,
                              server_namespace,
                              server_name,
                              *,
                              tls=True,
                              mtls=True):
        self.create_client_tls_policy(tls=tls, mtls=mtls)
        self.backend_service_apply_client_mtls_policy(server_namespace,
                                                      server_name)

    def cleanup(self, *, force=False):
        # Cleanup in the reverse order of creation
        # todo(sergiitk): todo: fix
        self.target_proxy_is_http = True
        super().cleanup(force=force)
        self.delete_endpoint_config_selector(force=force)
        self.delete_server_tls_policy(force=force)
        self.delete_client_tls_policy(force=force)

    def create_server_tls_policy(self, *, tls, mtls):
        name = self._ns_name(self.SERVER_TLS_POLICY_NAME)
        logger.info('Creating Server TLS Policy %s', name)
        if not tls and not mtls:
            logger.warning(
                'Server TLS Policy %s neither TLS, nor mTLS '
                'policy. Skipping creation', name)
            return

        grpc_endpoint = {
            "grpcEndpoint": {
                "targetUri": self.GRPC_ENDPOINT_TARGET_URI
            }
        }

        policy = {}
        if tls:
            policy["serverCertificate"] = grpc_endpoint
        if mtls:
            policy["mtlsPolicy"] = {"clientValidationCa": [grpc_endpoint]}

        self.netsec.create_server_tls_policy(name, policy)
        self.server_tls_policy = self.netsec.get_server_tls_policy(name)
        logger.debug('Server TLS Policy loaded: %r', self.server_tls_policy)

    def delete_server_tls_policy(self, force=False):
        if force:
            name = self._ns_name(self.SERVER_TLS_POLICY_NAME)
        elif self.server_tls_policy:
            name = self.server_tls_policy.name
        else:
            return
        logger.info('Deleting Server TLS Policy %s', name)
        self.netsec.delete_server_tls_policy(name)
        self.server_tls_policy = None

    def create_endpoint_config_selector(self, server_port):
        name = self._ns_name(self.ENDPOINT_CONFIG_SELECTOR_NAME)
        logger.info('Creating Endpoint Config Selector %s', name)

        # todo(sergiitk): user server config value
        endpoint_matcher_labels = [{
            "labelName": "version",
            "labelValue": "production"
        }]
        port_selector = {"ports": [str(server_port)]}

        label_matcher_all = {
            "metadataLabelMatchCriteria": "MATCH_ALL",
            "metadataLabels": endpoint_matcher_labels
        }
        config = {
            "type": "SIDECAR_PROXY",
            "httpFilters": {},
            "trafficPortSelector": port_selector,
            "endpointMatcher": {
                "metadataLabelMatcher": label_matcher_all
            },
        }
        if self.server_tls_policy:
            config["serverTlsPolicy"] = self.server_tls_policy.name
        else:
            logger.warning(
                'Creating Endpoint Config Selector %s with '
                'no Server TLS policy attached', name)

        self.netsvc.create_endpoint_config_selector(name, config)
        self.ecs = self.netsvc.get_endpoint_config_selector(name)
        logger.debug('Loaded Endpoint Config Selector: %r', self.ecs)

    def delete_endpoint_config_selector(self, force=False):
        if force:
            name = self._ns_name(self.ENDPOINT_CONFIG_SELECTOR_NAME)
        elif self.ecs:
            name = self.ecs.name
        else:
            return
        logger.info('Deleting Endpoint Config Selector %s', name)
        self.netsvc.delete_endpoint_config_selector(name)
        self.ecs = None

    def create_client_tls_policy(self, *, tls, mtls):
        name = self._ns_name(self.CLIENT_TLS_POLICY_NAME)
        logger.info('Creating Client TLS Policy %s', name)
        if not tls and not mtls:
            logger.warning(
                'Client TLS Policy %s neither TLS, nor mTLS '
                'policy. Skipping creation', name)
            return

        grpc_endpoint = {
            "grpcEndpoint": {
                "targetUri": self.GRPC_ENDPOINT_TARGET_URI
            }
        }

        policy = {}
        if tls:
            policy["serverValidationCa"] = [grpc_endpoint]
        if mtls:
            policy["clientCertificate"] = grpc_endpoint

        self.netsec.create_client_tls_policy(name, policy)
        self.client_tls_policy = self.netsec.get_client_tls_policy(name)
        logger.debug('Client TLS Policy loaded: %r', self.client_tls_policy)

    def delete_client_tls_policy(self, force=False):
        if force:
            name = self._ns_name(self.CLIENT_TLS_POLICY_NAME)
        elif self.client_tls_policy:
            name = self.client_tls_policy.name
        else:
            return
        logger.info('Deleting Client TLS Policy %s', name)
        self.netsec.delete_client_tls_policy(name)
        self.client_tls_policy = None

    def backend_service_apply_client_mtls_policy(
            self,
            server_namespace,
            server_name,
    ):
        if not self.client_tls_policy:
            logger.warning(
                'Client TLS policy not created, '
                'skipping attaching to Backend Service %s',
                self.backend_service.name)
            return

        server_spiffe = (f'spiffe://{self.project}.svc.id.goog/'
                         f'ns/{server_namespace}/sa/{server_name}')
        logging.info(
            'Adding Client TLS Policy to Backend Service %s: %s, '
            'server %s', self.backend_service.name, self.client_tls_policy.url,
            server_spiffe)

        self.compute.patch_backend_service(
            self.backend_service, {
                'securitySettings': {
                    'clientTlsPolicy': self.client_tls_policy.url,
                    'subjectAltNames': [server_spiffe]
                }
            })
