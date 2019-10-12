# Copyright 2019 gRPC authors.
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

import argparse

from concurrent import futures
from time import sleep

import grpc
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests.unit.framework.common import test_constants


# TODO (https://github.com/grpc/grpc/issues/19762)
# Change for an asynchronous server version once it's implemented.
class TestServiceServicer(test_pb2_grpc.TestServiceServicer):

    def UnaryCall(self, request, context):
        return messages_pb2.SimpleResponse()

    def EmptyCall(self, request, context):
        while True:
            sleep(test_constants.LONG_TIMEOUT)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Synchronous gRPC server.')
    parser.add_argument(
        '--host_and_port',
        required=True,
        type=str,
        nargs=1,
        help='the host and port to listen.')
    args = parser.parse_args()

    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=1),
        options=(('grpc.so_reuseport', 1),))
    test_pb2_grpc.add_TestServiceServicer_to_server(TestServiceServicer(),
                                                    server)
    server.add_insecure_port(args.host_and_port[0])
    server.start()
    server.wait_for_termination()
