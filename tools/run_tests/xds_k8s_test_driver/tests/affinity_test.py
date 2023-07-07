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
from typing import List

from absl import flags
from absl.testing import absltest
from google.protobuf import json_format

from framework import xds_k8s_flags
from framework import xds_k8s_testcase
from framework import xds_url_map_testcase
from framework.helpers import skips
from framework.rpc import grpc_channelz

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
_ChannelzChannelState = grpc_channelz.ChannelState
_Lang = skips.Lang

# Testing consts
_TEST_AFFINITY_METADATA_KEY = "xds_md"
_TD_PROPAGATE_CHECK_INTERVAL_SEC = 10
_TD_PROPAGATE_TIMEOUT = 600
_REPLICA_COUNT = 3
_RPC_COUNT = 100


class AffinityTest(xds_k8s_testcase.RegularXdsKubernetesTestCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        # Force the python client to use the reference server image (Java)
        # because the python server doesn't yet support set_not_serving RPC.
        # TODO(https://github.com/grpc/grpc/issues/30635): Remove when resolved.
        if cls.lang_spec.client_lang == _Lang.PYTHON:
            cls.server_image = xds_k8s_flags.SERVER_IMAGE_CANONICAL.value

    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        if config.client_lang in _Lang.CPP | _Lang.JAVA:
            return config.version_gte("v1.40.x")
        elif config.client_lang == _Lang.GO:
            return config.version_gte("v1.41.x")
        elif config.client_lang == _Lang.PYTHON:
            # TODO(https://github.com/grpc/grpc/issues/27430): supported after
            #      the issue is fixed.
            return False
        elif config.client_lang == _Lang.NODE:
            return False
        return True

    def test_affinity(self) -> None:  # pylint: disable=too-many-statements
        with self.subTest("00_create_health_check"):
            self.td.create_health_check()

        with self.subTest("01_create_backend_services"):
            self.td.create_backend_service(
                affinity_header=_TEST_AFFINITY_METADATA_KEY
            )

        with self.subTest("02_create_url_map"):
            self.td.create_url_map(self.server_xds_host, self.server_xds_port)

        with self.subTest("03_create_target_proxy"):
            self.td.create_target_proxy()

        with self.subTest("04_create_forwarding_rule"):
            self.td.create_forwarding_rule(self.server_xds_port)

        test_servers: List[_XdsTestServer]
        with self.subTest("05_start_test_servers"):
            test_servers = self.startTestServers(replica_count=_REPLICA_COUNT)

        with self.subTest("06_add_server_backends_to_backend_services"):
            self.setupServerBackends()

        test_client: _XdsTestClient
        with self.subTest("07_start_test_client"):
            test_client = self.startTestClient(
                test_servers[0],
                rpc="EmptyCall",
                metadata="EmptyCall:%s:123" % _TEST_AFFINITY_METADATA_KEY,
            )
            # Validate the number of received endpoints and affinity configs.
            config = test_client.csds.fetch_client_status(
                log_level=logging.INFO
            )
            self.assertIsNotNone(config)
            json_config = json_format.MessageToDict(config)
            parsed = xds_url_map_testcase.DumpedXdsConfig(json_config)
            logging.info("Client received CSDS response: %s", parsed)
            self.assertLen(parsed.endpoints, _REPLICA_COUNT)
            self.assertEqual(
                parsed.rds["virtualHosts"][0]["routes"][0]["route"][
                    "hashPolicy"
                ][0]["header"]["headerName"],
                _TEST_AFFINITY_METADATA_KEY,
            )
            self.assertEqual(parsed.cds[0]["lbPolicy"], "RING_HASH")

        with self.subTest("08_test_client_xds_config_exists"):
            self.assertXdsConfigExists(test_client)

        with self.subTest("09_test_server_received_rpcs_from_test_client"):
            self.assertSuccessfulRpcs(test_client)

        with self.subTest("10_first_100_affinity_rpcs_pick_same_backend"):
            rpc_stats = self.getClientRpcStats(test_client, _RPC_COUNT)
            json_lb_stats = json_format.MessageToDict(rpc_stats)
            rpc_distribution = xds_url_map_testcase.RpcDistributionStats(
                json_lb_stats
            )
            self.assertEqual(1, rpc_distribution.num_peers)

            # Check subchannel states.
            # One should be READY.
            ready_channels = test_client.find_subchannels_with_state(
                _ChannelzChannelState.READY
            )
            self.assertLen(
                ready_channels,
                1,
                msg=(
                    "(AffinityTest) The client expected to have one READY"
                    " subchannel to one of the test servers. Found"
                    f" {len(ready_channels)} instead."
                ),
            )
            # The rest should be IDLE.
            expected_idle_channels = _REPLICA_COUNT - 1
            idle_channels = test_client.find_subchannels_with_state(
                _ChannelzChannelState.IDLE
            )
            self.assertLen(
                idle_channels,
                expected_idle_channels,
                msg=(
                    "(AffinityTest) The client expected to have IDLE"
                    f" subchannels to {expected_idle_channels} of the test"
                    f" servers. Found {len(idle_channels)} instead."
                ),
            )
            # Remember the backend inuse, and turn it down later.
            first_backend_inuse = list(
                rpc_distribution.raw["rpcsByPeer"].keys()
            )[0]

        with self.subTest("11_turn_down_server_in_use"):
            for server in test_servers:
                if server.hostname == first_backend_inuse:
                    server.set_not_serving()

        with self.subTest("12_wait_for_unhealth_status_propagation"):
            deadline = time.time() + _TD_PROPAGATE_TIMEOUT
            parsed = None
            try:
                while time.time() < deadline:
                    config = test_client.csds.fetch_client_status(
                        log_level=logging.INFO
                    )
                    self.assertIsNotNone(config)
                    json_config = json_format.MessageToDict(config)
                    parsed = xds_url_map_testcase.DumpedXdsConfig(json_config)
                    if len(parsed.endpoints) == _REPLICA_COUNT - 1:
                        break
                    logging.info(
                        (
                            "CSDS got unexpected endpoints, will retry after %d"
                            " seconds"
                        ),
                        _TD_PROPAGATE_CHECK_INTERVAL_SEC,
                    )
                    time.sleep(_TD_PROPAGATE_CHECK_INTERVAL_SEC)
                else:
                    self.fail(
                        "unhealthy status did not propagate after 600 seconds"
                    )
            finally:
                logging.info("Client received CSDS response: %s", parsed)

        with self.subTest("12_next_100_affinity_rpcs_pick_different_backend"):
            rpc_stats = self.getClientRpcStats(test_client, _RPC_COUNT)
            json_lb_stats = json_format.MessageToDict(rpc_stats)
            rpc_distribution = xds_url_map_testcase.RpcDistributionStats(
                json_lb_stats
            )
            self.assertEqual(1, rpc_distribution.num_peers)
            new_backend_inuse = list(rpc_distribution.raw["rpcsByPeer"].keys())[
                0
            ]
            self.assertNotEqual(new_backend_inuse, first_backend_inuse)


if __name__ == "__main__":
    absltest.main(failfast=True)
