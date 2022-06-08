# Copyright 2022 gRPC authors.
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
import logging

from absl import flags
from absl.testing import absltest
from absl.testing import parameterized

from framework import xds_k8s_testcase
from framework.helpers import rand as helpers_rand
from framework.helpers import retryers
from framework.helpers import skips
from framework.infrastructure import k8s
from framework.infrastructure import traffic_director
from framework.test_app import client_app
from framework.test_app import server_app

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
TrafficDirectorManager = traffic_director.TrafficDirectorManager
XdsTestServer = xds_k8s_testcase.XdsTestServer
XdsTestClient = xds_k8s_testcase.XdsTestClient
KubernetesServerRunner = server_app.KubernetesServerRunner
KubernetesClientRunner = client_app.KubernetesClientRunner
_timedelta = datetime.timedelta

class _BootstrapGeneratorBaseTest(xds_k8s_testcase.XdsKubernetesBaseTestCase):
    """Common functionality to support testing of bootstrap generator versions
    across gRPC clients and servers."""


    @classmethod
    def setUpClass(cls):
        """Hook method for setting up class fixture before running tests in
        the class.
        """
        super().setUpClass()
        if cls.server_maintenance_port is None:
            cls.server_maintenance_port = \
                KubernetesServerRunner.DEFAULT_MAINTENANCE_PORT

        # Bootstrap generator tests are run as parameterized tests which only
        # perform steps specific to the parameterized version of the bootstrap
        # generator under test.
        #
        # Here, we perform setup steps which are common across client and server
        # side variants of the bootstrap generator test.
        if cls._resource_suffix_randomize:
            cls.resource_suffix = helpers_rand.random_resource_suffix()
        logger.info('Test run resource prefix: %s, suffix: %s',
                    cls.resource_prefix, cls.resource_suffix)

        # TD Manager
        cls.td = cls.initTrafficDirectorManager()

        # Test namespaces for client and server.
        cls.server_namespace = KubernetesServerRunner.make_namespace_name(
            cls.resource_prefix, cls.resource_suffix)
        cls.client_namespace = KubernetesClientRunner.make_namespace_name(
            cls.resource_prefix, cls.resource_suffix)

        # Ensures the firewall exist
        if cls.ensure_firewall:
            cls.td.create_firewall_rule(
                allowed_ports=cls.firewall_allowed_ports)

        # Randomize xds port, when it's set to 0
        if cls.server_xds_port == 0:
            # TODO(sergiitk): this is prone to race conditions:
            #  The port might not me taken now, but there's not guarantee
            #  it won't be taken until the tests get to creating
            #  forwarding rule. This check is better than nothing,
            #  but we should find a better approach.
            cls.server_xds_port = cls.td.find_unused_forwarding_rule_port()
            logger.info('Found unused xds port: %s', cls.server_xds_port)

        # Common TD resources across client and server tests.
        cls.td.create_health_check()
        cls.td.create_backend_service()
        cls.td.create_url_map(cls.server_xds_host, cls.server_xds_port)
        cls.td.create_target_proxy()
        cls.td.create_forwarding_rule(cls.server_xds_port)


    @classmethod
    def tearDownClass(cls):
        cls.td.cleanup(force=cls.force_cleanup)
        super().tearDownClass()


    @classmethod
    def initTrafficDirectorManager(cls) -> TrafficDirectorManager:
        return TrafficDirectorManager(
            cls.gcp_api_manager,
            project=cls.project,
            resource_prefix=cls.resource_prefix,
            resource_suffix=cls.resource_suffix,
            network=cls.network,
            compute_api_version=cls.compute_api_version)


    @classmethod
    def initKubernetesServerRunner(cls, td_bootstrap_image=None) -> KubernetesServerRunner:
        if td_bootstrap_image == None:
            td_bootstrap_image = cls.td_bootstrap_image
        return KubernetesServerRunner(
            k8s.KubernetesNamespace(cls.k8s_api_manager,
                                    cls.server_namespace),
            deployment_name=cls.server_name,
            image_name=cls.server_image,
            td_bootstrap_image=td_bootstrap_image,
            gcp_project=cls.project,
            gcp_api_manager=cls.gcp_api_manager,
            gcp_service_account=cls.gcp_service_account,
            xds_server_uri=cls.xds_server_uri,
            network=cls.network,
            debug_use_port_forwarding=cls.debug_use_port_forwarding,
            enable_workload_identity=cls.enable_workload_identity)


    @staticmethod
    def startTestServer(server_runner,
                        port,
                        maintenance_port,
                        xds_host,
                        xds_port,
                        replica_count=1,
                        **kwargs) -> XdsTestServer:
        test_server = server_runner.run(
            replica_count=replica_count,
            test_port=port,
            maintenance_port=maintenance_port,
            **kwargs)[0]
        test_server.set_xds_address(xds_host, xds_port)
        return test_server


    def initKubernetesClientRunner(self, td_bootstrap_image=None) -> KubernetesClientRunner:
        if td_bootstrap_image == None:
            td_bootstrap_image = self.td_bootstrap_image
        return KubernetesClientRunner(
            k8s.KubernetesNamespace(self.k8s_api_manager,
                                    self.client_namespace),
            deployment_name=self.client_name,
            image_name=self.client_image,
            td_bootstrap_image=td_bootstrap_image,
            gcp_project=self.project,
            gcp_api_manager=self.gcp_api_manager,
            gcp_service_account=self.gcp_service_account,
            xds_server_uri=self.xds_server_uri,
            network=self.network,
            debug_use_port_forwarding=self.debug_use_port_forwarding,
            enable_workload_identity=self.enable_workload_identity,
            stats_port=self.client_port,
            reuse_namespace=self.server_namespace == self.client_namespace)


    def startTestClient(self, test_server: XdsTestServer,
                        **kwargs) -> XdsTestClient:
        test_client = self.client_runner.run(server_target=test_server.xds_uri,
                                             **kwargs)
        test_client.wait_for_active_server_channel()
        return test_client

class BootstrapGeneratorClientTest(_BootstrapGeneratorBaseTest, parameterized.TestCase):
    server_runner: KubernetesServerRunner
    test_server: XdsTestServer


    @classmethod
    def setUpClass(cls):
        """Hook method for setting up class fixture before running tests in
        the class.
        """
        super().setUpClass()

        # For client tests, we use a single server instance that can be shared
        # across all the parameterized clients. And this server runner will use
        # the version of the bootstrap generator as configured via the
        # --td_bootstrap_image flag.
        cls.server_runner = cls.initKubernetesServerRunner()
        cls.test_server = BootstrapGeneratorClientTest.startTestServer(
            server_runner=cls.server_runner,
            port=cls.server_port,
            maintenance_port=cls.server_maintenance_port,
            xds_host=cls.server_xds_host,
            xds_port=cls.server_xds_port)

        # Load backends.
        neg_name, neg_zones = cls.server_runner.k8s_namespace.get_service_neg(
            cls.server_runner.service_name, cls.server_port)

        # Add backends to the Backend Service.
        cls.td.backend_service_add_neg_backends(neg_name, neg_zones)
        cls.td.wait_for_backends_healthy_status()


    @classmethod
    def tearDownClass(cls):
        cls.server_runner.cleanup(force=cls.force_cleanup)
        super().tearDownClass()


    def tearDown(self):
        logger.info('----- TestMethod %s teardown -----', self.id())
        retryer = retryers.constant_retryer(wait_fixed=_timedelta(seconds=10),
                                            attempts=3,
                                            log_level=logging.INFO)
        try:
            retryer(self._cleanup)
        except retryers.RetryError:
            logger.exception('Got error during teardown')
        super().tearDown()


    def _cleanup(self):
        self.client_runner.cleanup(force=self.force_cleanup)

    @parameterized.named_parameters(
        # Add images corresponding to future releases here.
        #
        # TODO: Update bootstrap generator release instructions to add a newly
        # released version to this list.
        ('v0.14.0', 'gcr.io/grpc-testing/td-grpc-bootstrap:d6baaf7b0e0c63054ac4d9bedc09021ff261d599'),
        ('v0.13.0', 'gcr.io/grpc-testing/td-grpc-bootstrap:203db6ce70452996f4183c30dd4c5ecaada168b0'),
        ('v0.12.0', 'gcr.io/grpc-testing/td-grpc-bootstrap:8765051ef3b742bc5cd20f16de078ae7547f2ba2'),
        ('v0.11.0', 'gcr.io/grpc-testing/td-grpc-bootstrap:b96f7a73314668aee83cbf86ab1e40135a0542fc'),
        # v0.10.0 uses v2 xDS transport protocol by default. TD only supports v3
        # and we can force the bootstrap generator to emit config with v3
        # support by setting the --include-v3-features-experimental flag to
        # true.
        #
        # TODO: Figure out how to pass flags to the bootstrap generator via the
        # client and server runners, and uncomment this version.
        # ('v0.10.0', 'gcr.io/grpc-testing/td-grpc-bootstrap:66de7ea0e170351c9fae17232b81adbfb3e80ec3'),
    )
    def test_baseline_in_client_with_bootstrap_version(self, image):
        """Runs the baseline test for multiple versions of the bootstrap
        generator on the client.
        """
        self.client_runner = self.initKubernetesClientRunner(td_bootstrap_image=image)
        test_client: XdsTestClient = self.startTestClient(self.test_server)
        self.assertXdsConfigExists(test_client)
        self.assertSuccessfulRpcs(test_client)


# TODO: Use unique client and server deployment names while creating the
# corresponding runners, by suffixing the version of the bootstrap generator
# being tested. Then, run these in parallel.
class BootstrapGeneratorServerTest(_BootstrapGeneratorBaseTest, parameterized.TestCase):
    server_runner: KubernetesServerRunner
    client_runner: KubernetesClientRunner
    test_server: XdsTestServer


    def tearDown(self):
        logger.info('----- TestMethod %s teardown -----', self.id())
        retryer = retryers.constant_retryer(wait_fixed=_timedelta(seconds=10),
                                            attempts=3,
                                            log_level=logging.INFO)
        try:
            retryer(self._cleanup)
        except retryers.RetryError:
            logger.exception('Got error during teardown')
        super().tearDown()


    def _cleanup(self):
        self.client_runner.cleanup(force=self.force_cleanup)
        self.removeServerBackends()
        self.server_runner.cleanup(force=self.force_cleanup)


    @parameterized.named_parameters(
        # Add images corresponding to future releases here.
        #
        # TODO: Update bootstrap generator release instructions to add a newly
        # released version to this list.
        ('v0.14.0', 'gcr.io/grpc-testing/td-grpc-bootstrap:d6baaf7b0e0c63054ac4d9bedc09021ff261d599'),
        ('v0.13.0', 'gcr.io/grpc-testing/td-grpc-bootstrap:203db6ce70452996f4183c30dd4c5ecaada168b0'),
        ('v0.12.0', 'gcr.io/grpc-testing/td-grpc-bootstrap:8765051ef3b742bc5cd20f16de078ae7547f2ba2'),
        ('v0.11.0', 'gcr.io/grpc-testing/td-grpc-bootstrap:b96f7a73314668aee83cbf86ab1e40135a0542fc'),
        # v0.10.0 uses v2 xDS transport protocol by default. TD only supports v3
        # and we can force the bootstrap generator to emit config with v3
        # support by setting the --include-v3-features-experimental flag to
        # true.
        #
        # TODO: Figure out how to pass flags to the bootstrap generator via the
        # client and server runners, and uncomment this version.
        # ('v0.10.0', 'gcr.io/grpc-testing/td-grpc-bootstrap:66de7ea0e170351c9fae17232b81adbfb3e80ec3'),
    )
    def test_baseline_in_server_with_bootstrap_version(self, image):
        """Runs the baseline test for multiple versions of the bootstrap
        generator on the server.
        """
        self.server_runner = self.initKubernetesServerRunner(td_bootstrap_image=image)
        self.test_server = BootstrapGeneratorClientTest.startTestServer(
            server_runner=self.server_runner,
            port=self.server_port,
            maintenance_port=self.server_maintenance_port,
            xds_host=self.server_xds_host,
            xds_port=self.server_xds_port)

        # Load backends.
        neg_name, neg_zones = self.server_runner.k8s_namespace.get_service_neg(
            self.server_runner.service_name, self.server_port)

        # Add backends to the Backend Service.
        self.td.backend_service_add_neg_backends(neg_name, neg_zones)
        self.td.wait_for_backends_healthy_status()

        self.client_runner = self.initKubernetesClientRunner()
        test_client: XdsTestClient = self.startTestClient(self.test_server)
        self.assertXdsConfigExists(test_client)
        self.assertSuccessfulRpcs(test_client)


if __name__ == '__main__':
    absltest.main(failfast=True)
