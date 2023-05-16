# Copyright 2022 gRPC authors.
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
import datetime
import logging

from absl import flags
from absl.testing import absltest
import grpc

from framework import xds_k8s_testcase
from framework.helpers import skips

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
_Lang = skips.Lang

_EXPECTED_STATUS = grpc.StatusCode.DATA_LOSS


class CustomLbTest(xds_k8s_testcase.RegularXdsKubernetesTestCase):

    # As of 2022-07-06 custom load balancer configuration via xDS is only supported by
    # Java clients v1.47.x and above.
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        if config.client_lang == _Lang.JAVA:
            return config.version_gte('v1.47.x')
        if config.client_lang == _Lang.CPP:
            return config.version_gte('v1.55.x')
        return False

    def test_custom_lb_config(self):
        with self.subTest('0_create_health_check'):
            self.td.create_health_check()

        # Configures a custom, test LB on the client to instruct the servers
        # to always respond with a specific error code.
        #
        # The first policy in the list is a non-existent one to verify that
        # the gRPC client can gracefully move down the list to the valid one
        # once it determines the first one is not available.
        with self.subTest('1_create_backend_service'):
            self.td.create_backend_service(locality_lb_policies=[{
                'customPolicy': {
                    'name': 'test.ThisLoadBalancerDoesNotExist',
                    'data': '{ "foo": "bar" }'
                },
            }, {
                'customPolicy': {
                    'name':
                        'test.RpcBehaviorLoadBalancer',
                    'data':
                        f'{{ "rpcBehavior": "error-code-{_EXPECTED_STATUS.value[0]}" }}'
                }
            }])

        with self.subTest('2_create_url_map'):
            self.td.create_url_map(self.server_xds_host, self.server_xds_port)

        with self.subTest('3_create_target_proxy'):
            self.td.create_target_proxy()

        with self.subTest('4_create_forwarding_rule'):
            self.td.create_forwarding_rule(self.server_xds_port)

        with self.subTest('5_start_test_server'):
            test_server: _XdsTestServer = self.startTestServers()[0]

        with self.subTest('6_add_server_backends_to_backend_service'):
            self.setupServerBackends()

        with self.subTest('7_start_test_client'):
            test_client: _XdsTestClient = self.startTestClient(test_server)

        with self.subTest('8_test_client_xds_config_exists'):
            self.assertXdsConfigExists(test_client)

        # Verify status codes from the servers have the configured one.
        with self.subTest('9_test_server_returned_configured_status_code'):
            self.assertRpcStatusCodes(test_client,
                                      expected_status=_EXPECTED_STATUS,
                                      duration=datetime.timedelta(seconds=10),
                                      method='UNARY_CALL')


if __name__ == '__main__':
    absltest.main(failfast=True)
