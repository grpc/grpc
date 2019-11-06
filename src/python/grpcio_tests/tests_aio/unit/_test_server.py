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

from time import sleep

from grpc.experimental import aio
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests.unit.framework.common import test_constants


class _TestServiceServicer(test_pb2_grpc.TestServiceServicer):

    async def UnaryCall(self, request, context):
        return messages_pb2.SimpleResponse()

    async def EmptyCall(self, request, context):
        while True:
            sleep(test_constants.LONG_TIMEOUT)


async def start_test_server():
    server = aio.server(options=(('grpc.so_reuseport', 0),))
    test_pb2_grpc.add_TestServiceServicer_to_server(_TestServiceServicer(),
                                                    server)
    port = server.add_insecure_port('[::]:0')
    await server.start()
    # NOTE(lidizheng) returning the server to prevent it from deallocation
    return 'localhost:%d' % port, server
