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
from typing import List

from absl import flags
from absl.testing import absltest

from framework import xds_k8s_testcase
from framework import xds_url_map_testcase
from framework.test_cases import session_affinity_util

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
RpcTypeUnaryCall = xds_url_map_testcase.RpcTypeUnaryCall

_REPLICA_COUNT = 3

# This is here temporarily to run this test separately from other app_net tests.
# TODO(sergiitk): Move into app_net_test.py


class AppNetSsaTest(xds_k8s_testcase.AppNetXdsKubernetesTestCase):
    def test_session_affinity_policy(self):
        test_servers: List[_XdsTestServer]

        with self.subTest("0_create_health_check"):
            self.td.create_health_check()

        with self.subTest("1_create_backend_service"):
            self.td.create_backend_service()

        with self.subTest("2_create_mesh"):
            self.td.create_mesh()

        with self.subTest("3_create_http_route"):
            self.td.create_http_route_with_content(
                {
                    "meshes": [self.td.mesh.url],
                    "hostnames": [
                        f"{self.server_xds_host}:{self.server_xds_port}"
                    ],
                    "rules": [
                        {
                            "action": {
                                "destinations": [
                                    {
                                        "serviceName": self.td.netsvc.resource_full_name(
                                            self.td.backend_service.name,
                                            "backendServices",
                                        ),
                                        "weight": 1,
                                    },
                                ],
                                "statefulSessionAffinity": {
                                    "cookieTtl": "50s",
                                },
                            },
                        },
                    ],
                }
            )

        with self.subTest("4_run_test_server"):
            test_servers = self.startTestServers(replica_count=_REPLICA_COUNT)

        with self.subTest("5_setup_server_backends"):
            self.setupServerBackends()

        # Default is round robin LB policy.

        with self.subTest("6_start_test_client"):
            test_client: _XdsTestClient = self.startTestClient(
                test_servers[0], config_mesh=self.td.mesh.name
            )

        with self.subTest("7_send_first_RPC_and_retrieve_cookie"):
            (
                cookie,
                chosen_server,
            ) = session_affinity_util.assert_eventually_retrieve_cookie_and_server(
                self, test_client, test_servers
            )

        with self.subTest("8_send_RPCs_with_cookie"):
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
