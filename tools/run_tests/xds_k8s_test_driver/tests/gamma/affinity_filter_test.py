# Copyright 2023 gRPC authors.
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

from framework import xds_gamma_testcase
from framework import xds_k8s_testcase
from framework import xds_url_map_testcase

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
RpcTypeUnaryCall = xds_url_map_testcase.RpcTypeUnaryCall

class AffinityTest(xds_gamma_testcase.GammaXdsKubernetesTestCase):
    def parse_metadata(
        self, metadatas_by_peer: dict[str, "MetadataByPeer"]
    ) -> dict[str, str]:
        # TDDO; Delete.
        print(metadatas_by_peer)
        cookies = dict()
        for peer, metadatas in metadatas_by_peer.items():
            for metadatas in metadatas.rpc_metadata:
                for metadata in metadatas.metadata:
                    if metadata.key.lower() == "set-cookie":
                        cookies[peer] = metadata.value
        return cookies

    def test_ping_pong(self):
        REPLICA_COUNT = 3

        test_servers: List[_XdsTestServer]
        with self.subTest("01_run_test_server"):
            test_servers = self.startTestServers(replica_count=REPLICA_COUNT,
                                                    route_template="gamma/route_http_ssafilter.yaml")

        with self.subTest("02_create_ssa_filter"):
            self.server_runner.createSessionAffinityFilter()

        # Default is round robin LB policy.

        with self.subTest("03_start_test_client"):
            test_client: _XdsTestClient = self.startTestClient(test_servers[0])

        with self.subTest("04_send_first_RPC_and_retrieve_cookie"):
            lb_stats = self.assertSuccessfulRpcs(test_client, 1)
            cookies = self.parse_metadata(lb_stats.metadatas_by_peer)
            print(cookies)
            assert len(cookies) == 1
            hostname = next(iter(cookies.keys()))
            cookie = cookies[hostname]
             
            chosen_server_candidates = tuple(srv for srv in test_servers if srv.hostname == hostname)
            assert len(chosen_server_candidates) == 1
            chosen_server = chosen_server_candidates[0]

        with self.subTest("05_send_RPCs_with_cookie"):
            test_client.update_config.configure(
                rpc_types=(RpcTypeUnaryCall,),
                metadata=(
                    (
                        RpcTypeUnaryCall,
                        "cookie",
                        cookie,
                    ),
                ),
            )
            self.assertRpcsEventuallyGoToGivenServers(
                test_client, [chosen_server], 10
            )

if __name__ == "__main__":
    absltest.main(failfast=True)
