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
import functools
import logging
from typing import Optional, Iterator

import tenacity

from framework.infrastructure import k8s
import framework.rpc
from framework.rpc import grpc_channelz
from framework.rpc import grpc_testing
from framework.test_app import base_runner

logger = logging.getLogger(__name__)

# Type aliases
ChannelzServiceClient = grpc_channelz.ChannelzServiceClient
ChannelConnectivityState = grpc_channelz.ChannelConnectivityState
LoadBalancerStatsServiceClient = grpc_testing.LoadBalancerStatsServiceClient


class XdsTestClient(framework.rpc.GrpcApp):
    def __init__(self, *,
                 ip: str,
                 rpc_port: int,
                 server_target: str,
                 rpc_host: Optional[str] = None,
                 maintenance_port: Optional[int] = None):
        super().__init__(rpc_host=(rpc_host or ip))
        self.ip = ip
        self.rpc_port = rpc_port
        self.server_target = server_target
        self.maintenance_port = maintenance_port or rpc_port

    @property
    @functools.lru_cache(None)
    def load_balancer_stats(self) -> LoadBalancerStatsServiceClient:
        return LoadBalancerStatsServiceClient(self._make_channel(self.rpc_port))

    @property
    @functools.lru_cache(None)
    def channelz(self) -> ChannelzServiceClient:
        return ChannelzServiceClient(self._make_channel(self.maintenance_port))

    def get_load_balancer_stats(
        self, *,
        num_rpcs: int,
        timeout_sec: Optional[int] = None,
    ) -> grpc_testing.LoadBalancerStatsResponse:
        """
        Shortcut to LoadBalancerStatsServiceClient.get_client_stats()
        """
        return self.load_balancer_stats.get_client_stats(
            num_rpcs=num_rpcs, timeout_sec=timeout_sec)

    def get_server_channels(self) -> Iterator[grpc_channelz.Channel]:
        return self.channelz.find_channels_for_target(self.server_target)

    def wait_for_active_server_channel(self):
        retryer = tenacity.Retrying(
            retry=(tenacity.retry_if_result(lambda r: r is None) |
                   tenacity.retry_if_exception_type()),
            wait=tenacity.wait_exponential(max=10),
            stop=tenacity.stop_after_delay(60 * 3),
            reraise=True)
        channel = retryer(self.get_active_server_channel)
        logger.info(
            'Active server channel found: channel_id: %s, %s',
            channel.ref.channel_id, channel.ref.name)
        logger.debug('Server channel:\n%r', channel)

    def get_active_server_channel(self) -> Optional[grpc_channelz.Channel]:
        for channel in self.get_server_channels():
            state: ChannelConnectivityState = channel.data.state
            logger.debug('Server channel: %s, state: %s',
                         channel.ref.name,
                         ChannelConnectivityState.State.Name(state.state))
            if state.state is ChannelConnectivityState.READY:
                return channel
        raise self.NotFound('Client has no active channel with the server')

    def get_client_socket_with_test_server(self) -> grpc_channelz.Socket:
        channel = self.get_active_server_channel()
        logger.debug('Retrieving client->server socket: channel %s',
                     channel.ref.name)
        # Get the first subchannel of the active server channel
        subchannel_id = channel.subchannel_ref[0].subchannel_id
        subchannel = self.channelz.get_subchannel(subchannel_id)
        logger.debug('Retrieving client->server socket: subchannel %s',
                     subchannel.ref.name)
        # Get the first socket of the subchannel
        socket = self.channelz.get_socket(subchannel.socket_ref[0].socket_id)
        logger.debug('Found client->server socket: %s', socket.ref.name)
        return socket


class KubernetesClientRunner(base_runner.KubernetesBaseRunner):
    def __init__(self,
                 k8s_namespace,
                 *,
                 deployment_name,
                 image_name,
                 gcp_service_account,
                 td_bootstrap_image,
                 service_account_name=None,
                 stats_port=8079,
                 network='default',
                 deployment_template='client.deployment.yaml',
                 service_account_template='service-account.yaml',
                 reuse_namespace=False,
                 namespace_template=None,
                 debug_use_port_forwarding=False):
        super().__init__(k8s_namespace, namespace_template, reuse_namespace)

        # Settings
        self.deployment_name = deployment_name
        self.image_name = image_name
        self.gcp_service_account = gcp_service_account
        self.service_account_name = service_account_name or deployment_name
        self.stats_port = stats_port
        # xDS bootstrap generator
        self.td_bootstrap_image = td_bootstrap_image
        self.network = network
        self.deployment_template = deployment_template
        self.service_account_template = service_account_template
        self.debug_use_port_forwarding = debug_use_port_forwarding

        # Mutable state
        self.deployment: Optional[k8s.V1Deployment] = None
        self.service_account: Optional[k8s.V1ServiceAccount] = None
        self.port_forwarder = None

    def run(self, *,
            server_target,
            rpc='UnaryCall', qps=25,
            secure_mode=False,
            print_response=False) -> XdsTestClient:
        super().run()
        # todo(sergiitk): make rpc UnaryCall enum or get it from proto

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
            network_name=self.network,
            stats_port=self.stats_port,
            server_target=server_target,
            rpc=rpc,
            qps=qps,
            secure_mode=secure_mode,
            print_response=print_response)

        self._wait_deployment_with_available_replicas(self.deployment_name)

        # Load test client pod. We need only one client at the moment
        pod = self.k8s_namespace.list_deployment_pods(self.deployment)[0]
        self._wait_pod_started(pod.metadata.name)
        pod_ip = pod.status.pod_ip
        rpc_host = None

        # Experimental, for local debugging.
        if self.debug_use_port_forwarding:
            logger.info('Enabling port forwarding from %s:%s',
                        pod_ip, self.stats_port)
            self.port_forwarder = self.k8s_namespace.port_forward_pod(
                pod, remote_port=self.stats_port)
            rpc_host = self.k8s_namespace.PORT_FORWARD_LOCAL_ADDRESS

        return XdsTestClient(ip=pod_ip,
                             rpc_port=self.stats_port,
                             server_target=server_target,
                             rpc_host=rpc_host)

    def cleanup(self, *, force=False, force_namespace=False):
        if self.port_forwarder:
            self.k8s_namespace.port_forward_stop(self.port_forwarder)
            self.port_forwarder = None
        if self.deployment or force:
            self._delete_deployment(self.deployment_name)
            self.deployment = None
        if self.service_account or force:
            self._delete_service_account(self.service_account_name)
            self.service_account = None
        super().cleanup(force=force_namespace and force)
