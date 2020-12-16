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
import enum
import hashlib
import logging
from typing import Tuple

from absl import flags
from absl.testing import absltest

from framework import xds_flags
from framework import xds_k8s_flags
from framework.infrastructure import k8s
from framework.infrastructure import gcp
from framework.infrastructure import traffic_director
from framework.rpc import grpc_channelz
from framework.test_app import client_app
from framework.test_app import server_app

logger = logging.getLogger(__name__)
_FORCE_CLEANUP = flags.DEFINE_bool(
    "force_cleanup",
    default=False,
    help="Force resource cleanup, even if not created by this test run")
flags.adopt_module_key_flags(xds_flags)
flags.adopt_module_key_flags(xds_k8s_flags)

# Type aliases
XdsTestServer = server_app.XdsTestServer
XdsTestClient = client_app.XdsTestClient


class XdsKubernetesTestCase(absltest.TestCase):
    k8s_api_manager: k8s.KubernetesApiManager
    gcp_api_manager: gcp.api.GcpApiManager

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    @classmethod
    def setUpClass(cls):
        # GCP
        cls.project: str = xds_flags.PROJECT.value
        cls.network: str = xds_flags.NETWORK.value
        cls.gcp_service_account: str = xds_k8s_flags.GCP_SERVICE_ACCOUNT.value
        cls.td_bootstrap_image = xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value

        # Base namespace
        # TODO(sergiitk): generate for each test
        cls.namespace: str = xds_flags.NAMESPACE.value

        # Test server
        cls.server_image = xds_k8s_flags.SERVER_IMAGE.value
        cls.server_name = xds_flags.SERVER_NAME.value
        cls.server_port = xds_flags.SERVER_PORT.value
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

        # Resource managers
        cls.k8s_api_manager = k8s.KubernetesApiManager(
            xds_k8s_flags.KUBE_CONTEXT.value)
        cls.gcp_api_manager = gcp.api.GcpApiManager()

    def setUp(self):
        # TODO(sergiitk): generate namespace with run id for each test
        self.server_namespace = self.namespace
        self.client_namespace = self.namespace

        # Init this in child class
        self.server_runner = None
        self.client_runner = None
        self.td = None

    @classmethod
    def tearDownClass(cls):
        cls.k8s_api_manager.close()
        cls.gcp_api_manager.close()

    def tearDown(self):
        logger.debug('######## tearDown(): resource cleanup initiated ########')
        self.td.cleanup(force=self.force_cleanup)
        self.client_runner.cleanup(force=self.force_cleanup)
        self.server_runner.cleanup(force=self.force_cleanup,
                                   force_namespace=self.force_cleanup)

    def setupTrafficDirectorGrpc(self):
        self.td.setup_for_grpc(self.server_xds_host, self.server_xds_port)

    def setupServerBackends(self):
        # Load Backends
        neg_name, neg_zones = self.server_runner.k8s_namespace.get_service_neg(
            self.server_runner.log_service_name, self.server_port)

        # Add backends to the Backend Service
        self.td.backend_service_add_neg_backends(neg_name, neg_zones)

    def assertSuccessfulRpcs(self,
                             test_client: XdsTestClient,
                             num_rpcs: int = 100):
        # Run the test
        lb_stats = test_client.get_load_balancer_stats(num_rpcs=num_rpcs)
        # Check the results
        self.assertAllBackendsReceivedRpcs(lb_stats)
        self.assertFailedRpcsAtMost(lb_stats, 0)

    def assertAllBackendsReceivedRpcs(self, lb_stats):
        # TODO(sergiitk): assert backends length
        logger.info(lb_stats.rpcs_by_peer)
        for backend, rpcs_count in lb_stats.rpcs_by_peer.items():
            self.assertGreater(
                int(rpcs_count),
                0,
                msg='Backend {backend} did not receive a single RPC')

    def assertFailedRpcsAtMost(self, lb_stats, limit):
        failed = int(lb_stats.num_failures)
        self.assertLessEqual(
            failed,
            limit,
            msg=f'Unexpected number of RPC failures {failed} > {limit}')


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
            network=self.network,
            td_bootstrap_image=self.td_bootstrap_image)

        # Test Client Runner
        self.client_runner = client_app.KubernetesClientRunner(
            k8s.KubernetesNamespace(self.k8s_api_manager,
                                    self.client_namespace),
            deployment_name=self.client_name,
            image_name=self.client_image,
            gcp_service_account=self.gcp_service_account,
            network=self.network,
            td_bootstrap_image=self.td_bootstrap_image,
            debug_use_port_forwarding=self.debug_use_port_forwarding,
            stats_port=self.client_port,
            reuse_namespace=self.server_namespace == self.client_namespace)

    def startTestServer(self, replica_count=1, **kwargs) -> XdsTestServer:
        test_server = self.server_runner.run(replica_count=replica_count,
                                             test_port=self.server_port,
                                             **kwargs)
        test_server.set_xds_address(self.server_xds_host, self.server_xds_port)
        return test_server

    def startTestClient(self, test_server: XdsTestServer,
                        **kwargs) -> XdsTestClient:
        test_client = self.client_runner.run(server_target=test_server.xds_uri,
                                             **kwargs)
        logger.debug('Waiting fot the client to establish healthy channel with '
                     'the server')
        test_client.wait_for_active_server_channel()
        return test_client


class SecurityXdsKubernetesTestCase(XdsKubernetesTestCase):

    class SecurityMode(enum.Enum):
        MTLS = enum.auto()
        TLS = enum.auto()
        PLAINTEXT = enum.auto()

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
            deployment_template='server-secure.deployment.yaml',
            debug_use_port_forwarding=self.debug_use_port_forwarding)

        # Test Client Runner
        self.client_runner = client_app.KubernetesClientRunner(
            k8s.KubernetesNamespace(self.k8s_api_manager,
                                    self.client_namespace),
            deployment_name=self.client_name,
            image_name=self.client_image,
            gcp_service_account=self.gcp_service_account,
            network=self.network,
            td_bootstrap_image=self.td_bootstrap_image,
            deployment_template='client-secure.deployment.yaml',
            stats_port=self.client_port,
            reuse_namespace=self.server_namespace == self.client_namespace,
            debug_use_port_forwarding=self.debug_use_port_forwarding)

    def startSecureTestServer(self, replica_count=1, **kwargs) -> XdsTestServer:
        test_server = self.server_runner.run(replica_count=replica_count,
                                             test_port=self.server_port,
                                             maintenance_port=8081,
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

    def startSecureTestClient(self, test_server: XdsTestServer,
                              **kwargs) -> XdsTestClient:
        test_client = self.client_runner.run(server_target=test_server.xds_uri,
                                             secure_mode=True,
                                             **kwargs)
        logger.debug('Waiting fot the client to establish healthy channel with '
                     'the server')
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
            raise TypeError(f'Incorrect security mode')

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
        self.assertNotEmpty(server_tls.local_certificate,
                            msg="(mTLS) Server local certificate is missing")
        self.assertNotEmpty(client_tls.remote_certificate,
                            msg="(mTLS) Client remote certificate is missing")
        self.assertEqual(
            server_tls.local_certificate,
            client_tls.remote_certificate,
            msg="(mTLS) Server local certificate must match client's "
            "remote certificate")

        # mTLS: server remote cert == client local cert
        self.assertNotEmpty(server_tls.remote_certificate,
                            msg="(mTLS) Server remote certificate is missing")
        self.assertNotEmpty(client_tls.local_certificate,
                            msg="(mTLS) Client local certificate is missing")
        self.assertEqual(
            server_tls.remote_certificate,
            client_tls.local_certificate,
            msg="(mTLS) Server remote certificate must match client's "
            "local certificate")

        # Success
        logger.info('mTLS security mode  confirmed!')

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
        self.assertNotEmpty(server_tls.local_certificate,
                            msg="(TLS) Server local certificate is missing")
        self.assertNotEmpty(client_tls.remote_certificate,
                            msg="(TLS) Client remote certificate is missing")
        self.assertEqual(server_tls.local_certificate,
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

        # Success
        logger.info('TLS security mode confirmed!')

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

        # Success
        logger.info('Plaintext security mode confirmed!')

    @staticmethod
    def getConnectedSockets(
            test_client: XdsTestClient, test_server: XdsTestServer
    ) -> Tuple[grpc_channelz.Socket, grpc_channelz.Socket]:
        client_sock = test_client.get_client_socket_with_test_server()
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
