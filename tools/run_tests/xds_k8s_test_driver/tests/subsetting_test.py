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

import collections
from typing import List

from absl import flags
from absl import logging
from absl.testing import absltest
from google.protobuf import json_format

from framework import xds_k8s_testcase
from framework import xds_url_map_testcase
from framework.helpers import skips

flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient

_SUBSET_SIZE = 4
_NUM_BACKENDS = 8
_NUM_CLIENTS = 3


class SubsettingTest(xds_k8s_testcase.RegularXdsKubernetesTestCase):

    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        # Subsetting is an experimental feature where most work is done on the
        # server-side. We limit it to only run on master branch to save
        # resources.
        return config.version_gte('master')

    def test_subsetting_basic(self) -> None:
        with self.subTest('00_create_health_check'):
            self.td.create_health_check()

        with self.subTest('01_create_backend_services'):
            self.td.create_backend_service(subset_size=_SUBSET_SIZE)

        with self.subTest('02_create_url_map'):
            self.td.create_url_map(self.server_xds_host, self.server_xds_port)

        with self.subTest('03_create_target_proxy'):
            self.td.create_target_proxy()

        with self.subTest('04_create_forwarding_rule'):
            self.td.create_forwarding_rule(self.server_xds_port)

        test_servers: List[_XdsTestServer]
        with self.subTest('05_start_test_servers'):
            test_servers = self.startTestServers(replica_count=_NUM_BACKENDS)

        with self.subTest('06_add_server_backends_to_backend_services'):
            self.setupServerBackends()

        rpc_distribution = collections.defaultdict(int)
        with self.subTest('07_start_test_client'):
            for i in range(_NUM_CLIENTS):
                # Clean created client pods if there is any.
                if self.client_runner.time_start_requested:
                    # TODO(sergiitk): Speed up by reusing the namespace.
                    self.client_runner.cleanup()

                # Create a test client
                test_client: _XdsTestClient = self.startTestClient(
                    test_servers[0])
                # Validate the number of received endpoints
                config = test_client.csds.fetch_client_status(
                    log_level=logging.INFO)
                self.assertIsNotNone(config)
                json_config = json_format.MessageToDict(config)
                parsed = xds_url_map_testcase.DumpedXdsConfig(json_config)
                logging.info('Client %d received endpoints (len=%s): %s', i,
                             len(parsed.endpoints), parsed.endpoints)
                self.assertLen(parsed.endpoints, _SUBSET_SIZE)
                # Record RPC stats
                lb_stats = self.getClientRpcStats(test_client,
                                                  _NUM_BACKENDS * 25)
                for key, value in lb_stats.rpcs_by_peer.items():
                    rpc_distribution[key] += value

        with self.subTest('08_log_rpc_distribution'):
            server_entries = sorted(rpc_distribution.items(),
                                    key=lambda x: -x[1])
            # Validate if clients are receiving different sets of backends (3
            # client received a total of 4 unique backends == FAIL, a total of 5
            # unique backends == PASS)
            self.assertGreater(len(server_entries), _SUBSET_SIZE)
            logging.info('RPC distribution (len=%s): %s', len(server_entries),
                         server_entries)
            peak = server_entries[0][1]
            mean = sum(map(lambda x: x[1],
                           server_entries)) / len(server_entries)
            logging.info('Peak=%d Mean=%.1f Peak-to-Mean-Ratio=%.2f', peak,
                         mean, peak / mean)


if __name__ == '__main__':
    absltest.main(failfast=True)
