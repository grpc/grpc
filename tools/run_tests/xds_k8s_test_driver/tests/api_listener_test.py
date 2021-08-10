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
from typing import List, Optional

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


class ApiListenerTest(xds_k8s_testcase.RegularXdsKubernetesTestCase):
    ALTERNATE_RESOURCE_SUFFIX = '2'

    def test_api_listener(self) -> None:
        with self.subTest('00_create_health_check'):
            self.td.create_health_check()

        with self.subTest('01_create_backend_services'):
            self.td.create_backend_service()

        with self.subTest('02_create_url_maps'):
            self.td.create_url_map(self.server_xds_host, self.server_xds_port)
            self.td.create_alternative_url_map(self.server_xds_host, self.server_xds_port)

        with self.subTest('03_create_target_proxies'):
            self.td.create_target_proxy()
            self.td.create_alternative_target_proxy()

        with self.subTest('04_create_forwarding_rule'):
            self.td.create_forwarding_rule(self.server_xds_port)
            self.td.create_alternative_forwarding_rule(self.server_xds_port, ip_address='10.10.10.10')

        with self.subTest('05_start_test_servers'):
            self.test_servers: List[_XdsTestServer] = self.startTestServers()

        with self.subTest('06_add_server_backends_to_backend_services'):
            self.setupServerBackends()

        with self.subTest('07_start_test_client'):
            self.test_client: _XdsTestClient = self.startTestClient(
                self.test_servers[0])

        with self.subTest('08_test_client_xds_config_exists'):
            self.assertXdsConfigExists(self.test_client)

        with self.subTest('09_test_server_received_rpcs_from_test_client'):
            self.assertSuccessfulRpcs(self.test_client)

        with self.subTest('10_delete_one_url_map_target_proxy_forwarding_rule'):
            self.td.delete_forwarding_rule()
            self.td.delete_target_grpc_proxy()
            self.td.delete_url_map()

        with self.subTest('11_test_server_continues_to_receive_rpcs'):
            # TODO: Use CSDS to check that deletions of the original forwarding
            # rule, target proxy, and URL map have propagated from TD to the
            # client instead of an unconditional wait.
            ATTEMPTS_TILL_PROPAGATION = 10
            for i in range(ATTEMPTS_TILL_PROPAGATION):
                self.assertSuccessfulRpcs(self.test_client)



if __name__ == '__main__':
    absltest.main(failfast=True)
