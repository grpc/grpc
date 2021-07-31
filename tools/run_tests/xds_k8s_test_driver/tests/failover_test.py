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
from framework.test_app import server_app

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient


class FailoverTest(xds_k8s_testcase.RegularXdsKubernetesTestCase):
    REPLICA_COUNT = 3
    MAX_RATE_PER_ENDPOINT = 100

    def setUp(self):
        super().setUp()
        self.secondary_server_runner = server_app.KubernetesServerRunner(
            k8s.KubernetesNamespace(self.secondary_k8s_api_manager,
                                    self.server_namespace),
            deployment_name=self.server_name + '-alt',
            image_name=self.server_image,
            gcp_service_account=self.gcp_service_account,
            td_bootstrap_image=self.td_bootstrap_image,
            gcp_project=self.project,
            gcp_api_manager=self.gcp_api_manager,
            xds_server_uri=self.xds_server_uri,
            network=self.network,
            debug_use_port_forwarding=self.debug_use_port_forwarding,
            reuse_namespace=True)

    def tearDown(self):
        if hasattr(self, 'secondary_server_runner'):
            self.secondary_server_runner.cleanup()
        super().tearDown()

    def test_failover(self) -> None:
        with self.subTest('00_create_health_check'):
            self.td.create_health_check()

        with self.subTest('01_create_backend_services'):
            self.td.create_backend_service()

        with self.subTest('02_create_url_map'):
            self.td.create_url_map(self.server_xds_host, self.server_xds_port)

        with self.subTest('03_create_target_proxy'):
            self.td.create_target_proxy()

        with self.subTest('04_create_forwarding_rule'):
            self.td.create_forwarding_rule(self.server_xds_port)

        with self.subTest('05_start_test_servers'):
            self.default_test_servers: List[
                _XdsTestServer] = self.startTestServers(
                    replica_count=self.REPLICA_COUNT)

            self.alternate_test_servers: List[
                _XdsTestServer] = self.startTestServers(
                    server_runner=self.secondary_server_runner)

        with self.subTest('06_add_server_backends_to_backend_services'):
            self.setupServerBackends(
                max_rate_per_endpoint=self.MAX_RATE_PER_ENDPOINT)
            self.setupServerBackends(
                server_runner=self.secondary_server_runner,
                max_rate_per_endpoint=self.MAX_RATE_PER_ENDPOINT)

        with self.subTest('07_start_test_client'):
            self.test_client: _XdsTestClient = self.startTestClient(
                self.default_test_servers[0])

        with self.subTest('08_test_client_xds_config_exists'):
            self.assertXdsConfigExists(self.test_client)

        with self.subTest('09_primary_locality_receives_requests'):
            self.assertRpcsEventuallyGoToGivenServers(self.test_client,
                                                      self.default_test_servers)

        with self.subTest(
                '10_secondary_locality_receives_no_requests_on_partial_primary_failure'
        ):
            self.default_test_servers[0].set_not_serving()
            self.assertRpcsEventuallyGoToGivenServers(
                self.test_client, self.default_test_servers[1:])

        with self.subTest('11_gentle_failover'):
            self.default_test_servers[1].set_not_serving()
            self.assertRpcsEventuallyGoToGivenServers(
                self.test_client,
                self.default_test_servers[2:] + self.alternate_test_servers)

        with self.subTest(
                '12_secondary_locality_receives_requests_on_primary_failure'):
            self.default_test_servers[2].set_not_serving()
            self.assertRpcsEventuallyGoToGivenServers(
                self.test_client, self.alternate_test_servers)

        with self.subTest('13_traffic_resumes_to_healthy_backends'):
            for i in range(self.REPLICA_COUNT):
                self.default_test_servers[i].set_serving()
            self.assertRpcsEventuallyGoToGivenServers(self.test_client,
                                                      self.default_test_servers)


if __name__ == '__main__':
    absltest.main(failfast=True)
