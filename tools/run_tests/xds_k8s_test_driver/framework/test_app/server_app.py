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
from typing import Iterator, Optional

from framework.infrastructure import k8s
import framework.rpc
from framework.rpc import grpc_channelz
from framework.test_app import base_runner

logger = logging.getLogger(__name__)

# Type aliases
_ChannelzServiceClient = grpc_channelz.ChannelzServiceClient


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
                 rpc_host: Optional[str] = None):
        super().__init__(rpc_host=(rpc_host or ip))
        self.ip = ip
        self.rpc_port = rpc_port
        self.maintenance_port = maintenance_port or rpc_port
        self.secure_mode = secure_mode
        self.server_id = server_id
        self.xds_host, self.xds_port = xds_host, xds_port

    @property
    @functools.lru_cache(None)
    def channelz(self) -> _ChannelzServiceClient:
        return _ChannelzServiceClient(self._make_channel(self.maintenance_port))

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
                 gcp_service_account,
                 service_account_name=None,
                 service_name=None,
                 neg_name=None,
                 td_bootstrap_image=None,
                 xds_server_uri=None,
                 network='default',
                 deployment_template='server.deployment.yaml',
                 service_account_template='service-account.yaml',
                 service_template='server.service.yaml',
                 reuse_service=False,
                 reuse_namespace=False,
                 namespace_template=None,
                 debug_use_port_forwarding=False):
        super().__init__(k8s_namespace, namespace_template, reuse_namespace)

        # Settings
        self.deployment_name = deployment_name
        self.image_name = image_name
        self.gcp_service_account = gcp_service_account
        self.service_account_name = service_account_name or deployment_name
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
        self.service_account_template = service_account_template
        self.service_template = service_template
        self.reuse_service = reuse_service
        self.debug_use_port_forwarding = debug_use_port_forwarding

        # Mutable state
        self.deployment: Optional[k8s.V1Deployment] = None
        self.service_account: Optional[k8s.V1ServiceAccount] = None
        self.service: Optional[k8s.V1Service] = None
        self.port_forwarder = None

    def run(self,
            *,
            test_port=DEFAULT_TEST_PORT,
            maintenance_port=None,
            secure_mode=False,
            server_id=None,
            replica_count=1) -> XdsTestServer:
        # TODO(sergiitk): multiple replicas
        if replica_count != 1:
            raise NotImplementedError("Multiple replicas not yet supported")

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
        for pod in pods:
            self._wait_pod_started(pod.metadata.name)

        # TODO(sergiitk): This is why multiple replicas not yet supported
        pod = pods[0]
        pod_ip = pod.status.pod_ip
        rpc_host = None
        # Experimental, for local debugging.
        if self.debug_use_port_forwarding:
            logger.info('LOCAL DEV MODE: Enabling port forwarding to %s:%s',
                        pod_ip, maintenance_port)
            self.port_forwarder = self.k8s_namespace.port_forward_pod(
                pod, remote_port=maintenance_port)
            rpc_host = self.k8s_namespace.PORT_FORWARD_LOCAL_ADDRESS

        return XdsTestServer(ip=pod_ip,
                             rpc_port=test_port,
                             maintenance_port=maintenance_port,
                             secure_mode=secure_mode,
                             server_id=server_id,
                             rpc_host=rpc_host)

    def cleanup(self, *, force=False, force_namespace=False):
        if self.port_forwarder:
            self.k8s_namespace.port_forward_stop(self.port_forwarder)
            self.port_forwarder = None
        if self.deployment or force:
            self._delete_deployment(self.deployment_name)
            self.deployment = None
        if (self.service and not self.reuse_service) or force:
            self._delete_service(self.service_name)
            self.service = None
        if self.service_account or force:
            self._delete_service_account(self.service_account_name)
            self.service_account = None
        super().cleanup(force=(force_namespace and force))
