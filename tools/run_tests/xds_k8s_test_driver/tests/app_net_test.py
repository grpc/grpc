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

from absl import flags
from absl.testing import absltest

from framework import xds_k8s_testcase

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient


class AppNetTest(xds_k8s_testcase.AppNetXdsKubernetesTestCase):

    def test_ping_pong(self):
        with self.subTest('0_create_health_check'):
            self.td.create_health_check()

        with self.subTest('1_create_backend_service'):
            self.td.create_backend_service()

        with self.subTest('2_create_grpc_route'):
            self.td.create_grpc_route(self.server_xds_host,
                                      self.server_xds_port)

        with self.subTest('3_create_router'):
            self.td.create_router()

        with self.subTest('4_start_test_server'):
            test_server: _XdsTestServer = self.startTestServers(
                replica_count=1)[0]

        with self.subTest('5_setup_server_backends'):
            self.setupServerBackends()

        with self.subTest('6_start_test_client'):
            test_client: _XdsTestClient = self.startTestClient(test_server)

        with self.subTest('7_assert_xds_config_exists'):
            self.assertXdsConfigExists(test_client)

        with self.subTest('8_assert_successful_rpcs'):
            self.assertSuccessfulRpcs(test_client)


if __name__ == '__main__':
    absltest.main(failfast=True)
