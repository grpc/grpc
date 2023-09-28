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
import datetime
from typing import List, Optional

from absl import flags
from absl.testing import absltest
from google.protobuf import json_format

from framework import xds_gamma_testcase
from framework import xds_k8s_testcase
from framework import xds_url_map_testcase
from framework.helpers import retryers
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
_TERMINATION_GRACE_PERIOD_SECONDS = 600


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


class SessionDrainAffinityTest(xds_gamma_testcase.GammaXdsKubernetesTestCase):
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

    def setUp(self):
        self.pre_stop_hook = True
        self.termination_grace_period_seconds = (
            _TERMINATION_GRACE_PERIOD_SECONDS
        )
        super(SessionDrainAffinityTest, self).setUp()

    def test_session_drain(self):
        with self.subTest("01_run_test_server"):
            test_servers = self.startTestServers(replica_count=_REPLICA_COUNT)

        with self.subTest("02_create_ssa_policy"):
            self.server_runner.createSessionAffinityPolicy(
                "gamma/session_affinity_policy_route.yaml"
            )

        with self.subTest("03_create_backend_policy"):
            self.server_runner.createBackendPolicy()

        # Default is round robin LB policy.

        with self.subTest("04_start_test_client"):
            test_client: _XdsTestClient = self.startTestClient(test_servers[0])

        with self.subTest("05_send_first_RPC_and_retrieve_cookie"):
            (
                cookie,
                chosen_server,
            ) = session_affinity_util.assert_eventually_retrieve_cookie_and_server(
                self, test_client, test_servers
            )

        with self.subTest("06_send_RPCs_with_cookie"):
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

        with self.subTest("07_initiate_backend_pod_termination"):
            chosen_server.send_hook_request_start_server()
            self.server_runner.delete_pod_async(chosen_server.hostname)

        with self.subTest("08_confirm_backend_is_draining"):

            def _assert_draining():
                config = test_client.csds.fetch_client_status(
                    log_level=logging.INFO
                )
                self.assertIsNotNone(config)
                json_config = json_format.MessageToDict(config)
                parsed = xds_url_map_testcase.DumpedXdsConfig(json_config)
                logging.info(f"Received CSDS: {parsed}")
                self.assertLen(parsed.draining_endpoints, 1)

            retryer = retryers.constant_retryer(
                wait_fixed=datetime.timedelta(seconds=10),
                attempts=3,
                log_level=logging.INFO,
            )
            retryer(_assert_draining)

        with self.subTest("09_send_RPCs_to_draining_server"):
            self.assertRpcsEventuallyGoToGivenServers(
                test_client, [chosen_server], 10
            )

        with self.subTest("10_kill_old_server_and_receive_new_assignment"):
            chosen_server.send_hook_request_return()
            refreshed_servers = self.refreshTestServers()
            (
                cookie,
                new_chosen_server,
            ) = session_affinity_util.assert_eventually_retrieve_cookie_and_server(
                self, test_client, refreshed_servers
            )

        with self.subTest("11_send_traffic_to_new_assignment"):
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
                test_client, [new_chosen_server], 10
            )


if __name__ == "__main__":
    absltest.main(failfast=True)
