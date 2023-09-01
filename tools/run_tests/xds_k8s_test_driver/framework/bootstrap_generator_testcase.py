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
import logging
from typing import Optional

from framework import xds_k8s_testcase
from framework.helpers import rand as helpers_rand
from framework.infrastructure import k8s
from framework.infrastructure import traffic_director
from framework.test_app.runners.k8s import k8s_xds_client_runner
from framework.test_app.runners.k8s import k8s_xds_server_runner

logger = logging.getLogger(__name__)

# Type aliases
TrafficDirectorManager = traffic_director.TrafficDirectorManager
XdsTestServer = xds_k8s_testcase.XdsTestServer
XdsTestClient = xds_k8s_testcase.XdsTestClient
KubernetesServerRunner = k8s_xds_server_runner.KubernetesServerRunner
KubernetesClientRunner = k8s_xds_client_runner.KubernetesClientRunner


class BootstrapGeneratorBaseTest(xds_k8s_testcase.XdsKubernetesBaseTestCase):
    """Common functionality to support testing of bootstrap generator versions
    across gRPC clients and servers."""

    @classmethod
    def setUpClass(cls):
        """Hook method for setting up class fixture before running tests in
        the class.
        """
        super().setUpClass()
        if cls.server_maintenance_port is None:
            cls.server_maintenance_port = (
                KubernetesServerRunner.DEFAULT_MAINTENANCE_PORT
            )

        # Bootstrap generator tests are run as parameterized tests which only
        # perform steps specific to the parameterized version of the bootstrap
        # generator under test.
        #
        # Here, we perform setup steps which are common across client and server
        # side variants of the bootstrap generator test.
        if cls.resource_suffix_randomize:
            cls.resource_suffix = helpers_rand.random_resource_suffix()
        logger.info(
            "Test run resource prefix: %s, suffix: %s",
            cls.resource_prefix,
            cls.resource_suffix,
        )

        # TD Manager
        cls.td = cls.initTrafficDirectorManager()

        # Test namespaces for client and server.
        cls.server_namespace = KubernetesServerRunner.make_namespace_name(
            cls.resource_prefix, cls.resource_suffix
        )
        cls.client_namespace = KubernetesClientRunner.make_namespace_name(
            cls.resource_prefix, cls.resource_suffix
        )

        # Ensures the firewall exist
        if cls.ensure_firewall:
            cls.td.create_firewall_rule(
                allowed_ports=cls.firewall_allowed_ports
            )

        # Randomize xds port, when it's set to 0
        if cls.server_xds_port == 0:
            # TODO(sergiitk): this is prone to race conditions:
            #  The port might not me taken now, but there's not guarantee
            #  it won't be taken until the tests get to creating
            #  forwarding rule. This check is better than nothing,
            #  but we should find a better approach.
            cls.server_xds_port = cls.td.find_unused_forwarding_rule_port()
            logger.info("Found unused xds port: %s", cls.server_xds_port)

        # Common TD resources across client and server tests.
        cls.td.setup_for_grpc(
            cls.server_xds_host,
            cls.server_xds_port,
            health_check_port=cls.server_maintenance_port,
        )

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
            compute_api_version=cls.compute_api_version,
        )

    @classmethod
    def initKubernetesServerRunner(
        cls, *, td_bootstrap_image: Optional[str] = None
    ) -> KubernetesServerRunner:
        if not td_bootstrap_image:
            td_bootstrap_image = cls.td_bootstrap_image
        return KubernetesServerRunner(
            k8s.KubernetesNamespace(cls.k8s_api_manager, cls.server_namespace),
            deployment_name=cls.server_name,
            image_name=cls.server_image,
            td_bootstrap_image=td_bootstrap_image,
            gcp_project=cls.project,
            gcp_api_manager=cls.gcp_api_manager,
            gcp_service_account=cls.gcp_service_account,
            xds_server_uri=cls.xds_server_uri,
            network=cls.network,
            debug_use_port_forwarding=cls.debug_use_port_forwarding,
            enable_workload_identity=cls.enable_workload_identity,
        )

    @staticmethod
    def startTestServer(
        server_runner,
        port,
        maintenance_port,
        xds_host,
        xds_port,
        replica_count=1,
        **kwargs,
    ) -> XdsTestServer:
        test_server = server_runner.run(
            replica_count=replica_count,
            test_port=port,
            maintenance_port=maintenance_port,
            **kwargs,
        )[0]
        test_server.set_xds_address(xds_host, xds_port)
        return test_server

    def initKubernetesClientRunner(
        self, td_bootstrap_image: Optional[str] = None
    ) -> KubernetesClientRunner:
        if not td_bootstrap_image:
            td_bootstrap_image = self.td_bootstrap_image
        return KubernetesClientRunner(
            k8s.KubernetesNamespace(
                self.k8s_api_manager, self.client_namespace
            ),
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
            reuse_namespace=self.server_namespace == self.client_namespace,
        )

    def startTestClient(
        self, test_server: XdsTestServer, **kwargs
    ) -> XdsTestClient:
        test_client = self.client_runner.run(
            server_target=test_server.xds_uri, **kwargs
        )
        test_client.wait_for_active_server_channel()
        return test_client
