# Copyright 2021 The gRPC Authors
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
from typing import Tuple

from absl import flags
from absl.testing import absltest

from framework import xds_url_map_testcase
from framework.helpers import skips
from framework.test_app import client_app

# Type aliases
HostRule = xds_url_map_testcase.HostRule
PathMatcher = xds_url_map_testcase.PathMatcher
GcpResourceManager = xds_url_map_testcase.GcpResourceManager
DumpedXdsConfig = xds_url_map_testcase.DumpedXdsConfig
RpcTypeUnaryCall = xds_url_map_testcase.RpcTypeUnaryCall
RpcTypeEmptyCall = xds_url_map_testcase.RpcTypeEmptyCall
XdsTestClient = client_app.XdsTestClient
_Lang = skips.Lang

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_url_map_testcase)

_NUM_RPCS = 50


class TestBasicCsds(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        if config.client_lang == _Lang.NODE:
            return config.version_gte("v1.5.x")
        return True

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        # Validate Endpoint Configs
        self.assertNumEndpoints(xds_config, 1)
        # Validate Node
        self.assertEqual(
            self.test_client.ip, xds_config["node"]["metadata"]["INSTANCE_IP"]
        )
        # Validate Listeners
        self.assertIsNotNone(xds_config.lds)
        self.assertEqual(self.hostname(), xds_config.lds["name"])
        # Validate Route Configs
        self.assertTrue(xds_config.rds["virtualHosts"])
        # Validate Clusters
        self.assertEqual(1, len(xds_config.cds))
        self.assertEqual("EDS", xds_config.cds[0]["type"])
        self.assertEqual(2, 3)

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeUnaryCall, RpcTypeEmptyCall],
            num_rpcs=_NUM_RPCS,
        )
        self.assertEqual(_NUM_RPCS, rpc_distribution.num_oks)


if __name__ == "__main__":
    absltest.main()
