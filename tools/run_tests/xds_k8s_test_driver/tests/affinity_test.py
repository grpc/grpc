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
import time
from typing import List, Optional

from absl import flags
from absl.testing import absltest
from google.protobuf import json_format

from framework import xds_k8s_testcase
from framework import xds_url_map_testcase
from framework.helpers import retryers
from framework.infrastructure import k8s
from framework.rpc import grpc_channelz
from framework.test_app import server_app

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
_ChannelzChannelState = grpc_channelz.ChannelState

# Testing consts
_TEST_AFFINITY_METADATA_KEY = 'xds_md'
_TD_PROPAGATE_CHECK_INTERVAL_SEC = 10
_TD_PROPAGATE_TIMEOUT = 600
_REPLICA_COUNT = 3
_RPC_COUNT = 100


class AffinityTest(xds_k8s_testcase.RegularXdsKubernetesTestCase):

    def test_affinity(self) -> None:

        with self.subTest('00_create_health_check'):
            self.td.create_health_check()

        with self.subTest('01_create_backend_services'):
            self.td.create_backend_service(
                affinity_header=_TEST_AFFINITY_METADATA_KEY)

        with self.subTest('02_create_url_map'):
            self.td.create_url_map(self.server_xds_host, self.server_xds_port)

        with self.subTest('03_create_target_proxy'):
            self.td.create_target_proxy()

        with self.subTest('04_create_forwarding_rule'):
            self.td.create_forwarding_rule(self.server_xds_port)

        with self.subTest('05_start_test_servers'):
            self.test_servers: List[_XdsTestServer] = self.startTestServers(
                replica_count=_REPLICA_COUNT)

        with self.subTest('06_add_server_backends_to_backend_services'):
            self.setupServerBackends()

        with self.subTest('07_start_test_client'):
            self.test_client: _XdsTestClient = self.startTestClient(
                self.test_servers[0],
                rpc='EmptyCall',
                metadata='EmptyCall:%s:123' % _TEST_AFFINITY_METADATA_KEY)
            # Validate the number of received endpoints and affinity configs.
            config = self.test_client.csds.fetch_client_status(
                log_level=logging.INFO)
            self.assertIsNotNone(config)
            json_config = json_format.MessageToDict(config)
            parsed = xds_url_map_testcase.DumpedXdsConfig(json_config)
            logging.info('Client received CSDS response: %s', parsed)
            self.assertLen(parsed.endpoints, _REPLICA_COUNT)
            self.assertEqual(
                parsed.rds['virtualHosts'][0]['routes'][0]['route']
                ['hashPolicy'][0]['header']['headerName'],
                _TEST_AFFINITY_METADATA_KEY)
            self.assertEqual(parsed.cds[0]['lbPolicy'], 'RING_HASH')

        with self.subTest('08_test_client_xds_config_exists'):
            self.assertXdsConfigExists(self.test_client)

        with self.subTest('09_test_server_received_rpcs_from_test_client'):
            self.assertSuccessfulRpcs(self.test_client)

        with self.subTest('10_first_100_affinity_rpcs_pick_same_backend'):
            rpc_stats = self.getClientRpcStats(self.test_client, _RPC_COUNT)
            json_lb_stats = json_format.MessageToDict(rpc_stats)
            rpc_distribution = xds_url_map_testcase.RpcDistributionStats(
                json_lb_stats)
            self.assertEqual(1, rpc_distribution.num_peers)
            self.assertLen(
                self.test_client.find_subchannels_with_state(
                    _ChannelzChannelState.READY),
                1,
            )
            self.assertLen(
                self.test_client.find_subchannels_with_state(
                    _ChannelzChannelState.IDLE),
                2,
            )
            # Remember the backend inuse, and turn it down later.
            self.first_backend_inuse = list(
                rpc_distribution.raw['rpcsByPeer'].keys())[0]

        with self.subTest('11_turn_down_server_in_use'):
            for s in self.test_servers:
                if s.pod_name == self.first_backend_inuse:
                    logging.info('setting backend %s to NOT_SERVING',
                                 s.pod_name)
                    s.set_not_serving()

        with self.subTest('12_wait_for_unhealth_status_propagation'):
            deadline = time.time() + _TD_PROPAGATE_TIMEOUT
            parsed = None
            try:
                while time.time() < deadline:
                    config = self.test_client.csds.fetch_client_status(
                        log_level=logging.INFO)
                    self.assertIsNotNone(config)
                    json_config = json_format.MessageToDict(config)
                    parsed = xds_url_map_testcase.DumpedXdsConfig(json_config)
                    if len(parsed.endpoints) == _REPLICA_COUNT - 1:
                        break
                    logging.info(
                        'CSDS got unexpected endpoints, will retry after %d seconds',
                        _TD_PROPAGATE_CHECK_INTERVAL_SEC)
                    time.sleep(_TD_PROPAGATE_CHECK_INTERVAL_SEC)
                else:
                    self.fail(
                        'unhealthy status did not propagate after 600 seconds')
            finally:
                logging.info('Client received CSDS response: %s', parsed)

        with self.subTest('12_next_100_affinity_rpcs_pick_different_backend'):
            rpc_stats = self.getClientRpcStats(self.test_client, _RPC_COUNT)
            json_lb_stats = json_format.MessageToDict(rpc_stats)
            rpc_distribution = xds_url_map_testcase.RpcDistributionStats(
                json_lb_stats)
            self.assertEqual(1, rpc_distribution.num_peers)
            new_backend_inuse = list(
                rpc_distribution.raw['rpcsByPeer'].keys())[0]
            self.assertNotEqual(new_backend_inuse, self.first_backend_inuse)


if __name__ == '__main__':
    absltest.main(failfast=True)
