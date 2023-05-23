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
from google.protobuf import json_format

from framework import xds_k8s_testcase
from framework import xds_url_map_testcase
from framework.helpers import skips

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
_DumpedXdsConfig = xds_url_map_testcase.DumpedXdsConfig
_Lang = skips.Lang

_TD_CONFIG_RETRY_WAIT_SEC = 2


class ApiListenerTest(xds_k8s_testcase.RegularXdsKubernetesTestCase):

    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        if config.client_lang == _Lang.PYTHON:
            # gRPC Python versions prior to v1.43.x don't support handling empty
            # RDS update.
            return config.version_gte('v1.43.x')
        return True

    def test_api_listener(self) -> None:
        with self.subTest('00_create_health_check'):
            pass

        with self.subTest('01_create_backend_services'):
            pass

        with self.subTest('02_create_default_url_map'):
            pass

        with self.subTest('03_create_default_target_proxy'):
            pass

        with self.subTest('04_create_default_forwarding_rule'):
            pass

        with self.subTest('05_start_test_server'):
            pass

        with self.subTest('06_add_server_backends_to_backend_services'):
            pass

        with self.subTest('07_start_test_client'):
            pass

        with self.subTest('08_test_client_xds_config_exists'):
            pass

        with self.subTest('09_test_server_received_rpcs'):
            pass

        with self.subTest('10_create_alternate_url_map'):
            pass

        with self.subTest('11_create_alternate_target_proxy'):
            pass

        with self.subTest('12_create_alternate_forwarding_rule'):
            pass

        with self.subTest('13_test_server_received_rpcs_with_two_url_maps'):
            pass

        with self.subTest('14_delete_one_url_map_target_proxy_forwarding_rule'):
            pass

        with self.subTest('15_test_server_continues_to_receive_rpcs'):
            pass


if __name__ == '__main__':
    absltest.main(failfast=True)
