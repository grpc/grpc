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
import datetime
import logging
from typing import List, Optional

from absl import flags
from absl.testing import absltest

from framework import xds_k8s_testcase
from framework.helpers import retryers

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient

_TD_CONFIG_RETRY_WAIT_SEC = 2


class ApiListenerTest(xds_k8s_testcase.RegularXdsKubernetesTestCase):

    def test_api_listener(self) -> None:
        with self.subTest('00_create_health_check'):
            self.td.create_health_check()

        with self.subTest('01_create_backend_services'):
            self.td.create_backend_service()

        with self.subTest('02_create_default_url_map'):
            self.td.create_url_map(self.server_xds_host, self.server_xds_port)

        with self.subTest('03_create_default_target_proxy'):
            self.td.create_target_proxy()

        with self.subTest('04_create_default_forwarding_rule'):
            self.td.create_forwarding_rule(self.server_xds_port)

        with self.subTest('05_start_test_server'):
            test_server: _XdsTestServer = self.startTestServers()[0]

        with self.subTest('06_add_server_backends_to_backend_services'):
            self.setupServerBackends()

        with self.subTest('07_start_test_client'):
            test_client: _XdsTestClient = self.startTestClient(test_server)

        with self.subTest('08_test_client_xds_config_exists'):
            self.assertXdsConfigExists(test_client)

        with self.subTest('09_test_server_received_rpcs'):
            self.assertSuccessfulRpcs(test_client)

        with self.subTest('10_create_alternate_url_map'):
            self.td.create_alternative_url_map(self.server_xds_host,
                                               self.server_xds_port)

        # Create alternate target proxy pointing to alternate url_map with the same
        # host name in host rule. The port is fixed because they point to the same backend service.
        # Therefore we have to choose a non-`0.0.0.0` ip because ip:port needs to be unique.
        # We also have to set validate_for_proxyless=false because requires `0.0.0.0` ip.
        # See https://github.com/grpc/grpc-java/issues/8009
        with self.subTest('11_create_alternate_target_proxy'):
            self.td.create_alternative_target_proxy()

        # Create a second suite of map+tp+fr with the same host name in host rule.
        # We set fr ip_address to be different from `0.0.0.0` and then set
        # validate_for_proxyless=false because ip:port needs to be unique.
        with self.subTest('12_create_alternate_forwarding_rule'):
            self.td.create_alternative_forwarding_rule(self.server_xds_port,
                                                       ip_address='10.10.10.10')

        with self.subTest('13_test_server_received_rpcs_with_two_url_maps'):
            self.assertSuccessfulRpcs(test_client)
            previous_route_config_version = self.getRouteConfigVersion(
                test_client)

        with self.subTest('14_delete_one_url_map_target_proxy_forwarding_rule'):
            self.td.delete_forwarding_rule()
            self.td.delete_target_grpc_proxy()
            self.td.delete_url_map()

        with self.subTest('15_test_server_continues_to_receive_rpcs'):

            retryer = retryers.constant_retryer(
                wait_fixed=datetime.timedelta(
                    seconds=_TD_CONFIG_RETRY_WAIT_SEC),
                timeout=datetime.timedelta(
                    seconds=xds_k8s_testcase._TD_CONFIG_MAX_WAIT_SEC),
                retry_on_exceptions=(TdPropagationRetryableError,),
                logger=logger,
                log_level=logging.INFO)
            try:
                retryer(
                    self.verify_route_traffic_continues,
                    previous_route_config_version=previous_route_config_version,
                    test_client=test_client)
            except retryers.RetryError as retry_error:
                logger.info(
                    'Retry exhausted. TD routing config propagation failed after timeout %ds.',
                    xds_k8s_testcase._TD_CONFIG_MAX_WAIT_SEC)
                raise retry_error

    def verify_route_traffic_continues(self, *, test_client: _XdsTestClient,
                                       previous_route_config_version: str):
        self.assertSuccessfulRpcs(test_client)
        route_config_version = self.getRouteConfigVersion(test_client)
        if previous_route_config_version == route_config_version:
            logger.info('Routing config not propagated yet. Retrying.')
            raise TdPropagationRetryableError(
                "CSDS not get updated routing config corresponding"
                " to the second set of url maps")
        else:
            self.assertSuccessfulRpcs(test_client)
            logger.info(
                '[SUCCESS] Confirmed successful RPC with the updated routing config, version=%s',
                route_config_version)


class TdPropagationRetryableError(Exception):
    pass


if __name__ == '__main__':
    absltest.main(failfast=True)
