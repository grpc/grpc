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
import datetime
import enum
import hashlib
import logging
import time
from typing import Optional, Tuple

from absl import flags
from absl.testing import absltest

from framework import xds_flags
from framework import xds_k8s_flags
from framework.helpers import retryers
from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.infrastructure import traffic_director
from framework.rpc import grpc_channelz
from framework.rpc import grpc_testing
from framework.test_app import client_app
from framework.test_app import server_app

logger = logging.getLogger(__name__)
_FORCE_CLEANUP = flags.DEFINE_bool(
    "force_cleanup",
    default=False,
    help="Force resource cleanup, even if not created by this test run")
# TODO(yashkt): We will no longer need this flag once Core exposes local certs
# from channelz
_CHECK_LOCAL_CERTS = flags.DEFINE_bool(
    "check_local_certs",
    default=True,
    help="Security Tests also check the value of local certs")
flags.adopt_module_key_flags(xds_flags)
flags.adopt_module_key_flags(xds_k8s_flags)

# Type aliases
XdsTestServer = server_app.XdsTestServer
XdsTestClient = client_app.XdsTestClient
LoadBalancerStatsResponse = grpc_testing.LoadBalancerStatsResponse
_ChannelState = grpc_channelz.ChannelState
_timedelta = datetime.timedelta
_DEFAULT_SECURE_MODE_MAINTENANCE_PORT = \
    server_app.KubernetesServerRunner.DEFAULT_SECURE_MODE_MAINTENANCE_PORT


class XdsKubernetesTestCase(absltest.TestCase):
    k8s_api_manager: k8s.KubernetesApiManager
    gcp_api_manager: gcp.api.GcpApiManager

    @classmethod
    def setUpClass(cls):
        # GCP
        cls.project: str = xds_flags.PROJECT.value
        cls.network: str = xds_flags.NETWORK.value
        cls.gcp_service_account: str = xds_k8s_flags.GCP_SERVICE_ACCOUNT.value
        cls.td_bootstrap_image = xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value
        cls.xds_server_uri = xds_flags.XDS_SERVER_URI.value

        # Base namespace
        # TODO(sergiitk): generate for each test
        cls.namespace: str = xds_flags.NAMESPACE.value

        # Test server
        cls.server_image = xds_k8s_flags.SERVER_IMAGE.value
        cls.server_name = xds_flags.SERVER_NAME.value
        cls.server_port = xds_flags.SERVER_PORT.value
        cls.server_maintenance_port = xds_flags.SERVER_MAINTENANCE_PORT.value
        cls.server_xds_host = xds_flags.SERVER_NAME.value
        cls.server_xds_port = xds_flags.SERVER_XDS_PORT.value

        # Test client
        cls.client_image = xds_k8s_flags.CLIENT_IMAGE.value
        cls.client_name = xds_flags.CLIENT_NAME.value
        cls.client_port = xds_flags.CLIENT_PORT.value

        # Test suite settings
        cls.force_cleanup = _FORCE_CLEANUP.value
        cls.debug_use_port_forwarding = \
            xds_k8s_flags.DEBUG_USE_PORT_FORWARDING.value
        cls.check_local_certs = _CHECK_LOCAL_CERTS.value

        # Resource managers
        cls.k8s_api_manager = k8s.KubernetesApiManager(
            xds_k8s_flags.KUBE_CONTEXT.value)
        cls.gcp_api_manager = gcp.api.GcpApiManager()

    def setUp(self):
        # TODO(sergiitk): generate namespace with run id for each test
        self.server_namespace = self.namespace
        self.client_namespace = self.namespace

        # Init this in child class
        # TODO(sergiitk): consider making a method to be less error-prone
        self.server_runner = None
        self.client_runner = None
        self.td = None

    @classmethod
    def tearDownClass(cls):
        cls.k8s_api_manager.close()
        cls.gcp_api_manager.close()

    def tearDown(self):
        logger.info('----- TestMethod %s teardown -----', self.id())
        retryer = retryers.constant_retryer(wait_fixed=_timedelta(seconds=10),
                                            attempts=3,
                                            log_level=logging.INFO)
        try:
            retryer(self._cleanup)
        except retryers.RetryError:
            logger.exception('Got error during teardown')

    def _cleanup(self):
        self.td.cleanup(force=self.force_cleanup)
        self.client_runner.cleanup(force=self.force_cleanup)
        self.server_runner.cleanup(force=self.force_cleanup,
                                   force_namespace=self.force_cleanup)

    def setupTrafficDirectorGrpc(self):
        self.td.setup_for_grpc(self.server_xds_host,
                               self.server_xds_port,
                               health_check_port=self.server_maintenance_port)

    def setupServerBackends(self, *, wait_for_healthy_status=True):
        # Load Backends
        neg_name, neg_zones = self.server_runner.k8s_namespace.get_service_neg(
            self.server_runner.service_name, self.server_port)

        # Add backends to the Backend Service
        self.td.backend_service_add_neg_backends(neg_name, neg_zones)
        if wait_for_healthy_status:
            self.td.wait_for_backends_healthy_status()

    def assertSuccessfulRpcs(self,
                             test_client: XdsTestClient,
                             num_rpcs: int = 100):
        lb_stats = self.sendRpcs(test_client, num_rpcs)
        self.assertAllBackendsReceivedRpcs(lb_stats)
        failed = int(lb_stats.num_failures)
        self.assertLessEqual(
            failed,
            0,
            msg=f'Expected all RPCs to succeed: {failed} of {num_rpcs} failed')

    def assertFailedRpcs(self,
                         test_client: XdsTestClient,
                         num_rpcs: Optional[int] = 100):
        lb_stats = self.sendRpcs(test_client, num_rpcs)
        failed = int(lb_stats.num_failures)
        self.assertEqual(
            failed,
            num_rpcs,
            msg=f'Expected all RPCs to fail: {failed} of {num_rpcs} failed')

    @staticmethod
    def sendRpcs(test_client: XdsTestClient,
                 num_rpcs: int) -> LoadBalancerStatsResponse:
        lb_stats = test_client.get_load_balancer_stats(num_rpcs=num_rpcs)
        logger.info(
            'Received LoadBalancerStatsResponse from test client %s:\n%s',
            test_client.ip, lb_stats)
        return lb_stats

    def assertAllBackendsReceivedRpcs(self, lb_stats):
        # TODO(sergiitk): assert backends length
        for backend, rpcs_count in lb_stats.rpcs_by_peer.items():
            self.assertGreater(
                int(rpcs_count),
                0,
                msg=f'Backend {backend} did not receive a single RPC')


class RegularXdsKubernetesTestCase(XdsKubernetesTestCase):

    def setUp(self):
        super().setUp()

        # Traffic Director Configuration
        self.td = traffic_director.TrafficDirectorManager(
            self.gcp_api_manager,
            project=self.project,
            resource_prefix=self.namespace,
            network=self.network)

        # Test Server Runner
        self.server_runner = server_app.KubernetesServerRunner(
            k8s.KubernetesNamespace(self.k8s_api_manager,
                                    self.server_namespace),
            deployment_name=self.server_name,
            image_name=self.server_image,
            gcp_service_account=self.gcp_service_account,
            td_bootstrap_image=self.td_bootstrap_image,
            xds_server_uri=self.xds_server_uri,
            network=self.network)

        # Test Client Runner
        self.client_runner = client_app.KubernetesClientRunner(
            k8s.KubernetesNamespace(self.k8s_api_manager,
                                    self.client_namespace),
            deployment_name=self.client_name,
            image_name=self.client_image,
            gcp_service_account=self.gcp_service_account,
            td_bootstrap_image=self.td_bootstrap_image,
            xds_server_uri=self.xds_server_uri,
            network=self.network,
            debug_use_port_forwarding=self.debug_use_port_forwarding,
            stats_port=self.client_port,
            reuse_namespace=self.server_namespace == self.client_namespace)

    def startTestServer(self, replica_count=1, **kwargs) -> XdsTestServer:
        test_server = self.server_runner.run(
            replica_count=replica_count,
            test_port=self.server_port,
            maintenance_port=self.server_maintenance_port,
            **kwargs)
        test_server.set_xds_address(self.server_xds_host, self.server_xds_port)
        return test_server

    def startTestClient(self, test_server: XdsTestServer,
                        **kwargs) -> XdsTestClient:
        test_client = self.client_runner.run(server_target=test_server.xds_uri,
                                             **kwargs)
        test_client.wait_for_active_server_channel()
        return test_client


class SecurityXdsKubernetesTestCase(XdsKubernetesTestCase):

    class SecurityMode(enum.Enum):
        MTLS = enum.auto()
        TLS = enum.auto()
        PLAINTEXT = enum.auto()

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        if cls.server_maintenance_port is None:
            # In secure mode, the maintenance port is different from
            # the test port to keep it insecure, and make
            # Health Checks and Channelz tests available.
            # When not provided, use explicit numeric port value, so
            # Backend Health Checks are created on a fixed port.
            cls.server_maintenance_port = _DEFAULT_SECURE_MODE_MAINTENANCE_PORT

    def setUp(self):
        super().setUp()

        # Traffic Director Configuration
        self.td = traffic_director.TrafficDirectorSecureManager(
            self.gcp_api_manager,
            project=self.project,
            resource_prefix=self.namespace,
            network=self.network)

        # Test Server Runner
        self.server_runner = server_app.KubernetesServerRunner(
            k8s.KubernetesNamespace(self.k8s_api_manager,
                                    self.server_namespace),
            deployment_name=self.server_name,
            image_name=self.server_image,
            gcp_service_account=self.gcp_service_account,
            network=self.network,
            td_bootstrap_image=self.td_bootstrap_image,
            xds_server_uri=self.xds_server_uri,
            deployment_template='server-secure.deployment.yaml',
            debug_use_port_forwarding=self.debug_use_port_forwarding)

        # Test Client Runner
        self.client_runner = client_app.KubernetesClientRunner(
            k8s.KubernetesNamespace(self.k8s_api_manager,
                                    self.client_namespace),
            deployment_name=self.client_name,
            image_name=self.client_image,
            gcp_service_account=self.gcp_service_account,
            td_bootstrap_image=self.td_bootstrap_image,
            xds_server_uri=self.xds_server_uri,
            network=self.network,
            deployment_template='client-secure.deployment.yaml',
            stats_port=self.client_port,
            reuse_namespace=self.server_namespace == self.client_namespace,
            debug_use_port_forwarding=self.debug_use_port_forwarding)

    def startSecureTestServer(self, replica_count=1, **kwargs) -> XdsTestServer:
        test_server = self.server_runner.run(
            replica_count=replica_count,
            test_port=self.server_port,
            maintenance_port=self.server_maintenance_port,
            secure_mode=True,
            **kwargs)
        test_server.set_xds_address(self.server_xds_host, self.server_xds_port)
        return test_server

    def setupSecurityPolicies(self, *, server_tls, server_mtls, client_tls,
                              client_mtls):
        self.td.setup_client_security(server_namespace=self.server_namespace,
                                      server_name=self.server_name,
                                      tls=client_tls,
                                      mtls=client_mtls)
        self.td.setup_server_security(server_namespace=self.server_namespace,
                                      server_name=self.server_name,
                                      server_port=self.server_port,
                                      tls=server_tls,
                                      mtls=server_mtls)

    def startSecureTestClient(self,
                              test_server: XdsTestServer,
                              *,
                              wait_for_active_server_channel=True,
                              **kwargs) -> XdsTestClient:
        test_client = self.client_runner.run(server_target=test_server.xds_uri,
                                             secure_mode=True,
                                             **kwargs)
        if wait_for_active_server_channel:
            test_client.wait_for_active_server_channel()
        return test_client

    def assertTestAppSecurity(self, mode: SecurityMode,
                              test_client: XdsTestClient,
                              test_server: XdsTestServer):
        client_socket, server_socket = self.getConnectedSockets(
            test_client, test_server)
        server_security: grpc_channelz.Security = server_socket.security
        client_security: grpc_channelz.Security = client_socket.security
        logger.info('Server certs: %s', self.debug_sock_certs(server_security))
        logger.info('Client certs: %s', self.debug_sock_certs(client_security))

        if mode is self.SecurityMode.MTLS:
            self.assertSecurityMtls(client_security, server_security)
        elif mode is self.SecurityMode.TLS:
            self.assertSecurityTls(client_security, server_security)
        elif mode is self.SecurityMode.PLAINTEXT:
            self.assertSecurityPlaintext(client_security, server_security)
        else:
            raise TypeError('Incorrect security mode')

    def assertSecurityMtls(self, client_security: grpc_channelz.Security,
                           server_security: grpc_channelz.Security):
        self.assertEqual(client_security.WhichOneof('model'),
                         'tls',
                         msg='(mTLS) Client socket security model must be TLS')
        self.assertEqual(server_security.WhichOneof('model'),
                         'tls',
                         msg='(mTLS) Server socket security model must be TLS')
        server_tls, client_tls = server_security.tls, client_security.tls

        # Confirm regular TLS: server local cert == client remote cert
        self.assertNotEmpty(client_tls.remote_certificate,
                            msg="(mTLS) Client remote certificate is missing")
        if self.check_local_certs:
            self.assertNotEmpty(
                server_tls.local_certificate,
                msg="(mTLS) Server local certificate is missing")
            self.assertEqual(
                server_tls.local_certificate,
                client_tls.remote_certificate,
                msg="(mTLS) Server local certificate must match client's "
                "remote certificate")

        # mTLS: server remote cert == client local cert
        self.assertNotEmpty(server_tls.remote_certificate,
                            msg="(mTLS) Server remote certificate is missing")
        if self.check_local_certs:
            self.assertNotEmpty(
                client_tls.local_certificate,
                msg="(mTLS) Client local certificate is missing")
            self.assertEqual(
                server_tls.remote_certificate,
                client_tls.local_certificate,
                msg="(mTLS) Server remote certificate must match client's "
                "local certificate")

    def assertSecurityTls(self, client_security: grpc_channelz.Security,
                          server_security: grpc_channelz.Security):
        self.assertEqual(client_security.WhichOneof('model'),
                         'tls',
                         msg='(TLS) Client socket security model must be TLS')
        self.assertEqual(server_security.WhichOneof('model'),
                         'tls',
                         msg='(TLS) Server socket security model must be TLS')
        server_tls, client_tls = server_security.tls, client_security.tls

        # Regular TLS: server local cert == client remote cert
        self.assertNotEmpty(client_tls.remote_certificate,
                            msg="(TLS) Client remote certificate is missing")
        if self.check_local_certs:
            self.assertNotEmpty(server_tls.local_certificate,
                                msg="(TLS) Server local certificate is missing")
            self.assertEqual(
                server_tls.local_certificate,
                client_tls.remote_certificate,
                msg="(TLS) Server local certificate must match client "
                "remote certificate")

        # mTLS must not be used
        self.assertEmpty(
            server_tls.remote_certificate,
            msg="(TLS) Server remote certificate must be empty in TLS mode. "
            "Is server security incorrectly configured for mTLS?")
        self.assertEmpty(
            client_tls.local_certificate,
            msg="(TLS) Client local certificate must be empty in TLS mode. "
            "Is client security incorrectly configured for mTLS?")

    def assertSecurityPlaintext(self, client_security, server_security):
        server_tls, client_tls = server_security.tls, client_security.tls
        # Not TLS
        self.assertEmpty(
            server_tls.local_certificate,
            msg="(Plaintext) Server local certificate must be empty.")
        self.assertEmpty(
            client_tls.local_certificate,
            msg="(Plaintext) Client local certificate must be empty.")

        # Not mTLS
        self.assertEmpty(
            server_tls.remote_certificate,
            msg="(Plaintext) Server remote certificate must be empty.")
        self.assertEmpty(
            client_tls.local_certificate,
            msg="(Plaintext) Client local certificate must be empty.")

    def assertClientCannotReachServerRepeatedly(
            self,
            test_client: XdsTestClient,
            *,
            times: Optional[int] = None,
            delay: Optional[_timedelta] = None):
        """
        Asserts that the client repeatedly cannot reach the server.

        With negative tests we can't be absolutely certain expected failure
        state is not caused by something else.
        To mitigate for this, we repeat the checks several times, and expect
        all of them to succeed.

        This is useful in case the channel eventually stabilizes, and RPCs pass.

        Args:
            test_client: An instance of XdsTestClient
            times: Optional; A positive number of times to confirm that
                the server is unreachable. Defaults to `3` attempts.
            delay: Optional; Specifies how long to wait before the next check.
                Defaults to `10` seconds.
        """
        if times is None or times < 1:
            times = 3
        if delay is None:
            delay = _timedelta(seconds=10)

        for i in range(1, times + 1):
            self.assertClientCannotReachServer(test_client)
            if i < times:
                logger.info('Check %s passed, waiting %s before the next check',
                            i, delay)
                time.sleep(delay.total_seconds())

    def assertClientCannotReachServer(self, test_client: XdsTestClient):
        self.assertClientChannelFailed(test_client)
        self.assertFailedRpcs(test_client)

    def assertClientChannelFailed(self, test_client: XdsTestClient):
        channel = test_client.wait_for_server_channel_state(
            state=_ChannelState.TRANSIENT_FAILURE)
        subchannels = list(
            test_client.channelz.list_channel_subchannels(channel))
        self.assertLen(subchannels,
                       1,
                       msg="Client channel must have exactly one subchannel "
                       "in state TRANSIENT_FAILURE.")

    @staticmethod
    def getConnectedSockets(
        test_client: XdsTestClient, test_server: XdsTestServer
    ) -> Tuple[grpc_channelz.Socket, grpc_channelz.Socket]:
        client_sock = test_client.get_active_server_channel_socket()
        server_sock = test_server.get_server_socket_matching_client(client_sock)
        return client_sock, server_sock

    @classmethod
    def debug_sock_certs(cls, security: grpc_channelz.Security):
        if security.WhichOneof('model') == 'other':
            return f'other: <{security.other.name}={security.other.value}>'

        return (f'local: <{cls.debug_cert(security.tls.local_certificate)}>, '
                f'remote: <{cls.debug_cert(security.tls.remote_certificate)}>')

    @staticmethod
    def debug_cert(cert):
        if not cert:
            return 'missing'
        sha1 = hashlib.sha1(cert)
        return f'sha1={sha1.hexdigest()}, len={len(cert)}'
