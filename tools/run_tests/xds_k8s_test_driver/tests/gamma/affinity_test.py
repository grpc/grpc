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
from typing import List, Optional

from absl import flags
from absl.testing import absltest

from framework import xds_gamma_testcase
from framework import xds_k8s_testcase
from framework import xds_url_map_testcase
from framework.rpc import grpc_testing
from framework.test_app import client_app
from framework.test_app import server_app
from framework.test_cases import session_affinity_util

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

_XdsTestServer = server_app.XdsTestServer
_XdsTestClient = client_app.XdsTestClient
RpcTypeUnaryCall = xds_url_map_testcase.RpcTypeUnaryCall

_REPLICA_COUNT = 3


class AffinityTest(xds_gamma_testcase.GammaXdsKubernetesTestCase):
    def getClientRpcStats(
        self,
        test_client: _XdsTestClient,
        num_rpcs: int,
        *,
        metadata_keys: Optional[tuple[str, ...]] = None,
    ) -> grpc_testing.LoadBalancerStatsResponse:
        """Load all metadata_keys by default."""
        return super().getClientRpcStats(
            test_client,
            num_rpcs,
            metadata_keys=metadata_keys or client_app.REQ_LB_STATS_METADATA_ALL,
        )

    def test_session_affinity_filter(self):
        test_servers: List[_XdsTestServer]
        with self.subTest("01_run_test_server"):
            test_servers = self.startTestServers(
                replica_count=_REPLICA_COUNT,
                route_template="gamma/route_http_ssafilter.yaml",
            )

        with self.subTest("02_create_ssa_filter"):
            self.server_runner.createSessionAffinityFilter()

        # Default is round robin LB policy.

        with self.subTest("03_start_test_client"):
            test_client: _XdsTestClient = self.startTestClient(test_servers[0])

        with self.subTest("04_send_first_RPC_and_retrieve_cookie"):
            (
                cookie,
                chosen_server,
            ) = session_affinity_util.assert_eventually_retrieve_cookie_and_server(
                self, test_client, test_servers
            )

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

    def test_session_affinity_policy_with_route_target(self):
        test_servers: List[_XdsTestServer]
        with self.subTest("01_run_test_server"):
            test_servers = self.startTestServers(replica_count=_REPLICA_COUNT)

        with self.subTest("02_create_ssa_policy"):
            self.server_runner.createSessionAffinityPolicy(
                "gamma/session_affinity_policy_route.yaml"
            )

        # Default is round robin LB policy.

        with self.subTest("03_start_test_client"):
            test_client: _XdsTestClient = self.startTestClient(test_servers[0])

        with self.subTest("04_send_first_RPC_and_retrieve_cookie"):
            (
                cookie,
                chosen_server,
            ) = session_affinity_util.assert_eventually_retrieve_cookie_and_server(
                self, test_client, test_servers
            )

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

    def test_session_affinity_policy_with_service_target(self):
        test_servers: List[_XdsTestServer]
        with self.subTest("01_run_test_server"):
            test_servers = self.startTestServers(replica_count=_REPLICA_COUNT)

        with self.subTest("02_create_ssa_policy"):
            self.server_runner.createSessionAffinityPolicy(
                "gamma/session_affinity_policy_service.yaml"
            )

        # Default is round robin LB policy.

        with self.subTest("03_start_test_client"):
            test_client: _XdsTestClient = self.startTestClient(test_servers[0])

        with self.subTest("04_send_first_RPC_and_retrieve_cookie"):
            (
                cookie,
                chosen_server,
            ) = session_affinity_util.assert_eventually_retrieve_cookie_and_server(
                self, test_client, test_servers
            )

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
    absltest.main()
