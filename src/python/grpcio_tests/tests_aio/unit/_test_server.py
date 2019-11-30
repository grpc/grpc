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

_INITIAL_METADATA_KEY = "initial-md-key"
_TRAILING_METADATA_KEY = "trailing-md-key-bin"


def _maybe_echo_metadata(servicer_context):
    """Copies metadata from request to response if it is present."""
    invocation_metadata = dict(servicer_context.invocation_metadata())
    if _INITIAL_METADATA_KEY in invocation_metadata:
        initial_metadatum = (_INITIAL_METADATA_KEY,
                             invocation_metadata[_INITIAL_METADATA_KEY])
        servicer_context.send_initial_metadata((initial_metadatum,))
    if _TRAILING_METADATA_KEY in invocation_metadata:
        trailing_metadatum = (_TRAILING_METADATA_KEY,
                              invocation_metadata[_TRAILING_METADATA_KEY])
        servicer_context.set_trailing_metadata((trailing_metadatum,))


class _TestServiceServicer(test_pb2_grpc.TestServiceServicer):

    async def UnaryCall(self, request, context):
        # _maybe_echo_metadata(context)
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
