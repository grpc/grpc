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

    def test_failover(self) -> None:
        with self.subTest('00_create_health_check'):
            pass

        with self.subTest('01_create_backend_services'):
            pass

        with self.subTest('02_create_url_map'):
            pass

        with self.subTest('03_create_target_proxy'):
            pass

        with self.subTest('04_create_forwarding_rule'):
            pass

        with self.subTest('05_start_test_servers'):
            pass

        with self.subTest('06_add_server_backends_to_backend_services'):
            pass

        with self.subTest('07_start_test_client'):
            pass

        with self.subTest('08_test_client_xds_config_exists'):
            pass

        with self.subTest('09_primary_locality_receives_requests'):
            pass

        with self.subTest(
                '10_secondary_locality_receives_no_requests_on_partial_primary_failure'
        ):
            pass

        with self.subTest('11_gentle_failover'):
            pass

        with self.subTest(
                '12_secondary_locality_receives_requests_on_primary_failure'):
            pass

        with self.subTest('13_traffic_resumes_to_healthy_backends'):
            pass

    def test_failover_second(self) -> None:
        with self.subTest('00_create_health_check'):
            pass

        with self.subTest('01_create_backend_services'):
            pass

        with self.subTest('02_create_url_map'):
            pass

        with self.subTest('03_create_target_proxy'):
            pass

        with self.subTest('04_create_forwarding_rule'):
            pass

        with self.subTest('05_start_test_servers'):
            pass

        with self.subTest('06_add_server_backends_to_backend_services'):
            pass

        with self.subTest('07_start_test_client'):
            pass

        with self.subTest('08_test_client_xds_config_exists'):
            pass

        with self.subTest('09_primary_locality_receives_requests'):
            pass

        with self.subTest(
                '10_secondary_locality_receives_no_requests_on_partial_primary_failure'
        ):
            pass

        with self.subTest('11_gentle_failover'):
            pass

        with self.subTest(
                '12_secondary_locality_receives_requests_on_primary_failure'):
            pass

        with self.subTest('13_traffic_resumes_to_healthy_backends'):
            pass


if __name__ == '__main__':
    absltest.main(failfast=True)
