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
"""
xDS Test Server.

TODO(sergiitk): separate XdsTestServer and KubernetesServerRunner to individual
modules.
"""
import functools
import logging
import threading
from typing import Iterator, List, Optional

from framework.infrastructure import gcp
from framework.infrastructure import k8s
import framework.rpc
from framework.rpc import grpc_channelz
from framework.rpc import grpc_testing
from framework.test_app import base_runner

logger = logging.getLogger(__name__)

# Type aliases
_ChannelzServiceClient = grpc_channelz.ChannelzServiceClient
_XdsUpdateHealthServiceClient = grpc_testing.XdsUpdateHealthServiceClient
_HealthClient = grpc_testing.HealthClient


class XdsTestServer(framework.rpc.grpc.GrpcApp):
    """
    Represents RPC services implemented in Server component of the xDS test app.
    https://github.com/grpc/grpc/blob/master/doc/xds-test-descriptions.md#server
    """

    def __init__(self,
                 *,
                 ip: str,
                 rpc_port: int,
                 maintenance_port: Optional[int] = None,
                 secure_mode: Optional[bool] = False,
                 server_id: Optional[str] = None,
                 xds_host: Optional[str] = None,
                 xds_port: Optional[int] = None,
                 rpc_host: Optional[str] = None,
                 pod_name: Optional[str] = None):
        super().__init__(rpc_host=(rpc_host or ip))
        self.ip = ip
        self.rpc_port = rpc_port
        self.maintenance_port = maintenance_port or rpc_port
        self.secure_mode = secure_mode
        self.server_id = server_id
        self.xds_host, self.xds_port = xds_host, xds_port
        self.pod_name = pod_name

    @property
    @functools.lru_cache(None)
    def channelz(self) -> _ChannelzServiceClient:
        return _ChannelzServiceClient(self._make_channel(self.maintenance_port))

    @property
    @functools.lru_cache(None)
    def update_health_service_client(self) -> _XdsUpdateHealthServiceClient:
        return _XdsUpdateHealthServiceClient(
            self._make_channel(self.maintenance_port))

    @property
    @functools.lru_cache(None)
    def health_client(self) -> _HealthClient:
        return _HealthClient(self._make_channel(self.maintenance_port))

    def set_serving(self):
        logger.info('Setting health status to serving')
        self.update_health_service_client.set_serving()
        logger.info('Server reports %s', self.health_client.check_health())

    def set_not_serving(self):
        logger.info('Setting health status to not serving')
        self.update_health_service_client.set_not_serving()
        logger.info('Server reports %s', self.health_client.check_health())

    def set_xds_address(self, xds_host, xds_port: Optional[int] = None):
        self.xds_host, self.xds_port = xds_host, xds_port

    @property
    def xds_address(self) -> str:
        if not self.xds_host:
            return ''
        if not self.xds_port:
            return self.xds_host
        return f'{self.xds_host}:{self.xds_port}'

    @property
    def xds_uri(self) -> str:
        if not self.xds_host:
            return ''
        return f'xds:///{self.xds_address}'

    def get_test_server(self) -> grpc_channelz.Server:
        """Return channelz representation of a server running TestService.

        Raises:
            GrpcApp.NotFound: Test server not found.
        """
        server = self.channelz.find_server_listening_on_port(self.rpc_port)
        if not server:
            raise self.NotFound(
                f'Server listening on port {self.rpc_port} not found')
        return server

    def get_test_server_sockets(self) -> Iterator[grpc_channelz.Socket]:
        """List all sockets of the test server.

        Raises:
            GrpcApp.NotFound: Test server not found.
        """
        server = self.get_test_server()
        return self.channelz.list_server_sockets(server)

    def get_server_socket_matching_client(self,
                                          client_socket: grpc_channelz.Socket):
        """Find test server socket that matches given test client socket.

        Sockets are matched using TCP endpoints (ip:port), further on "address".
        Server socket remote address matched with client socket local address.

         Raises:
             GrpcApp.NotFound: Server socket matching client socket not found.
         """
        client_local = self.channelz.sock_address_to_str(client_socket.local)
        logger.debug('Looking for a server socket connected to the client %s',
                     client_local)

        server_socket = self.channelz.find_server_socket_matching_client(
            self.get_test_server_sockets(), client_socket)
        if not server_socket:
            raise self.NotFound(
                f'Server socket to client {client_local} not found')

        logger.info('Found matching socket pair: server(%s) <-> client(%s)',
                    self.channelz.sock_addresses_pretty(server_socket),
                    self.channelz.sock_addresses_pretty(client_socket))
        return server_socket


class KubernetesServerRunner(base_runner.KubernetesBaseRunner):
    DEFAULT_TEST_PORT = 8080
    DEFAULT_MAINTENANCE_PORT = 8080
    DEFAULT_SECURE_MODE_MAINTENANCE_PORT = 8081

    def __init__(self,
                 k8s_namespace,
                 *,
                 deployment_name,
                 image_name,
                 td_bootstrap_image,
                 gcp_api_manager: gcp.api.GcpApiManager,
                 gcp_project: str,
                 gcp_service_account: str,
                 service_account_name=None,
                 service_name=None,
                 neg_name=None,
                 xds_server_uri=None,
                 network='default',
                 deployment_template='server.deployment.yaml',
                 service_account_template='service-account.yaml',
                 service_template='server.service.yaml',
                 reuse_service=False,
                 reuse_namespace=False,
                 namespace_template=None,
                 debug_use_port_forwarding=False,
                 enable_workload_identity=True):
        super().__init__(k8s_namespace, namespace_template, reuse_namespace)

        # Settings
        self.deployment_name = deployment_name
        self.image_name = image_name
        self.service_name = service_name or deployment_name
        # xDS bootstrap generator
        self.td_bootstrap_image = td_bootstrap_image
        self.xds_server_uri = xds_server_uri
        # This only works in k8s >= 1.18.10-gke.600
        # https://cloud.google.com/kubernetes-engine/docs/how-to/standalone-neg#naming_negs
        self.neg_name = neg_name or (f'{self.k8s_namespace.name}-'
                                     f'{self.service_name}')
        self.network = network
        self.deployment_template = deployment_template
        self.service_template = service_template
        self.reuse_service = reuse_service
        self.debug_use_port_forwarding = debug_use_port_forwarding
        self.enable_workload_identity = enable_workload_identity
        # Service account settings:
        # Kubernetes service account
        if self.enable_workload_identity:
            self.service_account_name = service_account_name or deployment_name
            self.service_account_template = service_account_template
        else:
            self.service_account_name = None
            self.service_account_template = None

        # GCP.
        self.gcp_project = gcp_project
        self.gcp_ui_url = gcp_api_manager.gcp_ui_url
        # GCP service account to map to Kubernetes service account
        self.gcp_service_account = gcp_service_account
        # GCP IAM API used to grant allow workload service accounts permission
        # to use GCP service account identity.
        self.gcp_iam = gcp.iam.IamV1(gcp_api_manager, gcp_project)

        # Mutable state
        self.deployment: Optional[k8s.V1Deployment] = None
        self.service_account: Optional[k8s.V1ServiceAccount] = None
        self.service: Optional[k8s.V1Service] = None
        self.port_forwarders: List[k8s.PortForwarder] = []

    def run(self,
            *,
            test_port=DEFAULT_TEST_PORT,
            maintenance_port=None,
            secure_mode=False,
            server_id=None,
            replica_count=1) -> List[XdsTestServer]:
        # Implementation detail: in secure mode, maintenance ("backchannel")
        # port must be different from the test port so communication with
        # maintenance services can be reached independently from the security
        # configuration under test.
        if maintenance_port is None:
            if not secure_mode:
                maintenance_port = self.DEFAULT_MAINTENANCE_PORT
            else:
                maintenance_port = self.DEFAULT_SECURE_MODE_MAINTENANCE_PORT

        if secure_mode and maintenance_port == test_port:
            raise ValueError('port and maintenance_port must be different '
                             'when running test server in secure mode')
        # To avoid bugs with comparing wrong types.
        if not (isinstance(test_port, int) and
                isinstance(maintenance_port, int)):
            raise TypeError('Port numbers must be integer')

        if secure_mode and not self.enable_workload_identity:
            raise ValueError('Secure mode requires Workload Identity enabled.')

        logger.info(
            'Deploying xDS test server "%s" to k8s namespace %s: test_port=%s '
            'maintenance_port=%s secure_mode=%s server_id=%s replica_count=%s',
            self.deployment_name, self.k8s_namespace.name, test_port,
            maintenance_port, secure_mode, server_id, replica_count)
        self._logs_explorer_link(deployment_name=self.deployment_name,
                                 namespace_name=self.k8s_namespace.name,
                                 gcp_project=self.gcp_project,
                                 gcp_ui_url=self.gcp_ui_url)

        # Create namespace.
        super().run()

        # Reuse existing if requested, create a new deployment when missing.
        # Useful for debugging to avoid NEG loosing relation to deleted service.
        if self.reuse_service:
            self.service = self._reuse_service(self.service_name)
        if not self.service:
            self.service = self._create_service(
                self.service_template,
                service_name=self.service_name,
                namespace_name=self.k8s_namespace.name,
                deployment_name=self.deployment_name,
                neg_name=self.neg_name,
                test_port=test_port)
        self._wait_service_neg(self.service_name, test_port)

        if self.enable_workload_identity:
            # Allow Kubernetes service account to use the GCP service account
            # identity.
            self._grant_workload_identity_user(
                gcp_iam=self.gcp_iam,
                gcp_service_account=self.gcp_service_account,
                service_account_name=self.service_account_name)

            # Create service account
            self.service_account = self._create_service_account(
                self.service_account_template,
                service_account_name=self.service_account_name,
                namespace_name=self.k8s_namespace.name,
                gcp_service_account=self.gcp_service_account)

        # Always create a new deployment
        self.deployment = self._create_deployment(
            self.deployment_template,
            deployment_name=self.deployment_name,
            image_name=self.image_name,
            namespace_name=self.k8s_namespace.name,
            service_account_name=self.service_account_name,
            td_bootstrap_image=self.td_bootstrap_image,
            xds_server_uri=self.xds_server_uri,
            network=self.network,
            replica_count=replica_count,
            test_port=test_port,
            maintenance_port=maintenance_port,
            server_id=server_id,
            secure_mode=secure_mode)

        self._wait_deployment_with_available_replicas(self.deployment_name,
                                                      replica_count)

        # Wait for pods running
        pods = self.k8s_namespace.list_deployment_pods(self.deployment)

        servers = []
        for pod in pods:
            pod_name = pod.metadata.name
            self._wait_pod_started(pod_name)

            pod_ip = pod.status.pod_ip
            rpc_host = None
            # Experimental, for local debugging.
            local_port = maintenance_port
            if self.debug_use_port_forwarding:
                logger.info('LOCAL DEV MODE: Enabling port forwarding to %s:%s',
                            pod_ip, maintenance_port)
                port_forwarder = self.k8s_namespace.port_forward_pod(
                    pod, remote_port=maintenance_port)
                self.port_forwarders.append(port_forwarder)
                local_port = port_forwarder.local_port
                rpc_host = port_forwarder.local_address

            servers.append(
                XdsTestServer(ip=pod_ip,
                              rpc_port=test_port,
                              maintenance_port=local_port,
                              secure_mode=secure_mode,
                              server_id=server_id,
                              rpc_host=rpc_host,
                              pod_name=pod_name))
        return servers

    def cleanup(self, *, force=False, force_namespace=False):
        if self.port_forwarders:
            for port_forwarder in self.port_forwarders:
                port_forwarder.close()
            self.port_forwarders = []
        if self.deployment or force:
            self._delete_deployment(self.deployment_name)
            self.deployment = None
        if (self.service and not self.reuse_service) or force:
            self._delete_service(self.service_name)
            self.service = None
        if self.enable_workload_identity and (self.service_account or force):
            self._revoke_workload_identity_user(
                gcp_iam=self.gcp_iam,
                gcp_service_account=self.gcp_service_account,
                service_account_name=self.service_account_name)
            self._delete_service_account(self.service_account_name)
            self.service_account = None
        super().cleanup(force=(force_namespace and force))

    @classmethod
    def make_namespace_name(cls,
                            resource_prefix: str,
                            resource_suffix: str,
                            name: str = 'server') -> str:
        """A helper to make consistent XdsTestServer kubernetes namespace name
        for given resource prefix and suffix.

        Note: the idea is to intentionally produce different namespace name for
        the test server, and the test client, as that closely mimics real-world
        deployments.
        :rtype: object
        """
        return cls._make_namespace_name(resource_prefix, resource_suffix, name)
