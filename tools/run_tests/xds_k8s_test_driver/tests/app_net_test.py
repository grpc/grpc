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

from framework.infrastructure.gcp import network_services

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

class AppNetTest(xds_k8s_testcase.AppNetXdsKubernetesTestCase):

    def test_ping_pong(self):
        self.td.create_health_check()
        self.td.create_backend_service()
        self.td.create_grpc_route(self.server_xds_host, self.server_xds_port)
        self.td.create_router()
        test_server: _XdsTestServer = self.startTestServer()
        self.setupServerBackends()
        test_client: _XdsTestClient = self.startTestClient(test_server)
        self.assertXdsConfigExists(test_client)
        self.assertSuccessfulRpcs(test_client)

if __name__ == '__main__':
    absltest.main(failfast=True)
