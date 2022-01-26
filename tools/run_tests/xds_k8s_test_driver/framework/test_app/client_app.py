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
xDS Test Client.

TODO(sergiitk): separate XdsTestClient and KubernetesClientRunner to individual
modules.
"""
import datetime
import functools
import logging
from typing import Iterable, List, Optional

from framework.helpers import retryers
from framework.infrastructure import gcp
from framework.infrastructure import k8s
import framework.rpc
from framework.rpc import grpc_channelz
from framework.rpc import grpc_csds
from framework.rpc import grpc_testing
from framework.test_app import base_runner

logger = logging.getLogger(__name__)

# Type aliases
_timedelta = datetime.timedelta
_LoadBalancerStatsServiceClient = grpc_testing.LoadBalancerStatsServiceClient
_XdsUpdateClientConfigureServiceClient = grpc_testing.XdsUpdateClientConfigureServiceClient
_ChannelzServiceClient = grpc_channelz.ChannelzServiceClient
_ChannelzChannel = grpc_channelz.Channel
_ChannelzChannelState = grpc_channelz.ChannelState
_ChannelzSubchannel = grpc_channelz.Subchannel
_ChannelzSocket = grpc_channelz.Socket
_CsdsClient = grpc_csds.CsdsClient


class XdsTestClient(framework.rpc.grpc.GrpcApp):
    """
    Represents RPC services implemented in Client component of the xds test app.
    https://github.com/grpc/grpc/blob/master/doc/xds-test-descriptions.md#client
    """

    def __init__(self,
                 *,
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
    def load_balancer_stats(self) -> _LoadBalancerStatsServiceClient:
        return _LoadBalancerStatsServiceClient(self._make_channel(
            self.rpc_port))

    @property
    @functools.lru_cache(None)
    def update_config(self):
        return _XdsUpdateClientConfigureServiceClient(
            self._make_channel(self.rpc_port))

    @property
    @functools.lru_cache(None)
    def channelz(self) -> _ChannelzServiceClient:
        return _ChannelzServiceClient(self._make_channel(self.maintenance_port))

    @property
    @functools.lru_cache(None)
    def csds(self) -> _CsdsClient:
        return _CsdsClient(self._make_channel(self.maintenance_port))

    def get_load_balancer_stats(
        self,
        *,
        num_rpcs: int,
        timeout_sec: Optional[int] = None,
    ) -> grpc_testing.LoadBalancerStatsResponse:
        """
        Shortcut to LoadBalancerStatsServiceClient.get_client_stats()
        """
        return self.load_balancer_stats.get_client_stats(
            num_rpcs=num_rpcs, timeout_sec=timeout_sec)

    def get_load_balancer_accumulated_stats(
        self,
        *,
        timeout_sec: Optional[int] = None,
    ) -> grpc_testing.LoadBalancerAccumulatedStatsResponse:
        """Shortcut to LoadBalancerStatsServiceClient.get_client_accumulated_stats()"""
        return self.load_balancer_stats.get_client_accumulated_stats(
            timeout_sec=timeout_sec)

    def wait_for_active_server_channel(self) -> _ChannelzChannel:
        """Wait for the channel to the server to transition to READY.

        Raises:
            GrpcApp.NotFound: If the channel never transitioned to READY.
        """
        return self.wait_for_server_channel_state(_ChannelzChannelState.READY)

    def get_active_server_channel_socket(self) -> _ChannelzSocket:
        channel = self.find_server_channel_with_state(
            _ChannelzChannelState.READY)
        # Get the first subchannel of the active channel to the server.
        logger.debug(
            'Retrieving client -> server socket, '
            'channel_id: %s, subchannel: %s', channel.ref.channel_id,
            channel.subchannel_ref[0].name)
        subchannel, *subchannels = list(
            self.channelz.list_channel_subchannels(channel))
        if subchannels:
            logger.warning('Unexpected subchannels: %r', subchannels)
        # Get the first socket of the subchannel
        socket, *sockets = list(
            self.channelz.list_subchannels_sockets(subchannel))
        if sockets:
            logger.warning('Unexpected sockets: %r', subchannels)
        logger.debug('Found client -> server socket: %s', socket.ref.name)
        return socket

    def wait_for_server_channel_state(
            self,
            state: _ChannelzChannelState,
            *,
            timeout: Optional[_timedelta] = None,
            rpc_deadline: Optional[_timedelta] = None) -> _ChannelzChannel:
        # When polling for a state, prefer smaller wait times to avoid
        # exhausting all allowed time on a single long RPC.
        if rpc_deadline is None:
            rpc_deadline = _timedelta(seconds=30)

        # Fine-tuned to wait for the channel to the server.
        retryer = retryers.exponential_retryer_with_timeout(
            wait_min=_timedelta(seconds=10),
            wait_max=_timedelta(seconds=25),
            timeout=_timedelta(minutes=5) if timeout is None else timeout)

        logger.info('Waiting for client %s to report a %s channel to %s',
                    self.ip, _ChannelzChannelState.Name(state),
                    self.server_target)
        channel = retryer(self.find_server_channel_with_state,
                          state,
                          rpc_deadline=rpc_deadline)
        logger.info('Client %s channel to %s transitioned to state %s:\n%s',
                    self.ip, self.server_target,
                    _ChannelzChannelState.Name(state), channel)
        return channel

    def find_server_channel_with_state(
            self,
            state: _ChannelzChannelState,
            *,
            rpc_deadline: Optional[_timedelta] = None,
            check_subchannel=True) -> _ChannelzChannel:
        rpc_params = {}
        if rpc_deadline is not None:
            rpc_params['deadline_sec'] = rpc_deadline.total_seconds()

        for channel in self.get_server_channels(**rpc_params):
            channel_state: _ChannelzChannelState = channel.data.state.state
            logger.info('Server channel: %s, state: %s', channel.ref.name,
                        _ChannelzChannelState.Name(channel_state))
            if channel_state is state:
                if check_subchannel:
                    # When requested, check if the channel has at least
                    # one subchannel in the requested state.
                    try:
                        subchannel = self.find_subchannel_with_state(
                            channel, state, **rpc_params)
                        logger.info('Found subchannel in state %s: %s',
                                    _ChannelzChannelState.Name(state),
                                    subchannel)
                    except self.NotFound as e:
                        # Otherwise, keep searching.
                        logger.info(e.message)
                        continue
                return channel

        raise self.NotFound(
            f'Client has no {_ChannelzChannelState.Name(state)} channel with '
            'the server')

    def get_server_channels(self, **kwargs) -> Iterable[_ChannelzChannel]:
        return self.channelz.find_channels_for_target(self.server_target,
                                                      **kwargs)

    def find_subchannel_with_state(self, channel: _ChannelzChannel,
                                   state: _ChannelzChannelState,
                                   **kwargs) -> _ChannelzSubchannel:
        subchannels = self.channelz.list_channel_subchannels(channel, **kwargs)
        for subchannel in subchannels:
            if subchannel.data.state.state is state:
                return subchannel

        raise self.NotFound(
            f'Not found a {_ChannelzChannelState.Name(state)} '
            f'subchannel for channel_id {channel.ref.channel_id}')

    def find_subchannels_with_state(self, state: _ChannelzChannelState,
                                    **kwargs) -> List[_ChannelzSubchannel]:
        subchannels = []
        for channel in self.channelz.find_channels_for_target(
                self.server_target, **kwargs):
            for subchannel in self.channelz.list_channel_subchannels(
                    channel, **kwargs):
                if subchannel.data.state.state is state:
                    subchannels.append(subchannel)
        return subchannels


class KubernetesClientRunner(base_runner.KubernetesBaseRunner):

    def __init__(self,
                 k8s_namespace,
                 *,
                 deployment_name,
                 image_name,
                 td_bootstrap_image,
                 gcp_api_manager: gcp.api.GcpApiManager,
                 gcp_project: str,
                 gcp_service_account: str,
                 xds_server_uri=None,
                 network='default',
                 config_scope=None,
                 service_account_name=None,
                 stats_port=8079,
                 deployment_template='client.deployment.yaml',
                 service_account_template='service-account.yaml',
                 reuse_namespace=False,
                 namespace_template=None,
                 debug_use_port_forwarding=False,
                 enable_workload_identity=True):
        super().__init__(k8s_namespace, namespace_template, reuse_namespace)

        # Settings
        self.deployment_name = deployment_name
        self.image_name = image_name
        self.stats_port = stats_port
        # xDS bootstrap generator
        self.td_bootstrap_image = td_bootstrap_image
        self.xds_server_uri = xds_server_uri
        self.network = network
        self.config_scope = config_scope
        self.deployment_template = deployment_template
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
        self.port_forwarder: Optional[k8s.PortForwarder] = None

    # TODO(sergiitk): make rpc UnaryCall enum or get it from proto
    def run(self,
            *,
            server_target,
            rpc='UnaryCall',
            qps=25,
            metadata='',
            secure_mode=False,
            print_response=False) -> XdsTestClient:
        logger.info(
            'Deploying xDS test client "%s" to k8s namespace %s: '
            'server_target=%s rpc=%s qps=%s metadata=%r secure_mode=%s '
            'print_response=%s', self.deployment_name, self.k8s_namespace.name,
            server_target, rpc, qps, metadata, secure_mode, print_response)
        self._logs_explorer_link(deployment_name=self.deployment_name,
                                 namespace_name=self.k8s_namespace.name,
                                 gcp_project=self.gcp_project,
                                 gcp_ui_url=self.gcp_ui_url)

        super().run()

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
            config_scope=self.config_scope,
            stats_port=self.stats_port,
            server_target=server_target,
            rpc=rpc,
            qps=qps,
            metadata=metadata,
            secure_mode=secure_mode,
            print_response=print_response)

        self._wait_deployment_with_available_replicas(self.deployment_name)

        # Load test client pod. We need only one client at the moment
        pod = self.k8s_namespace.list_deployment_pods(self.deployment)[0]
        self._wait_pod_started(pod.metadata.name)
        pod_ip = pod.status.pod_ip
        rpc_port = self.stats_port
        rpc_host = None

        # Experimental, for local debugging.
        if self.debug_use_port_forwarding:
            logger.info('LOCAL DEV MODE: Enabling port forwarding to %s:%s',
                        pod_ip, self.stats_port)
            self.port_forwarder = self.k8s_namespace.port_forward_pod(
                pod, remote_port=self.stats_port)
            rpc_port = self.port_forwarder.local_port
            rpc_host = self.port_forwarder.local_address

        return XdsTestClient(ip=pod_ip,
                             rpc_port=rpc_port,
                             server_target=server_target,
                             rpc_host=rpc_host)

    def cleanup(self, *, force=False, force_namespace=False):
        if self.port_forwarder:
            self.port_forwarder.close()
            self.port_forwarder = None
        if self.deployment or force:
            self._delete_deployment(self.deployment_name)
            self.deployment = None
        if self.enable_workload_identity and (self.service_account or force):
            self._revoke_workload_identity_user(
                gcp_iam=self.gcp_iam,
                gcp_service_account=self.gcp_service_account,
                service_account_name=self.service_account_name)
            self._delete_service_account(self.service_account_name)
            self.service_account = None
        super().cleanup(force=force_namespace and force)

    @classmethod
    def make_namespace_name(cls,
                            resource_prefix: str,
                            resource_suffix: str,
                            name: str = 'client') -> str:
        """A helper to make consistent XdsTestClient kubernetes namespace name
        for given resource prefix and suffix.

        Note: the idea is to intentionally produce different namespace name for
        the test server, and the test client, as that closely mimics real-world
        deployments.
        """
        return cls._make_namespace_name(resource_prefix, resource_suffix, name)
