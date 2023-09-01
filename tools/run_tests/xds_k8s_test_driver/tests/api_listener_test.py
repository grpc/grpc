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
            return config.version_gte("v1.43.x")
        return True

    def test_api_listener(self) -> None:
        with self.subTest("00_create_health_check"):
            self.td.create_health_check()

        with self.subTest("01_create_backend_services"):
            self.td.create_backend_service()

        with self.subTest("02_create_default_url_map"):
            self.td.create_url_map(self.server_xds_host, self.server_xds_port)

        with self.subTest("03_create_default_target_proxy"):
            self.td.create_target_proxy()

        with self.subTest("04_create_default_forwarding_rule"):
            self.td.create_forwarding_rule(self.server_xds_port)

        test_server: _XdsTestServer
        with self.subTest("05_start_test_server"):
            test_server = self.startTestServers()[0]

        with self.subTest("06_add_server_backends_to_backend_services"):
            self.setupServerBackends()

        test_client: _XdsTestClient
        with self.subTest("07_start_test_client"):
            test_client = self.startTestClient(test_server)

        with self.subTest("08_test_client_xds_config_exists"):
            self.assertXdsConfigExists(test_client)

        with self.subTest("09_test_server_received_rpcs"):
            self.assertSuccessfulRpcs(test_client)

        with self.subTest("10_create_alternate_url_map"):
            self.td.create_alternative_url_map(
                self.server_xds_host,
                self.server_xds_port,
                self.td.backend_service,
            )

        # Create alternate target proxy pointing to alternate url_map with the same
        # host name in host rule. The port is fixed because they point to the same backend service.
        # Therefore we have to choose a non-`0.0.0.0` ip because ip:port needs to be unique.
        # We also have to set validate_for_proxyless=false because requires `0.0.0.0` ip.
        # See https://github.com/grpc/grpc-java/issues/8009
        with self.subTest("11_create_alternate_target_proxy"):
            self.td.create_alternative_target_proxy()

        # Create a second suite of map+tp+fr with the same host name in host rule.
        # We set fr ip_address to be different from `0.0.0.0` and then set
        # validate_for_proxyless=false because ip:port needs to be unique.
        with self.subTest("12_create_alternate_forwarding_rule"):
            self.td.create_alternative_forwarding_rule(
                self.server_xds_port, ip_address="10.10.10.10"
            )

        with self.subTest("13_test_server_received_rpcs_with_two_url_maps"):
            self.assertSuccessfulRpcs(test_client)
            raw_config = test_client.csds.fetch_client_status(
                log_level=logging.INFO
            )
            dumped_config = _DumpedXdsConfig(
                json_format.MessageToDict(raw_config)
            )
            previous_route_config_version = dumped_config.rds_version
            logger.info(
                (
                    "received client config from CSDS with two url maps, "
                    "dump config: %s, rds version: %s"
                ),
                dumped_config,
                previous_route_config_version,
            )

        with self.subTest("14_delete_one_url_map_target_proxy_forwarding_rule"):
            self.td.delete_forwarding_rule()
            self.td.delete_target_grpc_proxy()
            self.td.delete_url_map()

        with self.subTest("15_test_server_continues_to_receive_rpcs"):
            self.assertRouteConfigUpdateTrafficHandoff(
                test_client,
                previous_route_config_version,
                _TD_CONFIG_RETRY_WAIT_SEC,
                xds_k8s_testcase._TD_CONFIG_MAX_WAIT_SEC,
            )


if __name__ == "__main__":
    absltest.main(failfast=True)
