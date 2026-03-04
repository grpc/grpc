# Copyright 2026 the gRPC authors.
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
"""Regression test: py_grpc_library must work when the proto_library source is
a generated file (not a checked-in source file).

See https://github.com/grpc/grpc/issues/41792.
"""

import unittest

import gen_echo_pb2
import gen_echo_pb2_grpc


class GeneratedProtoImportTest(unittest.TestCase):
    def test_stubs_are_importable(self):
        from gen_echo_pb2_grpc import (
            EchoServiceServicer,
            EchoServiceStub,
            add_EchoServiceServicer_to_server,
        )

    def test_proto_message_is_importable(self):
        from gen_echo_pb2 import EchoRequest


if __name__ == "__main__":
    unittest.main()
