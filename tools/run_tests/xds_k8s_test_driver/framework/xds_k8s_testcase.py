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
import abc
import contextlib
import datetime
import enum
import hashlib
import logging
import re
import signal
import time
from types import FrameType
from typing import Any, Callable, List, Optional, Tuple, Union

from absl import flags
from absl.testing import absltest
from google.protobuf import json_format
import grpc

from framework import xds_flags
from framework import xds_k8s_flags
from framework import xds_url_map_testcase
from framework.helpers import grpc as helpers_grpc
from framework.helpers import rand as helpers_rand
from framework.helpers import retryers
from framework.helpers import skips
from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.infrastructure import traffic_director
from framework.rpc import grpc_channelz
from framework.rpc import grpc_csds
from framework.rpc import grpc_testing
from framework.test_app import client_app
from framework.test_app import server_app
from framework.test_app.runners.k8s import k8s_xds_client_runner
from framework.test_app.runners.k8s import k8s_xds_server_runner

logger = logging.getLogger(__name__)
# TODO(yashkt): We will no longer need this flag once Core exposes local certs
# from channelz
_CHECK_LOCAL_CERTS = flags.DEFINE_bool(
    "check_local_certs",
    default=True,
    help="Security Tests also check the value of local certs")
flags.adopt_module_key_flags(xds_flags)
flags.adopt_module_key_flags(xds_k8s_flags)

# Type aliases
TrafficDirectorManager = traffic_director.TrafficDirectorManager
TrafficDirectorAppNetManager = traffic_director.TrafficDirectorAppNetManager
TrafficDirectorSecureManager = traffic_director.TrafficDirectorSecureManager
XdsTestServer = server_app.XdsTestServer
XdsTestClient = client_app.XdsTestClient
KubernetesServerRunner = k8s_xds_server_runner.KubernetesServerRunner
KubernetesClientRunner = k8s_xds_client_runner.KubernetesClientRunner
LoadBalancerStatsResponse = grpc_testing.LoadBalancerStatsResponse
_ChannelState = grpc_channelz.ChannelState
_timedelta = datetime.timedelta
ClientConfig = grpc_csds.ClientConfig
# pylint complains about signal.Signals for some reason.
_SignalNum = Union[int, signal.Signals]  # pylint: disable=no-member
_SignalHandler = Callable[[_SignalNum, Optional[FrameType]], Any]

_TD_CONFIG_MAX_WAIT_SEC = 600


class TdPropagationRetryableError(Exception):
    """Indicates that TD config hasn't propagated yet, and it's safe to retry"""


class XdsKubernetesBaseTestCase(absltest.TestCase):
    lang_spec: skips.TestConfig
    client_namespace: str
    client_runner: KubernetesClientRunner
    ensure_firewall: bool
    force_cleanup: bool
    gcp_api_manager: gcp.api.GcpApiManager
    gcp_service_account: Optional[str]
    k8s_api_manager: k8s.KubernetesApiManager
    secondary_k8s_api_manager: k8s.KubernetesApiManager
    network: str
    project: str
    resource_prefix: str
    resource_suffix: str = ''
    # Whether to randomize resources names for each test by appending a
    # unique suffix.
    resource_suffix_randomize: bool = True
    server_maintenance_port: Optional[int]
    server_namespace: str
    server_runner: KubernetesServerRunner
    server_xds_host: str
    server_xds_port: int
    td: TrafficDirectorManager
    td_bootstrap_image: str
    _prev_sigint_handler: Optional[_SignalHandler] = None
    _handling_sigint: bool = False

    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        """Overridden by the test class to decide if the config is supported.

        Returns:
          A bool indicates if the given config is supported.
        """
        del config
        return True

    @classmethod
    def setUpClass(cls):
        """Hook method for setting up class fixture before running tests in
        the class.
        """
        logger.info('----- Testing %s -----', cls.__name__)
        logger.info('Logs timezone: %s', time.localtime().tm_zone)

        # Raises unittest.SkipTest if given client/server/version does not
        # support current test case.
        cls.lang_spec = skips.evaluate_test_config(cls.is_supported)

        # Must be called before KubernetesApiManager or GcpApiManager init.
        xds_flags.set_socket_default_timeout_from_flag()

        # GCP
        cls.project = xds_flags.PROJECT.value
        cls.network = xds_flags.NETWORK.value
        cls.gcp_service_account = xds_k8s_flags.GCP_SERVICE_ACCOUNT.value
        cls.td_bootstrap_image = xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value
        cls.xds_server_uri = xds_flags.XDS_SERVER_URI.value
        cls.ensure_firewall = xds_flags.ENSURE_FIREWALL.value
        cls.firewall_allowed_ports = xds_flags.FIREWALL_ALLOWED_PORTS.value
        cls.compute_api_version = xds_flags.COMPUTE_API_VERSION.value

        # Resource names.
        cls.resource_prefix = xds_flags.RESOURCE_PREFIX.value
        if xds_flags.RESOURCE_SUFFIX.value is not None:
            cls.resource_suffix_randomize = False
            cls.resource_suffix = xds_flags.RESOURCE_SUFFIX.value

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
        cls.force_cleanup = xds_flags.FORCE_CLEANUP.value
        cls.debug_use_port_forwarding = \
            xds_k8s_flags.DEBUG_USE_PORT_FORWARDING.value
        cls.enable_workload_identity = \
            xds_k8s_flags.ENABLE_WORKLOAD_IDENTITY.value
        cls.check_local_certs = _CHECK_LOCAL_CERTS.value

        # Resource managers
        cls.k8s_api_manager = k8s.KubernetesApiManager(
            xds_k8s_flags.KUBE_CONTEXT.value)
        cls.secondary_k8s_api_manager = k8s.KubernetesApiManager(
            xds_k8s_flags.SECONDARY_KUBE_CONTEXT.value)
        cls.gcp_api_manager = gcp.api.GcpApiManager()

    @classmethod
    def tearDownClass(cls):
        cls.k8s_api_manager.close()
        cls.secondary_k8s_api_manager.close()
        cls.gcp_api_manager.close()

    def setUp(self):
        self._prev_sigint_handler = signal.signal(signal.SIGINT,
                                                  self.handle_sigint)

    def handle_sigint(self, signalnum: _SignalNum,
                      frame: Optional[FrameType]) -> None:
        logger.info('Caught Ctrl+C, cleaning up...')
        self._handling_sigint = True
        # Force resource cleanup by their name. Addresses the case where ctrl-c
        # is pressed while waiting for the resource creation.
        self.force_cleanup = True
        self.tearDown()
        self.tearDownClass()
        self._handling_sigint = False
        if self._prev_sigint_handler is not None:
            signal.signal(signal.SIGINT, self._prev_sigint_handler)
        raise KeyboardInterrupt

    @contextlib.contextmanager
    def subTest(self, msg, **params):  # noqa pylint: disable=signature-differs
        logger.info('--- Starting subTest %s.%s ---', self.id(), msg)
        try:
            yield super().subTest(msg, **params)
        finally:
            if not self._handling_sigint:
                logger.info('--- Finished subTest %s.%s ---', self.id(), msg)

    def setupTrafficDirectorGrpc(self):
        self.td.setup_for_grpc(self.server_xds_host,
                               self.server_xds_port,
                               health_check_port=self.server_maintenance_port)

    def setupServerBackends(self,
                            *,
                            wait_for_healthy_status=True,
                            server_runner=None,
                            max_rate_per_endpoint: Optional[int] = None):
        if server_runner is None:
            server_runner = self.server_runner
        # Load Backends
        neg_name, neg_zones = server_runner.k8s_namespace.get_service_neg(
            server_runner.service_name, self.server_port)

        # Add backends to the Backend Service
        self.td.backend_service_add_neg_backends(
            neg_name, neg_zones, max_rate_per_endpoint=max_rate_per_endpoint)
        if wait_for_healthy_status:
            self.td.wait_for_backends_healthy_status()

    def removeServerBackends(self, *, server_runner=None):
        if server_runner is None:
            server_runner = self.server_runner
        # Load Backends
        neg_name, neg_zones = server_runner.k8s_namespace.get_service_neg(
            server_runner.service_name, self.server_port)

        # Remove backends from the Backend Service
        self.td.backend_service_remove_neg_backends(neg_name, neg_zones)

    def assertSuccessfulRpcs(self,
                             test_client: XdsTestClient,
                             num_rpcs: int = 100):
        lb_stats = self.getClientRpcStats(test_client, num_rpcs)
        self.assertAllBackendsReceivedRpcs(lb_stats)
        failed = int(lb_stats.num_failures)
        self.assertLessEqual(
            failed,
            0,
            msg=f'Expected all RPCs to succeed: {failed} of {num_rpcs} failed')

    @staticmethod
    def diffAccumulatedStatsPerMethod(
        before: grpc_testing.LoadBalancerAccumulatedStatsResponse,
        after: grpc_testing.LoadBalancerAccumulatedStatsResponse
    ) -> grpc_testing.LoadBalancerAccumulatedStatsResponse:
        """Only diffs stats_per_method, as the other fields are deprecated."""
        diff = grpc_testing.LoadBalancerAccumulatedStatsResponse()
        for method, method_stats in after.stats_per_method.items():
            for status, count in method_stats.result.items():
                count -= before.stats_per_method[method].result[status]
                if count < 0:
                    raise AssertionError("Diff of count shouldn't be negative")
                if count > 0:
                    diff.stats_per_method[method].result[status] = count
        return diff

    def assertRpcStatusCodes(self,
                             test_client: XdsTestClient,
                             *,
                             expected_status: grpc.StatusCode,
                             duration: _timedelta,
                             method: str,
                             stray_rpc_limit: int = 0) -> None:
        """Assert all RPCs for a method are completing with a certain status."""
        # Sending with pre-set QPS for a period of time
        before_stats = test_client.get_load_balancer_accumulated_stats()
        response_type = 'LoadBalancerAccumulatedStatsResponse'
        logging.info('Received %s from test client %s: before:\n%s',
                     response_type, test_client.hostname, before_stats)
        time.sleep(duration.total_seconds())
        after_stats = test_client.get_load_balancer_accumulated_stats()
        logging.info('Received %s from test client %s: after:\n%s',
                     response_type, test_client.hostname, after_stats)

        diff_stats = self.diffAccumulatedStatsPerMethod(before_stats,
                                                        after_stats)
        stats = diff_stats.stats_per_method[method]
        for found_status_int, count in stats.result.items():
            found_status = helpers_grpc.status_from_int(found_status_int)
            if found_status != expected_status and count > stray_rpc_limit:
                self.fail(f"Expected only status"
                          f" {helpers_grpc.status_pretty(expected_status)},"
                          " but found status"
                          f" {helpers_grpc.status_pretty(found_status)}"
                          f" for method {method}:\n{diff_stats}")

        expected_status_int: int = expected_status.value[0]
        self.assertGreater(
            stats.result[expected_status_int],
            0,
            msg=("Expected non-zero RPCs with status"
                 f" {helpers_grpc.status_pretty(expected_status)}"
                 f" for method {method}, got:\n{diff_stats}"))

    def assertRpcsEventuallyGoToGivenServers(self,
                                             test_client: XdsTestClient,
                                             servers: List[XdsTestServer],
                                             num_rpcs: int = 100):
        retryer = retryers.constant_retryer(
            wait_fixed=datetime.timedelta(seconds=1),
            timeout=datetime.timedelta(seconds=_TD_CONFIG_MAX_WAIT_SEC),
            log_level=logging.INFO)
        try:
            retryer(self._assertRpcsEventuallyGoToGivenServers, test_client,
                    servers, num_rpcs)
        except retryers.RetryError as retry_error:
            logger.exception(
                'Rpcs did not go to expected servers before timeout %s',
                _TD_CONFIG_MAX_WAIT_SEC)
            raise retry_error

    def _assertRpcsEventuallyGoToGivenServers(self, test_client: XdsTestClient,
                                              servers: List[XdsTestServer],
                                              num_rpcs: int):
        server_hostnames = [server.hostname for server in servers]
        logger.info('Verifying RPCs go to servers %s', server_hostnames)
        lb_stats = self.getClientRpcStats(test_client, num_rpcs)
        failed = int(lb_stats.num_failures)
        self.assertLessEqual(
            failed,
            0,
            msg=f'Expected all RPCs to succeed: {failed} of {num_rpcs} failed')
        for server_hostname in server_hostnames:
            self.assertIn(server_hostname, lb_stats.rpcs_by_peer,
                          f'Server {server_hostname} did not receive RPCs')
        for server_hostname in lb_stats.rpcs_by_peer.keys():
            self.assertIn(server_hostname, server_hostnames,
                          f'Unexpected server {server_hostname} received RPCs')

    def assertXdsConfigExists(self, test_client: XdsTestClient):
        config = test_client.csds.fetch_client_status(log_level=logging.INFO)
        self.assertIsNotNone(config)
        seen = set()
        want = frozenset([
            'listener_config',
            'cluster_config',
            'route_config',
            'endpoint_config',
        ])
        for xds_config in config.xds_config:
            seen.add(xds_config.WhichOneof('per_xds_config'))
        for generic_xds_config in config.generic_xds_configs:
            if re.search(r'\.Listener$', generic_xds_config.type_url):
                seen.add('listener_config')
            elif re.search(r'\.RouteConfiguration$',
                           generic_xds_config.type_url):
                seen.add('route_config')
            elif re.search(r'\.Cluster$', generic_xds_config.type_url):
                seen.add('cluster_config')
            elif re.search(r'\.ClusterLoadAssignment$',
                           generic_xds_config.type_url):
                seen.add('endpoint_config')
        logger.debug('Received xDS config dump: %s',
                     json_format.MessageToJson(config, indent=2))
        self.assertSameElements(want, seen)

    def assertRouteConfigUpdateTrafficHandoff(
            self, test_client: XdsTestClient,
            previous_route_config_version: str, retry_wait_second: int,
            timeout_second: int):
        retryer = retryers.constant_retryer(
            wait_fixed=datetime.timedelta(seconds=retry_wait_second),
            timeout=datetime.timedelta(seconds=timeout_second),
            retry_on_exceptions=(TdPropagationRetryableError,),
            logger=logger,
            log_level=logging.INFO)
        try:
            for attempt in retryer:
                with attempt:
                    self.assertSuccessfulRpcs(test_client)
                    raw_config = test_client.csds.fetch_client_status(
                        log_level=logging.INFO)
                    dumped_config = xds_url_map_testcase.DumpedXdsConfig(
                        json_format.MessageToDict(raw_config))
                    route_config_version = dumped_config.rds_version
                    if previous_route_config_version == route_config_version:
                        logger.info(
                            'Routing config not propagated yet. Retrying.')
                        raise TdPropagationRetryableError(
                            "CSDS not get updated routing config corresponding"
                            " to the second set of url maps")
                    else:
                        self.assertSuccessfulRpcs(test_client)
                        logger.info(
                            ('[SUCCESS] Confirmed successful RPC with the '
                             'updated routing config, version=%s'),
                            route_config_version)
        except retryers.RetryError as retry_error:
            logger.info(
                ('Retry exhausted. TD routing config propagation failed after '
                 'timeout %ds. Last seen client config dump: %s'),
                timeout_second, dumped_config)
            raise retry_error

    def assertFailedRpcs(self,
                         test_client: XdsTestClient,
                         num_rpcs: Optional[int] = 100):
        lb_stats = self.getClientRpcStats(test_client, num_rpcs)
        failed = int(lb_stats.num_failures)
        self.assertEqual(
            failed,
            num_rpcs,
            msg=f'Expected all RPCs to fail: {failed} of {num_rpcs} failed')

    @staticmethod
    def getClientRpcStats(test_client: XdsTestClient,
                          num_rpcs: int) -> LoadBalancerStatsResponse:
        lb_stats = test_client.get_load_balancer_stats(num_rpcs=num_rpcs)
        logger.info(
            'Received LoadBalancerStatsResponse from test client %s:\n%s',
            test_client.hostname, lb_stats)
        return lb_stats

    def assertAllBackendsReceivedRpcs(self, lb_stats):
        # TODO(sergiitk): assert backends length
        for backend, rpcs_count in lb_stats.rpcs_by_peer.items():
            self.assertGreater(
                int(rpcs_count),
                0,
                msg=f'Backend {backend} did not receive a single RPC')


class IsolatedXdsKubernetesTestCase(XdsKubernetesBaseTestCase,
                                    metaclass=abc.ABCMeta):
    """Isolated test case.

    Base class for tests cases where infra resources are created before
    each test, and destroyed after.
    """

    def setUp(self):
        """Hook method for setting up the test fixture before exercising it."""
        super().setUp()

        if self.resource_suffix_randomize:
            self.resource_suffix = helpers_rand.random_resource_suffix()
        logger.info('Test run resource prefix: %s, suffix: %s',
                    self.resource_prefix, self.resource_suffix)

        # TD Manager
        self.td = self.initTrafficDirectorManager()

        # Test Server runner
        self.server_namespace = KubernetesServerRunner.make_namespace_name(
            self.resource_prefix, self.resource_suffix)
        self.server_runner = self.initKubernetesServerRunner()

        # Test Client runner
        self.client_namespace = KubernetesClientRunner.make_namespace_name(
            self.resource_prefix, self.resource_suffix)
        self.client_runner = self.initKubernetesClientRunner()

        # Ensures the firewall exist
        if self.ensure_firewall:
            self.td.create_firewall_rule(
                allowed_ports=self.firewall_allowed_ports)

        # Randomize xds port, when it's set to 0
        if self.server_xds_port == 0:
            # TODO(sergiitk): this is prone to race conditions:
            #  The port might not me taken now, but there's not guarantee
            #  it won't be taken until the tests get to creating
            #  forwarding rule. This check is better than nothing,
            #  but we should find a better approach.
            self.server_xds_port = self.td.find_unused_forwarding_rule_port()
            logger.info('Found unused xds port: %s', self.server_xds_port)

    @abc.abstractmethod
    def initTrafficDirectorManager(self) -> TrafficDirectorManager:
        raise NotImplementedError

    @abc.abstractmethod
    def initKubernetesServerRunner(self) -> KubernetesServerRunner:
        raise NotImplementedError

    @abc.abstractmethod
    def initKubernetesClientRunner(self) -> KubernetesClientRunner:
        raise NotImplementedError

    def tearDown(self):
        logger.info('----- TestMethod %s teardown -----', self.id())
        logger.debug('Getting pods restart times')
        client_restarts: int = 0
        server_restarts: int = 0
        try:
            client_restarts = self.client_runner.get_pod_restarts(
                self.client_runner.deployment)
            server_restarts = self.server_runner.get_pod_restarts(
                self.server_runner.deployment)
        except (retryers.RetryError, k8s.NotFound) as e:
            logger.exception(e)

        retryer = retryers.constant_retryer(wait_fixed=_timedelta(seconds=10),
                                            attempts=3,
                                            log_level=logging.INFO)
        try:
            retryer(self.cleanup)
        except retryers.RetryError:
            logger.exception('Got error during teardown')
        finally:
            logger.info('----- Test client/server logs -----')
            self.client_runner.logs_explorer_link()
            self.server_runner.logs_explorer_link()

            # Fail if any of the pods restarted.
            self.assertEqual(
                client_restarts,
                0,
                msg=
                ('Client pods unexpectedly restarted'
                 f' {client_restarts} times during test.'
                 ' In most cases, this is caused by the test client app crash.'
                ))
            self.assertEqual(
                server_restarts,
                0,
                msg=
                ('Server pods unexpectedly restarted'
                 f' {server_restarts} times during test.'
                 ' In most cases, this is caused by the test client app crash.'
                ))

    def cleanup(self):
        self.td.cleanup(force=self.force_cleanup)
        self.client_runner.cleanup(force=self.force_cleanup)
        self.server_runner.cleanup(force=self.force_cleanup,
                                   force_namespace=self.force_cleanup)


class RegularXdsKubernetesTestCase(IsolatedXdsKubernetesTestCase):
    """Regular test case base class for testing PSM features in isolation."""

    @classmethod
    def setUpClass(cls):
        """Hook method for setting up class fixture before running tests in
        the class.
        """
        super().setUpClass()
        if cls.server_maintenance_port is None:
            cls.server_maintenance_port = \
                KubernetesServerRunner.DEFAULT_MAINTENANCE_PORT

    def initTrafficDirectorManager(self) -> TrafficDirectorManager:
        return TrafficDirectorManager(
            self.gcp_api_manager,
            project=self.project,
            resource_prefix=self.resource_prefix,
            resource_suffix=self.resource_suffix,
            network=self.network,
            compute_api_version=self.compute_api_version)

    def initKubernetesServerRunner(self) -> KubernetesServerRunner:
        return KubernetesServerRunner(
            k8s.KubernetesNamespace(self.k8s_api_manager,
                                    self.server_namespace),
            deployment_name=self.server_name,
            image_name=self.server_image,
            td_bootstrap_image=self.td_bootstrap_image,
            gcp_project=self.project,
            gcp_api_manager=self.gcp_api_manager,
            gcp_service_account=self.gcp_service_account,
            xds_server_uri=self.xds_server_uri,
            network=self.network,
            debug_use_port_forwarding=self.debug_use_port_forwarding,
            enable_workload_identity=self.enable_workload_identity)

    def initKubernetesClientRunner(self) -> KubernetesClientRunner:
        return KubernetesClientRunner(
            k8s.KubernetesNamespace(self.k8s_api_manager,
                                    self.client_namespace),
            deployment_name=self.client_name,
            image_name=self.client_image,
            td_bootstrap_image=self.td_bootstrap_image,
            gcp_project=self.project,
            gcp_api_manager=self.gcp_api_manager,
            gcp_service_account=self.gcp_service_account,
            xds_server_uri=self.xds_server_uri,
            network=self.network,
            debug_use_port_forwarding=self.debug_use_port_forwarding,
            enable_workload_identity=self.enable_workload_identity,
            stats_port=self.client_port,
            reuse_namespace=self.server_namespace == self.client_namespace)

    def startTestServers(self,
                         replica_count=1,
                         server_runner=None,
                         **kwargs) -> List[XdsTestServer]:
        if server_runner is None:
            server_runner = self.server_runner
        test_servers = server_runner.run(
            replica_count=replica_count,
            test_port=self.server_port,
            maintenance_port=self.server_maintenance_port,
            **kwargs)
        for test_server in test_servers:
            test_server.set_xds_address(self.server_xds_host,
                                        self.server_xds_port)
        return test_servers

    def startTestClient(self, test_server: XdsTestServer,
                        **kwargs) -> XdsTestClient:
        test_client = self.client_runner.run(server_target=test_server.xds_uri,
                                             **kwargs)
        test_client.wait_for_active_server_channel()
        return test_client


class AppNetXdsKubernetesTestCase(RegularXdsKubernetesTestCase):
    td: TrafficDirectorAppNetManager

    def initTrafficDirectorManager(self) -> TrafficDirectorAppNetManager:
        return TrafficDirectorAppNetManager(
            self.gcp_api_manager,
            project=self.project,
            resource_prefix=self.resource_prefix,
            resource_suffix=self.resource_suffix,
            network=self.network,
            compute_api_version=self.compute_api_version)


class SecurityXdsKubernetesTestCase(IsolatedXdsKubernetesTestCase):
    """Test case base class for testing PSM security features in isolation."""
    td: TrafficDirectorSecureManager

    class SecurityMode(enum.Enum):
        MTLS = enum.auto()
        TLS = enum.auto()
        PLAINTEXT = enum.auto()

    @classmethod
    def setUpClass(cls):
        """Hook method for setting up class fixture before running tests in
        the class.
        """
        super().setUpClass()
        if cls.server_maintenance_port is None:
            # In secure mode, the maintenance port is different from
            # the test port to keep it insecure, and make
            # Health Checks and Channelz tests available.
            # When not provided, use explicit numeric port value, so
            # Backend Health Checks are created on a fixed port.
            cls.server_maintenance_port = \
                KubernetesServerRunner.DEFAULT_SECURE_MODE_MAINTENANCE_PORT

    def initTrafficDirectorManager(self) -> TrafficDirectorSecureManager:
        return TrafficDirectorSecureManager(
            self.gcp_api_manager,
            project=self.project,
            resource_prefix=self.resource_prefix,
            resource_suffix=self.resource_suffix,
            network=self.network,
            compute_api_version=self.compute_api_version)

    def initKubernetesServerRunner(self) -> KubernetesServerRunner:
        return KubernetesServerRunner(
            k8s.KubernetesNamespace(self.k8s_api_manager,
                                    self.server_namespace),
            deployment_name=self.server_name,
            image_name=self.server_image,
            td_bootstrap_image=self.td_bootstrap_image,
            gcp_project=self.project,
            gcp_api_manager=self.gcp_api_manager,
            gcp_service_account=self.gcp_service_account,
            network=self.network,
            xds_server_uri=self.xds_server_uri,
            deployment_template='server-secure.deployment.yaml',
            debug_use_port_forwarding=self.debug_use_port_forwarding)

    def initKubernetesClientRunner(self) -> KubernetesClientRunner:
        return KubernetesClientRunner(
            k8s.KubernetesNamespace(self.k8s_api_manager,
                                    self.client_namespace),
            deployment_name=self.client_name,
            image_name=self.client_image,
            td_bootstrap_image=self.td_bootstrap_image,
            gcp_project=self.project,
            gcp_api_manager=self.gcp_api_manager,
            gcp_service_account=self.gcp_service_account,
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
            **kwargs)[0]
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
