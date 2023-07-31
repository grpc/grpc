# Copyright 2015 gRPC authors.
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
"""Secure client-server interoperability as a unit test."""

import sys
import unittest

import grpc

from src.proto.grpc.testing import test_pb2_grpc
from tests.interop import _intraop_test_case
from tests.interop import resources
from tests.interop import service
from tests.unit import test_common

_SERVER_HOST_OVERRIDE = "foo.test.google.fr"


@unittest.skipIf(
    sys.version_info[0] < 3, "ProtoBuf descriptor has moved on from Python2"
)
class SecureIntraopTest(_intraop_test_case.IntraopTestCase, unittest.TestCase):
    def setUp(self):
        self.server = test_common.test_server()
        test_pb2_grpc.add_TestServiceServicer_to_server(
            service.TestService(), self.server
        )
        port = self.server.add_secure_port(
            "[::]:0",
            grpc.ssl_server_credentials(
                [(resources.private_key(), resources.certificate_chain())]
            ),
        )
        self.server.start()
        self.stub = test_pb2_grpc.TestServiceStub(
            grpc.secure_channel(
                f"localhost:{port}",
                grpc.ssl_channel_credentials(
                    resources.test_root_certificates()
                ),
                (
                    (
                        "grpc.ssl_target_name_override",
                        _SERVER_HOST_OVERRIDE,
                    ),
                ),
            )
        )

    def tearDown(self):
        self.server.stop(None)


if __name__ == "__main__":
    unittest.main(verbosity=2)
