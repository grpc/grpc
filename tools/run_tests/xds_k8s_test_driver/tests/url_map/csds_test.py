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
import sys
import logging
import json
from typing import Tuple

from absl import flags
from absl.testing import absltest

from framework import xds_url_map_testcase
from framework.test_app import client_app
from google.protobuf import json_format

# Type aliases
HostRule = xds_url_map_testcase.HostRule
PathMatcher = xds_url_map_testcase.PathMatcher
GcpResourceManager = xds_url_map_testcase.GcpResourceManager
DumpedXdsConfig = xds_url_map_testcase.DumpedXdsConfig
RpcTypeUnaryCall = xds_url_map_testcase.RpcTypeUnaryCall
RpcTypeEmptyCall = xds_url_map_testcase.RpcTypeEmptyCall
XdsTestClient = client_app.XdsTestClient

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_url_map_testcase)

_NUM_RPCS = 50


class TestBasicCsds(xds_url_map_testcase.XdsUrlMapTestCase):

    @staticmethod
    def url_map_change(
            host_rule: HostRule,
            path_matcher: PathMatcher) -> Tuple[HostRule, PathMatcher]:
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        # Validate Node
        self.assertEqual(
            self.test_client.ip,
            xds_config.json_config['node']['metadata']['INSTANCE_IP'])
        # Validate Listeners
        self.assertNotNone(xds_config.lds)
        self.assertEqual(self.hostname(), xds_config.lds['name'])
        # Validate Route Configs
        self.assertTrue(xds_config.rds['virtual_hosts'])
        # Validate Clusters
        self.assertEqual('EDS', xds_config.cds['type'])
        # Validate Endpoint Configs
        self.assertEqual(2, len(self.endpoints))

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeUnaryCall, RpcTypeEmptyCall],
            num_rpcs=_NUM_RPCS)
        self.assertEqual(_NUM_RPCS, rpc_distribution.num_oks)


def load_tests(loader: absltest.TestLoader, unused_tests, unused_pattern):
    return xds_url_map_testcase.load_tests(loader,
                                           sys.modules[__name__],
                                           module_name_override='csds_test')


if __name__ == '__main__':
    absltest.main(failfast=True)
