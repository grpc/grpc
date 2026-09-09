# Copyright 2026 gRPC authors.
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
"""Concurrency tests of grpc_csds."""

import logging
import os
import unittest

from envoy.service.status.v3 import csds_pb2
from envoy.service.status.v3 import csds_pb2_grpc
import grpc
import grpc_csds

from concurrency_tests._concurrency_base import ITERATIONS_PER_THREAD
from concurrency_tests._concurrency_base import RPC_TIMEOUT
from concurrency_tests._concurrency_base import ConcurrencyTestCase

_DUMMY_XDS_ADDRESS = "xds:///foo.bar"
_DUMMY_BOOTSTRAP_FILE = """
{
  \"xds_servers\": [
    {
      \"server_uri\": \"fake:///xds_server\",
      \"channel_creds\": [
        {
          \"type\": \"fake\"
        }
      ],
      \"server_features\": [\"xds_v3\"]
    }
  ],
  \"node\": {
    \"id\": \"python_test_csds\",
    \"cluster\": \"test\",
    \"metadata\": {
      \"foo\": \"bar\"
    },
    \"locality\": {
      \"region\": \"corp\",
      \"zone\": \"svl\",
      \"sub_zone\": \"mp3\"
    }
  }
}\
"""


class CsdsConcurrencyTest(ConcurrencyTestCase):
    def setUp(self) -> None:
        super().setUp()
        os.environ["GRPC_XDS_BOOTSTRAP_CONFIG"] = _DUMMY_BOOTSTRAP_FILE

        _, self._stub = self.start_server(
            None,
            lambda _servier, server: grpc_csds.add_csds_servicer(server),
            csds_pb2_grpc.ClientStatusDiscoveryServiceStub,
        )

        # force XdsClient into existence so the dump has real content to
        # serialize. The RPC is expected to fail fast; we only need the side
        # effect of initializing the xDS client.
        self._dummy_channel = grpc.insecure_channel(_DUMMY_XDS_ADDRESS)
        try:
            self._dummy_channel.unary_unary("/force/init")(
                b"", wait_for_ready=False, timeout=1
            )
        except grpc.RpcError:
            pass

    def tearDown(self) -> None:
        self._dummy_channel.close()
        os.environ.pop("GRPC_XDS_BOOTSTRAP_CONFIG", None)
        super().tearDown()

    def _fetch(self, unused_index):
        for i in range(ITERATIONS_PER_THREAD):
            response = self._stub.FetchClientStatus(
                csds_pb2.ClientStatusRequest(), timeout=RPC_TIMEOUT
            )
            self.assertIsNotNone(response)

    def test_concurrent_fetch(self):
        self.spawn_workers(self._fetch)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
