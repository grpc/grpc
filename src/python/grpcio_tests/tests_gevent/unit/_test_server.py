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

from concurrent import futures
from typing import Any, Tuple

import gevent
import grpc

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc

LONG_UNARY_CALL_WITH_SLEEP_VALUE = 1


class TestServiceServicer(test_pb2_grpc.TestServiceServicer):
    def UnaryCall(self, request, context):
        return messages_pb2.SimpleResponse()

    def UnaryCallWithSleep(self, unused_request, unused_context):
        gevent.sleep(LONG_UNARY_CALL_WITH_SLEEP_VALUE)
        return messages_pb2.SimpleResponse()


def start_test_server(port: int = 0) -> Tuple[str, Any]:
    server = grpc.server(futures.ThreadPoolExecutor())
    servicer = TestServiceServicer()
    test_pb2_grpc.add_TestServiceServicer_to_server(
        TestServiceServicer(), server
    )

    server.add_generic_rpc_handlers((_create_extra_generic_handler(servicer),))
    port = server.add_insecure_port("[::]:%d" % port)
    server.start()
    return "localhost:%d" % port, server


def _create_extra_generic_handler(servicer: TestServiceServicer) -> Any:
    # Add programatically extra methods not provided by the proto file
    # that are used during the tests
    rpc_method_handlers = {
        "UnaryCallWithSleep": grpc.unary_unary_rpc_method_handler(
            servicer.UnaryCallWithSleep,
            request_deserializer=messages_pb2.SimpleRequest.FromString,
            response_serializer=messages_pb2.SimpleResponse.SerializeToString,
        )
    }
    return grpc.method_handlers_generic_handler(
        "grpc.testing.TestService", rpc_method_handlers
    )
