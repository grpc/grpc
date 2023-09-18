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
from google.protobuf import json_format

from framework import xds_gamma_testcase
from framework import xds_k8s_testcase
from framework import xds_url_map_testcase
from framework.test_cases.session_affinity_util import *

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
RpcTypeUnaryCall = xds_url_map_testcase.RpcTypeUnaryCall

_REPLICA_COUNT = 3

class AffinityTest(xds_gamma_testcase.GammaXdsKubernetesTestCase):
    def test_session_affinity_filter(self):
        test_servers: List[_XdsTestServer]
        with self.subTest("01_run_test_server"):
            test_servers = self.startTestServers(replica_count=_REPLICA_COUNT,
                                                    route_template="gamma/route_http_ssafilter.yaml")

        with self.subTest("02_create_ssa_filter"):
            self.server_runner.createSessionAffinityFilter()

        # Default is round robin LB policy.

        with self.subTest("03_start_test_client"):
            test_client: _XdsTestClient = self.startTestClient(test_servers[0])

        with self.subTest("04_send_first_RPC_and_retrieve_cookie"):
            cookie, chosen_server = assert_eventually_retrieve_cookie_and_server(self, test_client, test_servers)

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
            self.server_runner.createSessionAffinityPolicy("gamma/session_affinity_policy_route.yaml")

        # Default is round robin LB policy.

        with self.subTest("03_start_test_client"):
            test_client: _XdsTestClient = self.startTestClient(test_servers[0])

        with self.subTest("04_send_first_RPC_and_retrieve_cookie"):
            cookie, chosen_server = assert_eventually_retrieve_cookie_and_server(self, test_client, test_servers)

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
            self.server_runner.createSessionAffinityPolicy("gamma/session_affinity_policy_service.yaml")

        # Default is round robin LB policy.

        with self.subTest("03_start_test_client"):
            test_client: _XdsTestClient = self.startTestClient(test_servers[0])

        with self.subTest("04_send_first_RPC_and_retrieve_cookie"):
            cookie, chosen_server = assert_eventually_retrieve_cookie_and_server(self, test_client, test_servers)

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

# class SessionDrainAffinityTest(xds_gamma_testcase.GammaXdsKubernetesTestCase):
#     def setUp(self):
#         self.pre_stop_hook = True
#         super(SessionDrainAffinityTest, self).setUp()
# 
#     def test_session_drain(self):
#         with self.subTest("01_run_test_server"):
#             test_servers = self.startTestServers(replica_count=_REPLICA_COUNT)
# 
#         with self.subTest("02_create_ssa_policy"):
#             self.server_runner.createSessionAffinityPolicy("gamma/session_affinity_policy_route.yaml")
# 
#         with self.subTest("03_create_backend_policy"):
#             self.server_runner.createBackendPolicy()
# 
#         # Default is round robin LB policy.
# 
#         with self.subTest("04_start_test_client"):
#             test_client: _XdsTestClient = self.startTestClient(test_servers[0])
# 
#         # with self.subTest("05_send_first_RPC_and_retrieve_cookie"):
#         #     cookie, chosen_server = assert_eventually_retrieve_cookie_and_server(self, test_client, test_servers)
# 
#         # with self.subTest("06_send_RPCs_with_cookie"):
#         #     test_client.update_config.configure(
#         #         rpc_types=(RpcTypeUnaryCall,),
#         #         metadata=(
#         #             (
#         #                 RpcTypeUnaryCall,
#         #                 "cookie",
#         #                 cookie,
#         #             ),
#         #         ),
#         #     )
#         #     self.assertRpcsEventuallyGoToGivenServers(
#         #         test_client, [chosen_server], 10
#         #     )
# 
#         # send RPC to start hook server
# 
#         # delete chosen_server pod 
#         with self.subTest("07_initiate_backend_pod_termination"):
#             self.server_runner.delete_pod_async(test_servers[0].hostname)
#             logging.info("Waiting 30 seconds")
#             import time; time.sleep(30)
#             # self.server_runner.delete_pod_async(chosen_server.hostname)
# 
#         with self.subTest("08_confirm_backend_is_draining"):
#             config = test_client.csds.fetch_client_status(
#                 log_level=logging.INFO
#             )
#             self.assertIsNotNone(config)
#             json_config = json_format.MessageToDict(config)
#             parsed = xds_url_map_testcase.DumpedXdsConfig(json_config)
#             logging.info("Received CSDS: {parsed}")
# 
#         # verify the backend has entered the draining state via CSDS
# 
#         # assert traffic continues to flow to the chosen server
# 
#         # send the hook return RPC
# 
#         # until traffic goes to a different backend
# 
#         # refresh the set of backend pods
# 
#         # retrieve a new set-cookie
#         # reconfigure client to the new chosen backend
#         # assert traffic to the new backend



if __name__ == "__main__":
    absltest.main(failfast=True)
