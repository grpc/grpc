# Copyright 2021 gRPC authors.
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
from typing import List

from absl import flags
from absl.testing import absltest

from framework import xds_k8s_testcase
from framework.infrastructure import k8s
from framework.test_app.runners.k8s import k8s_xds_server_runner

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
_KubernetesServerRunner = k8s_xds_server_runner.KubernetesServerRunner


class RemoveNegTest(xds_k8s_testcase.RegularXdsKubernetesTestCase):
    def setUp(self):
        super().setUp()
        self.alternate_server_runner = _KubernetesServerRunner(
            k8s.KubernetesNamespace(
                self.k8s_api_manager, self.server_namespace
            ),
            deployment_name=self.server_name + "-alt",
            image_name=self.server_image,
            gcp_service_account=self.gcp_service_account,
            td_bootstrap_image=self.td_bootstrap_image,
            gcp_project=self.project,
            gcp_api_manager=self.gcp_api_manager,
            xds_server_uri=self.xds_server_uri,
            network=self.network,
            debug_use_port_forwarding=self.debug_use_port_forwarding,
            reuse_namespace=True,
        )

    def cleanup(self):
        super().cleanup()
        if hasattr(self, "alternate_server_runner"):
            self.alternate_server_runner.cleanup(
                force=self.force_cleanup, force_namespace=self.force_cleanup
            )

    def test_remove_neg(self) -> None:
        with self.subTest("00_create_health_check"):
            self.td.create_health_check()

        with self.subTest("01_create_backend_services"):
            self.td.create_backend_service()

        with self.subTest("02_create_url_map"):
            self.td.create_url_map(self.server_xds_host, self.server_xds_port)

        with self.subTest("03_create_target_proxy"):
            self.td.create_target_proxy()

        with self.subTest("04_create_forwarding_rule"):
            self.td.create_forwarding_rule(self.server_xds_port)

        default_test_servers: List[_XdsTestServer]
        same_zone_test_servers: List[_XdsTestServer]
        with self.subTest("05_start_test_servers"):
            default_test_servers = self.startTestServers()
            same_zone_test_servers = self.startTestServers(
                server_runner=self.alternate_server_runner
            )

        with self.subTest("06_add_server_backends_to_backend_services"):
            self.setupServerBackends()
            self.setupServerBackends(server_runner=self.alternate_server_runner)

        test_client: _XdsTestClient
        with self.subTest("07_start_test_client"):
            test_client = self.startTestClient(default_test_servers[0])

        with self.subTest("08_test_client_xds_config_exists"):
            self.assertXdsConfigExists(test_client)

        with self.subTest("09_test_server_received_rpcs_from_test_client"):
            self.assertSuccessfulRpcs(test_client)

        with self.subTest("10_remove_neg"):
            self.assertRpcsEventuallyGoToGivenServers(
                test_client, default_test_servers + same_zone_test_servers
            )
            self.removeServerBackends(
                server_runner=self.alternate_server_runner
            )
            self.assertRpcsEventuallyGoToGivenServers(
                test_client, default_test_servers
            )


if __name__ == "__main__":
    absltest.main(failfast=True)
