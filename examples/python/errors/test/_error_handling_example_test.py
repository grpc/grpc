# Copyright 2019 The gRPC Authors
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
"""Tests of the error handling example."""

# NOTE(lidiz) This module only exists in Bazel BUILD file, for more details
# please refer to comments in the "bazel_namespace_package_hack" module.
try:
    from tests import bazel_namespace_package_hack
    bazel_namespace_package_hack.sys_path_to_site_dir_hack()
except ImportError:
    pass

import unittest
import logging

import grpc

from examples.protos import helloworld_pb2_grpc
from examples.python.errors import client as error_handling_client
from examples.python.errors import server as error_handling_server


class ErrorHandlingExampleTest(unittest.TestCase):

    def setUp(self):
        self._server, port = error_handling_server.create_server('[::]:0')
        self._server.start()
        self._channel = grpc.insecure_channel('localhost:%d' % port)

    def tearDown(self):
        self._channel.close()
        self._server.stop(None)

    def test_error_handling_example(self):
        stub = helloworld_pb2_grpc.GreeterStub(self._channel)
        error_handling_client.process(stub)
        error_handling_client.process(stub)
        # No unhandled exception raised, test passed!


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
