# Copyright 2020 The gRPC Authors
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
"""A simple test to ensure that the Python wrapper can get xDS config."""

import logging
import os
import unittest
from concurrent.futures import ThreadPoolExecutor

import grpc
import grpc_csds

from envoy.service.status.v3 import csds_pb2, csds_pb2_grpc


class TestCsds(unittest.TestCase):

    def test_xds_config_dump(self):
        server = grpc.server(ThreadPoolExecutor())
        port = server.add_insecure_port('localhost:0')
        grpc_csds.add_csds_servicer(server)
        server.start()

        channel = grpc.insecure_channel(f'localhost:{port}')
        stub = csds_pb2_grpc.ClientStatusDiscoveryServiceStub(channel)
        resp = stub.FetchClientStatus(csds_pb2.ClientStatusRequest())
        print(resp)

        channel.close()
        server.stop(0)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
