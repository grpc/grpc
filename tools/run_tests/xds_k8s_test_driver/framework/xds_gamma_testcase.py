# Copyright 2023 gRPC authors.
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

from framework.helpers.rand import rand_string
from framework.infrastructure import k8s
import framework.infrastructure.traffic_director_gamma as td_gamma
from framework.test_app import client_app
from framework.test_app import server_app
from framework.test_app.runners.k8s import gamma_server_runner
from framework.test_app.runners.k8s import k8s_xds_client_runner
import framework.xds_k8s_testcase as xds_k8s_testcase

GammaServerRunner = gamma_server_runner.GammaServerRunner
KubernetesClientRunner = k8s_xds_client_runner.KubernetesClientRunner
XdsTestClient = client_app.XdsTestClient
XdsTestServer = server_app.XdsTestServer

logger = logging.getLogger(__name__)


# TODO(sergiitk): [GAMMA] Move into framework/test_cases
class GammaXdsKubernetesTestCase(xds_k8s_testcase.RegularXdsKubernetesTestCase):
    server_runner: GammaServerRunner
    frontend_service_name: str

    def setUp(self):
        """Hook method for setting up the test fixture before exercising it."""
        # TODO(sergiitk): [GAMMA] Remove when refactored to be TD-manager-less.
        # pylint: disable=bad-super-call
        # Skips RegularXdsKubernetesTestCase and IsolatedXdsKubernetesTestCase
        # and calls setUp on XdsKubernetesBaseTestCase.
        # IsolatedXdsKubernetesTestCase randomizes server_xds_port when it's 0,
        # and in GAMMA we always need it unset.
        # Calls XdsKubernetesBaseTestCase.setUp():
        super(xds_k8s_testcase.IsolatedXdsKubernetesTestCase, self).setUp()
        # pylint: enable=bad-super-call

        # Random suffix per test.
        self.createRandomSuffix()

        # TODO(sergiitk): [GAMMA] Make a TD-manager-less base test case
        # TD Manager
        self.td = self.initTrafficDirectorManager()

        # Generate unique mesh name too.
        unique = rand_string()
        self.frontend_service_name = f"{self.resource_prefix}-{self.resource_suffix.lower()}"

        # Test Server runner
        self.server_namespace = GammaServerRunner.make_namespace_name(
            self.resource_prefix, self.resource_suffix
        )
        self.server_runner = self.initKubernetesServerRunner()

        # Test Client runner
        self.client_namespace = KubernetesClientRunner.make_namespace_name(
            self.resource_prefix, self.resource_suffix
        )
        self.client_runner = self.initKubernetesClientRunner()

        # Cleanup.
        self.force_cleanup = True
        self.force_cleanup_namespace = True

    # TODO(sergiitk): [GAMMA] Make a TD-manager-less base test case
    def initTrafficDirectorManager(
        self,
    ) -> td_gamma.TrafficDirectorGammaManager:
        return td_gamma.TrafficDirectorGammaManager(
            self.gcp_api_manager,
            project=self.project,
            resource_prefix=self.resource_prefix,
            resource_suffix=self.resource_suffix,
            network=self.network,
            compute_api_version=self.compute_api_version,
        )

    def initKubernetesServerRunner(self) -> GammaServerRunner:
        return GammaServerRunner(
            k8s.KubernetesNamespace(
                self.k8s_api_manager, self.server_namespace
            ),
            self.frontend_service_name,
            deployment_name=self.server_name,
            image_name=self.server_image,
            td_bootstrap_image=self.td_bootstrap_image,
            gcp_project=self.project,
            gcp_api_manager=self.gcp_api_manager,
            gcp_service_account=self.gcp_service_account,
            xds_server_uri=self.xds_server_uri,
            network=self.network,
            debug_use_port_forwarding=self.debug_use_port_forwarding,
            enable_workload_identity=self.enable_workload_identity,
        )

    def startTestClient(
        self, test_server: XdsTestServer, **kwargs
    ) -> XdsTestClient:
        server_target =f"xds:///{self.frontend_service_name}.svc.cluster.local:8080"
        return super().startTestClient(
            test_server, generate_mesh_id=True,
            server_target=server_target
        )
