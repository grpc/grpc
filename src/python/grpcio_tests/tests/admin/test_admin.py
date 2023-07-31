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
"""A test to ensure that admin services are registered correctly."""

from concurrent.futures import ThreadPoolExecutor
import logging
import sys
import unittest

import grpc
import grpc_admin
from grpc_channelz.v1 import channelz_pb2
from grpc_channelz.v1 import channelz_pb2_grpc
from grpc_csds import csds_pb2
from grpc_csds import csds_pb2_grpc


@unittest.skipIf(
    sys.version_info[0] < 3, "ProtoBuf descriptor has moved on from Python2"
)
class TestAdmin(unittest.TestCase):
    def setUp(self):
        self._server = grpc.server(ThreadPoolExecutor())
        port = self._server.add_insecure_port("localhost:0")
        grpc_admin.add_admin_servicers(self._server)
        self._server.start()

        self._channel = grpc.insecure_channel(f"localhost:{port}")

    def tearDown(self):
        self._channel.close()
        self._server.stop(0)

    def test_has_csds(self):
        stub = csds_pb2_grpc.ClientStatusDiscoveryServiceStub(self._channel)
        resp = stub.FetchClientStatus(csds_pb2.ClientStatusRequest())
        # No exception raised and the response is valid
        self.assertGreater(len(resp.config), 0)

    def test_has_channelz(self):
        stub = channelz_pb2_grpc.ChannelzStub(self._channel)
        resp = stub.GetTopChannels(channelz_pb2.GetTopChannelsRequest())
        # No exception raised and the response is valid
        self.assertGreater(len(resp.channel), 0)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
