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
Provides an interface to xDS Test Server running remotely.
"""
import functools
import logging
from typing import Iterator, Optional

import framework.rpc
from framework.rpc import grpc_channelz
from framework.rpc import grpc_testing

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
