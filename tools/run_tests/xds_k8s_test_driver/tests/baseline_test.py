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
import logging

from absl import flags
from absl.testing import absltest

from framework import xds_k8s_testcase

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient


class BaselineTest(xds_k8s_testcase.RegularXdsKubernetesTestCase):

    def test_traffic_director_grpc_setup(self):
        with self.subTest('0_create_health_check'):
            self.td.create_health_check()

        with self.subTest('1_create_backend_service'):
            self.td.create_backend_service()

        with self.subTest('2_create_url_map'):
            self.td.create_url_map(self.server_xds_host, self.server_xds_port)

        with self.subTest('3_create_target_proxy'):
            self.td.create_target_proxy()

        with self.subTest('4_create_forwarding_rule'):
            self.td.create_forwarding_rule(self.server_xds_port)

        with self.subTest('5_start_test_server'):
            test_server: _XdsTestServer = self.startTestServer()

        with self.subTest('6_add_server_backends_to_backend_service'):
            self.setupServerBackends()

        with self.subTest('7_start_test_client'):
            test_client: _XdsTestClient = self.startTestClient(test_server)

        with self.subTest('8_test_server_received_rpcs_from_test_client'):
            self.assertSuccessfulRpcs(test_client)


if __name__ == '__main__':
    absltest.main(failfast=True)
