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

from absl import flags
from absl.testing import absltest

from framework import xds_gamma_testcase
from framework import xds_k8s_testcase

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient


class GammaBaselineTest(xds_gamma_testcase.GammaXdsKubernetesTestCase):
    def test_ping_pong(self):
        with self.subTest("1_run_test_server"):
            test_server: _XdsTestServer = self.startTestServers()[0]

        with self.subTest("2_start_test_client"):
            test_client: _XdsTestClient = self.startTestClient(test_server)

        with self.subTest("3_test_server_received_rpcs_from_test_client"):
            self.assertSuccessfulRpcs(test_client)


if __name__ == "__main__":
    absltest.main(failfast=True)
